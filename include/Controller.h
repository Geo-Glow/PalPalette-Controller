#ifndef CONTROLLER_H
#define CONTROLLER_H
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#else
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#endif

#include <DNSServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

// Custom Modules
#include "MQTTClient.h"
#include "NanoleafApiWrapper.h"
#include "ColorPaletteAdapter.h"
#include "FileSystemHandler.h"

// Constants
const unsigned long PUBLISH_INTERVAL = 30000;
const char *CONFIG_FILE = "/config.json";
const size_t CONFIG_JSON_SIZE = 1024;
const char *API_URL_PREFIX = "http://139.6.56.197/friends/";

// WIFI Constants
const int WIFI_MAX_ATTEMPTS = 10;      // Maximum Wifi connection attempts
const int WIFI_RETRY_DELAY = 500;      // Delay between each attempt
const int WIFI_CONNECT_TIMEOUT = 5000; // Maximum time to wait for the connection (in ms)

// MDNS Constants
const int MDNS_MAX_RETRIES = 10;           // Maximum MDNS Retries
const int MDNS_INITIAL_RETRY_DELAY = 1000; // Initial delay (in ms)
const float MDNS_BACKOFF_FACTOR = 1.5;     // Backoff factor for exponential delay

// MQTT Constants
const char *DEFAULT_MQTT_BROKER = "hivemq.dock.moxd.io"; // MQTT Broker Address
const int DEFAULT_MQTT_PORT = 1883;                      // MQTT Broker Port

// Function Prototypes
void initializeUUID();
void loadConfigFromFile();
void saveConfigToFile();
bool generateMDNSNanoleafURL();
void attemptNanoleafConnection();
void setupWiFiManager();
void setupMQTTClient();
void publishStatus();
void saveConfigCallback();
void connectToWifi();
void generateShortUUID(char *uuid, size_t length);
void connectToWifi(bool useSavedCredentials = true);
void registerNanoleafEvents();
void performReset();
bool resetBtnLongPress();
void IRAM_ATTR handleResetInterrupt();
#endif // CONTROLLER_H