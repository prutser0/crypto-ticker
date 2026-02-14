#include "data_manager.h"
#include "config.h"
#include "api_client.h"
#include <Arduino.h>
#include <LittleFS.h>

static AppConfig* appConfig = nullptr;
static TickerData* tickers = nullptr;

// Timing tracking
static unsigned long lastCryptoFetch = 0;
static unsigned long lastStockFetch = 0;
static unsigned long lastSparklineFetch = 0;

// Round-robin indices
static int currentStockIndex = 0;
static int currentSparklineTickerIndex = 0;
static int currentSparklineTimeframe = 0; // 0=24h, 1=7d, 2=30d, 3=90d

// Cache sparkline to LittleFS: /cache/<apiId>_<tf>.bin
static String cachePath(const char* apiId, int tf) {
  String path = "/cache/";
  path += apiId;
  path += "_";
  path += String(tf);
  path += ".bin";
  return path;
}

static void saveSparklineCache(const char* apiId, int tf, const SparklineData* sp) {
  String path = cachePath(apiId, tf);
  File f = LittleFS.open(path, "w");
  if (!f) return;
  f.write((uint8_t*)sp, sizeof(SparklineData));
  f.close();
}

static bool loadSparklineCache(const char* apiId, int tf, SparklineData* sp) {
  String path = cachePath(apiId, tf);
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  if (f.size() != sizeof(SparklineData)) { f.close(); return false; }
  f.read((uint8_t*)sp, sizeof(SparklineData));
  f.close();
  return sp->valid;
}

void initDataManager(AppConfig* config, TickerData* tickerData) {
  appConfig = config;
  tickers = tickerData;

  // Ensure cache directory exists
  LittleFS.mkdir("/cache");

  // Copy symbol and type from config into ticker data
  for (int i = 0; i < config->numTickers; i++) {
    strlcpy(tickerData[i].symbol, config->tickers[i].symbol, MAX_SYMBOL_LEN);
    tickerData[i].type = config->tickers[i].type;

    // Load cached sparklines
    for (int tf = 0; tf < 4; tf++) {
      if (loadSparklineCache(config->tickers[i].apiId, tf, &tickerData[i].sparklines[tf])) {
        Serial.printf("[DataMgr] Loaded cached sparkline: %s tf=%d\n", config->tickers[i].symbol, tf);

        // Compute change% from cached sparkline for stocks/forex
        SparklineData* sp = &tickerData[i].sparklines[tf];
        if (config->tickers[i].type != TICKER_CRYPTO && sp->valid && sp->len > 1) {
          float range = sp->priceMax - sp->priceMin;
          if (range > 0.0001f) {
            float startPrice = sp->priceMin + (sp->points[0] / 255.0f) * range;
            float endPrice = sp->priceMin + (sp->points[sp->len - 1] / 255.0f) * range;
            if (startPrice > 0.0001f) {
              tickerData[i].priceChange[tf] = ((endPrice - startPrice) / startPrice) * 100.0f;
              if (tf == 0) tickerData[i].priceChange24h = tickerData[i].priceChange[tf];
            }
          }
        }
      }
    }
  }

  lastCryptoFetch = 0;
  lastStockFetch = 0;
  lastSparklineFetch = 0;

  currentStockIndex = 0;
  currentSparklineTickerIndex = 0;
  currentSparklineTimeframe = 0;

  Serial.println("[DataMgr] Initialized");
}

void forceRefresh() {
  lastCryptoFetch = 0;
  lastStockFetch = 0;
  lastSparklineFetch = 0;
  Serial.println("[DataMgr] Forced refresh scheduled");
}

