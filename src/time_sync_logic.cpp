#include "time_sync_logic.h"

#include <stdio.h>
#include <time.h>

#include "config_network.h"
#include "config_timing.h"

using namespace AppConfig;

namespace {

bool isLeapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t DAYS[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  if (month < 1 || month > 12) {
    return 0;
  }
  return month == 2 && isLeapYear(year) ? 29 : DAYS[month - 1];
}

TimeSyncActions finish(TimeSyncLogicState &state,
                       TimeSyncPhase phase,
                       TimeSyncFailure failure,
                       uint32_t completedDateKey = 0) {
  TimeSyncActions actions;
  actions.stopSntp = state.sntpActive;
  actions.releaseNetwork = state.ownsNetwork;
  state.sntpActive = false;
  state.ownsNetwork = false;
  state.phase = phase;
  state.failure = failure;
  state.result.status = phase == TimeSyncPhase::Succeeded
                            ? TaskRunStatus::Succeeded
                            : TaskRunStatus::Failed;
  state.result.completedDateKey = completedDateKey;
  state.resultPending = true;
  return actions;
}

TimeSyncActions startAttempt(TimeSyncLogicState &state, uint32_t nowMs) {
  TimeSyncActions actions;
  ++state.attempt;
  state.phase = TimeSyncPhase::WaitingForSntp;
  state.phaseDeadlineMs = nowMs + TIME_SYNC_SNTP_DEADLINE_MS;
  state.sntpActive = true;
  actions.startSntp = true;
  return actions;
}

TimeSyncActions failAttempt(TimeSyncLogicState &state,
                            uint32_t nowMs,
                            TimeSyncFailure finalFailure) {
  TimeSyncActions actions;
  actions.stopSntp = state.sntpActive;
  state.sntpActive = false;
  if (state.attempt >= TIME_SYNC_MAX_ATTEMPTS ||
      timeSyncDeadlineReached(nowMs, state.totalDeadlineMs)) {
    TimeSyncActions terminal =
        finish(state, TimeSyncPhase::Failed, finalFailure);
    terminal.stopSntp = terminal.stopSntp || actions.stopSntp;
    return terminal;
  }
  state.phase = TimeSyncPhase::RetryDelay;
  state.phaseDeadlineMs = nowMs + TIME_SYNC_RETRY_DELAY_MS;
  return actions;
}

}  // namespace

void timeSyncLogicBegin(TimeSyncLogicState &state) {
  state = TimeSyncLogicState{};
}

TimeSyncActions timeSyncLogicStart(TimeSyncLogicState &state,
                                   bool policyAuto,
                                   bool credentialsPresent,
                                   uint32_t nowMs) {
  if (state.phase != TimeSyncPhase::Idle || state.resultPending) {
    return TimeSyncActions{};
  }
  state = TimeSyncLogicState{};
  if (!policyAuto) {
    return finish(state, TimeSyncPhase::Failed,
                  TimeSyncFailure::DisabledByPolicy);
  }
  if (!credentialsPresent) {
    return finish(state, TimeSyncPhase::Failed,
                  TimeSyncFailure::MissingCredentials);
  }

  state.phase = TimeSyncPhase::WaitingForWifi;
  state.failure = TimeSyncFailure::None;
  state.ownsNetwork = true;
  state.totalDeadlineMs = nowMs + TIME_SYNC_WIFI_DEADLINE_MS;
  TimeSyncActions actions;
  actions.requestNetwork = true;
  return actions;
}

TimeSyncActions timeSyncLogicUpdate(TimeSyncLogicState &state,
                                    const TimeSyncUpdateInputs &inputs) {
  if (state.phase == TimeSyncPhase::Idle ||
      state.phase == TimeSyncPhase::Succeeded ||
      state.phase == TimeSyncPhase::Failed ||
      state.phase == TimeSyncPhase::WritingRtc) {
    return TimeSyncActions{};
  }
  if (!inputs.policyAuto) {
    return finish(state, TimeSyncPhase::Failed,
                  TimeSyncFailure::DisabledByPolicy);
  }
  if (!inputs.credentialsPresent) {
    return finish(state, TimeSyncPhase::Failed,
                  TimeSyncFailure::MissingCredentials);
  }

  if (state.phase == TimeSyncPhase::WaitingForWifi) {
    if (inputs.wifiStatus == TimeSyncWifiStatus::Failed) {
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::WifiFailed);
    }
    if (timeSyncDeadlineReached(inputs.nowMs, state.totalDeadlineMs)) {
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::WifiTimedOut);
    }
    if (inputs.wifiStatus == TimeSyncWifiStatus::Connected) {
      return startAttempt(state, inputs.nowMs);
    }
    return TimeSyncActions{};
  }

  if (state.phase == TimeSyncPhase::RetryDelay) {
    if (inputs.wifiStatus == TimeSyncWifiStatus::Failed ||
        timeSyncDeadlineReached(inputs.nowMs, state.totalDeadlineMs)) {
      return finish(state, TimeSyncPhase::Failed,
                    inputs.wifiStatus == TimeSyncWifiStatus::Failed
                        ? TimeSyncFailure::WifiFailed
                        : TimeSyncFailure::WifiTimedOut);
    }
    if (!timeSyncDeadlineReached(inputs.nowMs, state.phaseDeadlineMs)) {
      return TimeSyncActions{};
    }
    if (inputs.wifiStatus == TimeSyncWifiStatus::Connected) {
      return startAttempt(state, inputs.nowMs);
    }
    state.phase = TimeSyncPhase::WaitingForWifi;
    return TimeSyncActions{};
  }

  if (timeSyncDeadlineReached(inputs.nowMs, state.phaseDeadlineMs) ||
      timeSyncDeadlineReached(inputs.nowMs, state.totalDeadlineMs)) {
    return failAttempt(state, inputs.nowMs, TimeSyncFailure::SntpTimedOut);
  }

  if (inputs.sntpCompleted) {
    // Only a COMPLETED event from this attempt may expose system time; an old
    // but plausible epoch must never reach the RTC commit path.
    RtcTime rtc;
    if (!timeSyncEpochToRtc(inputs.networkEpoch, rtc)) {
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::InvalidNetworkTime);
    }
    TimeSyncActions actions;
    actions.stopSntp = state.sntpActive;
    state.sntpActive = false;
    state.pendingEpoch = inputs.networkEpoch;
    state.pendingRtc = rtc;
    state.phase = TimeSyncPhase::WritingRtc;
    actions.commitRtc = true;
    return actions;
  }

  if (inputs.wifiStatus == TimeSyncWifiStatus::Disconnected ||
      inputs.wifiStatus == TimeSyncWifiStatus::Failed) {
    return failAttempt(state, inputs.nowMs, TimeSyncFailure::WifiFailed);
  }
  return TimeSyncActions{};
}

