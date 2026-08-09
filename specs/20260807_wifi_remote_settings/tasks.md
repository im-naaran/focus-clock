> 状态：已确认

# 实现任务：WiFi 远程设置

日期：2026-08-07
Feature：wifi_remote_settings
阶段：Phase 3

## 执行规则

- 只在本任务文档确认后进入 Phase 4。
- 严格按依赖顺序执行；修改范围重叠的任务串行处理。
- 每完成一项即运行该项验证、勾选状态并追加 `changelog.md`。
- 自动化结果与硬件人工结果分开汇报；用户未确认真机结果前不得声明硬件验证通过或生产可用。
- 不修改或覆盖用户已暂存的 `docs/wifi-remote-settings.md`、`docs/ntp-time-sync.md` 和 `docs/power-optimization-roadmap.md`。

## 任务清单

- [x] task-01: [网络模型] 建立两态策略、目标模式推导和凭据纯逻辑

  - 追踪需求：R-09..R-19、NFR-05、NFR-07。
  - 依赖任务：无。
  - 修改范围：新增 `src/network_types.h`、`src/wifi_logic.h/.cpp`、`src/config_network.h`；更新 `src/config.h`；新增对应 host 测试。
  - 公共能力处理：新建。已检索 `src/`，当前没有网络模型、模式推导或凭据 candidate helper；这些逻辑会被 persistence、service、portal 和测试共同复用。
  - 代码注释要求：注释连接测试绕过持久 `OFF` 的受控例外、空 SSID 清除和空密码保留的业务语义。
  - 完成标准：定义 `WifiPolicy::Off/Auto`、`WifiTargetMode`、`NetworkConfig`；纯函数覆盖 AP/STA demand 组合、policy 校验、SSID/密码边界和 candidate 更新；所有网络常量集中配置。
  - 自动化验证：系统 C++ 编译器运行 host 测试，覆盖全部模式组合、非法 policy、32-byte SSID、空/非空密码、空 SSID 清除和 `millis()` 回绕；随后执行 `pio run` 兜底。
  - 人工验证关注点：无 UI；审查默认值必须为 `OFF` 和空凭据，日志测试数据不得包含真实密码。
  - 待确认问题：无。
  - 执行记录（2026-08-07）：已新增纯 C++ 网络类型、集中网络常量、模式推导、策略/凭据校验、candidate 更新和溢出安全 deadline helper。宿主测试命令成功，覆盖目标模式、非法 policy、SSID/密码边界、凭据更新语义和 `millis()` 回绕；`pio run` 成功，RAM 12.0%、Flash 58.8%。本任务不涉及硬件 UI，默认 `OFF`/空凭据和无密码日志仍由后续持久化及集成任务继续核对。

- [x] task-02: [持久化] 增加版本化 NetworkConfig NVS blob

  - 追踪需求：R-09、R-17..R-21、AC-01、AC-05、AC-08、AC-14。
  - 依赖任务：task-01。
  - 修改范围：`src/persistence.h/.cpp`；扩展 host 测试覆盖 blob 校验和 candidate 提交。
  - 公共能力处理：扩展现有 Preferences wrapper；不在 WiFi service 或 HTTP handler 中直接调用 Preferences。
  - 代码注释要求：说明 blob version、整份回退规则以及单次 `putBytes` 避免网络字段混合状态的原因。
  - 完成标准：`persistenceLoadNetworkConfig()` / `persistenceSaveNetworkConfig()` 可处理 key 缺失、size/version/policy/string 非法和保存失败；密码不进入日志；旧设备默认回退 `OFF`/空凭据。
  - 自动化验证：host 测试覆盖 V1 encode/decode、截断 blob、无终止符、非法枚举和最大长度；`pio run` 验证 Preferences API 编译。
  - 人工验证关注点：串口观察首次启动和重启加载；用测试 SSID验证策略与凭据持久化，确认日志无密码。
  - 待确认问题：无。
  - 执行记录（2026-08-07）：已增加固定 100 字节的 V1 `netCfg` blob codec 及 Preferences 读写接口；缺失 key 返回 `OFF`/空凭据，非法 size/version/policy/终止符/字段组合整份回退，保存使用单次 `putBytes` 且只在完整写入后更新缓存。宿主测试成功，覆盖 V1 往返、截断、未知版本、非法枚举、无终止符和非法运行态；`pio run` 成功，RAM 12.0%、Flash 58.8%。首次启动、重启持久化及串口无密码仍需真机验证。

