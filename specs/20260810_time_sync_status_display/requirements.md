# 需求规格：最后成功对时状态展示

> 状态：已确认

日期：2026-08-10  
Feature：time_sync_status_display  
阶段：Phase 1

## 背景与目标

设备已经能够执行每日 NTP 自动对时，并持久化最后一次完整成功的 UTC epoch，但用户当前无法在 OLED 的 SETTING 页面查看该结果；Portal 源码虽然已有相关字段，实际设备页面中也未可靠观察到它。

本功能的目标是让用户在 OLED 和 WiFi 配置 Portal 中都能明确查看最后一次完整成功的对时时间，并确保两处使用同一数据源、固定 UTC+8 语义且在设备重启后保持一致。同时补齐 Portal 从源码、固件嵌入、HTTP 响应到浏览器实际 DOM 的交付验收。

## 业务语义

“最后成功对时时间”仅指最近一次满足以下全部条件的 NTP 对时结果：

1. 本轮 SNTP 明确完成。
2. 网络时间合法并成功转换为固定 UTC+8 的 RTC 时间。
3. DS1302 写入及强制回读成功。
4. 最后成功 epoch 持久化成功。

失败、超时、WiFi 策略关闭、凭据缺失、RTC 提交失败或结果持久化失败均不得覆盖已有成功时间。epoch 为 0 或持久化记录无效时，视为从未成功同步。

## 功能需求

### R-01：OLED 设置菜单入口

- SETTING 菜单应提供一个名为 `TIME SYNC` 的只读查看入口。
- 该入口应位于 `TIME SET` 之后，使时钟相关项目相邻。
- 菜单扩展为 6 项后，应继续使用现有三行滚动窗口。
- 旋钮正向、反向移动以及菜单首尾循环行为应与现有菜单一致。
- 菜单顺序应为：`BRIGHTNESS`、`TIME SET`、`TIME SYNC`、`SCREEN SCHEDULE`、`WIFI CONFIG`、`WIFI`。

### R-02：OLED 最后成功详情页

- 用户在 `TIME SYNC` 菜单项按 Confirm 后，应进入只读详情页。
- 页面标题应为 `TIME SYNC`，字段标签应为 `LAST SUCCESS`。
- 存在成功记录时，应将固定 UTC+8 时间拆为 `YYYY-MM-DD` 和 `HH:MM:SS` 两行展示。
- 从未成功同步时，应展示 `NEVER`，且不展示伪造或默认时间。
- Cancel 应返回 SETTING 菜单，并保留 `TIME SYNC` 为当前选中项。
- Confirm 和旋钮在详情页不得触发同步、编辑、清除记录、网络请求、RTC 写入或其他业务副作用。

### R-03：OLED 展示刷新

- 每次进入详情页时，应根据当前 `AppState.lastTimeSyncSuccessEpoch` 生成展示内容。
- 用户停留在详情页期间，如果一轮 NTP 对时完整成功，页面应自动重绘并显示新的最后成功时间，无需退出后重新进入。
- 对时失败或中止时，页面不得清空或修改当前展示的已有成功时间。

### R-04：Portal 最后成功展示

- WiFi 配置 Portal 的 Device 区域应以只读方式展示字段 `Last successful sync`。
- 存在成功记录时，应展示设备端生成的固定 UTC+8 `YYYY-MM-DD HH:MM:SS` 文本。
- 从未成功同步时，应展示 `never`。
- Portal 不得使用浏览器 `Date` 或手机时区再次转换该字段。
- 该字段不得成为输入控件，不得加入配置保存 payload，也不得改变现有配置保存行为。
- 用户刷新或重新打开 Portal 后，应看到设备当时的最新值；已打开页面期间无需自动轮询或推送更新。

### R-05：Portal 实际交付一致性

