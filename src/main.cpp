#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_bt.h>
#include <esp_sleep.h>
#include <stdio.h>
#include <string.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "rtc.h"

using namespace AppConfig;

enum class AppMode : uint8_t {
  Clock,
  Timer,
};

enum class TimerState : uint8_t {
  Idle,
  Adjusting,
  FwdRun,
  FwdPause,
  CdRun,
  CdPause,
  Finished,
};

enum class ButtonId : uint8_t {
  Mode,
  Confirm,
  Cancel,
};

static volatile int32_t encoderDelta = 0;
static volatile int8_t encoderQuarterSteps = 0;
static volatile uint8_t lastEncoderState = 0;
static volatile uint32_t lastEncoderIsrUs = 0;

static AppMode appMode = AppMode::Clock;
static TimerState timerState = TimerState::Idle;
static RtcTime rtcTime;

static uint32_t settingSeconds = 0;
static uint32_t timerSeconds = 0;
static uint32_t lastSecondTickMs = 0;
static uint32_t lastRtcReadMs = 0;
static uint32_t wakeHoldUntilMs = 0;
static uint32_t lastWakeButtonPressMs = 0;
static uint8_t lastRenderedRtcHour = 255;
static uint8_t lastRenderedRtcMinute = 255;
static bool lastRenderedRtcValid = false;
static ButtonId wakeButtonId = ButtonId::Mode;
static bool wakeButtonPressLatched = false;
static bool wakeButtonPressPending = false;
static bool displayDirty = true;
static bool encoderReady = false;
static bool rtcOk = false;

static constexpr uint8_t USER_INPUT_PINS[] = {
    PIN_EC11_A,
    PIN_EC11_B,
    PIN_CONFIRM,
    PIN_MODE,
    PIN_CANCEL,
};
static constexpr size_t HEADER_LINE_LEN = 22;
static constexpr uint8_t HEADER_TITLE_MAX_CHARS = 15;
static constexpr uint8_t HEADER_TIME_COL = 16;
static constexpr uint32_t TIMER_SECOND_MS = 1000;

struct DebouncedButton {
  uint8_t pin = 0;
  bool stableState = HIGH;
  bool lastRaw = HIGH;
  uint32_t lastChangeMs = 0;

  void begin(uint8_t assignedPin) {
    pin = assignedPin;
    const bool raw = digitalRead(pin);
    stableState = raw;
    lastRaw = raw;
    lastChangeMs = millis();
  }

  bool update() {
    const bool raw = digitalRead(pin);
    if (raw != lastRaw) {
      lastRaw = raw;
      lastChangeMs = millis();
    }
    if (millis() - lastChangeMs >= BUTTON_DEBOUNCE_MS && stableState != raw) {
      stableState = raw;
      return stableState == LOW;
    }
    return false;
  }
};

static DebouncedButton modeButton;
static DebouncedButton confirmButton;
static DebouncedButton cancelButton;

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

