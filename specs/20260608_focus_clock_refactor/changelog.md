# 变更记录：Focus Clock 重构规格

## 2026-06-08

- 创建规格目录。
- 添加 Phase 0 项目上下文文档。
- 添加 Phase 1 需求规格草稿，依据包括根目录 `REQUIREMENTS.md`、仓库检查结果，以及用户补充说明：备份代码不一定正确，SETTING 页面此前尚未实现。
- 将已生成文档改为中文表述。
- 根据用户确认更新 TIME SET 成功/失败行为、亮度实时保存行为、WS2812 临时功能约束，以及 RTC 正常状态读取策略。
- 明确 RTC 正常读取策略为“分钟边界优先 + 最长 30 秒兜底”，异常、自动初始化和写入确认阶段使用 1 秒短周期。
- 添加 Phase 2 技术设计文档。
- 根据用户反馈补充模块分层、组件边界、依赖方向和配置文件拆分设计。
- 添加 Phase 3 任务拆解文档。
- 完成 Phase 4 实现、构建验证和硬件人工验收清单。

## 2026-06-10

- 根据 `knob_debug` 独立测试固件的硬件验证结论，修复主固件旋钮旋转解码。
- 将旋钮 LIVE 序列解码单独封装为 `LeobogRotationDecoder`，供 `LeobogKnob` 和后续其他固件复用。
- 更新 `LeobogKnob` 文档，明确 `00/11` 稳定锚点、`from -> middle -> target` 判定规则和取消序列。

## 2026-06-14

- 根据代码审查结果，确认 WS2812 输入反馈当前为有意禁用；更新需求、设计和任务文档，说明 `feedback.*` 仅保留未来灯效扩展点，当前输入不点亮 LED 且不阻止 Light Sleep。
- 修复 Light Sleep 唤醒后 Confirm/Cancel 可能重复产生 `Pressed` 事件的问题：唤醒桥接会同步所有按钮的输入状态机，避免同一次持续低电平再次被普通消抖消费。
- 修复 RTC 读取失败诊断：每次 `rtcReadTime()` 失败都会输出 raw registers，自动初始化仍保持每次启动最多尝试一次。
- 补充 RTC 原始 BCD 字段校验，避免非法 nibble 经 `fromBcd()` 转换后误判为合法时间。
- 清理未使用的 `timeEditInitialized` 字段，并将反馈事件收敛为 Mode、Confirm、Cancel、Knob 四类需求语义。
