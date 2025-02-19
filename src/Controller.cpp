#include "Controller.h"

// Global Variables
WiFiManager wifiManager;
WiFiClient wifiClientForMQTT;
WiFiClient wifiClientForHTTP;
HTTPClient httpClient;
MQTTClient mqttClient(wifiClientForMQTT);
NanoleafApiWrapper nanoleaf(wifiClientForMQTT);
ColorPaletteAdapter colorPaletteAdapter(nanoleaf);

// Wi-Fi credentials
char ssid[32];     // Wifi SSID
char password[64]; // Wifi Password

// NanoLeaf Vars
char nanoleafBaseUrl[55] = "";   // NanoLeaf Baseurl (http://<ip>:<port>)
char nanoleafAuthToken[33] = ""; // Nanoleaf Auth Token

// Setup Vars
char friendId[36] = "";
char name[36] = "";
char groupId[36] = "";

// Flags and Timers
unsigned long lastPublishTime = 0;
bool shouldSaveConfig = false;
bool layoutChanged = false;
bool initialSetupDone = false;
unsigned long lastColorTime = 0;
bool currentlyShowingCustomColor = false;

// Reset Logic
#define RESET_BTN_PIN 0      // Flash Button Pin
#define LONG_PRESS_TIME 3000 // Milliseconds (3 sec)
volatile unsigned long buttonPressStartTime = 0;

// Layout Publish Mode
#define ONEVENTS 0                // Layout will be published using events (only when layout changes)
#define ONHEARTBEAT 1             // Layout will be published each heartbeat (fallback if events are not working)
int publishLayoutMode = ONEVENTS; // ONEVENTS should be the default, ONHEARTBEAT is just the fallback

#if defined(ESP32)
#define LED_BUILTIN 2
#endif

void performReset()
{
    wifiManager.erase();
    FileSystemHandler::removeConfigFile(CONFIG_FILE);
    ESP.restart();
}

// Interrupt Handler - This way the reset can be done no matter what the controller is executing currently
void IRAM_ATTR handleResetInterrupt()
{
    if (digitalRead(RESET_BTN_PIN) == LOW)
    {
        buttonPressStartTime = millis();
    }
    else
    {
        if (millis() - buttonPressStartTime >= LONG_PRESS_TIME)
        {
            performReset();
        }
    }
}

void connectToWifi(bool useSavedCredentials)
{
    if (!useSavedCredentials || strlen(ssid) == 0 || strlen(password) == 0)
    {
        setupWiFiManager();
        return;
    }

    Serial.println("Connecting to Wi-Fi using saved credentials...");
    WiFi.begin(ssid, password);

    unsigned long startAttemptTime = millis();
    int attempts = 0;

    while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < WIFI_CONNECT_TIMEOUT && attempts < WIFI_MAX_ATTEMPTS)
    {
        delay(WIFI_RETRY_DELAY);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nConnected to Wi-Fi");
    }
    else
    {
        Serial.println("\nFailed to connect to Wi-Fi using saved credentials. Launching WiFi Manager...");
        setupWiFiManager();
    }
}

void setupWiFiManager()
{
    wifiManager.setDebugOutput(true); // Disable the debug output to keep Serial clean
    wifiManager.setTitle("PalPalette Controller Konfigurator");
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    WiFiManagerParameter customGroupId("groupId", "Gruppen ID", groupId, 36);
    WiFiManagerParameter customName("name", "Name", name, 36);
    WiFiManagerParameter customFriendId("friendId", "Freundes ID", friendId, 36);

    wifiManager.addParameter(&customGroupId);
    wifiManager.addParameter(&customName);
    wifiManager.addParameter(&customFriendId);

    if (!wifiManager.autoConnect("PalPalette"))
    {
        Serial.println("Failed to connect via WiFi Manager and hit timeout");
        delay(3000);
        ESP.restart();
    }

    Serial.println("WiFi Manager has established a connection.");

    strncpy(ssid, WiFi.SSID().c_str(), sizeof(ssid) - 1);
    strncpy(password, WiFi.psk().c_str(), sizeof(password) - 1);
    strncpy(groupId, customGroupId.getValue(), sizeof(groupId) - 1);
    strncpy(name, customName.getValue(), sizeof(name) - 1);
    strncpy(friendId, customFriendId.getValue(), sizeof(friendId) - 1);

    // Checks to prevent buffer overflow
    if (strlen(groupId) >= sizeof(groupId) - 1)
        groupId[sizeof(groupId) - 1] = '\0';
    if (strlen(name) >= sizeof(name) - 1)
        name[sizeof(name) - 1] = '\0';
}