- [x] task-03: [显示底层] 增加 page bitmap、WiFi 图标和 QR 编码能力

  - 追踪需求：R-08、R-16、R-38..R-40、AC-02、AC-12、NFR-06。
  - 依赖任务：task-01。
  - 修改范围：`src/display.h/.cpp`；可新增 `src/qr_render.h/.cpp`；新增 bitmap/QR host 测试。
  - 公共能力处理：扩展现有 display 驱动；QR 和 WiFi 图标共用 page-aligned bitmap API，复用 SDK `qrcode.h/libqrcode.a`，不新增依赖。
  - 代码注释要求：注释 SSD1306 column/page 数据布局、QR 安静区和文本 cache/overlay 清除约束。
  - 完成标准：可写入/清除指定 page 区域；8x8 WiFi 图标可显式出现/消失；短配置 URL生成不裁剪的 40x40 QR bitmap；页面切换无陈旧 overlay。
  - 自动化验证：host 测试验证模块到 page bytes 的映射、边界、安静区和清零；`pio run` 验证 SDK QR 链接。
  - 人工验证关注点：真机显示测试图案、图标出现/消失和 QR；至少一台手机成功扫描，检查无残影。
  - 待确认问题：无。
  - 执行记录（2026-08-07）：已扩展 SSD1306 page-region 写入/清零 API，区域写入后失效对应文本 cache；新增复用该 API 的 8x8 WiFi 图标显式显示/清除。新增固定 40x40、5-page QR renderer，SDK 使用低纠错和 max version 2，四模块安静区不足时拒绝裁剪。宿主测试命令成功，覆盖模块到 page bit 映射、四边安静区、输出边界、超限拒绝和陈旧缓冲清零；既有网络/持久化测试保持成功。`pio run` 成功（RAM 12.0%、Flash 58.8%），ESP32-C3 工具链可重定位链接确认 SDK QR 三个符号均已解析。真机图案、图标显隐、手机扫码及页面切换残影尚待人工验证。

- [x] task-04: [应用状态] 扩展五项 SETTING、WiFi 策略编辑和配置模式请求

  - 追踪需求：R-01..R-08、R-22..R-25、AC-03、AC-04。
  - 依赖任务：task-01。
  - 修改范围：`src/app_state.h`、`src/app_controller.h/.cpp`；新增菜单导航纯逻辑测试。
  - 公共能力处理：扩展现有 app controller；把手写三项切换改成数组/count 驱动的通用循环和三行可视窗口索引。
  - 代码注释要求：注释配置模式只存 request/runtime、不持久化；物理 Cancel 优先退出 portal 的语义。
  - 完成标准：新增 `WifiConfigConfirm`、`WifiConfigPortal`、`WifiPolicyEdit`；五项菜单循环正确；Confirm/Cancel 保存或放弃 `OFF/AUTO` 编辑；重启不自动请求配置模式。
  - 自动化验证：host 测试覆盖五项正反向循环、窗口边界和 policy edit；`pio run` 验证状态机 switch 完整。
  - 人工验证关注点：旋钮逐项导航、确认页防误触、Cancel 返回层级、现有亮度/时间/夜间息屏入口仍可达。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已新增五项 `SettingMenuItem`、三行窗口纯逻辑、`WifiConfigConfirm` / `WifiConfigPortal` / `WifiPolicyEdit` 状态，以及非持久化 `configModeRequested`。控制器只允许物理 Confirm 发起配置模式，Portal 状态下物理 Cancel 清除请求并优先返回菜单；WiFi 策略使用编辑草稿，NVS blob 保存成功后才更新运行态，Cancel 放弃草稿。新增宿主测试覆盖五项正反向及多步循环、窗口边界和 `OFF/AUTO` 编辑；既有网络/持久化与 QR 测试继续成功，`pio run` 成功（RAM 12.1%、Flash 59.0%）。OLED 新状态页面及真机导航按 task-11 集成后验证。

- [x] task-05: [WiFi Service] 实现 AP/STA demand 和非阻塞模式协调核心

  - 追踪需求：R-01..R-07、R-09..R-16、R-37、NFR-01、NFR-05。
  - 依赖任务：task-01、task-02。
  - 修改范围：新增 `src/wifi_service.h/.cpp`；按需调整公开 network runtime 类型。
  - 公共能力处理：新建统一无线生命周期服务；替代 `main.cpp` 中分散的 `disableRadios()` WiFi 操作，后续 portal/scan/test 只发命令。
  - 代码注释要求：注释模式切换顺序、AP 保持优先、普通 AUTO consumer 在 `OFF` 下拒绝以及 consumer bitmask 幂等性。
  - 完成标准：service 初始化为 `WIFI_OFF`；可协调 Off/Ap/Sta/ApSta；开放 AP 使用 `FocusClock-xxxx` 和集中 IP；STA demand 释放后关闭无线；公开 runtime view 只在变化时更新。
  - 自动化验证：复用 task-01 模式矩阵测试，并增加 service transition helper 的 host 测试；`pio run` 验证 Arduino WiFi API。
  - 人工验证关注点：串口观察模式切换，确认配置 AP 不设密码、地址正确、STA 失败不关闭 AP、无线结束后确实回到 OFF。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已新增统一 `wifi_service`，集中管理无线模式、开放 SoftAP、STA 尝试/重连、AP 客户端采样和公开运行视图；普通 AUTO consumer 在 `OFF` 策略下被拒绝，位掩码登记/释放保持幂等。AP SSID 由 eFuse MAC 低 16 位生成，IP、信道、URL 和重试参数均来自集中配置。纯迁移计划测试覆盖 `OFF/AP/STA/AP_STA`、AP 保持和接口释放，网络与设置宿主测试成功，`pio run` 成功（RAM 12.1%、Flash 59.0%；服务尚未在 task-12 接入主循环，因此链接器会移除未引用实现）。SoftAP、STA 和日志行为仍需真机集成验证。

