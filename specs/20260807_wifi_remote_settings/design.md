> 状态：已确认

# 技术设计：WiFi 远程设置

日期：2026-08-07
Feature：wifi_remote_settings
阶段：Phase 2

## 设计目标

在现有 ESP32-C3 Arduino 单线程主循环中增加按需 WiFi、开放 SoftAP 配置门户和远程设置能力，同时保持 CLOCK、TIMER、RTC、OLED 息屏和物理输入语义不变。

设计原则：

- `WIFI CONFIG` 与 `OFF/AUTO` 策略相互独立。
- AP 需求和 STA 需求分别建模，再由纯函数推导无线目标模式。
- 不引入持续联网的 `ON`，不让“保存配置”隐式形成 STA 常连接。
- 扫描、连接、测试和重试均使用状态机，不阻塞主循环。
- HTTP 只允许经 SoftAP 接口到达，STA 局域网侧统一拒绝。
- 页面状态可恢复，不能依赖浏览器始终在线。
- 复用 Arduino ESP32 内置组件，不新增项目依赖。
- 80MHz 保持为默认频率，先通过资源边界和真机指标验证，不预先升频。

覆盖需求：R-01..R-45、NFR-01..NFR-07、AC-01..AC-14。

## 总体架构

```text
Physical input
    |
    v
app_controller ----------> AppState / NetworkConfig
    |                              |
    | config mode request          | render view
    v                              v
wifi_service <-------------- ui_render / sleep_manager
    |  AP/STA/scan/test
    |
    +-------- wifi_portal -------- WebServer -------- Browser
                   |                    |
                   |                    +-- AP-interface guard
                   |
                   +-- persistence / RTC service / config validation
```

模块职责：

| 模块 | 职责 |
| --- | --- |
| `network_types.h` | 与 Arduino 无关的策略、配置、目标模式及公开状态类型 |
| `wifi_logic.*` | 目标模式推导、凭据更新规则、字段校验等纯逻辑 |
| `config_network.h` | AP 地址、SSID 规则、超时、扫描上限和 HTTP 限制 |
| `wifi_service.*` | WiFi AP/STA 生命周期、异步扫描、连接测试和运行状态 |
| `wifi_portal.*` | `WebServer` 生命周期、接口路由、AP 接口限制、页面及 API |
| `web/wifi_portal.html` | 独立维护的 HTML/CSS/JS 页面源码，由构建系统嵌入 Flash |
| `persistence.*` | 版本化网络配置 blob 的读取与保存 |
| `display.*` | page-aligned bitmap 区域绘制，用于 WiFi 图标 |
| `app_state.h` | 持久网络配置、SETTING 编辑态和只读网络运行视图 |
| `app_controller.cpp` | `WIFI CONFIG` 确认/取消和 `OFF/AUTO` 编辑 |
| `ui_render.cpp` | 五项滚动菜单、配置引导页、文本网址和连接图标 |
| `sleep_manager.cpp` | 配置模式或 AUTO 任务活动时禁止现有主动 Light Sleep |
| `main.cpp` | 初始化并按固定顺序协调 service、portal、render 和 sleep |

`wifi_service` 不直接写业务配置；`wifi_portal` 不直接决定无线模式。两者通过明确命令和状态组合，避免 HTTP handler 内部随意调用 `WiFi.mode(...)`。

## 数据模型

### 持久配置

```cpp
enum class WifiPolicy : uint8_t {
  Off = 0,
  Auto = 1,
};

struct NetworkConfig {
  WifiPolicy policy = WifiPolicy::Off;
  char staSsid[33] = {};
  char staPassword[65] = {};
};
```

SSID 按 UTF-8 字节数限制为 32 字节，末尾预留 `\0`。密码允许为空；非空值的长度和格式按当前 Arduino ESP32 支持范围校验。

`UiConfig` 继续保存亮度和夜间息屏，不把 `configModeRunning` 放入任何持久结构。

### 无线目标模式

```cpp
enum class WifiTargetMode : uint8_t {
  Off,
  Ap,
  Sta,
  ApSta,
};

struct WifiModeInputs {
  bool configModeRequested = false;
  WifiPolicy policy = WifiPolicy::Off;
  bool autoTaskDemand = false;
  bool portalScanDemand = false;
  bool connectionTestDemand = false;
};
```

