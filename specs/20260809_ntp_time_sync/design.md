# 技术设计：定时任务插件与 NTP 自动对时

> 状态：已确认

日期：2026-08-09
Feature：ntp_time_sync
阶段：Phase 2

## 方案概述

采用“纯调度逻辑 + 调度 service + 明确插件状态机”的三层方案：

1. `scheduled_task_logic` 只处理日期键、精确分钟匹配、当日去重和结果类型，不依赖 Arduino。
2. `scheduled_task_service` 持有固定容量运行态，读取 RTC 和持久化的已尝试日期，决定是否派发稳定的 `ScheduledTaskId`。
3. `time_sync_task` 实现 NTP 插件的非阻塞业务状态机，拥有并释放 `WifiConsumer::TimeSync`，调度器不理解 WiFi、SNTP 或 RTC 写入细节。

首期不建立动态插件注册表。`ScheduledTaskId::TimeSync` 通过明确 `switch` 分发；稳定 ID、统一运行结果和 start/update/take-result 生命周期形成可复用契约。出现第二类插件后，可以把显式分发演进为编译期描述表，不改变纯调度和插件资源所有权。

每日规则只在 RTC 处于 `08:00:00..08:00:59` 时匹配，不使用“大于等于 08:00”，因此晚启动、Light Sleep 错过整分钟或后续 WiFi 恢复均不会补执行。任务匹配后先把当天日期写为 `lastAttemptDateKey`，写入成功才派发插件；该记录同时阻止主循环重复派发和执行中重启后的再次派发。

## 采用原因

- 符合现有 `Begin` / `Update` service 和主循环轮询模式，不引入 RTOS task 或动态分配。
- 调度规则、插件生命周期和业务状态机职责清楚，后续任务可复用日期去重和资源租约。
- 派发前持久化提供明确的 at-most-once 语义；即使插件失败、断电或重启，当天也不再派发。
- 显式状态机可以持续推进输入、Portal、Timer、显示和休眠判断。
- 只抽取已经明确稳定的公共边界，不为一个插件建立回调注册平台。

## 涉及模块

| 模块 | 变更 | 职责 |
| --- | --- | --- |
| `src/scheduled_task_logic.h/.cpp` | 新建 | 稳定任务 ID、日期键、精确分钟到期判断、通用任务结果 |
| `src/scheduled_task_service.h/.cpp` | 新建 | 固定容量运行态、持久化恢复、派发即记账、终态消费 |
| `src/time_sync_logic.h/.cpp` | 新建 | NTP 状态转换、attempt 判断、epoch 到固定 UTC+8 `RtcTime` 转换及格式化 |
| `src/time_sync_task.h/.cpp` | 新建 | WiFi/SNTP/RTC 非阻塞插件状态机和资源释放 |
| `src/rtc.h/.cpp` | 调整 | 让 `RtcTime` 头文件脱离 Arduino 依赖，供宿主纯逻辑复用 |
| `src/rtc_service.h/.cpp` | 调整 | 删除编译时间自动写入；无效 RTC 保持无效并继续短周期读取 |
| `src/app_state.h` | 调整 | 删除 RTC 自动初始化状态；增加最后成功对时 epoch 运行视图 |
| `src/wifi_service.h` | 扩展 | 增加稳定的 `WifiConsumer::TimeSync` bit |
| `src/persistence.h/.cpp` | 扩展 | 读取/保存任务已尝试日期和最后成功 epoch |
| `src/persistence_codec.h/.cpp` | 扩展 | 两个 V1 定长 blob 的纯编解码和版本校验 |
| `src/config_timing.h`、`src/config_network.h` | 扩展 | 固定 08:00、attempt、deadline、NTP server 和 UTC+8 常量 |
| `src/main.cpp` | 调整 | 初始化记录并按确定顺序协调 scheduler、WiFi 和 NTP 插件 |
| `src/wifi_portal.cpp` | 扩展 | 配置 GET API 增加只读最后成功时间 |
| `web/wifi_portal.html` | 扩展 | Device 区域展示 `never` 或固定 UTC+8 文本 |
| `src/ui_render.cpp` | 调整 | RTC 无效时所有当前时间显示使用 `12:00`，故障状态文字保留 |
| `tests/host/` | 扩展 | 调度、转换、状态机和 blob 的宿主测试 |
| `README.md` | 更新 | 同步每日对时、精确触发、RTC 故障展示和模块说明 |

