# 变更记录：OLED 空闲息屏规格

## 2026-06-16

- 创建规格目录 `specs/20260616_oled_idle_screen_off/`。
- 添加 Phase 0 项目上下文文档，基于 `README.md`、`platformio.ini`、`power-optimization-roadmap.md` 和当前 `src/` 代码结构。
- 添加 Phase 1 需求规格草稿，聚焦 OLED 夜间空闲息屏、输入唤醒拦截、持久化配置和 SETTING 页面扩展。
- Phase 1 需求规格获得用户确认，状态更新为已确认。
- 添加 Phase 2 技术设计文档，明确 `display_power.*` 屏幕电源状态机、输入拦截、持久化和 SETTING 扩展方案。
- Phase 2 技术设计获得用户确认，状态更新为已确认。
- 添加 Phase 3 任务拆解文档。
- Phase 3 任务拆解获得用户确认，状态更新为已确认，进入 Phase 4 实现。
- 完成 Phase 4 实现：新增 OLED `displaySleep()` / `displayWake()`、`display_power.*` 屏幕电源状态机、息屏输入拦截、夜间息屏持久化、SETTING 菜单和夜间息屏配置页面。
- 执行 `/Users/naaran/.platformio/penv/bin/pio run`，构建通过。
- 修正自动息屏条件：自动息屏现在必须满足最近一次用户输入已超过 `SCREEN_WAKE_GRACE_MS`，避免从 SETTING 返回 CLOCK 后因夜间窗口和渲染完成而立即息屏；构建验证通过。
