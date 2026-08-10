# 任务拆解：定时任务插件与 NTP 自动对时

> 状态：已确认

日期：2026-08-09
Feature：ntp_time_sync
阶段：Phase 3

- [x] task-01: [Scheduler Logic] 新增固定容量每日调度纯逻辑
- [x] task-02: [Persistence] 新增任务尝试记录和对时结果持久化
- [x] task-03: [RTC] 移除无效 RTC 自动写入并增加 `12:00` 故障展示
- [x] task-04: [WiFi] 增加 Time Sync 网络 consumer
- [x] task-05: [Time Logic] 新增 Time Sync 状态转换和时间转换纯逻辑
- [x] task-06: [Scheduler Service] 实现派发即记账的调度服务
- [x] task-07: [SNTP Plugin] 实现按需网络和非阻塞 SNTP 执行
- [x] task-08: [RTC Commit] 接入 RTC 单次写入和成功结果持久化
- [x] task-09: [Integration] 接入初始化、主循环和运行状态
- [x] task-10: [Portal] 展示最后成功对时时间
- [x] task-11: [Verification] 完成说明文档、全量自动检查和真机验收清单

## task-01: [Scheduler Logic] 新增固定容量每日调度纯逻辑

**追踪需求：** R-01, R-02, R-03, NFR-02, NFR-03, NFR-04

**依赖任务：** 无

**修改范围：** `src/scheduled_task_logic.h`、`src/scheduled_task_logic.cpp`、`src/config_timing.h`、`tests/host/test_scheduled_task_logic.cpp`

**公共能力处理：** 新建。已检索 `src/rtc_service.*`、`src/display_power.*`、`src/sleep_manager.*` 和 `src/wifi_logic.*`；现有代码只有 RTC 读取周期、分钟窗口和溢出安全 deadline，没有稳定任务 ID、精确分钟触发、日期键或当日去重。新模块保持标准 C++ 依赖，供后续每日任务复用。

**代码注释要求：** 在精确分钟等值判断处说明“不补执行”的业务语义；在固定 4 槽容量处说明 V1 持久化兼容边界。普通 enum 和字段映射不添加复述性注释。

**完成标准：**

- 定义稳定的 `ScheduledTaskId::TimeSync`、`Count`、统一 `TaskRunStatus` / `TaskRunResult` 和每日任务定义。
- 固定 Time Sync 调度分钟为 `8 * 60`，不增加运行时配置。
- 提供 `RtcTime` 到合法 `YYYYMMDD` 日期键和分钟值的纯函数。
- 提供只在目标分钟相等、RTC 有效、当天未尝试且未运行时返回到期的纯判断。
- 固定持久化槽位容量为 4，并用编译期断言约束任务 ID。
- 不实现 cron、动态注册表、继承体系或单次调用包装层。

**自动化验证：**

- 测试目标：证明精确分钟触发、不补执行、日期去重和固定槽位边界。
- 测试用例 / 脚本：新增 `tests/host/test_scheduled_task_logic.cpp`，覆盖 07:59、08:00:00、08:00:59、08:01、07:59 跳到 08:00:30、跳到 08:01、RTC invalid、运行中、同日 RAM/恢复记录、次日、闰年日期和非法日期。
- 执行命令：`c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/test_scheduled_task_logic.cpp src/scheduled_task_logic.cpp -o /tmp/focus_clock_test_scheduler && /tmp/focus_clock_test_scheduler`。
- 兜底检查：`pio run -e esp32-c3-zero` 只验证目标工具链可以编译新类型，不证明调度语义正确。

**人工验证关注点：** 本任务为纯逻辑，无独立硬件入口；在 task-11 通过实际 RTC 跨分钟验证唤醒和读取节奏。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已新增稳定任务 ID、4 槽容量、固定 08:00 定义、RTC 字段到日期键/分钟的纯函数和精确分钟到期判断；宿主测试覆盖 07:59、08:00、08:01 不补执行、invalid/running/同日去重、次日恢复、闰年及非法日期并成功运行，task-01 至 task-04 批次完整固件构建成功。真机跨分钟仍属于 task-11。

## task-02: [Persistence] 新增任务尝试记录和对时结果持久化

**追踪需求：** R-03, R-08, NFR-04, NFR-05

**依赖任务：** task-01

**修改范围：** `src/persistence_codec.h`、`src/persistence_codec.cpp`、`src/persistence.h`、`src/persistence.cpp`、`tests/host/test_task_persistence.cpp`

**公共能力处理：** 扩展。已检索 `src/persistence.*`、`src/persistence_codec.*` 和现有 network V1 blob；复用同一 Preferences namespace、完整 blob 写入、进程内缓存和纯 codec 测试模式。新增 `taskRuns` 和 `timeSync` 两个独立 V1 blob，不建立泛型序列化框架。

