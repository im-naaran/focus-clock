# 任务拆解：最后成功对时状态展示

> 状态：已确认

日期：2026-08-10  
Feature：time_sync_status_display  
阶段：Phase 3

## 任务清单

- [x] task-01: [Setting Logic] 扩展六项菜单模型和导航测试
- [x] task-02: [OLED UI] 实现最后成功对时只读详情页并统一息屏计划文案
- [x] task-03: [Time Sync] 完整成功后触发 OLED 即时重绘
- [x] task-04: [Portal] 收敛展示文案并禁用旧页面缓存
- [x] task-05: [Verification] 更新说明并完成自动化与真机验收

## task-01: [Setting Logic] 扩展六项菜单模型和导航测试

**追踪需求：** R-01, R-07, NFR-01, NFR-04, AC-09

**依赖任务：** 无

**修改范围：** `src/setting_logic.h`、`tests/host/test_setting_logic.cpp`

**公共能力处理：** 扩展并复用。已检索 `settingMenuMove()`、`settingMenuWindowStart()` 和 `wrapIndex()`；现有实现已按菜单数量计算循环和窗口，无需新增导航抽象，只插入 `SettingMenuItem::TimeSync` 并将菜单数量改为 6。

**代码注释要求：** 按实现约束补充必要注释；枚举和直接测试无需叙述性注释。

**完成标准：**

- `SettingMenuItem::TimeSync` 位于 `TimeSet` 与 `NightScreenOff` 之间。
- `SETTING_MENU_ITEM_COUNT` 为 6，三行可见窗口常量不变。
- 正向和反向移动均能跨越首尾循环。
- 聚合多步移动保持步数语义。
- 六个菜单项对应的窗口起点均正确，选中项始终位于三行窗口内。
- 不改变 WiFi Policy 移动和 Portal phase 纯逻辑。

**自动化验证：**

- 测试目标：证明扩展后的菜单循环和三行窗口边界正确。
- 测试用例：覆盖 `Brightness -> TimeSet -> TimeSync`、`WifiPolicy -> Brightness`、反向首尾循环、跨多项移动，以及六项各自的窗口起点。
- 执行命令：`c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/test_setting_logic.cpp src/setting_logic.cpp -o /tmp/focus_clock_test_setting && /tmp/focus_clock_test_setting`。
- 验证局限：宿主测试不覆盖 OLED 标签绘制和物理旋钮手感，由 task-02/task-05 验证。

**人工验证关注点：** 入口为 OLED SETTING 菜单；正反向旋转并跨越首尾，预期六项顺序正确、当前项始终可见且三行窗口无跳动。异常状态重点检查一次产生多步旋钮事件时不会跳错窗口。

**待确认问题：** 无

**执行记录（2026-08-10）：** 已在 `TimeSet` 与 `NightScreenOff` 之间加入 `TimeSync`，菜单数扩展为 6；宿主测试覆盖六项正反向首尾循环、多步移动和所有三行窗口起点，并以 `-Wall -Wextra -Werror` 编译运行成功。OLED 标签和物理旋钮路径留待 task-02/task-05 验证。

## task-02: [OLED UI] 实现最后成功对时只读详情页并统一息屏计划文案

**追踪需求：** R-01, R-02, R-06, R-07, NFR-01, NFR-02, NFR-03, NFR-04, AC-01, AC-02, AC-03, AC-09, AC-10

**依赖任务：** task-01

**修改范围：** `src/app_state.h`、`src/app_controller.cpp`、`src/ui_render.cpp`

**公共能力处理：** 扩展并复用。已检索 `SettingState`、SETTING Confirm/Cancel/旋钮分支、`invalidatePageLayout()`、现有设置渲染和 `timeSyncFormatLocalEpoch()`；新增单一 `TimeSyncInfo` 状态并直接复用这些能力，不建立页面框架或保存时间副本。

**代码注释要求：** formatter 成功后切分固定 19 字符文本时，简要说明其依赖公共 formatter 的固定输出契约；显式无操作的 Confirm/旋钮分支和简单 switch 不添加空泛注释。

**完成标准：**

- `SettingState` 新增无附加数据的 `TimeSyncInfo`。
- 菜单标签按 `BRIGHTNESS`、`TIME SET`、`TIME SYNC`、`SCREEN SCHEDULE`、`WIFI CONFIG`、`WIFI` 排列。
- 菜单行缓冲使用 `AppConfig::LINE_CACHE_LEN`，`SCREEN SCHEDULE` 不截断。
- 在菜单选中 `TIME SYNC` 后按 Confirm 进入详情页；Cancel 返回菜单且仍选中 `TIME SYNC`。
- 详情页 Confirm 和旋钮无业务副作用，不申请网络、不写 RTC/NVS、不修改 epoch。
- 合法 epoch 通过 `timeSyncFormatLocalEpoch()` 转换，并将日期和时间分两行展示；0 或无效 epoch 展示 `NEVER`。
- 详情页 header 为 `TIME SYNC`，正文 `LAST SUCCESS`、日期、时间或 `NEVER` 适配现有 128x64 行布局。
- OLED 原 `NIGHT OFF` 菜单和编辑页标题统一改为 `SCREEN SCHEDULE`；`OFF AT`、`ON AT`、开关及内部状态含义不变。
- 所有受影响 switch 完整处理 `TimeSyncInfo`，现有设置交互保持原行为。

