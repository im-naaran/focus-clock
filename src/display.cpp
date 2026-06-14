#include "display.h"

#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

using namespace AppConfig;

static constexpr uint8_t GLYPH_HALF_BITS = 4;

enum class LineStyle : uint8_t {
  Invalid,
  Left,
  Center,
  ScaledTop,
  ScaledContinuation,
  Dialog,
};

static char oledLineCache[OLED_PAGE_COUNT][LINE_CACHE_LEN] = {};
static LineStyle oledLineStyle[OLED_PAGE_COUNT] = {};
static uint8_t oledLineScale[OLED_PAGE_COUNT] = {};

static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

static void oledCommand(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

static void oledData(uint8_t data) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  Wire.write(data);
  Wire.endTransmission();
}

static void oledSetCursor(uint8_t col, uint8_t page) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  oledCommand(0xB0 + page);
  oledCommand(0x00 + (col & 0x0F));
  oledCommand(0x10 + (col >> 4));
}

static void oledClearLine(uint8_t page) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  oledSetCursor(0, page);
  for (uint8_t col = 0; col < OLED_WIDTH_PX; ++col) {
    oledData(0x00);
  }
}

static const uint8_t *resolveGlyph(char c) {
  if (c >= 'a' && c <= 'z') {
    c -= 32;
  }
  if (c >= ' ' && c <= 'Z') {
    return font5x7[c - ' '];
  }
  return font5x7[0];
}

static void oledPrintGlyphSpan(const char *text, uint8_t scale, uint8_t bitOffset) {
  while (*text != '\0') {
    const uint8_t *glyph = resolveGlyph(*text++);
    for (uint8_t i = 0; i < GLYPH_WIDTH_PX; ++i) {
      uint8_t expanded = 0;
      for (uint8_t bit = 0; bit < GLYPH_HALF_BITS; ++bit) {
        if (glyph[i] & (1 << (bit + bitOffset))) {
          expanded |= (0x03 << (bit * 2));
        }
      }
      for (uint8_t repeat = 0; repeat < scale; ++repeat) {
        oledData(expanded);
      }
    }
    for (uint8_t repeat = 0; repeat < scale; ++repeat) {
      oledData(0x00);
    }
  }
}

static void oledPrint(uint8_t col, uint8_t page, const char *text) {
  oledSetCursor(col, page);
  while (*text != '\0') {
    const uint8_t *glyph = resolveGlyph(*text++);
    for (uint8_t i = 0; i < GLYPH_WIDTH_PX; ++i) {
      oledData(glyph[i]);
    }
    oledData(0x00);
  }
}

static uint8_t textWidth(const char *text, uint8_t scale = 1) {
  const size_t width = strlen(text) * GLYPH_ADVANCE_PX * scale;
  return width > 255 ? 255 : static_cast<uint8_t>(width);
}

static uint8_t centeredTextCol(const char *text, uint8_t scale = 1) {
  const uint8_t width = textWidth(text, scale);
  return width >= OLED_WIDTH_PX ? 0 : (OLED_WIDTH_PX - width) / 2;
}

static void oledPrintScaled(uint8_t col, uint8_t page, const char *text, uint8_t scale) {
  if (scale <= 1) {
    oledPrint(col, page, text);
    return;
  }
  oledSetCursor(col, page);
  oledPrintGlyphSpan(text, scale, 0);
  oledSetCursor(col, page + 1);
  oledPrintGlyphSpan(text, scale, GLYPH_HALF_BITS);
}

static bool cacheMatches(uint8_t page, LineStyle style, const char *text, uint8_t scale = 1) {
  return page < OLED_PAGE_COUNT &&
         oledLineStyle[page] == style &&
         oledLineScale[page] == scale &&
         strncmp(oledLineCache[page], text, sizeof(oledLineCache[page])) == 0;
}

static void cacheLine(uint8_t page, LineStyle style, const char *text, uint8_t scale = 1) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  oledLineStyle[page] = style;
  oledLineScale[page] = scale;
  snprintf(oledLineCache[page], sizeof(oledLineCache[page]), "%s", text);
}