纯函数推导：

```text
apNeeded  = configModeRequested
staNeeded = connectionTestDemand
            OR portalScanDemand
            OR (policy == AUTO AND autoTaskDemand)

false, false -> WIFI_OFF
true,  false -> WIFI_AP
false, true  -> WIFI_STA
true,  true  -> WIFI_AP_STA
```

连接测试和 Portal 扫描只有配置模式运行时才可启动，因此它们可以在持久策略为 `OFF` 时临时形成 `WIFI_AP_STA`。普通 AUTO task 在策略为 `OFF` 时请求失败，不得打开无线。Portal 扫描结束、失败或被物理 Cancel 取消后立即释放 scan demand，使目标模式恢复 `WIFI_AP`。

### 运行状态

```cpp
enum class WifiConnectionState : uint8_t {
  Disabled,
  NotConfigured,
  Starting,
  Connecting,
  Connected,
  Failed,
};

enum class WifiScanState : uint8_t {
  Idle,
  Running,
  Complete,
  Failed,
};

enum class WifiTestState : uint8_t {
  Idle,
  Connecting,
  Succeeded,
  Failed,
  TimedOut,
};

struct WifiRuntimeView {
  bool configModeRunning = false;
  bool apClientConnected = false;
  bool networkTaskActive = false;
  WifiConnectionState connectionState = WifiConnectionState::Disabled;
  WifiScanState scanState = WifiScanState::Idle;
  WifiTestState testState = WifiTestState::Idle;
  char staIp[16] = {};
  char apSsid[18] = {};
  char portalUrl[24] = {};
};
```

`WifiRuntimeView` 挂入 `AppState`，只供 controller、renderer 和 sleep gate 观察。重试 deadline、测试 timeout、扫描结果和实际模式保留在 `WifiServiceState` 内，不污染应用模型。

`WifiServiceState` 保存：

- 当前和目标 `WifiTargetMode`。
- 配置模式启动/停止阶段。
- AUTO consumer bitmask；本阶段只预留类型和接口，不创建虚假业务 consumer。
- 异步扫描状态、结果数量和清理标记。
- 测试开始时间、deadline、最终结果和最近错误。
- STA 当前连接尝试和下次重连时间。
- AP 客户端数量的上次采样值。

所有 deadline 使用 `int32_t(now - deadline) >= 0` 形式，保证 `millis()` 回绕安全。

## 配置常量

新增 `src/config_network.h` 并由 `config.h` 引入：

```cpp
static constexpr const char *WIFI_CONFIG_AP_SSID_PREFIX = "FocusClock-";
static constexpr uint8_t WIFI_CONFIG_AP_CHANNEL = 6;

static constexpr uint8_t WIFI_CONFIG_AP_IP[] = {192, 168, 4, 1};
static constexpr uint8_t WIFI_CONFIG_AP_GATEWAY[] = {192, 168, 4, 1};
static constexpr uint8_t WIFI_CONFIG_AP_SUBNET[] = {255, 255, 255, 0};

static constexpr uint32_t WIFI_AP_CLIENT_POLL_MS = 250;
static constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_STA_RECONNECT_MS = 30000;
static constexpr uint8_t WIFI_SCAN_MAX_RESULTS = 20;
static constexpr size_t WIFI_SSID_MAX_BYTES = 32;
static constexpr size_t WIFI_PASSWORD_MAX_BYTES = 64;
static constexpr size_t HTTP_MAX_BODY_BYTES = 1024;
```

AP SSID 使用 eFuse MAC 低 16 位的大写十六进制：`FocusClock-A1B2`。SoftAP 调用不传密码，形成开放网络。

访问 URL 由 AP IP 格式化生成，SoftAP 配置和 OLED 文本均调用同一 helper，不重复硬编码地址。

## WiFi Service 状态机

### 初始化

`setup()` 顺序调整为：