## 公共插件契约

### 稳定标识与结果

```cpp
enum class ScheduledTaskId : uint8_t {
  TimeSync = 0,
  Count,
};

enum class TaskRunStatus : uint8_t {
  Idle,
  Running,
  Succeeded,
  Failed,
};

struct TaskRunResult {
  TaskRunStatus status = TaskRunStatus::Idle;
  uint32_t completedDateKey = 0;
};
```

- `ScheduledTaskId` 的数值同时作为固定持久化数组下标，已有值不得重排。
- `Count` 只用于容量和边界检查，不作为可派发 ID。
- 插件公开 `Begin`、`Start`、`Update`、`TakeResult`；`Start` 只接受一轮新执行，`Update` 不阻塞，`TakeResult` 对终态只消费一次。
- 首期调度 service 使用 `switch (id)` 调用 Time Sync；不新增函数指针表、继承体系或堆对象。

### 资源所有权

- 插件成功申请的每项资源必须由同一插件在统一终态收口函数中释放。
- 调度器不直接操作 WiFi 或 SNTP。
- Time Sync 只清除 `WifiConsumer::TimeSync`；其他 consumer 和 Portal AP 不受影响。
- `Start` 在策略拒绝或无凭据时也产生一次可消费的 Failed 结果，但不会登记 WiFi consumer。

## 调度设计

### 编译期配置

```cpp
static constexpr uint16_t TIME_SYNC_DAILY_MINUTE = 8 * 60;
```

首期 `DailyTaskDefinition` 固定包含：

```cpp
struct DailyTaskDefinition {
  ScheduledTaskId id;
  uint16_t minuteOfDay;
};
```

定义数组元素数量为 `ScheduledTaskId::Count`，通过 `static_assert` 保证 ID 落在固定 4 槽容量内。后续每日任务可在不改变 V1 blob 尺寸的前提下增加到 4 个；超过容量时需要显式升级持久化版本。cron、每周和动态时间不属于该类型。

### 到期判断

```text
RTC 读取成功且 RtcTime.valid
AND hour * 60 + minute == definition.minuteOfDay
AND lastAttemptDateKey[id] != YYYYMMDD
AND 当前没有运行中的该任务
```

不比较秒，不要求主循环命中 `08:00:00`；现有 RTC 正常读取最长 30 秒，正常运行或定时唤醒应至少在 08:00 分钟内观察一次。若整分钟均未观察到，则当天不执行。

### 派发即记账

```text
匹配 08:00
  -> 先更新 RAM lastAttemptDateKey
  -> 保存 ScheduledTaskRecordsV1
  -> 保存成功：返回 TimeSync dispatch
  -> 保存失败：本次开机仍由 RAM 阻止重复，不启动插件并记录日志
```

先持久化、后启动可保证正常 NVS 条件下的 at-most-once：任务执行中重启也不会重跑。若 NVS 写入失败，无法跨重启保证去重；设计选择不启动网络任务，避免在无法记账时执行可能重复的工作。

当 Time Sync 成功校正日期后，scheduler 用 `rtcServiceForceRead()` 得到的日期更新 `lastAttemptDateKey[TimeSync]`。若校正前后日期不同，再保存一次调度 blob，防止按真实日期重复派发；该异常修正写入不属于常态每日一次写入。

### 手动调时

- 一轮任务已派发后，当天日期键阻止手动调回 08:00 造成再次派发。
- 若当天尚未派发，用户主动把 RTC 设置到 08:00，RTC 本身成为有效调度时间源，会触发当天唯一一轮。这仍符合“仅看时间”的规则，不增加 UI 或 WiFi 触发源。

