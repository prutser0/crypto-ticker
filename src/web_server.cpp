#include "web_server.h"
#include "wifi_manager.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>

static AsyncWebServer server(80);
static AppConfig* g_config = nullptr;
static TickerData* g_tickerData = nullptr;
static void (*g_onConfigChanged)() = nullptr;

void saveConfig(AppConfig* config) {
    JsonDocument doc;
    doc["brightness"] = config->brightness;
    doc["baseTimeMs"] = config->baseTimeMs;
    doc["numTickers"] = config->numTickers;
    doc["twelveDataApiKey"] = config->twelveDataApiKey;
    doc["coinGeckoApiKey"] = config->coinGeckoApiKey;
    doc["cmcApiKey"] = config->cmcApiKey;

    JsonArray tickers = doc["tickers"].to<JsonArray>();
    for (int i = 0; i < config->numTickers; i++) {
        JsonObject t = tickers.add<JsonObject>();
        t["symbol"] = config->tickers[i].symbol;
        t["apiId"] = config->tickers[i].apiId;
        t["type"] = (int)config->tickers[i].type;
        t["timeMultiplier"] = config->tickers[i].timeMultiplier;
        t["enabled"] = config->tickers[i].enabled;
    }

    File f = LittleFS.open("/config.json", "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
        Serial.println("Config saved to LittleFS");
    } else {
        Serial.println("Failed to save config");
    }
}

void initWebServer(AppConfig* config, TickerData* tickerData, void (*onConfigChanged)()) {
    g_config = config;
    g_tickerData = tickerData;
    g_onConfigChanged = onConfigChanged;

    // Serve static files from LittleFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/style.css", "text/css");
    });

    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/app.js", "application/javascript");
    });

    // API endpoint: Get current config
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["brightness"] = g_config->brightness;
        doc["baseTimeMs"] = g_config->baseTimeMs;
        doc["numTickers"] = g_config->numTickers;
        doc["twelveDataApiKey"] = g_config->twelveDataApiKey;
        doc["coinGeckoApiKey"] = g_config->coinGeckoApiKey;
        doc["cmcApiKey"] = g_config->cmcApiKey;

        JsonArray tickers = doc["tickers"].to<JsonArray>();
        for (int i = 0; i < g_config->numTickers; i++) {
            JsonObject t = tickers.add<JsonObject>();
            t["symbol"] = g_config->tickers[i].symbol;
            t["apiId"] = g_config->tickers[i].apiId;
            t["type"] = (int)g_config->tickers[i].type;
            t["timeMultiplier"] = g_config->tickers[i].timeMultiplier;
            t["enabled"] = g_config->tickers[i].enabled;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API endpoint: Update config
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String body;

            if (index == 0) {
                body = "";
            }

            for (size_t i = 0; i < len; i++) {
                body += (char)data[i];
            }

            if (index + len == total) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, body);

                if (error) {
                    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                    return;
                }

                // Update config
                if (!doc["brightness"].isNull()) {
                    g_config->brightness = doc["brightness"];
                }
                if (!doc["baseTimeMs"].isNull()) {
                    g_config->baseTimeMs = doc["baseTimeMs"];
                }
                if (!doc["twelveDataApiKey"].isNull()) {
                    strlcpy(g_config->twelveDataApiKey, doc["twelveDataApiKey"] | "", sizeof(g_config->twelveDataApiKey));
                }
                if (!doc["coinGeckoApiKey"].isNull()) {
                    strlcpy(g_config->coinGeckoApiKey, doc["coinGeckoApiKey"] | "", sizeof(g_config->coinGeckoApiKey));
                }
                if (!doc["cmcApiKey"].isNull()) {
                    strlcpy(g_config->cmcApiKey, doc["cmcApiKey"] | "", sizeof(g_config->cmcApiKey));
                }

                if (!doc["tickers"].isNull()) {
                    JsonArray tickers = doc["tickers"];
                    g_config->numTickers = min((int)tickers.size(), MAX_TICKERS);

                    for (int i = 0; i < g_config->numTickers; i++) {
                        JsonObject t = tickers[i];
                        strlcpy(g_config->tickers[i].symbol, t["symbol"] | "", sizeof(g_config->tickers[i].symbol));
                        strlcpy(g_config->tickers[i].apiId, t["apiId"] | "", sizeof(g_config->tickers[i].apiId));
                        g_config->tickers[i].type = (TickerType)(t["type"] | 0);
                        g_config->tickers[i].timeMultiplier = t["timeMultiplier"] | 1;
                        g_config->tickers[i].enabled = t["enabled"] | true;
                    }
                }

                // Save to LittleFS
                saveConfig(g_config);

                // Trigger callback
                if (g_onConfigChanged) {
                    g_onConfigChanged();
                }

                request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
        }
    );

    // API endpoint: Get system status
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        doc["wifiSSID"] = getSSID();
        doc["wifiIP"] = getIPAddress();
        doc["wifiRSSI"] = getRSSI();
        doc["firmwareVersion"] = FIRMWARE_VERSION;

        // Add current ticker prices
        JsonArray prices = doc["prices"].to<JsonArray>();
        for (int i = 0; i < g_config->numTickers; i++) {
            if (g_config->tickers[i].enabled) {
                JsonObject p = prices.add<JsonObject>();
                p["symbol"] = g_tickerData[i].symbol;
                p["price"] = g_tickerData[i].currentPrice;
                p["change24h"] = g_tickerData[i].priceChange24h;
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API endpoint: Get all ticker data
    server.on("/api/tickers", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        JsonArray tickers = doc.to<JsonArray>();

        for (int i = 0; i < g_config->numTickers; i++) {
            if (g_config->tickers[i].enabled) {
                JsonObject t = tickers.add<JsonObject>();
                t["symbol"] = g_tickerData[i].symbol;
                t["currentPrice"] = g_tickerData[i].currentPrice;
                t["change24h"] = g_tickerData[i].priceChange24h;
                t["high24h"] = g_tickerData[i].high24h;
                t["low24h"] = g_tickerData[i].low24h;
                t["lastUpdate"] = g_tickerData[i].lastPriceUpdate;
                t["isValid"] = g_tickerData[i].priceValid;
            }
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // OTA firmware update endpoint
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", success ? "OK" : "FAIL");
            response->addHeader("Connection", "close");
            request->send(response);
            if (success) {
                delay(500);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("Update start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("Update success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    server.begin();
    Serial.println("Web server started");
}

void handleWebServer() {
    // Not needed for AsyncWebServer, but kept for compatibility
}
