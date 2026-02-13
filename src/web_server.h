#pragma once
#include "ticker_types.h"

// Initialize web server on port 80
// config: pointer to AppConfig (will be modified when user saves)
// tickerData: pointer to TickerData array (for status display)
// onConfigChanged: callback when config is saved (to reload data)
void initWebServer(AppConfig* config, TickerData* tickerData, void (*onConfigChanged)());

// Call in loop to handle OTA (not needed for async but kept for future use)
void handleWebServer();