// Function Definitions
void saveConfigCallback()
{
    Serial.println("Config should be saved.");
    shouldSaveConfig = true;
}

void generateShortUUID(char *uuid, size_t length)
{
    const char *hexChars = "0123456789ABCDEF";
    for (size_t i = 0; i < length - 1; ++i)
    {
        uuid[i] = hexChars[random(16)];
    }
    uuid[length - 1] = '\0';
}

bool generateMDNSNanoleafURL()
{
    if (MDNS.begin("esp8266"))
    {
        Serial.println("MDNS wurde gestartet.");

        int retryCount = 0;
        int retryDelay = MDNS_INITIAL_RETRY_DELAY;

        while (retryCount < MDNS_MAX_RETRIES)
        {
            Serial.printf("Versuch %d den Nanoleaf Service zu finden...\n", retryCount + 1);
            int n = MDNS.queryService("nanoleafapi", "tcp");

            if (n > 0)
            {
                String ip = MDNS.IP(0).toString();
                int port = MDNS.port(0);

                snprintf(nanoleafBaseUrl, sizeof(nanoleafBaseUrl), "http://%s:%d", ip.c_str(), port);
                Serial.printf("Nanoleaf Service wurde gefunden: %s\n", nanoleafBaseUrl);
                saveConfigToFile();
                return true;
            }
            else
            {
                Serial.println("Es wurde kein Nanoleaf Service gefunden. Neuer Versuch...");
                retryCount++;
                delay(retryDelay);

                // Exponential Backoff
                retryDelay = static_cast<int>(retryDelay * MDNS_BACKOFF_FACTOR);
                if (retryDelay > MDNS_INITIAL_RETRY_DELAY * 10)
                {
                    retryDelay = MDNS_INITIAL_RETRY_DELAY * 10; // Cap the delay to a max of a factor of 10
                }
            }
        }

        // If we exit the loop without finding a service
        Serial.println("Es konnte kein Nanoleaf Service nach der maximalen Anzahl an Versuchen gefunden werden.");
    }
    else
    {
        Serial.println("Fehler beim Starten von MNDS.");
    }
    return false;
}

void loadConfigFromFile()
{
    JsonDocument jsonConfig;
    if (!FileSystemHandler::loadConfigFromFile(CONFIG_FILE, jsonConfig, CONFIG_JSON_SIZE))
    {
        return;
    }

    strncpy(ssid, jsonConfig["ssid"], sizeof(ssid) - 1);
    strncpy(password, jsonConfig["password"], sizeof(password) - 1);
    strncpy(nanoleafAuthToken, jsonConfig["nanoleafAuthToken"], sizeof(nanoleafAuthToken) - 1);
    strncpy(name, jsonConfig["name"], sizeof(name) - 1);
    strncpy(groupId, jsonConfig["groupId"], sizeof(groupId) - 1);
    strncpy(nanoleafBaseUrl, jsonConfig["nanoleafBaseUrl"], sizeof(nanoleafBaseUrl) - 1);
    strncpy(friendId, jsonConfig["friendId"], sizeof(friendId) - 1);
    initialSetupDone = jsonConfig["setupDone"];

    Serial.println("Parsed JSON config");
}

void saveConfigToFile()
{
    JsonDocument jsonConfig;
    jsonConfig["ssid"] = ssid;
    jsonConfig["password"] = password;
    jsonConfig["nanoleafAuthToken"] = nanoleafAuthToken;
    jsonConfig["friendId"] = friendId;
    jsonConfig["name"] = name;
    jsonConfig["nanoleafBaseUrl"] = nanoleafBaseUrl;
    jsonConfig["groupId"] = groupId;
    jsonConfig["setupDone"] = initialSetupDone;
    shouldSaveConfig = false;

    if (!FileSystemHandler::saveConfigToFile(CONFIG_FILE, jsonConfig))
    {
        Serial.println("Error");
    }
}

