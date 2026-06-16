> 状态：已确认

# 技术设计：OLED 空闲息屏

日期：2026-06-16
Feature：oled_idle_screen_off
阶段：Phase 2

## 设计目标

在当前 ESP32-C3 Focus Clock 固件中增加夜间 OLED 空闲息屏能力，降低电池供电时 OLED 常亮带来的耗电。

设计原则：

- 保持现有 Arduino 主循环和模块边界。
- OLED 息屏只控制 SSD1306 显示输出，不改变应用模式、计时器、RTC 调度和输入状态机。
- 所有用户输入先经过屏幕电源管理，再决定是否进入业务状态机。
- 自动息屏只允许在 CLOCK 页面执行；TIMER 和 SETTING 始终保持亮屏。
- RTC 无效时不根据夜间时间做自动息屏判断。
- 亮度 contrast 与显示开关相互独立。

覆盖需求：R-01 到 R-53，NFR-01 到 NFR-08，AC-01 到 AC-12。

## 总体方案

新增一个轻量的屏幕电源状态模块，建议命名为 `display_power.*`。它不直接处理业务输入含义，只回答三个问题：

1. 当前 RTC 分钟是否落在夜间息屏窗口。
2. 当前是否允许自动关闭 OLED。
3. 当前输入是否应被拦截为“只唤醒屏幕”。

主循环保持现有结构，但在两个位置接入：

```text
loop()
  inputUpdate(nowMs)
  updateHeldButtonFeedback()

  pending button -> dispatchInputEvent(...)
  input queue     -> dispatchInputEvent(...)

  timer / rtc / setting blink
  renderApp(...) when displayDirty

  feedback / sleep button release
  displayPowerUpdate(...)
  sleepManagerMaybeEnter(...)
```

`dispatchInputEvent(...)` 增加屏幕电源拦截：

```text
dispatchInputEvent(event, nowMs)
  displayPowerNoteInput(...)
  if OLED is off:
    displayPowerWakeForInput(...)
    return
  flashForInputEvent(...)
  ignore raw/session events for business
  appHandleInput(...)
```

这样普通输入和 `sleepManagerPopPendingButton(...)` 产生的 pending button 都会经过同一入口，避免 OLED 关闭时第一次按键同时触发业务动作。

## 模块变更

| 模块 | 变更 |
| --- | --- |
| `config_timing.h` | 新增 `SCREEN_WAKE_GRACE_MS = 60 * 1000`。 |
| `config_display.h` | 新增夜间息屏默认分钟常量和时间范围校验辅助函数。 |
| `display.*` | 新增 `displaySleep()` / `displayWake()` 或 `displaySetPower(bool on)`。 |
| `app_state.h` | 扩展 `UiConfig`、`SettingModel`，并增加 `DisplayPowerState`。 |
| `display_power.*` | 新增屏幕电源状态机、输入拦截和夜间区间判断。 |
| `main.cpp` | 初始化配置，统一输入拦截，渲染后更新屏幕电源。 |
| `persistence.*` | 新增夜间息屏配置读写。 |
| `app_controller.cpp` | 扩展 SETTING 菜单和夜间息屏设置交互。 |
| `ui_render.cpp` | 扩展 SETTING 菜单和配置编辑页面渲染。 |
| `sleep_manager.cpp` | 不直接管理 OLED；必要时只复用现有 Light Sleep 条件辅助。 |

## 数据模型

### UiConfig

```cpp
struct UiConfig {
  uint8_t brightnessLevel = AppConfig::DEFAULT_BRIGHTNESS_LEVEL;
  bool nightScreenOffEnabled = true;
  uint16_t nightScreenOffMinute = 20 * 60;
  uint16_t nightScreenOnMinute = 8 * 60;
};
```

`nightScreenOffMinute` 和 `nightScreenOnMinute` 表示当天 0 点起的分钟数，范围 `0..1439`。

### DisplayPowerState

```cpp
struct DisplayPowerState {
  bool screenOn = true;
  uint32_t lastUserInputMs = 0;
  uint32_t manualWakeUntilMs = 0;
  uint32_t lastScreenPowerChangeMs = 0;
};
```

建议挂在 `AppState` 中：

```cpp
struct AppState {
  ...
  DisplayPowerState displayPower;
  bool displayDirty = true;
};
```

选择放入 `AppState` 的原因：

- 渲染、输入和 SETTING 都需要读写这组状态。
- 状态随应用生命周期存在，不属于底层 display 驱动。
- 后续如需串口日志或调试页面，可直接从应用状态观察。

### SETTING 状态

当前 `SettingMenuItem` 只有 `Brightness`、`TimeSet`。新增：

```cpp
enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
  NightScreenOff,
};
```