**自动化验证：**

- 测试目标：验证菜单逻辑与公共 formatter 契约，为 OLED 分支提供确定输入。
- 测试用例 / 脚本：运行 task-01 setting host test；运行现有 Time Sync host test覆盖 0 epoch、合法 epoch、UTC+8 跨日和 20 字节输出；静态检查 UI 包含 `TIME SYNC`、`LAST SUCCESS`、`NEVER`、`SCREEN SCHEDULE` 且生产 UI 不再含字符串 `NIGHT OFF`。
- 执行命令：运行 task-01 命令；`c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/test_time_sync_logic.cpp src/time_sync_logic.cpp -o /tmp/focus_clock_test_time_sync && /tmp/focus_clock_test_time_sync`；通过 `rg` 检查目标文案；执行 PlatformIO 构建验证 enum/switch 与目标显示代码兼容。
- 验证局限：formatter 与编译无法证明 OLED 像素布局、缓存清理或物理按键路径，必须人工验收。

**人工验证关注点：**

- 入口：长按 Mode 进入 SETTING，选择 `TIME SYNC`。
- 步骤：分别使用无记录和有合法记录状态进入详情页，操作 Confirm、Cancel 和旋钮；再进入 `SCREEN SCHEDULE` 完成开关、OFF AT、ON AT 编辑。
- 预期：详情分别显示 `NEVER` 或正确日期/秒级时间；Confirm/旋钮无动作；Cancel 返回原选中位置；新名称完整显示，原息屏计划可保存。
- 异常状态：检查最长标签、日期、时间、RTC header 不重叠，不遗留前一页面缓存文字；无效 epoch 不显示默认时间。

**待确认问题：** 无

**执行记录（2026-08-10）：** 已新增 `SettingState::TimeSyncInfo`，接入菜单 Confirm/Cancel/旋钮只读行为，复用固定 UTC+8 formatter 分行显示成功日期/时间或 `NEVER`；菜单和息屏编辑页用户可见名称统一为 `SCREEN SCHEDULE`，菜单缓冲改用 `LINE_CACHE_LEN`。setting/time-sync 宿主测试和 ESP32-C3 固件构建成功；OLED 像素布局与物理交互留待 task-05。

## task-03: [Time Sync] 完整成功后触发 OLED 即时重绘

**追踪需求：** R-03, R-06, NFR-01, NFR-03, NFR-04, AC-04, AC-06, AC-07

**依赖任务：** task-02

**修改范围：** `src/time_sync_task.cpp`

**公共能力处理：** 直接复用。已检索最后成功 epoch 的唯一生产发布点、`AppState.displayDirty` 和主循环渲染路径；在现有成功分支设置显示脏标记，不新增事件、observer、轮询或 UI 定时器。

**代码注释要求：** 在完整成功发布 epoch 并标记重绘处添加简短注释，说明停留在只读详情页时需要立即反映最新成功结果；普通赋值不重复解释。

**完成标准：**

- 只有 RTC 写入、强制回读和结果持久化全部成功的现有 `Succeeded` 分支更新 epoch 并设置 `app.displayDirty = true`。
- 用户停留在 `TimeSyncInfo` 时，主循环下一次渲染读取新 epoch。
- 失败、超时、策略关闭、凭据缺失和 RTC 提交失败不修改既有 epoch。
- 不改变 Time Sync phase、结果消费、WiFi consumer 释放或持久化顺序。
- 不新增阻塞等待或额外网络行为。

**自动化验证：**

- 测试目标：证明现有成功/失败状态语义不回归，并确认重绘标记只位于成功发布点。
- 测试用例 / 脚本：运行 Time Sync host test覆盖完整成功和各提交失败路径；源码检查 `lastTimeSyncSuccessEpoch` 与 `displayDirty` 位于同一成功分支，失败分支不清零 epoch；PlatformIO 构建验证集成。
- 执行命令：运行 task-02 的 Time Sync host test命令；使用 `rg` / 人工源码核对成功发布点；执行 PlatformIO 构建。
- 验证局限：纯逻辑测试不持有 Arduino `AppState` 渲染循环，停留页面即时刷新必须真机确认。