## NTP 插件状态机

### 状态与终态原因

```cpp
enum class TimeSyncPhase : uint8_t {
  Idle,
  WaitingForWifi,
  WaitingForSntp,
  RetryDelay,
  WritingRtc,
  Succeeded,
  Failed,
};

enum class TimeSyncFailure : uint8_t {
  None,
  DisabledByPolicy,
  MissingCredentials,
  WifiFailed,
  WifiTimedOut,
  SntpTimedOut,
  InvalidNetworkTime,
  RtcWriteFailed,
  RtcReadbackFailed,
  ResultPersistenceFailed,
};
```

状态、failure、attempt、deadline、是否持有 consumer 和本轮 epoch 保存在 `TimeSyncTaskState`。失败原因只存在 RAM 和受控串口日志中。

### 状态流

```text
Idle
  -> Start
     -> policy OFF / no credentials -> Failed
     -> request consumer -> WaitingForWifi

WaitingForWifi
  -> Connected -> start attempt 1 -> WaitingForSntp
  -> Failed / policy OFF / credentials cleared / 20s deadline -> Failed

WaitingForSntp
  -> current attempt COMPLETED -> validate epoch -> WritingRtc
  -> disconnected or 10s deadline
       -> attempt < 2 -> stop SNTP -> RetryDelay
       -> attempt == 2 -> Failed

RetryDelay
  -> policy/config invalid or WiFi total deadline exhausted -> Failed
  -> delay 3s and WiFi connected -> start next attempt -> WaitingForSntp
  -> delay reached but WiFi pending -> WaitingForWifi（保留下一 attempt）

WritingRtc
  -> rtcSetTime once + force read + persist epoch -> Succeeded
  -> 任一步失败 -> Failed

Succeeded / Failed
  -> stop SNTP if active
  -> release TimeSync consumer if held
  -> expose one-shot TaskRunResult
  -> Idle after result is consumed
```

WiFi 总 deadline 从 `Start` 计算 20 秒，不因 WiFi service 内部 30 秒重连或插件状态切换而延长。第一次连通后若断网，当前 SNTP attempt 失败；仍有 attempt 且总 deadline 未到时才等待重连，否则结束。

### SNTP 本轮成功证明

目标 PlatformIO `espressif32 6.13.0` 使用 Arduino-ESP32 `2.0.17`，本地 SDK 已确认提供：

- `esp_sntp_get_sync_status()`
- `esp_sntp_set_sync_status()`
- `esp_sntp_stop()`
- `configTime()`

每个 attempt 按以下顺序执行：

```text
停止可能残留的 SNTP
设置 SNTP_SYNC_STATUS_RESET
configTime(UTC+8, 0, server1, server2, server3)
轮询 esp_sntp_get_sync_status()
仅 COMPLETED 视为本轮成功
```

不调用默认阻塞等待的 `getLocalTime()`。显式 RESET 使旧系统时间和上一轮完成状态不能冒充本轮结果。读取到 COMPLETED 后立即取得 UTC epoch，再停止 SNTP。

配置服务器保持：

```cpp
"ntp.aliyun.com"
"pool.ntp.org"
"time.google.com"
```

## 时间转换与 RTC 写入

### 固定 UTC+8 转换

系统 `time()` 返回 UTC epoch。转换和 Portal 格式化统一使用：

```text
localEpoch = utcEpoch + 8 * 3600
gmtime_r(localEpoch)
```

不依赖浏览器时区，也不依赖调用点当前 `TZ` 环境。`tm_wday` 的 `0..6` 映射为 DS1302 的 `7,1..6`，与项目现有星期语义一致。

`timeSyncEpochToRtc()` 校验：

- UTC epoch 非 0，转换无溢出。
- 本地年份为 2000..2099。
- 月、日、时、分、秒及真实日历组合有效。
- 星期为 1..7。

