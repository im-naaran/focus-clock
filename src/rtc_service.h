#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "rtc.h"

struct RtcServiceState {
  uint32_t nextReadDueMs = 0;
  uint32_t lastReadMs = 0;
};

void rtcServiceBegin(RtcServiceState &service, AppState &app, uint32_t nowMs);
bool rtcServiceUpdate(RtcServiceState &service, AppState &app, uint32_t nowMs);
bool rtcServiceForceRead(RtcServiceState &service, AppState &app, uint32_t nowMs);
uint32_t rtcServiceNextReadDueMs(const RtcServiceState &service);
const char *rtcServiceStatusText(const RtcServiceState &service, const AppState &app);
