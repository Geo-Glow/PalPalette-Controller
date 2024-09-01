#include "GeoGlowTileController.h"

// Constants
const unsigned long PUBLISH_INTERVAL = 30000;
const char* CONFIG_FILE = "/config.json";
const size_t CONFIG_JSON_SIZE = 1024;
const int MDNS_RETRIES = 5; // Number of times to retry mDNS query
const int MDNS_RETRY_DELAY = 2000; // Delay between retries in milliseconds

// Global Variables
WiFiManager wifiManager;
WiFiClient wifiClient;
MQTTClient mqttClient(wifiClient);
NanoleafApiWrapper nanoleaf(wifiClient);
ColorPaletteAdapter colorPaletteAdapter(nanoleaf);

unsigned long lastPublishTime = 0;

char mqttBroker[40];
char mqttPort[6] = "1883";
char nanoleafBaseUrl[55] = "";
char nanoleafAuthToken[33] = "";
char friendId[36] = "";
char deviceId[36] = "";

bool shouldSaveConfig = false;

// Function Definitions
void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void setup() {
    Serial.begin(115200);
    delay(10);

    initializeUUID();
    loadConfigFromFile();
    setupWiFiManager();
    generateMDNSNanoleafURL(); // Always generate MDNS Nanoleaf URL
    setupMQTTClient();
    attemptNanoleafConnection();

    if (shouldSaveConfig) {
        saveConfigToFile();
    }
}

void loop() {
    mqttClient.loop();

    if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
        publishStatus();
        lastPublishTime = millis();
    }
}

void initializeUUID() {
    UUID uuid;
    uint32_t seed1 = random(999999999);
    uint32_t seed2 = random(999999999);

    uuid.seed(seed1, seed2);
    uuid.generate();
    strcpy(deviceId, uuid.toCharArray());
}

void loadConfigFromFile() {
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount FS");
        return;
    }
    
    if (!SPIFFS.exists(CONFIG_FILE)) {
        return;
    }

    File configFile = SPIFFS.open(CONFIG_FILE, "r");
    Serial.println("Reading config file");
    
    size_t size = configFile.size();
    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);

    StaticJsonDocument<CONFIG_JSON_SIZE> jsonConfig;
    DeserializationError error = deserializeJson(jsonConfig, buf.get());

    if (error) {
        Serial.println("Failed to parse JSON config file");
        return;
    }

    strcpy(mqttBroker, jsonConfig["mqttBroker"]);
    strcpy(mqttPort, jsonConfig["mqttPort"]);
    strcpy(nanoleafAuthToken, jsonConfig["nanoleafAuthToken"]);
    strcpy(deviceId, jsonConfig["deviceId"]);
    strcpy(friendId, jsonConfig["friendId"]);

    configFile.close();
    Serial.println("Parsed JSON config");
}

void generateMDNSNanoleafURL() {
    bool success = false;

    for (int attempt = 0; attempt < MDNS_RETRIES; ++attempt) {
        if (MDNS.begin("esp8266")) {
            int n = MDNS.queryService("nanoleafapi", "tcp");

            if (n > 0) {
                IPAddress ip = MDNS.IP(0);
                uint16_t port = MDNS.port(0);

                snprintf(nanoleafBaseUrl, sizeof(nanoleafBaseUrl), "http://%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], port);
                Serial.println("Nanoleaf Base URL: " + String(nanoleafBaseUrl));
                success = true;
                break;
            } else {
                Serial.println("mDNS query returned no results (attempt " + String(attempt + 1) + ")");
            }
        } else {
            Serial.println("Failed to start mDNS (attempt " + String(attempt + 1) + ")");
        }

        delay(MDNS_RETRY_DELAY);
    }

    if (!success) {
        Serial.println("Failed to retrieve Nanoleaf Base URL via mDNS after " + String(MDNS_RETRIES) + " attempts");
    }
}

void saveConfigToFile() {
    if (!shouldSaveConfig) return; // No need to save if no changes

    SPIFFS.begin();
    File configFile = SPIFFS.open(CONFIG_FILE, "w");
    
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }

    StaticJsonDocument<CONFIG_JSON_SIZE> jsonConfig;
    jsonConfig["mqttBroker"] = mqttBroker;
    jsonConfig["mqttPort"] = mqttPort;
    jsonConfig["nanoleafAuthToken"] = nanoleafAuthToken;
    jsonConfig["friendId"] = friendId;
    jsonConfig["deviceId"] = deviceId;

    serializeJson(jsonConfig, configFile);
    configFile.close();
    Serial.println("Config saved successfully");
}

void attemptNanoleafConnection() {
    nanoleaf.setup(nanoleafBaseUrl, nanoleafAuthToken);
    delay(1000);

    while (!nanoleaf.isConnected()) {
        Serial.print("Attempting Nanoleaf connection...");
        Serial.println("Failed");

        String newToken = nanoleaf.generateToken();
        if (!newToken.isEmpty()) {
            newToken.toCharArray(nanoleafAuthToken, sizeof(nanoleafAuthToken));
            nanoleaf.setup(nanoleafBaseUrl, nanoleafAuthToken);
            shouldSaveConfig = true;
        }
        delay(5000);
    }

    Serial.println("Nanoleaf connected");
}

void setupWiFiManager() {
    WiFiManagerParameter customMqttBroker("mqttBroker", "MQTT Broker", mqttBroker, 40);
    WiFiManagerParameter customMqttPort("mqttPort", "MQTT Port", mqttPort, 6);
    WiFiManagerParameter customFriendId("friendId", "Friend ID", friendId, 36);

    wifiManager.setSaveConfigCallback(saveConfigCallback);
    wifiManager.addParameter(&customMqttBroker);
    wifiManager.addParameter(&customMqttPort);
    wifiManager.addParameter(&customFriendId);

    if (!wifiManager.autoConnect("GeoGlow")) {
        Serial.println("Failed to connect and hit timeout");
        delay(3000);
        ESP.restart();
        delay(5000);
    }

    Serial.println("Connected");

    strcpy(mqttBroker, customMqttBroker.getValue());
    strcpy(mqttPort, customMqttPort.getValue());
    strcpy(friendId, customFriendId.getValue());

    if (strlen(friendId) >= sizeof(friendId) - 1) friendId[sizeof(friendId) - 1] = '\0';
    if (strlen(deviceId) >= sizeof(deviceId) - 1) deviceId[sizeof(deviceId) - 1] = '\0';
}

void setupMQTTClient() {
    mqttClient.setup(mqttBroker, String(mqttPort).toInt(), friendId, deviceId);
    mqttClient.addTopicAdapter(&colorPaletteAdapter);

    nanoleaf.setPower(true);
    delay(1500);
    nanoleaf.setPower(false);
}

void publishStatus() {
    StaticJsonDocument<200> jsonPayload;
    jsonPayload["friendId"] = friendId;
    jsonPayload["deviceId"] = deviceId;
    JsonArray panelIds = jsonPayload.createNestedArray("panelIds");

    for (const String &panelId: nanoleaf.getPanelIds()) {
        panelIds.add(panelId);
    }

    mqttClient.publish("GeoGlow/Friend-Service/ping", jsonPayload);
}