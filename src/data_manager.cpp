#include "data_manager.h"
#include "config.h"
#include "api_client.h"
#include <Arduino.h>

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

void initDataManager(AppConfig* config, TickerData* tickerData) {
  appConfig = config;
  tickers = tickerData;

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

  // 1. Fetch crypto prices (batch call for all enabled crypto tickers)
  if (now - lastCryptoFetch >= CRYPTO_FETCH_INTERVAL_MS || lastCryptoFetch == 0) {
    // Build comma-separated list of CoinGecko IDs
    String cryptoIds = "";
    int cryptoCount = 0;

    for (int i = 0; i < appConfig->numTickers; i++) {
      if (appConfig->tickers[i].enabled && appConfig->tickers[i].type == TICKER_CRYPTO) {
        if (cryptoCount > 0) {
          cryptoIds += ",";
        }
        cryptoIds += appConfig->tickers[i].apiId;
        cryptoCount++;
      }
    }

    if (cryptoCount > 0) {
      Serial.printf("[DataMgr] Fetching %d crypto tickers\n", cryptoCount);
      int updated = fetchCryptoPrices(cryptoIds.c_str(), tickers, appConfig->numTickers, appConfig->tickers);
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
        // Calculate 24h change (simplified - would need historical data for accuracy)
        // For now, just update price and mark as valid
        float oldPrice = tickers[currentStockIndex].currentPrice;
        tickers[currentStockIndex].currentPrice = price;
        tickers[currentStockIndex].priceValid = true;

        // Calculate change if we have old price
        if (oldPrice > 0 && tickers[currentStockIndex].priceChange24h == 0) {
          tickers[currentStockIndex].priceChange24h = ((price - oldPrice) / oldPrice) * 100.0;
        }

        Serial.printf("[DataMgr] Updated %s: $%.2f\n", config->symbol, price);
      } else {
        Serial.printf("[DataMgr] Failed to fetch %s\n", config->symbol);
      }

      currentStockIndex = (currentStockIndex + 1) % appConfig->numTickers;
    }

    lastStockFetch = now;
  }

  // 3. Fetch sparkline data (round-robin through all tickers and timeframes)
  unsigned long sparklineInterval = SPARKLINE_24H_INTERVAL_MS;
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
        Serial.printf("[DataMgr] Updated sparkline for %s (%dd)\n", config->symbol, days);
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
