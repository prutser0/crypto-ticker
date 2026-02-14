#include "display_renderer.h"
#include "config.h"

// Static display instance
static MatrixPanel_I2S_DMA* dma_display = nullptr;

// Colors computed at runtime via dma_display->color565()
static uint16_t COLOR_WHITE;
static uint16_t COLOR_GREEN;
static uint16_t COLOR_RED;
static uint16_t COLOR_BRIGHT_GREEN;
static uint16_t COLOR_BRIGHT_RED;
static uint16_t COLOR_DIM_GRAY;

static void initColors() {
    COLOR_WHITE       = dma_display->color565(255, 255, 255);
    COLOR_GREEN       = dma_display->color565(0, 255, 0);
    COLOR_RED         = dma_display->color565(255, 0, 0);
    COLOR_BRIGHT_GREEN = dma_display->color565(180, 255, 180);
    COLOR_BRIGHT_RED   = dma_display->color565(255, 180, 180);
    COLOR_DIM_GRAY    = dma_display->color565(60, 60, 60);
}

// ============================================================
// Custom 5x7 pixel font - 5px wide, 7px tall, 6px advance
// Each char: 7 rows, each row is 5 bits (bit4=left, bit0=right)
// ============================================================
static const uint8_t FONT5X7[][7] = {
    // Index 0: space
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // Index 1: '!'
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    // Index 2: '$'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    // Index 3: '%'
    {0x19,0x1A,0x02,0x04,0x08,0x0B,0x13},
    // Index 4: '+'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    // Index 5: '-'
    {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
    // Index 6: '.'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    // Index 7: '/'
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    // Index 8-17: '0'-'9'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x11,0x01,0x02,0x04,0x04,0x04}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    // Index 18-43: 'A'-'Z'
    {0x04,0x0A,0x11,0x11,0x1F,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x13,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x19,0x15,0x13,0x13,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

// Map ASCII char to font index
static int fontIndex(char c) {
    if (c == ' ')  return 0;
    if (c == '!')  return 1;
    if (c == '$')  return 2;
    if (c == '%')  return 3;
    if (c == '+')  return 4;
    if (c == '-')  return 5;
    if (c == '.')  return 6;
    if (c == '/')  return 7;
    if (c >= '0' && c <= '9') return 8 + (c - '0');
    if (c >= 'A' && c <= 'Z') return 18 + (c - 'A');
    return -1;
}

// Draw single 5x7 character at (x,y) top-left, returns next x
static int drawChar(int x, int y, char c, uint16_t color, int advance = 6) {
    int idx = fontIndex(c);
    if (idx < 0) return x + advance;
    if (c == ' ') return x + advance;
    const uint8_t* glyph = FONT5X7[idx];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                dma_display->drawPixel(x + col, y + row, color);
            }
        }
    }
    return x + advance;
}

// Draw text string, returns x after last char
static int drawText(int x, int y, const char* text, uint16_t color, int advance = 6) {
    while (*text) {
        if (x + 5 > 64) break;
        x = drawChar(x, y, *text, color, advance);
        text++;
    }
    return x;
}

// Calculate pixel width of text string
static int textWidth(const char* text, int advance = 6) {
    int len = strlen(text);
    if (len == 0) return 0;
    return len * advance - 1;
}

// Get advance for price char (tight on both sides of '.')
static int priceAdv(char c, char next) {
    if (c == '.') return 4;      // dot itself tight
    if (next == '.') return 4;   // char before dot tight
    return 6;
}

// Calculate pixel width of price string (tighter '.' spacing)
static int priceWidth(const char* text) {
    int len = strlen(text);
    if (len == 0) return 0;
    int w = 0;
    for (int i = 0; i < len - 1; i++) {
        w += priceAdv(text[i], text[i + 1]);
    }
    w += 5; // last char: just char width, no trailing gap
    return w;
}

// Draw price string with tighter '.' spacing
static int drawPrice(int x, int y, const char* text, uint16_t color) {
    while (*text) {
        char next = *(text + 1);
        int adv = next ? priceAdv(*text, next) : 6;
        if (x + 5 > 64) break;
        x = drawChar(x, y, *text, color, adv);
        text++;
    }
    return x;
}

// ============================================================

bool initDisplay(uint8_t brightness) {
    if (dma_display) {
        dma_display->setBrightness8(brightness);
        dma_display->clearScreen();
        return true;
    }

    HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT);

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

    mxconfig.driver = HUB75_I2S_CFG::SHIFTREG;
    mxconfig.setPixelColorDepthBits(3);
    mxconfig.double_buff = true;
    mxconfig.latch_blanking = 4;
    mxconfig.clkphase = false;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    if (!dma_display) return false;

    if (!dma_display->begin()) {
        delete dma_display;
        dma_display = nullptr;
        return false;
    }

    initColors();
    dma_display->setBrightness8(brightness);
    dma_display->clearScreen();
    dma_display->flipDMABuffer();

    return true;
}

void setDisplayBrightness(uint8_t brightness) {
    if (dma_display) dma_display->setBrightness8(brightness);
}

MatrixPanel_I2S_DMA* getDisplay() { return dma_display; }

