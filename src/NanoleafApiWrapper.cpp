#include "NanoleafApiWrapper.h"

NanoleafApiWrapper::NanoleafApiWrapper(const WiFiClient &wifiClient)
    : client(wifiClient)
{
}

void NanoleafApiWrapper::setup(const char *nanoleafBaseUrl, const char *nanoleafAuthToken)
{
    this->nanoleafBaseUrl = nanoleafBaseUrl;
    this->nanoleafAuthToken = nanoleafAuthToken;
}

bool NanoleafApiWrapper::sendRequest(const String &method, const String &endpoint, const JsonDocument *requestBody,
                                     JsonDocument *responseBody, const bool useAuthToken)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi Disconnected");
        return false;
    }
    HTTPClient http;
    String url = nanoleafBaseUrl;

    if (useAuthToken)
    {
        url += "/api/v1/" + nanoleafAuthToken;
    }
    else
    {
        url += "/api/v1";
    }

    url += endpoint;

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = -1;

    if (method.equalsIgnoreCase("GET"))
    {
        httpResponseCode = http.GET();
    }
    else if (method.equalsIgnoreCase("POST"))
    {
        if (requestBody != nullptr)
        {
            String stringPayload;
            serializeJson(*requestBody, stringPayload);
            httpResponseCode = http.POST(stringPayload);
        }
        else
        {
            httpResponseCode = http.POST("");
        }
    }
    else if (method.equalsIgnoreCase("PUT"))
    {
        if (requestBody != nullptr)
        {
            String stringPayload;
            serializeJson(*requestBody, stringPayload);
            httpResponseCode = http.PUT(stringPayload);
        }
        else
        {
            httpResponseCode = http.PUT("");
        }
    }

    if (httpResponseCode > 0)
    {
        String response = http.getString();
        if (responseBody != nullptr)
        {
            deserializeJson(*responseBody, response);
        }

        http.end();
        return true;
    }
    Serial.print("Error on sending ");
    Serial.print(method);
    Serial.print(": ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
}

bool NanoleafApiWrapper::isConnected()
{
    JsonDocument jsonResponse;
    if (sendRequest("GET", "/", nullptr, &jsonResponse, true))
    {
        if (jsonResponse["serialNo"].is<const char *>())
        {
            return true;
        }
    }
    return false;
}

String NanoleafApiWrapper::generateToken()
{
    JsonDocument jsonResponse;
    if (sendRequest("POST", "/new", nullptr, &jsonResponse, false))
    {
        const String strPayload = jsonResponse["auth_token"];
        if (strPayload != nullptr && strPayload != "null")
        {
            return strPayload;
        }
    }
    return "";
}

bool NanoleafApiWrapper::registerEvents(const std::vector<int> &eventIds)
{
    String eventIdString = "";
    for (size_t i = 0; i < eventIds.size(); i++)
    {
        eventIdString += String(eventIds[i]);
        if (i < eventIds.size() - 1)
        {
            eventIdString += ",";
        }
    }

    String url = "/events?id=" + eventIdString;
    if (WiFi.status() == WL_CONNECTED)
    {
        String fullUrl = this->nanoleafBaseUrl + "/api/v1/" + this->nanoleafAuthToken + url;
        httpClient.begin(client, fullUrl);
        httpClient.addHeader("Content-Type", "text/event-stream");
        int httpResponseCode = httpClient.GET();
        if (httpResponseCode == HTTP_CODE_OK)
        {
            Serial.println("Listening for events...");
            eventClient = httpClient.getStreamPtr();
            registeredForEvents = true;
            return true;
        }
        else
        {
            Serial.printf("GET request failed, error: %s\n", httpClient.errorToString(httpResponseCode).c_str());
            httpClient.end();
            return false;
        }
    }
    return false;
}

void NanoleafApiWrapper::processEvents()
{
    if (registeredForEvents && eventClient && eventClient->available())
    {
        String line = eventClient->readStringUntil('\n');
        String eventId = line.substring(4);
        if (eventId == "2")
        {
            if (this->layoutChangeCallback)
            {
                this->layoutChangeCallback();
            }
        }
    }
}

void NanoleafApiWrapper::setLayoutChangeCallback(LayoutChangeCallback callback)
{
    this->layoutChangeCallback = callback;
}

