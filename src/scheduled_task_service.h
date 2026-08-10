#pragma once

#include <stdint.h>

#include "persistence_codec.h"
#include "rtc.h"
#include "scheduled_task_logic.h"

struct ScheduledTaskServiceState {
  ScheduledTaskRecords records;
  bool running[SCHEDULED_TASK_SLOT_COUNT] = {};
};

void scheduledTaskServiceBegin(ScheduledTaskServiceState &service);
bool scheduledTaskServiceUpdate(ScheduledTaskServiceState &service,
                                const RtcTime &rtc,
                                ScheduledTaskId &dispatch);
bool scheduledTaskServiceConsumeResult(ScheduledTaskServiceState &service,
                                       ScheduledTaskId id,
                                       const TaskRunResult &result);
bool scheduledTaskServiceIsRunning(const ScheduledTaskServiceState &service,
                                   ScheduledTaskId id);