**代码注释要求：** 调度记录完整写入后才更新缓存处说明防止 RAM/NVS 状态分裂；固定 4 槽和 reserved 字段只在布局声明处说明版本兼容意图。

**完成标准：**

- 定义固定布局 `PersistedScheduledTaskRecordsV1`，包含 version、`slotCount=4`、reserved 和 4 个日期键。
- 定义固定布局 `PersistedTimeSyncResultV1`，包含 version、reserved 和最后成功 UTC epoch。
- codec 拒绝错误 size、version、slotCount、非法日期键和 UTC+8 后超出 2000..2099 的 epoch；0 表示无记录。
- persistence 层提供加载/保存任务记录和最后成功 epoch 的显式接口。
- 保存使用完整 blob，失败时不更新缓存；不保存 phase、failure、attempt 或 deadline。
- 不修改已有 network blob 的布局、key 或回退语义。

**自动化验证：**

- 测试目标：验证两个 V1 blob 的稳定布局、往返和整份回退规则。
- 测试用例 / 脚本：覆盖空记录、有效往返、固定 size、reserved 清零、截断、未知版本、错误 slotCount、非法月份/日期、4 个槽位、0 epoch、2000/2099 边界和越界 epoch。
- 执行命令：`c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/test_task_persistence.cpp src/persistence_codec.cpp src/scheduled_task_logic.cpp src/wifi_logic.cpp -o /tmp/focus_clock_test_task_persistence && /tmp/focus_clock_test_task_persistence`。
- 兜底检查：`pio run -e esp32-c3-zero` 只验证 Preferences API 和 blob 布局能在固件中编译。

**人工验证关注点：** 真机在 task-11 验证派发后重启仍保留当天记录、成功 epoch 重启后仍可读取；NVS 实际写入失败不易稳定制造，需以串口错误路径和 codec 测试补充。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已新增固定 20 字节的 4 槽任务记录 V1 blob、固定 8 字节的 Time Sync 结果 V1 blob，以及 `taskRuns` / `timeSync` Preferences 读写接口；非法 size/version/slotCount/日期键/UTC+8 年份范围整份回退，完整写入后才更新缓存。独立宿主测试覆盖空值、reserved 清零、四槽往返、闰日、截断和 2000/2099 epoch 边界并成功运行；真实 NVS 重启恢复留待 task-11 真机验证。

## task-03: [RTC] 移除无效 RTC 自动写入并增加 `12:00` 故障展示

**追踪需求：** R-10, R-12, NFR-01, NFR-06

**依赖任务：** 无

**修改范围：** `src/rtc_service.h`、`src/rtc_service.cpp`、`src/app_state.h`、`src/config_timing.h`、`src/ui_render.cpp`、`README.md` 中现有 RTC 行为说明

**公共能力处理：** 收敛现有能力。已检索 RTC service、AppState 和 CLOCK/TIMER 渲染；删除与确认需求冲突的编译时间自动初始化状态，复用已有 invalid 标志、短周期读取和状态文字。固定 `12:00` 直接留在渲染位置，不为单个展示值新增公共 formatter。

**代码注释要求：** RTC 读取失败分支补充一条业务意图注释，说明保持 invalid 且不写猜测时间是为了暴露硬件故障。简单的 `12:00` 文本选择无需注释。

**完成标准：**

- 删除 `RtcAutoInitState`、自动初始化字段、编译时间解析和 `RTC_AUTO_INIT_DELAY_MS`。
- RTC 无效时不调用 `rtcSetTime()`，保持每秒短周期读取。
- CLOCK 主时间及其他会显示“当前 RTC 时间”的位置在 invalid 时显示固定 `12:00`。
- CLOCK 状态/日期行继续显示 `RTC READ FAIL`，不把 `12:00` 表示成真实时间。
- 读到有效 RTC 后自然恢复正常显示和调度输入。
- 本机 TIME SET 和 Portal 显式设置 RTC 的既有能力不变。

**自动化验证：**

- 测试目标：确认移除自动写入路径且固件 switch/接口仍完整。
- 测试用例 / 脚本：使用 `rg` 确认生产代码无 `RtcAutoInitState`、`buildTimeToRtcTime`、`RTC_AUTO_INIT_DELAY_MS` 及 RTC service 中的自动 `rtcSetTime`；运行既有 setting/portal host tests 防止手动设置校验回归。
- 执行命令：`rg -n "RtcAutoInitState|buildTimeToRtcTime|RTC_AUTO_INIT_DELAY_MS" src` 应无结果；运行现有 `tests/host/test_setting_logic.cpp` 和 `tests/host/test_portal_validation.cpp` 对应宿主命令。
- 兜底检查：`pio run -e esp32-c3-zero` 只验证删除状态后的固件编译，不证明硬件不会被写入或显示正确。