1. 保持 80MHz 和关闭蓝牙。
2. 初始化输入、RTC、display 和 feedback。
3. 加载现有 UiConfig 和 NetworkConfig。
4. 初始化 `wifiServiceBegin(...)`，根据默认/持久策略推导 `WIFI_OFF`。
5. 初始化 portal，但不启动 server。
6. 初始化 RTC、display power 和 sleep manager。

现有 `disableRadios()` 中 WiFi 操作由 `wifiServiceBegin()` 接管，避免启动代码和 service 争夺无线状态；蓝牙关闭仍保留在 main 的硬件初始化中。

### 主循环顺序

```text
inputUpdate / dispatch input
timerUpdate / rtcServiceUpdate / setting blink

wifiServiceUpdate
  reconcile target mode
  advance async scan
  advance STA/test state
  sample AP client count
  update AppState.wifiRuntime

wifiPortalUpdate
  start/stop WebServer after AP readiness
  handle one iteration of HTTP

render when displayDirty
feedback / display power
sleepManagerMaybeEnter
```

无线状态变化只有在公开 view 真正变化时设置 `displayDirty`，避免无意义 OLED 刷新。

### 模式协调

模式切换集中在一个 reconcile 函数：

- `Off -> Ap`：先 `WiFi.mode(WIFI_AP)`，设置静态 AP 网络，再启动开放 AP。
- `Ap -> ApSta`：切换到 `WIFI_AP_STA` 并保留 AP；模式迁移本身不建立 STA 连接。
- `ApSta -> Ap`：断开 STA，但不传 `eraseap=true`，然后保持 AP 模式。
- `Sta -> Off`：断开 STA并进入 `WIFI_OFF`。
- `Ap/ApSta -> Off`：先停止 HTTP，再关闭 AP 和无线。

禁止在保存 handler 里直接关闭 AP。保存只更新配置和 demand；主循环在 handler 返回后统一协调模式。

### AUTO task 接口

预留明确 consumer API：

```cpp
bool wifiServiceRequestAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer,
                                   WifiPolicy policy,
                                   uint32_t nowMs);

void wifiServiceReleaseAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer);
```

策略不是 `AUTO` 时普通 consumer 请求返回 false。consumer 使用 bitmask 幂等登记，多个未来任务可共享一次 STA 连接；最后一个 consumer 释放后关闭 STA。本阶段不实现通用 sleep blocker 系统，sleep manager 只读取聚合的 `networkTaskActive`。

### 异步扫描

启动扫描：

```cpp
WiFi.scanDelete();
WiFi.scanNetworks(true, false);
```

Arduino ESP32 的 `scanNetworks(true)` 内部会调用 `WiFi.enableSTA(true)`。为避免框架绕过 service 改变实际模式，启动扫描分两步执行：先登记 `portalScanDemand`，由 reconcile 将 `WIFI_AP` 协调为 `WIFI_AP_STA`；确认目标模式就绪后才调用异步扫描。模式 reconcile 只负责接口开关，不调用 `WiFi.begin()`；只有 AUTO consumer 或一次性连接测试才形成 STA 连接 demand。完成、失败或退出配置模式时先 `scanDelete()`，再释放 demand，由 reconcile 恢复 `WIFI_AP`。

主循环通过 `WiFi.scanComplete()` 观察：

- `WIFI_SCAN_RUNNING`：保持 Running。
- `WIFI_SCAN_FAILED`：记录 Failed。
- `>= 0`：提取结果后标记 Complete。

结果处理规则：

- 最多返回 20 项。
- 隐藏 SSID 不返回。
- 相同 SSID 去重，保留 RSSI 最强项。
- 按 RSSI 降序。
- 只输出 `ssid`、`rssi`、`secure`，不输出 BSSID。
- 容量满后若出现更强的新 SSID，替换已保留的最弱项，确保最终为 RSSI 最强的 20 项。
- 新扫描启动或退出配置模式时调用 `WiFi.scanDelete()` 释放驱动结果内存。

扫描会占用无线电并可能短暂影响 AP 吞吐，但不会阻塞 CPU 主循环。物理 Cancel 始终优先由输入队列处理；退出配置模式时取消/清理扫描状态。

### 一次性连接测试

前置条件：

- 配置模式正在运行。
- 已成功持久化的 SSID 非空。
- 页面没有未保存的 WiFi 字段修改。
- 当前没有扫描或其他连接测试。