void NanoleafApiWrapper::setColorCallback(ColorCallback callback)
{
    this->colorCallback = callback;
}

bool NanoleafApiWrapper::identify()
{
    return sendRequest("PUT", "/identify", nullptr, nullptr, true);
}

std::vector<String> NanoleafApiWrapper::getPanelIds()
{
    JsonDocument jsonResponse;
    std::vector<String> panelIds;
    this->triangleIds.clear();

    if (!sendRequest("GET", "/panelLayout/layout", nullptr, &jsonResponse, true))
    {
        return panelIds; // Return empty if request failed
    }

    // Check if positionData exists and is an array
    if (!jsonResponse["positionData"].is<JsonArray>())
    {
        Serial.println("positionData is not an array or does not exist.");
        return panelIds;
    }

    JsonArray positionData = jsonResponse["positionData"].as<JsonArray>();
    for (JsonObject panel : positionData)
    {
        if (!panel["panelId"].is<int>() || !panel["shapeType"].is<int>())
        {
            continue; // Skip invalid entries
        }

        String panelId = panel["panelId"].as<String>();
        String shapeType = panel["shapeType"].as<String>();

        if (panelId != "0")
        {
            if (shapeType == "9")
            {
                triangleIds.push_back(panelId);
            }
            else
            {
                panelIds.push_back(panelId);
            }
        }
    }

    return panelIds;
}

bool NanoleafApiWrapper::setPower(const bool &state)
{
    JsonDocument jsonPayload;
    jsonPayload["on"] = JsonObject();
    jsonPayload["on"]["value"] = state;
    return sendRequest("PUT", "/state", &jsonPayload, nullptr, true);
}

void NanoleafApiWrapper::setStaticColor(const int rgb[3])
{
    const char *jsonPayload = R"(
        {
            "write": {
                "command": "display",
                "animType": "solid",
                "palette": [
                    {
                        "hue": 0,
                        "saturation": 100,
                        "brightness": 100
                    }
                ],
                "colorType": "HSB"
            }
        }
    )";
    JsonDocument payload;
    DeserializationError error = deserializeJson(payload, jsonPayload);
    if (error)
    {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    for (int i = 0; i < 3; i++)
    {
        sendRequest("PUT", "/effects", &payload, nullptr, true);
        delay(1000);
        this->setPower(false);
        delay(1000);
    }
}

bool NanoleafApiWrapper::setStaticColors(const JsonObject &doc)
{
    String animData = "";
    const unsigned int tileCount = doc.size() - 1 + this->triangleIds.size();
    animData += String(tileCount) + " ";

    auto fromFriendColor = doc["fromFriendColor"].as<JsonArray>();
    for (JsonPair kv : doc)
    {
        String tileId = kv.key().c_str();
        if (tileId.equals("fromFriendColor"))
            continue;

        auto rgb = kv.value().as<JsonArray>();

        // animData += tileId + " 2 " + String(rgb[0].as<int>()) + " " + String(rgb[1].as<int>()) + " " +
        //            String(rgb[2].as<int>()) + " 0 " + String(10 * 360) + " 0 0 0 0 360 ";
        animData += tileId + " 2 " + "0 0 0 0 30 " + String(rgb[0].as<int>()) + " " + String(rgb[1].as<int>()) + " " +
                    String(rgb[2].as<int>()) + " 0 50 ";
    }

    for (const auto &triangleId : triangleIds)
    {
        animData += triangleId + " 2 " + String(fromFriendColor[0].as<int>()) + " " + String(fromFriendColor[1].as<int>()) + " " +
                    String(fromFriendColor[2].as<int>()) + " 0 " + String(10 * 360) + " 0 0 0 0 360 ";
    }

    JsonDocument jsonPayload;
    jsonPayload["write"] = JsonObject();
    jsonPayload["write"]["command"] = "display";
    jsonPayload["write"]["version"] = "2.0";
    jsonPayload["write"]["animType"] = "custom";
    jsonPayload["write"]["animData"] = animData;
    jsonPayload["write"]["loop"] = false;
    jsonPayload["write"]["palette"] = JsonArray();
    jsonPayload["write"]["palette"].add(JsonObject());
    jsonPayload["write"]["palette"][0]["hue"] = 0;

    this->colorCallback();
    return sendRequest("PUT", "/effects", &jsonPayload, nullptr, true);
}
