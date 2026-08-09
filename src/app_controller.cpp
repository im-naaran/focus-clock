#include "app_controller.h"

#include <stdio.h>

#include "config.h"
#include "display.h"
#include "persistence.h"
#include "rtc.h"
#include "timer_model.h"

using namespace AppConfig;

static void invalidatePageLayout(AppState &app) {
  displayClear();
  displayInvalidateCache();
  app.displayDirty = true;
}

static void enterMode(AppState &app, AppMode mode) {
  if (app.mode == mode) {
    return;
  }
  app.mode = mode;
  invalidatePageLayout(app);
}

static void enterSetting(AppState &app, uint32_t nowMs) {
  app.mode = AppMode::Setting;
  app.setting.state = SettingState::SettingMenu;
  app.setting.showBlinkField = true;
  app.setting.lastBlinkToggleMs = nowMs;
  app.setting.timeSetErrorVisible = false;
  app.setting.timeSetError[0] = '\0';
  invalidatePageLayout(app);
}

static int wrapValue(int value, int range) {
  if (range <= 0) {
    return 0;
  }
  value %= range;
  if (value < 0) {
    value += range;
  }
  return value;
}

static void handleModeShort(AppState &app) {
  if (app.mode == AppMode::Clock) {
    enterMode(app, AppMode::Timer);
    return;
  }
  if (app.mode == AppMode::Timer) {
    if (app.timer.state == TimerState::Finished) {
      timerReset(app.timer);
    }
    enterMode(app, AppMode::Clock);
  }
}

static void handleModeLong(AppState &app, uint32_t nowMs) {
  if (app.mode == AppMode::Clock || app.mode == AppMode::Timer) {
    enterSetting(app, nowMs);
  }
}

static void enterTimeEdit(AppState &app, uint32_t nowMs) {
  app.setting.state = SettingState::TimeEditHour;
  app.setting.editHour = app.rtcOk && app.rtcTime.valid ? app.rtcTime.hour : 0;
  app.setting.editMinute = app.rtcOk && app.rtcTime.valid ? app.rtcTime.minute : 0;
  app.setting.showBlinkField = true;
  app.setting.lastBlinkToggleMs = nowMs;
  app.setting.timeSetErrorVisible = false;
  app.setting.timeSetError[0] = '\0';
  invalidatePageLayout(app);
}

static void enterNightOffEdit(AppState &app, uint32_t nowMs) {
  app.setting.state = SettingState::NightOffEnabledEdit;
  app.setting.editNightOffEnabled = app.config.nightScreenOffEnabled;
  app.setting.editNightOffHour = app.config.nightScreenOffMinute / 60;
  app.setting.editNightOffMinute = app.config.nightScreenOffMinute % 60;
  app.setting.editNightOnHour = app.config.nightScreenOnMinute / 60;
  app.setting.editNightOnMinute = app.config.nightScreenOnMinute % 60;
  app.setting.showBlinkField = true;
  app.setting.lastBlinkToggleMs = nowMs;
  invalidatePageLayout(app);
}

static void enterWifiPolicyEdit(AppState &app) {
  app.setting.state = SettingState::WifiPolicyEdit;
  app.setting.editWifiPolicy = app.networkConfig.policy;
  app.setting.wifiPolicySaveErrorVisible = false;
  invalidatePageLayout(app);
}

static uint16_t minuteOfDay(uint8_t hour, uint8_t minute) {
  return static_cast<uint16_t>(hour) * 60 + minute;
}

static void saveNightOffConfig(AppState &app) {
  app.config.nightScreenOffEnabled = app.setting.editNightOffEnabled;
  app.config.nightScreenOffMinute = minuteOfDay(app.setting.editNightOffHour,
                                                app.setting.editNightOffMinute);
  app.config.nightScreenOnMinute = minuteOfDay(app.setting.editNightOnHour,
                                               app.setting.editNightOnMinute);

  NightScreenOffConfig config;
  config.enabled = app.config.nightScreenOffEnabled;
  config.offMinute = app.config.nightScreenOffMinute;
  config.onMinute = app.config.nightScreenOnMinute;
  persistenceSaveNightScreenOff(config);
}

static void setTimeEditError(AppState &app, const char *message) {
  app.setting.timeSetErrorVisible = true;
  snprintf(app.setting.timeSetError, sizeof(app.setting.timeSetError), "%s", message);
  app.displayDirty = true;
}