流程：

```text
POST test
  -> validate persisted config
  -> testState = Connecting
  -> connectionTestDemand = true
  -> target becomes WIFI_AP_STA
  -> WiFi.begin(saved ssid, saved password)

loop
  -> WL_CONNECTED: capture success, release demand
  -> terminal failure / timeout: capture error, release demand
  -> target returns WIFI_AP
```

测试结果保留在设备运行态，直到下一次测试、清空配置或退出配置模式。STA 关闭后结果仍可查询，避免页面错过短暂的成功状态。

STA 连接可能迫使 AP 跟随目标路由器信道，手机会短暂断开。测试前页面显示确认提示；测试中覆盖显示状态并禁用保存、清空、扫描和重复测试。JS 每 750ms 查询一次，网络失败后指数退避至 3 秒并持续重试。

`beforeunload` 只作为尽力提示，不能成为正确性依赖。刷新或重新连接后，页面从设备读取权威 `testState` 并恢复状态展示。

## 配置模式与本机 UI

### SETTING 状态

新增菜单项：

```cpp
enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
  NightScreenOff,
  WifiConfig,
  WifiPolicy,
};
```

新增设置状态：

```cpp
WifiConfigPortal,
WifiPolicyEdit,
```

在 SETTING 菜单选中 `WIFI CONFIG` 并按物理 Confirm 时，同一输入处理直接设置 `configModeRequested=true` 并进入 `WifiConfigPortal`；不保留 `WifiConfigConfirm` 或二次确认页。Portal 页先显示 AP 启动态，成功后显示手机连接页；启动失败时继续显示启动状态并由 service 重试，Cancel 可随时返回菜单。

portal 页中的物理 Cancel：

1. 清除 `configModeRequested`。
2. portal 停止接受新请求并关闭 server。
3. service 释放扫描/测试状态和 AP。
4. 返回 SETTING 菜单并重绘。

### 五项菜单

OLED 可用三行菜单区域，采用固定三行滚动窗口，不把五项同时压入屏幕：

```text
SETTING          12:30

  TIME SET
> NIGHT OFF
  WIFI CONFIG
```

旋钮仍循环导航；选中项始终保持在可见窗口内。菜单 label 数组和 item count 集中定义，替代现有手写三分支循环，后续新增项不再复制判断链。

### 两阶段配置引导

无 AP 客户端：

```text
WIFI CONFIG
CONNECT PHONE
FocusClock-A1B2


CANCEL TO EXIT
```

检测到至少一个客户端后：

```text
OPEN IN BROWSER
HTTP://
192.168.4.1/

CANCEL TO EXIT
```

完整网址允许按协议和地址分行，以保证 128x64 上清晰可读；不生成或显示 QR。最后一个 AP 客户端断开后回到 SSID 引导页，文本重绘必须清除上一页内容。

## Display overlay

现有 QR renderer 和对应宿主测试删除，不再链接 Espressif QR 组件。page-aligned bitmap API 继续保留并只服务 WiFi 图标等小型 overlay。

### Bitmap API

新增：

```cpp
void displayWritePageBitmap(uint8_t x,
                            uint8_t firstPage,
                            uint8_t width,
                            uint8_t pageCount,
                            const uint8_t *data);

void displayClearPageRegion(uint8_t x,
                            uint8_t firstPage,
                            uint8_t width,
                            uint8_t pageCount);
```

数据采用 SSD1306 兼容的 column-major/page byte 格式，避免引入 1024 字节全屏 framebuffer。实现约束：

- 页面切换继续调用 `displayClear()` 和 `displayInvalidateCache()`。
- 客户端接入后的网址页使用整行文本覆盖上一页，避免页面切换留下陈旧内容。
- WiFi 图标固定保留在 page 7 最右 8 像素，常规底栏文本不得使用该区域。
- 图标消失时必须显式写 8 个零字节，不能只依赖文本 cache。

8x8 WiFi bitmap 复用该 API，不新增专用硬编码传输路径。

## 持久化设计

### 网络配置 blob

