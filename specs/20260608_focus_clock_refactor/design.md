# 技术设计：Focus Clock 重构

日期：2026-06-08
Feature：focus_clock_refactor
阶段：Phase 2

## 设计目标

本设计面向 ESP32-C3 / Arduino / PlatformIO 固件重构，目标是在标准生产代码结构中重新实现 Focus Clock。旧代码只作为参考，不作为正确性基准。

核心目标：

- 保持当前硬件接线和 PlatformIO 环境。
- 明确拆分硬件驱动、输入、状态机、页面渲染、持久化和低功耗职责。
- 将 SETTING 作为新增功能完整设计。
- 隔离 WS2812 临时输入反馈，便于后续移除。
- 改进 RTC 读取策略，正常状态使用“分钟边界优先 + 最长 30 秒兜底”，异常和写入确认使用 1 秒短周期。

覆盖需求：R-01 到 R-86，AC-01 到 AC-13。

## 分层与分离原则

本次重构不追求“大框架”，但需要明确分离边界，避免所有逻辑继续堆在 `main.cpp`。

分层原则：

- `main.cpp` 只负责启动顺序和主循环编排，不承载具体业务规则。
- 驱动层只处理硬件协议和设备能力，不读取或修改应用模式。
- 服务层封装跨循环的设备状态，例如 RTC 调度、输入事件、Light Sleep。
- 业务模型层只处理纯状态转换，例如 TIMER 和 SETTING，不直接调用 GPIO、I2C、Preferences 或 WS2812。
- 渲染层只根据状态绘制页面，不改变业务状态。
- 配置层集中管理硬件引脚、行为参数和 UI 映射，禁止魔法数字散落在模块中。

依赖方向：

```text
main.cpp
  -> app_state / timer_model / ui_render
  -> input / feedback / rtc_service / persistence / sleep_manager
  -> display / rtc

ui_render -> display
rtc_service -> rtc
sleep_manager -> input state helpers / feedback status
business models -> config constants only
```

禁止的依赖：

- `display.*` 不依赖 `AppState`。
- `rtc.*` 不依赖 UI、SETTING 或 TIMER。
- `timer_model.*` 不依赖显示、输入、RTC 或 Preferences。
- `feedback.*` 不反向调用业务状态机。
- `persistence.*` 不直接修改 OLED 或应用模式。

## 总体方案

采用轻量模块化设计，仍保留 Arduino 主循环模型。`loop()` 只做调度，不直接散落业务细节。

建议生产目录：

```text
src/
├── main.cpp
├── app_state.h
├── config.h
├── config_display.h
├── config_hardware.h
├── config_timing.h
├── display.h
├── display.cpp
├── feedback.h
├── feedback.cpp
├── input.h
├── input.cpp
├── persistence.h
├── persistence.cpp
├── rtc.h
├── rtc.cpp
├── rtc_service.h
├── rtc_service.cpp
├── sleep_manager.h
├── sleep_manager.cpp
├── timer_model.h
├── timer_model.cpp
├── ui_render.h
└── ui_render.cpp
lib/
└── LeobogKnob/
```

模块职责：

| 模块 | 职责 |
| --- | --- |
| `config.h` | 汇总配置入口，只 include 具体配置头 |
| `config_hardware.h` | GPIO、I2C 地址、硬件开关 |
| `config_timing.h` | 消抖、长按、RTC 调度、Light Sleep、计时器步进 |
| `config_display.h` | OLED 尺寸、亮度档位映射、UI 文本长度 |
| `app_state.*` | 顶层模式、SETTING 子状态、共享状态结构 |
| `timer_model.*` | TIMER 状态机和秒 tick 逻辑 |
| `input.*` | Mode/Cancel 消抖、Mode 长按、旋钮事件、Confirm 事件 |
| `feedback.*` | WS2812 临时输入反馈适配层 |
| `display.*` | SSD1306 基础绘制、缓存、对比度、提示框 |
| `ui_render.*` | CLOCK/TIMER/SETTING 页面渲染 |
| `rtc.*` | DS1302 低层读写、校验、写入确认 |
| `rtc_service.*` | RTC 自动初始化、读取调度、状态文本 |
| `persistence.*` | Preferences 读写 UI 配置 |
| `sleep_manager.*` | Light Sleep 条件判断、唤醒源和唤醒输入桥接 |
| `main.cpp` | 初始化和主循环编排 |

