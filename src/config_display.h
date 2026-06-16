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

static constexpr bool DEFAULT_NIGHT_SCREEN_OFF_ENABLED = true;
static constexpr uint16_t MINUTES_PER_DAY = 24 * 60;
static constexpr uint16_t DEFAULT_NIGHT_SCREEN_OFF_MINUTE = 20 * 60;
static constexpr uint16_t DEFAULT_NIGHT_SCREEN_ON_MINUTE = 8 * 60;

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

inline bool isValidMinuteOfDay(uint16_t minute) {
  return minute < MINUTES_PER_DAY;
}

}  // namespace AppConfig