void updateData() {
  if (!appConfig || !tickers) {
    return;
  }

  unsigned long now = millis();

  // 1. Fetch crypto prices (CMC preferred, CoinGecko fallback)
  if (now - lastCryptoFetch >= CRYPTO_FETCH_INTERVAL_MS || lastCryptoFetch == 0) {
    // Build comma-separated list of slugs/IDs
    String cryptoSlugs = "";
    int cryptoCount = 0;

    for (int i = 0; i < appConfig->numTickers; i++) {
      if (appConfig->tickers[i].enabled && appConfig->tickers[i].type == TICKER_CRYPTO) {
        if (cryptoCount > 0) {
          cryptoSlugs += ",";
        }
        cryptoSlugs += appConfig->tickers[i].apiId;
        cryptoCount++;
      }
    }

    if (cryptoCount > 0) {
      int updated = 0;
      if (strlen(appConfig->cmcApiKey) > 0) {
        // Use CoinMarketCap (gives per-timeframe change%)
        Serial.printf("[DataMgr] CMC: fetching %d crypto tickers\n", cryptoCount);
        updated = fetchCMCPrices(cryptoSlugs.c_str(), tickers, appConfig->numTickers, appConfig->tickers);
      } else {
        // Fallback to CoinGecko
        Serial.printf("[DataMgr] CoinGecko: fetching %d crypto tickers\n", cryptoCount);
        updated = fetchCryptoPrices(cryptoSlugs.c_str(), tickers, appConfig->numTickers, appConfig->tickers);
      }
      Serial.printf("[DataMgr] Updated %d/%d crypto tickers\n", updated, cryptoCount);
    }

    lastCryptoFetch = now;
  }

  // 2. Fetch stock/forex prices (round-robin, one per interval)
  if (now - lastStockFetch >= STOCK_FETCH_INTERVAL_MS || lastStockFetch == 0) {
    // Find next enabled stock/forex ticker
    int startIndex = currentStockIndex;
    bool found = false;

    do {
      if (appConfig->tickers[currentStockIndex].enabled &&
          appConfig->tickers[currentStockIndex].type != TICKER_CRYPTO) {
        found = true;
        break;
      }
      currentStockIndex = (currentStockIndex + 1) % appConfig->numTickers;
    } while (currentStockIndex != startIndex);

    if (found) {
      const TickerConfig* config = &appConfig->tickers[currentStockIndex];
      float price = 0;

      Serial.printf("[DataMgr] Fetching stock: %s\n", config->symbol);

      if (fetchStockPrice(config->apiId, appConfig->twelveDataApiKey, &price)) {
        tickers[currentStockIndex].currentPrice = price;
        tickers[currentStockIndex].priceValid = true;
        // Change% is computed from sparkline data (see sparkline fetch below)

        Serial.printf("[DataMgr] Updated %s: $%.2f\n", config->symbol, price);
      } else {
        Serial.printf("[DataMgr] Failed to fetch %s\n", config->symbol);
      }

      currentStockIndex = (currentStockIndex + 1) % appConfig->numTickers;
    }

    lastStockFetch = now;
  }

  // 3. Fetch sparkline data (round-robin through all tickers and timeframes)
  // Use fast interval (2s) until all sparklines are populated, then normal intervals
  bool allPopulated = true;
  for (int i = 0; i < appConfig->numTickers && allPopulated; i++) {
    if (!appConfig->tickers[i].enabled) continue;
    for (int tf = 0; tf < 4; tf++) {
      if (!tickers[i].sparklines[tf].valid) { allPopulated = false; break; }
    }
  }
  unsigned long sparklineInterval = allPopulated ? SPARKLINE_24H_INTERVAL_MS : 15000;
  if (now - lastSparklineFetch >= sparklineInterval || lastSparklineFetch == 0) {
    // Find next enabled ticker
    int startTicker = currentSparklineTickerIndex;
    bool found = false;

    do {
      if (appConfig->tickers[currentSparklineTickerIndex].enabled) {
        found = true;
        break;
      }
      currentSparklineTickerIndex = (currentSparklineTickerIndex + 1) % appConfig->numTickers;
    } while (currentSparklineTickerIndex != startTicker);

    if (found) {
      const TickerConfig* config = &appConfig->tickers[currentSparklineTickerIndex];
      SparklineData* sparkline = nullptr;
      int days = 0;
      const char* interval = nullptr;
      int outputsize = 0;

      // Select sparkline based on current timeframe
      switch (currentSparklineTimeframe) {
        case 0: // 24h
          sparkline = &tickers[currentSparklineTickerIndex].sparklines[TIMEFRAME_24H];
          days = 1;
          interval = "1h";
          outputsize = 24;
          break;
        case 1: // 7d
          sparkline = &tickers[currentSparklineTickerIndex].sparklines[TIMEFRAME_7D];
          days = 7;
          interval = "1day";
          outputsize = 7;
          break;
        case 2: // 30d
          sparkline = &tickers[currentSparklineTickerIndex].sparklines[TIMEFRAME_30D];
          days = 30;
          interval = "1day";
          outputsize = 30;
          break;
        case 3: // 90d
          sparkline = &tickers[currentSparklineTickerIndex].sparklines[TIMEFRAME_90D];
          days = 90;
          interval = "1day";
          outputsize = 90;
          break;
      }

      Serial.printf("[DataMgr] Fetching sparkline for %s (%dd)\n", config->symbol, days);

      bool success = false;
      if (config->type == TICKER_CRYPTO) {
        success = fetchCryptoChart(config->apiId, days, sparkline);
      } else {
        success = fetchStockChart(config->apiId, appConfig->twelveDataApiKey, interval, outputsize, sparkline);
      }

      if (success) {
        saveSparklineCache(config->apiId, currentSparklineTimeframe, sparkline);

        // Compute change% from sparkline data for stocks/forex
        // (crypto uses CMC's per-timeframe change% which is more accurate)
        if (config->type != TICKER_CRYPTO && sparkline->valid && sparkline->len > 1) {
          float range = sparkline->priceMax - sparkline->priceMin;
          if (range > 0.0001f) {
            float startPrice = sparkline->priceMin + (sparkline->points[0] / 255.0f) * range;
            float endPrice = sparkline->priceMin + (sparkline->points[sparkline->len - 1] / 255.0f) * range;
            if (startPrice > 0.0001f) {
              float pct = ((endPrice - startPrice) / startPrice) * 100.0f;
              tickers[currentSparklineTickerIndex].priceChange[currentSparklineTimeframe] = pct;
              if (currentSparklineTimeframe == 0) {
                tickers[currentSparklineTickerIndex].priceChange24h = pct;
              }
              Serial.printf("[DataMgr] %s %dd change: %.1f%%\n", config->symbol, days, pct);
            }
          }
        }

        Serial.printf("[DataMgr] Updated + cached sparkline for %s (%dd)\n", config->symbol, days);
      } else {
        Serial.printf("[DataMgr] Failed to fetch sparkline for %s (%dd)\n", config->symbol, days);
      }

      // Move to next timeframe
      currentSparklineTimeframe++;
      if (currentSparklineTimeframe >= 4) {
        currentSparklineTimeframe = 0;
        // Move to next ticker after completing all timeframes
        currentSparklineTickerIndex = (currentSparklineTickerIndex + 1) % appConfig->numTickers;
      }
    }

    lastSparklineFetch = now;
  }
}

String getDataStatus() {
  if (!appConfig || !tickers) {
    return "Not initialized";
  }

  unsigned long now = millis();
  String status = "";

  status += "Crypto: ";
  status += (now - lastCryptoFetch) / 1000;
  status += "s ago | Stock: ";
  status += (now - lastStockFetch) / 1000;
  status += "s ago | Chart: ";
  status += (now - lastSparklineFetch) / 1000;
  status += "s ago";

  return status;
}