void generateNanoleafToken()
{
    String newToken = nanoleaf.generateToken();
    if (!newToken.isEmpty())
    {
        newToken.toCharArray(nanoleafAuthToken, sizeof(nanoleafAuthToken));
        nanoleaf.setup(nanoleafBaseUrl, nanoleafAuthToken);
    }
}

void attemptNanoleafConnection()
{
    nanoleaf.setup(nanoleafBaseUrl, nanoleafAuthToken);
    int attempts = 0;
    const int maxAttempts = 5;

    while (!nanoleaf.isConnected() && attempts < maxAttempts)
    {
        Serial.printf("Attempting Nanoleaf connection... (%d/%d)\n", attempts + 1, maxAttempts);
        delay(6000);

        if (!nanoleaf.isConnected())
        {
            generateNanoleafToken();
        }
        attempts++;
    }

    if (nanoleaf.isConnected())
    {
        Serial.println("Nanoleaf connected");
        registerNanoleafEvents();
    }
    else
    {
        Serial.println("Failed to connect to Nanoleaf with saved baseURL, reattempting MDNS lookup.");
        generateMDNSNanoleafURL();
        attemptNanoleafConnection();
    }
}

void setupMQTTClient()
{
    mqttClient.setup(DEFAULT_MQTT_BROKER, DEFAULT_MQTT_PORT, friendId);
    auto colorAdapter = std::make_unique<ColorPaletteAdapter>(nanoleaf);
    mqttClient.addTopicAdapter(std::move(colorAdapter));
}

void publishHeartbeat()
{
    if (!nanoleaf.isConnected())
    {
        Serial.println("Lost connection to nanoleafs. Trying to reconnect.");
        attemptNanoleafConnection();

        if (nanoleaf.isConnected())
        {
            Serial.println("Reconnecting worked! Continuing as before.");
        }
        else
        {
            Serial.println("Connection failed. Restarting ESP");
            ESP.restart();
        }
    }

    String url = String(API_URL_PREFIX) + friendId + "/heartbeat";
    httpClient.begin(wifiClientForHTTP, url);

    int httpResponseCode = httpClient.POST("{}");
    String responseMsg = httpClient.errorToString(httpResponseCode).c_str();
    switch (httpResponseCode)
    {
    case 204:
        Serial.println("Heartbeat posted");
        break;
    case 404:
        publishStatus();
        break;
    case 500:
        Serial.printf("Failed posting heartbeat: %s\n", responseMsg.c_str());
        break;
    default:
        Serial.printf("Unknown error occured: %s\n", responseMsg.c_str());
        break;
    }
    httpClient.end();
}

void publishStatus()
{
    JsonDocument jsonPayload;
    jsonPayload["friendId"] = friendId;
    jsonPayload["name"] = name;
    jsonPayload["groupId"] = groupId;
    JsonArray tileIds = jsonPayload["tileIds"].to<JsonArray>();

    for (const String &panelId : nanoleaf.getPanelIds())
    {
        tileIds.add(panelId);
    }

    char buffer[512];
    size_t n = serializeJson(jsonPayload, buffer);

    String url = String(API_URL_PREFIX) + friendId;
    httpClient.begin(wifiClientForHTTP, url);
    httpClient.addHeader("Content-Type", "application/json");

    int httpResponseCode = httpClient.sendRequest("PATCH", (uint8_t *)buffer, n);

    if (httpResponseCode == 201 || httpResponseCode == 204)
    {
        Serial.printf("PATCH successfull, response code: %d\n", httpResponseCode);
    }
    else
    {
        Serial.printf("Error occured while making PATCH request: %s\n", httpClient.errorToString(httpResponseCode).c_str());
    }

    httpClient.end();
}

bool ensureNanoleafURL()
{
    if (strlen(nanoleafBaseUrl) == 0)
    {
        bool success = generateMDNSNanoleafURL();
        if (success)
        {
            Serial.println("NanoLeaf URL wurde gefunden.");
            nanoleaf.setup(nanoleafBaseUrl, nanoleafAuthToken);
            return true;
        }
        else
        {
            Serial.println("Nanoleaf URL konnte nicht gefunden werden.");
            return false;
        }
    }
    else
        return true;
}

