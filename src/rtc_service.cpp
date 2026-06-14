#include "rtc_service.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

using namespace AppConfig;

static bool timeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

static uint8_t monthFromBuildDate(const char *monthName) {
  static const char *months[] = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
  };
  for (uint8_t i = 0; i < 12; ++i) {
    if (strncmp(monthName, months[i], 3) == 0) {
      return i + 1;
    }
  }
  return 1;
}

static uint8_t weekdayFromDate(uint16_t year, uint8_t month, uint8_t date) {
  static const uint8_t monthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  uint16_t y = year;
  if (month < 3) {
    --y;
  }
  const uint8_t sundayBased =
      (y + y / 4 - y / 100 + y / 400 + monthOffsets[month - 1] + date) % 7;
  return sundayBased == 0 ? 7 : sundayBased;
}

static bool buildTimeToRtcTime(RtcTime &time) {
  char monthName[4] = {};
  int year = 0;
  int monthDate = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (sscanf(__DATE__, "%3s %d %d", monthName, &monthDate, &year) != 3 ||
      sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
    return false;
  }

  time.year = static_cast<uint16_t>(year);
  time.month = monthFromBuildDate(monthName);
  time.date = static_cast<uint8_t>(monthDate);
  time.hour = static_cast<uint8_t>(hour);
  time.minute = static_cast<uint8_t>(minute);
  time.second = static_cast<uint8_t>(second);
  time.day = weekdayFromDate(time.year, time.month, time.date);
  time.valid = true;
  return true;
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

static void scheduleRtcAutoInitIfNeeded(RtcServiceState &service, const AppState &app, uint32_t nowMs) {
  if (app.rtcOk ||
      service.autoInitAttempted ||
      service.autoInitState != RtcAutoInitState::Idle) {
    return;
  }
  service.autoInitState = RtcAutoInitState::Waiting;
  service.autoInitDueMs = nowMs + RTC_AUTO_INIT_DELAY_MS;
}

static bool readRtcNow(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  const bool oldOk = app.rtcOk;
  const uint8_t oldHour = app.rtcTime.hour;
  const uint8_t oldMinute = app.rtcTime.minute;

  app.rtcOk = rtcReadTime(app.rtcTime);
  service.lastReadMs = nowMs;
  if (!app.rtcOk) {
    logRtcRawRegisters("RTC read failed");
    scheduleRtcAutoInitIfNeeded(service, app, nowMs);
    app.displayDirty = true;
  } else if (service.autoInitState != RtcAutoInitState::Failed) {
    service.autoInitState = RtcAutoInitState::Idle;
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

static bool updateAutoInit(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  if (app.rtcOk) {
    if (service.autoInitState != RtcAutoInitState::Failed) {
      service.autoInitState = RtcAutoInitState::Idle;
    }
    return false;
  }

  if (service.autoInitState == RtcAutoInitState::Waiting) {
    if (!timeReached(nowMs, service.autoInitDueMs)) {
      return false;
    }
    service.autoInitState = RtcAutoInitState::Writing;
    app.displayDirty = true;
  }

  if (service.autoInitState != RtcAutoInitState::Writing) {
    return false;
  }

  service.autoInitAttempted = true;
  logRtcRawRegisters("Before auto init");

  RtcTime buildTime;
  if (!buildTimeToRtcTime(buildTime)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("RTC auto init skipped: build time parse failed");
    }
    service.autoInitState = RtcAutoInitState::Failed;
    app.displayDirty = true;
    return true;
  }

  if (ENABLE_SERIAL_LOGGING) {
    Serial.printf("RTC invalid, initializing from build time: %04u-%02u-%02u %02u:%02u:%02u day=%u\n",
                  buildTime.year,
                  buildTime.month,
                  buildTime.date,
                  buildTime.hour,
                  buildTime.minute,
                  buildTime.second,
                  buildTime.day);
  }

  if (!rtcSetTime(buildTime)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("RTC auto init failed");
    }
    service.autoInitState = RtcAutoInitState::Failed;
    app.displayDirty = true;
    return true;
  }

  app.rtcOk = rtcReadTime(app.rtcTime);
  service.lastReadMs = nowMs;
  logRtcRawRegisters("After auto init");
  if (ENABLE_SERIAL_LOGGING) {
    Serial.printf("RTC auto init %s\n", app.rtcOk ? "OK" : "FAILED");
  }
  service.autoInitState = app.rtcOk ? RtcAutoInitState::Idle : RtcAutoInitState::Failed;
  if (app.rtcOk) {
    app.mode = AppMode::Clock;
    app.setting.state = SettingState::SettingMenu;
  }
  scheduleNextRead(service, app, nowMs);
  app.displayDirty = true;
  return true;
}

void rtcServiceBegin(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  service = RtcServiceState{};
  rtcServiceForceRead(service, app, nowMs);
}

bool rtcServiceUpdate(RtcServiceState &service, AppState &app, uint32_t nowMs) {
  bool changed = updateAutoInit(service, app, nowMs);
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
  if (app.rtcOk && app.rtcTime.valid) {
    return nullptr;
  }
  switch (service.autoInitState) {
    case RtcAutoInitState::Writing:
      return "RTC INIT...";
    case RtcAutoInitState::Failed:
      return "RTC INIT FAIL";
    case RtcAutoInitState::Waiting:
      return "RTC INVALID";
    case RtcAutoInitState::Idle:
      return "RTC READ FAIL";
  }
  return "RTC READ FAIL";
}
