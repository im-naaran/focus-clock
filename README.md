# ESP32-C3 Focus Clock

一个基于微雪 Waveshare ESP32-C3-Zero 的桌面时钟 / 专注计时器。当前固件使用 SSD1306 OLED 显示、DS1302 RTC 保持时间、键盘旋钮和独立按钮完成时钟查看、倒计时和正计时操作。

## 当前功能

- 默认进入 `CLOCK` 页面，显示当前时间和日期。
- `TIMER` 页面支持倒计时和正计时。
- 旋钮每步调整 1 分钟倒计时时长。
- 板载 WS2812 用作输入反馈灯。
- RTC 无效时会尝试用固件编译时间自动初始化。
- `CLOCK` 页面空闲时进入 Light Sleep，降低待机功耗。

## 硬件组成

- 微雪 Waveshare ESP32-C3-Zero
- SSD1306 128x64 I2C OLED
- DS1302 RTC 模块
- Leobog 风格键盘旋钮：`V/W` 旋转相位，`X/Z` 按压开关
- 独立 `Mode` / `Cancel` 按钮

引脚定义集中在 [src/config.h](/Users/naaran/Github/focus-clock/src/config.h:7)。

## 当前 GPIO 接线

```text
左侧                              右侧
+--------------------------------+        +--------------------------------+
| 5V                             |        | GPIO21 -> Mode 按钮 / UART0 TX |
| GND                            |        | GPIO20 -> 旋钮 V / UART0 RX    |
| 3V3-OUT                        |        | GPIO19 -> USB D+，避免占用     |
| GPIO0    -> 未用               |        | GPIO18 -> USB D-，避免占用     |
| GPIO1    -> OLED SDA           |        | GPIO10 -> 板载 WS2812，避免占用 |
| GPIO2    -> OLED SCL / strap   |        | GPIO9  -> 未用 / BOOT / strap  |
| GPIO3    -> DS1302 CLK         |        | GPIO8  -> 旋钮 X / strap       |
| GPIO4    -> DS1302 DAT         |        | GPIO7  -> 旋钮 W / 旋钮1       |
| GPIO5    -> DS1302 RST         |        | GPIO6  -> Cancel 按钮          |
+--------------------------------+        +--------------------------------+
```

旋钮接地：

```text
旋钮 Y / 旋转 GND ----+
                      +---- ESP32 GND
旋钮 Z / 按钮 GND ----+
```

当前实际定义：

| 模块 | GPIO |
| --- | --- |
| OLED SDA | `GPIO1` |
| OLED SCL | `GPIO2` |
| DS1302 CLK | `GPIO3` |
| DS1302 DAT / I/O | `GPIO4` |
| DS1302 RST / CE | `GPIO5` |
| Cancel 按钮 | `GPIO6` |
| 旋钮 V / 旋转相位 1 | `GPIO20` |
| 旋钮 X / Confirm | `GPIO8` |
| 旋钮 W / 旋转相位 2 | `GPIO7` |
| 板载 WS2812 | `GPIO10` |
| Mode 按钮 | `GPIO21` |

按钮和旋钮输入使用 `INPUT_PULLUP`，按下或导通时接 GND。

## GPIO 注意事项

当前先按已有布线维护代码和文档，后续如果重排硬件，优先处理这些引脚风险：

| GPIO | 板上/芯片用途 | 使用建议 |
| --- | --- | --- |
| `GPIO2` | ESP32-C3 strapping 引脚 | I2C 上拉通常可接受；外设不能在启动采样时强拉低 |
| `GPIO8` | ESP32-C3 strapping 引脚 | 当前接旋钮按压；上电或复位时不要按住旋钮 |
| `GPIO9` | BOOT 按键，ESP32-C3 strapping 引脚 | 当前未用；避免外设在启动采样时强拉 |
| `GPIO10` | 板载 WS2812/RGB LED | 已用于输入反馈灯，不建议外接其他设备 |
| `GPIO18` | USB D- | 保留给原生 USB、USB CDC、下载和日志 |
| `GPIO19` | USB D+ | 保留给原生 USB、USB CDC、下载和日志 |
| `GPIO20` | 默认 UART0 RX 标注 | 当前接旋钮 V；如果后续需要 UART0，应调整 |
| `GPIO21` | 默认 UART0 TX 标注 | 当前接 Mode；如果后续需要 UART0，应调整 |
| `GPIO12`..`GPIO17` | 未引出，且通常用于板载 Flash | 不作为可用 GPIO 考虑 |

