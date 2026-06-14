#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "rtc_service.h"

struct SleepManagerState {
  uint32_t wakeHoldUntilMs = 0;
  uint32_t lastWakeButtonPressMs = 0;
  bool wakeButtonLatched = false;
  bool pendingButtonPress = false;
  ButtonId pendingButton = ButtonId::Mode;
};

void sleepManagerBegin();
void sleepManagerMaybeEnter(SleepManagerState &sleepState,
                            const AppState &app,
                            const RtcServiceState &rtcService,
                            uint32_t nowMs);
bool sleepManagerPopPendingButton(SleepManagerState &sleepState, ButtonId &button, uint32_t nowMs);
void sleepManagerUpdateButtonRelease(SleepManagerState &sleepState);
bool sleepManagerWakeHoldActive(const SleepManagerState &sleepState, uint32_t nowMs);
