#include "scheduled_task_service.h"

#include <Arduino.h>

#include "config.h"
#include "persistence.h"

using namespace AppConfig;

namespace {

size_t taskIndex(ScheduledTaskId id) {
  return static_cast<size_t>(id);
}

bool validTaskId(ScheduledTaskId id) {
  return taskIndex(id) < static_cast<size_t>(ScheduledTaskId::Count) &&
         taskIndex(id) < SCHEDULED_TASK_SLOT_COUNT;
}

}  // namespace

void scheduledTaskServiceBegin(ScheduledTaskServiceState &service) {
  service = ScheduledTaskServiceState{};
  service.records = persistenceLoadScheduledTaskRecords();
}

bool scheduledTaskServiceUpdate(ScheduledTaskServiceState &service,
                                const RtcTime &rtc,
                                ScheduledTaskId &dispatch) {
  const size_t index = taskIndex(ScheduledTaskId::TimeSync);
  const ScheduledTaskDispatchDecision decision =
      scheduledTaskPlanDailyDispatch(
          TIME_SYNC_DAILY_TASK, rtc.valid, rtc.year, rtc.month, rtc.date,
          rtc.hour, rtc.minute, service.records.lastAttemptDateKeys[index],
          service.running[index]);
  if (!decision.shouldPersist) {
    return false;
  }

  // Record the attempt before dispatch to provide at-most-once behavior even
  // when the device restarts while the plugin is running.
  service.records.lastAttemptDateKeys[index] = decision.attemptDateKey;
  const bool saved = persistenceSaveScheduledTaskRecords(service.records);
  if (!scheduledTaskDispatchAfterPersist(decision, saved)) {
    // RAM keeps the attempted date so an NVS failure cannot trigger a network
    // retry loop during this boot, while the unsaved task is never started.
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Scheduled task dispatch skipped: attempt save failed");
    }
    return false;
  }

  service.running[index] = true;
  dispatch = ScheduledTaskId::TimeSync;
  return true;
}

bool scheduledTaskServiceConsumeResult(ScheduledTaskServiceState &service,
                                       ScheduledTaskId id,
                                       const TaskRunResult &result) {
  if (!validTaskId(id) ||
      (result.status != TaskRunStatus::Succeeded &&
       result.status != TaskRunStatus::Failed)) {
    return false;
  }
  const size_t index = taskIndex(id);
  service.running[index] = false;
  const ScheduledTaskResultDecision decision = scheduledTaskPlanResult(
      service.records.lastAttemptDateKeys[index], result);
  if (!decision.shouldPersist) {
    return true;
  }

  // A successful RTC correction can cross a date boundary. Persist the
  // readback date so the corrected day cannot dispatch the same task again.
  service.records.lastAttemptDateKeys[index] = decision.attemptDateKey;
  return persistenceSaveScheduledTaskRecords(service.records);
}

bool scheduledTaskServiceIsRunning(const ScheduledTaskServiceState &service,
                                   ScheduledTaskId id) {
  return validTaskId(id) && service.running[taskIndex(id)];
}
