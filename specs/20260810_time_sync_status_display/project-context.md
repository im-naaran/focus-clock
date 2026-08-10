# 项目上下文：最后成功对时状态展示

> 状态：已确认

日期：2026-08-10  
Feature：time_sync_status_display  
阶段：Phase 0

## 需求来源与当前状态

- 主要需求来源为 `docs/time-sync-status-display.md`，该文件当前为用户新增的“方案草稿，待确认”。
- 本功能建立在已完成自动化实现、但仍保留真机验收项的 `specs/20260809_ntp_time_sync/` 之上。
- NTP 规格已经提供最后成功 epoch 的持久化、启动恢复、运行态更新、UTC+8 格式化以及 Portal API/页面的初步展示能力。
- 用户反馈的实际问题是 OLED 无法查看最后成功时间，且真实浏览器 Portal 中没有观察到该字段。因此，源码中存在 API 字段或 HTML 节点不能替代实际交付链路验收。
- 本规格不会修改或覆盖 `docs/time-sync-status-display.md`，也不会在需求、设计和任务获得确认前修改生产代码。

## 技术栈与构建

- 硬件：Waveshare ESP32-C3-Zero、SSD1306 128x64 OLED、DS1302 RTC、键盘旋钮与独立按键。
- 固件：C++ / Arduino framework，PlatformIO 环境 `esp32-c3-zero`。
- Web：同步 `WebServer` 提供配置 API 和 Portal；单文件 `web/wifi_portal.html` 通过 `board_build.embed_txtfiles` 嵌入固件。
- 持久化：ESP32 `Preferences` / NVS，最后成功 epoch 使用已有版本化 Time Sync result blob。
- 测试：`tests/host/` 无 Arduino 宿主测试、Portal HTML/JavaScript 静态检查、PlatformIO 固件构建及真机人工验证。

## 架构与相关模块

- `src/app_state.h`：`AppState.lastTimeSyncSuccessEpoch` 保存当前可展示的最后成功 UTC epoch；`SettingState` 尚无只读对时详情状态。
- `src/main.cpp`：启动时调用持久化接口恢复最后成功 epoch，并协调 RTC、调度、Time Sync、WiFi、Portal、渲染和休眠。
- `src/time_sync_task.cpp`：只有 SNTP、RTC 写入、强制回读和结果持久化全部成功后，才更新 `lastTimeSyncSuccessEpoch`。
- `src/time_sync_logic.*`：`timeSyncFormatLocalEpoch()` 已按固定 UTC+8 输出 `YYYY-MM-DD HH:MM:SS`，0 或无效 epoch 格式化失败。
- `src/setting_logic.*`：SETTING 菜单使用枚举、循环移动和三行滚动窗口；当前固定为 5 项。
- `src/app_controller.cpp`：通过 `SettingState` 处理 Confirm、Cancel 和旋钮输入；现有详情页均沿用该状态机。
- `src/ui_render.cpp`：OLED 使用 8 行 5x7 ASCII 文本渲染；菜单标签和各设置子页采用固定分支。
- `src/wifi_portal.cpp`：`GET /api/config` 已输出可空的 `timeSync.lastSuccess`，值由设备端 UTC+8 formatter 产生；根页面响应尚未显式设置禁用缓存头。
- `web/wifi_portal.html`：Device 区域已有 `Last time sync` 只读 `<output>`，加载 API 后将 `null` 显示为 `never`；当前页面源码存在不代表设备嵌入产物或浏览器实际页面已更新。

## 当前数据流

```text
NTP 完整成功
  -> 保存最后成功 epoch 到 NVS
  -> 更新 AppState.lastTimeSyncSuccessEpoch

设备启动
  -> 从 NVS 恢复最后成功 epoch
  -> AppState.lastTimeSyncSuccessEpoch

AppState.lastTimeSyncSuccessEpoch
  -> timeSyncFormatLocalEpoch() -> GET /api/config -> Portal 源码输出
  -> OLED 尚无展示入口
```

## 现有公共能力与复用点

| 能力 | 现有实现 | 复用判断 |
| --- | --- | --- |
| 最后成功运行态 | `AppState.lastTimeSyncSuccessEpoch` | 直接复用，避免建立第二份 UI 状态 |
| 重启恢复 | `persistenceLoadLastTimeSyncSuccessEpoch()` | 直接复用，不新增 NVS 字段 |
| UTC+8 格式化 | `timeSyncFormatLocalEpoch()` | OLED 与 API 应共同复用 |
| 设置菜单导航 | `settingMenuMove()`、`settingMenuWindowStart()` | 扩展枚举和菜单数量，并补充边界测试 |
| 设置页交互 | `SettingState` 与 `app_controller.cpp` | 增加只读详情状态，不引入新页面框架 |
| Portal 配置读取 | `GET /api/config` | 保持现有只读字段，补强真实响应验证 |
| Portal 页面嵌入 | PlatformIO `embed_txtfiles` | 继续复用，验证构建产物和 HTTP 缓存行为 |

## 公共能力差距

- SETTING 菜单没有 `TIME SYNC` 项，也没有最后成功时间的只读详情状态和渲染分支。
- 现有五项菜单测试没有覆盖扩展为六项后的循环移动和三行窗口边界。
- Time Sync 成功更新运行态时虽会触发显示脏标记，但尚未验证用户停留在新详情页时能够即时看到新值。
- Portal 根页面响应未显式声明适合本地配置页的禁用缓存策略；同一 `192.168.4.1` 地址可能让浏览器复用旧 HTML。
- 自动检查尚不能证明设备实际烧录版本、固件嵌入页面、浏览器 DOM 与 `/api/config` 四者一致。

## 实现约束

- 保持当前单线程、非阻塞主循环；打开详情页不得发起网络、写 RTC、写 NVS 或等待同步。
- OLED 继续使用现有 5x7 ASCII 和行渲染能力，每行最多约 21 个字符。
- 固定使用设备端 UTC+8 格式化，不由浏览器 `Date` 二次转换。
- 不改变 NTP 调度、WiFi consumer、RTC 提交、持久化 blob 或 Light Sleep 语义。
- 不新增依赖，不修改锁文件、CI/CD、部署或基础设施配置。

## 已知风险

- 新菜单项插入枚举中部会改变后续枚举值；所有 switch、标签数组和导航测试必须同步更新。
- OLED 日期和时间需拆成两行，避免 19 字符完整时间与标题或状态文本挤压。
- 仅修改 `web/wifi_portal.html` 不足以修复真实页面；旧烧录版本、过期嵌入产物和浏览器缓存都可能造成字段不可见。
- `Cache-Control` 等 HTTP 策略可以减少旧页面复用，但仍需真机浏览器验证，不能只依赖源码扫描或固件构建。
- 当前 NTP 功能仍有未完成的真机验收项，本功能的端到端验收依赖可实际完成一次成功与失败对时。