网络配置使用一个版本化 NVS blob，避免 SSID、密码和策略逐 key 更新造成网络配置内部混合状态：

```cpp
struct PersistedNetworkConfigV1 {
  uint8_t version;
  uint8_t policy;
  char ssid[33];
  char password[65];
};
```

Preferences key 使用短名 `netCfg`。读取规则：

- key 不存在：返回 `OFF` 和空凭据。
- size/version 不匹配：记录日志并回退默认值。
- policy 非法、字符串无终止符或字段超限：整份网络配置回退默认值。
- 密码不输出到日志。

保存使用单次 `putBytes`；只有成功后才更新内存中的 `NetworkConfig`。旧固件没有该 key，天然兼容。

### Web 保存规则

先解析并校验整个请求，再构造 candidate：

```text
ssid empty
  -> candidate.ssid = ""
  -> candidate.password = ""

ssid non-empty
  -> candidate.ssid = submitted ssid
  -> password empty: preserve current password
  -> password non-empty: replace password
```

独立“清空 WiFi 配置”接口构造空 SSID/密码 candidate，语义与保存空 SSID一致。策略独立保留，除非请求明确提交新的 `OFF/AUTO`。

页面 GET 只返回 `passwordConfigured`。页面密码输入始终为空，并显示“留空保持原密码”的辅助文本。

亮度、夜间息屏、网络 blob 和 RTC 分属不同存储/硬件，无法形成跨设备事务。保存顺序和失败语义：

1. 校验全部字段，不通过则零写入。
2. 保存 NVS 配置项；每项成功后才更新对应内存配置。
3. 请求明确包含 RTC 更新时最后写 RTC。
4. 任一步失败返回 `500 APPLY_PARTIAL` 和实际成功/失败 section 列表，不报告整体成功。
5. 页面收到 partial 后立即重新 GET，以设备实际状态覆盖表单。

不尝试用补偿写“回滚”硬件 RTC 或 NVS，因为掉电和二次写失败会让伪回滚更不可靠。

## HTTP Portal

### Server 生命周期

使用框架内置同步 `WebServer`，每轮主循环调用一次 `handleClient()`。只有 AP 成功运行后 `begin()`；配置模式退出时 `stop()`。

每个 handler 开头统一调用：

```cpp
bool requestArrivedViaSoftAp(WebServer &server) {
  return server.client().localIP() == WiFi.softAPIP();
}
```

不满足时返回 `403 INTERFACE_FORBIDDEN`。该 guard 同样覆盖 `/`、所有 API 和 not-found handler，确保 STA 地址不能访问页面。

### 路由

```text
GET  /
GET  /api/config
POST /api/config
POST /api/wifi/clear
POST /api/wifi/scan
GET  /api/wifi/scan
POST /api/wifi/test
GET  /api/wifi/test
```

POST 使用 `application/x-www-form-urlencoded; charset=UTF-8`，由 `WebServer` 原生表单 parser 读取并生成参数，不手写 JSON parser。handler 再检查 Content-Type 和 1024-byte Content-Length，超限返回 `413` 且不进入配置校验/写入。不使用 `WebServer` raw callback：当表单小于框架固定 1436-byte raw 块时，该路径会等待未发送的剩余字节直到 client timeout，使正常保存固定延迟数秒。所有响应使用 JSON；HTML 仅 `/` 返回。

不新增 ArduinoJson。新增一个 portal 内部 JSON writer，统一负责：

- 字符串引号、反斜杠、控制字符和 UTF-8 字节的正确转义。
- success/error envelope。
- 有上限的 scan array 流式输出。

该 helper 被 config、scan、test 和 error handler 共同复用，并为 escape 逻辑增加 host 测试，符合新增公共能力的复用条件。

### API 响应

成功 envelope：

```json
{"ok":true,"data":{}}
```

错误 envelope：

```json
{"ok":false,"error":{"code":"INVALID_SSID","message":"SSID exceeds 32 bytes"}}
```

主要状态码：

- `400`：字段缺失、类型或范围非法。
- `409`：扫描/测试状态冲突、页面有未保存 WiFi 变更、SSID 未配置。
- `413`：请求体超过上限。
- `415`：Content-Type 不支持。
- `500`：NVS、RTC、AP 或内部状态错误。
- `503`：无线服务尚未就绪。