- [x] task-06: [WiFi 扫描] 实现异步扫描、结果归一化和生命周期清理

  - 追踪需求：R-25、R-30..R-32、AC-04、AC-09、NFR-01。
  - 依赖任务：task-05。
  - 修改范围：`src/wifi_service.h/.cpp`；新增 scan normalize 纯逻辑及 host 测试。
  - 公共能力处理：扩展 WiFi service，复用框架 `scanNetworks(true)` / `scanComplete()`；不新建线程或 async server。
  - 代码注释要求：注释驱动结果所有权、`scanDelete()` 时机、隐藏 SSID过滤和同名最强 RSSI 去重。
  - 完成标准：扫描 start/status 非阻塞；最多 20 项、按 RSSI 排序；退出配置模式会清理扫描；扫描中物理 Cancel 不受影响。
  - 模式协调补充：start 先登记 `portalScanDemand`，等待 service 进入 `WIFI_AP_STA` 后调用框架异步扫描；完成、失败或 Cancel 时清理驱动结果并释放 demand，恢复 `WIFI_AP`。该 demand 可在持久策略 `OFF` 下使用，但只能存在于配置模式。
  - 自动化验证：host 测试覆盖去重、排序、上限、空/隐藏结果和失败状态；`pio run` 兜底。
  - 人工验证关注点：扫描期间连续操作旋钮/Cancel，观察 OLED、RTC和 Timer 是否继续更新；重复扫描无堆持续下降。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已扩展 WiFi service 的 `portalScanDemand`、异步扫描启动/轮询/取消和驱动结果清理；扫描前由 service 协调到 `WIFI_AP_STA`，完成或失败后释放 demand 并恢复 AP。新增固定容量 scan candidate/result 归一化，过滤隐藏 SSID、按同名最强 RSSI 去重并降序排列，最多 20 项。宿主扫描归一化、网络/设置逻辑测试和 `pio run` 均成功；扫描期间设备输入与 OLED/RTC/TIMER 主循环持续性仍需真机验证。

- [x] task-07: [连接测试] 实现只使用已保存配置的一次性 STA 测试

  - 追踪需求：R-11..R-16、R-41..R-44、AC-06、AC-07。
  - 依赖任务：task-02、task-05。
  - 修改范围：`src/wifi_service.h/.cpp`；新增连接测试状态机 host 测试。
  - 公共能力处理：扩展 service demand 模型；测试复用 STA 连接能力，不新增独立 WiFi 客户端实现。
  - 代码注释要求：注释测试在配置模式内绕过 `OFF` 的原因、结果在 STA 释放后保留以及 timeout 的溢出安全判断。
  - 完成标准：空 SSID拒绝；测试从已持久化配置启动；Connecting/Success/Failed/TimedOut 可查询；结束后回到 AP-only；结果不因页面断线丢失。
  - 自动化验证：host 测试用模拟状态覆盖成功、终端失败、超时、重复启动、取消和回绕；`pio run` 兜底。
  - 人工验证关注点：正确/错误密码、不存在 SSID、测试中物理 Cancel、AP 信道切换短断和结果恢复。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已在 WiFi service 中实现仅使用已保存配置的一次性连接测试：要求配置模式运行、非空 SSID、无扫描或既有测试；测试 demand 可在 `OFF` 策略下临时形成 `WIFI_AP_STA`。设备保留 Connecting/Succeeded/Failed/TimedOut 权威状态，终态立即释放 STA 并在下一轮恢复 AP-only，退出配置模式会取消并清空测试。纯状态测试覆盖等待、成功、终端失败、超时，既有 deadline 测试覆盖 `millis()` 回绕；网络/设置宿主测试及 `pio run` 成功。正确/错误凭据、AP 信道短断和刷新恢复仍需真机验证。

- [x] task-08: [HTTP 基础] 建立 portal 生命周期、AP 接口 guard 和 JSON 响应

  - 追踪需求：R-03..R-06、R-26、R-32..R-36、NFR-02、NFR-06。
  - 依赖任务：task-05。
  - 修改范围：新增 `src/wifi_portal.h/.cpp`、`src/wifi_portal_page.h` 初始资源、JSON writer/escape helper 及 host 测试。
  - 公共能力处理：复用内置 `WebServer`；新建 portal 内部 JSON writer，因为所有 API 和错误路由均需正确转义；不引入 ArduinoJson。
  - 代码注释要求：注释用 `server.client().localIP()` 判断到达接口的安全边界，以及 server 只能随 AP 生命周期启动/停止。
  - 完成标准：AP ready 后启动、Cancel 后停止；`/` 和 API skeleton 可访问；所有路由及 not-found 经 AP guard；STA 接口返回 403；body/content-type 有上限检查。
  - 自动化验证：host 测试 JSON 引号、反斜杠、控制字符和 UTF-8；`pio run`；硬件可达时 curl 检查 AP 200、STA 403、错误 envelope。
  - 人工验证关注点：反复开启/关闭 portal，无残留 server；从 AP 地址可访问，从 STA 地址不能访问。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已新增同步 WebServer Portal 基础和 Flash 内初始页面；AP ready 后启动、请求取消或 AP 停止后关闭，每轮 update 只调用一次 `handleClient()`。根路由、API skeleton 和 not-found 均统一检查 `client().localIP() == WiFi.softAPIP()`，POST 额外限制 form Content-Type 与 1024 字节 body。新增固定缓冲 JSON writer 和一致错误 envelope，宿主测试覆盖引号、反斜杠、控制字符、UTF-8 与溢出；既有宿主测试和 `pio run` 成功。AP 200、STA 403 和反复启停仍需真机/curl 验证。

