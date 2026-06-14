#include "ui_render.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
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
      snprintf(timeText, sizeof(timeText), "--:--");
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
    displayPrintScaledLineCentered(2, "--:--", 2);
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
  (void)app;
  displayPrintLine(2, "");
  displayPrintLine(3, app.setting.selectedItem == SettingMenuItem::Brightness ? "> BRIGHTNESS" : "  BRIGHTNESS");
  displayPrintLine(4, app.setting.selectedItem == SettingMenuItem::TimeSet ? "> TIME SET" : "  TIME SET");
  displayPrintLine(5, "");
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

static void renderSetting(const AppState &app) {
  renderHeader("SETTING", app, true);
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
}