`GET /api/config` 返回：

- brightness。
- RTC 值、valid 和写入能力。
- night screen off 配置。
- `OFF/AUTO` policy。
- SSID、`passwordConfigured`。
- config mode、AP client、scan/test/connection 状态。
- STA connected 时可返回 STA IP；HTTP guard 仍拒绝从该接口访问。

`POST /api/config` 的 RTC 字段只有 `setRtc=1` 时才写入；普通读取和保存其他字段不得改 RTC。

### 页面资源与交互

HTML/CSS/JS 独立存放于 `web/wifi_portal.html`，使用常规多行结构、两空格缩进、CSS 每条规则独立分行、JavaScript 按函数和事件分段，禁止为了固件嵌入而手工压缩成超长行。`platformio.ini` 使用 `board_build.embed_txtfiles` 将原文件直接嵌入固件，C++ 通过链接器提供的 start/end symbol 和明确长度调用 `send_P`，不维护手写或生成的页面头文件。页面不依赖 CDN，为单列移动端设置工具：

- 基础设置：五档 select 亮度（1 最暗、5 最亮）、RTC、本机时间按钮。
- 夜间息屏：开关、关闭和开启时间。
- WiFi：`OFF/AUTO`、SSID、密码、扫描、保存、清空配置、测试连接。
- 运行状态：保存结果、扫描状态、测试状态和连接错误。

交互规则：

- GET 后填充 SSID，不填充密码。
- 任一 WiFi 字段改变即 `wifiDirty=true` 并禁用测试。
- 保存成功且 SSID 非空后 `wifiDirty=false`，启用测试。
- 点击清空先二次确认；成功后清空字段并禁用测试。
- 选择扫描结果只填写 SSID，不猜测或修改密码。
- 测试前弹出 AP 可能中断提示；测试中启用 `beforeunload`。
- fetch 失败时不把测试判失败，而是显示 AP 暂不可用并重试。
- 页面重新加载时从设备恢复测试状态。

浏览器不能被强制留在页面，设计只提供提示和恢复能力，不宣称绝对阻止退出。

## RTC 写入

远程页面默认只读展示 RTC。用户点击“使用当前浏览器时间”只填表，不立即写硬件；只有随后保存且 `setRtc=1` 才执行：

1. 校验闰年、月份、日期、时分秒范围。
2. 构造完整 `RtcTime`，计算/校验 weekday。
3. 调用 `rtcSetTime(...)`，即使当前 RTC 无效也允许。
4. 成功后 `rtcServiceForceRead(...)`。
5. 返回实际刷新状态。

本功能中的其他代码不得调用 `rtcSetTime`。未来 NTP 服务必须通过独立已批准设计接入。

## Light Sleep 与功耗

`canEnterClockLightSleep(...)` 增加：

```cpp
!app.wifiRuntime.configModeRunning &&
!app.wifiRuntime.networkTaskActive
```

配置模式、异步扫描、连接测试或未来 AUTO task 活动时不进入当前主动 Light Sleep。全部结束且回到 `WIFI_OFF` 后恢复原条件。

CPU 保持 80MHz。第一版限制：

- 独立 HTML 页面和静态资源位于 Flash。
- 请求体最大 1024 bytes。
- 扫描最多 20 项。
- 不引入大型全屏 framebuffer。
- handler 不执行同步扫描或连接等待。

串口调试在 `ENABLE_SERIAL_LOGGING` 下记录最大 handler 时间、连接测试耗时和关键失败；不得打印 STA 密码。真机若出现持续高延迟或内存不足，再单独评估配置模式临时升频，不在本设计中预设 160MHz。

## 公共能力复用评估

检索范围：`src/`、`platformio.ini`、本机 Arduino ESP32 framework 的 WiFi/WebServer/qrcode 头文件和库。