- [x] task-09: [配置 API] 实现配置读取、保存、清空和 RTC 显式写入

  - 追踪需求：R-17..R-21、R-27..R-29、R-33..R-35、R-45、AC-08、AC-10、AC-14。
  - 依赖任务：task-02、task-04、task-08。
  - 修改范围：`src/wifi_portal.cpp`、`src/persistence.*` 必要扩展、`src/rtc_service.*` 必要接口；host 校验测试。
  - 公共能力处理：复用 persistence、`rtcSetTime`、`rtcServiceForceRead` 和 task-01 candidate helper；只把多路由复用的字段校验抽取 helper。
  - 代码注释要求：注释全量校验后再应用、跨 NVS/RTC partial apply 的诚实响应、RTC 只有 `setRtc=1` 才写。
  - 完成标准：GET 不返回密码；form 保存符合空 SSID清除、空密码保留规则；clear 按钮接口清除凭据并保留 policy；非法请求零写入；RTC 无效时可由显式合法输入修复。
  - 自动化验证：host 测试字段范围、日期/闰年、candidate 和 error mapping；`pio run`；硬件 curl 验证 GET、保存、清空、非法请求和密码不泄漏。
  - 人工验证关注点：页面重新读取实际配置；NVS/RTC 失败显示错误；清空后重启仍为空；修改其他设置不误写 RTC。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已实现 GET 配置、全量 form 保存和需明确 `confirm=1` 的 WiFi 清空接口。GET 返回亮度、RTC、夜间息屏、策略、SSID、`passwordConfigured` 和网络运行状态，不返回密码。POST 在任何写入前严格解析必填字段、范围、枚举、SSID/密码及完整日期/闰年；随后依次应用亮度、夜间配置、NetworkConfig blob 和显式 `setRtc=1` 的 RTC，并用 `APPLY_PARTIAL` 逐 section 报告结果。清空只清除 SSID/密码并保留策略，同时清理连接测试状态。新增纯校验测试覆盖数字范围、非法字符、闰年、月份天数和 weekday；JSON/网络测试及 `pio run` 成功。NVS/RTC 真机失败、重启持久化和 curl 密码检查仍需硬件验证。

- [x] task-10: [Web 交互] 完成移动配置页、异步扫描和可恢复连接测试流程

  - 追踪需求：R-26..R-35、R-41..R-45、AC-02、AC-06、AC-09、AC-10。
  - 依赖任务：task-06、task-07、task-08、task-09。
  - 修改范围：`src/wifi_portal_page.h`、`src/wifi_portal.cpp` 的 scan/test 路由；必要的静态页面提取/检查脚本。
  - 公共能力处理：复用现有 portal envelope 和 service scan/test；页面原生 HTML/CSS/JS，不引入前端框架/CDN。
  - 代码注释要求：仅对测试断线重试、device-authoritative status 和 `beforeunload` 尽力语义添加简短注释。
  - 完成标准：页面编辑全部配置；扫描 start/poll；WiFi 字段 dirty 时禁用测试；保存后测试；测试警告、操作锁定、断线重试和刷新恢复；清空需确认并禁用测试。
  - 自动化验证：提取内嵌 JS 后执行语法检查；host/curl 状态序列验证 running/complete/failed；`pio run`。页面视觉和实际断线仍需人工验证。
  - 人工验证关注点：手机窄屏无溢出/重叠；测试时不能重复操作；AP 不可用提示清楚；刷新/重新连接后恢复最终状态。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已完成无外部资源的移动单列页面，覆盖亮度、RTC/浏览器时间、夜间息屏、策略、SSID/密码、保存、确认清空、扫描和连接测试。页面跟踪 WiFi dirty 状态，测试中锁定保存/清空/扫描/重复测试并启用 `beforeunload`；测试 GET 使用设备权威状态，fetch 失败按 750ms 至 3s 退避重试，刷新后可从 config runtime 恢复。设备端 scan/test 路由返回固定容量 JSON，且测试中保存/清空/扫描同样返回 409。内嵌 JS 语法检查、portal 校验/JSON 宿主测试和 `pio run` 成功。`browser-act` 当前仅配置需显式授权的 chrome-direct，因此未控制本机 Chrome；手机窄屏、AP 短断提示和真实刷新恢复仍需人工验证。

- [x] task-11: [OLED UI] 完成滚动菜单、两阶段配置页、QR 和 WiFi 图标

  - 追踪需求：R-08、R-16、R-22..R-25、R-38..R-40、AC-02、AC-12。
  - 依赖任务：task-03、task-04、task-05。
  - 修改范围：`src/ui_render.cpp`、`src/display.*` 必要协调、AppState runtime view 渲染接入。
  - 公共能力处理：复用 task-03 bitmap API和 task-04 菜单窗口；不为单页另建第二套 display 驱动。
  - 代码注释要求：注释 QR 保留区域、最后客户端断开回到 SSID 页和底栏图标预留区域。
  - 完成标准：五项菜单始终可见导航；无客户端显示 SSID，连接后自动显示 URL QR，无 Confirm；Cancel 提示持续；AUTO task 连接成功显示图标，结束后清除。
  - 自动化验证：host 测试菜单窗口/显示状态选择；`pio run`；bitmap byte 测试继续通过。
  - 人工验证关注点：所有页面切换无残影；最长 SSID/URL不裁剪；二维码真机可扫；图标不覆盖底栏文字。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已将 SETTING 菜单改为五项 label 数组和三行滚动窗口；新增 WiFi 配置确认、AP 启动、连接手机、URL+40x40 QR 和策略编辑页面。无客户端显示开放 AP SSID，客户端接入后自动显示集中 URL 与 QR，断开后整行重绘可清除 bitmap；物理 Cancel 提示常驻。CLOCK/TIMER 仅在网络任务活动且 STA 已连接时绘制 8x8 图标，SETTING 显式清除。纯阶段/窗口测试、QR bitmap 测试和 `pio run` 成功（RAM 12.2%、Flash 59.7%）。真机残影、最长文本、图标显隐和手机扫码仍需人工验证。

