# 技术设计：最后成功对时状态展示

> 状态：已确认

日期：2026-08-10  
Feature：time_sync_status_display  
阶段：Phase 2

## 设计目标

在不改变 NTP、RTC、持久化和 WiFi 生命周期的前提下，扩展现有 SETTING 状态机，提供最后成功对时的 OLED 只读详情页；收敛 Portal 的字段文案和缓存行为，使源码、固件嵌入页面、API 与浏览器实际展示可端到端验证。同时将息屏计划的用户可见名称统一为 `SCREEN SCHEDULE`。

## 采用方案

### 1. 扩展现有 SETTING 状态机

在现有菜单枚举和 `SettingState` 中分别新增一项，不建立新的页面路由、通用 UI 框架或页面对象：

```cpp
enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
  TimeSync,
  NightScreenOff,
  WifiConfig,
  WifiPolicy,
};

enum class SettingState : uint8_t {
  SettingMenu,
  BrightnessEdit,
  TimeEditHour,
  TimeEditMinute,
  TimeSyncInfo,
  // existing states...
};
```

`NightScreenOff` 等内部枚举名称继续保留，只调整它对应的用户可见标签。这样可避免无业务价值的全局符号改名，也不会触及持久化或配置 API。

选择该方案的原因：

- 与现有 Brightness、Time Set、WiFi Policy 等页面的控制和渲染方式一致。
- 新页面只有一个只读状态，不值得引入通用页面抽象。
- `settingMenuMove()` 和 `settingMenuWindowStart()` 已按菜单数量工作，只需将数量从 5 调整为 6 并扩展测试。

### 2. 直接读取 AppState 并复用时间格式化

`TimeSyncInfo` 不保存时间副本。每次 OLED 重绘时直接读取 `AppState.lastTimeSyncSuccessEpoch`，调用 `timeSyncFormatLocalEpoch()` 生成 `YYYY-MM-DD HH:MM:SS`：

- 格式化失败或 epoch 为 0：展示 `NEVER`。
- 格式化成功：在固定索引处分隔日期和时间，分别展示 `YYYY-MM-DD` 与 `HH:MM:SS`。

formatter 的输出契约固定为 19 字符，因此可在本地 `char[20]` 中安全地将索引 10 的空格替换为 `\0`，日期从首地址读取、时间从索引 11 读取。调用前仍以 formatter 返回值为准，不自行解释 epoch。

选择直接读取而非进入页面时缓存，是为了让同步完成后的重绘自然取得新值，并避免两份状态失去一致性。

### 3. 在成功发布点显式标记 OLED 重绘

`timeSyncTaskUpdate()` 已在完整成功后更新 `app.lastTimeSyncSuccessEpoch`，但当前没有同时设置 `app.displayDirty`。在同一成功分支中设置 `app.displayDirty = true`：

```text
RTC 写入、回读、持久化完整成功
  -> app.lastTimeSyncSuccessEpoch = pendingEpoch
  -> app.displayDirty = true
```

该更新点是最后成功时间进入 AppState 的唯一生产发布点。无论当前位于 CLOCK、TIMER、SETTING 菜单或 `TimeSyncInfo`，重绘都是无副作用的；只有详情页会显示该值。失败分支不修改 epoch，也不为本需求额外触发重绘。

不新增事件总线、观察者或定时刷新，因为单个状态更新点已足够表达需求。

### 4. Portal 保持现有 API，补齐页面与缓存交付

保留现有响应结构：

```json
{
  "timeSync": {
    "lastSuccess": "2026-08-10 08:00:05"
  }
}
```

无成功记录时 `lastSuccess` 继续为 JSON `null`。页面仅将该字段赋给 `<output>`，不使用浏览器 `Date`，也不加入保存 payload。

页面变更：

- `Last time sync` 改为 `Last successful sync`。
- `Night screen` 改为 `Screen schedule`。
- 保持现有 Device 区域、窄屏 grid 和 `load()` 数据加载方式。

HTTP 响应变更：

- Portal 根页面 `/` 在 `send_P()` 前发送 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`。
- 同时发送兼容旧客户端的 `Pragma: no-cache` 与 `Expires: 0`。
- 动态的 `GET /api/config` 使用相同的 no-store 策略，确保页面刷新后读取设备当前值，而不是浏览器缓存的旧配置响应。
- 不给静态页面增加版本化 URL，因为设备入口固定为 `http://192.168.4.1/`，禁用缓存更符合临时本地配置页语义。

缓存头通过 `wifi_portal.cpp` 内部的小型 helper 复用，仅服务根页面和配置 GET；不改变 POST、扫描、连接测试或错误响应语义。

### 5. 用户可见文案统一，内部契约保持不变

统一以下外部文案：

