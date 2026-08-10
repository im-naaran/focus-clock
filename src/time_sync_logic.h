#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rtc.h"
#include "scheduled_task_logic.h"

enum class TimeSyncPhase : uint8_t {
  Idle,
  WaitingForWifi,
  WaitingForSntp,
  RetryDelay,
  WritingRtc,
  Succeeded,
  Failed,
};

enum class TimeSyncFailure : uint8_t {
  None,
  DisabledByPolicy,
  MissingCredentials,
  WifiFailed,
  WifiTimedOut,
  SntpTimedOut,
  InvalidNetworkTime,
  RtcWriteFailed,
  RtcReadbackFailed,
  ResultPersistenceFailed,
};

enum class TimeSyncWifiStatus : uint8_t {
  Pending,
  Connected,
  Failed,
  Disconnected,
};

enum class TimeSyncRtcCommitOutcome : uint8_t {
  Succeeded,
  WriteFailed,
  ReadbackFailed,
  PersistenceFailed,
};

struct TimeSyncLogicState {
  TimeSyncPhase phase = TimeSyncPhase::Idle;
  TimeSyncFailure failure = TimeSyncFailure::None;
  uint8_t attempt = 0;
  bool ownsNetwork = false;
  bool sntpActive = false;
  bool resultPending = false;
  uint32_t totalDeadlineMs = 0;
  uint32_t phaseDeadlineMs = 0;
  uint32_t pendingEpoch = 0;
  RtcTime pendingRtc;
  TaskRunResult result;
};

struct TimeSyncActions {
  bool requestNetwork = false;
  bool startSntp = false;
  bool stopSntp = false;
  bool releaseNetwork = false;
  bool commitRtc = false;
};

struct TimeSyncUpdateInputs {
  uint32_t nowMs = 0;
  bool policyAuto = false;
  bool credentialsPresent = false;
  TimeSyncWifiStatus wifiStatus = TimeSyncWifiStatus::Pending;
  bool sntpCompleted = false;
  uint32_t networkEpoch = 0;
};

void timeSyncLogicBegin(TimeSyncLogicState &state);
TimeSyncActions timeSyncLogicStart(TimeSyncLogicState &state,
                                   bool policyAuto,
                                   bool credentialsPresent,
                                   uint32_t nowMs);
TimeSyncActions timeSyncLogicUpdate(TimeSyncLogicState &state,
                                    const TimeSyncUpdateInputs &inputs);
TimeSyncActions timeSyncLogicCompleteRtc(
    TimeSyncLogicState &state,
    TimeSyncRtcCommitOutcome outcome,
    uint32_t completedDateKey);
bool timeSyncLogicTakeResult(TimeSyncLogicState &state, TaskRunResult &result);
bool timeSyncDeadlineReached(uint32_t nowMs, uint32_t deadlineMs);
bool timeSyncEpochToRtc(uint32_t utcEpoch, RtcTime &rtc);
bool timeSyncFormatLocalEpoch(uint32_t utcEpoch,
                              char *buffer,
                              size_t bufferSize);
