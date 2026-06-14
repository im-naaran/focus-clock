#pragma once

#include <Arduino.h>

enum class TimerState : uint8_t {
  Idle,
  Adjusting,
  FwdRun,
  FwdPause,
  CdRun,
  CdPause,
  Finished,
};

struct TimerModel {
  TimerState state = TimerState::Idle;
  uint32_t settingSeconds = 0;
  uint32_t timerSeconds = 0;
  uint32_t lastSecondTickMs = 0;
};

void timerReset(TimerModel &timer);
void timerHandleConfirm(TimerModel &timer, uint32_t nowMs);
void timerHandleCancel(TimerModel &timer);
bool timerAdjustSetting(TimerModel &timer, int32_t steps);
bool timerUpdateElapsed(TimerModel &timer, uint32_t nowMs);
bool timerIsRunning(const TimerModel &timer);
uint32_t timerDisplayedSeconds(const TimerModel &timer);