static void handleSettingConfirm(AppState &app, RtcServiceState &rtcService, uint32_t nowMs) {
  switch (app.setting.state) {
    case SettingState::SettingMenu:
      switch (app.setting.selectedItem) {
        case SettingMenuItem::Brightness:
          app.setting.state = SettingState::BrightnessEdit;
          invalidatePageLayout(app);
          break;
        case SettingMenuItem::TimeSet:
          enterTimeEdit(app, nowMs);
          break;
        case SettingMenuItem::NightScreenOff:
          enterNightOffEdit(app, nowMs);
          break;
        case SettingMenuItem::WifiConfig:
          // The menu Confirm is the local authorization for this runtime-only AP.
          app.configModeRequested = true;
          app.setting.state = SettingState::WifiConfigPortal;
          invalidatePageLayout(app);
          break;
        case SettingMenuItem::WifiPolicy:
          enterWifiPolicyEdit(app);
          break;
      }
      break;
    case SettingState::BrightnessEdit:
      break;
    case SettingState::TimeEditHour:
      app.setting.state = SettingState::TimeEditMinute;
      app.setting.showBlinkField = true;
      app.setting.lastBlinkToggleMs = nowMs;
      app.setting.timeSetErrorVisible = false;
      app.displayDirty = true;
      break;
    case SettingState::TimeEditMinute:
      if (!app.rtcOk || !app.rtcTime.valid) {
        setTimeEditError(app, "RTC FAIL");
        break;
      }
      {
        RtcTime next = app.rtcTime;
        next.hour = app.setting.editHour;
        next.minute = app.setting.editMinute;
        next.second = 0;
        if (rtcSetTime(next)) {
          rtcServiceForceRead(rtcService, app, nowMs);
          app.setting.state = SettingState::SettingMenu;
          app.setting.timeSetErrorVisible = false;
          app.setting.timeSetError[0] = '\0';
          invalidatePageLayout(app);
        } else {
          setTimeEditError(app, "RTC WRITE FAIL");
        }
      }
      break;
    case SettingState::NightOffEnabledEdit:
      app.setting.state = SettingState::NightOffStartHourEdit;
      app.setting.showBlinkField = true;
      app.setting.lastBlinkToggleMs = nowMs;
      app.displayDirty = true;
      break;
    case SettingState::NightOffStartHourEdit:
      app.setting.state = SettingState::NightOffStartMinuteEdit;
      app.setting.showBlinkField = true;
      app.setting.lastBlinkToggleMs = nowMs;
      app.displayDirty = true;
      break;
    case SettingState::NightOffStartMinuteEdit:
      app.setting.state = SettingState::NightOffEndHourEdit;
      app.setting.showBlinkField = true;
      app.setting.lastBlinkToggleMs = nowMs;
      app.displayDirty = true;
      break;
    case SettingState::NightOffEndHourEdit:
      app.setting.state = SettingState::NightOffEndMinuteEdit;
      app.setting.showBlinkField = true;
      app.setting.lastBlinkToggleMs = nowMs;
      app.displayDirty = true;
      break;
    case SettingState::NightOffEndMinuteEdit:
      saveNightOffConfig(app);
      app.setting.state = SettingState::SettingMenu;
      invalidatePageLayout(app);
      break;
    case SettingState::WifiConfigPortal:
      break;
    case SettingState::WifiPolicyEdit: {
      NetworkConfig candidate = app.networkConfig;
      candidate.policy = app.setting.editWifiPolicy;
      if (persistenceSaveNetworkConfig(candidate)) {
        app.networkConfig = candidate;
        app.setting.wifiPolicySaveErrorVisible = false;
        app.setting.state = SettingState::SettingMenu;
        invalidatePageLayout(app);
      } else {
        app.setting.wifiPolicySaveErrorVisible = true;
        app.displayDirty = true;
      }
      break;
    }
  }
}

static void handleSettingCancel(AppState &app) {
  switch (app.setting.state) {
    case SettingState::SettingMenu:
      persistenceSaveBrightness(app.config.brightnessLevel);
      enterMode(app, AppMode::Timer);
      break;
    case SettingState::BrightnessEdit:
      app.setting.state = SettingState::SettingMenu;
      invalidatePageLayout(app);
      break;
    case SettingState::TimeEditHour:
    case SettingState::TimeEditMinute:
    case SettingState::NightOffEnabledEdit:
    case SettingState::NightOffStartHourEdit:
    case SettingState::NightOffStartMinuteEdit:
    case SettingState::NightOffEndHourEdit:
    case SettingState::NightOffEndMinuteEdit:
    case SettingState::WifiPolicyEdit:
      app.setting.state = SettingState::SettingMenu;
      app.setting.timeSetErrorVisible = false;
      app.setting.timeSetError[0] = '\0';
      app.setting.wifiPolicySaveErrorVisible = false;
      invalidatePageLayout(app);
      break;
    case SettingState::WifiConfigPortal:
      // Physical Cancel has priority over any portal client or network activity.
      app.configModeRequested = false;
      app.setting.state = SettingState::SettingMenu;
      invalidatePageLayout(app);
      break;
  }
}

