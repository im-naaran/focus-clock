# 变更记录

## 2026-08-09

- 根据用户确认收敛需求：每日 08:00 写死且仅在该分钟触发，不做错过后的补执行。
- 明确每轮最多两个 SNTP attempt，RTC 写入和回读只执行一次，失败后不重试 RTC。
- 记录 RTC 无效时 `12:00` 回退方式及执行中重启去重语义两个待确认问题。
- Phase 1 需求规格获得确认：RTC 无效时仅展示 `12:00` 且不写入，任务在派发时记录当天已尝试以防重启后再次派发。
- Phase 2 技术设计获得确认：采用固定容量调度、派发即记账、显式 Time Sync 状态机和现有 WiFi consumer 租约。
- Phase 3 任务拆解获得确认；按用户指定范围开始执行 task-01 至 task-04。
- 完成 task-01：新增固定容量每日调度纯逻辑及精确分钟宿主测试。
- 完成 task-02：新增任务尝试记录和最后成功对时结果的版本化持久化 blob 及宿主测试。
- 完成 task-03：移除无效 RTC 的编译时间自动写入，改为固定 `12:00` 故障展示和短周期恢复读取。
- 完成 task-04：新增 Time Sync WiFi consumer bit，并保持现有 consumer mask 与休眠门禁设计。
- 完成 task-05：新增 Time Sync 纯状态转换、有限 deadline、UTC+8 `RtcTime` 转换与固定格式化宿主逻辑。
- 完成 task-06：新增派发前持久化、NVS 失败时 RAM 去重和成功日期修正的 scheduler service。
- 完成 task-07：新增按需 WiFi consumer、两次非阻塞 SNTP attempt 和统一资源释放的 Time Sync 插件层。
- 完成 task-08：接入单次 RTC 写入、单次强制回读、成功 epoch 持久化及完整事务后结果发布。
- 完成 task-09：按确定主循环顺序接入 scheduler、Time Sync、结果消费和最后成功运行状态。
- 完成 task-10：Portal API 与 Device 区域增加 nullable、固定 UTC+8 的最后成功对时展示。
- 完成 task-11：更新 README，执行全量宿主/页面/源码/固件自动检查并记录资源占用；因未发现 ESP32 串口，真机验收逐项保留待验证。