**人工验证关注点：** 入口为 OLED `TIME SYNC` 详情页；停留页面并完成真实成功同步，预期无需退出即可更新。随后制造 WiFi/NTP/RTC 失败，预期原成功时间保持且不闪回 `NEVER`。

**待确认问题：** 无

**执行记录（2026-08-10）：** 已在完整成功发布 `lastTimeSyncSuccessEpoch` 的同一分支设置 `app.displayDirty = true`，失败终态和资源/持久化顺序未变。Time Sync 宿主测试、成功发布点源码核对和 ESP32-C3 固件构建成功；详情页停留期间的实际刷新留待 task-05 真机验证。

## task-04: [Portal] 收敛展示文案并禁用旧页面缓存

**追踪需求：** R-04, R-05, R-06, R-07, NFR-01, NFR-02, NFR-03, NFR-04, AC-05, AC-06, AC-07, AC-08, AC-10

**依赖任务：** 无

**修改范围：** `src/wifi_portal.cpp`、`web/wifi_portal.html`

**公共能力处理：** 扩展并复用。已检索 `GET /api/config`、根页面 `send_P()`、Device fieldset、页面 `load()` 和 PlatformIO `embed_txtfiles`；保留现有 API schema、formatter、DOM 和保存 payload，只新增 `wifi_portal.cpp` 内部 no-store header helper 并复用两次。

**代码注释要求：** no-store helper 前添加简短注释，说明固定 `192.168.4.1` 地址必须避免跨固件复用旧配置页；DOM 文案赋值无需注释。

**完成标准：**

- Portal 字段文案由 `Last time sync` 改为 `Last successful sync`，null 仍显示 `never`。
- Portal fieldset 由 `Night screen` 改为 `Screen schedule`。
- 最后成功值继续直接使用 `data.timeSync.lastSuccess`，不通过浏览器 `Date` 或本地时区转换。
- 该值不进入保存 payload，不增加输入控件、轮询、WebSocket 或 SSE。
- 根页面 `/` 在发送嵌入 HTML 前设置 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`、`Pragma: no-cache`、`Expires: 0`。
- `GET /api/config` 发送动态 JSON 前使用相同的 no-store header。
- POST、扫描、测试、SoftAP guard 和 API schema 保持不变。
- `web/wifi_portal.html` 继续由现有 PlatformIO 配置嵌入固件。

**自动化验证：**

- 测试目标：验证页面语法、只读数据流、文案和缓存 header 实现。
- 测试用例 / 脚本：提取内嵌 `<script>` 后运行 `node --check`；静态检查目标 DOM、`Last successful sync`、`Screen schedule`、`never`；确认不存在对 `timeSync.lastSuccess` 的 `new Date` 转换且保存 payload 无该字段；检查根页面和配置 GET 均调用 no-store helper；运行 Time Sync/JSON/Portal validation 宿主测试。
- 执行命令：使用现有页面脚本提取方式执行 `node --check`；运行 `tests/host/test_time_sync_logic.cpp`、`test_json_writer.cpp`、`test_portal_validation.cpp` 的既有编译/执行命令；使用 `rg` 做文案和数据流检查；执行 PlatformIO 构建确认嵌入符号和链接。
- 验证局限：源码和构建无法证明设备实际响应 header、烧录版本、浏览器缓存与手机布局，必须执行 task-05 真机 HTTP/DOM 验收。

**人工验证关注点：**

- 入口：设备 SoftAP 的 `http://192.168.4.1/`。
- 步骤：使用 `curl -i` 请求根页面和 `/api/config`，再用曾访问旧固件的手机重新打开页面；分别验证无记录和有记录状态，并尝试保存其他配置。
- 预期：实际响应包含 no-store headers；DOM 显示新文案和 `never` / UTC+8 文本；刷新取得当前值；配置保存 payload 与行为不变。
- 异常状态：API 有值但 DOM 无值时分别核对刷入固件、返回 HTML、嵌入产物和浏览器缓存；窄屏标签和值不重叠。

**待确认问题：** 无

**执行记录（2026-08-10）：** 已将 Portal 文案更新为 `Last successful sync` 和 `Screen schedule`；新增局部 no-store header helper，并用于根页面与配置 GET，保持 API schema、保存 payload 和其他路由不变。页面脚本语法、同步字段数据流、JSON writer、Portal validation 宿主测试及嵌入页面固件构建成功；实际设备 HTTP header、旧浏览器缓存和窄屏 DOM 留待 task-05。

## task-05: [Verification] 更新说明并完成自动化与真机验收

**追踪需求：** R-01 至 R-07, NFR-01 至 NFR-04, AC-01 至 AC-10

**依赖任务：** task-02, task-03, task-04

