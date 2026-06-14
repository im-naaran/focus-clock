#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>

#include "app_controller.h"
#include "app_state.h"
#include "config.h"
#include "display.h"
#include "feedback.h"
#include "input.h"
#include "persistence.h"
#include "rtc.h"
#include "rtc_service.h"
#include "sleep_manager.h"
#include "timer_model.h"
#include "ui_render.h"

using namespace AppConfig;

static AppState app;
static RtcServiceState rtcService;
static SleepManagerState sleepState;

static void disableRadios() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();
}

static InputEvent makePendingButtonEvent(ButtonId button) {
  InputEvent event;
  event.kind = InputEventKind::Button;
  event.button = button;
  event.buttonEvent = ButtonEventType::Pressed;
  event.knobSteps = 0;
  return event;
}

static void flashForInputEvent(const InputEvent &event) {
  if (event.kind == InputEventKind::KnobRaw ||
      event.kind == InputEventKind::KnobRotationStart ||
      event.kind == InputEventKind::KnobRotationEnd ||
      event.kind == InputEventKind::KnobStep) {
    feedbackFlash(FeedbackEvent::Knob);
    return;
  }

  if (event.buttonEvent != ButtonEventType::Pressed) {
    return;
  }
  switch (event.button) {
    case ButtonId::Mode:
      feedbackFlash(FeedbackEvent::Mode);
      break;
    case ButtonId::Confirm:
      feedbackFlash(FeedbackEvent::Confirm);
      break;
    case ButtonId::Cancel:
      feedbackFlash(FeedbackEvent::Cancel);
      break;
  }
}

static void dispatchInputEvent(const InputEvent &event, uint32_t nowMs) {
  flashForInputEvent(event);
  if (event.kind == InputEventKind::KnobRaw ||
      event.kind == InputEventKind::KnobRotationStart ||
      event.kind == InputEventKind::KnobRotationEnd) {
    return;
  }
  appHandleInput(app, rtcService, event, nowMs);
}

static void updateHeldButtonFeedback() {
  if (inputButtonHeldLow(ButtonId::Mode)) {
    feedbackSetHeld(true, FeedbackEvent::Mode);
    return;
  }
  if (inputButtonHeldLow(ButtonId::Confirm)) {
    feedbackSetHeld(true, FeedbackEvent::Confirm);
    return;
  }
  if (inputButtonHeldLow(ButtonId::Cancel)) {
    feedbackSetHeld(true, FeedbackEvent::Cancel);
    return;
  }
  feedbackSetHeld(false, FeedbackEvent::Mode);
}

void setup() {
  setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
  disableRadios();

  Serial.begin(115200);
  delay(300);

  feedbackBegin();
  rtcBegin();
  inputBegin();

  displayBegin();
  displayClear();
  displayInvalidateCache();
  displayPrintLine(0, "BOOTING...");

  app.config.brightnessLevel = persistenceLoadBrightness();
  displaySetContrast(brightnessLevelToContrast(app.config.brightnessLevel));

  const uint32_t nowMs = millis();
  rtcServiceBegin(rtcService, app, nowMs);
  sleepManagerBegin();
  app.displayDirty = true;

  Serial.printf("focus-clock ready, CPU %u MHz\n", getCpuFrequencyMhz());
}

void loop() {
  const uint32_t nowMs = millis();

  inputUpdate(nowMs);
  updateHeldButtonFeedback();

  ButtonId pendingButton = ButtonId::Mode;
  if (sleepManagerPopPendingButton(sleepState, pendingButton, nowMs)) {
    dispatchInputEvent(makePendingButtonEvent(pendingButton), nowMs);
  }

  InputEvent event;
  while (inputPopEvent(event)) {
    dispatchInputEvent(event, nowMs);
  }

  if (timerUpdateElapsed(app.timer, nowMs)) {
    app.displayDirty = true;
  }

  rtcServiceUpdate(rtcService, app, nowMs);
  appUpdateSettingBlink(app, nowMs);

  if (app.displayDirty) {
    renderApp(app, rtcServiceStatusText(rtcService, app));
    app.displayDirty = false;
  }

  feedbackUpdate(nowMs);
  sleepManagerUpdateButtonRelease(sleepState);
  sleepManagerMaybeEnter(sleepState, app, nowMs);
}