选择原因：

- 代码仍适合嵌入式小项目，不引入复杂框架。
- SETTING、RTC 调度和 Light Sleep 风险较高，单独模块更容易审查。
- WS2812 后续可能移除，`feedback.*` 可让业务只调用语义化接口。
- 配置文件按硬件、时间和显示拆分，既保留集中管理，又减少单个 `config.h` 变成杂项堆积。

## 配置文件设计

配置使用 `namespace AppConfig` 下的 `constexpr`，避免运行期成本。

`config_hardware.h`：

- GPIO 分配。
- OLED I2C 地址和总线频率。
- WS2812 设备数量和引脚。
- 与硬件存在直接关系的启用开关。

`config_timing.h`：

- `BUTTON_DEBOUNCE_MS`
- `MODE_LONG_PRESS_MS`
- `SETTING_BLINK_MS`
- `RTC_SHORT_REFRESH_MS`
- `RTC_NORMAL_MAX_REFRESH_MS`
- `RTC_MINUTE_REFRESH_GRACE_MS`
- `RTC_AUTO_INIT_DELAY_MS`
- `INPUT_LED_FLASH_MS`
- `IDLE_LIGHT_SLEEP_US`
- `WAKE_INPUT_HOLD_MS`
- `WAKE_BUTTON_REPEAT_GUARD_MS`
- `TIMER_STEP_SECONDS`
- `TIMER_MAX_SECONDS`
- `KNOB_ROTATION_SETTLE_MS`

`config_display.h`：

- OLED page、宽高、行缓存长度。
- 默认亮度档位。
- 亮度档位到 contrast 的映射函数或表。
- UI 字符长度限制。

`config.h`：

```cpp
#pragma once

#include "config_hardware.h"
#include "config_timing.h"
#include "config_display.h"
```

约束：

- 业务代码统一 include `config.h`，特殊底层模块可直接 include 对应配置头。
- 配置头只放常量和简单纯函数，不放可变状态。
- 运行期用户配置放 `UiConfig` 和 `persistence.*`，不放配置头。

## 组件边界

| 组件 | 输入 | 输出 | 不负责 |
| --- | --- | --- | --- |
| `input.*` | GPIO 电平、LeobogKnob 事件、唤醒桥接状态 | 规范化按钮事件、旋钮 step | 页面切换、计时器修改、LED 反馈 |
| `timer_model.*` | Confirm/Cancel/旋钮调整、当前 `millis()` | Timer 状态和显示秒数 | 显示绘制、声音/LED、RTC |
| `rtc.*` | DS1302 GPIO 操作请求 | 原始寄存器、校验后的 `RtcTime`、写入结果 | 自动初始化、读取调度、UI 状态文本 |
| `rtc_service.*` | 当前时间、RTC 读写结果 | `rtcOk`、`rtcTime`、状态文本、下次读取时间 | OLED 绘制、用户输入 |
| `display.*` | 文本、page、contrast、提示框消息 | OLED 像素输出 | 页面布局决策、业务状态变化 |
| `ui_render.*` | `AppState`、RTC 状态文本 | OLED 页面内容 | 输入处理、持久化、RTC 写入 |
| `persistence.*` | UI 配置读写请求 | 亮度档位和保存结果 | 应用配置到 OLED、SETTING 导航 |
| `feedback.*` | 语义化反馈事件 | WS2812 闪烁状态 | 决定何时反馈、业务动作 |
| `sleep_manager.*` | App 状态、输入保持状态、反馈状态 | 是否进入 Light Sleep、唤醒桥接信息 | 按键短/长按判定、业务分发 |

## 数据模型

### 枚举类型

```cpp
enum class AppMode : uint8_t {
  Clock,
  Timer,
  Setting,
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

enum class SettingState : uint8_t {
  SettingMenu,
  BrightnessEdit,
  TimeEditHour,
  TimeEditMinute,
};

enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
};

enum class RtcAutoInitState : uint8_t {
  Idle,
  Waiting,
  Writing,
  Failed,
};

enum class ButtonId : uint8_t {
  Mode,
  Confirm,
  Cancel,
};

enum class ButtonEventType : uint8_t {
  Pressed,
  ShortReleased,
  LongPressed,
};
```

