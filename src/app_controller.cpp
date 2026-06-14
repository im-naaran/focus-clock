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

static void setTimeEditError(AppState &app, const char *message) {
  app.setting.timeSetErrorVisible = true;
  snprintf(app.setting.timeSetError, sizeof(app.setting.timeSetError), "%s", message);
  app.displayDirty = true;
}

static void handleSettingConfirm(AppState &app, RtcServiceState &rtcService, uint32_t nowMs) {
  switch (app.setting.state) {
    case SettingState::SettingMenu:
      if (app.setting.selectedItem == SettingMenuItem::Brightness) {
        app.setting.state = SettingState::BrightnessEdit;
        invalidatePageLayout(app);
      } else {
        enterTimeEdit(app, nowMs);
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
      app.setting.state = SettingState::SettingMenu;
      app.setting.timeSetErrorVisible = false;
      app.setting.timeSetError[0] = '\0';
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
      app.setting.selectedItem = app.setting.selectedItem == SettingMenuItem::Brightness
                                     ? SettingMenuItem::TimeSet
                                     : SettingMenuItem::Brightness;
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
       app.setting.state != SettingState::TimeEditMinute)) {
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
