#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_timing.h"
#include "time_sync_logic.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}

TimeSyncUpdateInputs inputs(uint32_t nowMs,
                            TimeSyncWifiStatus wifiStatus) {
  TimeSyncUpdateInputs value;
  value.nowMs = nowMs;
  value.policyAuto = true;
  value.credentialsPresent = true;
  value.wifiStatus = wifiStatus;
  return value;
}

void startConnectedAttempt(TimeSyncLogicState &state, uint32_t nowMs = 100) {
  timeSyncLogicBegin(state);
  const TimeSyncActions start = timeSyncLogicStart(state, true, true, nowMs);
  expect(start.requestNetwork && state.phase == TimeSyncPhase::WaitingForWifi,
         "start requests its network consumer");
  const TimeSyncActions connected = timeSyncLogicUpdate(
      state, inputs(nowMs + 1, TimeSyncWifiStatus::Connected));
  expect(connected.startSntp && state.attempt == 1 &&
             state.phase == TimeSyncPhase::WaitingForSntp,
         "connected WiFi starts attempt one");
}

void testStartGuards() {
  TimeSyncLogicState state;
  timeSyncLogicBegin(state);
  TimeSyncActions actions = timeSyncLogicStart(state, false, true, 0);
  expect(state.failure == TimeSyncFailure::DisabledByPolicy &&
             !actions.requestNetwork && !actions.releaseNetwork,
         "OFF policy fails without acquiring a consumer");
  TaskRunResult result;
  expect(timeSyncLogicTakeResult(state, result) &&
             result.status == TaskRunStatus::Failed,
         "policy rejection exposes one failed result");
  expect(!timeSyncLogicTakeResult(state, result),
         "terminal result can only be consumed once");

  actions = timeSyncLogicStart(state, true, false, 0);
  expect(state.failure == TimeSyncFailure::MissingCredentials &&
             !actions.requestNetwork,
         "missing credentials fail before network request");
}

void testWifiFailuresAndChanges() {
  TimeSyncLogicState state;
  timeSyncLogicBegin(state);
  timeSyncLogicStart(state, true, true, 10);
  TimeSyncUpdateInputs update = inputs(11, TimeSyncWifiStatus::Failed);
  TimeSyncActions actions = timeSyncLogicUpdate(state, update);
  expect(state.failure == TimeSyncFailure::WifiFailed &&
             actions.releaseNetwork,
         "WiFi failure releases the owned consumer");

  timeSyncLogicBegin(state);
  timeSyncLogicStart(state, true, true, 100);
  update = inputs(101, TimeSyncWifiStatus::Pending);
  update.policyAuto = false;
  actions = timeSyncLogicUpdate(state, update);
  expect(state.failure == TimeSyncFailure::DisabledByPolicy &&
             actions.releaseNetwork,
         "policy changes terminate an active run");

  timeSyncLogicBegin(state);
  timeSyncLogicStart(state, true, true, 200);
  update = inputs(201, TimeSyncWifiStatus::Pending);
  update.credentialsPresent = false;
  actions = timeSyncLogicUpdate(state, update);
  expect(state.failure == TimeSyncFailure::MissingCredentials &&
             actions.releaseNetwork,
         "credential removal terminates an active run");
}

void testCompletedGateAndSuccess() {
  TimeSyncLogicState state;
  startConnectedAttempt(state);
  TimeSyncUpdateInputs update = inputs(102, TimeSyncWifiStatus::Connected);
  update.networkEpoch = 1786233605;
  TimeSyncActions actions = timeSyncLogicUpdate(state, update);
  expect(!actions.commitRtc && state.phase == TimeSyncPhase::WaitingForSntp,
         "an old valid epoch is ignored before COMPLETED");

  update.sntpCompleted = true;
  actions = timeSyncLogicUpdate(state, update);
  expect(actions.stopSntp && actions.commitRtc &&
             state.phase == TimeSyncPhase::WritingRtc &&
             state.pendingRtc.hour == 8,
         "COMPLETED gates conversion and RTC commit");

  actions = timeSyncLogicCompleteRtc(
      state, TimeSyncRtcCommitOutcome::Succeeded, 20260809);
  expect(actions.releaseNetwork && state.phase == TimeSyncPhase::Succeeded,
         "complete RTC transaction releases the consumer");
  TaskRunResult result;
  expect(timeSyncLogicTakeResult(state, result) &&
             result.status == TaskRunStatus::Succeeded &&
             result.completedDateKey == 20260809,
         "success exposes the readback date once");
}