void clearDisplay() {
    if (dma_display) dma_display->clearScreen();
}

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

    // 5x7 font, 6px advance
    // Layout for 64x32:
    //   Row 0-6:   Symbol (left) + Price (right)
    //   Row 8-14:  Change% (left) + Timeframe (right)
    //   Row 16-31: Sparkline (16 rows)

    // Line 1: Symbol left, Price right
    drawText(0, 0, ticker.symbol, COLOR_WHITE);

    char priceStr[16];
    formatPrice(ticker.currentPrice, priceStr, sizeof(priceStr));
    int priceW = priceWidth(priceStr);
    drawPrice(63 - priceW, 0, priceStr, COLOR_WHITE);

    // Use per-timeframe change% (from CMC API), fallback to 24h
    const SparklineData& sparkline = ticker.sparklines[timeframe];
    float changePercent = ticker.priceChange[timeframe];
    if (changePercent == 0.0f && timeframe != TIMEFRAME_24H) {
        changePercent = ticker.priceChange24h;
    }

    // Line 2: Change% (left, tight 5px advance) + Timeframe (right)
    bool isPositive = changePercent >= 0;
    uint16_t changeColor = isPositive ? COLOR_GREEN : COLOR_RED;

    char changeStr[16];
    snprintf(changeStr, sizeof(changeStr), "%s%.1f%%",
             isPositive ? "+" : "", changePercent);

    // Draw change% with tight spacing around +/- . and %
    {
        int cx = 0;
        const char* p = changeStr;
        while (*p) {
            char next = *(p + 1);
            int adv = 5; // default tight for digits
            if (*p == '+' || *p == '-') adv = 4;   // sign tight
            if (*p == '.') adv = 4;                  // dot tight
            if (next == '.' || next == '%') adv = 4; // before dot/% tight
            if (!next) adv = 6;                      // last char
            if (cx + 5 > 64) break;
            cx = drawChar(cx, 8, *p, changeColor, adv);
            p++;
        }
    }

    const char* tfLabel = getTimeframeLabel(timeframe);
    int tfW = textWidth(tfLabel);
    drawText(63 - tfW, 8, tfLabel, changeColor);

    // Sparkline: row 16-31 (16 rows)
    if (sparkline.valid && sparkline.len > 0) {
        drawSparkline(sparkline.points, sparkline.len,
                     0, 16, 64, 16, isPositive);
    }

    dma_display->flipDMABuffer();
}

void renderLoadingScreen(const char* message) {
    if (!dma_display) return;
    dma_display->clearScreen();
    drawText(1, 12, message, COLOR_WHITE);
    dma_display->flipDMABuffer();
}

void renderErrorScreen(const char* message) {
    if (!dma_display) return;
    dma_display->clearScreen();
    drawText(1, 2, "ERROR", COLOR_RED);
    drawText(1, 16, message, COLOR_WHITE);
    dma_display->flipDMABuffer();
}

void drawSparkline(const uint8_t* data, uint8_t len, int x, int y, int w, int h, bool positive) {
    if (!dma_display || !data || len == 0 || w == 0 || h == 0) return;

    // Line (the curve): full brightness
    uint16_t lineColor = positive ?
        dma_display->color565(0, 255, 0) :
        dma_display->color565(255, 0, 0);

    // Fill (area under curve): dimmer
    uint16_t fillColor = positive ?
        dma_display->color565(0, 128, 0) :
        dma_display->color565(128, 0, 0);

    uint8_t minVal = 255;
    uint8_t maxVal = 0;
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < minVal) minVal = data[i];
        if (data[i] > maxVal) maxVal = data[i];
    }

    uint8_t range = maxVal - minVal;
    if (range == 0) range = 1;

    // Pass 1: fill area under curve
    for (int i = 0; i < w; i++) {
        int dataIdx = (i * len) / w;
        if (dataIdx >= len) dataIdx = len - 1;

        int scaledValue = ((data[dataIdx] - minVal) * (h - 1)) / range;
        int pixelY = y + h - 1 - scaledValue;

        for (int fillY = y + h - 1; fillY > pixelY; fillY--) {
            dma_display->drawPixel(x + i, fillY, fillColor);
        }
    }

    // Pass 2: bright line on top (connecting lines + data points)
    for (int i = 0; i < w; i++) {
        int dataIdx = (i * len) / w;
        if (dataIdx >= len) dataIdx = len - 1;

        int scaledValue = ((data[dataIdx] - minVal) * (h - 1)) / range;
        int pixelY = y + h - 1 - scaledValue;

        // Draw data point
        dma_display->drawPixel(x + i, pixelY, lineColor);

        // Draw connecting line to previous point
        if (i > 0) {
            int prevDataIdx = ((i - 1) * len) / w;
            if (prevDataIdx >= len) prevDataIdx = len - 1;
            int prevScaledValue = ((data[prevDataIdx] - minVal) * (h - 1)) / range;
            int prevPixelY = y + h - 1 - prevScaledValue;
            dma_display->drawLine(x + i - 1, prevPixelY, x + i, pixelY, lineColor);
        }
    }
}