| 位置 | 旧文案 | 新文案 |
| --- | --- | --- |
| OLED SETTING 菜单 | `NIGHT OFF` | `SCREEN SCHEDULE` |
| OLED 息屏编辑页 | `NIGHT OFF` | `SCREEN SCHEDULE` |
| Portal fieldset | `Night screen` | `Screen schedule` |
| README | `NIGHT OFF` / 夜间息屏 | `SCREEN SCHEDULE` / 息屏计划 |

以下内部标识保持不变：

- `SettingMenuItem::NightScreenOff`
- `NightScreenOffConfig`
- `nightScreenOffEnabled`、`nightScreenOffMinute`、`nightScreenOnMinute`
- persistence key / blob 和 Portal API 的 `night` 对象字段

这样只改变用户理解，不引入数据迁移、API 破坏或大范围机械重命名。

## 页面与交互设计

### SETTING 菜单

六项顺序如下：

```text
BRIGHTNESS
TIME SET
TIME SYNC
SCREEN SCHEDULE
WIFI CONFIG
WIFI
```

菜单仍显示三行。`settingMenuWindowStart()` 继续保证当前选中项可见，首尾循环由现有 `wrapIndex()` 逻辑处理。

### TIME SYNC 详情页

`renderSetting()` 在 `TimeSyncInfo` 状态使用 `TIME SYNC` 作为 header；内容布局为：

```text
TIME SYNC          HH:MM


LAST SUCCESS
2026-08-10
08:00:05


```

从未成功时：

```text
TIME SYNC          HH:MM


LAST SUCCESS
NEVER



```

具体横向位置复用现有 header 和居中行 API。所有正文最长为 `LAST SUCCESS` 的 12 字符，低于约 21 字符限制。

交互矩阵：

| 状态 | Confirm | Cancel | 旋钮 |
| --- | --- | --- | --- |
| 菜单选中 `TIME SYNC` | 进入 `TimeSyncInfo` | 按现有行为退出 SETTING | 按现有行为移动菜单 |
| `TimeSyncInfo` | 无操作 | 返回 `SettingMenu` | 无操作 |

进入和离开详情页沿用 `invalidatePageLayout()`，确保 header 和固定行缓存被清理。`selectedItem` 不修改，因此 Cancel 后自然保留 `TIME SYNC` 选中位置。

## 模块与数据流

```text
persistenceLoadLastTimeSyncSuccessEpoch()
                   |
                   v
AppState.lastTimeSyncSuccessEpoch <----- Time Sync 完整成功
                   |                          |
                   |                          +-> displayDirty = true
                   |
         +---------+----------------+
         |                          |
         v                          v
timeSyncFormatLocalEpoch()   GET /api/config
         |                          |
         v                          v
OLED TimeSyncInfo            Portal <output>
日期 + 时间 / NEVER          完整文本 / never
```

涉及模块：

| 模块 | 设计变更 |
| --- | --- |
| `src/setting_logic.h` | 新增 `TimeSync` 菜单项，菜单数改为 6 |
| `src/app_state.h` | 新增 `SettingState::TimeSyncInfo` |
| `src/app_controller.cpp` | 增加进入、Confirm 无操作、Cancel 返回、旋钮无操作分支 |
| `src/ui_render.cpp` | 六项标签、Time Sync 详情渲染、`SCREEN SCHEDULE` 文案 |
| `src/time_sync_task.cpp` | 完整成功更新 epoch 时标记显示脏 |
| `src/wifi_portal.cpp` | 根页面和配置 GET 增加 no-store 响应头 |
| `web/wifi_portal.html` | 两处用户可见文案收敛 |
| `tests/host/test_setting_logic.cpp` | 六项循环和窗口边界测试 |
| `README.md` | 更新 SETTING 与 Portal 用户可见名称和最后成功入口说明 |

## 接口、类型与状态设计

### 类型变化

- `SettingMenuItem::TimeSync`：插入在 `TimeSet` 和 `NightScreenOff` 之间。
- `SETTING_MENU_ITEM_COUNT = 6`。
- `SettingState::TimeSyncInfo`：只读显示状态，不携带额外字段。

不新增或修改持久化类型、网络 API schema、Time Sync result 类型及 RTC 类型。

### 函数边界

- OLED 格式化留在 `ui_render.cpp` 的专用渲染函数内，直接调用公共 `timeSyncFormatLocalEpoch()`；该逻辑单次使用且简单，不新增通用 view model helper。
- HTTP no-store header 可由 `wifi_portal.cpp` 内部 helper 统一追加，避免根页面与配置 GET 重复三次 header 调用。
- 不改变 `timeSyncTaskUpdate()` 签名；其已持有可变 `AppState &`，可在成功发布 epoch 时同时设置 `displayDirty`。

## 公共能力复用评估

### 检索范围

已检索：

- `src/app_state.h`、`src/app_controller.cpp`、`src/setting_logic.*`
- `src/ui_render.cpp`、`src/display.*`
- `src/time_sync_logic.*`、`src/time_sync_task.cpp`、`src/main.cpp`
- `src/wifi_portal.cpp`、`web/wifi_portal.html`、`platformio.ini`
- `tests/host/test_setting_logic.cpp`、Time Sync / JSON / Portal 相关宿主测试
- `specs/20260809_ntp_time_sync/` 与 `specs/20260807_wifi_remote_settings/`