当前 `SettingState` 只有亮度和 RTC 时间编辑。新增夜间息屏设置状态：

```cpp
enum class SettingState : uint8_t {
  SettingMenu,
  BrightnessEdit,
  TimeEditHour,
  TimeEditMinute,
  NightOffEnabledEdit,
  NightOffStartHourEdit,
  NightOffStartMinuteEdit,
  NightOffEndHourEdit,
  NightOffEndMinuteEdit,
};
```

其中 `Start` 对应关闭屏幕时间，`End` 对应恢复亮屏时间。UI 文案建议使用：

- 菜单：`NIGHT OFF`
- 开关页：`NIGHT OFF` / `ON` / `OFF`
- 关闭时间：`OFF AT`
- 开启时间：`ON AT`

`SettingModel` 新增临时编辑字段：

```cpp
bool editNightOffEnabled = true;
uint8_t editNightOffHour = 20;
uint8_t editNightOffMinute = 0;
uint8_t editNightOnHour = 8;
uint8_t editNightOnMinute = 0;
```

进入夜间息屏设置时从 `app.config` 复制到临时字段。完成最后一个字段后写回 `app.config` 并保存。Cancel 返回菜单且不保存未确认修改。

## 夜间区间判断

新增纯函数，便于审查和后续单元测试：

```cpp
bool displayPowerMinuteInNightWindow(uint16_t minute,
                                     uint16_t offMinute,
                                     uint16_t onMinute) {
  if (offMinute == onMinute) {
    return false;
  }
  if (offMinute < onMinute) {
    return minute >= offMinute && minute < onMinute;
  }
  return minute >= offMinute || minute < onMinute;
}
```

当前 RTC 分钟：

```cpp
uint16_t rtcMinuteOfDay(const RtcTime &time) {
  return time.hour * 60 + time.minute;
}
```

该判断只在 `app.rtcOk && app.rtcTime.valid` 为真时使用。

## Display API

在 `display.h` 中新增：

```cpp
void displaySleep();
void displayWake();
```

实现：

```cpp
void displaySleep() {
  oledCommand(0xAE);
}

void displayWake() {
  oledCommand(0xAF);
  displayInvalidateCache();
}
```

不在 `display.*` 内保存 `screenOn`，因为当前屏幕是否应该点亮是应用策略，不是 SSD1306 驱动能力。驱动只提供命令能力。

如果后续需要幂等保护，可由 `display_power.*` 保证只在状态变化时调用。

## 屏幕电源状态机

新增 `display_power.h`：

```cpp
void displayPowerBegin(AppState &app, uint32_t nowMs);

bool displayPowerHandleInput(AppState &app,
                             const InputEvent &event,
                             uint32_t nowMs);

void displayPowerUpdate(AppState &app,
                        const SleepManagerState &sleepState,
                        uint32_t nowMs);
```

### 初始化

`setup()` 加载亮度和夜间息屏配置后调用：

```cpp
displayPowerBegin(app, nowMs);
```

初始状态：

- `screenOn = true`
- `lastUserInputMs = nowMs`
- `manualWakeUntilMs = 0`
- `lastScreenPowerChangeMs = nowMs`

启动后先显示 BOOTING，再读取配置和 RTC。即使当前时间在夜间区间，也等主循环完成首次渲染并 `displayDirty == false` 后再自动息屏。

### 输入处理

`displayPowerHandleInput(...)` 返回 `true` 表示输入已被屏幕电源层消费，业务层不得继续处理。

逻辑：

```text
记录 lastUserInputMs = nowMs

如果 screenOn == false:
  displayWake()
  screenOn = true
  manualWakeUntilMs = nowMs + SCREEN_WAKE_GRACE_MS
  lastScreenPowerChangeMs = nowMs
  app.displayDirty = true
  return true

return false
```

这会把 `KnobRaw`、`KnobRotationStart`、`KnobRotationEnd`、`KnobStep` 和按钮事件全部视作用户活动。

### 自动关闭

`displayPowerUpdate(...)` 在渲染后调用。关闭条件：

- `app.displayPower.screenOn == true`
- `app.config.nightScreenOffEnabled == true`
- `app.mode == AppMode::Clock`
- `app.rtcOk && app.rtcTime.valid`
- 当前分钟在夜间区间
- `app.displayDirty == false`
- `!inputAnyButtonHeldLow()`
- `!inputHasPendingDebounce()`
- `!feedbackActive(nowMs)`
- `!sleepManagerWakeHoldActive(sleepState, nowMs)`
- 当前时间已超过 `manualWakeUntilMs`

满足后：

```text
displaySleep()
screenOn = false
lastScreenPowerChangeMs = nowMs
```

