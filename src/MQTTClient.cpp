#include <ArduinoJson.h>
#include "MQTTClient.h"

const char *MQTTClient::FIRMWARE_VERSION = "1.16";

MQTTClient::MQTTClient(WiFiClient &wifiClient)
    : client(wifiClient)
{
    client.setBufferSize(MQTT_BUFFER_SIZE);
    memset(friendId, 0, sizeof(friendId));
}

bool MQTTClient::setup(const char *mqttBroker, uint16_t mqttPort, const char *friendId)
{
    // Validate inputs
    if (!mqttBroker || !friendId)
    {
        Serial.println("[MQTT] Error: Invalid broker or friendId");
        return false;
    }

    strncpy(this->friendId, friendId, sizeof(this->friendId) - 1);
    this->friendId[sizeof(this->friendId) - 1] = '\0';

    // Configure MQTT client
    client.setServer(mqttBroker, mqttPort);
    client.setCallback([this](char *topic, byte *payload, unsigned int length)
                       { this->callback(topic, payload, length); });

    // Prepare Last Will & Testament (LWT)
    char willTopic[64];
    snprintf(willTopic, sizeof(willTopic), "GeoGlow/%s/status", this->friendId);

    JsonDocument willMsg;
    willMsg["device"] = this->friendId;
    willMsg["status"] = "unexpected_disconnect";
    willMsg["version"] = FIRMWARE_VERSION;

    char willPayload[128];
    serializeJson(willMsg, willPayload);

    // Generate client ID with device identifier
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "GeoGlow-%.12s", this->friendId);

    // Attempt connection with LWT
    bool connected = client.connect(
        clientId,   // Client ID
        nullptr,    // Username
        nullptr,    // Password
        willTopic,  // LWT Topic
        1,          // QoS 1
        true,       // Retain LWT
        willPayload // LWT Message
    );

    if (connected)
    {
        // Connection successful
        Serial.printf("[MQTT] Connected to %s as %s\n", mqttBroker, clientId);

        // Subscribe to all registered topics
        if (!subscribeToAdapterTopics())
        {
            Serial.println("[MQTT] Warning: Partial subscription failures");
        }

        return true;
    }

    Serial.printf("[MQTT] Connection failed (rc=%d)\n", client.state());
    return false;
}

void MQTTClient::loop()
{
    if (!client.connected())
    {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= reconnectInterval)
        {
            lastReconnectAttempt = now;
            reconnect();
        }
    }
    client.loop();
}

bool MQTTClient::reconnect()
{
    if (client.connected())
        return true;

    Serial.print("Attempting MQTT connection...");
    char clientId[32];
    snprintf(clientId, sizeof(clientId), "GeoGlow-%.12s", friendId);

    if (client.connect(clientId))
    {
        Serial.println("connected");
        return subscribeToAdapterTopics();
    }

    Serial.printf("failed, rc=%d\n", client.state());
    return false;
}

bool MQTTClient::subscribeToAdapterTopics()
{
    bool allSuccess = true;
    for (const auto &adapter : topicAdapters)
    {
        String topic = buildTopic(adapter.get());
        if (!client.subscribe(topic.c_str()))
        {
            Serial.printf("Failed to subscribe to topic: %s\n", topic.c_str());
            allSuccess = false;
        }
    }
    return allSuccess;
}

bool MQTTClient::publish(const char *topic, const JsonDocument &jsonPayload)
{
    if (!client.connected())
    {
        Serial.println("MQTT not connected");
        return false;
    }

    char buffer[JSON_BUFFER_SIZE];
    size_t len = serializeJson(jsonPayload, buffer);
    return client.publish(topic, buffer, len);
}

bool MQTTClient::addTopicAdapter(std::unique_ptr<TopicAdapter> adapter)
{
    topicAdapters.push_back(std::move(adapter));
    if (client.connected())
    {
        String topic = buildTopic(topicAdapters.back().get());
        return client.subscribe(topic.c_str());
    }
    return false;
}

bool MQTTClient::publishStatusUpdate(const char *statusType, const char *message)
{
    JsonDocument doc;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["friendId"] = friendId;
    doc[statusType] = message;
    return publish("GeoGlow/status/update", doc);
}

bool MQTTClient::publishErrorMessage(const char *errorMessage)
{
    JsonDocument doc;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["friendId"] = friendId;
    doc["error"] = errorMessage;
    return publish("GeoGlow/status/error", doc);
}

String MQTTClient::buildTopic(const TopicAdapter *adapter) const
{
    return String("GeoGlow/") + friendId + "/" + adapter->getTopic();
}

bool MQTTClient::matches(const String &subscribedTopic, const String &receivedTopic) const
{
    if (subscribedTopic == receivedTopic)
        return true;
    if (subscribedTopic.endsWith("#"))
    {
        return receivedTopic.startsWith(subscribedTopic.substring(0, subscribedTopic.length() - 1));
    }

    int wildcardPos = subscribedTopic.indexOf('+');
    if (wildcardPos >= 0)
    {
        return receivedTopic.startsWith(subscribedTopic.substring(0, wildcardPos)) &&
               receivedTopic.endsWith(subscribedTopic.substring(wildcardPos + 1));
    }
    return false;
}

void MQTTClient::callback(char *topic, byte *payload, unsigned int length)
{
    // Prevent buffer overflow
    if (length >= JSON_BUFFER_SIZE)
    {
        publishErrorMessage("Message too large");
        return;
    }

    char payloadBuffer[JSON_BUFFER_SIZE];
    memcpy(payloadBuffer, payload, length);
    payloadBuffer[length] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, payloadBuffer))
    {
        publishErrorMessage("JSON deserialization failed");
        return;
    }

    String receivedTopic(topic);
    for (const auto &adapter : topicAdapters)
    {
        if (matches(buildTopic(adapter.get()), receivedTopic))
        {
            adapter->callback(topic, doc.as<JsonObject>(), length);
            return;
        }
    }

    Serial.printf("Unhandled topic: %s\n", topic);
    publishErrorMessage("Unhandled topic received");
}

bool MQTTClient::isConnected()
{
    return client.connected();
}