### 复用结论

| 能力 | 结论 | 差距处理 |
| --- | --- | --- |
| 菜单循环与三行窗口 | 复用 | 只扩展枚举数量和测试 |
| 设置交互状态机 | 扩展 | 新增一个无数据的只读状态 |
| 页面清理与重绘 | 复用 | 进入/退出调用现有 invalidation，成功时设置 dirty |
| UTC+8 时间格式化 | 直接复用 | 不新增 OLED 专用时区转换 |
| 最后成功运行态/NVS | 直接复用 | 不新增字段或 view model |
| Portal API | 直接复用 | schema 和保存 payload 不变 |
| HTML 嵌入 | 直接复用 | 补充响应缓存头及实际产物验证 |
| HTTP 缓存 header | 新增局部 helper | WebServer 无项目级公共封装，局部复用足够 |

## 实现约束

- 仅在完整成功 epoch 发布点增加重绘标记；不得改变任何失败终态或持久化顺序。
- `TimeSyncInfo` 的 Confirm 和旋钮分支应显式无操作，避免落入其他编辑状态。
- 所有新增 enum 值必须补齐 `app_controller.cpp`、`ui_render.cpp` 的相关 switch。
- 菜单标签缓冲区使用项目已有的 `AppConfig::LINE_CACHE_LEN`，确保容纳 `> SCREEN SCHEDULE`、终止符及合理余量。
- HTTP header 应在对应 `send()` / `send_P()` 前设置，且只作用于当前响应。
- 用户可见文案更名不扩展到内部符号，不改 persistence、API schema 或 JavaScript 表单字段。
- 注释仅用于解释“完整成功后重绘”和“固定地址 Portal 禁用缓存”的业务原因；枚举分支和简单渲染不增加叙述性注释。

## 验证设计

### 自动化验证

- 扩展 setting host test，覆盖 6 项正反向首尾循环、多步移动以及每个菜单项的三行窗口起点。
- 运行 Time Sync host test，确认 0 epoch 和合法 epoch 的 formatter 契约未回归。
- 对 Portal HTML 执行脚本语法检查，静态断言 `Last successful sync`、`Screen schedule`、`never` 与目标 DOM 存在，且同步字段未使用浏览器日期转换、未进入保存 payload。
- 对生产 UI 和 README 做用户可见文案扫描，确认不再出现 `NIGHT OFF` / `Night screen`；内部兼容标识不作为失败条件。
- 对 `wifi_portal.cpp` 检查根页面和配置 GET 的 no-store header；该检查只能证明实现存在，真实 header 由真机 HTTP 验收确认。
- 运行所有相关宿主测试及 PlatformIO 构建；构建仅验证 enum/switch、嵌入页面和目标 SDK 兼容性。

### 人工验证

1. 在无成功记录设备进入 `SETTING -> TIME SYNC`，确认 `NEVER`、按键行为和无网络副作用。
2. 在有成功记录设备确认日期、时间和 Portal 完整文本对应同一 UTC+8 时刻。
3. 停留在详情页完成真实对时，确认不退出页面即可刷新。
4. 成功后重启，再制造一次失败，确认两端恢复并保留原成功值。
5. 遍历六项菜单，确认 `SCREEN SCHEDULE` 可见、三行窗口和首尾循环正确，原息屏配置可编辑并保持。
6. 使用 `curl -i http://192.168.4.1/` 和 `/api/config` 检查实际 no-store header、HTML 与 JSON。
7. 使用曾访问旧固件 Portal 的手机打开新固件页面，检查实际 DOM、`Last successful sync`、`Screen schedule` 和窄屏布局，无需清缓存。

## 主要风险与规避

| 风险 | 规避方式 |
| --- | --- |
| 插入菜单项后标签数组和 enum 错位 | 使用相同枚举顺序，补齐每项导航/窗口测试和完整 switch 编译 |
| `SCREEN SCHEDULE` 用尽旧 `char line[18]` 的可见容量 | 改用已有 `AppConfig::LINE_CACHE_LEN`，避免截断并保留文案余量 |
| 详情页停留时成功值不刷新 | 在唯一成功发布点显式设置 `displayDirty`，真机停留验收 |
| 时间字符串切分依赖错误格式 | 只在公共 formatter 成功后按其 19 字符固定契约切分，并保留 formatter 测试 |
| 浏览器仍显示旧 Portal | 根页面和配置 GET 禁用缓存，同时核对刷入版本、响应 HTML、DOM 与 API |
| 文案更名误触数据兼容 | 只改用户可见字符串，内部标识、NVS 和 API 不变 |
| 自动检查被误当作真机通过 | tasks 中单列实际 OLED、HTTP header、浏览器 DOM 和 NTP 验收，未经确认不声明完整通过 |

## 待确认问题

无
