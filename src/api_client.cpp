#include "api_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <float.h>

static HTTPClient http;
static WiFiClientSecure client;
static String coinGeckoApiKey = "";

void initApiClient() {
  client.setInsecure(); // Skip cert validation - ESP32 has limited CA store
  Serial.println("[API] Client initialized");
}

void setCoinGeckoApiKey(const char* key) {
  coinGeckoApiKey = String(key);
  Serial.printf("[API] CoinGecko API key set: %s\n", key);
}

int fetchCryptoPrices(const char* ids, TickerData* tickerData, int numTickers, const TickerConfig* configs) {
  if (!ids || strlen(ids) == 0) {
    Serial.println("[API] No crypto IDs provided");
    return 0;
  }

  String url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=";
  url += ids;
  url += "&price_change_percentage=24h&sparkline=false";

  if (coinGeckoApiKey.length() > 0) {
    url += "&x_cg_demo_api_key=";
    url += coinGeckoApiKey;
  }

  Serial.printf("[API] Fetching crypto prices: %s\n", ids);

  http.begin(client, url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[API] HTTP error: %d\n", httpCode);
    http.end();
    return 0;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("[API] JSON parse error: %s\n", error.c_str());
    return 0;
  }

  int updated = 0;
  JsonArray array = doc.as<JsonArray>();

  // Match each API result to the corresponding ticker by apiId
  for (JsonObject coin : array) {
    const char* coinId = coin["id"];

    for (int i = 0; i < numTickers; i++) {
      if (configs[i].type == TICKER_CRYPTO && strcmp(configs[i].apiId, coinId) == 0) {
        tickerData[i].currentPrice = coin["current_price"].as<float>();
        tickerData[i].priceChange24h = coin["price_change_percentage_24h"].as<float>();
        tickerData[i].priceValid = true;
        updated++;

        Serial.printf("[API] Updated %s: $%.2f (%.2f%%)\n",
                     configs[i].symbol, tickerData[i].currentPrice, tickerData[i].priceChange24h);
        break;
      }
    }
  }

  delay(200); // Be nice to the API
  return updated;
}

bool fetchCryptoChart(const char* coinId, int days, SparklineData* outSparkline) {
  if (!coinId || !outSparkline) {
    return false;
  }

  String url = "https://api.coingecko.com/api/v3/coins/";
  url += coinId;
  url += "/market_chart?vs_currency=usd&days=";
  url += String(days);

  if (coinGeckoApiKey.length() > 0) {
    url += "&x_cg_demo_api_key=";
    url += coinGeckoApiKey;
  }

  Serial.printf("[API] Fetching chart for %s (%dd)\n", coinId, days);

  http.begin(client, url);
  http.setTimeout(15000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[API] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("[API] JSON parse error: %s\n", error.c_str());
    return false;
  }

  JsonArray prices = doc["prices"];
  int rawCount = prices.size();

  if (rawCount < 2) {
    Serial.println("[API] Insufficient data points");
    return false;
  }

  // Find min and max prices
  float minPrice = FLT_MAX;
  float maxPrice = -FLT_MAX;

  for (JsonArray point : prices) {
    float price = point[1].as<float>();
    if (price < minPrice) minPrice = price;
    if (price > maxPrice) maxPrice = price;
  }

  // Downsample to SPARKLINE_POINTS buckets
  float buckets[SPARKLINE_POINTS] = {0};
  int bucketCounts[SPARKLINE_POINTS] = {0};
  int bucketSize = rawCount / SPARKLINE_POINTS;
  if (bucketSize < 1) bucketSize = 1;

  int bucketIndex = 0;
  int pointIndex = 0;

  for (JsonArray point : prices) {
    float price = point[1].as<float>();
    buckets[bucketIndex] += price;
    bucketCounts[bucketIndex]++;
    pointIndex++;

    if (pointIndex >= bucketSize && bucketIndex < SPARKLINE_POINTS - 1) {
      pointIndex = 0;
      bucketIndex++;
    }
  }

  // Average buckets and scale to 0-255
  float priceRange = maxPrice - minPrice;
  if (priceRange < 0.0001) priceRange = 1.0; // Avoid division by zero

  for (int i = 0; i < SPARKLINE_POINTS; i++) {
    if (bucketCounts[i] > 0) {
      float avgPrice = buckets[i] / bucketCounts[i];
      float normalized = (avgPrice - minPrice) / priceRange;
      outSparkline->points[i] = (uint8_t)(normalized * 255.0);
    } else {
      outSparkline->points[i] = 0;
    }
  }

  outSparkline->len = SPARKLINE_POINTS;
  outSparkline->priceMin = minPrice;
  outSparkline->priceMax = maxPrice;
  outSparkline->valid = true;

  Serial.printf("[API] Chart data: %d points, range $%.2f - $%.2f\n",
               rawCount, minPrice, maxPrice);

  delay(200); // Be nice to the API
  return true;
}

