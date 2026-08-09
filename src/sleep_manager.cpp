#include "sleep_manager.h"

#include <driver/gpio.h>
#include <esp_sleep.h>

#include "config.h"
#include "feedback.h"
#include "input.h"
#include "timer_model.h"

using namespace AppConfig;

static constexpr uint8_t BUTTON_INPUT_PINS[] = {
    PIN_KNOB_X,
    PIN_MODE,
    PIN_CANCEL,
};

static constexpr uint8_t KNOB_ROTATION_INPUT_PINS[] = {
    PIN_KNOB_V,
    PIN_KNOB_W,
};

static bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

static bool detectWakeButton(ButtonId &button) {
  if (inputButtonHeldLow(ButtonId::Mode)) {
    button = ButtonId::Mode;
    return true;
  }
  if (inputButtonHeldLow(ButtonId::Confirm)) {
    button = ButtonId::Confirm;
    return true;
  }
  if (inputButtonHeldLow(ButtonId::Cancel)) {
    button = ButtonId::Cancel;
    return true;
  }
  return false;
}

static uint64_t sleepTimerWakeupUs(const RtcServiceState &rtcService, uint32_t nowMs) {
  const uint32_t nextReadDueMs = rtcServiceNextReadDueMs(rtcService);
  uint32_t sleepMs = static_cast<uint32_t>(nextReadDueMs - nowMs);
  if (timeReached(nowMs, nextReadDueMs) || sleepMs < LIGHT_SLEEP_MIN_TIMER_MS) {
    sleepMs = LIGHT_SLEEP_MIN_TIMER_MS;
  }
  return static_cast<uint64_t>(sleepMs) * 1000ULL;
}

static void setupWakeupSources(const RtcServiceState &rtcService, uint32_t nowMs) {
  esp_sleep_enable_timer_wakeup(sleepTimerWakeupUs(rtcService, nowMs));
  for (uint8_t pin : BUTTON_INPUT_PINS) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(pin), GPIO_INTR_LOW_LEVEL);
  }
  for (uint8_t pin : KNOB_ROTATION_INPUT_PINS) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(pin),
                       digitalRead(pin) == HIGH ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
}

static bool canEnterClockLightSleep(const SleepManagerState &sleepState,
                                    const AppState &app,
                                    uint32_t nowMs) {
  return ENABLE_CLOCK_LIGHT_SLEEP &&
         app.mode == AppMode::Clock &&
         !timerIsRunning(app.timer) &&
         !app.displayDirty &&
         !inputAnyButtonHeldLow() &&
         !inputHasPendingDebounce() &&
         !feedbackActive(nowMs) &&
         // Active radio work must keep the cooperative loop servicing WiFi.
         !app.wifiRuntime.configModeRunning &&
         !app.wifiRuntime.networkTaskActive &&
         !sleepManagerWakeHoldActive(sleepState, nowMs);
}

void sleepManagerBegin() {
}

void sleepManagerMaybeEnter(SleepManagerState &sleepState,
                            const AppState &app,
                            const RtcServiceState &rtcService,
                            uint32_t nowMs) {
  if (!canEnterClockLightSleep(sleepState, app, nowMs)) {
    delay(5);
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  setupWakeupSources(rtcService, nowMs);
  const uint8_t knobSleepState = inputKnobRotationState();
  esp_light_sleep_start();

  const uint32_t wokeMs = millis();
  if (!inputAnyButtonHeldLow()) {
    inputHandleKnobWakeEdge(knobSleepState, micros());
    sleepState.wakeHoldUntilMs = wokeMs + WAKE_INPUT_HOLD_MS;
    return;
  }

  inputSyncKnobRotationState();

  sleepState.wakeHoldUntilMs = wokeMs + WAKE_INPUT_HOLD_MS;

  ButtonId button = ButtonId::Mode;
  if (!detectWakeButton(button)) {
    return;
  }

  inputPrimeWakeButton(button, wokeMs);
  if (button == ButtonId::Mode) {
    sleepState.wakeButtonLatched = false;
    sleepState.pendingButtonPress = true;
    sleepState.pendingButton = ButtonId::Mode;
    return;
  }

  sleepState.pendingButton = button;
  sleepState.pendingButtonPress = true;
}

bool sleepManagerPopPendingButton(SleepManagerState &sleepState, ButtonId &button, uint32_t nowMs) {
  if (!sleepState.pendingButtonPress) {
    return false;
  }
  if (sleepState.wakeButtonLatched) {
    return false;
  }
  if (static_cast<uint32_t>(nowMs - sleepState.lastWakeButtonPressMs) <
      WAKE_BUTTON_REPEAT_GUARD_MS) {
    sleepState.pendingButtonPress = false;
    sleepState.wakeButtonLatched = true;
    return false;
  }

  button = sleepState.pendingButton;
  sleepState.pendingButtonPress = false;
  sleepState.wakeButtonLatched = true;
  sleepState.lastWakeButtonPressMs = nowMs;
  return true;
}

void sleepManagerUpdateButtonRelease(SleepManagerState &sleepState) {
  if (!inputButtonHeldLow(sleepState.pendingButton)) {
    sleepState.wakeButtonLatched = false;
  }
}

bool sleepManagerWakeHoldActive(const SleepManagerState &sleepState, uint32_t nowMs) {
  return sleepState.wakeHoldUntilMs != 0 && !timeReached(nowMs, sleepState.wakeHoldUntilMs);
}