### 应用状态

```cpp
struct UiConfig {
  uint8_t brightnessLevel = DEFAULT_BRIGHTNESS_LEVEL;
};

struct TimerModel {
  TimerState state = TimerState::Idle;
  uint32_t settingSeconds = 0;
  uint32_t timerSeconds = 0;
  uint32_t lastSecondTickMs = 0;
};

struct SettingModel {
  SettingState state = SettingState::SettingMenu;
  SettingMenuItem selectedItem = SettingMenuItem::Brightness;
  uint8_t editHour = 0;
  uint8_t editMinute = 0;
  bool showBlinkField = true;
  uint32_t lastBlinkToggleMs = 0;
  bool timeSetErrorVisible = false;
  char timeSetError[22] = {};
};

struct AppState {
  AppMode mode = AppMode::Clock;
  TimerModel timer;
  SettingModel setting;
  UiConfig config;
  RtcTime rtcTime;
  bool rtcOk = false;
  bool displayDirty = true;
};
```

### RTC 服务状态

`rtc_service` 持有读取调度和自动初始化状态：

```cpp
struct RtcServiceState {
  RtcAutoInitState autoInitState = RtcAutoInitState::Idle;
  bool autoInitAttempted = false;
  uint32_t autoInitDueMs = 0;
  uint32_t nextReadDueMs = 0;
  uint32_t lastReadMs = 0;
};
```

## 初始化流程

`setup()` 顺序：

1. 设置 CPU 频率为 `CPU_FREQUENCY_MHZ`。
2. 关闭 Wi-Fi 和蓝牙。
3. 初始化 Serial。
4. 初始化 `feedback`，默认关闭 WS2812。
5. 初始化 DS1302 GPIO。
6. 初始化输入引脚和输入模块。
7. 初始化 OLED，清屏并显示 `BOOTING...`。
8. 通过 `persistenceLoadConfig()` 读取亮度档位，非法值回退默认 3 档。
9. 调用 `displaySetContrast(brightnessToContrast(level))` 应用亮度。
10. 初始化 `AppState` 默认 CLOCK 和 TIMER Idle。
11. 执行一次 RTC 立即读取；失败则安排自动初始化。
12. 初始化旋钮库和 Confirm 按钮事件。
13. 配置 Light Sleep 唤醒源。
14. 标记 `displayDirty = true`，进入首次渲染。

追踪需求：R-01 到 R-05、R-41 到 R-48、R-63 到 R-86。

## 主循环调度

`loop()` 建议顺序：

1. `inputUpdate()` 采集按钮和旋钮事件。
2. 派发输入事件到应用状态机，并调用 `feedbackFlash(...)`。
3. `timerUpdateElapsed()` 根据 `millis()` 补偿处理秒 tick。
4. `rtcServiceUpdate()` 处理自动初始化和读取调度。
5. `settingUpdateBlink()` 处理 TIME SET 字段闪烁。
6. 如 `displayDirty`，调用 `renderApp(...)`。
7. `feedbackUpdate()` 关闭到期 LED。
8. `sleepManagerMaybeEnter()` 在满足条件时进入 Light Sleep，否则短 delay。

调度原则：

- 所有时间比较使用有符号差值函数，兼容 `millis()` 回绕。
- `timerUpdateElapsed()` 不依赖 RTC，保证 SETTING 页面中计时继续运行。
- 只有页面内容、状态、分钟显示或错误提示变化时才标记 `displayDirty`。

## 输入设计

### Mode 按钮

Mode 需要支持短按和 3 秒长按：

- 按下后进入候选状态。
- 持续按下达到 `MODE_LONG_PRESS_MS` 后派发 `LongPressed`，并设置 `longConsumed = true`。
- 释放时，如果 `longConsumed == false`，派发 `ShortReleased`。
- 释放时，如果 `longConsumed == true`，不派发短按。

应用响应：

