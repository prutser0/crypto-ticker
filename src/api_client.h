#pragma once
#include <Arduino.h>
#include "ticker_types.h"

// Initialize HTTP client (call once in setup)
void initApiClient();

// Fetch current prices + 24h change for all crypto tickers in one batch call
// Uses CoinGecko /coins/markets endpoint with sparkline=false
// ids: comma-separated CoinGecko IDs (e.g. "bitcoin,ethereum,solana")
// Results are written directly into the tickerData array
// Returns number of tickers successfully updated
int fetchCryptoPrices(const char* ids, TickerData* tickerData, int numTickers, const TickerConfig* configs);

// Fetch sparkline/chart data for a single crypto ticker
// Uses CoinGecko /coins/{id}/market_chart?vs_currency=usd&days=N
// Downsamples the price array to SPARKLINE_POINTS (64) uint8_t values
// Returns true on success
bool fetchCryptoChart(const char* coinId, int days, SparklineData* outSparkline);

// Fetch current price for a single stock/forex ticker
// Uses Twelve Data /price endpoint
// Returns true on success
bool fetchStockPrice(const char* symbol, const char* apiKey, float* outPrice);

// Fetch historical data for a single stock/forex ticker
// Uses Twelve Data /time_series endpoint
// interval: "1h" for 24h, "1day" for 7d/30d/90d
// outputsize: number of data points to fetch
// Returns true on success
bool fetchStockChart(const char* symbol, const char* apiKey, const char* interval, int outputsize, SparklineData* outSparkline);

// Set optional CoinGecko demo API key (adds x_cg_demo_api_key param)
void setCoinGeckoApiKey(const char* key);

// Set CoinMarketCap API key
void setCMCApiKey(const char* key);

// Fetch prices + per-timeframe change% from CoinMarketCap
// slugs: comma-separated slugs (e.g. "bitcoin,ethereum,solana")
// Returns number of tickers successfully updated
int fetchCMCPrices(const char* slugs, TickerData* tickerData, int numTickers, const TickerConfig* configs);
