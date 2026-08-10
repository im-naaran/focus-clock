#include "time_sync_task.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "persistence.h"
#include "rtc.h"

using namespace AppConfig;

namespace {

const char *failureText(TimeSyncFailure failure) {
  switch (failure) {
    case TimeSyncFailure::None:
      return "none";
    case TimeSyncFailure::DisabledByPolicy:
      return "disabled-by-policy";
    case TimeSyncFailure::MissingCredentials:
      return "missing-credentials";
    case TimeSyncFailure::WifiFailed:
      return "wifi-failed";
    case TimeSyncFailure::WifiTimedOut:
      return "wifi-timeout";
    case TimeSyncFailure::SntpTimedOut:
      return "sntp-timeout";
    case TimeSyncFailure::InvalidNetworkTime:
      return "invalid-network-time";
    case TimeSyncFailure::RtcWriteFailed:
      return "rtc-write-failed";
    case TimeSyncFailure::RtcReadbackFailed:
      return "rtc-readback-failed";
    case TimeSyncFailure::ResultPersistenceFailed:
      return "result-persistence-failed";
  }
  return "unknown";
}

bool credentialsPresent(const NetworkConfig &config) {
  return config.staSsid[0] != '\0';
}

TimeSyncWifiStatus wifiStatusFor(const TimeSyncLogicState &logic,
                                 WifiConnectionState status) {
  if (status == WifiConnectionState::Connected) {
    return TimeSyncWifiStatus::Connected;
  }
  if (status == WifiConnectionState::Failed) {
    return TimeSyncWifiStatus::Failed;
  }
  if (logic.phase == TimeSyncPhase::WaitingForSntp) {
    return TimeSyncWifiStatus::Disconnected;
  }
  return TimeSyncWifiStatus::Pending;
}

void stopSntp() {
  esp_sntp_stop();
}

void startSntpAttempt() {
  // Stop and RESET before every attempt so only this run's COMPLETED status
  // can authorize reading the system epoch.
  esp_sntp_stop();
  esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTime(TIME_SYNC_UTC_OFFSET_SECONDS, 0,
             TIME_SYNC_NTP_SERVERS[0], TIME_SYNC_NTP_SERVERS[1],
             TIME_SYNC_NTP_SERVERS[2]);
}

void applyResourceActions(const TimeSyncActions &actions,
                          WifiServiceState &wifiService) {
  if (actions.stopSntp) {
    stopSntp();
  }
  // The task owns only this bit; other consumers and a concurrent Portal keep
  // their radio demands when Time Sync reaches a terminal state.
  if (actions.releaseNetwork) {
    wifiServiceReleaseAutoNetwork(wifiService, WifiConsumer::TimeSync);
  }
}

TimeSyncRtcCommitOutcome commitRtcOnce(TimeSyncTaskState &task,
                                       RtcServiceState &rtcService,
                                       AppState &app,
                                       uint32_t nowMs,
                                       uint32_t &completedDateKey) {
  // Publish success only after the single RTC write, forced readback, and
  // epoch persistence all complete in this order.
  if (!rtcSetTime(task.logic.pendingRtc)) {
    return TimeSyncRtcCommitOutcome::WriteFailed;
  }
  if (!rtcServiceForceRead(rtcService, app, nowMs) || !app.rtcTime.valid) {
    return TimeSyncRtcCommitOutcome::ReadbackFailed;
  }
  if (!scheduledTaskDateKey(app.rtcTime.year, app.rtcTime.month,
                            app.rtcTime.date, completedDateKey)) {
    return TimeSyncRtcCommitOutcome::ReadbackFailed;
  }
  if (!persistenceSaveLastTimeSyncSuccessEpoch(task.logic.pendingEpoch)) {
    return TimeSyncRtcCommitOutcome::PersistenceFailed;
  }
  return TimeSyncRtcCommitOutcome::Succeeded;
}

void logTerminal(const TimeSyncLogicState &logic) {
  if (!ENABLE_SERIAL_LOGGING ||
      (logic.phase != TimeSyncPhase::Succeeded &&
       logic.phase != TimeSyncPhase::Failed)) {
    return;
  }
  Serial.printf("Time Sync finished: status=%s failure=%s attempts=%u\n",
                logic.phase == TimeSyncPhase::Succeeded ? "succeeded" : "failed",
                failureText(logic.failure), logic.attempt);
}

}  // namespace

void timeSyncTaskBegin(TimeSyncTaskState &task) {
  timeSyncLogicBegin(task.logic);
}

bool timeSyncTaskStart(TimeSyncTaskState &task,
                       WifiServiceState &wifiService,
                       const NetworkConfig &networkConfig,
                       uint32_t nowMs) {
  if (task.logic.phase != TimeSyncPhase::Idle || task.logic.resultPending) {
    return false;
  }
  const TimeSyncActions actions = timeSyncLogicStart(
      task.logic, networkConfig.policy == WifiPolicy::Auto,
      credentialsPresent(networkConfig), nowMs);
  if (actions.requestNetwork) {
    wifiServiceRequestAutoNetwork(wifiService, WifiConsumer::TimeSync,
                                  networkConfig.policy, nowMs);
  }
  applyResourceActions(actions, wifiService);
  logTerminal(task.logic);
  return true;
}

void timeSyncTaskUpdate(TimeSyncTaskState &task,
                        WifiServiceState &wifiService,
                        const NetworkConfig &networkConfig,
                        const WifiRuntimeView &wifiRuntime,
                        RtcServiceState &rtcService,
                        AppState &app,
                        uint32_t nowMs) {
  const TimeSyncPhase previousPhase = task.logic.phase;
  TimeSyncUpdateInputs inputs;
  inputs.nowMs = nowMs;
  inputs.policyAuto = networkConfig.policy == WifiPolicy::Auto;
  inputs.credentialsPresent = credentialsPresent(networkConfig);
  inputs.wifiStatus = wifiStatusFor(task.logic, wifiRuntime.connectionState);
  if (task.logic.phase == TimeSyncPhase::WaitingForSntp &&
      esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    inputs.sntpCompleted = true;
    inputs.networkEpoch = static_cast<uint32_t>(time(nullptr));
  }

  TimeSyncActions actions = timeSyncLogicUpdate(task.logic, inputs);
  applyResourceActions(actions, wifiService);
  if (actions.startSntp) {
    startSntpAttempt();
  }
  if (actions.commitRtc) {
    uint32_t completedDateKey = 0;
    const TimeSyncRtcCommitOutcome outcome =
        commitRtcOnce(task, rtcService, app, nowMs, completedDateKey);
    actions = timeSyncLogicCompleteRtc(task.logic, outcome, completedDateKey);
    applyResourceActions(actions, wifiService);
    if (task.logic.phase == TimeSyncPhase::Succeeded) {
      app.lastTimeSyncSuccessEpoch = task.logic.pendingEpoch;
      // Refresh a visible TIME SYNC detail page as soon as the result is published.
      app.displayDirty = true;
    }
  }
  if (previousPhase != task.logic.phase &&
      (task.logic.phase == TimeSyncPhase::Succeeded ||
       task.logic.phase == TimeSyncPhase::Failed)) {
    logTerminal(task.logic);
  }
}

bool timeSyncTaskTakeResult(TimeSyncTaskState &task, TaskRunResult &result) {
  return timeSyncLogicTakeResult(task.logic, result);
}

bool timeSyncTaskIsRunning(const TimeSyncTaskState &task) {
  return task.logic.phase != TimeSyncPhase::Idle &&
         task.logic.phase != TimeSyncPhase::Succeeded &&
         task.logic.phase != TimeSyncPhase::Failed;
}
