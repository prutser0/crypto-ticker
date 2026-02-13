#include "display_renderer.h"
#include "config.h"
#include <Fonts/Picopixel.h>

// Static display instance
static MatrixPanel_I2S_DMA* dma_display = nullptr;

// Color definitions (RGB565 format)
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_GREEN = dma_display ? dma_display->color565(0, 255, 0) : 0x07E0;
static const uint16_t COLOR_RED = dma_display ? dma_display->color565(255, 0, 0) : 0xF800;
static const uint16_t COLOR_DIM_GRAY = dma_display ? dma_display->color565(64, 64, 64) : 0x2104;
static const uint16_t COLOR_DARK_GRAY = dma_display ? dma_display->color565(32, 32, 32) : 0x1082;

bool initDisplay(uint8_t brightness) {
    // Configure the HUB75 matrix panel
    HUB75_I2S_CFG mxconfig(
        PANEL_WIDTH,   // Module width (from config.h)
        PANEL_HEIGHT   // Module height (from config.h)
    );

    // Configure pins
    mxconfig.gpio.r1 = R1_PIN;
    mxconfig.gpio.g1 = G1_PIN;
    mxconfig.gpio.b1 = B1_PIN;
    mxconfig.gpio.r2 = R2_PIN;
    mxconfig.gpio.g2 = G2_PIN;
    mxconfig.gpio.b2 = B2_PIN;
    mxconfig.gpio.a = A_PIN;
    mxconfig.gpio.b = B_PIN;
    mxconfig.gpio.c = C_PIN;
    mxconfig.gpio.d = D_PIN;
    mxconfig.gpio.lat = LAT_PIN;
    mxconfig.gpio.oe = OE_PIN;
    mxconfig.gpio.clk = CLK_PIN;

    // Create display instance
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display) {
        return false;
    }

    // Initialize the display
    if (!dma_display->begin()) {
        delete dma_display;
        dma_display = nullptr;
        return false;
    }

    // Set brightness
    dma_display->setBrightness8(brightness);

    // Clear display
    dma_display->clearScreen();

    // Set font to Picopixel
    dma_display->setFont(&Picopixel);

    return true;
}

void setDisplayBrightness(uint8_t brightness) {
    if (dma_display) {
        dma_display->setBrightness8(brightness);
    }
}

MatrixPanel_I2S_DMA* getDisplay() {
    return dma_display;
}

void clearDisplay() {
    if (dma_display) {
        dma_display->clearScreen();
    }
}

// Helper function to format price based on magnitude
void formatPrice(float price, char* buffer, size_t bufferSize) {
    if (price >= 10000) {
        snprintf(buffer, bufferSize, "$%.0f", price);
    } else if (price >= 100) {
        snprintf(buffer, bufferSize, "$%.1f", price);
    } else if (price >= 1) {
        snprintf(buffer, bufferSize, "$%.2f", price);
    } else {
        snprintf(buffer, bufferSize, "$%.4f", price);
    }
}


void renderTickerScreen(const TickerData& ticker, ChartTimeframe timeframe) {
    if (!dma_display) return;

    dma_display->clearScreen();
    dma_display->setFont(&Picopixel);

    // Row 0-5: Symbol + Price
    // Cursor Y is at baseline, so for top text, set Y to 5 (bottom of text area)
    dma_display->setCursor(0, 5);
    dma_display->setTextColor(COLOR_WHITE);
    dma_display->print(ticker.symbol);

    // Format and display price on the same line
    char priceStr[16];
    formatPrice(ticker.currentPrice, priceStr, sizeof(priceStr));
    dma_display->print(" ");
    dma_display->print(priceStr);

    // Row 6: Separator line
    dma_display->drawFastHLine(0, 6, 64, COLOR_DIM_GRAY);

    // Row 7-9: Change% + timeframe label
    // Position at Y=12 (baseline for second line of text)
    dma_display->setCursor(0, 12);

    // Choose color based on positive/negative change
    bool isPositive = ticker.priceChange24h >= 0;
    uint16_t changeColor = isPositive ? COLOR_GREEN : COLOR_RED;
    dma_display->setTextColor(changeColor);

    // Format change percentage
    char changeStr[16];
    snprintf(changeStr, sizeof(changeStr), "%s%.1f%%",
             isPositive ? "+" : "", ticker.priceChange24h);
    dma_display->print(changeStr);

    // Add timeframe label
    dma_display->print(" ");
    dma_display->print(getTimeframeLabel(timeframe));

    // Row 10-31: Sparkline chart (64 wide x 22 tall)
    const SparklineData& sparkline = ticker.sparklines[timeframe];
    if (sparkline.valid && sparkline.len > 0) {
        drawSparkline(sparkline.points, sparkline.len,
                     0, 10, 64, 22, isPositive);
    }
}

void renderLoadingScreen(const char* message) {
    if (!dma_display) return;

    dma_display->clearScreen();
    dma_display->setFont(&Picopixel);
    dma_display->setTextColor(COLOR_WHITE);

    // Center the message vertically
    dma_display->setCursor(2, 16);
    dma_display->print(message);
}

void renderErrorScreen(const char* message) {
    if (!dma_display) return;

    dma_display->clearScreen();
    dma_display->setFont(&Picopixel);
    dma_display->setTextColor(COLOR_RED);

    // Display "ERROR" at top
    dma_display->setCursor(0, 5);
    dma_display->print("ERROR");

    // Display message below
    dma_display->setCursor(0, 16);
    dma_display->print(message);
}

void drawSparkline(const uint8_t* data, uint8_t len, int x, int y, int w, int h, bool positive) {
    if (!dma_display || !data || len == 0 || w == 0 || h == 0) return;

    // Determine line color
    uint16_t lineColor = positive ?
        dma_display->color565(0, 255, 0) :
        dma_display->color565(255, 0, 0);

    // Dimmed fill color (1/5 brightness)
    uint16_t fillColor = positive ?
        dma_display->color565(0, 51, 0) :
        dma_display->color565(51, 0, 0);

    // Find min and max values for scaling
    uint8_t minVal = 255;
    uint8_t maxVal = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < minVal) minVal = data[i];
        if (data[i] > maxVal) maxVal = data[i];
    }

    // Avoid division by zero
    uint8_t range = maxVal - minVal;
    if (range == 0) range = 1;

    // Draw the sparkline
    for (int i = 0; i < w; i++) {
        // Map pixel position to data index
        int dataIdx = (i * len) / w;
        if (dataIdx >= len) dataIdx = len - 1;

        // Scale data point to chart height
        int scaledValue = ((data[dataIdx] - minVal) * (h - 1)) / range;
        int pixelY = y + h - 1 - scaledValue; // Invert Y (bottom = high value)

        // Draw vertical line from bottom to data point (fill)
        for (int fillY = y + h - 1; fillY > pixelY; fillY--) {
            dma_display->drawPixel(x + i, fillY, fillColor);
        }

        // Draw the line point
        dma_display->drawPixel(x + i, pixelY, lineColor);

        // Connect to previous point if not first pixel
        if (i > 0) {
            int prevDataIdx = ((i - 1) * len) / w;
            if (prevDataIdx >= len) prevDataIdx = len - 1;

            int prevScaledValue = ((data[prevDataIdx] - minVal) * (h - 1)) / range;
            int prevPixelY = y + h - 1 - prevScaledValue;

            // Draw line between points
            dma_display->drawLine(x + i - 1, prevPixelY, x + i, pixelY, lineColor);
        }
    }
}