### RTC 一次写入

```text
rtcSetTime(convertedRtc) 一次
  -> false：RtcWriteFailed，不重试
rtcServiceForceRead() 一次
  -> false / invalid：RtcReadbackFailed，不重试
保存 lastSuccessEpoch
  -> 失败：ResultPersistenceFailed，不重试
成功：更新 AppState.lastTimeSyncSuccessEpoch，返回校正后的日期键
```

`rtcSetTime()` 自身已有约 20 ms 写后寄存器确认，本期不再增加第二次 RTC 写入。最后成功 epoch 只在完整写入、回读和 NVS 保存成功后更新到 AppState。

## RTC 无效行为调整

现有编译时间自动初始化与确认后的需求冲突，本期删除：

- `RtcAutoInitState`
- `autoInitAttempted` / `autoInitDueMs`
- `RTC_AUTO_INIT_DELAY_MS`
- `buildTimeToRtcTime()` 和 `updateAutoInit()`

RTC service 读取失败时：

- 保持 `app.rtcOk=false` 和 `app.rtcTime.valid=false`。
- 不调用 `rtcSetTime()`，不猜测日期，scheduler 不派发。
- 每 `RTC_SHORT_REFRESH_MS` 继续读取；重启或硬件恢复后读到有效值即自然恢复。
- CLOCK 主时间和其他“当前时间”位置显示固定 `12:00`。
- CLOCK 日期/状态行继续展示 `RTC READ FAIL`，避免 `12:00` 被误认为真实时间。
- 用户通过本机 TIME SET 或 Portal 显式提交完整时间的既有写入能力保留。

## 持久化设计

继续复用 `focusclock` Preferences namespace，新增两个独立版本 blob：

```cpp
struct PersistedScheduledTaskRecordsV1 {
  uint8_t version;
  uint8_t slotCount;
  uint8_t reserved[2];
  uint32_t lastAttemptDateKeys[4];
};

struct PersistedTimeSyncResultV1 {
  uint8_t version;
  uint8_t reserved[3];
  uint32_t lastSuccessEpoch;
};
```

建议 key：

- `taskRuns`：调度记录。
- `timeSync`：最后成功对时结果。

规则：

- blob 使用 `static_assert` 固定布局，reserved 写 0。
- size、version、固定 `slotCount=4`、日期键和 epoch 任一非法时，整份 blob 回退为空记录。
- 日期键必须是可解析的 `YYYYMMDD` 或 0；epoch 必须转换为 DS1302 支持年份内的 UTC+8 时间或为 0。
- 保存完整写入后才更新 persistence 层缓存。
- 每日常态写一次 `taskRuns`；成功时另写一次 `timeSync`。只有 NTP 修正本地日期时可能再次写 `taskRuns`。
- 不持久化 phase、failure、deadline、attempt 或 consumer 状态。

## AppState 与 Portal API

`AppState` 只增加页面所需的稳定结果：

```cpp
uint32_t lastTimeSyncSuccessEpoch = 0;
```

插件详细状态仍由 `TimeSyncTaskState` 持有，避免业务 service 状态膨胀 AppState。

`GET /api/config` 增加：

```json
{
  "timeSync": {
    "lastSuccess": "2026-08-09 08:00:05"
  }
}
```

从未成功时返回：

```json
{
  "timeSync": {
    "lastSuccess": null
  }
}
```

文本由设备端按固定 UTC+8 格式化，页面直接展示，避免 JavaScript `Date` 按手机时区二次换算。页面在现有 Device fieldset 增加普通只读 output 行，不创建新的嵌套卡片或编辑控件。

## 主循环与初始化顺序

### setup

```text
加载现有配置
加载 scheduled task records
加载 last time sync result 到 AppState
begin WiFi / Portal / RTC / scheduler / Time Sync / sleep
```

### loop