static void invalidateLine(uint8_t page) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  oledLineCache[page][0] = '\0';
  oledLineStyle[page] = LineStyle::Invalid;
  oledLineScale[page] = 0;
}

void displayBegin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(OLED_I2C_CLOCK_HZ);
  delay(100);
  const uint8_t init[] = {
      0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
      0x81, brightnessLevelToContrast(DEFAULT_BRIGHTNESS_LEVEL), 0xA1, 0xA6,
      0xA8, 0x3F, 0xA4, 0xD3, 0x00, 0xD5, 0x80, 0xD9,
      0xF1, 0xDA, 0x12, 0xDB, 0x40, 0x8D, 0x14, 0xAF,
  };
  for (uint8_t cmd : init) {
    oledCommand(cmd);
  }
}

void displayClear() {
  for (uint8_t page = 0; page < OLED_PAGE_COUNT; ++page) {
    oledClearLine(page);
  }
}

void displayInvalidateCache() {
  for (uint8_t page = 0; page < OLED_PAGE_COUNT; ++page) {
    invalidateLine(page);
  }
}

void displaySetContrast(uint8_t contrast) {
  oledCommand(0x81);
  oledCommand(contrast);
}

void displayPrintLine(uint8_t page, const char *text) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  char clipped[LINE_CACHE_LEN] = {};
  snprintf(clipped, sizeof(clipped), "%s", text != nullptr ? text : "");
  if (cacheMatches(page, LineStyle::Left, clipped)) {
    return;
  }
  oledClearLine(page);
  oledPrint(0, page, clipped);
  cacheLine(page, LineStyle::Left, clipped);
}

void displayPrintLineCentered(uint8_t page, const char *text) {
  if (page >= OLED_PAGE_COUNT) {
    return;
  }
  char clipped[LINE_CACHE_LEN] = {};
  snprintf(clipped, sizeof(clipped), "%s", text != nullptr ? text : "");
  if (cacheMatches(page, LineStyle::Center, clipped)) {
    return;
  }
  oledClearLine(page);
  oledPrint(centeredTextCol(clipped), page, clipped);
  cacheLine(page, LineStyle::Center, clipped);
}

void displayPrintScaledLineCentered(uint8_t page, const char *text, uint8_t scale) {
  if (page + 1 >= OLED_PAGE_COUNT) {
    return;
  }
  char clipped[LINE_CACHE_LEN] = {};
  snprintf(clipped, sizeof(clipped), "%s", text != nullptr ? text : "");
  if (cacheMatches(page, LineStyle::ScaledTop, clipped, scale) &&
      oledLineStyle[page + 1] == LineStyle::ScaledContinuation &&
      oledLineScale[page + 1] == scale) {
    return;
  }
  oledClearLine(page);
  oledClearLine(page + 1);
  oledPrintScaled(centeredTextCol(clipped, scale), page, clipped, scale);
  cacheLine(page, LineStyle::ScaledTop, clipped, scale);
  cacheLine(page + 1, LineStyle::ScaledContinuation, "", scale);
}

void displayDrawDialog(const char *message) {
  static constexpr uint8_t TOP_PAGE = 2;
  static constexpr uint8_t BOTTOM_PAGE = 5;
  const char *top = "+-------------------+";
  const char *bottom = "+-------------------+";
  char clipped[LINE_CACHE_LEN] = {};
  snprintf(clipped, sizeof(clipped), "%s", message != nullptr ? message : "");

  for (uint8_t page = TOP_PAGE; page <= BOTTOM_PAGE; ++page) {
    oledClearLine(page);
    invalidateLine(page);
  }
  oledPrint(centeredTextCol(top), TOP_PAGE, top);
  oledPrint(centeredTextCol(clipped), TOP_PAGE + 1, clipped);
  oledPrint(centeredTextCol(bottom), BOTTOM_PAGE, bottom);
  cacheLine(TOP_PAGE, LineStyle::Dialog, top);
  cacheLine(TOP_PAGE + 1, LineStyle::Dialog, clipped);
  cacheLine(TOP_PAGE + 2, LineStyle::Dialog, "");
  cacheLine(BOTTOM_PAGE, LineStyle::Dialog, bottom);
}