- [x] task-12: [系统集成] 接入 main loop、睡眠门禁和完整生命周期

  - 追踪需求：R-01..R-16、R-25、R-31、R-36、NFR-01、NFR-03、NFR-05、AC-03..AC-07。
  - 依赖任务：task-04、task-05、task-06、task-07、task-08、task-09、task-10、task-11。
  - 修改范围：`src/main.cpp`、`src/sleep_manager.cpp` 及必要头文件签名；移除 main 中重复 WiFi 关闭所有权。
  - 公共能力处理：复用 service/portal/runtime view；扩展现有集中 sleep 条件，不新建通用 blocker 系统。
  - 代码注释要求：在主循环插入点说明 service 先于 portal、portal 先于 render/sleep 的业务原因；注释无线活动 sleep gate。
  - 完成标准：启动默认无线关闭；配置模式 AP/HTTP 全生命周期正确；测试/扫描不阻塞；配置模式或 AUTO task 禁止 Light Sleep；结束后恢复；物理 Cancel 可从任何 portal 子状态退出。
  - 自动化验证：运行全部 host 测试和 `pio run`；检查编译 warning；用串口时间戳验证主循环在扫描/测试期间持续推进。
  - 人工验证关注点：CLOCK/TIMER/SETTING、夜间息屏、RTC、输入唤醒无回归；80MHz 下无持续卡顿、看门狗重启或明显 heap 泄漏。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已由 `wifiServiceBegin()` 接管启动无线关闭，加载持久 NetworkConfig 后初始化 service/portal；主循环按输入与周期状态、WiFi service、Portal、render、display power、sleep 顺序协作推进。物理 Cancel 清除配置模式请求后，同轮 service 取消 scan/test、关闭 AP/STA，Portal 停止 server 并返回 SETTING。Light Sleep 门禁新增配置模式和网络任务条件，Portal 位于 SETTING 时现有显示电源策略保持 OLED 唤醒。全部五组宿主测试、内嵌 JS 语法、`git diff --check` 和完整 `pio run` 成功（RAM 13.0%、Flash 66.1%）。硬件串口时序、Light Sleep、80MHz 响应、heap 和既有功能回归仍需真机验证。

- [x] task-13: [验收与文档] 执行综合验证并更新当前功能说明

  - 追踪需求：AC-01..AC-14、NFR-03、NFR-04、NFR-07。
  - 依赖任务：task-12。
  - 修改范围：补充/整理 host 测试与硬件验证记录；按实际实现更新 `README.md` 的功能、配置入口、安全限制和构建说明；不改概念来源文档。
  - 公共能力处理：不新增；汇总前述验证并清理仅用于调试的临时代码，保留受 `ENABLE_SERIAL_LOGGING` 控制的必要诊断。
  - 代码注释要求：无新增业务代码；若修复验收发现的问题，按对应任务约束补充必要注释并记录回退阶段。
  - 完成标准：所有自动化命令成功；README 与实际行为一致；硬件检查项逐项记录结果和未验证项；密码未出现在日志/API；工作树无无关改动。
  - 自动化验证：一次性运行全部 host 测试、内嵌 JS 语法检查、HTTP 脚本（硬件可达时）和 `/Users/naaran/.platformio/penv/bin/pio run`。
  - 人工验证关注点：按 design 的 13 个硬件场景执行，重点记录 AP_STA 短断恢复、手机扫码、STA 侧 403、Light Sleep 门禁和 80MHz/heap 表现。必须由用户确认结果。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已更新 README 的当前功能、SETTING/WiFi 入口、配置 Portal、`OFF/AUTO` 语义、安全限制、功耗门禁、构建和模块说明。最终批次成功运行网络/持久化、SETTING、QR、JSON、Portal 校验五组宿主测试、内嵌 JS 语法检查、`git diff --check` 和完整 `pio run`；最终构建 RAM 13.0%、Flash 66.1%。源码扫描确认 `WiFi.mode/begin/disconnect/softAP/scanNetworks` 只由 `wifi_service.cpp` 调用，API 与默认日志没有输出明文 STA 密码。现有用户 docs 变更和未跟踪 `.DS_Store` 未修改。
  - 硬件验收记录（未执行）：开放 AP/SSID/IP、无客户端与客户端两阶段 OLED、手机扫码、Portal 无超时和物理 Cancel、异步扫描交互、正确/错误凭据测试、AP_STA 短断恢复、清空后重启、`OFF/AUTO` 重启、STA 侧 403、Light Sleep 恢复、CLOCK/TIMER/SETTING/夜间息屏/RTC 回归、80MHz 响应/heap/看门狗均等待用户真机确认。因此当前仅完成实现和自动化检查，不声明硬件验证通过或生产可用。

