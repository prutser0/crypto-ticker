#include "api_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <float.h>

static HTTPClient http;
static WiFiClientSecure client;
static String coinGeckoApiKey = "";
static String cmcApiKey = "";

void initApiClient() {
  client.setInsecure(); // Skip cert validation - ESP32 has limited CA store
  Serial.println("[API] Client initialized");
}

void setCoinGeckoApiKey(const char* key) {
  coinGeckoApiKey = String(key);
  Serial.printf("[API] CoinGecko API key set: %s\n", key);
}

void setCMCApiKey(const char* key) {
  cmcApiKey = String(key);
  Serial.printf("[API] CMC API key set\n");
}

int fetchCMCPrices(const char* slugs, TickerData* tickerData, int numTickers, const TickerConfig* configs) {
  if (!slugs || strlen(slugs) == 0 || cmcApiKey.length() == 0) {
    Serial.println("[API] CMC: no slugs or API key");
    return 0;
  }

  String url = "https://pro-api.coinmarketcap.com/v2/cryptocurrency/quotes/latest?slug=";
  url += slugs;

  Serial.printf("[API] CMC fetching: %s\n", slugs);

  http.begin(client, url);
  http.setTimeout(10000);
  http.addHeader("X-CMC_PRO_API_KEY", cmcApiKey);
  http.addHeader("Accept", "application/json");
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[API] CMC HTTP error: %d\n", httpCode);
    http.end();
    return 0;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("[API] CMC JSON parse error: %s\n", error.c_str());
    return 0;
  }

  // Check API status
  int errCode = doc["status"]["error_code"] | -1;
  if (errCode != 0) {
    const char* errMsg = doc["status"]["error_message"] | "unknown";
    Serial.printf("[API] CMC API error %d: %s\n", errCode, errMsg);
    return 0;
  }

  int updated = 0;
  JsonObject data = doc["data"];

  // Iterate over all entries in data (keyed by CMC numeric ID)
  for (JsonPair kv : data) {
    JsonObject coin = kv.value();
    const char* slug = coin["slug"];
    if (!slug) continue;

    // Match slug to our ticker configs
    for (int i = 0; i < numTickers; i++) {
      if (configs[i].type == TICKER_CRYPTO && strcmp(configs[i].apiId, slug) == 0) {
        JsonObject quote = coin["quote"]["USD"];
        tickerData[i].currentPrice = quote["price"].as<float>();
        tickerData[i].priceChange24h = quote["percent_change_24h"].as<float>();
        tickerData[i].priceChange[TIMEFRAME_24H] = quote["percent_change_24h"].as<float>();
        tickerData[i].priceChange[TIMEFRAME_7D]  = quote["percent_change_7d"].as<float>();
        tickerData[i].priceChange[TIMEFRAME_30D] = quote["percent_change_30d"].as<float>();
        tickerData[i].priceChange[TIMEFRAME_90D] = quote["percent_change_90d"].as<float>();
        tickerData[i].priceValid = true;
        updated++;

        Serial.printf("[API] CMC %s: $%.2f (24h:%.1f%% 7d:%.1f%% 30d:%.1f%% 90d:%.1f%%)\n",
                     configs[i].symbol, tickerData[i].currentPrice,
                     tickerData[i].priceChange[0], tickerData[i].priceChange[1],
                     tickerData[i].priceChange[2], tickerData[i].priceChange[3]);
        break;
      }
    }
  }

  Serial.printf("[API] CMC credits used: %d\n", doc["status"]["credit_count"] | 0);
  return updated;
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
  // Use daily interval for 30d+ to reduce response size (avoids memory issues)
  if (days >= 14) {
    url += "&interval=daily";
  }

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

  // Collect raw prices into temporary array
  float* rawPrices = new float[rawCount];
  int idx = 0;
  for (JsonArray point : prices) {
    rawPrices[idx++] = point[1].as<float>();
  }

  // Resample to SPARKLINE_POINTS using linear interpolation
  float priceRange = maxPrice - minPrice;
  if (priceRange < 0.0001) priceRange = 1.0;

  for (int i = 0; i < SPARKLINE_POINTS; i++) {
    float srcPos = (float)i * (rawCount - 1) / (SPARKLINE_POINTS - 1);
    int lo = (int)srcPos;
    int hi = lo + 1;
    if (hi >= rawCount) hi = rawCount - 1;
    float frac = srcPos - lo;
    float price = rawPrices[lo] * (1.0f - frac) + rawPrices[hi] * frac;
    float normalized = (price - minPrice) / priceRange;
    outSparkline->points[i] = (uint8_t)(normalized * 255.0);
  }

  delete[] rawPrices;

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

  // Parse prices in reverse order (API returns newest first) and find min/max
  float* rawPrices = new float[rawCount];
  float minPrice = FLT_MAX;
  float maxPrice = -FLT_MAX;

  for (int i = 0; i < rawCount; i++) {
    float price = values[rawCount - 1 - i]["close"].as<float>(); // reverse: oldest first
    rawPrices[i] = price;
    if (price < minPrice) minPrice = price;
    if (price > maxPrice) maxPrice = price;
  }

  // Resample to SPARKLINE_POINTS using linear interpolation
  float priceRange = maxPrice - minPrice;
  if (priceRange < 0.0001) priceRange = 1.0;

  for (int i = 0; i < SPARKLINE_POINTS; i++) {
    float srcPos = (float)i * (rawCount - 1) / (SPARKLINE_POINTS - 1);
    int lo = (int)srcPos;
    int hi = lo + 1;
    if (hi >= rawCount) hi = rawCount - 1;
    float frac = srcPos - lo;
    float price = rawPrices[lo] * (1.0f - frac) + rawPrices[hi] * frac;
    float normalized = (price - minPrice) / priceRange;
    outSparkline->points[i] = (uint8_t)(normalized * 255.0);
  }

  delete[] rawPrices;

  outSparkline->len = SPARKLINE_POINTS;
  outSparkline->priceMin = minPrice;
  outSparkline->priceMax = maxPrice;
  outSparkline->valid = true;

  Serial.printf("[API] Chart data: %d points, range $%.2f - $%.2f\n",
               rawCount, minPrice, maxPrice);

  delay(200);
  return true;
}
