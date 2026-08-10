#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config_timing.h"

enum class ScheduledTaskId : uint8_t {
  TimeSync = 0,
  Count,
};

enum class TaskRunStatus : uint8_t {
  Idle,
  Running,
  Succeeded,
  Failed,
};

struct TaskRunResult {
  TaskRunStatus status = TaskRunStatus::Idle;
  uint32_t completedDateKey = 0;
};

struct DailyTaskDefinition {
  ScheduledTaskId id;
  uint16_t minuteOfDay;
};

struct ScheduledTaskDispatchDecision {
  bool shouldPersist = false;
  uint32_t attemptDateKey = 0;
};

struct ScheduledTaskResultDecision {
  bool shouldPersist = false;
  uint32_t attemptDateKey = 0;
};

// Four stable slots let V1 persistence add tasks without changing its layout.
static constexpr size_t SCHEDULED_TASK_SLOT_COUNT = 4;
static constexpr DailyTaskDefinition TIME_SYNC_DAILY_TASK = {
    ScheduledTaskId::TimeSync,
    AppConfig::TIME_SYNC_DAILY_MINUTE,
};

static_assert(static_cast<size_t>(ScheduledTaskId::Count) <=
                  SCHEDULED_TASK_SLOT_COUNT,
              "Scheduled task IDs must fit the persisted slot capacity");

bool scheduledTaskDateKey(uint16_t year,
                          uint8_t month,
                          uint8_t date,
                          uint32_t &dateKey);
bool scheduledTaskMinuteOfDay(uint8_t hour,
                             uint8_t minute,
                             uint16_t &minuteOfDay);
bool scheduledTaskDailyIsDue(const DailyTaskDefinition &definition,
                             bool rtcValid,
                             uint16_t year,
                             uint8_t month,
                             uint8_t date,
                             uint8_t hour,
                             uint8_t minute,
                             uint32_t lastAttemptDateKey,
                             bool running);
ScheduledTaskDispatchDecision scheduledTaskPlanDailyDispatch(
    const DailyTaskDefinition &definition,
    bool rtcValid,
    uint16_t year,
    uint8_t month,
    uint8_t date,
    uint8_t hour,
    uint8_t minute,
    uint32_t lastAttemptDateKey,
    bool running);
bool scheduledTaskDispatchAfterPersist(
    const ScheduledTaskDispatchDecision &decision,
    bool persistSucceeded);
ScheduledTaskResultDecision scheduledTaskPlanResult(
    uint32_t currentAttemptDateKey,
    const TaskRunResult &result);
