# 项目上下文：Focus Clock 重构

日期：2026-06-08
Feature：focus_clock_refactor

## 仓库现状

本项目是一个运行在 ESP32-C3 上的 PlatformIO / Arduino 固件项目，用于桌面时钟和专注计时器。

当前工作树中，生产固件代码已不再位于 `src/`，旧实现已备份到 `backup/`。后续重构需要在标准 PlatformIO 目录结构中创建或恢复生产代码，但 Phase 0 到 Phase 3 期间只允许修改本规格目录下的文档。

重要输入来源：

- `REQUIREMENTS.md`：当前目标行为和重构验收点。
- `README.md`：项目概览、接线、当前行为和 PlatformIO 使用方式。
- `platformio.ini`：当前 PlatformIO 环境配置。
- `backup/src/`：旧固件实现，仅可作为参考。
- `backup/lib/LeobogKnob/`：旧本地旋钮库。

旧实现不是正确性基准。旧代码可能存在错误或未完成行为。尤其是 `REQUIREMENTS.md` 中描述的 SETTING 页面在旧固件中尚未实现，应按新增功能处理。

## 技术栈

- MCU：ESP32-C3，目标板为 Waveshare ESP32-C3-Zero 风格开发板。
- 构建系统：PlatformIO。
- 框架：Arduino。
- PlatformIO 环境：`esp32-c3-zero`。
- Board profile：`esp32-c3-devkitm-1`。
- 外部依赖：`adafruit/Adafruit NeoPixel`。
- 重构后预期本地依赖：`lib/LeobogKnob`。
- USB CDC 编译标志：
  - `ARDUINO_USB_CDC_ON_BOOT=1`
  - `ARDUINO_USB_MODE=1`

## 硬件模块

- SSD1306 128x64 I2C OLED 显示屏。
- DS1302 RTC，使用三线 GPIO 接口。
- Leobog 风格键盘旋钮：
  - `V/W` 用于旋转。
  - `X` 用于 Confirm 按压。
- 独立 Mode 按钮。
- 独立 Cancel 按钮。
- 板载 WS2812 RGB LED，用于输入反馈。

## 当前 GPIO 分配

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

硬件注意事项：

- GPIO2 和 GPIO8 与 ESP32-C3 启动采样相关。
- GPIO10 被板载 WS2812 使用。
- GPIO18 和 GPIO19 应保留给 USB。
- GPIO20 和 GPIO21 在部分开发板标注中与 UART 功能重叠。

## 旧架构参考

备份中的旧实现大致组织为：

- `src/main.cpp`：应用状态机、显示调度、输入分发、计时逻辑、RTC 自动初始化、Light Sleep。
- `src/config.h`：GPIO 和行为常量。
- `src/display.*`：SSD1306 命令/数据绘制、5x7 ASCII 字库、行缓存、居中和放大文本辅助函数。
- `src/rtc.*`：DS1302 寄存器访问、BCD 转换、时间校验、写入确认。
- `lib/LeobogKnob`：基于中断的 V/W 旋转解码和按钮消抖。

该架构可以作为起点，但重构时应适当拆分职责，使 SETTING 状态和持久化逻辑更容易审查和验证。

## 行为范围

目标固件包含以下主要行为：

- 启动和硬件初始化。
- CLOCK 页面显示和 RTC 状态展示。
- TIMER 页面，包含正计时和倒计时状态机。
- SETTING 页面，包含亮度调整和 RTC 时间编辑。
- 按钮与旋钮输入分发。
- WS2812 输入反馈。
- RTC 读取、校验、写入和一次性自动初始化。
- OLED 渲染和对比度控制。
- 使用 NVS / Preferences 持久化 UI 设置。
- 仅在 CLOCK 页面进行保守的 Light Sleep。

## 关键约束

- UI 文本必须使用英文，因为当前显示字库只支持 ASCII 范围。
- OLED 为 128x64，按 8 个 page、每页 8 像素寻址。
- 页面切换时必须清屏或使显示缓存失效，避免旧文本残留。
- 进入 SETTING 后，后台计时器必须继续运行。
- 长按 Mode 进入 SETTING 后，释放 Mode 不能再触发短按切页。
- Light Sleep 不能破坏计时准确性或长按检测。
- 亮度属于 UI 配置，必须保存到 ESP32 NVS / Preferences，不能保存到 DS1302。
- TIME SET 只修改小时、分钟和秒；必须保留当前 RTC 的日期和星期。
- WS2812 输入反馈是临时功能，后续可能移除，实现时应隔离为独立模块或适配层。
- RTC 正常状态下应避免固定每秒读取；CLOCK/TIMER 只显示到分钟，设计应采用“分钟边界优先 + 最长 30 秒兜底”的读取策略，异常和写入确认路径使用 1 秒短周期。

## 已知风险

- 旧代码缺少 SETTING、Mode 长按处理、亮度持久化和 OLED 对比度接口。
- 旧代码的唤醒按键处理会把唤醒按键当作普通短按消费，这与 Mode 长按进入 SETTING 的需求冲突，需要重新设计。
- DS1302 写入确认涉及 BCD 秒字段和秒进位，需要谨慎处理。
- RTC 读取调度如果只按分钟边界执行，需要确保 TIME SET 写入、RTC 异常恢复和界面首次渲染可以立即刷新。
- 需求规定 SETTING 菜单页 Cancel 固定保存亮度并返回 TIMER，不管从哪个页面进入 SETTING。该行为不太常规，实现时需要显式处理。
- 当前 `README.md` 引用了 `src/` 路径，但工作树中 `src/` 已被移到 `backup/`。
- RTC、OLED 亮度档位、旋钮方向和 Light Sleep 唤醒仍需要硬件实测。

## 验证方式

可用自动化验证有限，因为这是嵌入式固件项目：

- 使用 `pio run` 做编译/构建验证。
- 静态审查状态转换和边界值处理。
- 在硬件上按 `REQUIREMENTS.md` 和本规格的验收点进行人工验证。

当前项目尚未配置单元测试框架。