关闭 OLED 后不设置 `displayDirty`，避免在同一轮循环立即重绘到已关闭屏幕。屏幕再次点亮或夜间结束时再设置 `displayDirty = true`。

### 自动打开

打开条件：

- `screenOn == false`
- 以下任一成立：
  - 当前不在允许自动息屏状态，例如 mode 不是 CLOCK。
  - 夜间息屏配置关闭。
  - RTC 有效且当前分钟已不在夜间区间。
  - RTC 从无效恢复为有效且当前不在夜间区间。

打开后：

```text
displayWake()
screenOn = true
lastScreenPowerChangeMs = nowMs
app.displayDirty = true
```

若 RTC 无效且屏幕已经关闭，默认打开屏幕。原因是无有效时间时继续黑屏会让用户无法看到 RTC 错误状态，也不符合“RTC 无效时不执行夜间自动息屏”的需求。

## 主循环接入

`dispatchInputEvent(...)` 修改为：

```cpp
static void dispatchInputEvent(const InputEvent &event, uint32_t nowMs) {
  if (displayPowerHandleInput(app, event, nowMs)) {
    return;
  }

  flashForInputEvent(event);
  if (event.kind == InputEventKind::KnobRaw ||
      event.kind == InputEventKind::KnobRotationStart ||
      event.kind == InputEventKind::KnobRotationEnd) {
    return;
  }
  appHandleInput(app, rtcService, event, nowMs);
}
```

`loop()` 渲染后接入：

```cpp
if (app.displayDirty) {
  renderApp(app, rtcServiceStatusText(rtcService, app));
  app.displayDirty = false;
}

feedbackUpdate(nowMs);
sleepManagerUpdateButtonRelease(sleepState);
displayPowerUpdate(app, sleepState, nowMs);
sleepManagerMaybeEnter(sleepState, app, rtcService, nowMs);
```

选择放在渲染后，是为了满足 `displayDirty == false` 才允许息屏，避免刚产生渲染请求就关屏。

## 与 Light Sleep 的关系

OLED 息屏和 Light Sleep 保持解耦：

- `sleep_manager.*` 继续只判断是否允许 MCU 进入 Light Sleep。
- OLED 关闭不阻止 Light Sleep。
- Light Sleep 定时唤醒继续由 `rtcServiceNextReadDueMs(...)` 决定。
- 夜间结束依赖 RTC 服务定时读取得到新分钟后，由 `displayPowerUpdate(...)` 打开 OLED。

不建议把 OLED 关闭状态加入 `canEnterClockLightSleep(...)` 的前置条件。OLED 关闭后仍应让设备继续 Light Sleep，以获得最大省电收益。

## 持久化设计

新增配置结构：

```cpp
struct NightScreenOffConfig {
  bool enabled = true;
  uint16_t offMinute = 20 * 60;
  uint16_t onMinute = 8 * 60;
};
```

`persistence.h` 新增：

```cpp
NightScreenOffConfig persistenceLoadNightScreenOff();
bool persistenceSaveNightScreenOff(const NightScreenOffConfig &config);
```

Preferences key 建议：

- `nightOffEn`：bool。
- `nightOffMin`：uint16。
- `nightOnMin`：uint16。

读取规则：

- key 不存在时使用默认值。
- minute 超出 `0..1439` 时回退默认值并输出日志。
- enabled 缺失时默认 true。

保存规则：

- 只在用户确认完成夜间息屏设置时保存。
- 保存失败仅记录日志，不阻断主功能。
- 可维护 `lastSavedNightScreenOff`，避免重复写入相同值。

## SETTING 交互设计

菜单项从 2 个扩展到 3 个，旋钮在三个菜单项间循环：

```text
BRIGHTNESS -> TIME SET -> NIGHT OFF -> BRIGHTNESS
```

进入 `NIGHT OFF` 后建议流程：

```text
NightOffEnabledEdit
  旋钮：ON/OFF
  Confirm：进入 NightOffStartHourEdit
  Cancel：返回 SettingMenu，不保存

NightOffStartHourEdit
  旋钮：0..23
  Confirm：进入 NightOffStartMinuteEdit
  Cancel：返回 SettingMenu，不保存

NightOffStartMinuteEdit
  旋钮：0..59
  Confirm：进入 NightOffEndHourEdit
  Cancel：返回 SettingMenu，不保存

NightOffEndHourEdit
  旋钮：0..23
  Confirm：进入 NightOffEndMinuteEdit
  Cancel：返回 SettingMenu，不保存

NightOffEndMinuteEdit
  旋钮：0..59
  Confirm：写回 app.config 并保存，然后返回 SettingMenu
  Cancel：返回 SettingMenu，不保存
```

即使用户将关闭时间和开启时间设为相同，也允许保存；运行时解释为无息屏窗口。

渲染保持 ASCII 文本：

