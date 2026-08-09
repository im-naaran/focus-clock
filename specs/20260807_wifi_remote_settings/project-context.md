> 状态：已确认

# 项目上下文：WiFi 远程设置

日期：2026-08-07
Feature：wifi_remote_settings
阶段：Phase 0

## 需求来源与当前状态

- 主要来源为 `docs/wifi-remote-settings.md`，该文档当前标记为“未来需求与方案备忘 / 概念方案”，尚不是已确认的实施规格。
- `docs/ntp-time-sync.md` 依赖本功能，但 NTP 不属于本次范围。
- 工作树中的上述两份文档及 `docs/power-optimization-roadmap.md` 已暂存为新增文件；本规格不得覆盖或改写这些用户已有变更。
- 本次按规范驱动流程先固化项目上下文和需求。Phase 1 确认前不生成技术设计，不修改生产代码。

## 技术栈与构建

- MCU：ESP32-C3。
- 硬件：Waveshare ESP32-C3-Zero、SSD1306 128x64 OLED、DS1302 RTC、旋钮及独立按键。
- 构建系统：PlatformIO，环境 `esp32-c3-zero`。
- 框架：Arduino ESP32。
- 当前显式外部依赖仅为 `adafruit/Adafruit NeoPixel`。
- 本机已安装的 Arduino ESP32 框架包含 `WiFi`、同步 `WebServer` 和 Espressif `qrcode` 组件，可优先复用；项目当前没有 JSON 库。
- 当前没有自动化单元测试框架，基础构建命令为 `/Users/naaran/.platformio/penv/bin/pio run`。

## 当前架构

主循环位于 `src/main.cpp`，采用单线程轮询：

1. 更新输入并消费事件。
2. 更新 Timer、RTC 和 SETTING 闪烁状态。
3. 按 `displayDirty` 渲染 OLED。
4. 更新反馈灯和显示电源。
5. 尝试进入 Light Sleep。

启动时 `disableRadios()` 会执行 `WiFi.disconnect(true)`、`WiFi.mode(WIFI_OFF)` 并关闭蓝牙控制器。WiFi 生命周期目前没有独立服务。

### 应用状态与交互

- `AppState` 聚合页面模式、Timer、SETTING、持久配置、RTC 和显示电源状态。
- 顶层页面为 `Clock`、`Timer`、`Setting`。
- SETTING 当前有 `BRIGHTNESS`、`TIME SET`、`NIGHT OFF` 三项，旋钮在固定枚举分支中循环；没有通用菜单列表或滚动窗口。
- Confirm 和独立 Cancel 按键已存在。配置模式的“取消”应优先复用物理 Cancel，而不是在 OLED 上模拟触控按钮。
- 长按 Mode 可从 CLOCK 或 TIMER 进入 SETTING，因此进入 WiFi 配置时 Timer 可能仍在后台运行。

### 显示能力

- OLED 为 128x64，按 8 个 page（每页 8 像素高）绘制。
- 当前只提供 ASCII 5x7 文本、双倍文本和对话框 API；每行最多缓存 21 个字符。
- 驱动按整行缓存和刷新，没有任意 bitmap / pixel 绘制 API。
- 需求采用两阶段 OLED：连接前显示开放 AP 名，客户端接入后分行显示完整文本 URL 和 Cancel 提示。早期 QR 方案因 128x64 上尺寸过小已取消。
- WiFi 状态图标同样需要区域绘制能力，且必须避免被后续整行文本刷新擦除。

### 持久化

- `src/persistence.*` 使用 `Preferences`，命名空间为 `focusClock`。
- 当前持久化亮度和夜间息屏配置，读取时校验并为旧设备提供默认值。
- 保存接口逐 key 写入，不具备事务语义。远程一次保存多个字段时，需要先完整校验，再定义部分写入失败的响应和运行态处理。

### RTC

- DS1302 通过 `rtcSetTime(...)` 写入，`RtcServiceState` 负责周期读取及强制刷新。
- 本机 TIME SET 当前要求 RTC 已有效才允许写入；远程设置“修复无效 RTC”是否允许，需要在本功能中明确，不能机械复用当前 UI 前置条件。

### Light Sleep 与联网

- `sleep_manager.cpp` 只允许 CLOCK 页面进入主动 Light Sleep，但会周期性调用 `esp_light_sleep_start()`。
- 当前睡眠条件不知道 WiFi 状态。配置门户运行时必须阻止 Light Sleep，否则 SoftAP/HTTP 不可持续服务。
- 最终需求不提供持续联网的 `ON` 策略，只保留 `OFF` 和按需联网的 `AUTO`。配置模式或 AUTO 联网任务活动期间阻止现有主动 Light Sleep，任务结束后关闭无线并恢复原睡眠策略。

## 可复用能力与差距

| 能力 | 现状 | 决策方向 |
| --- | --- | --- |
| WiFi AP/STA/扫描 | Arduino `WiFi` 可用 | 复用，封装生命周期和策略状态机 |
| HTTP 服务 | Arduino `WebServer` 可用 | 复用；每轮主循环非阻塞轮询 |
| 文本网址 | 现有 OLED 文本绘制可用 | 按协议和地址分行显示，不使用 QR 组件 |
| JSON | 项目无结构化 JSON 库 | 设计阶段比较小型依赖与受控序列化方案，禁止无确认升级依赖 |
| 持久化 | `Preferences` 包装已存在 | 扩展现有模块并保留旧设备默认值兼容 |
| RTC 写入 | `rtcSetTime`、RTC service 可用 | 复用写入和强制刷新能力 |
| OLED 文本 | 整行文本和缓存可用 | 复用文本 API 显示网址；区域 bitmap API 仅供 WiFi 图标 |
| SETTING | 状态机和旋钮输入可用 | 扩展为可滚动菜单或明确五项菜单窗口 |
| 睡眠门禁 | 单一 `canEnterClockLightSleep` | 扩展无线活动 blocker，不引入通用任务 blocker |

## 关键风险

- `WiFi.scanNetworks()` 同步扫描可能阻塞数秒，期间主循环无法处理物理 Cancel、Timer、RTC、OLED 和 HTTP；这与现有交互架构冲突。
- `WebServer` 默认监听所有活动网络接口。配置模式采用 `WIFI_AP_STA` 时，配置页可能通过 STA 地址暴露到外部局域网，与“只在本地 AP 内使用”冲突。
- 文本 URL 不能让手机自动加入 SoftAP；用户需先按 OLED 显示的 SSID 加入开放热点，再手动输入网址。
- SSID 最大值是 32 字节而非 32 个 Unicode 字符；WPA2 密码规则也不能只用缓冲区长度表达。
- HTTP 保存与本机输入共享状态。虽然同步 `WebServer` 可在主循环内避免并发写，但任何阻塞 handler 都会影响设备实时交互。
- AP 与 STA 共用单个 ESP32-C3 2.4GHz 无线电，`AP_STA` 下 AP 信道通常会跟随 STA，连接过程可能让手机短暂掉线；保存后的页面反馈必须容忍连接中断。
- 配置模式按需求永不超时且 SoftAP 不设密码，附近任何人都可在配置模式运行期间加入；OLED 必须持续醒目提示，且物理 Cancel 必须可靠。
- 多字段 NVS 保存不是原子的；掉电或单 key 写失败可能形成混合配置。
- WiFi 图标使用区域绘制后，现有整行缓存可能出现覆盖或陈旧内容，需要统一显示合成规则。