void registerNanoleafEvents()
{
    std::vector<int> eventIds = {2};
    bool success = false;
    const int maxRetries = 5;

    for (int attempt = 1; attempt <= maxRetries; attempt++)
    {
        success = nanoleaf.registerEvents(eventIds);
        if (success)
        {
            nanoleaf.setLayoutChangeCallback([]()
                                             { layoutChanged = true; });
            break;
        }
        else
        {
            Serial.printf("Event registration failed, attempt %d/%d\n", attempt, maxRetries);
            delay(1000);
        }
    }

    if (!success)
    {
        Serial.println("Event registration failed after maximum retries. Setting publishLayoutMode to ONHEARTBEAT.");
        publishLayoutMode = ONHEARTBEAT;
        return;
    }

    Serial.println("Nanoleaf events registered.");
}

void publishInitialHeartbeat()
{
    publishHeartbeat();
    Serial.println("Initial Heartbeat published.");

    if (shouldSaveConfig)
    {
        saveConfigToFile();
        Serial.println("Configuration saved.");
    }
}

void blink_led(int blinkDelay)
{
    digitalWrite(LED_BUILTIN, LOW);
    delay(blinkDelay);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(blinkDelay);
}

void initialSetup()
{
    bool success = false;

    Serial.println("Captive Portal wird aufgesetzt.");
    setupWiFiManager();

    Serial.println("Nanoleaf MDNS Lookup");
    unsigned long now = millis();
    unsigned long then = millis();
    // While not successfull and not longer ago than 60 Seconds
    while (!success)
    {
        success = ensureNanoleafURL();
        then = millis();

        // If still not successfull after 60 seconds
        // blink led slow to indicate problem
        if (then - now > (60 * 1000))
        {
            for (int i = 0; i < 20; i++)
            {
                blink_led(2000);
            }
            now = millis();
            then = millis();
        }
    }

    /*Serial.println("MQTT Verbindung wird aufgebaut...");
    setupMQTTClient();*/

    Serial.println("Generating Auth token");
    while (!nanoleaf.isConnected())
    {
        // Blink LED fast while generating the token
        blink_led(500);
        generateNanoleafToken();
    }

    initialSetupDone = true;
    digitalWrite(LED_BUILTIN, HIGH);
    saveConfigToFile();

    const int red[] = {255, 0, 0};
    nanoleaf.setStaticColor(red);
    Serial.println("Ersteinrichtung abgeschlossen. Der ESP wird neu gestartet...");
    ESP.restart();
}

void colorCallback()
{
    lastColorTime = millis();
    currentlyShowingCustomColor = true;
}

void setup()
{
    Serial.begin(115200);
    // Initialize onboard led and turn off
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    // Set Pin Mode for Reset button
    pinMode(RESET_BTN_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(RESET_BTN_PIN), handleResetInterrupt, CHANGE);

    loadConfigFromFile();

    if (!initialSetupDone)
    {
        initialSetup();
    }

    // loadConfigFromFile();
    connectToWifi(true);

    setupMQTTClient();
    mqttClient.publishStatusUpdate("mqttStatus", mqttClient.isConnected() ? "Connected" : "Disconnected");
    mqttClient.publishStatusUpdate("wifiStatus", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

    ensureNanoleafURL();
    attemptNanoleafConnection();
    nanoleaf.setColorCallback(colorCallback);
    publishStatus();
    publishInitialHeartbeat();
}

void loop()
{
    mqttClient.loop();
    nanoleaf.processEvents();
    unsigned long now = millis();

    if (now - lastColorTime >= 360000 && currentlyShowingCustomColor)
    {
        nanoleaf.setPower(false);
        currentlyShowingCustomColor = false;
    }

    if (layoutChanged)
    {
        publishStatus();
        layoutChanged = false;
    }

    if (now - lastPublishTime >= PUBLISH_INTERVAL)
    {
        // If publish mode is on heartbeat, publish layout every heartbeat
        if (publishLayoutMode == ONHEARTBEAT)
        {
            publishStatus();
        }
        else // just publish the heartbeat itself
        {
            publishHeartbeat();
        }

        lastPublishTime = now;
    }
}