- [x] task-14: [OLED 简化] 移除 QR 并改为完整文本网址

  - 追踪需求：R-08、R-38..R-40、AC-02、AC-12。
  - 依赖任务：task-13。
  - 修改范围：`src/ui_render.cpp`，删除 `src/qr_render.h/.cpp` 和 `tests/host/test_qr_render.cpp`；README 必要同步。
  - 公共能力处理：保留 display page bitmap API 供 WiFi 图标复用；删除不再有调用方的 QR 专用能力。
  - 代码注释要求：仅在分行网址组合不直观处说明完整 URL 来源；删除旧 QR 区域注释。
  - 完成标准：客户端接入后 OLED 清晰显示协议、`192.168.4.1/` 和 Cancel；不生成 QR，页面切换可覆盖旧 QR 区域。
  - 自动化验证：设置阶段宿主测试继续成功；`rg` 确认生产代码无 QR 引用；`pio run`。
  - 人工验证关注点：真机确认完整网址可读、接入/断开页面无旧 QR 残影、Cancel 不被裁剪。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已删除 `qr_render.*` 和对应宿主测试，客户端接入后的 OLED 页面从统一 `portalUrl` 解析并分行显示协议与地址，保留 `OPEN IN BROWSER` 和物理 Cancel 提示；通用 page bitmap API 继续供 WiFi 图标使用。`rg` 确认生产代码/测试无 QR renderer 引用，设置逻辑宿主测试和 `pio run` 成功（RAM 12.9%、Flash 65.4%）。完整网址可读性和旧 QR 残影仍需真机确认。

- [x] task-15: [页面资源] 独立 HTML 并将亮度改为五档选择框

  - 追踪需求：R-26、R-27、R-27A、NFR-06、AC-10。
  - 依赖任务：task-13。
  - 修改范围：新增 `web/wifi_portal.html`，更新 `platformio.ini`、`src/wifi_portal.cpp`，删除 `src/wifi_portal_page.h`，更新 README。
  - 公共能力处理：复用 PlatformIO `board_build.embed_txtfiles` 和链接器 symbol，不新增生成脚本、运行时文件系统或依赖。
  - 代码注释要求：注释链接器 start/end symbol 的来源和发送长度；HTML 内只保留必要的断线恢复逻辑注释。
  - 完成标准：前端工程师只编辑独立 HTML；固件根路由返回该资源；亮度 select 明确展示 1 最暗至 5 最亮，保存字段仍为 `brightness=1..5`。
  - 自动化验证：从独立 HTML 提取 JS 做 Node 语法检查；检查五个 option 标记；`pio run` 验证 embed symbol 链接；页面实际渲染仍需人工确认。
  - 人工验证关注点：手机确认亮度当前值可见、五档标签清楚、选择保存后重新读取一致，页面布局无回归。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已将页面主体从 `src/wifi_portal_page.h` 移动为独立 `web/wifi_portal.html`，使用 `board_build.embed_txtfiles` 直接嵌入 Flash；Portal 通过 `_binary_web_wifi_portal_html_start/end` 和去除末尾 NUL 后的明确长度返回页面，无生成头文件或新增依赖。亮度改为 select，提供 1 Darkest、2 Dark、3 Medium、4 Bright、5 Brightest 五个可见档位，保存字段仍为 `brightness=1..5`。独立 HTML JS 语法、五 option 检查和 embed symbol 完整 `pio run` 成功（RAM 12.9%、Flash 65.4%）。手机实际布局、档位标签和保存回读仍需人工确认。

- [x] task-16: [变更验收] 更新文档并重跑完整自动化

  - 追踪需求：R-08、R-26、R-27A、R-38..R-40、AC-02、AC-10、AC-12、AC-13。
  - 依赖任务：task-14、task-15。
  - 修改范围：README、规格执行记录和 changelog；必要的测试调整。
  - 公共能力处理：不新增。
  - 代码注释要求：无特殊要求。
  - 完成标准：文档不再说明 QR 或页面头文件；全部自动化命令成功；真机未验证项明确记录。
  - 自动化验证：全部现存 host 测试、独立 HTML JS 语法检查、`git diff --check` 和 `pio run`。
  - 人工验证关注点：OLED 文本网址与页面亮度 select 由用户确认。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：README 已改为文本网址、独立 `web/wifi_portal.html`、PlatformIO embed 和五档亮度 select 的当前说明。最终批次成功运行网络/持久化、SETTING、JSON、Portal 校验四组宿主测试，独立 HTML JavaScript 语法与五个亮度 option 标签检查，过时 QR/页面头文件/亮度 range 引用扫描，`git diff --check` 和完整 `pio run`；最终 RAM 12.9%、Flash 65.4%。OLED 文本清晰度、旧 QR 残影、手机页面布局及亮度保存回读仍需用户真机确认，不声明这些人工项已验证。

