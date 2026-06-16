> 状态：已确认

# 任务拆解：OLED 空闲息屏

日期：2026-06-16
Feature：oled_idle_screen_off
阶段：Phase 3

## 任务清单

- [x] task-01: [display/config] 增加 OLED 开关 API 和息屏配置常量

  追踪需求：R-01, R-02, R-03, R-07, R-08, R-09, R-10, R-11, R-12

  依赖任务：无

  修改范围：`src/config_display.h`、`src/config_timing.h`、`src/display.h`、`src/display.cpp`

  完成标准：

  - 新增默认夜间息屏常量：启用、`20:00` 关闭、`08:00` 开启。
  - 新增 `SCREEN_WAKE_GRACE_MS = 60 * 1000`。
  - 新增 minute-of-day 范围校验辅助能力。
  - 新增 `displaySleep()` 和 `displayWake()`。
  - `displaySleep()` 发送 SSD1306 `0xAE`。
  - `displayWake()` 发送 SSD1306 `0xAF` 并使显示缓存失效。
  - `displaySetContrast(...)` 语义不变。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - 在硬件上临时调用 `displaySleep()` / `displayWake()` 时 OLED 能关闭和恢复。
  - 恢复后亮度仍保持当前 contrast 设置。

- [x] task-02: [app_state/display_power] 增加屏幕电源状态模型和默认状态机

  追踪需求：R-04, R-05, R-06, R-13, R-16, R-17, R-18, R-19, R-24, R-25, R-26, R-27, R-28, R-38, R-39, R-40, R-41, NFR-05, NFR-08

  依赖任务：task-01

  修改范围：`src/app_state.h`、新增 `src/display_power.h`、新增 `src/display_power.cpp`、`src/main.cpp`

  完成标准：

  - `UiConfig` 包含夜间息屏启用、关闭分钟、开启分钟字段，并使用默认值初始化。
  - `AppState` 包含 `DisplayPowerState`。
  - 新增夜间区间判断函数，覆盖跨午夜、同日区间、起止相同三种情况。
  - 新增 `displayPowerBegin(...)` 初始化屏幕电源状态。
  - 新增 `displayPowerUpdate(...)`，在默认配置下可在 CLOCK 夜间空闲时关闭 OLED。
  - RTC 无效时不会自动关闭 OLED；若屏幕已关闭且 RTC 无效，应自动打开并重绘。
  - 夜间结束时屏幕自动打开并设置 `displayDirty = true`。
  - 使用 `millis()` 差值方式判断时间窗口，兼容回绕。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - 默认 `20:00 -> 08:00` 窗口内 CLOCK 空闲后 OLED 自动关闭。
  - `08:00` 后 OLED 自动打开并重绘。
  - RTC 无效时保持亮屏并显示错误状态。

- [x] task-03: [main/input] 接入息屏输入拦截并处理同一按住周期

  追踪需求：R-30, R-31, R-32, R-33, R-34, R-35, R-36, R-37

  依赖任务：task-02

  修改范围：`src/display_power.h`、`src/display_power.cpp`、`src/main.cpp`

  完成标准：

  - `dispatchInputEvent(...)` 在输入反馈和业务处理前调用屏幕电源输入处理。
  - OLED 关闭时，第一次按钮或旋钮事件只点亮屏幕、设置 `manualWakeUntilMs`、标记 `displayDirty`，并返回不进入业务处理。
  - `sleepManagerPopPendingButton(...)` 产生的 pending button 和 `inputPopEvent(...)` 普通事件经过同一拦截路径。
  - `KnobRaw`、`KnobRotationStart`、`KnobRotationEnd`、`KnobStep` 都刷新 `lastUserInputMs`。
  - 息屏时被拦截的按钮按住周期内，后续同一按钮的 `LongPressed` 或释放相关业务事件不会进入业务状态机。
  - OLED 点亮时输入按现有逻辑分发。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - OLED 关闭时 Mode、Confirm、Cancel、旋钮按压、旋钮旋转均只亮屏，不触发切页、计时或设置动作。
  - OLED 关闭时长按 Mode 只亮屏，不在同一次按住中进入 SETTING。
  - 亮屏后再次操作按现有业务规则生效。

- [x] task-04: [sleep/power] 完整化自动息屏前置条件

  追踪需求：R-14, R-15, R-20, R-21, R-22, R-23, R-24, R-29, NFR-06, NFR-07, NFR-08

  依赖任务：task-03

  修改范围：`src/display_power.cpp`、`src/main.cpp`

  完成标准：

  - TIMER 页面不自动息屏。
  - SETTING 页面及所有子页面不自动息屏。
  - 按钮按住、输入消抖等待、反馈活跃、Light Sleep 唤醒保持窗口、手动唤醒保护窗口期间不自动息屏。
  - 从息屏状态进入 TIMER 或 SETTING 时自动亮屏并重绘。
  - OLED 关闭状态不阻止 MCU 进入现有 CLOCK Light Sleep。
  - Timer 状态和 RTC 读取调度不因 OLED 开关改变。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - TIMER 页面长时间空闲保持亮屏。
  - SETTING 页面长时间空闲保持亮屏。
  - 手动亮屏后仍在 CLOCK 夜间窗口，60 秒无操作后再次关闭。
  - 计时器运行准确性不受息屏影响。