`GPIO8/GPIO9/GPIO2` 是启动采样相关引脚。旋钮相位脚会随机械位置变化，上电瞬间状态不可控，因此后续重排时不建议把旋钮 `V/W` 放在这些引脚上。

## 页面与交互

`CLOCK` 页面：

- 中间大字显示 `HH:MM`。
- 下方显示日期和星期。
- RTC 读取失败时显示 `RTC READ FAIL`、`RTC INIT...` 或 `RTC INIT FAIL`。

`TIMER` 页面：

- 空闲状态显示可设置的计时时长，默认 `00:00:00`。
- 旋钮只在 `TIMER` 空闲或调整状态下修改时长。
- 设置值为 `00:00:00` 时按 Confirm 启动正计时。
- 设置值大于 0 时按 Confirm 启动倒计时。

按键行为：

| 输入 | 行为 |
| --- | --- |
| Mode | 在 `CLOCK` 和 `TIMER` 之间切换；如果倒计时已完成，会先重置计时器 |
| Confirm / 旋钮按压 | 在 `TIMER` 页面启动、暂停、恢复或重置计时 |
| Cancel | 在 `TIMER` 页面重置计时器 |
| 旋钮旋转 | 在 `TIMER` 空闲或调整状态下，以 1 分钟为步进调整倒计时 |

WS2812 输入反馈：

| 输入 | 颜色 |
| --- | --- |
| Mode | 蓝色 |
| Confirm | 绿色 |
| Cancel | 红色 |
| 旋钮旋转 | 黄绿色 |

## RTC 行为

DS1302 使用三线接口读取和写入时间。固件启动后会读取 RTC：

- 读取有效时正常显示 RTC 时间。
- 读取无效时，串口打印 RTC 原始寄存器，并在短暂延迟后尝试用 `__DATE__` / `__TIME__` 编译时间写入 RTC。
- 编译时间不是精确校时时间，首次自动初始化可能会有上传和启动造成的固定偏差。

当前没有用户设置时间页面；精确校时后续应通过设置页面或串口命令补齐。

## 低功耗策略

当前低功耗策略偏保守：

- 启动后 CPU 降到 `80MHz`。
- 启动时关闭 Wi-Fi 和蓝牙控制器。
- 仅在 `CLOCK` 页面、显示已刷新、没有按键按下、输入反馈灯熄灭时进入 Light Sleep。
- `TIMER` 页面和计时运行期间不进入 Light Sleep。

Light Sleep 唤醒源：

- 1 秒定时唤醒，用于检查 RTC 分钟变化。
- Mode、Confirm、Cancel 的 GPIO 低电平唤醒。
- 旋钮 V/W 根据当前电平配置相反边沿唤醒。

按键唤醒后有一次性消费逻辑，避免短按被普通消抖采样漏掉。

## 构建与串口

项目使用 PlatformIO，环境名为 `esp32-c3-zero`：

```ini
[env:esp32-c3-zero]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
```

当前启用 USB CDC：

```ini
-DARDUINO_USB_CDC_ON_BOOT=1
-DARDUINO_USB_MODE=1
```

串口监视器波特率为 `115200`，上传速度为 `921600`。

## 代码结构

- [src/main.cpp](/Users/naaran/Github/focus-clock/src/main.cpp:1)：应用状态机、页面渲染、按钮和旋钮交互、计时逻辑、Light Sleep。
- [src/config.h](/Users/naaran/Github/focus-clock/src/config.h:1)：GPIO、刷新周期、低功耗参数、计时器步进参数。
- [src/display.cpp](/Users/naaran/Github/focus-clock/src/display.cpp:1) / [src/display.h](/Users/naaran/Github/focus-clock/src/display.h:1)：SSD1306 初始化、5x7 字库、行缓存和文本绘制。
- [src/rtc.cpp](/Users/naaran/Github/focus-clock/src/rtc.cpp:1) / [src/rtc.h](/Users/naaran/Github/focus-clock/src/rtc.h:1)：DS1302 读写、BCD 转换、时间有效性校验。
- [lib/LeobogKnob](/Users/naaran/Github/focus-clock/lib/LeobogKnob/README.md:1)：当前键盘旋钮的 V/W 解码组件。

`SETTING_FEATURE_PLAN.md` 是后续设置页面规划，不代表当前固件已实现功能。