```text
SETTING          HH:MM

NIGHT OFF
ON

OFF AT
20:00

ON AT
08:00
```

可复用现有 `showBlinkField` 和 `lastBlinkToggleMs` 实现当前字段闪烁。`appUpdateSettingBlink(...)` 需要覆盖新增的时间编辑状态。

## 需求追踪

| 需求 | 设计覆盖 |
| --- | --- |
| R-01..R-07 | `UiConfig` 默认值、配置常量、持久化设计。 |
| R-08..R-12 | `displaySleep()`、`displayWake()` 和 display/cache 语义。 |
| R-13..R-25 | `displayPowerUpdate(...)` 自动关闭条件。 |
| R-26..R-29 | `displayPowerUpdate(...)` 自动打开条件。 |
| R-30..R-37 | `displayPowerHandleInput(...)` 和 `dispatchInputEvent(...)` 接入。 |
| R-38..R-41 | `DisplayPowerState`。 |
| R-42..R-45 | `persistenceLoadNightScreenOff(...)` / `persistenceSaveNightScreenOff(...)`。 |
| R-46..R-51 | SETTING 状态、菜单和渲染设计。 |
| R-52..R-53 | 串口日志由 `ENABLE_SERIAL_LOGGING` 守卫。 |
| NFR-01..NFR-08 | 模块边界、无新增依赖、主循环接入和 Light Sleep 解耦。 |
| AC-01..AC-12 | 自动关闭/打开、输入拦截、持久化和构建验证设计。 |

## 风险与规避

### OLED 关闭后缓存不一致

风险：SSD1306 关闭显示期间仍可能接受绘制命令，但重新点亮后缓存层可能跳过必要重绘。

规避：`displayWake()` 必须调用 `displayInvalidateCache()`，屏幕电源层必须设置 `app.displayDirty = true`。

### 第一次输入误触发业务动作

风险：OLED 关闭时，唤醒输入同时切页或启动计时。

规避：把屏幕电源拦截放在 `dispatchInputEvent(...)` 最前，覆盖 pending button 和普通输入队列。

### Mode 长按语义被破坏

风险：息屏状态下长按 Mode，第一次 `Pressed` 被拦截后，后续 `LongPressed` 可能仍进入业务层。

规避：息屏唤醒后的 `manualWakeUntilMs` 只阻止自动息屏，不阻止业务输入。是否允许同一次长按后续 `LongPressed` 进入业务层需要实现时重点处理。推荐策略是在拦截唤醒输入时记录被拦截按钮，直到该按钮释放前丢弃同一按住周期的后续事件，避免一次长按既亮屏又进 SETTING。

### 夜间结束无法准时亮屏

风险：设备处于 Light Sleep，OLED 关闭，夜间结束时不能立即点亮。

规避：继续保留 RTC 服务的分钟边界和最长 30 秒兜底定时唤醒；`displayPowerUpdate(...)` 在 RTC 更新后执行。

### NVS 写入过多

风险：旋钮连续调整夜间时间时频繁保存。

规避：夜间息屏设置采用临时字段，最后 Confirm 才保存；Cancel 不保存。

### 同名配置兼容

风险：Preferences key 名称过长或旧值非法。

规避：使用短 key，读取时校验 minute 范围，非法值回退默认并记录日志。

## 实施拆分建议

1. 增加 `displaySleep()` / `displayWake()`，用最小测试验证 `0xAE/0xAF` 可独立控制 OLED。
2. 新增 `DisplayPowerState` 和 `display_power.*`，先使用默认配置，不接入 SETTING。
3. 在 `dispatchInputEvent(...)` 接入息屏输入拦截，覆盖 pending button 和普通输入。
4. 增加夜间息屏配置持久化，确保旧设备默认值兼容。
5. 扩展 SETTING 菜单和夜间息屏设置页面。
6. 补充串口调试日志，便于硬件测量自动关闭、输入唤醒和自动打开路径。

## 验证计划

自动化验证：

- `/Users/naaran/.platformio/penv/bin/pio run`

人工验证：

- 设置 RTC 到 `20:00` 后停留 CLOCK，确认 OLED 自动关闭。
- OLED 关闭时分别按 Mode、Confirm、Cancel、旋钮按压和旋转，确认只亮屏不触发业务动作。
- 手动亮屏后继续停留 CLOCK，等待 60 秒确认再次关闭。
- 手动亮屏后切到 TIMER，确认屏幕保持点亮。
- SETTING 及夜间息屏设置页面保持点亮。
- 设置 RTC 到 `08:00` 附近，确认夜间结束后自动亮屏。
- 断电重启后确认夜间息屏设置保留。
- RTC 无效时确认不自动黑屏，并能显示 RTC 错误状态。