| 当前模式 | Mode 短按 | Mode 长按 |
| --- | --- | --- |
| CLOCK | 切 TIMER | 进入 SETTING |
| TIMER | 切 CLOCK，Finished 先 reset | 进入 SETTING |
| SETTING | 无业务动作 | 无业务动作 |

追踪需求：R-08 到 R-12、R-62、R-63。

### Confirm 和 Cancel

Confirm 由 `LeobogKnob` 按钮事件提供，只处理按下事件。

Cancel 使用软件消抖，只处理按下事件。

二者按当前 `AppMode` 分发：

- CLOCK：无业务动作。
- TIMER：按 TIMER 状态机处理。
- SETTING：按 SETTING 子状态处理。

### 旋钮旋转

旋钮 step 按当前上下文处理：

| 当前状态 | 行为 |
| --- | --- |
| CLOCK | 无业务动作 |
| TIMER Idle/Adjusting | 调整 `settingSeconds` |
| TIMER 其他状态 | 忽略 |
| SETTING menu | 切换菜单项 |
| BrightnessEdit | 调整亮度、应用、实时保存 |
| TimeEditHour | 调整小时，0..23 环绕或夹紧 |
| TimeEditMinute | 调整分钟，0..59 环绕或夹紧 |

小时和分钟建议采用环绕调整，因为旋钮设置体验更好；实现任务中应在代码注释或测试点明确。

## TIMER 状态机

`timer_model` 提供：

```cpp
void timerReset(TimerModel &timer);
void timerHandleConfirm(TimerModel &timer, uint32_t nowMs);
void timerHandleCancel(TimerModel &timer);
void timerAdjustSetting(TimerModel &timer, int32_t steps);
bool timerUpdateElapsed(TimerModel &timer, uint32_t nowMs);
bool timerIsRunning(const TimerModel &timer);
uint32_t timerDisplayedSeconds(const TimerModel &timer);
```

关键规则：

- Idle/Adjusting 下 Confirm：
  - `settingSeconds == 0`：进入 FwdRun。
  - `settingSeconds > 0`：复制到 `timerSeconds` 并进入 CdRun。
- 状态切换到运行态时重置 `lastSecondTickMs = nowMs`。
- 运行时使用 `while (now - lastSecondTickMs >= 1000)` 补偿 loop 抖动。
- 正计时到上限后保持上限，不溢出。
- 倒计时从 1 秒递减后进入 Finished。

追踪需求：R-18 到 R-32。

## SETTING 状态机

### 进入 SETTING

`enterSetting()`：

- `mode = AppMode::Setting`。
- `setting.state = SettingState::SettingMenu`。
- 保留 `selectedItem`，或者默认选中 `Brightness`。建议保留，方便反复设置。
- 清屏并使显示缓存失效。
- 不改变 `timer`。

追踪需求：R-09 到 R-12、R-33。

### 菜单

菜单项固定两项：

- `BRIGHTNESS`
- `TIME SET`

旋钮切换选中项。Confirm 进入子页面。Cancel 退出 SETTING，确保配置已保存，返回 TIMER。

返回 TIMER 是固定行为，不根据进入 SETTING 前的页面决定。

### BrightnessEdit

进入后显示当前档位。旋钮调整时：

1. 档位限制在 1..5。
2. 立即调用 `displaySetContrast(...)`。
3. 立即调用 `persistenceSaveBrightness(level)`。
4. 保存失败时 Serial 记录，不阻断 UI。
5. 标记显示刷新。

Cancel 只返回 SETTING 菜单，不回滚亮度。

追踪需求：R-38、R-41 到 R-48、R-79 到 R-81。

### TimeEdit

进入 TIME SET：

- 如果 RTC 有效，复制 `rtcTime.hour` 和 `rtcTime.minute` 到编辑字段。
- 如果 RTC 无效，建议初始化为 `00:00` 并显示当前 RTC 状态；Confirm 写入前仍需要有效日期。若写入所需日期不可用，写入失败并显示提示框 `RTC FAIL`。

字段切换：

- Hour 阶段 Confirm -> Minute 阶段。
- Minute 阶段 Confirm -> 构造 `RtcTime` 并写入。

写入成功：

1. 调用 `rtcSetTime()` 写 DS1302，秒为 0，日期和星期取当前有效 RTC。
2. 立即调用 `rtcServiceForceRead()`。
3. 清除错误提示。
4. 返回 `SettingMenu`。
5. 标记显示刷新。

