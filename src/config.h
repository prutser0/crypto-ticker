#pragma once
#include <Arduino.h>

// =================== DISPLAY ===================
// HUB75 Pin Mapping (default for ESP32)
// Adjust if your wiring differs
#define R1_PIN  25
#define G1_PIN  26
#define B1_PIN  27
#define R2_PIN  14
#define G2_PIN  12
#define B2_PIN  13
#define A_PIN   23
#define B_PIN   19
#define C_PIN    5
#define D_PIN   17
#define CLK_PIN 16
#define LAT_PIN  4
#define OE_PIN  15

#define PANEL_WIDTH   64
#define PANEL_HEIGHT  32

// =================== TICKERS ===================
#define MAX_TICKERS       15
#define MAX_SYMBOL_LEN    12
#define MAX_NAME_LEN      20
#define MAX_API_ID_LEN    32
#define SPARKLINE_POINTS  64  // Downsampled to display width

// =================== TIMING ===================
#define DEFAULT_BASE_TIME_MS      8000   // 8 seconds per timeframe
#define DEFAULT_BRIGHTNESS        128
#define CRYPTO_FETCH_INTERVAL_MS  60000  // 60 sec
#define STOCK_FETCH_INTERVAL_MS   8000   // 8 sec per stock (round-robin)
#define SPARKLINE_24H_INTERVAL_MS 600000 // 10 min
#define SPARKLINE_7D_INTERVAL_MS  1800000 // 30 min
#define SPARKLINE_30D_INTERVAL_MS 3600000 // 60 min
#define SPARKLINE_90D_INTERVAL_MS 3600000 // 60 min

// =================== API ===================
#define COINGECKO_BASE_URL    "https://api.coingecko.com/api/v3"
#define TWELVEDATA_BASE_URL   "https://api.twelvedata.com"

// =================== WIFI ===================
#define WIFI_AP_NAME          "CryptoTicker"
#define WIFI_RECONNECT_MS     30000

// =================== FIRMWARE ===================
#define FIRMWARE_VERSION      "2.0.0"
