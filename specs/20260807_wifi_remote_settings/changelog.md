# 变更记录：WiFi 远程设置

日期：2026-08-07
Feature：wifi_remote_settings

- 2026-08-07：Phase 1 需求规格已确认。确定 `OFF/AUTO` 两态策略、开放 SoftAP、两阶段 OLED URL QR、异步扫描、仅 AP 侧访问、保存后一次性连接测试、RTC 写入边界及 WiFi 凭据保存/清空规则。
- 2026-08-07：Phase 2 技术设计已确认。确定 AP/STA demand 模型、非阻塞 WiFi service、可恢复连接测试、SoftAP 接口 guard、版本化网络配置 blob、无新增依赖的 HTTP/QR 方案及 80MHz 资源边界。
- 2026-08-07：Phase 3 实现任务已确认。拆分为网络模型、持久化、显示底层、应用状态、WiFi service、扫描、连接测试、HTTP、Web/OLED 交互、系统集成及验收文档共 13 项任务。
- 2026-08-07：完成 task-01 网络模型。新增 `WifiPolicy`、`WifiTargetMode`、`NetworkConfig`、集中网络常量及纯逻辑 helper；宿主测试覆盖模式、凭据边界/candidate 语义和 `millis()` 回绕，PlatformIO 构建成功。硬件与日志检查留待后续集成任务。
- 2026-08-07：完成 task-02 网络配置持久化。新增固定布局 V1 `netCfg` blob、严格解码回退和单次 NVS blob 写入；宿主 codec 测试及 PlatformIO 构建成功，真机重启与日志检查尚未执行。
- 2026-08-07：完成 task-03 显示底层。新增 page bitmap 区域写入/清零、8x8 WiFi 图标和 40x40 QR page buffer renderer；宿主映射/安静区/边界测试、PlatformIO 构建及 SDK QR 符号链接检查成功，真机扫码与残影检查尚未执行。
- 2026-08-08：完成 task-04 应用状态。新增五项 SETTING 菜单与三行窗口纯逻辑、WiFi 配置确认/Portal/策略编辑状态及非持久化配置模式请求；物理 Cancel 可优先清除 Portal 请求，策略只在 NVS 保存成功后提交。新增设置逻辑宿主测试，既有宿主测试及 PlatformIO 构建成功；OLED 与真机交互留待后续集成验证。
- 2026-08-08：完成 task-05 WiFi Service 核心。新增统一 AP/STA 生命周期、AUTO consumer demand、模式迁移、STA 非阻塞重连、开放 AP 配置和变化驱动运行视图；宿主测试覆盖四种模式迁移及 AP 保持，PlatformIO 构建成功。硬件模式切换与无线关闭效果留待系统集成验证。
- 2026-08-08：执行 task-06 前发现 Arduino ESP32 异步扫描会隐式启用 STA，原设计缺少 scan demand，可能造成 service 状态与实际无线模式不一致。已将需求、设计和任务状态退回草稿，提议新增仅配置模式可用的 `portalScanDemand`：扫描前协调到 `WIFI_AP_STA`，结束/失败/Cancel 后恢复 `WIFI_AP`；等待确认后继续。
- 2026-08-08：需求、设计和 task-06 的 `portalScanDemand` 调整已确认。扫描可在持久策略 `OFF` 下临时协调到 `WIFI_AP_STA`，结束、失败或物理 Cancel 后释放 demand 并恢复 `WIFI_AP`；三份规格重新标记为已确认。
- 2026-08-08：完成 task-06 WiFi 扫描。新增异步扫描状态机、Portal scan demand 和固定容量结果归一化/清理；宿主测试覆盖隐藏 SSID、同名最强 RSSI、排序和上限，PlatformIO 构建成功。硬件扫描响应与重复扫描资源表现留待集成验证。
- 2026-08-08：完成 task-07 一次性连接测试。测试仅使用已保存凭据，在配置模式内临时申请 STA，保留设备端终态并在完成/取消后释放 demand；宿主测试覆盖连接、失败、超时与回绕 deadline，PlatformIO 构建成功。真实 AP 信道切换和凭据结果留待硬件验证。
- 2026-08-08：完成 task-08 HTTP 基础。新增 Portal 生命周期、全路由 SoftAP interface guard、POST 类型/长度限制、Flash 页面 skeleton 和固定缓冲 JSON writer；转义宿主测试及 PlatformIO 构建成功。AP/STA 实际访问边界留待硬件 curl 验证。
- 2026-08-08：完成 task-09 配置 API。实现无密码泄漏的配置读取、全量校验后分 section 保存、显式 RTC 修复和保留策略的凭据清空；宿主测试覆盖数字、日期/闰年与 weekday，PlatformIO 构建成功。NVS/RTC 与 curl 行为留待真机验证。
- 2026-08-08：完成 task-10 Web 交互。移动端内嵌页面支持完整配置、异步扫描、dirty 门禁、可恢复连接测试、操作锁定、退避重试和确认清空；设备路由同步执行冲突门禁。JS 语法、宿主测试和 PlatformIO 构建成功，实际手机布局与 AP 短断恢复待人工验证。
- 2026-08-08：完成 task-11 OLED UI。实现五项三行滚动菜单、配置确认/启动/SSID/URL QR 阶段、策略编辑和主页面 WiFi 图标；菜单/阶段与 QR 宿主测试及 PlatformIO 构建成功。OLED 残影、图标和手机扫码待真机验证。
- 2026-08-08：完成 task-12 系统集成。启动与主循环接入 WiFi service/Portal/持久配置，移除 main 中重复 WiFi 所有权，并为配置模式/网络任务增加 Light Sleep 门禁；全部宿主测试、JS 语法、diff 检查和完整 PlatformIO 构建成功。硬件生命周期、功耗、性能与回归仍待人工验证。
- 2026-08-08：完成 task-13 自动化验收与文档。README 已与 WiFi 配置门户实际实现同步；五组宿主测试、内嵌 JS 语法、diff 检查和完整 PlatformIO 构建成功，最终 RAM 13.0%、Flash 66.1%。13 类硬件场景全部记录为未执行，等待用户真机确认，不声明生产可用。
- 2026-08-08：收到真机反馈，功能基本正常，但 QR 太小难扫描、页面头文件不利于前端维护、亮度滑杆不可见数值。需求/设计/任务退回草稿，提议删除 OLED QR 与专用 renderer，改为完整文本 URL；页面迁移到 `web/wifi_portal.html` 并由 PlatformIO embed；亮度改为带 1 最暗至 5 最亮标记的 select。等待确认后执行 task-14..task-16。
- 2026-08-08：OLED 文本网址、独立 HTML embed 和五档亮度 select 的变更方案已确认；需求、设计和任务恢复为已确认，开始执行 task-14..task-16。
- 2026-08-08：完成 task-14 OLED 简化。删除 QR renderer/测试，客户端接入后改为从统一 URL 分行显示协议和地址；设置宿主测试与 PlatformIO 构建成功，Flash 降至 65.4%。真机文本与残影待确认。
- 2026-08-08：完成 task-15 页面资源调整。页面迁移为独立 `web/wifi_portal.html` 并通过 PlatformIO embed_txtfiles 链接，删除页面头文件；亮度改为带最暗/最亮标记的五档 select。HTML/JS、option 检查和完整构建成功，手机实际效果待确认。
- 2026-08-08：完成 task-16 变更验收。README 已同步文本网址、独立 HTML embed 与五档亮度 select；四组宿主测试、HTML/JS/option 检查、过时引用扫描、diff 检查和 PlatformIO 构建成功，最终 RAM 12.9%、Flash 65.4%。OLED 和手机实际效果待用户确认。
- 2026-08-08：收到后续反馈：`WIFI CONFIG` 不需要二次 `START OPEN AP?` 确认，且独立 HTML 仍因大量单行压缩代码难维护。需求/设计/任务退回草稿，提议一次菜单 Confirm 直接请求 Portal，并将 HTML/CSS/JS 按常规多行格式整理；等待确认后执行 task-17..task-19。
- 2026-08-08：单次 Confirm 直接启动 Portal 和独立 HTML 多行格式化方案已确认；需求、设计和任务恢复为已确认，开始执行 task-17..task-19。
- 2026-08-08：完成 task-17 配置入口简化。删除二次确认状态和页面，SETTING 菜单一次 Confirm 直接请求 Portal；过时状态/文本扫描、设置宿主测试和 PlatformIO 构建成功。真机入口与 Cancel 待确认。
- 2026-08-08：完成 task-18 前端维护调整。独立 HTML/CSS/JavaScript 已按常规多行结构和两空格缩进整理，行为 token、JS 语法、行长、设置测试及 embed 构建成功；格式化后 Flash 65.9%。手机页面回归待确认。
- 2026-08-08：完成 task-19 变更验收。README 已同步单次 Confirm 行为；四组宿主测试、格式化 HTML 检查、过时确认状态扫描、diff 检查和 PlatformIO 构建成功，最终 RAM 12.9%、Flash 65.9%。入口和页面真机回归待用户确认。
- 2026-08-08：代码与 spec 审查发现扫描 demand 意外触发 `WiFi.begin()`、满容量扫描未必保留最强项、POST body 上限在框架默认解析后才检查，以及当前规格存在过时 QR/类型/常量描述。用户已明确批准修复上述问题，需求、设计和 task-20..task-22 已同步为已确认执行基线。
- 2026-08-08：完成 task-20 WiFi Service 修正。无线接口模式与 STA 连接 demand 已分离，Portal 扫描不再触发已保存凭据连接；满容量扫描保留 RSSI 最强项。新增边界宿主测试成功，真机扫描无信道切换待确认。
- 2026-08-08：完成 task-21 HTTP 与出口清理。URL-encoded POST 改为固定上限 raw 收取后复用 form parser，避免预期请求在长度检查前分配等长 body；无用常量、封装、getter 和预声明 NTP consumer 已清理。边界宿主测试和 PlatformIO 构建成功，RAM 13.3%、Flash 65.9%；非预期 multipart 仍是同步框架残余风险。
- 2026-08-08：完成 task-22 修正验收。当前 spec 和 README 已同步接口/连接 demand 分离、最强 20 项扫描、固定上限 POST 和 multipart 残余风险。四组 host 测试、HTML 检查、过时引用扫描、diff 检查和 PlatformIO 构建成功，最终 RAM 13.3%、Flash 65.9%；新增真机回归项仍待用户确认。
- 2026-08-08：用户反馈页面 Save 从即时响应退化为接近 5 秒。检查确认 task-21 引入的 `WebServer` raw callback 固定读取 1436-byte chunk，对数百字节表单会等待剩余字节直到 client timeout。用户已批准恢复原生 form parser，保留 handler 1024-byte 拒绝检查并接受同步 parser 在 handler 前分配的已记录残余风险；task-23..task-24 作为已确认执行基线。
- 2026-08-08：完成 task-23 HTTP 延迟修正。四个 POST route 已移除 raw callback 并恢复框架原生 URL-encoded parser，固定 chunk 超时路径不再存在；handler 1024-byte 拒绝仍保留。Portal 测试、源码扫描和完整构建成功，RAM 回到 12.9%、Flash 65.9%；真机延迟待确认。
- 2026-08-08：完成 task-24 延迟修正验收。当前 spec 和 README 已区分 handler 超限拒绝与框架解析前内存风险；四组 host 测试、HTML 检查、生产码 raw 路径扫描、diff 检查和 PlatformIO 构建成功。Save 响应速度待用户真机回归。