TimeSyncActions timeSyncLogicCompleteRtc(
    TimeSyncLogicState &state,
    TimeSyncRtcCommitOutcome outcome,
    uint32_t completedDateKey) {
  if (state.phase != TimeSyncPhase::WritingRtc) {
    return TimeSyncActions{};
  }
  switch (outcome) {
    case TimeSyncRtcCommitOutcome::Succeeded:
      if (completedDateKey != 0) {
        return finish(state, TimeSyncPhase::Succeeded,
                      TimeSyncFailure::None, completedDateKey);
      }
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::RtcReadbackFailed);
    case TimeSyncRtcCommitOutcome::WriteFailed:
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::RtcWriteFailed);
    case TimeSyncRtcCommitOutcome::ReadbackFailed:
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::RtcReadbackFailed);
    case TimeSyncRtcCommitOutcome::PersistenceFailed:
      return finish(state, TimeSyncPhase::Failed,
                    TimeSyncFailure::ResultPersistenceFailed);
  }
  return TimeSyncActions{};
}

bool timeSyncLogicTakeResult(TimeSyncLogicState &state, TaskRunResult &result) {
  if (!state.resultPending) {
    return false;
  }
  result = state.result;
  state.resultPending = false;
  state.phase = TimeSyncPhase::Idle;
  return true;
}

bool timeSyncDeadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

bool timeSyncEpochToRtc(uint32_t utcEpoch, RtcTime &rtc) {
  if (utcEpoch == 0) {
    return false;
  }
  const uint64_t localSeconds =
      static_cast<uint64_t>(utcEpoch) + TIME_SYNC_UTC_OFFSET_SECONDS;
  const time_t localEpoch = static_cast<time_t>(localSeconds);
  if (localEpoch < 0 || static_cast<uint64_t>(localEpoch) != localSeconds) {
    return false;
  }

  struct tm local = {};
  // The fixed offset plus gmtime_r avoids global TZ state and browser locale.
  if (gmtime_r(&localEpoch, &local) == nullptr) {
    return false;
  }
  const int year = local.tm_year + 1900;
  if (year < 2000 || year > 2099) {
    return false;
  }

  RtcTime converted;
  converted.year = static_cast<uint16_t>(year);
  converted.month = static_cast<uint8_t>(local.tm_mon + 1);
  converted.date = static_cast<uint8_t>(local.tm_mday);
  converted.hour = static_cast<uint8_t>(local.tm_hour);
  converted.minute = static_cast<uint8_t>(local.tm_min);
  converted.second = static_cast<uint8_t>(local.tm_sec);
  converted.day = static_cast<uint8_t>(local.tm_wday == 0 ? 7 : local.tm_wday);
  if (converted.date < 1 ||
      converted.date > daysInMonth(converted.year, converted.month) ||
      converted.hour >= 24 || converted.minute >= 60 ||
      converted.second >= 60 || converted.day < 1 || converted.day > 7) {
    return false;
  }
  converted.valid = true;
  rtc = converted;
  return true;
}

bool timeSyncFormatLocalEpoch(uint32_t utcEpoch,
                              char *buffer,
                              size_t bufferSize) {
  if (buffer == nullptr || bufferSize < 20) {
    return false;
  }
  RtcTime rtc;
  if (!timeSyncEpochToRtc(utcEpoch, rtc)) {
    buffer[0] = '\0';
    return false;
  }
  const int written = snprintf(buffer, bufferSize, "%04u-%02u-%02u %02u:%02u:%02u",
                               rtc.year, rtc.month, rtc.date, rtc.hour,
                               rtc.minute, rtc.second);
  return written == 19;
}