| 能力 | 结论 | 处理 |
| --- | --- | --- |
| WiFi AP/STA/async scan | 框架已有 | 复用，service 统一生命周期 |
| HTTP server | 框架已有 | 复用 `WebServer`，不新增 async server 依赖 |
| POST request parse | 项目没有；表单字段为扁平结构 | 复用 WebServer 原生 form parser，handler 拒绝超限请求，不新增 JSON/HTTP 依赖 |
| JSON response escape | 项目没有，多 endpoint 都需要 | 新建 portal 内部 writer 并测试 |
| Preferences | 项目已有 wrapper | 扩展版本化 blob，不直接散落 Preferences 调用 |
| RTC | `rtcSetTime` / force read 已有 | 复用并放宽远程修复无效 RTC 的入口条件 |
| OLED bitmap | 当前没有 | 扩展 page bitmap API供 WiFi icon 使用 |
| 菜单导航 | 当前手写三项 | 扩展为数组和可视窗口，五项共用 |
| Sleep gate | 当前集中在一个纯条件 | 扩展条件，不新建通用 blocker 框架 |

## 实现约束

- Phase 4 按任务拆分小步实现，不在 WiFi 改动中顺带重构无关 RTC/Timer 代码。
- 网络模式只能由 `wifi_service` 调用 `WiFi.mode`、`softAP`、`begin`、`disconnect`。
- HTTP handler 不等待扫描或连接完成。
- 密码只在持久配置和 `WiFi.begin` 边界读取，不参与日志、GET 或错误消息。
- 对非显然逻辑添加短注释：模式推导的配置测试例外、STA 接口 guard、测试结果保留、bitmap cache 协调和 NVS partial apply。
- 单次使用的页面字段解析保留在 handler 附近；只有校验、JSON escape 和模式推导等真实复用逻辑抽取公共 helper。
- 不修改或覆盖 `docs/wifi-remote-settings.md` 等用户已有暂存文件。

## 错误与恢复

| 场景 | 行为 |
| --- | --- |
| AP 启动失败 | OLED 显示错误；不启动 server；允许重试/Cancel |
| AP 客户端断开 | OLED 回到 SSID 引导；配置模式继续 |
| STA 测试导致 AP 短断 | 页面本地提示并重试；设备测试继续；重连后查询结果 |
| 错误密码/找不到网络 | 测试记录失败，关闭 STA，AP 继续 |
| 扫描失败 | 返回 failed；可再次启动；AP 继续 |
| HTTP 经 STA 到达 | 403，不返回配置数据 |
| 请求过大/非法 | 4xx，零配置写入 |
| NetworkConfig blob 非法 | 整份回退 OFF/空凭据，不输出密码 |
| NVS 部分失败 | 500 APPLY_PARTIAL，返回 section，页面重新读取实际状态 |
| RTC 写失败 | 不伪造成功；保留原 RTC 状态并强制重新读取 |
| 物理 Cancel | 优先停止 server/scan/test/AP，返回 SETTING |

## 验证方案

### 自动化逻辑验证

新增无 Arduino 依赖的 host 测试，使用系统 C++ 编译器直接编译纯逻辑：

- 16 组以上 AP/STA target mode 组合，包括 `OFF + connection test` 例外。
- SSID 空/相同/变化与密码空/非空的 candidate 规则。
- 非法 policy 和长度边界。
- JSON escape 的引号、反斜杠、控制字符和 UTF-8。
- deadline 回绕行为。
- scan 去重、上限、满容量强网络替换和 RSSI 排序逻辑。
- 扫描 demand 需要 STA 接口但不形成 STA 连接 demand；AUTO/test 才形成连接 demand。
- POST 的 Content-Type、Content-Length 上限和超限零写入行为。

基础固件验证：

```text
/Users/naaran/.platformio/penv/bin/pio run
```

构建只作为兜底，不替代上述逻辑测试和硬件验证。

### HTTP 脚本验证

在硬件可访问时使用脚本/curl 验证：

- AP 接口 GET/POST 成功。
- STA 地址访问所有路由均返回 403。
- 非法 Content-Type、超限请求、非法枚举/时间/SSID 返回对应错误。
- GET 从不包含密码。
- 空 SSID保存和 clear endpoint 均清除 SSID/密码但保留 policy。
- 密码留空保留原值；非空替换。
- 扫描 start/status 不阻塞其他 GET。
- 测试只能在保存后启动，刷新后能恢复状态。