- 固件构建应继续将当前 `web/wifi_portal.html` 嵌入固件。
- 设备提供 Portal 根页面时，应使用禁用旧页面复用的 HTTP 缓存策略，使同一设备地址上的旧页面不会长期隐藏新增字段。
- 验收应同时检查设备实际返回的根页面、浏览器实际 DOM 和 `/api/config` 响应，不能只检查工作区源文件。
- 当 API 已返回最后成功时间但页面未展示时，应能够区分并排查烧录固件版本、嵌入页面产物和浏览器缓存问题。

### R-06：数据一致性与重启恢复

- OLED 和 Portal 应读取同一个 `lastTimeSyncSuccessEpoch` 数据源，并采用相同的固定 UTC+8 转换规则。
- 设备重启后，两处应继续展示重启前持久化的最后成功时间。
- 后续同步失败不得清空或覆盖已有成功时间。
- epoch 为 0 或持久化记录无效时，两处应分别显示 `NEVER` 和 `never`。

### R-07：息屏计划文案统一

- OLED SETTING 菜单中原有 `NIGHT OFF` 应更名为 `SCREEN SCHEDULE`，准确表达该功能按时间关闭并恢复屏幕的语义。
- 息屏计划编辑页的标题应同步使用 `SCREEN SCHEDULE`，原有 `OFF AT`、`ON AT` 和开关含义保持不变。
- Portal 中对应区域标题应由 `Night screen` 更名为 `Screen schedule`。
- README 等面向用户的功能说明应使用“息屏计划”或 `SCREEN SCHEDULE`，不再将该功能称为 `NIGHT OFF`。
- 内部类型、状态字段、持久化 key 和 API 字段不要求随 UI 文案重命名；本次更名不得造成数据迁移或兼容性变化。

## 非功能需求

### NFR-01：最小影响面

- 应复用现有 AppState、NVS 记录、时间格式化、设置导航和 Portal 配置 API。
- 不得新增持久化字段，不得改变 NTP 调度、WiFi consumer、RTC 提交或 Light Sleep 语义。
- 不应为单一只读页面引入新的通用页面框架或网络刷新机制。

### NFR-02：显示适配

- OLED 文本应适配 128x64 屏幕、现有 5x7 ASCII 字体和每行最多约 21 字符的限制。
- 页面文字不得与标题、其他行或屏幕边界重叠。
- Portal 的标签和值应在常用手机窄屏下保持可读，不得横向溢出或相互遮挡。

### NFR-03：只读与响应性

- 打开或停留在 OLED 详情页不得阻塞主循环。
- 展示操作不得主动申请网络、写 NVS 或写 RTC。
- Portal 应继续通过现有配置 GET API 读取该值，不新增 WebSocket、SSE 或后台轮询。

### NFR-04：兼容与回归

- CLOCK、TIMER、其他 SETTING 项、WiFi Portal 配置读写、每日 NTP 对时和输入反馈行为应保持不变。
- 新增枚举项、状态和渲染分支后，所有相关 switch 应保持完整且无未处理状态。

## 验收标准

### AC-01：OLED 从未同步

Given 设备没有合法的最后成功 epoch  
When 用户进入 `SETTING -> TIME SYNC`  
Then 页面显示 `LAST SUCCESS` 和 `NEVER`，且不启动 WiFi、不写 RTC、不写 NVS

### AC-02：OLED 展示成功记录

Given 设备存在合法的最后成功 epoch  
When 用户进入 `SETTING -> TIME SYNC`  
Then 页面按固定 UTC+8 分两行显示正确日期和秒级时间

### AC-03：OLED 导航与只读行为

Given 用户位于 `TIME SYNC` 详情页  
When 用户旋转旋钮、按 Confirm 或按 Cancel  
Then 旋钮和 Confirm 不产生业务副作用，Cancel 返回菜单且仍选中 `TIME SYNC`

### AC-04：停留期间即时刷新

Given 用户停留在 `TIME SYNC` 详情页  
When 本轮 SNTP、RTC 写入、强制回读和结果持久化完整成功  
Then OLED 自动重绘为新的最后成功时间