void testRetryAndTimeouts() {
  TimeSyncLogicState state;
  startConnectedAttempt(state, 1000);
  TimeSyncActions actions = timeSyncLogicUpdate(
      state, inputs(11001, TimeSyncWifiStatus::Connected));
  expect(actions.stopSntp && state.phase == TimeSyncPhase::RetryDelay &&
             state.attempt == 1,
         "first SNTP timeout enters non-blocking retry delay");
  actions = timeSyncLogicUpdate(
      state, inputs(14000, TimeSyncWifiStatus::Connected));
  expect(!actions.startSntp,
         "retry does not begin before its deadline");
  actions = timeSyncLogicUpdate(
      state, inputs(14001, TimeSyncWifiStatus::Connected));
  expect(actions.startSntp && state.attempt == 2,
         "retry deadline starts the second attempt");
  actions = timeSyncLogicUpdate(
      state, inputs(21000, TimeSyncWifiStatus::Connected));
  expect(state.failure == TimeSyncFailure::SntpTimedOut &&
             actions.stopSntp && actions.releaseNetwork,
         "the total deadline terminates attempt two");

  timeSyncLogicBegin(state);
  timeSyncLogicStart(state, true, true, 0);
  actions = timeSyncLogicUpdate(
      state, inputs(AppConfig::TIME_SYNC_WIFI_DEADLINE_MS,
                    TimeSyncWifiStatus::Pending));
  expect(state.failure == TimeSyncFailure::WifiTimedOut &&
             actions.releaseNetwork,
         "WiFi waiting has a finite total deadline");
}

void testDisconnectRetry() {
  TimeSyncLogicState state;
  startConnectedAttempt(state, 500);
  TimeSyncActions actions = timeSyncLogicUpdate(
      state, inputs(600, TimeSyncWifiStatus::Disconnected));
  expect(actions.stopSntp && state.phase == TimeSyncPhase::RetryDelay,
         "first disconnect consumes an attempt and schedules retry");
  actions = timeSyncLogicUpdate(
      state, inputs(3600, TimeSyncWifiStatus::Connected));
  expect(actions.startSntp && state.attempt == 2,
         "reconnected WiFi starts only the remaining attempt");
  actions = timeSyncLogicUpdate(
      state, inputs(3700, TimeSyncWifiStatus::Disconnected));
  expect(state.failure == TimeSyncFailure::WifiFailed &&
             actions.releaseNetwork,
         "second disconnect is terminal");
}

void testRtcOutcomes() {
  const TimeSyncRtcCommitOutcome outcomes[] = {
      TimeSyncRtcCommitOutcome::WriteFailed,
      TimeSyncRtcCommitOutcome::ReadbackFailed,
      TimeSyncRtcCommitOutcome::PersistenceFailed,
  };
  const TimeSyncFailure failures[] = {
      TimeSyncFailure::RtcWriteFailed,
      TimeSyncFailure::RtcReadbackFailed,
      TimeSyncFailure::ResultPersistenceFailed,
  };
  for (size_t i = 0; i < 3; ++i) {
    TimeSyncLogicState state;
    startConnectedAttempt(state);
    TimeSyncUpdateInputs update = inputs(102, TimeSyncWifiStatus::Connected);
    update.sntpCompleted = true;
    update.networkEpoch = 1786233605;
    timeSyncLogicUpdate(state, update);
    const TimeSyncActions actions =
        timeSyncLogicCompleteRtc(state, outcomes[i], 0);
    expect(state.failure == failures[i] && actions.releaseNetwork,
           "each RTC transaction failure terminates and releases once");
    const TimeSyncActions repeated =
        timeSyncLogicCompleteRtc(state, outcomes[i], 0);
    expect(!repeated.releaseNetwork,
           "a terminal RTC outcome cannot be applied twice");
  }
}

