#include "timer_model.h"

#include "config.h"

using namespace AppConfig;

static void transitionTimerState(TimerModel &timer, TimerState next, uint32_t nowMs) {
  timer.state = next;
  timer.lastSecondTickMs = nowMs;
}

static void handleSecondTick(TimerModel &timer) {
  if (timer.state == TimerState::FwdRun) {
    if (timer.timerSeconds < TIMER_MAX_SECONDS) {
      timer.timerSeconds++;
    }
    return;
  }

  if (timer.state == TimerState::CdRun) {
    if (timer.timerSeconds > 1) {
      timer.timerSeconds--;
    } else {
      timer.timerSeconds = 0;
      timer.state = TimerState::Finished;
    }
  }
}

void timerReset(TimerModel &timer) {
  timer.state = TimerState::Idle;
  timer.settingSeconds = 0;
  timer.timerSeconds = 0;
  timer.lastSecondTickMs = 0;
}

void timerHandleConfirm(TimerModel &timer, uint32_t nowMs) {
  switch (timer.state) {
    case TimerState::Idle:
    case TimerState::Adjusting:
      if (timer.settingSeconds > 0) {
        timer.timerSeconds = timer.settingSeconds;
        transitionTimerState(timer, TimerState::CdRun, nowMs);
      } else {
        timer.timerSeconds = 0;
        transitionTimerState(timer, TimerState::FwdRun, nowMs);
      }
      break;
    case TimerState::FwdRun:
      transitionTimerState(timer, TimerState::FwdPause, nowMs);
      break;
    case TimerState::FwdPause:
      transitionTimerState(timer, TimerState::FwdRun, nowMs);
      break;
    case TimerState::CdRun:
      transitionTimerState(timer, TimerState::CdPause, nowMs);
      break;
    case TimerState::CdPause:
      transitionTimerState(timer, TimerState::CdRun, nowMs);
      break;
    case TimerState::Finished:
      timerReset(timer);
      break;
  }
}

void timerHandleCancel(TimerModel &timer) {
  timerReset(timer);
}

bool timerAdjustSetting(TimerModel &timer, int32_t steps) {
  if (steps == 0) {
    return false;
  }
  if (timer.state != TimerState::Idle && timer.state != TimerState::Adjusting) {
    return false;
  }

  int64_t next = static_cast<int64_t>(timer.settingSeconds) +
                 static_cast<int64_t>(steps) * static_cast<int64_t>(TIMER_STEP_SECONDS);
  if (next < 0) {
    next = 0;
  }
  if (next > TIMER_MAX_SECONDS) {
    next = TIMER_MAX_SECONDS;
  }

  const bool changed = static_cast<uint32_t>(next) != timer.settingSeconds ||
                       timer.state != TimerState::Adjusting;
  timer.settingSeconds = static_cast<uint32_t>(next);
  timer.state = TimerState::Adjusting;
  return changed;
}

bool timerUpdateElapsed(TimerModel &timer, uint32_t nowMs) {
  if (!timerIsRunning(timer)) {
    return false;
  }
  bool changed = false;
  while (static_cast<uint32_t>(nowMs - timer.lastSecondTickMs) >= TIMER_SECOND_MS) {
    timer.lastSecondTickMs += TIMER_SECOND_MS;
    const TimerState oldState = timer.state;
    const uint32_t oldSeconds = timer.timerSeconds;
    handleSecondTick(timer);
    changed = changed || oldState != timer.state || oldSeconds != timer.timerSeconds;
  }
  return changed;
}

bool timerIsRunning(const TimerModel &timer) {
  return timer.state == TimerState::FwdRun || timer.state == TimerState::CdRun;
}

uint32_t timerDisplayedSeconds(const TimerModel &timer) {
  if (timer.state == TimerState::Idle || timer.state == TimerState::Adjusting) {
    return timer.settingSeconds;
  }
  return timer.timerSeconds;
}
