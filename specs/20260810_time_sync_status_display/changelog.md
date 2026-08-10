# 变更记录

> 本文件在阶段产物获得确认或执行任务完成时按时间顺序追加记录。

## 2026-08-10

- Phase 1 需求规格获得确认：OLED 使用 `TIME SYNC` / `LAST SUCCESS`，Portal 使用 `Last successful sync`；Portal 通过刷新或重新打开取得最新值，OLED 详情页在完整同步成功后即时刷新。
- 用户确认扩大本次范围：将含义不准确的 `NIGHT OFF` 用户可见名称统一调整为 `SCREEN SCHEDULE`，保留内部标识和持久化/API 兼容性。
- Phase 2 技术设计获得确认：扩展现有 SETTING 状态机，复用最后成功 epoch 与 UTC+8 formatter，在成功发布点触发 OLED 重绘，并为 Portal 根页面和配置 GET 增加 no-store 缓存策略。
- Phase 3 任务拆解获得确认；按依赖进入 Phase 4，执行菜单逻辑、OLED 页面、即时重绘、Portal 交付和最终验证五项任务。
- 完成 task-01：菜单模型扩展为六项，并补充 `TIME SYNC` 导航、首尾循环、多步移动和三行窗口边界宿主测试。
- 完成 task-02：新增 OLED `TIME SYNC` 只读详情页，支持 `NEVER` 与固定 UTC+8 分行展示，并将息屏计划用户文案统一为 `SCREEN SCHEDULE`；宿主测试和固件构建成功。
- 完成 task-03：最后成功 epoch 发布时同步标记 OLED 重绘，保持所有失败终态和资源释放语义不变。
- 完成 task-04：Portal 文案收敛为 `Last successful sync` / `Screen schedule`，根页面和配置 GET 增加 no-store 响应头，页面与固件构建检查成功。
- 完成 task-05：更新 README，七组宿主测试、页面脚本、源码检查、diff 检查和 ESP32-C3 构建全部成功；用户确认实际固件真机测试通过。
- 完成最终代码审查与精简：合并只读状态的无操作分支，将缓存 helper 更名为 `addNoStoreHeaders`；未发现行为缺陷，清理后全量回归结果保持成功。