static void renderHeader(const char *title, bool showCurrentTime) {
  char line[HEADER_LINE_LEN];
  memset(line, ' ', sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  for (uint8_t i = 0; title[i] != '\0' && i < HEADER_TITLE_MAX_CHARS; ++i) {
    line[i] = title[i];
  }

  if (showCurrentTime) {
    char timeText[6];
    if (rtcOk && rtcTime.valid) {
      snprintf(timeText, sizeof(timeText), "%02u:%02u", rtcTime.hour, rtcTime.minute);
    } else {
      snprintf(timeText, sizeof(timeText), "--:--");
    }
    for (uint8_t i = 0; i < 5; ++i) {
      line[HEADER_TIME_COL + i] = timeText[i];
    }
  }

  displayPrintLine(0, line);
}

static void resetTimer() {
  settingSeconds = 0;
  timerSeconds = 0;
  timerState = TimerState::Idle;
}

static void transitionTimerState(TimerState next) {
  timerState = next;
  lastSecondTickMs = millis();
}

static bool timeReached(uint32_t deadlineMs) {
  return static_cast<int32_t>(millis() - deadlineMs) >= 0;
}

static bool rtcMinuteChangedSinceRender() {
  const bool valid = rtcOk && rtcTime.valid;
  if (valid != lastRenderedRtcValid) {
    return true;
  }
  if (!valid) {
    return false;
  }
  return rtcTime.hour != lastRenderedRtcHour || rtcTime.minute != lastRenderedRtcMinute;
}

static void rememberRenderedRtcMinute() {
  lastRenderedRtcValid = rtcOk && rtcTime.valid;
  if (lastRenderedRtcValid) {
    lastRenderedRtcHour = rtcTime.hour;
    lastRenderedRtcMinute = rtcTime.minute;
  } else {
    lastRenderedRtcHour = 255;
    lastRenderedRtcMinute = 255;
  }
}

static uint32_t displayedTimerSeconds() {
  if (timerState == TimerState::Idle || timerState == TimerState::Adjusting) {
    return settingSeconds;
  }
  return timerSeconds;
}

static void renderDateOrStatus(const char *fallback) {
  char line[24];
  if (rtcOk && rtcTime.valid) {
    snprintf(line, sizeof(line), "%04u-%02u-%02u %s",
             rtcTime.year,
             rtcTime.month,
             rtcTime.date,
             weekdayShortName(rtcTime.day));
    displayPrintLineCentered(5, line);
  } else {
    displayPrintLineCentered(5, fallback);
  }
}

static void renderClock() {
  char line[24];

  renderHeader("CLOCK", false);
  displayPrintLine(1, "");
  if (rtcOk && rtcTime.valid) {
    snprintf(line, sizeof(line), "%02u:%02u",
             rtcTime.hour,
             rtcTime.minute);
    displayPrintScaledLineCentered(2, line, 2);
    renderDateOrStatus("RTC READ FAIL");
  } else {
    displayPrintScaledLineCentered(2, "--:--", 2);
    displayPrintLineCentered(5, "RTC READ FAIL");
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
  rememberRenderedRtcMinute();
}

// TIMER 系列页面共用布局：顶部标题 + 当前时间，中间大号计时值，第 5 页状态。
static void renderTimerReadout(const char *title, const char *duration, const char *status) {
  renderHeader(title, true);
  displayPrintLine(1, "");
  displayPrintScaledLineCentered(2, duration, 2);
  if (status != nullptr && status[0] != '\0') {
    displayPrintLineCentered(5, status);
  } else {
    displayPrintLine(5, "");
  }
  displayPrintLine(6, "");
  displayPrintLine(7, "");
  rememberRenderedRtcMinute();
}

static void renderTimer() {
  char duration[12];

  if (timerState == TimerState::Idle || timerState == TimerState::Adjusting) {
    formatDuration(settingSeconds, duration, sizeof(duration));
    renderTimerReadout("TIMER", duration, nullptr);
    return;
  }

  formatDuration(displayedTimerSeconds(), duration, sizeof(duration));

  if (timerState == TimerState::Finished) {
    renderTimerReadout("COUNTDOWN", "00:00:00", "TIME'S UP");
    return;
  }

  if (timerState == TimerState::FwdRun || timerState == TimerState::FwdPause) {
    renderTimerReadout("STOPWATCH", duration, timerState == TimerState::FwdRun ? "RUNNING" : "PAUSED");
    return;
  }

  if (timerState == TimerState::CdRun || timerState == TimerState::CdPause) {
    renderTimerReadout("COUNTDOWN", duration, timerState == TimerState::CdRun ? "REMAINING" : "PAUSED");
    return;
  }
}

static void renderDisplay() {
  if (appMode == AppMode::Clock) {
    renderClock();
  } else {
    renderTimer();
  }
  displayDirty = false;
}

// CLOCK 和 TIMER 会复用相同 page，但对齐方式和字体大小不同；
// 切换模式时清屏并重置缓存，避免残留旧布局。
static void enterMode(AppMode mode) {
  if (appMode == mode) {
    return;
  }
  appMode = mode;
  displayClear();
  displayInvalidateCache();
  lastRenderedRtcHour = 255;
  lastRenderedRtcMinute = 255;
  lastRenderedRtcValid = false;
  displayDirty = true;
  Serial.printf("Mode changed: %s\n", appMode == AppMode::Clock ? "CLOCK" : "TIMER");
}

static void handleModePress() {
  if (timerState == TimerState::Finished) {
    resetTimer();
  }
  enterMode(appMode == AppMode::Clock ? AppMode::Timer : AppMode::Clock);
}

// 确认键在 TIMER 空闲或调整时长后启动计时；运行后作为暂停/恢复键。
static void handleConfirmPress() {
  if (appMode == AppMode::Clock) {
    return;
  }

  switch (timerState) {
    case TimerState::Idle:
    case TimerState::Adjusting:
      if (settingSeconds > 0) {
        timerSeconds = settingSeconds;
        transitionTimerState(TimerState::CdRun);
      } else {
        timerSeconds = 0;
        transitionTimerState(TimerState::FwdRun);
      }
      break;
    case TimerState::FwdRun:
      transitionTimerState(TimerState::FwdPause);
      break;
    case TimerState::FwdPause:
      transitionTimerState(TimerState::FwdRun);
      break;
    case TimerState::CdRun:
      transitionTimerState(TimerState::CdPause);
      break;
    case TimerState::CdPause:
      transitionTimerState(TimerState::CdRun);
      break;
    case TimerState::Finished:
      resetTimer();
      break;
  }
  displayDirty = true;
}

static void handleCancelPress() {
  if (appMode == AppMode::Clock) {
    return;
  }
  resetTimer();
  displayDirty = true;
}

static void handleAnyButton(ButtonId button) {
  switch (button) {
    case ButtonId::Mode:
      handleModePress();
      break;
    case ButtonId::Confirm:
      handleConfirmPress();
      break;
    case ButtonId::Cancel:
      handleCancelPress();
      break;
  }
}

// 旋转编码器只在计时器空闲或调整状态下修改时长。
static void handleEncoderSteps(int32_t steps) {
  if (steps == 0) {
    return;
  }
  if (appMode != AppMode::Timer) {
    return;
  }
  if (timerState != TimerState::Idle && timerState != TimerState::Adjusting) {
    return;
  }

  int64_t next = static_cast<int64_t>(settingSeconds) +
                 static_cast<int64_t>(steps) * static_cast<int64_t>(TIMER_STEP_SECONDS);
  if (next < 0) {
    next = 0;
  }
  if (next > TIMER_MAX_SECONDS) {
    next = TIMER_MAX_SECONDS;
  }

  settingSeconds = static_cast<uint32_t>(next);
  timerState = TimerState::Adjusting;
  displayDirty = true;
}

// 运行态不进入 Light Sleep，秒 tick 用 millis() 累计差值补偿普通 loop 抖动。
static void handleSecondTick() {
  if (timerState == TimerState::FwdRun) {
    if (timerSeconds < TIMER_MAX_SECONDS) {
      timerSeconds++;
    }
    displayDirty = true;
    return;
  }

  if (timerState == TimerState::CdRun) {
    if (timerSeconds > 1) {
      timerSeconds--;
    } else {
      timerSeconds = 0;
      timerState = TimerState::Finished;
    }
    displayDirty = true;
  }
}

static void readRtcIfDue() {
  const uint32_t now = millis();
  if (now - lastRtcReadMs < RTC_REFRESH_MS) {
    return;
  }
  rtcOk = rtcReadTime(rtcTime);
  lastRtcReadMs = now;
  if (rtcMinuteChangedSinceRender()) {
    displayDirty = true;
  }
}

static uint8_t readEncoderState() {
  return (digitalRead(PIN_EC11_A) ? 0x02 : 0x00) |
         (digitalRead(PIN_EC11_B) ? 0x01 : 0x00);
}

static void IRAM_ATTR decodeEncoderState(uint8_t state) {
  static const int8_t transitionTable[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0,
  };
  const uint8_t transition = (lastEncoderState << 2) | state;
  const int8_t quarterStep = transitionTable[transition & 0x0F];

  if (quarterStep != 0) {
    encoderQuarterSteps += quarterStep;
    if (encoderQuarterSteps >= ENCODER_QUARTER_STEPS_PER_CLICK) {
      encoderDelta += ENCODER_DIRECTION;
      encoderQuarterSteps = 0;
    } else if (encoderQuarterSteps <= -ENCODER_QUARTER_STEPS_PER_CLICK) {
      encoderDelta -= ENCODER_DIRECTION;
      encoderQuarterSteps = 0;
    }
  } else if (state != lastEncoderState) {
    encoderQuarterSteps = 0;
  }
  lastEncoderState = state;
}

static void IRAM_ATTR handleEncoderChange() {
  const uint32_t nowUs = micros();
  if (nowUs - lastEncoderIsrUs < ENCODER_ISR_MIN_US) {
    return;
  }
  lastEncoderIsrUs = nowUs;

  // 解码 A/B 灰码跳变。不同 EC11 模块每个卡点产生的有效跳变数可能不同，
  // 因此阈值放在 config.h 里配置。
  decodeEncoderState(readEncoderState());
}

static bool userInputHeldLow() {
  for (uint8_t pin : USER_INPUT_PINS) {
    if (digitalRead(pin) == LOW) {
      return true;
    }
  }
  return false;
}

static uint8_t buttonPin(ButtonId button) {
  switch (button) {
    case ButtonId::Mode:
      return PIN_MODE;
    case ButtonId::Confirm:
      return PIN_CONFIRM;
    case ButtonId::Cancel:
      return PIN_CANCEL;
  }
  return PIN_MODE;
}

static bool buttonHeldLow(ButtonId button) {
  return digitalRead(buttonPin(button)) == LOW;
}

static bool detectWakeButton(ButtonId &button) {
  if (buttonHeldLow(ButtonId::Mode)) {
    button = ButtonId::Mode;
    return true;
  }
  if (buttonHeldLow(ButtonId::Confirm)) {
    button = ButtonId::Confirm;
    return true;
  }
  if (buttonHeldLow(ButtonId::Cancel)) {
    button = ButtonId::Cancel;
    return true;
  }
  return false;
}

static void setupLightSleepWakeup() {
  esp_sleep_enable_timer_wakeup(IDLE_LIGHT_SLEEP_US);
  for (uint8_t pin : USER_INPUT_PINS) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(pin), GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
}

static void handleElapsedSecondTicks() {
  const uint32_t now = millis();
  while (now - lastSecondTickMs >= TIMER_SECOND_MS) {
    lastSecondTickMs += TIMER_SECOND_MS;
    handleSecondTick();
  }
}

static bool canEnterIdleLightSleep() {
  return appMode == AppMode::Timer &&
         timerState == TimerState::Idle &&
         !displayDirty &&
         !userInputHeldLow() &&
         (wakeHoldUntilMs == 0 || timeReached(wakeHoldUntilMs));
}

static bool shouldRefreshDisplay() {
  return displayDirty;
}

static void consumeWakeButtonPressIfNeeded() {
  // Light Sleep 唤醒可能早于普通消抖采样；pending 记录这次唤醒，
  // latched 保证同一次按住按键只消费一次，避免释放前重复触发。
  if (!buttonHeldLow(wakeButtonId)) {
    wakeButtonPressLatched = false;
    return;
  }
  if (wakeButtonPressLatched) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastWakeButtonPressMs < WAKE_BUTTON_REPEAT_GUARD_MS) {
    wakeButtonPressLatched = true;
    return;
  }

  wakeButtonPressLatched = true;
  lastWakeButtonPressMs = now;
  handleAnyButton(wakeButtonId);
}

static void updateWakeButtonLatchRelease() {
  if (!buttonHeldLow(wakeButtonId)) {
    wakeButtonPressLatched = false;
  }
}

static bool wakeLatchedFor(ButtonId button) {
  return wakeButtonPressLatched && wakeButtonId == button;
}

static void enterLightSleepIfIdle() {
  if (!canEnterIdleLightSleep()) {
    delay(5);
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_enable_timer_wakeup(IDLE_LIGHT_SLEEP_US);
  esp_light_sleep_start();
  if (userInputHeldLow()) {
    wakeHoldUntilMs = millis() + WAKE_INPUT_HOLD_MS;
    wakeButtonPressPending = detectWakeButton(wakeButtonId);
  }
}

static void setupEncoderInterrupts() {
  lastEncoderState = readEncoderState();
  attachInterrupt(digitalPinToInterrupt(PIN_EC11_A), handleEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_EC11_B), handleEncoderChange, CHANGE);
  encoderReady = true;
}

static void disableRadios() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  // Arduino-ESP32 默认不启动 BT；这里保留防御性关闭，避免未来引入蓝牙后漏关。
  esp_bt_controller_disable();
}

