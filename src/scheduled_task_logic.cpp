#include "scheduled_task_logic.h"

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
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return DAYS[month - 1];
}

}  // namespace

bool scheduledTaskDateKey(uint16_t year,
                          uint8_t month,
                          uint8_t date,
                          uint32_t &dateKey) {
  if (year < 2000 || year > 2099 || date < 1 ||
      date > daysInMonth(year, month)) {
    return false;
  }
  dateKey = static_cast<uint32_t>(year) * 10000UL +
            static_cast<uint32_t>(month) * 100UL + date;
  return true;
}

bool scheduledTaskMinuteOfDay(uint8_t hour,
                             uint8_t minute,
                             uint16_t &minuteOfDay) {
  if (hour >= 24 || minute >= 60) {
    return false;
  }
  minuteOfDay = static_cast<uint16_t>(hour) * 60 + minute;
  return true;
}

bool scheduledTaskDailyIsDue(const DailyTaskDefinition &definition,
                             bool rtcValid,
                             uint16_t year,
                             uint8_t month,
                             uint8_t date,
                             uint8_t hour,
                             uint8_t minute,
                             uint32_t lastAttemptDateKey,
                             bool running) {
  if (!rtcValid || running || definition.minuteOfDay >= 24 * 60) {
    return false;
  }

  uint32_t currentDateKey = 0;
  uint16_t currentMinute = 0;
  if (!scheduledTaskDateKey(year, month, date, currentDateKey) ||
      !scheduledTaskMinuteOfDay(hour, minute, currentMinute)) {
    return false;
  }

  // Exact equality is intentional: a missed minute is never backfilled.
  return currentMinute == definition.minuteOfDay &&
         currentDateKey != lastAttemptDateKey;
}

ScheduledTaskDispatchDecision scheduledTaskPlanDailyDispatch(
    const DailyTaskDefinition &definition,
    bool rtcValid,
    uint16_t year,
    uint8_t month,
    uint8_t date,
    uint8_t hour,
    uint8_t minute,
    uint32_t lastAttemptDateKey,
    bool running) {
  ScheduledTaskDispatchDecision decision;
  if (!scheduledTaskDailyIsDue(definition, rtcValid, year, month, date,
                               hour, minute, lastAttemptDateKey, running) ||
      !scheduledTaskDateKey(year, month, date, decision.attemptDateKey)) {
    return ScheduledTaskDispatchDecision{};
  }
  decision.shouldPersist = true;
  return decision;
}

bool scheduledTaskDispatchAfterPersist(
    const ScheduledTaskDispatchDecision &decision,
    bool persistSucceeded) {
  return decision.shouldPersist && decision.attemptDateKey != 0 &&
         persistSucceeded;
}

ScheduledTaskResultDecision scheduledTaskPlanResult(
    uint32_t currentAttemptDateKey,
    const TaskRunResult &result) {
  ScheduledTaskResultDecision decision;
  decision.attemptDateKey = currentAttemptDateKey;
  if (result.status == TaskRunStatus::Succeeded &&
      result.completedDateKey != 0 &&
      result.completedDateKey != currentAttemptDateKey) {
    decision.shouldPersist = true;
    decision.attemptDateKey = result.completedDateKey;
  }
  return decision;
}
