#pragma once

#include <stdint.h>

#include "app_state.h"
#include "rtc_service.h"
#include "time_sync_logic.h"
#include "wifi_service.h"

struct TimeSyncTaskState {
  TimeSyncLogicState logic;
};

void timeSyncTaskBegin(TimeSyncTaskState &task);
bool timeSyncTaskStart(TimeSyncTaskState &task,
                       WifiServiceState &wifiService,
                       const NetworkConfig &networkConfig,
                       uint32_t nowMs);
void timeSyncTaskUpdate(TimeSyncTaskState &task,
                        WifiServiceState &wifiService,
                        const NetworkConfig &networkConfig,
                        const WifiRuntimeView &wifiRuntime,
                        RtcServiceState &rtcService,
                        AppState &app,
                        uint32_t nowMs);
bool timeSyncTaskTakeResult(TimeSyncTaskState &task, TaskRunResult &result);
bool timeSyncTaskIsRunning(const TimeSyncTaskState &task);