```text
input / timer
rtcServiceUpdate
scheduledTaskServiceUpdate
  -> 精确 08:00 匹配
  -> 持久化当天已尝试
  -> 返回 TimeSync dispatch
timeSyncTaskStart（同轮申请 consumer）

wifiServiceUpdate
timeSyncTaskUpdate（观察最新 WiFi 状态，推进 SNTP/RTC）
scheduledTaskServiceConsumeResult
  -> 成功时按回读 RTC 修正已尝试日期

wifiPortalUpdate
render / feedback / display power
sleepManagerMaybeEnter
```

任务在 WiFi update 前申请 consumer，使无线同轮开始切换；在 WiFi update 后读取最新连接状态。终态释放 consumer 后，WiFi service 下一轮关闭不再需要的 STA。consumer 存续期间现有 `networkTaskActive` 自动阻止 Light Sleep。

## 公共能力复用评估

| 能力类型 | 检索范围 | 现有实现 | 差距 | 决策 | 影响 |
| --- | --- | --- | --- | --- | --- |
| Service 生命周期 | `src/main.cpp`、`*_service.*` | `Begin` / `Update` + 显式状态结构 | 无通用插件结果消费 | 扩展 | 新插件采用同一轮询契约，不新增 RTOS task |
| 定时逻辑 | RTC、display power、sleep manager | 有分钟窗口和 RTC deadline，无每日任务去重 | 缺少日期键、精确分钟与持久记录 | 新建 | `scheduled_task_logic/service` 可供后续每日任务复用 |
| WiFi 资源 | `wifi_service.*`、`wifi_logic.*` | consumer bitmask、AUTO policy、统一 AP/STA 协调 | consumer 枚举尚无成员；request 不校验凭据 | 扩展 | 新增 TimeSync bit；插件启动前自行校验凭据，释放沿用现有 API |
| 休眠门禁 | `sleep_manager.*`、`WifiRuntimeView` | `networkTaskActive` 已阻止 Light Sleep | 无 | 复用 | 不新增 sleep blocker |
| deadline | `wifi_logic.*` | `wifiDeadlineReached()` 已有溢出安全实现 | time sync 纯逻辑不应反向依赖 WiFi 业务模块 | 扩展为共享约定 | Time Sync 使用同一有符号差值算法并覆盖回绕测试，不新增复杂工具层 |
| RTC 类型与写入 | `rtc.*`、`rtc_service.*` | `RtcTime`、字段校验、写后确认、force read | `rtc.h` 依赖 Arduino；无 epoch 转换 | 扩展 | 头文件改为标准整数依赖，新建纯转换逻辑，复用写入和回读 |
| SNTP | Arduino core、ESP-IDF/lwIP headers | `configTime`、状态 get/set、stop 均存在 | 无本轮隔离和插件状态机 | 新建局部适配 | 只在 `time_sync_task.cpp` 接触 SDK API |
| Preferences | `persistence.*`、`persistence_codec.*` | namespace、缓存、版本 blob codec | 无任务与结果 blob | 扩展 | 保持单入口和现有测试模式 |
| JSON | `wifi_portal.cpp`、`json_writer.*` | 定长响应和安全追加 | 无 nullable time sync 字段 | 复用并扩容 | 不新增 JSON 库；重新核算响应容量 |
| Portal UI | `web/wifi_portal.html` | Device fieldset、统一 load | 无最后成功展示 | 扩展 | 增加只读 output 和 `never` 回退 |
| RTC 故障 | `rtc_service.*`、`ui_render.cpp` | 自动写入编译时间，失败显示状态 | 与“不写错误时间”要求冲突 | 收敛现有能力 | 删除自动写入，固定 `12:00` 只作为显示值 |

## 实现约束

### 注释策略

- 派发前持久化需注释说明 at-most-once 意图和 NVS 失败时不启动的原因。
- SNTP attempt 前 RESET 和完成状态判断需说明其用于隔离旧系统时间。
- UTC epoch 加固定偏移再 `gmtime_r` 需说明不依赖全局时区或浏览器时区。
- consumer 统一释放和校正后日期覆盖需说明资源所有权及防重复意图。
- 普通状态赋值、switch 分支和字段映射不添加复述性注释。

