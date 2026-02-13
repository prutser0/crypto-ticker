#pragma once

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "ticker_types.h"

// Initialize the display hardware
bool initDisplay(uint8_t brightness);

// Set brightness (0-255)
void setDisplayBrightness(uint8_t brightness);

// Get pointer to the DMA display (for direct drawing if needed)
MatrixPanel_I2S_DMA* getDisplay();

// Render a ticker screen: symbol + price + change% + timeframe label + sparkline chart
// Layout on 64x32:
//   Row 0-5:   Symbol + Price (e.g. "BTC $97,234")
//   Row 6:     Separator line
//   Row 7-9:   Change% + timeframe label (e.g. "+2.4% 24H")
//   Row 10-31: Sparkline chart (64 wide x 22 tall)
void renderTickerScreen(const TickerData& ticker, ChartTimeframe timeframe);

// Render a "loading" screen
void renderLoadingScreen(const char* message);

// Render an error screen
void renderErrorScreen(const char* message);

// Draw a sparkline chart
// data: array of uint8_t values (0 = bottom, max = top)
// x, y: top-left corner of chart area
// w, h: width and height of chart area
// positive: true = green tones, false = red tones
void drawSparkline(const uint8_t* data, uint8_t len, int x, int y, int w, int h, bool positive);

// Clear the display
void clearDisplay();