**修改范围：** `README.md`、`specs/20260810_time_sync_status_display/tasks.md`、`specs/20260810_time_sync_status_display/changelog.md`；仅在验证发现本功能缺陷时回到对应任务列出的生产文件修复

**公共能力处理：** 复用全部前置任务和现有测试/构建入口，不新增公共能力。复核 SETTING、Time Sync、Portal、persistence 和 display dirty 的所有权边界；发现与已确认设计不一致时停止实现并回到 Phase 2/3 更新规格。

**代码注释要求：** README 和验证记录不新增代码注释；如修复缺陷，遵循对应任务的注释要求。

**完成标准：**

- README 说明 `TIME SYNC` 最后成功查看入口、`NEVER` 语义、固定 UTC+8 和 Portal `Last successful sync`。
- README 将用户可见 `NIGHT OFF` / 夜间息屏名称收敛为 `SCREEN SCHEDULE` / 息屏计划，同时准确保留原开关、关闭和恢复时间行为。
- 生产 OLED、Portal 和 README 不残留旧用户可见文案；内部兼容标识允许保留。
- task-01 至 task-04 的自动化验证全部执行并记录结果。
- 执行相关宿主测试全量回归、页面 JavaScript 检查、文案/缓存/数据流源码扫描、`git diff --check` 和 PlatformIO 构建。
- 记录编译 warning、RAM/Flash 占用和所有自动检查局限。
- 真机逐项验证 OLED、真实 NTP、重启恢复、失败保留、实际 HTTP header、浏览器 DOM、旧缓存和窄屏布局。
- 未执行或用户未确认的真机项目明确保留为待验证；不得据自动化结果声明完整验收或生产可用。

**自动化验证：**

- 测试目标：覆盖新增逻辑及 SETTING、Time Sync、persistence、Portal 和核心 UI 编译回归。
- 测试用例 / 脚本：运行 `tests/host/` 下 setting、Time Sync、task persistence、JSON writer、Portal validation、scheduled task 和 WiFi 等相关测试；执行页面脚本语法检查；扫描旧文案、浏览器时间转换、缓存 helper 调用和最后成功发布点；确认 PlatformIO 嵌入页面并成功链接。
- 执行命令：逐个执行仓库既有 host test 编译/运行命令；提取 HTML script 后运行 `node --check`；执行 `git diff --check`；执行 `/Users/naaran/.platformio/penv/bin/pio run -e esp32-c3-zero`。
- 验证局限：自动化不能证明 SSD1306 实际布局、物理输入、DS1302/NTP、AP_STA、设备 HTTP header 或手机缓存行为。

**人工验证关注点：**

- OLED 无记录：显示 `LAST SUCCESS` + `NEVER`，Confirm/旋钮无副作用，Cancel 回到 `TIME SYNC`。
- OLED 有记录：固定 UTC+8 日期/时间正确、无重叠，停留期间同步成功后即时刷新。
- 一致性：OLED 与 Portal 对应同一成功时刻，重启后恢复，后续失败不覆盖。
- 菜单：六项首尾循环和三行窗口正确，`SCREEN SCHEDULE` 完整显示并保持原编辑/保存行为。
- Portal：根页面与配置 GET 实际 no-store header、新文案、`never` / 成功值、保存不改写同步值。
- 旧缓存与窄屏：曾访问旧固件的手机无需清缓存看到新页面，常用手机宽度下标签和值不遮挡。
- 回归：CLOCK、TIMER、其他 SETTING、Portal 配置、每日 Time Sync、按钮和旋钮持续可用。

**待确认问题：** 无

**执行记录（2026-08-10）：** README 已补充六项菜单、`TIME SYNC`、`NEVER`、固定 UTC+8、`Last successful sync`、Portal 禁用缓存和 `SCREEN SCHEDULE` 语义。scheduled task、Time Sync、task persistence、WiFi、setting、JSON writer、Portal validation 七组宿主测试均以 `-Wall -Wextra -Werror` 编译运行成功；页面脚本通过 `node --check`，生产 UI/README 旧用户文案扫描无结果，最后成功字段未进入保存 payload 或浏览器时区转换，no-store helper 调用点与成功发布重绘点已核对，`git diff --check` 无输出。ESP32-C3 构建成功且无编译 warning，RAM 42652 / 327680 bytes（13.0%），Flash 858238 / 1310720 bytes（65.5%）；固件 ELF 包含 `Last successful sync` 与 `Screen schedule`。用户随后确认实际 ESP32 固件测试通过，覆盖本任务的真机验收；本地模拟浏览器检查按用户要求取消并关闭服务/会话。最终代码审查未发现行为缺陷，仅合并只读无操作状态分支并将缓存 helper 更名为更准确的 `addNoStoreHeaders`，清理后再次完成全部自动化回归和固件构建。
