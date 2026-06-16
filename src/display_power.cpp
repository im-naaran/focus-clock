#include "display_power.h"

#include "config.h"
#include "display.h"
#include "feedback.h"

using namespace AppConfig;

static bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

static uint16_t rtcMinuteOfDay(const RtcTime &time) {
  return static_cast<uint16_t>(time.hour) * 60 + time.minute;
}

static bool rtcInNightWindow(const AppState &app) {
  return app.rtcOk &&
         app.rtcTime.valid &&
         displayPowerMinuteInNightWindow(rtcMinuteOfDay(app.rtcTime),
                                         app.config.nightScreenOffMinute,
                                         app.config.nightScreenOnMinute);
}

static void wakeScreen(AppState &app, uint32_t nowMs, bool manualWake) {
  displayWake();
  app.displayPower.screenOn = true;
  app.displayPower.lastScreenPowerChangeMs = nowMs;
  if (manualWake) {
    app.displayPower.manualWakeUntilMs = nowMs + SCREEN_WAKE_GRACE_MS;
  }
  app.displayDirty = true;

  if (ENABLE_SERIAL_LOGGING) {
    Serial.println(manualWake ? "OLED wake: input" : "OLED wake: policy");
  }
}

static void sleepScreen(AppState &app, uint32_t nowMs) {
  displaySleep();
  app.displayPower.screenOn = false;
  app.displayPower.lastScreenPowerChangeMs = nowMs;

  if (ENABLE_SERIAL_LOGGING) {
    Serial.println("OLED sleep: night idle");
  }
}

static bool canAutoSleep(const AppState &app,
                         const SleepManagerState &sleepState,
                         uint32_t nowMs) {
  const bool userIdleElapsed =
      timeReached(nowMs, app.displayPower.lastUserInputMs + SCREEN_WAKE_GRACE_MS);
  return app.displayPower.screenOn &&
         app.config.nightScreenOffEnabled &&
         app.mode == AppMode::Clock &&
         app.rtcOk &&
         app.rtcTime.valid &&
         rtcInNightWindow(app) &&
         !app.displayDirty &&
         !inputAnyButtonHeldLow() &&
         !inputHasPendingDebounce() &&
         !feedbackActive(nowMs) &&
         !sleepManagerWakeHoldActive(sleepState, nowMs) &&
         userIdleElapsed &&
         (app.displayPower.manualWakeUntilMs == 0 ||
          timeReached(nowMs, app.displayPower.manualWakeUntilMs));
}

static bool shouldWakeByPolicy(const AppState &app) {
  if (app.displayPower.screenOn) {
    return false;
  }
  if (app.mode != AppMode::Clock) {
    return true;
  }
  if (!app.config.nightScreenOffEnabled) {
    return true;
  }
  if (!app.rtcOk || !app.rtcTime.valid) {
    return true;
  }
  return !rtcInNightWindow(app);
}

bool displayPowerMinuteInNightWindow(uint16_t minute, uint16_t offMinute, uint16_t onMinute) {
  if (!isValidMinuteOfDay(minute) ||
      !isValidMinuteOfDay(offMinute) ||
      !isValidMinuteOfDay(onMinute) ||
      offMinute == onMinute) {
    return false;
  }
  if (offMinute < onMinute) {
    return minute >= offMinute && minute < onMinute;
  }
  return minute >= offMinute || minute < onMinute;
}

void displayPowerBegin(AppState &app, uint32_t nowMs) {
  app.displayPower.screenOn = true;
  app.displayPower.lastUserInputMs = nowMs;
  app.displayPower.manualWakeUntilMs = 0;
  app.displayPower.lastScreenPowerChangeMs = nowMs;
  app.displayPower.suppressButtonUntilRelease = false;
  app.displayPower.suppressedButton = ButtonId::Mode;
}

bool displayPowerHandleInput(AppState &app, const InputEvent &event, uint32_t nowMs) {
  app.displayPower.lastUserInputMs = nowMs;

  if (app.displayPower.suppressButtonUntilRelease) {
    if (!inputButtonHeldLow(app.displayPower.suppressedButton)) {
      app.displayPower.suppressButtonUntilRelease = false;
    } else if (event.kind == InputEventKind::Button &&
               event.button == app.displayPower.suppressedButton) {
      return true;
    }
  }

  if (app.displayPower.screenOn) {
    return false;
  }

  wakeScreen(app, nowMs, true);
  if (event.kind == InputEventKind::Button) {
    app.displayPower.suppressButtonUntilRelease = true;
    app.displayPower.suppressedButton = event.button;
  }
  return true;
}

void displayPowerUpdate(AppState &app, const SleepManagerState &sleepState, uint32_t nowMs) {
  if (inputAnyButtonHeldLow() || inputHasPendingDebounce()) {
    app.displayPower.lastUserInputMs = nowMs;
  }

  if (shouldWakeByPolicy(app)) {
    wakeScreen(app, nowMs, false);
    return;
  }

  if (canAutoSleep(app, sleepState, nowMs)) {
    sleepScreen(app, nowMs);
  }
}
