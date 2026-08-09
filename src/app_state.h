#pragma once

#include <Arduino.h>

#include "config.h"
#include "network_types.h"
#include "rtc.h"
#include "setting_logic.h"
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
  NightOffEnabledEdit,
  NightOffStartHourEdit,
  NightOffStartMinuteEdit,
  NightOffEndHourEdit,
  NightOffEndMinuteEdit,
  WifiConfigPortal,
  WifiPolicyEdit,
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
  bool nightScreenOffEnabled = AppConfig::DEFAULT_NIGHT_SCREEN_OFF_ENABLED;
  uint16_t nightScreenOffMinute = AppConfig::DEFAULT_NIGHT_SCREEN_OFF_MINUTE;
  uint16_t nightScreenOnMinute = AppConfig::DEFAULT_NIGHT_SCREEN_ON_MINUTE;
};

struct DisplayPowerState {
  bool screenOn = true;
  uint32_t lastUserInputMs = 0;
  uint32_t manualWakeUntilMs = 0;
  uint32_t lastScreenPowerChangeMs = 0;
  bool suppressButtonUntilRelease = false;
  ButtonId suppressedButton = ButtonId::Mode;
};

struct SettingModel {
  SettingState state = SettingState::SettingMenu;
  SettingMenuItem selectedItem = SettingMenuItem::Brightness;
  uint8_t editHour = 0;
  uint8_t editMinute = 0;
  bool editNightOffEnabled = AppConfig::DEFAULT_NIGHT_SCREEN_OFF_ENABLED;
  uint8_t editNightOffHour = AppConfig::DEFAULT_NIGHT_SCREEN_OFF_MINUTE / 60;
  uint8_t editNightOffMinute = AppConfig::DEFAULT_NIGHT_SCREEN_OFF_MINUTE % 60;
  uint8_t editNightOnHour = AppConfig::DEFAULT_NIGHT_SCREEN_ON_MINUTE / 60;
  uint8_t editNightOnMinute = AppConfig::DEFAULT_NIGHT_SCREEN_ON_MINUTE % 60;
  WifiPolicy editWifiPolicy = WifiPolicy::Off;
  bool wifiPolicySaveErrorVisible = false;
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
  NetworkConfig networkConfig;
  RtcTime rtcTime;
  bool rtcOk = false;
  // Configuration mode is an explicit runtime request and is never persisted.
  bool configModeRequested = false;
  WifiRuntimeView wifiRuntime;
  DisplayPowerState displayPower;
  bool displayDirty = true;
};