### 封装边界

- 只把调度、日期、转换和状态判断放入纯逻辑模块；SDK 调用留在 `time_sync_task.cpp`。
- `scheduled_task_service` 不调用 WiFi/SNTP/RTC 写入，只派发 ID 并接收结果。
- 不为一个插件增加抽象基类、模板注册器或通用事件总线。
- Portal 只读取 AppState 稳定结果，不直接访问插件内部状态。

### 最小影响面

- WiFi service 仅增加 enum bit，不改变现有模式推导和连接测试。
- Sleep manager 不修改；复用 `networkTaskActive`。
- RTC 手动设置和 Portal 显式设置流程保持原样，只删除自动猜测写入。
- 页面只增加只读结果，不改变保存 payload。
- 不升级依赖、不修改 PlatformIO 版本、锁文件、CI 或部署配置。

## 自动化验证设计

### 调度宿主测试

- 07:59 不触发；08:00 任意秒触发；08:01 和晚启动不触发。
- Light Sleep/轮询从 07:59 跳到 08:00:30 可触发，跳到 08:01 不补执行。
- 同日 RAM 或持久化日期阻止重复；次日恢复资格。
- 运行中不重复；RTC 无效不触发。
- 派发记录保存失败时不启动且本次开机不循环尝试。
- 修正日期后更新到真实日期。

### Time Sync 纯逻辑测试

- OFF、无凭据、WiFi 失败/超时/断线。
- SNTP attempt 1 成功、首次超时后重试、第二次超时终止。
- 未收到 COMPLETED 时即使 epoch 有效也不得进入 RTC 写入。
- deadline 在 `millis()` 回绕前后正确。
- UTC+8 日期跨日、闰年、星期映射、2000/2099 边界及越界拒绝。
- RTC 写入、回读和结果持久化任一失败后不重试 RTC、不更新 last success。
- 每个持有 consumer 的终态都产生释放动作。

### Persistence 测试

- 两个 V1 blob 往返。
- 空记录、截断、错误 size/version/slotCount、非法日期键和越界 epoch 回退。
- reserved 字段编码为 0，任务 ID 下标稳定。

### 页面和集成检查

- 提取 `web/wifi_portal.html` 内嵌 JavaScript 执行 `node --check`。
- 检查 null 显示 `never`、有效文本原样展示且无 `new Date(lastSuccess)` 二次换算。
- 运行全部宿主测试。
- 执行 `/Users/naaran/.platformio/penv/bin/pio run`，验证 SNTP API、链接、RAM 和 Flash 容量。
- `git diff --check` 仅作为格式兜底，不替代逻辑测试。

### 真机人工验证

- 将 RTC 调到 07:59，确认 08:00 只派发一轮；当天再次调到 08:00 不派发。
- 设备从 07:59 睡眠后在 08:00 分钟内醒来可派发；08:01 后启动不补执行。
- 任务执行中重启，当天不再次派发。
- OFF、空凭据、错误凭据、NTP 不可达和成功场景均在终态关闭不再需要的 STA并恢复 Light Sleep。
- Portal 并发时 AP、HTTP、Cancel、输入和显示持续可用。
- 成功后 DS1302 日期、星期、时间及重启保持正确，页面展示固定 UTC+8 成功时间。
- 断开或制造 RTC 无效后，显示固定 `12:00` 与 `RTC READ FAIL`，串口确认固件未写 RTC；恢复硬件或重启后有效读取自然恢复。
- 网络等待和约 20 ms RTC 写入期间确认输入、Timer、OLED 和 HTTP 无明显停顿。

## 主要风险

