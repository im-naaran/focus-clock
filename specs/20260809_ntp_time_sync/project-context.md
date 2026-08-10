# 项目上下文：定时任务插件与 NTP 自动对时

> 状态：已确认

日期：2026-08-09
Feature：ntp_time_sync
阶段：Phase 0

## 需求来源与当前状态

- 主要需求来源为 `docs/ntp-time-sync.md`，文档当前标记为“方案草稿，待确认”。
- 本功能依赖已经实现并提交的 WiFi 远程设置能力，相关规格位于 `specs/20260807_wifi_remote_settings/`。
- 用户特别要求插件能力尽量可供后续功能复用，并支持定时任务场景。
- `docs/ntp-time-sync.md` 和 `docs/power-optimization-roadmap.md` 是用户已暂存的新增文件，本规格不修改或覆盖它们。

## 技术栈

- 硬件：Waveshare ESP32-C3-Zero、DS1302 RTC、SSD1306 OLED。
- 固件：C++ / Arduino framework，PlatformIO 环境 `esp32-c3-zero`。
- 网络：ESP32 Arduino `WiFi`、同步 `WebServer`；现有架构以循环轮询推进 WiFi 和 Portal 状态。
- 持久化：ESP32 `Preferences` / NVS，网络配置采用带版本的定长 blob。
- Web：单文件 `web/wifi_portal.html`，构建时嵌入固件。
- 测试：`tests/host/` 下的无 Arduino 宿主 C++ 测试；完整固件通过 PlatformIO 构建验证。

## 架构与目录

- `src/main.cpp`：初始化各 service，并在 `loop()` 中按顺序协调输入、RTC、WiFi、Portal、渲染和休眠。
- `src/app_state.h`：跨模块应用状态及 UI 所需运行态视图。
- `src/rtc.*`：DS1302 底层读写和 `RtcTime` 字段合法性检查。
- `src/rtc_service.*`：周期读取、强制回读和 RTC 无效时的编译时间自动初始化。
- `src/wifi_service.*`：AP/STA 生命周期、扫描、连接测试和按需网络 consumer 管理。
- `src/wifi_logic.*`：与 Arduino 无关的 WiFi 模式、deadline 和数据校验纯逻辑。
- `src/persistence.*`、`src/persistence_codec.*`：Preferences 访问及可宿主测试的 blob 编解码。
- `src/wifi_portal.*`、`src/json_writer.*`、`web/wifi_portal.html`：配置 API、JSON 输出和移动端配置页面。
- `src/sleep_manager.*`：根据页面、输入、显示、WiFi 任务等状态决定是否进入 Light Sleep。
- `src/config_*.h`：硬件、显示、网络和时间常量分域配置，由 `src/config.h` 聚合。

## 相关运行流程

当前主循环关键顺序为：

```text
input / timer
rtcServiceUpdate
wifiServiceUpdate
wifiPortalUpdate
render / feedback / display power
sleepManagerMaybeEnter
```

NTP 功能需要在 RTC 更新之后判断定时任务是否到期，在 WiFi service 更新前申请网络，并在 WiFi service 更新后推进联网、SNTP、RTC 写入及终态释放。

## 编码与接口约定

- 全局 service 使用显式状态结构和 `Begin` / `Update` 函数，由 `main.cpp` 持有实例并协调。
- 需要宿主测试的状态转换、日期判断、deadline 和持久化编解码应放在不依赖 Arduino 的纯 C++ 模块中。
- 运行态使用固定容量和值类型；当前工程不依赖动态分配、RTOS 任务或复杂虚函数体系。
- `millis()` deadline 比较必须处理 `uint32_t` 回绕。
- 主循环中的网络等待不得使用长 `delay()`；`rtcSetTime()` 当前约 20 ms 的写后确认是已有同步边界。
- 公共配置常量按职责进入 `config_network.h` 或 `config_timing.h`，不在业务模块散落 magic number。

## 现有公共能力与复用点

### 按需网络租约

- `WifiServiceState::autoConsumerMask` 已支持多个 bit consumer 聚合需求。
- `wifiServiceRequestAutoNetwork()` 会校验 `AUTO` 策略和已保存凭据，再登记 consumer。
- `wifiServiceReleaseAutoNetwork()` 只释放指定 consumer；最后一个 demand 消失后 service 关闭不再需要的 STA。
- `WifiConsumer` 枚举当前为空，可扩展稳定 consumer 位，无需新建第二套网络生命周期。

### 休眠门禁

- `wifiServiceUpdate()` 已将 consumer、扫描和连接测试聚合为 `WifiRuntimeView::networkTaskActive`。
- `sleep_manager.cpp` 已在该值为真时阻止 Light Sleep。NTP 应复用这条链路，不新增独立 sleep blocker。

### RTC 写入和回读

- `rtcSetTime()` 负责 DS1302 写入与底层确认。
- `rtcServiceForceRead()` 可立即更新 `AppState.rtcTime` 并重排下一次周期读取。
- `RtcTime` 现有类型适合作为调度输入和最终写入值，但日期键、星期映射和 SNTP `tm` 转换尚无公共纯逻辑。

### 持久化

- `persistenceBegin()` 复用同一 Preferences namespace，并在进程内保持打开。
- 网络配置已经展示“定长版本 blob + 独立 codec 测试”的既有模式。
- 尚无通用定时任务运行记录或对时结果存储结构；新增结构应保持版本化和固定布局。

### API 与页面

- `GET /api/config` 已统一返回配置、RTC、WiFi 和运行态 JSON。
- `JsonWriter` 提供定长缓冲区的追加和转义，但响应容量需要随新增字段重新核算。
- 页面已有配置加载与状态展示流程，可扩展一个只读最后成功时间字段。

## 公共能力差距

- 没有定时任务到期判断、当日补执行、运行中去重或完成日期恢复能力。
- 没有可被不同触发源复用的插件执行生命周期契约；现有 service 均由 `main.cpp` 直接调用。
- 没有 SNTP 本轮完成信号、超时、有限重试和显式停止的封装。
- 没有 NTP 时间到 `RtcTime` 的可测试转换与 DS1302 年份边界校验。
- 没有定时任务完成记录和最后成功对时 epoch 的持久化格式。
- 没有展示对时结果的 AppState 视图和 Portal API 字段。

## 约束与风险

- RTC 是每日调度的本地时间来源；RTC 无效时无法可靠判断“今天”和是否已经到期。
- NTP 成功可能修正日期，若完成日期仍使用同步前 RTC 日期，会在同一真实日期重复派发。
- ESP32 Arduino / lwIP SNTP API 的回调、状态和停止函数存在版本约束，必须由固件构建及真机验证确认。
- 系统时间可能保留上次值；仅检查年份合法不能证明本轮 SNTP 已完成。
- `AUTO` 并非常驻联网。所有成功、失败、超时和运行中策略变化路径都必须释放自己的 consumer。
- Portal 与 NTP 任务可能并发形成 `AP_STA`；NTP 只能释放自身 STA demand，不能破坏 Portal AP 生命周期。
- 每轮任务终态写 NVS 会形成每日一次写入，频率可接受；轮询状态和瞬时失败原因不应持久化。
- 页面展示固定中国标准时间时，UTC epoch 到本地文本的转换口径必须唯一，避免浏览器时区造成二次偏移。
- 只有一个首期插件，公共契约应围绕明确的生命周期和资源所有权建立；动态注册、通用 cron 或复杂回调表会扩大当前风险面。
