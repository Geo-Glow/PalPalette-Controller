#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

#include <PubSubClient.h>
#include <vector>
#include <memory>
#include <ArduinoJson.h>
#include "TopicAdapter.h"

class MQTTClient
{
public:
    static constexpr size_t MQTT_BUFFER_SIZE = 2048;
    static constexpr size_t JSON_BUFFER_SIZE = 512;
    static const char *FIRMWARE_VERSION;

    explicit MQTTClient(WiFiClient &wifiClient);

    bool setup(const char *mqttBroker, uint16_t mqttPort, const char *friendId);
    void loop();
    bool publish(const char *topic, const JsonDocument &jsonPayload);
    bool addTopicAdapter(std::unique_ptr<TopicAdapter> adapter);
    bool publishStatusUpdate(const char *statusType, const char *message);
    bool publishErrorMessage(const char *errorMessage);
    bool isConnected();

private:
    bool reconnect();
    String buildTopic(const TopicAdapter *adapter) const;
    bool subscribeToAdapterTopics();
    void callback(char *topic, byte *payload, unsigned int length);
    bool matches(const String &subscribedTopic, const String &receivedTopic) const;

    PubSubClient client;
    char friendId[32];
    std::vector<std::unique_ptr<TopicAdapter>> topicAdapters;
    unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 5000;
};

#endif // MQTTCLIENT_H