- [x] task-05: [persistence] 持久化夜间息屏配置

  追踪需求：R-42, R-43, R-44, R-45

  依赖任务：task-02

  修改范围：`src/persistence.h`、`src/persistence.cpp`、`src/main.cpp`

  完成标准：

  - 新增夜间息屏配置读写 API。
  - Preferences 中保存启用开关、关闭分钟、开启分钟。
  - 旧设备没有配置时使用默认值。
  - 非法分钟值回退默认值。
  - 保存失败仅在 `ENABLE_SERIAL_LOGGING` 为真时输出日志，不阻断 CLOCK/TIMER。
  - 启动时加载夜间息屏配置到 `app.config`。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - 首次刷入或擦除 NVS 后使用默认 `20:00 -> 08:00`。
  - 修改配置并重启后设置保留。

- [x] task-06: [setting/model] 扩展 SETTING 状态和交互

  追踪需求：R-46, R-47, R-48, R-49, R-50

  依赖任务：task-05

  修改范围：`src/app_state.h`、`src/app_controller.cpp`

  完成标准：

  - SETTING 菜单新增 `NightScreenOff` 菜单项。
  - 菜单旋钮在 `BRIGHTNESS`、`TIME SET`、`NIGHT OFF` 之间循环。
  - Confirm 进入夜间息屏设置流程。
  - 支持编辑启用开关、关闭小时、关闭分钟、开启小时、开启分钟。
  - 小时范围 `0..23`，分钟范围 `0..59`，旋钮循环调整。
  - 最后一个字段 Confirm 后写回 `app.config` 并保存。
  - Cancel 返回菜单且不保存未确认修改。
  - 调整过程中不频繁写 NVS。
  - 新增时间编辑状态参与现有闪烁逻辑。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - SETTING 菜单可进入和退出 `NIGHT OFF`。
  - ON/OFF、OFF AT、ON AT 均可用旋钮调整。
  - Confirm 保存，Cancel 放弃未确认修改。

- [x] task-07: [setting/render] 渲染夜间息屏设置页面

  追踪需求：R-46, R-47, R-48, R-49, R-51

  依赖任务：task-06

  修改范围：`src/ui_render.cpp`

  完成标准：

  - SETTING 菜单显示 `NIGHT OFF`，并正确显示选中箭头。
  - 启用开关页面显示英文 ASCII 文本。
  - 关闭时间页面显示 `OFF AT` 和 `HH:MM`。
  - 开启时间页面显示 `ON AT` 和 `HH:MM`。
  - 当前编辑字段按现有闪烁节奏隐藏或显示。
  - 文本不超过当前 `LINE_CACHE_LEN` 和 OLED 宽度约束。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - 菜单和各子页面无旧文本残留。
  - 闪烁字段清晰可辨。
  - 所有新增文本在 OLED 上完整显示。

- [x] task-08: [debug/verification] 补充日志并执行构建验证

  追踪需求：R-52, R-53, AC-01, AC-02, AC-03, AC-04, AC-05, AC-06, AC-07, AC-08, AC-09, AC-10, AC-11, AC-12

  依赖任务：task-01, task-02, task-03, task-04, task-05, task-06, task-07

  修改范围：`src/display_power.cpp`、`src/persistence.cpp`、`specs/20260616_oled_idle_screen_off/changelog.md`

  完成标准：

  - `ENABLE_SERIAL_LOGGING` 为真时，自动息屏、自动亮屏、输入唤醒和配置保存失败有日志。
  - `ENABLE_SERIAL_LOGGING` 为 false 时不输出新增日志。
  - 完整执行 `pio run`。
  - 记录实现完成和验证结果到 changelog。

  自动化验证：`/Users/naaran/.platformio/penv/bin/pio run`

  人工验证关注点：

  - 对照验收标准 AC-01 到 AC-11 在硬件上逐项确认。
  - 测量 OLED 点亮、OLED 关闭、CLOCK Light Sleep + OLED 关闭的电流差异。

## 依赖关系

```text
task-01
  -> task-02
      -> task-03
          -> task-04
      -> task-05
          -> task-06
              -> task-07

task-08 depends on task-01..task-07
```

## 执行顺序建议

1. 先完成 `task-01` 到 `task-04`，用默认配置验证核心息屏与输入拦截。
2. 再完成 `task-05` 到 `task-07`，接入持久化和 SETTING。
3. 最后执行 `task-08`，整理日志、构建和硬件验收记录。