| 风险 | 影响 | 规避方式 |
| --- | --- | --- |
| 精确分钟被整段错过 | 当天不执行 | 这是确认后的语义；复用最长 30 秒 RTC 定时唤醒，并用跳到 08:01 的测试证明不补执行 |
| NVS 在派发记账时失败 | 无法提供跨重启 at-most-once | RAM 当天去重且不启动插件；记录日志，避免未记账执行 |
| SNTP 完成状态残留 | 旧系统时间被误写 RTC | 每 attempt stop + RESET，只有本轮 COMPLETED 才读 epoch |
| WiFi service 继续内部重连 | 任务超过有限窗口或耗电 | 插件持有独立 20 秒总 deadline，终态主动释放 consumer |
| NTP 校正日期 | 真实日期可能再次派发 | 成功后用 RTC 回读日期覆盖任务已尝试日期 |
| Portal 与任务并发 | AP 短断或错误关闭 | 复用 consumer mask 和 AP_STA；只释放 TimeSync bit，真机并发验收 |
| RTC 自动初始化被移除 | 首次无效 RTC 不再自动获得近似时间 | 这是明确需求；固定 `12:00` + 故障文字暴露异常，保留手动/Portal 设置和周期恢复读取 |
| `uint32_t` epoch 上界 | 2106 后溢出 | DS1302 仅支持到 2099，codec 和转换在更早边界拒绝 |
| JSON 响应增长 | 固定缓冲溢出返回 500 | 增大并静态核算容量，保留 JsonWriter 失败检查 |
| 20 ms RTC 同步写 | 短暂影响输入或 HTTP | 不增加写入重试，真机验证可感知停顿 |

## 需求追踪

| 需求 | 设计点 |
| --- | --- |
| R-01 | 稳定 ID、统一结果、Begin/Start/Update/TakeResult、显式编译期分发 |
| R-02 | 精确分钟等值判断、固定 08:00、不补执行 |
| R-03 | 派发前保存 `lastAttemptDateKey`、启动中重启去重、校正日期覆盖 |
| R-04 | `WifiConsumer::TimeSync`、统一终态释放、复用 networkTaskActive |
| R-05 | 非阻塞状态机、20/10/3 秒配置、两个 attempt、SNTP stop/reset/status |
| R-06 | UTC epoch + 8 小时后 `gmtime_r`、年份/日期/星期校验 |
| R-07 | `rtcSetTime` 一次、force read 一次、失败不重试 |
| R-08 | `PersistedTimeSyncResultV1`、成功后 AppState 恢复 |
| R-09 | nullable `timeSync.lastSuccess`、设备端格式化、只读 output |
| R-10 | scheduler 在 WiFi 前、task update 在 WiFi 后、其他循环服务持续推进 |
| R-11 | RAM phase/failure、受控串口日志、不输出凭据 |
| R-12 | 删除编译时间自动写入、固定 `12:00` 故障展示、短周期自然恢复 |
| NFR-01 | 无长 delay，RTC 仅保留已有一次 20 ms 确认 |
| NFR-02 | 固定容量和值类型，无 RTOS task/动态插件/虚函数体系 |
| NFR-03 | 调度逻辑、插件生命周期、资源租约分离 |
| NFR-04 | 纯逻辑 host tests、JS 语法与 PlatformIO 构建 |
| NFR-05 | 常态每日一次 taskRuns 写入，不持久化瞬时状态 |
| NFR-06 | WiFi/Portal/休眠策略保持，RTC 自动猜测写入按确认需求移除 |

## 待确认问题

### 阻塞性问题

无

### 非阻塞假设

- A-01：若当天尚未派发，用户手动把有效 RTC 设置到 08:00 会触发当天唯一一轮；调度器只看 RTC 时间，不识别“自然走时”与“手动写入”。后续如需抑制，可在手动设置成功时增加显式 suppression 记录。
- A-02：派发记录 NVS 保存失败时，本次开机在 RAM 中视为当天已尝试但不启动 NTP；重启后是否再次尝试取决于 NVS 是否恢复，不为硬件存储故障增加额外 journal。
- A-03：最后成功时间由设备端格式化为 `YYYY-MM-DD HH:MM:SS`，页面语言沿用现有英文。
