#pragma once
#include <Arduino.h>
#include "config.h"

enum TickerType : uint8_t {
    TICKER_CRYPTO = 0,
    TICKER_STOCK  = 1,
    TICKER_FOREX  = 2
};

enum ChartTimeframe : uint8_t {
    TIMEFRAME_24H = 0,
    TIMEFRAME_7D  = 1,
    TIMEFRAME_30D = 2,
    TIMEFRAME_90D = 3,
    TIMEFRAME_COUNT = 4
};

// Sparkline data for one timeframe, pre-scaled to 0..chartHeight
struct SparklineData {
    uint8_t points[SPARKLINE_POINTS];
    uint8_t len;
    float priceMin;
    float priceMax;
    bool valid;
};

// All data for a single ticker
struct TickerData {
    char symbol[MAX_SYMBOL_LEN];
    char name[MAX_NAME_LEN];
    TickerType type;
    float currentPrice;
    float priceChange24h;  // percentage (from API)
    float priceChange[TIMEFRAME_COUNT]; // per-timeframe change% (24h,7d,30d,90d)
    float high24h;
    float low24h;
    uint32_t lastPriceUpdate;
    bool priceValid;

    SparklineData sparklines[TIMEFRAME_COUNT];
};

// Configuration for one ticker slot (stored in config.json)
struct TickerConfig {
    char symbol[MAX_SYMBOL_LEN];
    char apiId[MAX_API_ID_LEN];  // CoinGecko ID or Twelve Data symbol
    TickerType type;
    float timeMultiplier;         // Display time multiplier (default 1.0)
    bool enabled;
};

// Full application configuration (stored in LittleFS)
struct AppConfig {
    uint8_t brightness;
    uint32_t baseTimeMs;          // Base display time per timeframe
    uint8_t numTickers;
    TickerConfig tickers[MAX_TICKERS];
    char twelveDataApiKey[64];
    char coinGeckoApiKey[64];     // Optional demo key
    char cmcApiKey[64];           // CoinMarketCap API key
};

// Get default config
inline AppConfig getDefaultConfig() {
    AppConfig cfg = {};
    cfg.brightness = DEFAULT_BRIGHTNESS;
    cfg.baseTimeMs = DEFAULT_BASE_TIME_MS;

    // Default tickers
    struct { const char* sym; const char* apiId; TickerType type; } defaults[] = {
        {"BTC",   "bitcoin",    TICKER_CRYPTO},
        {"ETH",   "ethereum",   TICKER_CRYPTO},
        {"SOL",   "solana",     TICKER_CRYPTO},
        {"LTC",   "litecoin",   TICKER_CRYPTO},
        {"DOGE",  "dogecoin",   TICKER_CRYPTO},
        {"XMR",   "monero",     TICKER_CRYPTO},
        {"MSTR",  "MSTR",       TICKER_STOCK},
        {"NDX",   "QQQ",        TICKER_STOCK},
        {"SPX",   "SPY",        TICKER_STOCK},
        {"RUT",   "IWM",        TICKER_STOCK},
        {"EUR",   "EUR/USD",    TICKER_FOREX},
    };

    cfg.numTickers = 11;

    for (int i = 0; i < cfg.numTickers; i++) {
        strncpy(cfg.tickers[i].symbol, defaults[i].sym, MAX_SYMBOL_LEN - 1);
        strncpy(cfg.tickers[i].apiId, defaults[i].apiId, MAX_API_ID_LEN - 1);
        cfg.tickers[i].type = defaults[i].type;
        cfg.tickers[i].timeMultiplier = 1.0f;
        cfg.tickers[i].enabled = true;
    }

    return cfg;
}

// Timeframe label strings
inline const char* getTimeframeLabel(ChartTimeframe tf) {
    switch (tf) {
        case TIMEFRAME_24H: return "24H";
        case TIMEFRAME_7D:  return "7D";
        case TIMEFRAME_30D: return "30D";
        case TIMEFRAME_90D: return "90D";
        default: return "?";
    }
}

// CoinGecko days parameter for each timeframe
inline int getTimeframeDays(ChartTimeframe tf) {
    switch (tf) {
        case TIMEFRAME_24H: return 1;
        case TIMEFRAME_7D:  return 7;
        case TIMEFRAME_30D: return 30;
        case TIMEFRAME_90D: return 90;
        default: return 1;
    }
}