### 人工硬件验证

入口：长按 Mode 进入 SETTING，选择 `WIFI CONFIG` 并确认。

重点场景：

1. 开放 AP 名为 `FocusClock-xxxx`，无需密码连接。
2. 无客户端时显示 SSID；连接后自动显示完整文本 URL。
3. 手机按 OLED 文本网址打开页面，网址清晰且不裁剪。
4. 配置模式无超时且不进入 Light Sleep；物理 Cancel 始终可用。
5. 异步扫描期间 OLED、Timer/RTC 和 Cancel 有响应。
6. 保存后才能测试；测试前有中断警告，测试中操作被禁用。
7. AP 因信道切换短断后，页面重连并显示最终测试结果。
8. 错误密码失败后 AP 保持；修正保存后测试成功。
9. 清空按钮清除凭据、保留 policy 并禁用测试。
10. `OFF/AUTO` 重启恢复；无 AUTO task 时无线关闭。
11. AUTO task/test 结束后 STA 和 WiFi 图标消失，Light Sleep 恢复。
12. CLOCK/TIMER/SETTING、夜间息屏和 RTC 手动设置无回归。
13. 80MHz 下记录页面响应、扫描交互、剩余 heap，无持续卡顿或重启。

不得在用户未完成上述真机确认前声明硬件验证通过或生产可用。

## 主要风险与规避

### AP_STA 信道切换

风险：STA 连接到不同信道时，SoftAP 可能切换信道并让手机短暂断开。

规避：测试前警告；状态保存在设备；页面自动重试；测试结束回到 AP-only。不能承诺 AP 在整个测试中绝不掉线。

### 开放 AP 配置暴露

风险：附近客户端在配置模式期间可接入并修改配置。

规避：只能本机开启；OLED 持续提示；不自动超时是已确认产品要求；物理 Cancel 可靠；HTTP 只允许 AP 接口；绝不返回 STA 密码。

### 同步 WebServer 慢客户端

风险：虽然扫描异步，慢 HTTP client 仍可能短暂占用 handler。

规避：静态页面从 Flash 发送，scan 最多 20 项，handler 不等待状态变化，并在任何配置写入前拒绝超过 1024 字节的请求。同步 `WebServer` 会在 handler 前解析 URL-encoded 或 multipart body，因此开放 AP 附近的恶意大请求内存压力是已接受残余风险；若真机 heap 压测不达标，需单独替换 HTTP parser，不再使用存在固定超时延迟的框架 raw callback。

### NVS 与 RTC 非事务

风险：跨 section 保存可能部分成功。

规避：先全量校验；网络配置内部单 blob；逐 section 更新内存；partial 响应后页面重新读取实际状态，不做不可靠补偿写。

### Display overlay cache

风险：WiFi icon 区域可能被整行 cache 错误擦除或留下残影。

规避：区域保留、明确 render 顺序、切页全清、overlay 显式清零，并执行页面切换和图标出现/消失真机检查。

### 80MHz 和 heap

风险：HTML、scan results 和字符串拼接造成延迟或堆碎片。

规避：Flash embed、固定上限、流式 JSON、无新增大型框架；以指标决定是否另行升频。

## 需求追踪

| 需求 | 设计覆盖 |
| --- | --- |
| R-01..R-08 | 配置模式状态、portal 生命周期、两阶段 OLED、物理 Cancel |
| R-09..R-16 | 两态策略、纯模式推导、AUTO demand、sleep gate、图标 |
| R-17..R-21 | NetworkConfig blob、保存/清空/密码规则、partial apply |
| R-22..R-25 | 五项滚动菜单、单次 Confirm 启动、policy edit、portal 页 |
| R-26..R-36 | Flash 页面、RTC、异步扫描、API、AP interface guard |
| R-37..R-40 | config_network、统一文本 URL 和 bitmap overlay |
| R-41..R-45 | 保存后测试、AP 短断提示/恢复、清空按钮 |
| NFR-01..NFR-07 | 非阻塞状态机、内置组件复用、资源边界和逻辑测试 |
| AC-01..AC-14 | 自动化、HTTP 和硬件验证方案 |

## 待确认问题

无。
