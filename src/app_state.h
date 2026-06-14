#pragma once

#include <Arduino.h>

#include "config.h"
#include "rtc.h"
#include "timer_model.h"

enum class AppMode : uint8_t {
  Clock,
  Timer,
  Setting,
};

enum class SettingState : uint8_t {
  SettingMenu,
  BrightnessEdit,
  TimeEditHour,
  TimeEditMinute,
};

enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
};

enum class RtcAutoInitState : uint8_t {
  Idle,
  Waiting,
  Writing,
  Failed,
};

enum class ButtonId : uint8_t {
  Mode,
  Confirm,
  Cancel,
};

enum class ButtonEventType : uint8_t {
  Pressed,
  ShortReleased,
  LongPressed,
};

struct UiConfig {
  uint8_t brightnessLevel = AppConfig::DEFAULT_BRIGHTNESS_LEVEL;
};

struct SettingModel {
  SettingState state = SettingState::SettingMenu;
  SettingMenuItem selectedItem = SettingMenuItem::Brightness;
  uint8_t editHour = 0;
  uint8_t editMinute = 0;
  bool showBlinkField = true;
  uint32_t lastBlinkToggleMs = 0;
  bool timeSetErrorVisible = false;
  char timeSetError[AppConfig::LINE_CACHE_LEN] = {};
};

struct AppState {
  AppMode mode = AppMode::Clock;
  TimerModel timer;
  SettingModel setting;
  UiConfig config;
  RtcTime rtcTime;
  bool rtcOk = false;
  bool displayDirty = true;
};
