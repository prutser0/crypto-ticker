#include <Arduino.h>
#include "config.h"
#include "ticker_types.h"
#include "display_renderer.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "api_client.h"
#include "data_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Global variables
AppConfig appConfig;
TickerData tickerData[MAX_TICKERS];
volatile bool configChanged = false;
SemaphoreHandle_t dataMutex = nullptr;

// Function prototypes
void loadConfig();
void onConfigChanged();
void fetchTask(void* param);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\nCrypto Ticker Starting...");

    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return;
    }
    Serial.println("LittleFS mounted");

    // Load configuration
    loadConfig();

    // Initialize display
    initDisplay(appConfig.brightness);
    renderLoadingScreen("Connecting WiFi...");

    // Initialize WiFi
    bool wifiConnected = initWiFi(WIFI_AP_NAME);

    if (wifiConnected) {
        String msg = "WiFi OK\n" + getIPAddress();
        renderLoadingScreen(msg.c_str());
        delay(2000);
    } else {
        renderErrorScreen("No WiFi");
        Serial.println("WiFi connection failed");
    }

    // Initialize API client
    initApiClient();
    if (strlen(appConfig.coinGeckoApiKey) > 0) {
        setCoinGeckoApiKey(appConfig.coinGeckoApiKey);
    }
    if (strlen(appConfig.cmcApiKey) > 0) {
        setCMCApiKey(appConfig.cmcApiKey);
    }

    // Initialize data manager
    initDataManager(&appConfig, tickerData);

    // Initialize web server
    initWebServer(&appConfig, tickerData, onConfigChanged);

    // Create mutex for thread-safe data access
    dataMutex = xSemaphoreCreateMutex();
    if (dataMutex == nullptr) {
        Serial.println("Failed to create mutex");
        return;
    }

    // Create FreeRTOS task for data fetching on Core 0
    xTaskCreatePinnedToCore(
        fetchTask,      // Task function
        "fetch",        // Task name
        8192,           // Stack size
        NULL,           // Parameters
        1,              // Priority
        NULL,           // Task handle
        0               // Core ID (0)
    );

    Serial.printf("Setup complete - Free heap: %d bytes, largest block: %d bytes\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // Force initial data refresh
    forceRefresh();
}

void loop() {
    // Main display loop runs on Core 1
    // Fixed cycle: ticker1 24H > 7D > 30D > 90D > ticker2 24H > 7D > ...
    bool anyEnabled = false;

    for (int i = 0; i < appConfig.numTickers; i++) {
        if (!appConfig.tickers[i].enabled) {
            continue;
        }

        anyEnabled = true;

        // Thread-safe copy of ticker data
        TickerData localTicker;
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            localTicker = tickerData[i];
            xSemaphoreGive(dataMutex);
        }

        // Always show all 4 timeframes in order
        for (int tf = 0; tf < 4; tf++) {
            renderTickerScreen(localTicker, (ChartTimeframe)tf);
            delay(appConfig.baseTimeMs * appConfig.tickers[i].timeMultiplier);
        }
    }

    // If no tickers enabled, show loading screen
    if (!anyEnabled) {
        renderLoadingScreen("No tickers\nenabled");
        delay(2000);
    }
}

void fetchTask(void* param) {
    Serial.println("Fetch task started on core " + String(xPortGetCoreID()));

    while (true) {
        // Update data from APIs
        updateData();

        // Check if config changed
        if (configChanged) {
            Serial.println("Config changed, reinitializing data manager");

            // Reinitialize data manager with new config
            initDataManager(&appConfig, tickerData);

            // Update API keys if changed
            if (strlen(appConfig.coinGeckoApiKey) > 0) {
                setCoinGeckoApiKey(appConfig.coinGeckoApiKey);
            }
            if (strlen(appConfig.cmcApiKey) > 0) {
                setCMCApiKey(appConfig.cmcApiKey);
            }

            // Force refresh with new config
            forceRefresh();

            configChanged = false;
        }

        // Small delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void onConfigChanged() {
    Serial.println("Config changed callback");

    // Update display brightness immediately
    initDisplay(appConfig.brightness);

    // Signal fetch task to reload
    configChanged = true;
}

void loadConfig() {
    File f = LittleFS.open("/config.json", "r");

    if (!f) {
        Serial.println("Config file not found, using defaults");
        appConfig = getDefaultConfig();
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        Serial.println("Failed to parse config, using defaults");
        appConfig = getDefaultConfig();
        return;
    }

    // Load config from JSON
    appConfig.brightness = doc["brightness"] | 128;
    appConfig.baseTimeMs = doc["baseTimeMs"] | 3000;
    appConfig.numTickers = doc["numTickers"] | 0;

    strlcpy(appConfig.twelveDataApiKey,
            doc["twelveDataApiKey"] | "",
            sizeof(appConfig.twelveDataApiKey));

    strlcpy(appConfig.coinGeckoApiKey,
            doc["coinGeckoApiKey"] | "",
            sizeof(appConfig.coinGeckoApiKey));

    strlcpy(appConfig.cmcApiKey,
            doc["cmcApiKey"] | "",
            sizeof(appConfig.cmcApiKey));

    if (!doc["tickers"].isNull()) {
        JsonArray tickers = doc["tickers"];
        appConfig.numTickers = min((int)tickers.size(), MAX_TICKERS);

        for (int i = 0; i < appConfig.numTickers; i++) {
            JsonObject t = tickers[i];
            strlcpy(appConfig.tickers[i].symbol,
                    t["symbol"] | "",
                    sizeof(appConfig.tickers[i].symbol));

            strlcpy(appConfig.tickers[i].apiId,
                    t["apiId"] | "",
                    sizeof(appConfig.tickers[i].apiId));

            appConfig.tickers[i].type = (TickerType)(t["type"] | 0);
            appConfig.tickers[i].timeMultiplier = t["timeMultiplier"] | 1;
            appConfig.tickers[i].enabled = t["enabled"] | true;
        }
    }

    Serial.printf("Config loaded: %d tickers, brightness %d\n",
                  appConfig.numTickers, appConfig.brightness);
}