### AC-05：Portal 从未同步与成功记录

Given 设备分别处于无记录和有合法成功记录状态  
When 用户打开或刷新 WiFi 配置 Portal  
Then Device 区域分别显示 `never` 和设备端固定 UTC+8 的完整时间

### AC-06：两端一致与重启恢复

Given 一轮对时成功且 OLED、Portal 已显示结果  
When 设备重启并重新打开两个页面  
Then 两处继续显示同一成功事件对应的时间

### AC-07：失败不覆盖成功记录

Given 已存在一次成功时间  
When 后续自动对时失败、超时或 RTC 提交失败  
Then OLED 和 Portal 仍显示原成功时间

### AC-08：Portal 不复用旧页面

Given 手机曾访问过旧固件的 `http://192.168.4.1/`，且新固件包含目标字段  
When 用户再次打开 Portal  
Then 浏览器取得当前固件页面并显示 `Last successful sync`，无需手工清理浏览器缓存

### AC-09：菜单和现有功能回归

Given SETTING 菜单已扩展为 6 项  
When 用户正反向旋转并跨越首尾，随后操作其他设置项和 Portal  
Then 三行窗口、循环选择、各设置流程和配置保存均保持正确

### AC-10：息屏计划名称一致

Given 用户查看 OLED SETTING、息屏计划编辑页、Portal 和 README

When 查找原 `NIGHT OFF` / `Night screen` 功能

Then 用户可见名称统一为 `SCREEN SCHEDULE` / `Screen schedule` 或对应中文“息屏计划”，且原有开关、关闭时间、恢复时间和持久化行为不变

## 验证要求

- 宿主测试应覆盖六项菜单的正反向循环、三行窗口边界以及 `TIME SYNC` 选中位置。
- 时间逻辑测试应继续覆盖 0 epoch、合法 epoch 和固定 UTC+8 格式化。
- Portal 自动检查应覆盖 `/api/config` 的 `null` / 格式化文本、目标 DOM 和文案、保存 payload 不含该字段、未使用浏览器时区转换，以及根页面禁用缓存响应。
- 文案检查应确认生产 UI 与 README 中不再残留用户可见的 `NIGHT OFF` 或 `Night screen`，同时允许内部兼容标识继续使用 `NightScreenOff` / `nightScreenOff`。
- Portal JavaScript 应通过语法检查，PlatformIO 构建应确认 HTML 嵌入和固件链接；两者仅作为基础检查，不能替代真机验收。
- 真机验收应覆盖 OLED 无记录/有记录、同步成功即时刷新、重启恢复、失败不覆盖、窄屏 Portal、曾访问旧页面的手机重新访问，以及浏览器 DOM 与 API 响应一致性。
- 未经用户确认真机结果，不得宣称本功能已通过完整验收或可投入生产。

## 范围外

- 手动“立即同步”入口或按钮。
- 清除最后成功记录的用户操作。
- 展示最后尝试时间、最后失败原因或完整同步历史。
- 修改每日 08:00 调度时间、重试策略或 UTC+8 固定时区策略。
- Portal 的 WebSocket、SSE、自动轮询或后台实时刷新。
- 修改最后成功 epoch 的 NVS blob 或新增其他持久化数据。

## 待确认问题

无。OLED 与 Portal 文案、刷新方式及 `SCREEN SCHEDULE` 更名均已确认。

## 非阻塞假设

- `TIME SYNC` 插入 `TIME SET` 之后，其他菜单项保持现有相对顺序。
- Cancel 返回菜单时依靠现有 `selectedItem` 保留选中位置，不额外持久化菜单位置。
- Portal 缓存策略仅作用于本地配置页交付，不改变配置 API 的业务语义。
- `SCREEN SCHEDULE` 的更名仅覆盖用户可见文案，内部代码与存储命名保持兼容。