写入失败：

- 保持当前 TimeEdit 状态。
- 设置 `timeSetErrorVisible = true`。
- `timeSetError` 使用短文本，例如 `RTC WRITE FAIL` 或 `RTC FAIL`。
- 渲染层绘制提示框效果，内容为失败内容。

字段闪烁：

- 每 `SETTING_BLINK_MS` 反转一次 `showBlinkField`。
- 只在 TimeEditHour / TimeEditMinute 中触发显示刷新。

追踪需求：R-49 到 R-57、R-77。

## RTC 设计

### DS1302 低层

`rtc.*` 负责：

- GPIO 初始化。
- DS1302 单寄存器读写。
- BCD 转换。
- `rtcReadTime(RtcTime&)` 校验字段和 CH 位。
- `rtcSetTime(const RtcTime&)` 写入并读取确认。
- `rtcReadRawRegisters()` 用于诊断日志。

### RTC 服务层

`rtc_service.*` 负责自动初始化、状态文本和读取调度。

读取策略：

- 启动后立即读取一次。
- RTC 正常时：
  - 根据当前秒数计算下一分钟边界附近读取时间：`(60 - second) * 1000 + RTC_MINUTE_REFRESH_GRACE_MS`。
  - 同时设置最长 30 秒兜底，因此实际下一次间隔为 `min(到分钟边界的时间, 30000ms)`。
  - 如果当前秒数为 0 且刚完成读取，下一次按 30 秒兜底或下一分钟边界计算，避免连续立即读取。
- RTC 异常、自动初始化等待、写入确认阶段：
  - 使用 `RTC_SHORT_REFRESH_MS = 1000`。
- TIME SET 写入成功后：
  - 强制立即读取，不等待调度时间。

自动初始化：

1. 读取失败后输出 raw registers。
2. 进入 `Waiting`，设置 `autoInitDueMs = now + RTC_AUTO_INIT_DELAY_MS`。
3. 到期后进入 `Writing`，使用 `__DATE__` / `__TIME__` 构造时间。
4. 写入后立即读取确认。
5. 成功回到 `Idle`；失败进入 `Failed`，且本次启动不再尝试。

状态文本：

| 状态 | 文本 |
| --- | --- |
| 读取失败或 Waiting | `RTC READ FAIL` |
| Writing | `RTC INIT...` |
| Failed | `RTC INIT FAIL` |

追踪需求：R-13 到 R-16、R-64 到 R-71。

## OLED 与渲染设计

### display 底层

保留直接 SSD1306 命令/数据绘制方式：

```cpp
void displayBegin();
void displayClear();
void displayInvalidateCache();
void displaySetContrast(uint8_t contrast);
void displayPrintLine(uint8_t page, const char *text);
void displayPrintLineCentered(uint8_t page, const char *text);
void displayPrintScaledLineCentered(uint8_t page, const char *text, uint8_t scale);
void displayDrawDialog(const char *message);
```

`displayDrawDialog()` 用于提示框效果：

- 先保存实现复杂度，不做真正帧缓冲。
- 建议清除 page 2..5，在 page 2 和 page 5 绘制简单边框字符效果，如 `+----------------+`。
- 中间 page 居中显示失败内容。
- 文本仍使用 ASCII，大于可显示长度时截断。

如果要做像素级矩形边框，需要扩展 OLED 绘图 API；本阶段优先简单可靠。

### ui_render 页面

`renderApp(const AppState&, const RtcServiceState&)` 根据 mode 分发：

- `renderClock`
- `renderTimer`
- `renderSetting`

渲染规则：

- 页面切换调用 `displayClear()` 和 `displayInvalidateCache()`。
- CLOCK/TIMER 的主时间只显示到分钟。
- SETTING header 复用统一 `renderHeader("SETTING", true)`。
- TIME SET 错误提示框优先覆盖当前 TimeEdit 页面内容。

追踪需求：R-13 到 R-17、R-31 到 R-40、R-72 到 R-78。

## 持久化设计

使用 ESP32 `Preferences`：