bool fetchStockPrice(const char* symbol, const char* apiKey, float* outPrice) {
  if (!symbol || !apiKey || !outPrice) {
    return false;
  }

  String url = "https://api.twelvedata.com/price?symbol=";
  url += symbol;
  url += "&apikey=";
  url += apiKey;

  Serial.printf("[API] Fetching stock price: %s\n", symbol);

  http.begin(client, url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[API] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("[API] JSON parse error: %s\n", error.c_str());
    return false;
  }

  if (!doc["price"].isNull()) {
    *outPrice = doc["price"].as<float>();
    Serial.printf("[API] %s price: $%.2f\n", symbol, *outPrice);
    delay(200);
    return true;
  } else {
    Serial.printf("[API] No price field in response for %s\n", symbol);
    return false;
  }
}

bool fetchStockChart(const char* symbol, const char* apiKey, const char* interval, int outputsize, SparklineData* outSparkline) {
  if (!symbol || !apiKey || !interval || !outSparkline) {
    return false;
  }

  String url = "https://api.twelvedata.com/time_series?symbol=";
  url += symbol;
  url += "&interval=";
  url += interval;
  url += "&outputsize=";
  url += String(outputsize);
  url += "&apikey=";
  url += apiKey;

  Serial.printf("[API] Fetching stock chart: %s (%s, %d points)\n", symbol, interval, outputsize);

  http.begin(client, url);
  http.setTimeout(15000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[API] HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("[API] JSON parse error: %s\n", error.c_str());
    return false;
  }

  if (doc["values"].isNull()) {
    Serial.println("[API] No values field in response");
    return false;
  }

  JsonArray values = doc["values"];
  int rawCount = values.size();

  if (rawCount < 2) {
    Serial.println("[API] Insufficient data points");
    return false;
  }

  // Parse prices and find min/max
  float* rawPrices = new float[rawCount];
  float minPrice = FLT_MAX;
  float maxPrice = -FLT_MAX;

  for (int i = 0; i < rawCount; i++) {
    float price = values[i]["close"].as<float>();
    rawPrices[i] = price;
    if (price < minPrice) minPrice = price;
    if (price > maxPrice) maxPrice = price;
  }

  // Downsample to SPARKLINE_POINTS buckets (reverse order - newest first in API)
  float buckets[SPARKLINE_POINTS] = {0};
  int bucketCounts[SPARKLINE_POINTS] = {0};
  int bucketSize = rawCount / SPARKLINE_POINTS;
  if (bucketSize < 1) bucketSize = 1;

  int bucketIndex = 0;
  int pointIndex = 0;

  for (int i = rawCount - 1; i >= 0; i--) { // Reverse order
    buckets[bucketIndex] += rawPrices[i];
    bucketCounts[bucketIndex]++;
    pointIndex++;

    if (pointIndex >= bucketSize && bucketIndex < SPARKLINE_POINTS - 1) {
      pointIndex = 0;
      bucketIndex++;
    }
  }

  delete[] rawPrices;

  // Average buckets and scale to 0-255
  float priceRange = maxPrice - minPrice;
  if (priceRange < 0.0001) priceRange = 1.0;

  for (int i = 0; i < SPARKLINE_POINTS; i++) {
    if (bucketCounts[i] > 0) {
      float avgPrice = buckets[i] / bucketCounts[i];
      float normalized = (avgPrice - minPrice) / priceRange;
      outSparkline->points[i] = (uint8_t)(normalized * 255.0);
    } else {
      outSparkline->points[i] = 0;
    }
  }

  outSparkline->len = SPARKLINE_POINTS;
  outSparkline->priceMin = minPrice;
  outSparkline->priceMax = maxPrice;
  outSparkline->valid = true;

  Serial.printf("[API] Chart data: %d points, range $%.2f - $%.2f\n",
               rawCount, minPrice, maxPrice);

  delay(200);
  return true;
}