**人工验证关注点：**

- 入口：断开、停振或制造 DS1302 无效寄存器后进入 CLOCK。
- 步骤：观察至少数次短周期读取，再重启设备或恢复 RTC。
- 预期：大字固定显示 `12:00`，状态行显示 `RTC READ FAIL`；串口只有读取失败/原始寄存器日志，没有自动写入日志；恢复有效读数后正常显示。
- 异常状态：确认本机 TIME SET 和 Portal 显式设置仍能修复 RTC，OLED 文本不重叠。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已删除 `RtcAutoInitState`、编译时间解析、延迟自动初始化和 RTC service 自动写入；无效 RTC 保持 invalid 并每秒读取，CLOCK 大字及当前时间 header 固定显示 `12:00`，状态行保持 `RTC READ FAIL`。源码扫描确认 RTC service 无 `rtcSetTime`，写入只剩本机 TIME SET 和 Portal 显式提交；setting/portal 宿主回归及完整固件构建成功。RTC 故障、恢复和 OLED 布局仍需 task-11 真机确认。

## task-04: [WiFi] 增加 Time Sync 网络 consumer

**追踪需求：** R-04, R-10, NFR-03, NFR-06

**依赖任务：** 无

**修改范围：** `src/network_types.h`、`src/wifi_service.h`、`tests/host/test_wifi_logic.cpp`

**公共能力处理：** 扩展。已检索 `WifiServiceState::autoConsumerMask`、request/release、mode 推导和 sleep manager；现有位掩码已经满足多 consumer 聚合，只需增加 `WifiConsumer::TimeSync = 1 << 0`。不增加第二套网络 service 或休眠 blocker。

**代码注释要求：** enum 值本身足够明确；保留现有“业务功能到来时再声明 consumer”的注释语义，无额外注释要求。

**完成标准：**

- 增加稳定且非零的 `WifiConsumer::TimeSync` bit。
- request/release 保持幂等，释放 TimeSync 不清除其他 consumer 位。
- `OFF` policy 仍拒绝普通 auto consumer；AP、扫描和连接测试逻辑不变。
- consumer 存续时继续通过现有 `networkTaskActive` 阻止 Light Sleep。

**自动化验证：**

- 测试目标：验证 consumer bit 可参与 mask 聚合且不改变模式规则。
- 测试用例 / 脚本：扩展 WiFi host test，至少静态断言 bit 非零且在 8-bit mask 内；现有模式矩阵、OFF/AUTO 和 deadline 测试全部继续运行。request/release 的 Arduino service 集成由 task-07、task-08 和真机验证覆盖。
- 执行命令：沿用 `tests/host/test_wifi_logic.cpp` 的宿主编译命令并运行；随后 `pio run -e esp32-c3-zero`。
- 兜底限制：固件构建不能证明 Portal 并发时 AP 生命周期正确。

**人工验证关注点：** 在 task-11 同时运行 Portal 和 Time Sync，确认释放 TimeSync 后 AP 仍在，最后一个 STA demand 消失后才关闭 STA。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已在无 Arduino 依赖的 `network_types.h` 增加稳定 `WifiConsumer::TimeSync` 单 bit，并由 `wifi_service.h` 编译期断言非零；mask 宿主测试确认释放 Time Sync 不清除其他 consumer 位，既有 OFF/AUTO 模式矩阵保持成功。完整固件构建成功；实际 request/release、Portal AP_STA 并发和 Light Sleep 恢复留待 task-07/task-11。

## task-05: [Time Logic] 新增 Time Sync 状态转换和时间转换纯逻辑

**追踪需求：** R-05, R-06, R-07, R-11, NFR-01, NFR-04

**依赖任务：** task-01

**修改范围：** `src/rtc.h`、`src/time_sync_logic.h`、`src/time_sync_logic.cpp`、`src/config_network.h`、`src/config_timing.h`、`tests/host/test_time_sync_logic.cpp`

**公共能力处理：** 新建并复用。已检索 RTC 日期校验、WiFi deadline 和 ESP SNTP API；复用 `RtcTime` 与已有有符号差值 deadline 约定，但不让通用 Time Sync 逻辑依赖 WiFi 业务模块。将 `rtc.h` 的整数类型依赖改为标准头文件，使转换逻辑可宿主测试；硬件函数接口保持不变。