- [x] task-17: [配置入口] 删除二次确认并直接启动 Portal

  - 追踪需求：R-02、R-24、R-25、AC-02。
  - 依赖任务：task-16。
  - 修改范围：`src/app_state.h`、`src/app_controller.cpp`、`src/ui_render.cpp`；设置逻辑宿主测试必要调整。
  - 公共能力处理：简化现有状态机，不新增能力；删除无调用方的 `WifiConfigConfirm` 状态和 renderer。
  - 代码注释要求：保留配置模式 request 不持久化、Portal Cancel 优先退出的业务注释；删除二次确认注释。
  - 完成标准：SETTING 菜单选中 `WIFI CONFIG` 后一次 Confirm 立即设置 request 并显示 AP 启动/连接页；不存在 `START OPEN AP?` 文本或第二次 Confirm。
  - 自动化验证：源码扫描确认无 `WifiConfigConfirm` / `START OPEN AP`；设置宿主测试和 `pio run`。
  - 人工验证关注点：真机一次 Confirm 进入 `STARTING AP` 或 `CONNECT PHONE`，Cancel 正常退出并关闭 AP。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已删除 `WifiConfigConfirm` 枚举及 controller/renderer 分支；SETTING 菜单中的 `WIFI CONFIG` Confirm 现在同一输入处理中设置非持久化 `configModeRequested=true` 并进入 `WifiConfigPortal`，直接显示 AP 启动或手机连接页面。Portal 物理 Cancel 清除 request 和返回菜单的优先逻辑保持不变。源码扫描确认无 `WifiConfigConfirm` / `START OPEN AP`，设置宿主测试和 `pio run` 成功（RAM 12.9%、Flash 65.4%）。一次 Confirm 与 Cancel 的真机流程仍需用户确认。

- [x] task-18: [前端维护] 格式化独立 HTML/CSS/JavaScript

  - 追踪需求：R-26、NFR-06。
  - 依赖任务：task-16。
  - 修改范围：`web/wifi_portal.html`。
  - 公共能力处理：不新增构建器；保持 PlatformIO 原文件 embed。
  - 代码注释要求：只在 AP 断线重试和 `beforeunload` 尽力语义处保留必要注释。
  - 完成标准：HTML 有清晰层级，CSS 与 JS 按规则/函数分行，无大段单行压缩代码；DOM id、API 路径和行为保持一致。
  - 自动化验证：独立 HTML JS 语法、五档亮度 option、必要 DOM/API token 检查和 `pio run`。
  - 人工验证关注点：页面功能和手机布局无回归；前端工程师可直接阅读修改。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：已按常规结构重排 `web/wifi_portal.html`：HTML 层级使用两空格缩进，CSS 每个选择器/声明分行，JavaScript 按状态、工具函数、加载、保存、扫描和连接测试分段；只在 AP 短断重试与 `beforeunload` 尽力语义处保留注释。DOM id、API 路径、字段及交互保持一致。JS 语法、五档 option、全部必要 DOM/API token、无超过 120 字符行、设置宿主测试和 `pio run` 成功（RAM 12.9%、Flash 65.9%）。手机布局与交互无回归仍需人工确认。

- [x] task-19: [变更验收] 更新记录并重跑完整自动化

  - 追踪需求：R-24、R-26、AC-02、AC-10、AC-13。
  - 依赖任务：task-17、task-18。
  - 修改范围：README、规格执行记录、changelog；必要测试调整。
  - 公共能力处理：不新增。
  - 代码注释要求：无特殊要求。
  - 完成标准：当前文档说明单次 Confirm 直接启动；全部自动化命令成功；人工项明确记录。
  - 自动化验证：全部 host 测试、HTML JS/token 检查、过时状态/文本扫描、`git diff --check` 和 `pio run`。
  - 人工验证关注点：单次 Confirm 流程与格式化页面由用户真机确认。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：README 已同步为菜单一次 Confirm 立即启动 Portal。最终批次成功运行网络/持久化、SETTING、JSON、Portal 校验四组宿主测试，格式化 HTML 的 JavaScript 语法/API token/行长检查，`WifiConfigConfirm` / `START OPEN AP` / 旧 README 文本扫描，`git diff --check` 和完整 `pio run`；最终 RAM 12.9%、Flash 65.9%。一次 Confirm、Cancel 关闭 AP 和手机页面功能/布局仍需用户真机确认，不声明人工项已验证。

- [x] task-20: [WiFi Service] 分离 STA 接口与连接 demand

  - 追踪需求：R-13、R-30、R-31、AC-05、AC-09。
  - 依赖任务：task-19。
  - 修改范围：`src/network_types.h`、`src/wifi_logic.*`、`src/wifi_service.cpp`、`tests/host/test_wifi_logic.cpp`。
  - 公共能力处理：扩展现有纯模式逻辑，明确区分无线接口需求和 STA 连接需求；不新建 service。
  - 代码注释要求：在扫描 demand 不允许连接已保存 AP 的判断处说明业务意图。
  - 完成标准：Portal 扫描可协调到 `AP_STA` 但不调用 `WiFi.begin()`；AUTO/test 仍可正常连接；容量满时保留最强 20 项。
  - 自动化验证：宿主测试覆盖 scan-only 接口 demand、AUTO/test 连接 demand，以及容量满后更强新 SSID 替换最弱项。
  - 人工验证关注点：有已保存凭据时扫描不应因连接外部 AP 导致 Portal 信道切换或断线。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：模式迁移计划已移除 `startSta`，新增独立 `wifiStaConnectionNeededFor()` 只让 AUTO consumer 和连接测试建立 STA 连接；扫描仅提升接口模式。扫描归一化在容量满时会用更强新 SSID 替换最弱项。严格告警宿主网络测试成功；真机扫描信道行为待人工确认。

