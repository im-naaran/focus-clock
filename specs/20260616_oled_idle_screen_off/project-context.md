> 状态：已确认

# 项目上下文：OLED 空闲息屏

日期：2026-06-16
Feature：oled_idle_screen_off
阶段：Phase 0

## 仓库现状

本项目是运行在 ESP32-C3 上的 PlatformIO / Arduino 固件，当前生产代码位于标准目录：

- `src/`：固件源码。
- `lib/LeobogKnob/`：本地旋钮解码库。
- `platformio.ini`：PlatformIO 环境配置。
- `README.md`：硬件、交互和低功耗现状说明。
- `power-optimization-roadmap.md`：电池供电优化路线，包含本次 OLED 空闲息屏目标。

当前工作树中 `power-optimization-roadmap.md` 和 `specs/` 尚未纳入 git 跟踪。后续设计和实现应避免误改已有未跟踪规格内容。

## 技术栈

- MCU：ESP32-C3。
- 目标硬件：Waveshare ESP32-C3-Zero 风格开发板。
- 构建系统：PlatformIO。
- 框架：Arduino。
- PlatformIO 环境：`esp32-c3-zero`。
- Board profile：`esp32-c3-devkitm-1`。
- 外部依赖：`adafruit/Adafruit NeoPixel`。
- 本地依赖：`lib/LeobogKnob`。

可用自动化验证主要是：

- `/Users/naaran/.platformio/penv/bin/pio run`

当前没有单元测试框架。

## 相关硬件

- SSD1306 128x64 I2C OLED。
- DS1302 RTC，用于判断夜间息屏时间段。
- Mode、Confirm、Cancel 按钮。
- Leobog 风格旋钮 V/W 相位和 X 按压。
- 板载 WS2812 反馈灯，目前反馈接口存在但默认不点亮。

GPIO 分配集中在 `src/config_hardware.h`：

| 功能 | GPIO |
| --- | ---: |
| OLED SDA | 1 |
| OLED SCL | 2 |
| DS1302 SCLK / CLK | 3 |
| DS1302 IO / DAT | 4 |
| DS1302 CE / RST | 5 |
| Cancel 按钮 | 6 |
| 旋钮 W | 7 |
| 旋钮 X / Confirm | 8 |
| 板载 WS2812 | 10 |
| 旋钮 V | 20 |
| Mode 按钮 | 21 |

## 当前架构

主循环位于 `src/main.cpp`，当前顺序为：

1. `inputUpdate(nowMs)` 更新输入状态。
2. `updateHeldButtonFeedback()` 更新按住反馈。
3. `sleepManagerPopPendingButton(...)` 消费 Light Sleep 唤醒桥接按钮。
4. `inputPopEvent(...)` 消费普通输入事件。
5. `timerUpdateElapsed(...)` 更新计时器。
6. `rtcServiceUpdate(...)` 更新 RTC 调度。
7. `appUpdateSettingBlink(...)` 更新 SETTING 闪烁。
8. `renderApp(...)` 在 `displayDirty` 时重绘。
9. `feedbackUpdate(...)`、`sleepManagerUpdateButtonRelease(...)`。
10. `sleepManagerMaybeEnter(...)` 尝试进入 Light Sleep。

输入分发集中在 `dispatchInputEvent(...)`：

- 输入反馈在业务处理前触发。
- `KnobRaw`、`KnobRotationStart`、`KnobRotationEnd` 不进入业务状态机。
- `KnobStep` 和按钮事件进入 `appHandleInput(...)`。

OLED 显示层位于 `src/display.*`：

- 已支持 `displayBegin()`、`displayClear()`、`displayInvalidateCache()`、`displaySetContrast(...)` 和文本绘制。
- 行缓存用于避免重复写入 OLED。
- 当前没有显示开关 API。
- SSD1306 初始化序列中已使用 `0xAE` 关闭和 `0xAF` 打开显示。

RTC 服务位于 `src/rtc_service.*`：

- 正常情况下按下一分钟边界和最长 30 秒兜底调度读取。
- RTC 异常时使用短周期。
- `AppState` 中保存 `rtcOk` 和 `rtcTime`。

Light Sleep 位于 `src/sleep_manager.*`：

- 仅允许 CLOCK 页面进入。
- 条件包括：Timer 不运行、`displayDirty == false`、无按钮按住、无消抖等待、反馈不活跃、唤醒保持窗口结束。
- 唤醒源包括定时器、Mode、Confirm、Cancel、旋钮 V/W。
- 唤醒后的按钮通过 `sleepManagerPopPendingButton(...)` 桥接为一次 `Pressed` 事件。

持久化位于 `src/persistence.*`：

- 使用 ESP32 `Preferences`，命名空间为 `focusClock`。
- 当前只持久化亮度，key 为 `bright`。
- Preferences 首次使用后保持打开，直到 `persistenceEnd()`。

SETTING 位于 `src/app_state.h`、`src/app_controller.cpp`、`src/ui_render.cpp`：

- 顶层模式包含 `Clock`、`Timer`、`Setting`。
- SETTING 子状态包含 `SettingMenu`、`BrightnessEdit`、`TimeEditHour`、`TimeEditMinute`。
- 菜单项当前包含 `Brightness` 和 `TimeSet`。
- UI 文本为英文 ASCII，适配当前 5x7 字库。

## 关键约束

- Phase 0 到 Phase 3 期间只允许修改本规格目录下文档，不修改 `src/`、`lib/` 或构建配置。
- OLED 息屏必须只关闭显示输出，不改变当前 `AppMode`、Timer 状态、RTC 调度和输入状态机。
- `TIMER`、`SETTING` 以及 SETTING 子页面不允许自动息屏。
- RTC 无效时不能执行夜间自动息屏。
- 第一次唤醒 OLED 的输入不能同时触发业务动作。
- Light Sleep 唤醒桥接按钮和普通输入事件必须经过同一套息屏输入拦截。
- Mode 长按进入 SETTING 的可靠性不能被息屏唤醒逻辑破坏。
- 亮度 contrast 与显示开关需要保持独立语义。
- OLED 关闭后重新打开必须使显示缓存失效并触发重绘。
- 现有 UI 字库只覆盖 ASCII 范围，新增 SETTING 文本应使用英文。

## 已知风险

- SSD1306 `0xAE/0xAF` 在具体 OLED 模块上的电流收益需要硬件实测。
- OLED 关闭后缓存仍认为内容已绘制，若不失效可能导致亮屏后画面空白或不完整。
- 如果屏幕关闭期间 RTC 分钟变化触发了 `displayDirty`，打开后需要能重绘最新 CLOCK。
- 如果夜间结束时设备正在 Light Sleep，必须依赖 RTC 定时唤醒后自动点亮并重绘。
- 旋钮 V/W 唤醒可能只产生 `KnobRaw` 或旋转起始事件，设计必须把这些事件也视作用户活动。
- Preferences 新增字段需要兼容旧设备没有配置的情况。
- 若后续在 SETTING 中连续旋转修改夜间时间，实时保存可能造成过多 NVS 写入，应优先采用退出子页面或确认后保存。

