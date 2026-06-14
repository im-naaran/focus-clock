#pragma once

#include <Arduino.h>

namespace AppConfig {

// Input timing.
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
static constexpr uint32_t MODE_LONG_PRESS_MS = 3000;
static constexpr uint32_t KNOB_ROTATION_SETTLE_MS = 2;

// Setting UI timing.
static constexpr uint32_t SETTING_BLINK_MS = 500;

// RTC scheduling.
static constexpr uint32_t RTC_SHORT_REFRESH_MS = 1000;
static constexpr uint32_t RTC_NORMAL_MAX_REFRESH_MS = 30000;
static constexpr uint32_t RTC_MINUTE_REFRESH_GRACE_MS = 50;
static constexpr uint32_t RTC_AUTO_INIT_DELAY_MS = 1000;

// Input feedback.
static constexpr uint16_t INPUT_LED_FLASH_MS = 300;
static constexpr uint8_t INPUT_LED_BRIGHTNESS = 8;

// Light Sleep.
static constexpr uint64_t IDLE_LIGHT_SLEEP_US = 1000000ULL;
static constexpr uint32_t WAKE_INPUT_HOLD_MS = 80;
static constexpr uint32_t WAKE_BUTTON_REPEAT_GUARD_MS = 180;

// Timer model.
static constexpr uint32_t TIMER_SECOND_MS = 1000;
static constexpr uint32_t TIMER_STEP_SECONDS = 60;
static constexpr uint32_t TIMER_MAX_SECONDS = 99UL * 3600UL + 59UL * 60UL + 59UL;

}  // namespace AppConfig
