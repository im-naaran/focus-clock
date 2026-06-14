#pragma once

#include <Arduino.h>

namespace AppConfig {

static constexpr uint8_t OLED_PAGE_COUNT = 8;
static constexpr uint8_t OLED_WIDTH_PX = 128;
static constexpr uint8_t OLED_HEIGHT_PX = 64;
static constexpr uint8_t OLED_PAGE_HEIGHT_PX = 8;

static constexpr uint8_t GLYPH_WIDTH_PX = 5;
static constexpr uint8_t GLYPH_ADVANCE_PX = 6;
static constexpr uint8_t LINE_CACHE_LEN = 22;
static constexpr uint8_t HEADER_LINE_LEN = 22;
static constexpr uint8_t HEADER_TITLE_MAX_CHARS = 15;
static constexpr uint8_t HEADER_TIME_COL = 16;

static constexpr uint8_t DEFAULT_BRIGHTNESS_LEVEL = 3;
static constexpr uint8_t MIN_BRIGHTNESS_LEVEL = 1;
static constexpr uint8_t MAX_BRIGHTNESS_LEVEL = 5;

inline uint8_t brightnessLevelToContrast(uint8_t level) {
  switch (level) {
    case 1:
      return 0x10;
    case 2:
      return 0x30;
    case 3:
      return 0x7F;
    case 4:
      return 0xBF;
    case 5:
      return 0xFF;
    default:
      return 0x7F;
  }
}

inline bool isValidBrightnessLevel(uint8_t level) {
  return level >= MIN_BRIGHTNESS_LEVEL && level <= MAX_BRIGHTNESS_LEVEL;
}

}  // namespace AppConfig
