# ESP32-C3 Focus Clock

一个基于微雪 Waveshare ESP32-C3-Zero 的桌面时钟 / 专注计时器。当前固件使用 SSD1306 OLED 显示、DS1302 RTC 保持时间、键盘旋钮和独立按钮完成时钟查看、倒计时和正计时操作。

## 当前功能

- 默认进入 `CLOCK` 页面，显示当前时间和日期。
- `TIMER` 页面支持倒计时和正计时。
- 旋钮每步调整 1 分钟倒计时时长。
- 板载 WS2812 用作输入反馈灯。
- RTC 无效时显示固定 `12:00` 和故障状态，不向 RTC 写入猜测时间。
- `CLOCK` 页面空闲时进入 Light Sleep，降低待机功耗。
- `SETTING` 提供亮度、RTC 时间、最后成功对时、息屏计划、WiFi 配置门户和 `OFF/AUTO` 联网策略。
- 本机确认后可临时开启开放 SoftAP，通过手机页面编辑设备配置、扫描网络并测试已保存的 STA 凭据。

## 硬件组成

- 微雪 Waveshare ESP32-C3-Zero
- SSD1306 128x64 I2C OLED
- DS1302 RTC 模块
- Leobog 风格键盘旋钮：`V/W` 旋转相位，`X/Z` 按压开关
- 独立 `Mode` / `Cancel` 按钮

引脚定义集中在 [src/config.h](src/config.h)。

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
- RTC 读取失败时固定显示 `12:00` 和 `RTC READ FAIL`。

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
| 长按 Mode | 从 `CLOCK` 或 `TIMER` 进入 `SETTING` |

`SETTING` 页面：

- 六项菜单采用三行滚动窗口，旋钮循环选择，Confirm 进入，Cancel 返回。
- `BRIGHTNESS` 调整 OLED 亮度。
- `TIME SET` 设置 RTC 小时和分钟。
- `TIME SYNC` 只读展示最后一次完整成功的 NTP 对时时间；按固定 UTC+8 分行显示日期和秒级时间，从未成功时显示 `NEVER`。
- `SCREEN SCHEDULE` 配置息屏计划开关、关闭时间和恢复时间。
- `WIFI` 选择持久策略 `OFF` 或 `AUTO`。`AUTO` 只允许业务按需申请联网，不保持常驻连接。
- 在菜单选中 `WIFI CONFIG` 后按一次 Confirm 即启动临时配置门户；Portal 页面中的物理 Cancel 是唯一退出方式。

## WiFi 配置门户

配置模式启动后，OLED 首先显示 `FocusClock-xxxx` 开放热点名称。手机接入后，OLED 自动分行显示 `http://192.168.4.1/`，可直接在手机浏览器输入该网址。

页面支持：

- 读取和保存亮度、RTC、息屏计划及 `OFF/AUTO` 策略；亮度使用 `1 - Darkest` 至 `5 - Brightest` 五档选择框。
- 只读展示最后一次成功 NTP 对时的中国标准时间；从未成功时显示 `never`。
- 保存或清空 STA SSID/密码。密码输入留空时保留已有密码；清空操作不修改策略。
- 异步扫描附近 2.4GHz WiFi，按信号强度展示去重后最强的 20 项；扫描不会连接已保存的外部 AP。
- 使用已成功保存的凭据执行一次性连接测试。测试期间 AP 可能因信道切换短暂中断，页面会重试并从设备恢复最终状态。

配置门户没有自动超时，保存配置也不会退出。必须在设备 OLED Portal 页面按物理 Cancel 停止 HTTP、SoftAP、扫描和连接测试。

安全限制：

- SoftAP 是无密码开放网络，只应在需要配置时由本机临时开启。
- Portal 使用 HTTP，无 TLS；任何已连接该 SoftAP 的客户端均可访问。
- Portal 拒绝从 ESP32 STA 局域网接口到达的请求，但这不替代网络隔离。
- handler 会将超过 1024 字节的 POST 拒绝为 `413`，且不修改配置；同步 `WebServer` 会在 handler 前解析 URL-encoded 和 multipart body，恶意大请求的解析前内存压力仍是残余风险。
- STA 密码由 ESP32 Preferences 以明文形式存入 NVS；API 和默认串口日志不会返回或打印密码。

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
- 读取无效时，时钟区域固定显示 `12:00`，状态行显示 `RTC READ FAIL`，串口打印 RTC 原始寄存器。
- 固件不会用编译时间或固定默认值写入无效 RTC，并继续短周期读取；重启、硬件恢复或用户显式设置有效时间后自然恢复正常显示。

