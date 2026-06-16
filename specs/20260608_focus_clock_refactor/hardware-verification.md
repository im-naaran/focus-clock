# 硬件人工验收清单：Focus Clock 重构

日期：2026-06-09
Feature：focus_clock_refactor

## 前提

- 已完成 `pio run -e esp32-c3-zero` 构建。
- 将固件烧录到当前 ESP32-C3-Zero 硬件。
- 串口监视器波特率为 `115200`。
- 本清单用于人工确认，不代表当前已经硬件验证通过。

## 启动与 CLOCK

- [ ] 上电后串口输出启动完成信息。
- [ ] 上电默认进入 `CLOCK` 页面。
- [ ] RTC 有效时显示 `HH:MM` 和 `YYYY-MM-DD DDD`。
- [ ] RTC 无效时显示 `--:--` 和 `RTC READ FAIL` / `RTC INIT...` / `RTC INIT FAIL`。
- [ ] RTC 读取失败时串口输出 raw registers。
- [ ] RTC 自动初始化每次启动最多尝试一次。

## TIMER

- [ ] Mode 短按从 CLOCK 切到 TIMER。
- [ ] TIMER 空闲初始显示 `00:00:00`。
- [ ] TIMER Idle/Adjusting 下旋钮每步调整 1 分钟。
- [ ] 设置值为 0 时 Confirm 启动正计时。
- [ ] 设置值大于 0 时 Confirm 启动倒计时。
- [ ] 正计时和倒计时 Confirm 可暂停/恢复。
- [ ] Cancel 在 TIMER 任意状态重置计时器。
- [ ] 倒计时完成显示 `TIME'S UP`。
- [ ] TIMER 运行或暂停时旋钮不改变计时值。

## SETTING

- [ ] CLOCK 或 TIMER 中长按 Mode 3 秒进入 SETTING。
- [ ] 长按进入 SETTING 后释放 Mode 不触发短按切页。
- [ ] SETTING 菜单显示 `BRIGHTNESS` 和 `TIME SET`，当前项前有 `>`。
- [ ] SETTING 菜单中旋钮可切换选中项。
- [ ] SETTING 菜单 Cancel 退出并返回 TIMER。
- [ ] 进入 SETTING 后，后台正计时或倒计时继续运行。

## BRIGHTNESS

- [ ] Confirm 进入 `BRIGHTNESS`。
- [ ] 旋钮调整亮度档位 1..5。
- [ ] 调整时 OLED 对比度实时变化。
- [ ] 调整过程中亮度实时保存。
- [ ] BrightnessEdit 中 Cancel 返回一级菜单，不回滚亮度。
- [ ] 重启后恢复上次保存的亮度档位。

## TIME SET

- [ ] Confirm 进入 `TIME SET` 后默认显示当前 RTC `HH:MM`。
- [ ] 先编辑小时字段，小时按 500ms 闪烁。
- [ ] 小时字段旋钮调整范围为 0..23。
- [ ] 小时字段 Confirm 后进入分钟字段。
- [ ] 分钟字段按 500ms 闪烁。
- [ ] 分钟字段旋钮调整范围为 0..59。
- [ ] 分钟字段 Confirm 写入 RTC，秒清零，日期和星期保持不变。
- [ ] 写入成功后返回 SETTING 一级菜单。
- [ ] 写入失败时显示提示框，内容为失败内容。
- [ ] TimeEdit 中 Cancel 返回一级菜单，不保存未确认时间。

## 输入反馈

- [ ] Mode 输入反馈为蓝色。
- [ ] Confirm 输入反馈为绿色。
- [ ] Cancel 输入反馈为红色。
- [ ] 旋钮输入反馈为亮绿色。
- [ ] 旋钮旋转每一格只触发一个 UI 步进，数字变化 1。
- [ ] 旋钮正反向方向与 `knob_debug` 验证结论一致：顺时针增加，逆时针减少。
- [ ] 反馈持续约 300ms 后熄灭。

## Light Sleep

- [ ] 仅 CLOCK 空闲时进入 Light Sleep。
- [ ] TIMER 页面不进入 Light Sleep。
- [ ] SETTING 页面不进入 Light Sleep。
- [ ] 后台正计时或倒计时运行时，CLOCK 不进入 Light Sleep。
- [ ] 定时器唤醒后 CLOCK 时间能刷新。
- [ ] Mode、Confirm、Cancel 可唤醒。
- [ ] 旋钮 V/W 旋转可唤醒。
- [ ] 从 Light Sleep 中按住 Mode 可触发长按进入 SETTING，而不是立即短按切页。

## 待测风险

- GPIO8 是 ESP32-C3 strapping 引脚，上电或复位时按住旋钮可能影响启动。
- 旋钮方向如与预期相反，需要调整 `KNOB_STEP_FROM_V_FIRST`。
- RTC 写入确认需要在真实 DS1302 模块上验证，尤其是秒清零和秒进位窗口。
- Light Sleep 唤醒行为依赖实际电平和机械按键抖动，需要实测。
- OLED 亮度档位可感知差异受具体屏幕和供电影响。