**代码注释要求：** UTC epoch 加固定偏移后使用 `gmtime_r` 处说明不依赖全局/浏览器时区；只有 SNTP completed 才允许转换的判断处说明隔离旧系统时间；deadline 普通实现不重复解释代码。

**完成标准：**

- 定义 Time Sync phase、failure、事件/动作或等价的纯状态转换类型。
- 集中配置三个 NTP server、UTC+8、WiFi 20 秒总 deadline、SNTP 10 秒、重试 3 秒和最多两个 attempt。
- 状态逻辑覆盖 OFF、无凭据、WiFi 失败/超时/断线、首次 SNTP 失败后重试和第二次失败终止。
- 在未收到本轮 completed 时，任何已有 epoch 都不能产生 RTC 写入动作。
- UTC epoch 转换为固定 UTC+8 `RtcTime`，正确处理跨日、闰年、星期和 2000..2099 边界。
- 提供最后成功 epoch 的固定格式 `YYYY-MM-DD HH:MM:SS` helper，供持久化校验和 Portal 共用。
- RTC 写入/回读失败动作直接终止，不产生 RTC 重试。

**自动化验证：**

- 测试目标：证明有限状态转换、旧时间隔离、回绕安全和日历转换正确。
- 测试用例 / 脚本：覆盖两个 attempt 的成功/超时序列、WiFi 总 deadline、断线、策略/凭据变化、completed gate、`millis()` 回绕、UTC+8 跨日、2000/2099、2100 拒绝、闰日、星期映射、格式化和 RTC 单次失败终止。
- 执行命令：`c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/test_time_sync_logic.cpp src/time_sync_logic.cpp -o /tmp/focus_clock_test_time_sync && /tmp/focus_clock_test_time_sync`。
- 兜底检查：`pio run -e esp32-c3-zero` 只验证目标 libc 的 `gmtime_r` 和类型兼容性。

**人工验证关注点：** 本任务是纯逻辑；真实 NTP server、DNS 和系统 epoch 由 task-07、task-11 验证。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已新增无 Arduino 依赖的 Time Sync phase/failure/action 状态转换、20/10/3 秒 deadline、最多两次 attempt、completed gate、固定 UTC+8 epoch 转换与格式化，并将 `RtcTime` 改为标准整数头依赖。独立宿主测试覆盖策略/凭据变化、WiFi 失败与断线、重试、旧 epoch 隔离、RTC 各终态、连续两轮状态重置、`millis()` 回绕、闰日/星期及 2000..2099 边界并成功运行；固件构建确认目标 libc 兼容。

## task-06: [Scheduler Service] 实现派发即记账的调度服务

**追踪需求：** R-01, R-02, R-03, R-10, R-11, NFR-03, NFR-05

**依赖任务：** task-01, task-02

**修改范围：** `src/scheduled_task_service.h`、`src/scheduled_task_service.cpp`、`tests/host/test_scheduled_task_logic.cpp`

**公共能力处理：** 新建。基于 task-01 的纯判断和 task-02 的 persistence 显式接口建立固定容量 service；不在 service 中引入 WiFi、SNTP、RTC 写入或插件回调表。首期通过稳定 ID 返回 dispatch，由 `main.cpp` 显式分发。

**代码注释要求：** 派发前写 NVS 处说明 at-most-once 意图；NVS 失败时 RAM 记账但不启动处说明避免未记账执行；同步成功后用回读日期覆盖记录处说明防止日期修正导致重复。

**完成标准：**

- `Begin` 从持久化记录恢复 4 槽日期键。
- `Update` 接受当前 RTC 和运行标志，只在精确 08:00 匹配时产生一次 `ScheduledTaskId::TimeSync` dispatch。
- 匹配后先更新 RAM 并保存；保存成功才返回 dispatch，失败时本次开机不循环尝试。
- `ConsumeResult` 清理运行态；成功且回读日期变化时覆盖并保存真实日期键。
- 成功、失败和执行中重启均不形成当天第二次派发。
- service 不保存瞬时 phase、failure 或插件对象。

**自动化验证：**

- 测试目标：验证派发、持久化调用顺序和结果消费语义。
- 测试用例 / 脚本：在 `scheduled_task_logic` 增加不接触 Preferences 的记账决策函数，覆盖保存成功、保存失败不 dispatch、运行中不重复、成功/失败终态、校正日期二次保存和恢复后不派发；service 只负责按该决策调用 persistence。
- 执行命令：运行 task-01 的 `test_scheduled_task_logic` 命令，并通过 `pio run -e esp32-c3-zero` 验证 service 与 Preferences 链接。
- 兜底检查：`pio run -e esp32-c3-zero` 只验证 persistence 链接和 service 集成。