用户可在本机 `TIME SET` 设置小时和分钟，也可在 WiFi 配置页显式提交完整浏览器本地时间。远程时间只有在保存请求明确包含时间更新时才写入 RTC，并可修复当前无效的 RTC。

## 每日 NTP 对时

固件在 RTC 有效且 WiFi 策略为 `AUTO` 时，于每天中国本地时间 `08:00:00..08:00:59` 内首次观察到该分钟后启动一次 NTP 对时。若设备或 RTC 读取错过整个 `08:00` 分钟，当天不会在 `08:01` 或更晚补执行；任务派发前会先保存当天已尝试记录，因此成功、最终失败或执行中重启都不会在同一天再次自动派发。

Time Sync 使用独立 WiFi consumer 按需申请 STA，不会让 `AUTO` 变成常驻联网。每轮最多进行两次非阻塞 SNTP attempt，单次等待 10 秒，两次之间等待 3 秒，整轮网络 deadline 为 20 秒。只有本轮 SNTP 状态明确完成后，UTC epoch 才会按固定 UTC+8 转换并写入 DS1302；RTC 只写一次并立即回读，完整成功后才持久化最后成功 epoch。所有终态都会停止本轮 SNTP 并释放 Time Sync consumer。

OLED 的 `TIME SYNC` 与 Portal 的 `Last successful sync` 使用同一个持久化成功 epoch。时间由设备按 `YYYY-MM-DD HH:MM:SS` 格式生成，不使用手机或浏览器时区转换；Portal 根页面和配置读取响应禁止缓存，刷新或重新打开后读取设备当前值。WiFi 为 `OFF`、凭据缺失、网络/NTP 超时或 RTC 提交失败时，本轮最终失败且当天不自动重试；RTC 无效时不会派发任务，并继续采用 `12:00` 与 `RTC READ FAIL` 的故障展示。

## 低功耗策略

当前低功耗策略偏保守：

- 启动后 CPU 降到 `80MHz`。
- 启动时关闭 Wi-Fi 和蓝牙控制器；配置模式或按需网络任务才临时启用 WiFi。
- 仅在 `CLOCK` 页面、显示已刷新、没有按键按下、输入反馈灯熄灭时进入 Light Sleep。
- `TIMER` 页面和计时运行期间不进入 Light Sleep。
- 配置模式、异步扫描、连接测试和每日 Time Sync 网络任务活动期间不进入 Light Sleep；任务结束并回到无线关闭状态后恢复原策略。

Light Sleep 唤醒源：

- 动态定时唤醒，用于按 RTC 下一次读取时间刷新；RTC 正常时对齐分钟边界并保留最长 30 秒兜底，RTC 异常时使用短周期。
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

构建固件：

```bash
pio run -e esp32-c3-zero
```

无硬件宿主测试位于 `tests/host/`，覆盖每日调度、Time Sync 状态与时间转换、网络/持久化逻辑、SETTING 导航、JSON 转义和 Portal 字段/日期校验。配置页面独立维护在 `web/wifi_portal.html`，其中的 JavaScript 可直接提取后用 Node.js 做语法检查；PlatformIO 构建时通过 `board_build.embed_txtfiles` 将页面嵌入固件。

串口监视器波特率为 `115200`，上传速度为 `921600`。

## 代码结构

- [src/main.cpp](src/main.cpp)：初始化并协调输入、周期状态、WiFi service、Portal、渲染与睡眠。
- [src/config.h](src/config.h)：聚合 GPIO、显示、网络、刷新周期、低功耗和计时配置。
- [src/display.cpp](src/display.cpp) / [src/display.h](src/display.h)：SSD1306 初始化、5x7 字库、行缓存和文本绘制。
- [src/rtc.cpp](src/rtc.cpp) / [src/rtc.h](src/rtc.h)：DS1302 读写、BCD 转换、时间有效性校验。
- [src/wifi_service.cpp](src/wifi_service.cpp)：AP/STA 生命周期、异步扫描、连接测试和网络运行状态。
- [src/scheduled_task_service.cpp](src/scheduled_task_service.cpp)：每日精确分钟调度、派发前记账和结果日期修正。
- [src/time_sync_task.cpp](src/time_sync_task.cpp)：按需 WiFi、非阻塞 SNTP、RTC 提交和资源释放状态机。
- [src/wifi_portal.cpp](src/wifi_portal.cpp)：HTTP 生命周期、接口 guard、配置 API 和页面路由。
- [web/wifi_portal.html](web/wifi_portal.html)：独立维护的移动配置页面，由 PlatformIO 嵌入 Flash。
- [lib/LeobogKnob](lib/LeobogKnob/README.md)：当前键盘旋钮的 V/W 解码组件。