```cpp
bool persistenceBegin();
uint8_t persistenceLoadBrightness();
bool persistenceSaveBrightness(uint8_t level);
void persistenceEnd();
```

建议 namespace：`focusClock`

建议 key：

- `bright`

规则：

- 读取失败、key 不存在或值不在 1..5 时使用默认 3。
- BrightnessEdit 每次档位变化立即保存。
- SETTING 菜单 Cancel 退出时可再调用一次保存或确认，无副作用。
- 保存失败只记录 Serial，不阻断 UI。

追踪需求：R-41 到 R-48、R-79 到 R-81。

## WS2812 反馈隔离设计

`feedback.*` 是临时功能边界。当前固件默认禁用 WS2812 输入反馈，接口保留为后续重新接入 WS2812 或其他灯效反馈的扩展点：

```cpp
enum class FeedbackEvent : uint8_t {
  Mode,
  Confirm,
  Cancel,
  Knob,
};

void feedbackBegin();
void feedbackFlash(FeedbackEvent event);
void feedbackSetHeld(bool active, FeedbackEvent event);
void feedbackUpdate(uint32_t nowMs);
bool feedbackActive(uint32_t nowMs);
```

业务层只调用 `feedbackFlash(FeedbackEvent::Mode)` 这类语义接口，不直接依赖 `Adafruit_NeoPixel`。

当前禁用策略：

- `feedback.*` 初始化 WS2812 后保持熄灭。
- `feedbackFlash()`、`feedbackSetHeld()` 和 `feedbackUpdate()` 不点亮 LED。
- `feedbackActive()` 返回 false，Light Sleep 不等待输入反馈结束。

如果后续重新启用灯效反馈：

- 优先只修改 `feedback.*`，必要时调整 `main.cpp` 中输入事件到 `FeedbackEvent` 的语义映射。
- 反馈事件保持 Mode、Confirm、Cancel、Knob 四类需求语义，避免暴露旋钮原始边沿、开始、结束等硬件细节。
- 如灯效有持续时间，`feedbackActive()` 应准确反映未结束状态，供 `sleep_manager` 判断。

追踪需求：R-58 到 R-61。

## Light Sleep 设计

进入条件：

- `ENABLE_CLOCK_LIGHT_SLEEP == true`。
- `mode == AppMode::Clock`。
- `timerIsRunning(timer) == false`。
- `displayDirty == false`。
- 没有按钮输入处于低电平。
- `feedbackActive(now) == false`。
- 唤醒保持窗口已结束。

唤醒源：

- timer wakeup，最长 `IDLE_LIGHT_SLEEP_US`。
- Mode、Confirm、Cancel 低电平唤醒。
- 旋钮 V/W 根据当前电平配置相反边沿唤醒。

唤醒后的输入处理：

- 唤醒后设置 `wakeHoldUntilMs = now + WAKE_INPUT_HOLD_MS`。
- 如果检测到 Mode 仍被按住，不立即派发短按；交给 `input.*` 的 Mode 长按状态机继续计时。
- Confirm 和 Cancel 可设置 pending press，但需要重复保护。
- 保持 `wakeButtonRepeatGuardMs`，避免同一次按住重复消费。

这是相对旧实现的重要变化：Mode 唤醒路径不能绕过长按状态机直接调用短按处理。

追踪需求：R-62、R-63、R-82 到 R-86。

## 错误处理与日志

串口日志用于硬件诊断：

- RTC 读取失败输出 raw registers。
- RTC 自动初始化开始、成功、失败。
- RTC TIME SET 写入失败。
- Preferences 读取非法值或保存失败。
- 启动完成和 CPU 频率。

OLED 错误提示只覆盖用户需要知道的状态：

- CLOCK/TIMER RTC 状态行。
- TIME SET 写入失败提示框。

## 配置常量

关键常量分散到对应配置头，建议新增或保留：

```cpp
static constexpr uint32_t MODE_LONG_PRESS_MS = 3000;
static constexpr uint32_t SETTING_BLINK_MS = 500;
static constexpr uint8_t DEFAULT_BRIGHTNESS_LEVEL = 3;
static constexpr uint32_t RTC_SHORT_REFRESH_MS = 1000;
static constexpr uint32_t RTC_NORMAL_MAX_REFRESH_MS = 30000;
static constexpr uint32_t RTC_MINUTE_REFRESH_GRACE_MS = 50;
```