**人工验证关注点：** task-11 将设备在 08:00 派发后立即重启，确认当日不再启动；观察 NVS 保存失败日志路径时不得打开 STA。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已新增固定容量 scheduler service，从 persistence 恢复记录，在精确分钟匹配后先更新 RAM 和保存 NVS，只有保存成功才派发稳定任务 ID；保存失败时本次开机 RAM 去重但不启动任务。结果消费会清理运行态，并在成功回读日期变化时覆盖记录；纯逻辑测试覆盖保存成功/失败、恢复去重、失败终态和日期修正，固件构建确认 service 与 Preferences 链接。真实 NVS 失败和派发中重启留待 task-11 真机确认。

## task-07: [SNTP Plugin] 实现按需网络和非阻塞 SNTP 执行

**追踪需求：** R-01, R-04, R-05, R-06, R-10, R-11, NFR-01, NFR-02

**依赖任务：** task-04, task-05

**修改范围：** `src/time_sync_task.h`、`src/time_sync_task.cpp`、`tests/host/test_time_sync_logic.cpp`

**公共能力处理：** 新建插件执行层并复用现有资源服务。SDK 调用局限在 `time_sync_task.cpp`；复用 task-05 的纯转换/状态决策和 `wifiServiceRequestAutoNetwork` / release，不新增网络连接 service、后台 task 或独立 SNTP 全局服务。

**代码注释要求：** 每 attempt 的 stop + status RESET 需说明用于证明本轮完成；统一终态函数说明只释放自身 consumer。普通 phase switch 不逐分支注释。

**完成标准：**

- 实现固定状态结构和 `Begin` / `Start` / `Update` / `TakeResult`。
- `Start` 校验 policy 和凭据；成功申请 TimeSync consumer 后设置 20 秒总 deadline。
- WiFi connected 后启动 SNTP；每个 attempt 前 stop、RESET，再用 `configTime()` 配置三个 server。
- 仅 `esp_sntp_get_sync_status() == COMPLETED` 后读取 epoch；不调用阻塞 `getLocalTime()`。
- 两个 attempt 之间使用 3 秒非阻塞 delay；最终成功/失败停止 SNTP并释放 consumer。
- 获得合法 epoch 后进入待提交阶段并保留本轮值，供 task-08 完成 RTC 提交；该阶段仍不重复启动 SNTP。
- 所有终态只产生一次可消费结果；日志不输出 SSID 密码。

**自动化验证：**

- 测试目标：验证生产状态机调用 task-05 决策、两个 attempt 边界和所有网络/SNTP 终态收口释放资源。
- 测试用例 / 脚本：task-05 host test 覆盖状态路径；源代码检查确认无 `getLocalTime` 和长 `delay`，并确认 `esp_sntp_stop`、RESET、COMPLETED、request/release、单个 `rtcSetTime` 调用点存在。
- 执行命令：运行 `test_time_sync_logic`；`rg -n "getLocalTime|delay\\(" src/time_sync_task.cpp` 应无阻塞调用；`pio run -e esp32-c3-zero` 验证 Arduino-ESP32 2.0.17 SNTP API 编译和链接。
- 兜底限制：mock 不覆盖 lwIP callback/thread 行为；真实 DNS、SNTP 状态和无线释放必须真机验证。

**人工验证关注点：**

- 用正确/错误凭据及隔离互联网分别触发成功、WiFi 失败和 SNTP 超时。
- 观察串口 phase/attempt/failure，确认最多两个 attempt，过程中 OLED、Timer、输入、Portal 持续响应。
- 所有终态确认 STA 按其他 demand 关闭且 Light Sleep 恢复。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已新增显式 `Begin` / `Start` / `Update` / `TakeResult` 插件状态，按需申请并只释放 `WifiConsumer::TimeSync`；每次 attempt 执行 SNTP stop + RESET + 三 server `configTime()`，仅 COMPLETED 后读取 epoch，两次 attempt 间使用非阻塞 deadline。源码检查确认无 `getLocalTime()` 或插件内 `delay()`，资源 API 和 SNTP 状态调用点完整；Arduino-ESP32 2.0.17 固件编译链接成功。真实 DNS/NTP、Portal 并发和无线/休眠释放留待 task-11 真机确认。

## task-08: [RTC Commit] 接入 RTC 单次写入和成功结果持久化

**追踪需求：** R-06, R-07, R-08, R-11, NFR-01, NFR-05

**依赖任务：** task-02, task-03, task-07

**修改范围：** `src/time_sync_task.h`、`src/time_sync_task.cpp`、`src/rtc_service.h`、`src/persistence.h`、`tests/host/test_time_sync_logic.cpp`