static void handleSettingKnob(AppState &app, int8_t steps) {
  if (steps == 0) {
    return;
  }

  switch (app.setting.state) {
    case SettingState::SettingMenu:
      app.setting.selectedItem = settingMenuMove(app.setting.selectedItem, steps);
      app.displayDirty = true;
      break;
    case SettingState::BrightnessEdit: {
      int next = static_cast<int>(app.config.brightnessLevel) + steps;
      if (next < MIN_BRIGHTNESS_LEVEL) {
        next = MIN_BRIGHTNESS_LEVEL;
      }
      if (next > MAX_BRIGHTNESS_LEVEL) {
        next = MAX_BRIGHTNESS_LEVEL;
      }
      if (static_cast<uint8_t>(next) != app.config.brightnessLevel) {
        app.config.brightnessLevel = static_cast<uint8_t>(next);
        displaySetContrast(brightnessLevelToContrast(app.config.brightnessLevel));
        persistenceSaveBrightness(app.config.brightnessLevel);
        app.displayDirty = true;
      }
      break;
    }
    case SettingState::TimeEditHour:
      app.setting.editHour = static_cast<uint8_t>(wrapValue(app.setting.editHour + steps, 24));
      app.setting.timeSetErrorVisible = false;
      app.displayDirty = true;
      break;
    case SettingState::TimeEditMinute:
      app.setting.editMinute = static_cast<uint8_t>(wrapValue(app.setting.editMinute + steps, 60));
      app.setting.timeSetErrorVisible = false;
      app.displayDirty = true;
      break;
    case SettingState::NightOffEnabledEdit:
      app.setting.editNightOffEnabled = !app.setting.editNightOffEnabled;
      app.displayDirty = true;
      break;
    case SettingState::NightOffStartHourEdit:
      app.setting.editNightOffHour =
          static_cast<uint8_t>(wrapValue(app.setting.editNightOffHour + steps, 24));
      app.displayDirty = true;
      break;
    case SettingState::NightOffStartMinuteEdit:
      app.setting.editNightOffMinute =
          static_cast<uint8_t>(wrapValue(app.setting.editNightOffMinute + steps, 60));
      app.displayDirty = true;
      break;
    case SettingState::NightOffEndHourEdit:
      app.setting.editNightOnHour =
          static_cast<uint8_t>(wrapValue(app.setting.editNightOnHour + steps, 24));
      app.displayDirty = true;
      break;
    case SettingState::NightOffEndMinuteEdit:
      app.setting.editNightOnMinute =
          static_cast<uint8_t>(wrapValue(app.setting.editNightOnMinute + steps, 60));
      app.displayDirty = true;
      break;
    case SettingState::WifiConfigPortal:
      break;
    case SettingState::WifiPolicyEdit:
      app.setting.editWifiPolicy =
          settingWifiPolicyMove(app.setting.editWifiPolicy, steps);
      app.setting.wifiPolicySaveErrorVisible = false;
      app.displayDirty = true;
      break;
  }
}

static void handleButton(AppState &app, RtcServiceState &rtcService, const InputEvent &event, uint32_t nowMs) {
  if (event.button == ButtonId::Mode) {
    if (event.buttonEvent == ButtonEventType::ShortReleased) {
      handleModeShort(app);
    } else if (event.buttonEvent == ButtonEventType::LongPressed) {
      handleModeLong(app, nowMs);
    }
    return;
  }

  if (event.buttonEvent != ButtonEventType::Pressed) {
    return;
  }

  if (event.button == ButtonId::Confirm) {
    if (app.mode == AppMode::Timer) {
      timerHandleConfirm(app.timer, nowMs);
      app.displayDirty = true;
    } else if (app.mode == AppMode::Setting) {
      handleSettingConfirm(app, rtcService, nowMs);
    }
    return;
  }

  if (event.button == ButtonId::Cancel) {
    if (app.mode == AppMode::Timer) {
      timerHandleCancel(app.timer);
      app.displayDirty = true;
    } else if (app.mode == AppMode::Setting) {
      handleSettingCancel(app);
    }
  }
}

void appHandleInput(AppState &app, RtcServiceState &rtcService, const InputEvent &event, uint32_t nowMs) {
  if (event.kind == InputEventKind::KnobStep) {
    if (app.mode == AppMode::Timer) {
      if (timerAdjustSetting(app.timer, event.knobSteps)) {
        app.displayDirty = true;
      }
    } else if (app.mode == AppMode::Setting) {
      handleSettingKnob(app, event.knobSteps);
    }
    return;
  }

  handleButton(app, rtcService, event, nowMs);
}

bool appUpdateSettingBlink(AppState &app, uint32_t nowMs) {
  if (app.mode != AppMode::Setting ||
      (app.setting.state != SettingState::TimeEditHour &&
       app.setting.state != SettingState::TimeEditMinute &&
       app.setting.state != SettingState::NightOffStartHourEdit &&
       app.setting.state != SettingState::NightOffStartMinuteEdit &&
       app.setting.state != SettingState::NightOffEndHourEdit &&
       app.setting.state != SettingState::NightOffEndMinuteEdit)) {
    return false;
  }
  if (static_cast<uint32_t>(nowMs - app.setting.lastBlinkToggleMs) < SETTING_BLINK_MS) {
    return false;
  }
  app.setting.lastBlinkToggleMs = nowMs;
  app.setting.showBlinkField = !app.setting.showBlinkField;
  app.displayDirty = true;
  return true;
}