void setup() {
  setCpuFrequencyMhz(CPU_FREQUENCY_MHZ);
  disableRadios();
  Serial.begin(115200);
  delay(300);

  rtcBegin();

  pinMode(PIN_EC11_A, INPUT_PULLUP);
  pinMode(PIN_EC11_B, INPUT_PULLUP);
  pinMode(PIN_CONFIRM, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_CANCEL, INPUT_PULLUP);

  displayBegin();
  displayClear();
  displayInvalidateCache();
  displayPrintLine(0, "BOOTING...");

  modeButton.begin(PIN_MODE);
  confirmButton.begin(PIN_CONFIRM);
  cancelButton.begin(PIN_CANCEL);

  lastRtcReadMs = millis();
  lastSecondTickMs = millis();

  displayInvalidateCache();
  renderDisplay();

  rtcOk = rtcReadTime(rtcTime);
  lastRtcReadMs = millis();
  displayDirty = true;

  setupEncoderInterrupts();
  setupLightSleepWakeup();
  Serial.printf("focus-clock ready, CPU %u MHz\n", getCpuFrequencyMhz());
}

void loop() {
  int32_t steps = 0;
  if (encoderReady) {
    noInterrupts();
    steps = encoderDelta;
    encoderDelta = 0;
    interrupts();
  }

  if (steps != 0) {
    handleEncoderSteps(steps);
  }

  if (wakeButtonPressPending) {
    wakeButtonPressPending = false;
    consumeWakeButtonPressIfNeeded();
  }

  updateWakeButtonLatchRelease();

  if (modeButton.update() && !wakeLatchedFor(ButtonId::Mode)) {
    handleAnyButton(ButtonId::Mode);
  }
  if (confirmButton.update() && !wakeLatchedFor(ButtonId::Confirm)) {
    handleAnyButton(ButtonId::Confirm);
  }
  if (cancelButton.update() && !wakeLatchedFor(ButtonId::Cancel)) {
    handleAnyButton(ButtonId::Cancel);
  }

  handleElapsedSecondTicks();

  readRtcIfDue();

  if (shouldRefreshDisplay()) {
    renderDisplay();
  }

  enterLightSleepIfIdle();
}