**公共能力处理：** 扩展 task-07 插件。复用 `rtcSetTime()` 的单次写后寄存器确认、`rtcServiceForceRead()`、task-02 的结果 persistence 和 task-05 的 epoch 转换；不创建第二套 RTC adapter 或通用事务框架。

**代码注释要求：** RTC 写入、回读和 epoch 保存顺序前说明只有三步全部完成才发布最后成功结果；失败直接终止且不重试的分支无需逐行注释。

**完成标准：**

- 待提交 epoch 通过 task-05 转换后只调用一次 `rtcSetTime()`。
- 写入成功后只调用一次 `rtcServiceForceRead()`，要求回读成功且有效。
- 写入或回读失败立即终止，不重新写 RTC、不重新请求 NTP、不更新最后成功 epoch。
- 回读成功后保存最后成功 UTC epoch；保存失败以 `ResultPersistenceFailed` 终止。
- 三步全部成功后才发布 last success 和包含回读日期键的 `TaskRunResult`。
- 成功与所有新增失败路径继续通过 task-07 的统一终态收口停止 SNTP、释放 consumer，并只产生一次结果。

**自动化验证：**

- 测试目标：证明 RTC 提交只有一次，失败不重试，成功结果只在完整事务后发布。
- 测试用例 / 脚本：扩展纯状态测试，覆盖转换失败、写入失败、回读失败、epoch 保存失败、完整成功、回读日期修正，以及每条路径的调用次数/动作序列和 consumer release。
- 执行命令：运行 task-05 的 `test_time_sync_logic`；源码检查 `time_sync_task.cpp` 只有一个 `rtcSetTime` 调用点和一个 force-read 调用点；执行 `pio run -e esp32-c3-zero`。
- 兜底限制：纯状态测试和源码调用点不能证明真实 DS1302 写入成功或 20 ms 停顿不可感知。

**人工验证关注点：**

- 成功后核对 DS1302 秒、日期、星期和重启保持。
- 制造 RTC 写入或回读失败，确认串口只记录一次提交且最后成功时间不变化。
- 对时修正日期时确认 scheduler 使用回读日期，设备重启后不会在真实日期再次派发。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已在待提交阶段按固定顺序执行一次 `rtcSetTime()`、一次 `rtcServiceForceRead()` 和一次成功 epoch persistence；写入、回读、日期键或保存失败均直接终止且不重试，完整成功才发布带回读日期的任务结果。纯状态测试覆盖三类提交失败、完整成功、重复完成保护和 consumer 单次释放；源码计数确认 RTC 写入/强制回读各只有一个调用点，固件构建成功。真实 DS1302 写入、回读日期修正和约 20 ms 交互影响留待 task-11 真机确认。

## task-09: [Integration] 接入初始化、主循环和运行状态

**追踪需求：** R-02, R-03, R-04, R-08, R-10, R-11, NFR-01, NFR-06

**依赖任务：** task-06, task-08

**修改范围：** `src/main.cpp`、`src/app_state.h`、必要时 `src/scheduled_task_service.*`、`src/time_sync_task.*`

**公共能力处理：** 复用。沿用 `main.cpp` 全局 service 实例和单线程 loop 编排；不增加 RTOS task、事件总线或第二套 App controller。AppState 只保存 Portal 需要的最后成功 epoch，不复制插件 phase。

**代码注释要求：** 主循环 service 顺序前保留一段注释，解释 scheduler 必须在 WiFi update 前申请 consumer、Time Sync 必须在其后观察连接；不为每个函数调用逐行注释。

**完成标准：**

- setup 加载任务记录和最后成功 epoch，初始化 scheduler 和 Time Sync service。
- loop 在 RTC update 后运行 scheduler，并在 WiFi update 前启动 dispatch。
- WiFi update 后推进 Time Sync，消费终态并向 scheduler 提交回读日期。
- 最后成功 epoch 只在插件完整成功时进入 AppState。
- consumer 活动继续通过现有 `networkTaskActive` 阻止 Light Sleep，sleep manager 无新增门禁。
- Portal、输入、Timer、render、feedback 和 display power 的既有推进顺序保持可用。

**自动化验证：**

- 测试目标：验证集成编译、调用顺序和既有模块无回归。
- 测试用例 / 脚本：运行所有新增/既有宿主测试；源码顺序检查 `rtcServiceUpdate -> scheduledTaskServiceUpdate/timeSyncTaskStart -> wifiServiceUpdate -> timeSyncTaskUpdate -> wifiPortalUpdate`。
- 执行命令：运行 `tests/host/` 全部对应二进制；`pio run -e esp32-c3-zero`；`git diff --check`。
- 兜底限制：构建和源码顺序不能证明主循环实时响应、Light Sleep 或 AP_STA 真机行为。