GPIO、计时器、WS2812、Light Sleep 常量不再全部堆在单一文件中，而是按 `config_hardware.h`、`config_timing.h`、`config_display.h` 拆分，并由 `config.h` 汇总。

## 实现约束

为保证模块和组件分离，Phase 4 实现时应遵守：

- 单个 `.cpp` 文件优先控制在一个明确职责内；如果 `main.cpp` 开始承载业务分支，应下沉到对应模块。
- 状态模型定义集中在 `app_state.h` 和 `timer_model.h`，不要在多个 `.cpp` 中重复定义枚举。
- 业务状态机函数通过参数接收状态引用，避免隐式全局状态扩散。
- 硬件驱动模块可以有内部静态对象，例如 `Adafruit_NeoPixel`、`Preferences`、OLED 缓存，但这些对象不暴露给业务层。
- 页面渲染函数不直接读取 GPIO、不写 Preferences、不调用 RTC 写入。
- 输入事件先归一化为 `ButtonId`、`ButtonEventType` 和旋钮 step，再由应用层分发。
- SETTING 的亮度保存通过 `persistence.*`，不要在输入处理或渲染代码里直接创建 `Preferences`。
- WS2812 只允许通过 `feedback.*` 使用，业务层不 include `Adafruit_NeoPixel.h`。
- RTC 低层只提供读写能力，自动初始化和读取调度必须放在 `rtc_service.*`。
- Light Sleep 进入条件集中在 `sleep_manager.*`，不要在主循环中散落条件判断。

## 风险与规避

| 风险 | 规避 |
| --- | --- |
| Mode 唤醒被误判短按 | Mode 唤醒后走长按状态机，不直接派发短按 |
| RTC 分钟边界调度漏刷新 | 使用最长 30 秒兜底，并在强制写入后立即读取 |
| SETTING 中后台计时暂停 | Timer 更新独立于 AppMode，每轮 loop 都执行 |
| 亮度实时保存导致频繁写 NVS | 只在档位实际变化时保存，档位仅 5 个，用户操作频率低 |
| WS2812 后续移除牵连业务 | 所有调用集中到 `feedback.*` |
| 提示框绘制影响缓存 | 提示框绘制前清理相关 page，并在关闭后使缓存失效 |
| 旧代码存在隐藏问题 | 旧代码只作参考，按本需求和设计重建关键状态机 |

## 验证计划

自动验证：

- `pio run -e esp32-c3-zero` 编译通过。

静态检查：

- 对照 R-01 到 R-86 检查状态转换。
- 检查 Mode 长按释放是否不会产生短按。
- 检查 SETTING 不改变 TimerModel 运行状态。
- 检查 RTC 正常调度不会固定 1 秒读取。

硬件人工验证：

- CLOCK 默认显示和 RTC 异常状态。
- TIMER 正计时、倒计时、暂停、恢复、完成、重置。
- Mode 长按进入 SETTING，释放不切页。
- SETTING 菜单、亮度实时保存、TIME SET 成功和失败提示框。
- WS2812 输入反馈。
- CLOCK 空闲 Light Sleep 与各唤醒源。

## 需求追踪摘要

| 需求范围 | 设计位置 |
| --- | --- |
| R-01 到 R-05 | 初始化流程、配置常量 |
| R-06 到 R-12 | 应用模式、输入设计、SETTING 状态机 |
| R-13 到 R-17 | OLED 与渲染设计、RTC 设计 |
| R-18 到 R-32 | TIMER 状态机 |
| R-33 到 R-40 | SETTING 状态机 |
| R-41 到 R-48 | BrightnessEdit、持久化设计 |
| R-49 到 R-57 | TimeEdit、提示框 |
| R-58 到 R-61 | WS2812 反馈隔离 |
| R-62 到 R-63 | 输入设计、Light Sleep 设计 |
| R-64 到 R-71 | RTC 设计 |
| R-72 到 R-78 | OLED 与渲染设计 |
| R-79 到 R-81 | 持久化设计 |
| R-82 到 R-86 | Light Sleep 设计 |
