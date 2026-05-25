#pragma once

#include <Arduino.h>

namespace AppConfig {

// SSD1306 OLED I2C 引脚。
// 接线：OLED SDA -> GPIO0，OLED SCL -> GPIO1。
static constexpr uint8_t PIN_OLED_SDA = 0;
static constexpr uint8_t PIN_OLED_SCL = 1;

// DS1302 RTC 三线接口引脚。
// CE/RST 为片选/复位，SCLK 为时钟，I/O 为双向数据线。
static constexpr uint8_t PIN_DS1302_CE = 3;
static constexpr uint8_t PIN_DS1302_SCLK = 4;
static constexpr uint8_t PIN_DS1302_IO = 5;

// EC11 旋转编码器引脚。
// A/B 用于旋转方向读取；GPIO8 当前作为确认键输入，内部上拉，按下接 GND。
// 注意：GPIO8 是 ESP32-C3 Strapping 引脚，若出现启动异常，可改用独立确认按钮或重新分配引脚。
static constexpr uint8_t PIN_EC11_A = 6;
static constexpr uint8_t PIN_EC11_B = 7;
static constexpr uint8_t PIN_CONFIRM = 8;

// 独立按钮引脚，统一使用 INPUT_PULLUP，按下接 GND。
static constexpr uint8_t PIN_MODE = 20;
static constexpr uint8_t PIN_CANCEL = 21;

// OLED 默认 I2C 地址与总线频率。
static constexpr uint8_t OLED_ADDR = 0x3C;
static constexpr uint32_t OLED_I2C_CLOCK_HZ = 400000;

// 交互刷新参数。
// BUTTON_DEBOUNCE_MS：按键软件消抖时间。
// RTC_REFRESH_MS：时钟模式下 DS1302 读取周期。
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
static constexpr uint32_t RTC_REFRESH_MS = 1000;

// 低功耗参数。
// CPU_FREQUENCY_MHZ：启动后主频，80MHz 对当前 UI/计时逻辑足够。
// IDLE_LIGHT_SLEEP_US：仅 TIMER 空闲页进入 Light Sleep 的最长睡眠时间。
// WAKE_INPUT_HOLD_MS：GPIO 唤醒后保留短暂清醒窗口，避免短按被消抖采样漏掉。
// WAKE_BUTTON_REPEAT_GUARD_MS：唤醒路径的一次按键重复保护，避免机械抖动多次触发。
static constexpr uint32_t CPU_FREQUENCY_MHZ = 80;
static constexpr uint64_t IDLE_LIGHT_SLEEP_US = 1000000ULL;
static constexpr uint32_t WAKE_INPUT_HOLD_MS = 80;
static constexpr uint32_t WAKE_BUTTON_REPEAT_GUARD_MS = 180;

// 计时器设置参数。
// TIMER_STEP_SECONDS：EC11 每步调整的秒数，当前为 1 分钟。
// TIMER_MAX_SECONDS：允许显示/设置的最大时长，当前为 99:59:59。
// ENCODER_DIRECTION：旋转方向修正；若实测顺/逆时针相反，改为 -1。
// ENCODER_ISR_MIN_US：编码器中断最小间隔过滤，用于压掉机械触点快速抖动。
// ENCODER_QUARTER_STEPS_PER_CLICK：累计多少个有效 A/B 相位变化算一次设置步进。
// 若旋转仍不敏感，可改为 1；若容易一格跳多次，可改回 4。
static constexpr uint32_t TIMER_STEP_SECONDS = 60;
static constexpr uint32_t TIMER_MAX_SECONDS = 99UL * 3600UL + 59UL * 60UL + 59UL;
static constexpr int8_t ENCODER_DIRECTION = 1;
static constexpr uint32_t ENCODER_ISR_MIN_US = 150;
static constexpr int8_t ENCODER_QUARTER_STEPS_PER_CLICK = 2;

}