**人工验证关注点：**

- 串口带时间戳观察任务各 phase 期间主循环持续推进。
- 在 CLOCK、TIMER 和 Portal 场景分别触发任务，确认输入、显示和 HTTP 不停顿。
- 任务中重启确认当日不重新派发；次日 08:00 恢复资格。
- 08:01 启动或唤醒确认不补执行。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已在 setup 恢复最后成功 epoch 并初始化 scheduler / Time Sync service；loop 按 RTC 更新、scheduler 派发并申请 consumer、WiFi 更新、Time Sync 推进、结果消费、Portal 更新的顺序接入。完整成功后才更新 `AppState.lastTimeSyncSuccessEpoch`，现有 `networkTaskActive` 继续承担休眠门禁。源码顺序检查及 ESP32-C3 固件构建成功；实时响应、AP_STA、Light Sleep 和重启去重仍需 task-11 真机确认。

## task-10: [Portal] 展示最后成功对时时间

**追踪需求：** R-08, R-09, R-10, NFR-04, NFR-06

**依赖任务：** task-05, task-09

**修改范围：** `src/wifi_portal.cpp`、`web/wifi_portal.html`、`tests/host/test_json_writer.cpp` 或新增格式化相关测试

**公共能力处理：** 扩展并复用。复用现有 `GET /api/config`、定长 `JsonWriter`、Device fieldset 和 `load()`；复用 task-05 的设备端 UTC+8 formatter。无需新增 API 路由、JSON 库、页面卡片或 JavaScript 日期转换。

**代码注释要求：** API 输出设备端格式文本处说明避免浏览器时区二次转换；普通 DOM 赋值无注释要求。

**完成标准：**

- `GET /api/config` 增加 `timeSync.lastSuccess`，无记录为 JSON `null`，有记录为 `YYYY-MM-DD HH:MM:SS`。
- 重新核算并调整固定响应缓冲区，保留 overflow 错误处理。
- Device fieldset 增加只读“Last time sync”展示；null 显示 `never`。
- 页面不使用 `new Date(lastSuccess)`，不增加立即同步、时间配置或时区控件。
- 移动端长时间文本不溢出或遮挡现有控件，保存 payload 不变。

**自动化验证：**

- 测试目标：验证 nullable API 值和页面不进行时区二次转换。
- 测试用例 / 脚本：host formatter 测试覆盖 null/有效文本；提取 HTML script 运行 Node 语法检查；静态检查存在 `never` 和目标 DOM，且不存在 `new Date(data.timeSync.lastSuccess)` 等转换。
- 执行命令：使用 `node --check` 检查提取脚本；运行 JSON/Time Sync host tests；`pio run -e esp32-c3-zero` 验证 embedded page 符号和响应构建。
- 兜底限制：语法和构建不能证明实际手机布局，必须人工检查。

**人工验证关注点：**

- 入口：设备 Portal 的 Device 区域。
- 步骤：分别清空/无成功记录和完成一次真实对时后刷新页面，并用不同时区手机或浏览器检查。
- 预期：分别显示 `never` 和设备生成的固定 UTC+8 文本；页面不随浏览器时区变化。
- 异常状态：窄屏下标签和时间换行合理，不与 Clock time、按钮或状态文字重叠；刷新和保存配置不改写该值。

**待确认问题：** 无

**执行记录（2026-08-09）：** 已为 `/api/config` 增加 nullable `timeSync.lastSuccess`，使用设备端固定 UTC+8 formatter 输出并将响应缓冲从 1536 调整为 1664 字节；Device 区域新增只读 `Last time sync`，null 显示 `never`，保存 payload 保持不变。formatter 宿主测试、页面脚本 Node 语法检查、静态时区转换检查及嵌入页面固件构建成功；异时区手机和窄屏布局留待 task-11 人工确认。

## task-11: [Verification] 完成说明文档、全量自动检查和真机验收清单

**追踪需求：** R-01 至 R-12, NFR-01 至 NFR-06

**依赖任务：** task-10

**修改范围：** `README.md`、`specs/20260809_ntp_time_sync/tasks.md`、`specs/20260809_ntp_time_sync/changelog.md`；仅在验证发现本功能缺陷时回到对应任务的已列生产文件修复

**公共能力处理：** 复用全部前置任务；不新增公共能力。复核 WiFi、RTC、persistence、scheduler、Time Sync 和 Portal 的所有权边界，发现设计差距时停止实现并回到 Phase 2/3 更新规格。

**代码注释要求：** 不为验证本身增加代码注释；如修复缺陷，遵循对应任务和 design 的注释约束。

**完成标准：**