- [x] task-21: [HTTP/cleanup] 有界读取 POST 并清理无用接口

  - 追踪需求：R-35、NFR-01、NFR-06、AC-08。
  - 依赖任务：task-20。
  - 修改范围：`src/wifi_portal.*`、`src/config_network.h`、`src/wifi_service.*`、`tests/host/test_portal_validation.cpp`。
  - 公共能力处理：复用 `WebServer` raw callback 和现有 form parser；只新增固定容量 body 收集状态，不引入 HTTP/JSON 依赖。
  - 代码注释要求：说明 raw callback 用于在 WebServer 默认表单分配前限制预期 body。
  - 完成标准：URL-encoded POST 在 1024-byte 固定上限内收取，超限返回 413；删除无用常量、单行赋值封装和无调用方的 AP SSID getter。
  - 自动化验证：body 收集边界宿主测试、Portal 现有校验测试和 `pio run`。
  - 人工验证关注点：正常保存/扫描/测试 POST 无回归；恶意 multipart 仍是框架残余风险。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：四个 POST route 已通过 `WebServer` raw callback 分块收入固定 1024-byte 缓冲，合法 body 才复用框架 form parser，超限 body 只排空并返回 413。已删除未使用扫描保留常量、单行凭据赋值封装、无调用方 AP SSID getter 和预声明 NTP consumer；保留空 consumer 类型作为未来业务扩展点。body 边界宿主测试与完整构建成功，RAM 13.3%、Flash 65.9%；恶意 multipart 仍为已记录的框架残余风险。

- [x] task-22: [修正验收] 同步规格并重跑完整验证

  - 追踪需求：R-30、R-31、R-35、AC-08、AC-09、AC-13。
  - 依赖任务：task-20、task-21。
  - 修改范围：本 spec 当前描述、README（如需）和 changelog。
  - 公共能力处理：不新增。
  - 代码注释要求：无特殊要求。
  - 完成标准：当前规格不再把 QR、未使用常量或扫描建立 STA 连接当作现行设计；自动化全部成功。
  - 自动化验证：四组 host 测试、HTML JavaScript/token/行长检查、过时引用扫描、`git diff --check` 和 `pio run`。
  - 人工验证关注点：扫描无外部 STA 连接与 Portal POST 回归仍需真机确认。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：需求、设计、项目上下文和 README 已同步最终扫描/文本 URL/POST 资源边界，历史 task/changelog 仍保留 QR 方案演进记录。四组严格告警 host 测试、HTML JavaScript/option/API token/行长检查、过时引用扫描、`git diff --check` 和完整 PlatformIO 构建均成功；最终 RAM 13.3%、Flash 65.9%。扫描无信道切换、正常 POST 和超限 POST 真机回归待用户确认。

- [x] task-23: [HTTP 性能] 恢复原生 form parser 并移除 raw 超时路径

  - 追踪需求：R-35、NFR-01、NFR-06、AC-03、AC-08。
  - 依赖任务：task-22。
  - 修改范围：`src/wifi_portal.*`、`src/portal_validation.*`、`tests/host/test_portal_validation.cpp`。
  - 公共能力处理：复用 `WebServer` 原生 URL-encoded parser；删除不再需要的 server 子类、raw body 状态和分块 helper。
  - 代码注释要求：无特殊要求；删除已失效的 raw 解析注释。
  - 完成标准：四个 POST route 不再登记 raw callback；保存路径不再等待 1436-byte raw 读取超时；handler 仍对超过 1024 字节的请求返回 413。
  - 自动化验证：Portal 校验宿主测试；源码扫描确认无 `RAW_*`/raw callback/body collector；`pio run` 验证路由和参数 API 编译。
  - 人工验证关注点：手机页面 Save、Clear、Scan 和 Test 的 POST 应恢复即时响应；超限请求和恶意大 body 仍需真机/curl 验证。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：四个 POST route 已恢复为单 handler 注册，由 `WebServer` 原生 URL-encoded parser 生成参数；已删除 raw callback、server 子类、1KB 常驻 body 缓冲、分块 helper 和对应测试。handler 仍在配置校验/写入前检查 1024-byte 上限并返回 413。Portal 校验宿主测试、过时路径源码扫描和 PlatformIO 构建成功，RAM 回到 12.9%、Flash 65.9%；真机 POST 延迟待用户确认。

- [x] task-24: [延迟修正验收] 同步资源风险说明并重跑自动化

  - 追踪需求：R-35、NFR-01、AC-03、AC-08、AC-13。
  - 依赖任务：task-23。
  - 修改范围：README、本 spec 执行记录和 changelog。
  - 公共能力处理：不新增。
  - 代码注释要求：无特殊要求。
  - 完成标准：文档不再声明严格解析前限流；自动化全部成功；真机延迟项保留为用户确认。
  - 自动化验证：四组 host 测试、HTML JavaScript/token/行长检查、raw 路径过时引用扫描、`git diff --check` 和 `pio run`。
  - 人工验证关注点：手机页面的 Save 耗时由用户确认恢复。
  - 待确认问题：无。
  - 执行记录（2026-08-08）：README、requirements 和 design 已明确 1024-byte 检查是 handler 拒绝/零写入边界，不是框架解析前内存上限；同步 parser 的恶意大 body 压力作为残余风险。四组严格告警 host 测试、HTML JavaScript/option/API token/行长检查、生产码 raw 路径扫描、`git diff --check` 和完整 PlatformIO 构建均成功；最终 RAM 12.9%、Flash 65.9%。Save 延迟真机回归待用户确认。

## 待确认问题

无。