void testEpochConversion() {
  RtcTime rtc;
  expect(timeSyncEpochToRtc(946656000, rtc) && rtc.year == 2000 &&
             rtc.month == 1 && rtc.date == 1 && rtc.hour == 0,
         "UTC+8 lower RTC boundary converts correctly");
  expect(timeSyncEpochToRtc(1709222400, rtc) && rtc.year == 2024 &&
             rtc.month == 3 && rtc.date == 1 && rtc.hour == 0 &&
             rtc.day == 5,
         "UTC+8 conversion crosses leap day with correct weekday");
  expect(timeSyncEpochToRtc(4102415999U, rtc) && rtc.year == 2099 &&
             rtc.month == 12 && rtc.date == 31 && rtc.hour == 23,
         "last local second of 2099 is accepted");
  expect(!timeSyncEpochToRtc(4102416000U, rtc),
         "local year 2100 is rejected");
  expect(!timeSyncEpochToRtc(0, rtc), "zero epoch is rejected");

  char text[20] = {};
  expect(timeSyncFormatLocalEpoch(1786233605, text, sizeof(text)) &&
             strcmp(text, "2026-08-09 08:00:05") == 0,
         "last success uses fixed UTC+8 formatting");
  expect(!timeSyncFormatLocalEpoch(1786233605, text, 19),
         "format helper rejects undersized buffers");
  expect(!timeSyncFormatLocalEpoch(0, text, sizeof(text)) && text[0] == '\0',
         "zero last-success epoch formats as no value");
}

void testMillisWrap() {
  TimeSyncLogicState state;
  const uint32_t start = 0xFFFFFFF0U;
  timeSyncLogicBegin(state);
  timeSyncLogicStart(state, true, true, start);
  expect(!timeSyncDeadlineReached(19983U, state.totalDeadlineMs),
         "wrapped total deadline is not reached early");
  expect(timeSyncDeadlineReached(19984U, state.totalDeadlineMs),
         "wrapped total deadline is reached exactly");
  const TimeSyncActions actions = timeSyncLogicUpdate(
      state, inputs(19984U, TimeSyncWifiStatus::Pending));
  expect(state.failure == TimeSyncFailure::WifiTimedOut &&
             actions.releaseNetwork,
         "state transition also handles millis wrap");
}

void testNextRunResetsTransientState() {
  TimeSyncLogicState state;
  startConnectedAttempt(state);
  TimeSyncUpdateInputs update = inputs(102, TimeSyncWifiStatus::Connected);
  update.sntpCompleted = true;
  update.networkEpoch = 1786233605;
  timeSyncLogicUpdate(state, update);
  timeSyncLogicCompleteRtc(state, TimeSyncRtcCommitOutcome::Succeeded,
                           20260809);
  TaskRunResult result;
  timeSyncLogicTakeResult(state, result);

  const TimeSyncActions actions = timeSyncLogicStart(state, true, true, 500);
  expect(actions.requestNetwork && state.attempt == 0 &&
             state.pendingEpoch == 0 && state.failure == TimeSyncFailure::None,
         "a later daily run starts with fresh transient state");
}

}  // namespace

int main() {
  testStartGuards();
  testWifiFailuresAndChanges();
  testCompletedGateAndSuccess();
  testRetryAndTimeouts();
  testDisconnectRetry();
  testRtcOutcomes();
  testEpochConversion();
  testMillisWrap();
  testNextRunResetsTransientState();

  if (failures != 0) {
    fprintf(stderr, "%d time sync logic test(s) failed\n", failures);
    return 1;
  }
  printf("time sync logic tests passed\n");
  return 0;
}
