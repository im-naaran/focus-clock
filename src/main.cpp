#include <Arduino.h>
#include <esp_bt.h>

#include "app_controller.h"
#include "app_state.h"
#include "config.h"
#include "display.h"
#include "display_power.h"
#include "feedback.h"
#include "input.h"
#include "persistence.h"
#include "rtc.h"
#include "rtc_service.h"
#include "scheduled_task_service.h"
#include "sleep_manager.h"
#include "time_sync_task.h"
#include "timer_model.h"
#include "ui_render.h"
#include "wifi_portal.h"
#include "wifi_service.h"

using namespace AppConfig;

static AppState app;
static RtcServiceState rtcService;
static SleepManagerState sleepState;
static WifiServiceState wifiService;
static WifiPortalState wifiPortal;
static ScheduledTaskServiceState scheduledTaskService;
static TimeSyncTaskState timeSyncTask;

static void disableBluetooth() {
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
  if (displayPowerHandleInput(app, event, nowMs)) {
    return;
  }
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
  disableBluetooth();

  if (ENABLE_SERIAL_LOGGING) {
    Serial.begin(115200);
    delay(300);
  }

  feedbackBegin();
  rtcBegin();
  inputBegin();

  displayBegin();
  displayClear();
  displayInvalidateCache();
  displayPrintLine(0, "BOOTING...");

  app.config.brightnessLevel = persistenceLoadBrightness();
  {
    const NightScreenOffConfig nightConfig = persistenceLoadNightScreenOff();
    app.config.nightScreenOffEnabled = nightConfig.enabled;
    app.config.nightScreenOffMinute = nightConfig.offMinute;
    app.config.nightScreenOnMinute = nightConfig.onMinute;
  }
  app.networkConfig = persistenceLoadNetworkConfig();
  app.lastTimeSyncSuccessEpoch = persistenceLoadLastTimeSyncSuccessEpoch();
  displaySetContrast(brightnessLevelToContrast(app.config.brightnessLevel));

  const uint32_t nowMs = millis();
  scheduledTaskServiceBegin(scheduledTaskService);
  timeSyncTaskBegin(timeSyncTask);
  wifiServiceBegin(wifiService, nowMs);
  wifiPortalBegin(wifiPortal, app, rtcService, wifiService);
  displayPowerBegin(app, nowMs);
  rtcServiceBegin(rtcService, app, nowMs);
  sleepManagerBegin();
  app.displayDirty = true;

  if (ENABLE_SERIAL_LOGGING) {
    Serial.printf("focus-clock ready, CPU %u MHz\n", getCpuFrequencyMhz());
  }
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

  // Scheduler runs before WiFi so a dispatched plugin can request its consumer
  // in this loop; Time Sync runs after WiFi to observe the latest STA state.
  ScheduledTaskId dispatch;
  if (scheduledTaskServiceUpdate(scheduledTaskService, app.rtcTime, dispatch)) {
    switch (dispatch) {
      case ScheduledTaskId::TimeSync:
        timeSyncTaskStart(timeSyncTask, wifiService, app.networkConfig, nowMs);
        break;
      case ScheduledTaskId::Count:
        break;
    }
  }

  if (wifiServiceUpdate(wifiService, app.networkConfig,
                        app.configModeRequested, app.wifiRuntime, nowMs)) {
    app.displayDirty = true;
  }
  timeSyncTaskUpdate(timeSyncTask, wifiService, app.networkConfig,
                     app.wifiRuntime, rtcService, app, nowMs);
  TaskRunResult taskResult;
  if (timeSyncTaskTakeResult(timeSyncTask, taskResult)) {
    scheduledTaskServiceConsumeResult(
        scheduledTaskService, ScheduledTaskId::TimeSync, taskResult);
  }
  wifiPortalUpdate(wifiPortal, app.configModeRequested, app.wifiRuntime);

  if (app.displayDirty) {
    renderApp(app, rtcServiceStatusText(rtcService, app));
    app.displayDirty = false;
  }

  feedbackUpdate(nowMs);
  sleepManagerUpdateButtonRelease(sleepState);
  displayPowerUpdate(app, sleepState, nowMs);
  sleepManagerMaybeEnter(sleepState, app, rtcService, nowMs);
}
