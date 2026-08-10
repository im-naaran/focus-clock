#include <stdint.h>
#include <stdio.h>

#include "scheduled_task_logic.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}

bool due(uint16_t year,
         uint8_t month,
         uint8_t date,
         uint8_t hour,
         uint8_t minute,
         uint32_t lastAttemptDateKey = 0,
         bool running = false,
         bool rtcValid = true) {
  return scheduledTaskDailyIsDue(TIME_SYNC_DAILY_TASK,
                                 rtcValid,
                                 year,
                                 month,
                                 date,
                                 hour,
                                 minute,
                                 lastAttemptDateKey,
                                 running);
}

void testDateKeys() {
  uint32_t key = 0;
  expect(scheduledTaskDateKey(2026, 8, 9, key) && key == 20260809,
         "valid date produces YYYYMMDD key");
  expect(scheduledTaskDateKey(2024, 2, 29, key) && key == 20240229,
         "leap day is accepted");
  expect(!scheduledTaskDateKey(2023, 2, 29, key),
         "non-leap February 29 is rejected");
  expect(!scheduledTaskDateKey(2026, 4, 31, key),
         "month-specific day limit is enforced");
  expect(!scheduledTaskDateKey(1999, 12, 31, key),
         "years before the RTC range are rejected");
  expect(!scheduledTaskDateKey(2100, 1, 1, key),
         "years after the RTC range are rejected");
}

void testMinuteValues() {
  uint16_t minute = 0;
  expect(scheduledTaskMinuteOfDay(8, 0, minute) && minute == 480,
         "08:00 maps to minute 480");
  expect(scheduledTaskMinuteOfDay(23, 59, minute) && minute == 1439,
         "23:59 maps to the final minute");
  expect(!scheduledTaskMinuteOfDay(24, 0, minute),
         "hour 24 is rejected");
  expect(!scheduledTaskMinuteOfDay(8, 60, minute),
         "minute 60 is rejected");
}

void testExactMinuteTrigger() {
  expect(!due(2026, 8, 9, 7, 59), "07:59 does not trigger");
  expect(due(2026, 8, 9, 8, 0), "08:00 triggers");
  expect(!due(2026, 8, 9, 8, 1), "08:01 is not backfilled");
  expect(!due(2026, 8, 9, 23, 59), "late startup is not backfilled");

  // Seconds do not enter the decision, so any RTC read in the 08:00 minute
  // has the same result, including a wake at 08:00:59.
  expect(due(2026, 8, 9, 8, 0),
         "a late read within the 08:00 minute still triggers");
}

void testDispatchGuards() {
  expect(!due(2026, 8, 9, 8, 0, 0, false, false),
         "invalid RTC does not trigger");
  expect(!due(2026, 8, 9, 8, 0, 0, true),
         "running task is not dispatched again");
  expect(!due(2026, 8, 9, 8, 0, 20260809),
         "same-day attempt prevents another dispatch");
  expect(due(2026, 8, 10, 8, 0, 20260809),
         "next date restores eligibility");
}

void testDefinitionBounds() {
  DailyTaskDefinition invalid = TIME_SYNC_DAILY_TASK;
  invalid.minuteOfDay = 24 * 60;
  expect(!scheduledTaskDailyIsDue(invalid, true, 2026, 8, 9, 8, 0, 0, false),
         "invalid schedule minute cannot trigger");
  expect(static_cast<size_t>(ScheduledTaskId::Count) <=
             SCHEDULED_TASK_SLOT_COUNT,
         "all task IDs fit the fixed persisted slots");
}

void testDispatchAccounting() {
  const ScheduledTaskDispatchDecision decision =
      scheduledTaskPlanDailyDispatch(TIME_SYNC_DAILY_TASK, true,
                                     2026, 8, 9, 8, 0, 0, false);
  expect(decision.shouldPersist && decision.attemptDateKey == 20260809,
         "due task plans the attempt record before dispatch");
  expect(!scheduledTaskDispatchAfterPersist(decision, false),
         "failed persistence prevents dispatch");
  expect(scheduledTaskDispatchAfterPersist(decision, true),
         "successful persistence allows dispatch");

  const ScheduledTaskDispatchDecision restored =
      scheduledTaskPlanDailyDispatch(TIME_SYNC_DAILY_TASK, true,
                                     2026, 8, 9, 8, 0, 20260809, false);
  expect(!restored.shouldPersist,
         "a restored attempt record prevents another dispatch");
}

void testResultAccounting() {
  TaskRunResult failed;
  failed.status = TaskRunStatus::Failed;
  ScheduledTaskResultDecision decision =
      scheduledTaskPlanResult(20260809, failed);
  expect(!decision.shouldPersist && decision.attemptDateKey == 20260809,
         "failed result retains the dispatch date");

  TaskRunResult succeeded;
  succeeded.status = TaskRunStatus::Succeeded;
  succeeded.completedDateKey = 20260810;
  decision = scheduledTaskPlanResult(20260809, succeeded);
  expect(decision.shouldPersist && decision.attemptDateKey == 20260810,
         "successful date correction plans a second record save");

  decision = scheduledTaskPlanResult(20260810, succeeded);
  expect(!decision.shouldPersist,
         "unchanged readback date does not write persistence again");
}

}  // namespace

int main() {
  testDateKeys();
  testMinuteValues();
  testExactMinuteTrigger();
  testDispatchGuards();
  testDefinitionBounds();
  testDispatchAccounting();
  testResultAccounting();

  if (failures != 0) {
    fprintf(stderr, "%d scheduled task logic test(s) failed\n", failures);
    return 1;
  }
  printf("scheduled task logic tests passed\n");
  return 0;
}
