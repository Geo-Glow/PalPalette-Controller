#include <ArduinoJson.h>
#include "MQTTClient.h"

const char *FIRMWARE_VERSION = "1.15";

MQTTClient::MQTTClient(WiFiClient &wifiClient)
    : client(wifiClient)
{
}

void MQTTClient::setup(const char *mqttBroker, const int mqttPort, const char *friendId)
{
    client.setServer(mqttBroker, mqttPort);
    this->friendId = friendId;
    client.setCallback(staticCallback);
    client.setBufferSize(MQTT_BUFFER_SIZE);
    client.setCallback([this](char *topic, byte *payload, unsigned int length)
                       { this->callback(topic, payload, length); });
    loop();
}

void MQTTClient::staticCallback(char *topic, byte *payload, unsigned int length)
{
}

void MQTTClient::loop()
{
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
}

void MQTTClient::publishStatusUpdate(const char *statusType, const char *message)
{
    JsonDocument jsonDoc;
    jsonDoc["firmwareVersion"] = FIRMWARE_VERSION;
    jsonDoc["friendId"] = this->friendId;
    jsonDoc[statusType] = message;

    publish("GeoGlow/status/update", jsonDoc);
}

void MQTTClient::publishErrorMessage(const char *errorMessage)
{
    JsonDocument jsonDoc;
    jsonDoc["firmwareVersion"] = FIRMWARE_VERSION;
    jsonDoc["friendId"] = friendId;
    jsonDoc["error"] = errorMessage;

    publish("GeoGlow/status/error", jsonDoc);
}

void MQTTClient::reconnect()
{
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        String mqttClientId = "GeoGlow-" + this->friendId;
        if (client.connect(mqttClientId.c_str()))
        {
            Serial.println("connected: " + mqttClientId);
            for (const auto &adapter : topicAdapters)
            {
                if (client.subscribe(buildTopic(adapter.get()).c_str()))
                {
                    Serial.println("Subscribed to topic: " + buildTopic(adapter.get()));
                }
                else
                {
                    Serial.println("Failed to subscribe to topic: " + buildTopic(adapter.get()));
                }
            }
        }
        else
        {
            Serial.print("Failed to connect, return code: ");
            Serial.print(client.state());
            Serial.println("Retrying again in 5 seconds");
            delay(5000);
        }
    }
}

void MQTTClient::publish(const char *topic, const JsonDocument &jsonPayload)
{
    if (client.connected())
    {
        char buffer[JSON_BUFFER_SIZE];
        size_t n = serializeJson(jsonPayload, buffer);
        client.publish(topic, buffer, n);
    }
    else
    {
        Serial.println("MQTT client not connected. Unable to publish message.");
        publishErrorMessage("MQTT client not connected during publish.");
    }
}

void MQTTClient::addTopicAdapter(std::unique_ptr<TopicAdapter> adapter)
{
    if (client.connected())
    {
        client.subscribe(buildTopic(adapter.get()).c_str());
    }
    topicAdapters.push_back(std::move(adapter));
}

String MQTTClient::buildTopic(const TopicAdapter *adapter) const
{
    return "GeoGlow/" + friendId + "/" + adapter->getTopic();
}

bool MQTTClient::matches(const String &subscribedTopic, const String &receivedTopic) const
{
    if (subscribedTopic.endsWith("#"))
    {
        String baseTopic = subscribedTopic.substring(0, subscribedTopic.length() - 1);
        return receivedTopic.startsWith(baseTopic);
    }
    else if (subscribedTopic.indexOf('+') >= 0)
    {
        int plusPos = subscribedTopic.indexOf('+');
        String preWildcard = subscribedTopic.substring(0, plusPos);
        String postWildcard = subscribedTopic.substring(plusPos + 1);
        if (receivedTopic.startsWith(preWildcard) && receivedTopic.endsWith(postWildcard))
        {
            return true;
        }
    }
    return subscribedTopic == receivedTopic;
}

void MQTTClient::callback(char *topic, byte *payload, unsigned int length)
{
    char payloadBuffer[length + 1];
    memcpy(payloadBuffer, payload, length);
    payloadBuffer[length] = '\0';

    JsonDocument jsonDocument;

    DeserializationError error = deserializeJson(jsonDocument, payloadBuffer);
    if (error)
    {
        Serial.print("JSON Deserialization failed: ");
        Serial.println(error.c_str());
        Serial.print("Payload: ");
        Serial.println(payloadBuffer);
        publishErrorMessage("JSON Deserialization failed.");
        return;
    }

    String receivedTopic = String(topic);
    for (const auto &adapter : topicAdapters)
    {
        if (matches(buildTopic(adapter.get()), receivedTopic))
        {
            adapter->callback(topic, jsonDocument.as<JsonObject>(), length);
            return;
        }
    }

    Serial.print("Unhandled message [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(payloadBuffer);
    publishErrorMessage("Unhandled MQTT message.");
}

bool MQTTClient::isConnected()
{
    return client.connected();
}