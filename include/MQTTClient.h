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
    explicit MQTTClient(WiFiClient &wifiClient);

    void setup(const char *mqttBroker, int mqttPort, const char *friendId);

    void loop();

    void publish(const char *topic, const JsonDocument &jsonPayload);

    void addTopicAdapter(std::unique_ptr<TopicAdapter> adapter);

    void publishStatusUpdate(const char *statusType, const char *message);
    void publishErrorMessage(const char *errorMessage);

    bool isConnected();

private:
    void reconnect();

    static constexpr size_t MQTT_BUFFER_SIZE = 2048;
    static constexpr size_t JSON_BUFFER_SIZE = 512;

    String buildTopic(const TopicAdapter *adapter) const;

    static void staticCallback(char *topic, byte *payload, unsigned int length);

    void callback(char *topic, byte *payload, unsigned int length);

    bool matches(const String &subscribedTopic, const String &receivedTopic) const;

    PubSubClient client;
    String friendId;
    std::vector<std::unique_ptr<TopicAdapter>> topicAdapters;
};

#endif // MQTTCLIENT_H