- README 准确说明固定 08:00 精确分钟触发、不补执行、两个 SNTP attempt、AUTO 按需网络、最后成功展示和 RTC invalid 的 `12:00` 故障语义。
- 所有 task 的自动化验证命令执行并记录结果，不以 build 代替逻辑测试。
- 源码扫描确认 WiFi 无线生命周期仍只由 `wifi_service.cpp` 管理，SNTP SDK 调用只位于 Time Sync 插件，日志无密码。
- 记录 RAM/Flash 构建占用和编译 warning。
- 将所有可执行真机步骤逐项记录结果；未执行或用户尚未确认的项目明确标记为待验证，不声明生产可用。
- 每完成一个任务更新复选框并追加 changelog，不覆盖历史记录。

**自动化验证：**

- 测试目标：覆盖新增逻辑和既有核心回归。
- 测试用例 / 脚本：运行 `tests/host/` 中调度、Time Sync、persistence、WiFi、setting、JSON 和 Portal 校验全部测试；检查 HTML JavaScript；扫描过时 RTC 自动初始化、阻塞 NTP、重复无线 API 和敏感日志引用。
- 执行命令：逐个执行 host test 编译/运行命令；提取页面脚本后 `node --check`；`git diff --check`；`/Users/naaran/.platformio/penv/bin/pio run -e esp32-c3-zero`。
- 验证局限：自动化无法证明 DS1302、真实 NTP/DNS、Light Sleep、AP_STA 并发、80 MHz 响应和手机布局。

**人工验证关注点：**

- 调度：07:59 不触发，08:00 分钟内一次触发，08:01 不补执行，同日调回 08:00 不再触发，次日恢复。
- 重启：派发后立即重启，当日不再次派发。
- 网络：OFF、空凭据、错误凭据、无互联网、成功和 Portal 并发；每个终态核对 STA consumer 与 Light Sleep 恢复。
- SNTP/RTC：最多两个 attempt，旧系统时间不冒充成功，成功写入正确日期/星期/秒，RTC 写失败不重试。
- RTC 故障：固定 `12:00` + `RTC READ FAIL`，不自动写入，恢复后自然显示有效时间。
- Portal：`never` / UTC+8 成功文本、异时区和窄屏布局。
- 回归：CLOCK、TIMER、SETTING、Portal、按钮、旋钮、夜间息屏、手动 RTC 设置持续可用且无明显停顿。

**待确认问题：** 无

**执行记录（2026-08-09）：** README 已补充固定 08:00 精确分钟且不补执行、派发即记账、最多两个 SNTP attempt、AUTO 按需网络、RTC 单次提交、Portal 最后成功展示和 RTC invalid `12:00` 故障语义。七组宿主测试均以 `-Wall -Wextra -Werror` 编译并以退出码 0 完成：scheduled task、Time Sync、task persistence、WiFi、setting、JSON writer、Portal validation；页面脚本经 `node --check` 以退出码 0 完成，`git diff --check` 无输出。源码扫描确认无线生命周期写操作仅在 `wifi_service.cpp`，Portal 只读取 SoftAP IP；SNTP SDK 调用仅在 `time_sync_task.cpp`；无阻塞 NTP、过时 RTC 自动初始化或密码日志引用。ESP32-C3 构建成功，编译 warning 为 0，RAM 42652 / 327680 bytes（13.0%），Flash 857622 / 1310720 bytes（65.4%）。

**真机验收记录（2026-08-09）：** `pio device list` 未发现 ESP32 串口，仅列出系统调试和蓝牙端口，以下项目均未执行并保持待验证；不据此声明生产可用。

- [ ] 调度：07:59 不触发，08:00 分钟内一次触发，08:01 不补执行，同日调回 08:00 不再触发，次日恢复。
- [ ] 重启：派发后立即重启，当日不再次派发；成功日期修正后重启仍去重。
- [ ] 网络：OFF、空凭据、错误凭据、无互联网、成功及 Portal 并发终态的 STA consumer 和 Light Sleep 恢复。
- [ ] SNTP/RTC：最多两个 attempt、旧系统时间隔离、正确日期/星期/秒、单次写入和失败不重试。
- [ ] RTC 故障：固定 `12:00`、`RTC READ FAIL`、不自动写入及硬件恢复后自然回归。
- [ ] Portal：`never` / UTC+8 成功文本、异时区一致性和窄屏无重叠。`browser-act` 仅配置了需显式授权的 `chrome-direct`，本轮未控制用户浏览器。
- [ ] 回归：CLOCK、TIMER、SETTING、Portal、按钮、旋钮、夜间息屏和手动 RTC 设置持续可用且无明显停顿。
