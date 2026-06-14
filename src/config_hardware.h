#pragma once

#include <Arduino.h>

namespace AppConfig {

// SSD1306 OLED I2C wiring.
static constexpr uint8_t PIN_OLED_SDA = 1;
static constexpr uint8_t PIN_OLED_SCL = 2;
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint32_t OLED_I2C_CLOCK_HZ = 400000;

// DS1302 RTC three-wire interface.
static constexpr uint8_t PIN_DS1302_CE = 5;
static constexpr uint8_t PIN_DS1302_SCLK = 3;
static constexpr uint8_t PIN_DS1302_IO = 4;

// Leobog-style keyboard knob.
static constexpr uint8_t PIN_KNOB_V = 20;
static constexpr uint8_t PIN_KNOB_W = 7;
static constexpr uint8_t PIN_KNOB_X = 8;
static constexpr int8_t KNOB_STEP_FROM_V_FIRST = 1;

// On-board WS2812/RGB LED.
static constexpr uint8_t PIN_WS2812 = 10;
static constexpr uint8_t WS2812_PIXEL_COUNT = 1;

// Independent active-low buttons.
static constexpr uint8_t PIN_MODE = 21;
static constexpr uint8_t PIN_CANCEL = 6;

// Power and sleep feature switches.
static constexpr uint32_t CPU_FREQUENCY_MHZ = 80;
static constexpr bool ENABLE_CLOCK_LIGHT_SLEEP = true;

}  // namespace AppConfig
