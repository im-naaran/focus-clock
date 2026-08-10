#include "rtc_service.h"

#include <stdio.h>

#include "config.h"

using namespace AppConfig;

static bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

static void logRtcRawRegisters(const char *label) {
  if (!ENABLE_SERIAL_LOGGING) {
    (void)label;
    return;
  }
  const RtcRawRegisters raw = rtcReadRawRegisters();
  Serial.printf("%s RTC raw: sec=0x%02X min=0x%02X hour=0x%02X date=0x%02X "
                "month=0x%02X day=0x%02X year=0x%02X ctrl=0x%02X\n",
                label,
                raw.second,
                raw.minute,
                raw.hour,
                raw.date,
                raw.month,
                raw.day,
                raw.year,
                raw.control);
}

static uint32_t normalNextReadDelayMs(const RtcTime &time) {
  uint32_t toMinuteMs = RTC_NORMAL_MAX_REFRESH_MS;
  if (time.valid && time.second < 60) {
    const uint8_t secondsToNextMinute = time.second == 0 ? 60 : 60 - time.second;
    toMinuteMs = static_cast<uint32_t>(secondsToNextMinute) * 1000UL +
                 RTC_MINUTE_REFRESH_GRACE_MS;
  }
  return toMinuteMs < RTC_NORMAL_MAX_REFRESH_MS ? toMinuteMs : RTC_NORMAL_MAX_REFRESH_MS;
}

static void scheduleNextRead(RtcServiceState &service, const AppState &app, uint32_t nowMs) {
  const uint32_t delayMs = app.rtcOk && app.rtcTime.valid
                               ? normalNextReadDelayMs(app.rtcTime)
                               : RTC_SHORT_REFRESH_MS;
  service.nextReadDueMs = nowMs + delayMs;
}

static bool readRtcNow(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  const bool oldOk = app.rtcOk;
  const uint8_t oldHour = app.rtcTime.hour;
  const uint8_t oldMinute = app.rtcTime.minute;

  app.rtcOk = rtcReadTime(app.rtcTime);
  service.lastReadMs = nowMs;
  if (!app.rtcOk) {
    // Keep invalid data visible as a hardware fault; never write guessed time.
    logRtcRawRegisters("RTC read failed");
    app.displayDirty = true;
  }
  scheduleNextRead(service, app, nowMs);

  const bool minuteChanged = oldOk != app.rtcOk ||
                             oldHour != app.rtcTime.hour ||
                             oldMinute != app.rtcTime.minute;
  if (minuteChanged) {
    app.displayDirty = true;
  }
  return app.rtcOk;
}

void rtcServiceBegin(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  service = RtcServiceState{};
  rtcServiceForceRead(service, app, nowMs);
}

bool rtcServiceUpdate(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  bool changed = false;
  if (timeReached(nowMs, service.nextReadDueMs)) {
    const bool oldOk = app.rtcOk;
    const uint8_t oldHour = app.rtcTime.hour;
    const uint8_t oldMinute = app.rtcTime.minute;
    readRtcNow(service, app, nowMs);
    changed = changed || oldOk != app.rtcOk ||
              oldHour != app.rtcTime.hour ||
              oldMinute != app.rtcTime.minute;
  }
  return changed;
}

bool rtcServiceForceRead(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  return readRtcNow(service, app, nowMs);
}

uint32_t rtcServiceNextReadDueMs(const RtcServiceState &service) {
  return service.nextReadDueMs;
}

const char *rtcServiceStatusText(const RtcServiceState &service, const AppState &app) {
  (void)service;
  if (app.rtcOk && app.rtcTime.valid) {
    return nullptr;
  }
  return "RTC READ FAIL";
}
