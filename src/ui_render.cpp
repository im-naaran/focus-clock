#include "ui_render.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "time_sync_logic.h"
#include "timer_model.h"

using namespace AppConfig;

static const char *weekdayShortName(uint8_t day) {
  static const char *names[] = {"", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  return day <= 7 ? names[day] : "";
}

static void formatDuration(uint32_t seconds, char *buffer, size_t bufferSize) {
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds / 60) % 60;
  const uint32_t secs = seconds % 60;
  snprintf(buffer, bufferSize, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(secs));
}

static void renderHeader(const char *title, const AppState &app, bool showCurrentTime) {
  char line[HEADER_LINE_LEN];
  memset(line, ' ', sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  for (uint8_t i = 0; title[i] != '\0' && i < HEADER_TITLE_MAX_CHARS; ++i) {
    line[i] = title[i];
  }

  if (showCurrentTime) {
    char timeText[6];
    if (app.rtcOk && app.rtcTime.valid) {
      snprintf(timeText, sizeof(timeText), "%02u:%02u", app.rtcTime.hour, app.rtcTime.minute);
    } else {
      snprintf(timeText, sizeof(timeText), "12:00");
    }
    for (uint8_t i = 0; i < 5; ++i) {
      line[HEADER_TIME_COL + i] = timeText[i];
    }
  }

  displayPrintLine(0, line);
}

static void renderDateOrStatus(const AppState &app, const char *rtcStatusText) {
  char line[24];
  if (app.rtcOk && app.rtcTime.valid) {
    snprintf(line, sizeof(line), "%04u-%02u-%02u %s",
             app.rtcTime.year,
             app.rtcTime.month,
             app.rtcTime.date,
             weekdayShortName(app.rtcTime.day));
    displayPrintLineCentered(5, line);
  } else {
    displayPrintLineCentered(5, rtcStatusText != nullptr ? rtcStatusText : "RTC READ FAIL");
  }
}

static void renderClock(const AppState &app, const char *rtcStatusText) {
  char line[24];

  renderHeader("CLOCK", app, false);
  displayPrintLine(1, "");
  if (app.rtcOk && app.rtcTime.valid) {
    snprintf(line, sizeof(line), "%02u:%02u", app.rtcTime.hour, app.rtcTime.minute);
    displayPrintScaledLineCentered(2, line, 2);
    renderDateOrStatus(app, rtcStatusText);
  } else {
    displayPrintScaledLineCentered(2, "12:00", 2);
    displayPrintLineCentered(5, rtcStatusText != nullptr ? rtcStatusText : "RTC READ FAIL");
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static const char *timerTitle(TimerState state) {
  switch (state) {
    case TimerState::FwdRun:
    case TimerState::FwdPause:
      return "STOPWATCH";
    case TimerState::CdRun:
    case TimerState::CdPause:
    case TimerState::Finished:
      return "COUNTDOWN";
    case TimerState::Idle:
    case TimerState::Adjusting:
      return "TIMER";
  }
  return "TIMER";
}

static const char *timerStatus(const AppState &app, const char *rtcStatusText) {
  switch (app.timer.state) {
    case TimerState::FwdRun:
      return "RUNNING";
    case TimerState::FwdPause:
      return "PAUSED";
    case TimerState::CdRun:
      return "REMAINING";
    case TimerState::CdPause:
      return "PAUSED";
    case TimerState::Finished:
      return "TIME'S UP";
    case TimerState::Idle:
    case TimerState::Adjusting:
      return rtcStatusText;
  }
  return rtcStatusText;
}

static void renderTimer(const AppState &app, const char *rtcStatusText) {
  char duration[12];
  if (app.timer.state == TimerState::Finished) {
    snprintf(duration, sizeof(duration), "00:00:00");
  } else {
    formatDuration(timerDisplayedSeconds(app.timer), duration, sizeof(duration));
  }

  renderHeader(timerTitle(app.timer.state), app, true);
  displayPrintLine(1, "");
  displayPrintScaledLineCentered(2, duration, 2);

  const char *status = timerStatus(app, rtcStatusText);
  if (status != nullptr && status[0] != '\0') {
    displayPrintLineCentered(5, status);
  } else {
    displayPrintLine(5, "");
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static void renderSettingMenu(const AppState &app) {
  static const char *labels[SETTING_MENU_ITEM_COUNT] = {
      "BRIGHTNESS", "TIME SET", "TIME SYNC", "SCREEN SCHEDULE",
      "WIFI CONFIG", "WIFI"};
  const uint8_t selected = static_cast<uint8_t>(app.setting.selectedItem);
  const uint8_t windowStart = settingMenuWindowStart(app.setting.selectedItem);
  char line[LINE_CACHE_LEN];
  displayPrintLine(2, "");
  for (uint8_t row = 0; row < SETTING_MENU_VISIBLE_ROWS; ++row) {
    const uint8_t index = windowStart + row;
    snprintf(line, sizeof(line), "%c %s", index == selected ? '>' : ' ',
             labels[index]);
    displayPrintLine(3 + row, line);
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static void renderBrightnessEdit(const AppState &app) {
  char line[16];
  snprintf(line, sizeof(line), "LEVEL %u", app.config.brightnessLevel);
  displayPrintLine(2, "");
  displayPrintLine(3, "BRIGHTNESS");
  displayPrintLine(4, line);
  displayPrintLine(5, "");
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static void renderTimeEdit(const AppState &app) {
  char timeText[6];
  const bool blink = app.setting.showBlinkField;
  if (app.setting.state == SettingState::TimeEditHour && !blink) {
    snprintf(timeText, sizeof(timeText), "  :%02u", app.setting.editMinute);
  } else if (app.setting.state == SettingState::TimeEditMinute && !blink) {
    snprintf(timeText, sizeof(timeText), "%02u:  ", app.setting.editHour);
  } else {
    snprintf(timeText, sizeof(timeText), "%02u:%02u", app.setting.editHour, app.setting.editMinute);
  }

  displayPrintLine(2, "");
  displayPrintLine(3, "TIME SET");
  displayPrintLineCentered(4, timeText);
  displayPrintLine(5, "");
  displayPrintLine(6, "");
  displayPrintLine(7, "");

  if (app.setting.timeSetErrorVisible) {
    displayDrawDialog(app.setting.timeSetError);
  }
}

static void renderTimeSyncInfo(const AppState &app) {
  char localTime[20] = {};
  const bool hasLastSuccess = timeSyncFormatLocalEpoch(
      app.lastTimeSyncSuccessEpoch, localTime, sizeof(localTime));

  displayPrintLine(2, "");
  displayPrintLineCentered(3, "LAST SUCCESS");
  if (hasLastSuccess) {
    // The shared formatter guarantees YYYY-MM-DD HH:MM:SS on success.
    localTime[10] = '\0';
    displayPrintLineCentered(4, localTime);
    displayPrintLineCentered(5, localTime + 11);
  } else {
    displayPrintLineCentered(4, "NEVER");
    displayPrintLine(5, "");
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static void formatNightOffTime(const AppState &app, bool editingStart, char *buffer, size_t bufferSize) {
  const bool blink = app.setting.showBlinkField;
  const uint8_t hour = editingStart ? app.setting.editNightOffHour : app.setting.editNightOnHour;
  const uint8_t minute = editingStart ? app.setting.editNightOffMinute : app.setting.editNightOnMinute;
  const bool editingHour = app.setting.state == SettingState::NightOffStartHourEdit ||
                           app.setting.state == SettingState::NightOffEndHourEdit;
  const bool editingMinute = app.setting.state == SettingState::NightOffStartMinuteEdit ||
                             app.setting.state == SettingState::NightOffEndMinuteEdit;

  if (editingHour && !blink) {
    snprintf(buffer, bufferSize, "  :%02u", minute);
  } else if (editingMinute && !blink) {
    snprintf(buffer, bufferSize, "%02u:  ", hour);
  } else {
    snprintf(buffer, bufferSize, "%02u:%02u", hour, minute);
  }
}

static void renderNightOffEdit(const AppState &app) {
  char line[8];
  displayPrintLine(2, "");

  switch (app.setting.state) {
    case SettingState::NightOffEnabledEdit:
      displayPrintLine(3, "SCREEN SCHEDULE");
      displayPrintLineCentered(4, app.setting.editNightOffEnabled ? "ON" : "OFF");
      break;
    case SettingState::NightOffStartHourEdit:
    case SettingState::NightOffStartMinuteEdit:
      formatNightOffTime(app, true, line, sizeof(line));
      displayPrintLine(3, "OFF AT");
      displayPrintLineCentered(4, line);
      break;
    case SettingState::NightOffEndHourEdit:
    case SettingState::NightOffEndMinuteEdit:
      formatNightOffTime(app, false, line, sizeof(line));
      displayPrintLine(3, "ON AT");
      displayPrintLineCentered(4, line);
      break;
    case SettingState::SettingMenu:
    case SettingState::BrightnessEdit:
    case SettingState::TimeEditHour:
    case SettingState::TimeEditMinute:
    case SettingState::TimeSyncInfo:
    case SettingState::WifiConfigPortal:
    case SettingState::WifiPolicyEdit:
      break;
  }

  displayPrintLine(5, "");
  displayPrintLine(6, "");
  displayPrintLine(7, "");
}

static void renderWifiConfigPortal(const AppState &app) {
  const WifiConfigPortalPhase phase = settingWifiConfigPortalPhase(
      app.wifiRuntime.configModeRunning,
      app.wifiRuntime.apClientConnected);
  if (phase == WifiConfigPortalPhase::Starting) {
    displayPrintLine(1, "");
    displayPrintLine(2, "");
    displayPrintLineCentered(3, "STARTING AP");
    displayPrintLine(4, "");
    displayPrintLine(5, "");
    displayPrintLine(6, "");
    displayPrintLineCentered(7, "CANCEL TO EXIT");
    return;
  }
  if (phase == WifiConfigPortalPhase::ConnectPhone) {
    displayPrintLine(1, "");
    displayPrintLineCentered(2, "CONNECT PHONE");
    displayPrintLineCentered(3, "OPEN WIFI");
    displayPrintLineCentered(4, app.wifiRuntime.apSsid);
    displayPrintLineCentered(5, "OPEN NETWORK");
    displayPrintLine(6, "");
    displayPrintLineCentered(7, "CANCEL TO EXIT");
    return;
  }

  char protocol[9] = {};
  const char *address = app.wifiRuntime.portalUrl;
  const char *separator = strstr(app.wifiRuntime.portalUrl, "://");
  if (separator != nullptr) {
    const size_t protocolLength =
        static_cast<size_t>(separator - app.wifiRuntime.portalUrl) + 3;
    if (protocolLength < sizeof(protocol)) {
      memcpy(protocol, app.wifiRuntime.portalUrl, protocolLength);
      protocol[protocolLength] = '\0';
      address = separator + 3;
    }
  }
  displayPrintLine(1, "");
  displayPrintLineCentered(2, "OPEN IN BROWSER");
  displayPrintLineCentered(3, protocol[0] != '\0' ? protocol
                                                   : app.wifiRuntime.portalUrl);
  displayPrintLineCentered(4, address);
  displayPrintLine(5, "");
  displayPrintLine(6, "");
  displayPrintLineCentered(7, "CANCEL TO EXIT");
}

static void renderWifiPolicyEdit(const AppState &app) {
  displayPrintLine(2, "");
  displayPrintLine(3, "WIFI POLICY");
  displayPrintLineCentered(4,
                           app.setting.editWifiPolicy == WifiPolicy::Auto
                               ? "AUTO"
                               : "OFF");
  displayPrintLine(5, "");
  displayPrintLine(6, "");
  displayPrintLine(7, "");
  if (app.setting.wifiPolicySaveErrorVisible) {
    displayDrawDialog("SAVE FAILED");
  }
}

static void renderSetting(const AppState &app) {
  const bool wifiConfigPage =
      app.setting.state == SettingState::WifiConfigPortal;
  const bool timeSyncPage = app.setting.state == SettingState::TimeSyncInfo;
  const char *title = wifiConfigPage ? "WIFI CONFIG"
                                    : (timeSyncPage ? "TIME SYNC" : "SETTING");
  renderHeader(title, app, !wifiConfigPage);
  displayPrintLine(1, "");

  switch (app.setting.state) {
    case SettingState::SettingMenu:
      renderSettingMenu(app);
      break;
    case SettingState::BrightnessEdit:
      renderBrightnessEdit(app);
      break;
    case SettingState::TimeEditHour:
    case SettingState::TimeEditMinute:
      renderTimeEdit(app);
      break;
    case SettingState::TimeSyncInfo:
      renderTimeSyncInfo(app);
      break;
    case SettingState::NightOffEnabledEdit:
    case SettingState::NightOffStartHourEdit:
    case SettingState::NightOffStartMinuteEdit:
    case SettingState::NightOffEndHourEdit:
    case SettingState::NightOffEndMinuteEdit:
      renderNightOffEdit(app);
      break;
    case SettingState::WifiConfigPortal:
      renderWifiConfigPortal(app);
      break;
    case SettingState::WifiPolicyEdit:
      renderWifiPolicyEdit(app);
      break;
  }
}

void renderApp(const AppState &app, const char *rtcStatusText) {
  switch (app.mode) {
    case AppMode::Clock:
      renderClock(app, rtcStatusText);
      break;
    case AppMode::Timer:
      renderTimer(app, rtcStatusText);
      break;
    case AppMode::Setting:
      renderSetting(app);
      break;
  }
  const bool showWifi = app.mode != AppMode::Setting &&
                        app.wifiRuntime.networkTaskActive &&
                        app.wifiRuntime.connectionState ==
                            WifiConnectionState::Connected;
  displaySetWifiConnectedIcon(showWifi);
}
