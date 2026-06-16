# 需求规格：Focus Clock 重构

日期：2026-06-08
Feature：focus_clock_refactor
来源：根目录 `REQUIREMENTS.md`、仓库检查结果和用户补充说明。

## 背景与目标

本次重构目标是在标准 PlatformIO 生产代码结构中重新实现 Focus Clock 固件，使其满足 `REQUIREMENTS.md` 描述的目标行为。

旧实现已移动到 `backup/`。旧代码可以作为参考材料，但不能假定正确。缺失或可疑行为必须以需求为准重新实现，不能盲目复制。SETTING 页面属于新增功能，旧固件中尚未实现。

重构应保持当前硬件接线，并确保固件可通过现有 `esp32-c3-zero` PlatformIO 环境构建。

## 功能需求

### 启动与硬件初始化

R-01：固件应能在 ESP32-C3 上运行，并使用现有 PlatformIO 环境和当前 GPIO 分配。

R-02：启动时应将 CPU 主频设置为 80 MHz，并关闭 Wi-Fi 和蓝牙以降低功耗。

R-03：启动时应初始化 Serial、WS2812 输入反馈、DS1302 GPIO、按钮输入、旋钮输入、OLED 显示、持久化 UI 配置、RTC 状态和 Light Sleep 唤醒源。

R-04：按钮和旋钮输入应使用 `INPUT_PULLUP`，按下或有效状态语义为低电平。

R-05：OLED 应使用配置的 I2C 引脚和 400 kHz I2C 时钟初始化，并且启动显示过程不应留下旧内容残影。

### 应用模式

R-06：固件应支持三个顶层模式：CLOCK、TIMER、SETTING。

R-07：启动后默认进入 CLOCK。

R-08：非 SETTING 状态下，Mode 短按应在 CLOCK 和 TIMER 之间切换。

R-09：在 CLOCK 或 TIMER 中长按 Mode 3 秒应进入 SETTING。

R-10：Mode 长按进入 SETTING 后，该长按应被消费，释放 Mode 时不能再派发短按事件。

R-11：进入 SETTING 不应暂停、重置或改变正在运行的正计时或倒计时。

R-12：SETTING 菜单页按 Cancel 应退出 SETTING，确保需要持久化的设置已保存，并返回 TIMER。

### CLOCK 页面

R-13：RTC 有效时，CLOCK 应显示标题 `CLOCK`、当前时间 `HH:MM` 和日期 `YYYY-MM-DD DDD`。

R-14：RTC 无效或正在初始化时，CLOCK 应显示 `--:--` 和明确的 RTC 状态文本。

R-15：CLOCK 应使用星期标签 `MON`、`TUE`、`WED`、`THU`、`FRI`、`SAT`、`SUN`。

R-16：CLOCK 应只在必要时刷新显示，包括分钟变化和 RTC 状态变化。

R-17：CLOCK 中 Confirm、Cancel 和旋钮旋转不应执行业务动作，但仍应触发输入反馈。

### TIMER 页面

R-18：TIMER 应支持 Idle、Adjusting、FwdRun、FwdPause、CdRun、CdPause、Finished 状态。

R-19：TIMER 的 Idle 和 Adjusting 状态应显示可编辑设置值 `HH:MM:SS`，初始为 `00:00:00`。

R-20：TIMER Idle 或 Adjusting 状态下，旋钮每步应按 `TIMER_STEP_SECONDS` 调整设置值，当前为 60 秒。

R-21：TIMER 设置值应限制在 `0..99:59:59`。

R-22：TIMER Idle 或 Adjusting 状态下，如果设置值为 `00:00:00`，Confirm 应启动正计时并进入 FwdRun。

R-23：TIMER Idle 或 Adjusting 状态下，如果设置值大于 0，Confirm 应启动倒计时并进入 CdRun。

R-24：正计时应每经过 1 秒增加 1 秒，最大到 `99:59:59`。

R-25：倒计时应每经过 1 秒减少 1 秒，并在归零时进入 Finished。

R-26：Confirm 应支持 FwdRun/FwdPause 和 CdRun/CdPause 的暂停与恢复。

R-27：TIMER 页面中，Cancel 应能从任意 TIMER 状态重置计时器。

R-28：TIMER Finished 状态下，Confirm 或 Cancel 应重置计时器。

R-29：TIMER Finished 状态下，Mode 短按切到 CLOCK 前应先重置计时器。

R-30：正计时或倒计时运行、暂停、完成期间，旋钮旋转应被忽略。

R-31：TIMER 应按当前计时状态显示标题 `TIMER`、`STOPWATCH` 或 `COUNTDOWN`。

R-32：TIMER 状态行应根据状态显示 `RUNNING`、`PAUSED`、`REMAINING`、`TIME'S UP`、RTC 状态或空行。

### SETTING 页面

R-33：SETTING 内部应包含 SettingMenu、BrightnessEdit、TimeEditHour、TimeEditMinute 状态。

R-34：SETTING 应显示顶部状态栏，左侧为 `SETTING`，右侧为当前时间 `HH:MM` 或 `--:--`。

R-35：SETTING 菜单应包含 `BRIGHTNESS` 和 `TIME SET`。

R-36：SETTING 菜单应使用旋钮移动选中项，并在当前选中项前显示 `>`。

R-37：SETTING 菜单中按 Confirm 应进入当前选中项。

R-38：BrightnessEdit 中按 Cancel 应返回 SETTING 菜单；亮度调节过程中已经实时应用并实时保存，Cancel 不回滚亮度。

R-39：TimeEditHour 或 TimeEditMinute 中按 Cancel 应返回 SETTING 菜单，且不保存未确认的时间修改。

R-40：SETTING UI 文本应保持英文，并兼容当前 ASCII 显示字库。

### 亮度设置

R-41：亮度应支持 1 到 5 档，默认 3 档。

R-42：亮度档位应映射到 OLED contrast：1=`0x10`，2=`0x30`，3=`0x7F`，4=`0xBF`，5=`0xFF`。

R-43：BrightnessEdit 中旋钮旋转应在 1 到 5 档范围内调整亮度。

R-44：亮度变化应立即应用到 OLED 对比度，用于实时预览。

R-45：启动时应从 ESP32 NVS / Preferences 读取亮度档位。

R-46：持久化亮度不存在或超出范围时，应回退到默认 3 档。

R-47：亮度档位应在 BrightnessEdit 调整过程中实时保存到 NVS / Preferences；从 SETTING 菜单页按 Cancel 退出时，应确保当前亮度档位已保存。

R-48：亮度不得保存到 DS1302 RTC。

### 时间设置

R-49：进入 TIME SET 时，应使用当前有效 RTC 时间初始化可编辑小时和分钟。

R-50：TIME SET 应先编辑小时字段，范围为 `0..23`。

R-51：编辑小时字段时按 Confirm 应切换到分钟字段。

R-52：TIME SET 应再编辑分钟字段，范围为 `0..59`。

R-53：编辑分钟字段时按 Confirm 应将新时间写入 DS1302。

R-54：TIME SET 写入时应将秒字段清零，并保留当前 RTC 的年、月、日和星期。

R-55：TIME SET 写入成功后，应立即重新读取 RTC，刷新所有当前时间显示位置，并返回 SETTING 一级菜单选取状态。

R-56：TIME SET 写入失败时，固件应停留在 TIME SET，并在 OLED 上显示提示框效果，提示框内容为失败内容，不能静默丢弃失败。

R-57：TIME SET 当前编辑字段应每 500 ms 闪烁一次，可通过隐藏当前字段文本实现。

### 输入反馈

R-58：代码应保留 Mode、Confirm、Cancel 和旋钮旋转的输入反馈适配调用点，便于后续重新接入 WS2812 或其他灯效反馈。

R-59：当前固件默认禁用 WS2812 输入反馈，不点亮 LED，不因输入反馈阻止 Light Sleep；如后续重新启用临时闪烁反馈，默认持续时间应使用 `INPUT_LED_FLASH_MS`，当前为 300 ms。

R-60：如后续重新启用 WS2812 输入闪烁反馈，建议颜色为：Mode 蓝色、Confirm 绿色、Cancel 红色、旋钮旋转亮绿色；实际效果以硬件验证后决定。

R-61：输入反馈属于临时扩展点，当前保留独立模块或适配层隔离调用点，避免未来灯效逻辑散落在业务状态机中。

R-62：按钮处理应支持消抖、短按、长按和长按释放消费。

R-63：Light Sleep 唤醒处理不得把按住 Mode 的长按错误转换为短按。

### RTC

R-64：RTC 读取不应在正常状态下固定每 1000 ms 执行。RTC 正常时，应优先按下一分钟边界附近调度读取，并设置最长 30 秒读取间隔作为兜底；RTC 异常、自动初始化等待或写入确认阶段可使用 1 秒短周期。

R-65：RTC 读取后应校验秒、分、时、日期、月份、星期、年份范围和 DS1302 CH 位。

R-66：RTC 读取失败时，应通过串口输出 RTC 原始寄存器。

R-67：RTC 读取失败时，应等待 `RTC_AUTO_INIT_DELAY_MS` 后尝试一次自动初始化，当前延迟为 1000 ms。

R-68：RTC 自动初始化应使用固件编译日期/时间，并计算星期。

R-69：RTC 自动初始化每次启动最多尝试一次。

R-70：RTC 自动初始化失败时，UI 应显示 `RTC INIT FAIL`。

R-71：RTC 写入应校验输入，支持年份 2000 到 2099，写入 DS1302 寄存器，并通过重新读取确认结果。

### OLED 显示

R-72：显示层应支持 SSD1306 128x64 page-based 文本渲染。

R-73：显示层应提供左对齐、居中和放大居中的 ASCII 文本绘制能力。

R-74：使用当前字库路径时，小写文本应转换为大写显示。

R-75：显示层应缓存行内容，避免不必要的 OLED 写入。

R-76：显示层应提供 `displaySetContrast(uint8_t contrast)` 或等价能力，用于亮度预览和启动恢复。

R-77：显示层应支持绘制简单提示框效果，用于 TIME SET 写入失败等短文本错误提示。

R-78：模式或布局变化时，应清屏或使缓存失效，避免旧布局残留。

### 持久化

R-79：UI 配置应保存到 ESP32 NVS / Preferences。

R-80：本次重构唯一必须持久化的配置是 OLED 亮度档位。

R-81：亮度档位在 BrightnessEdit 调整过程中应实时保存；持久化失败不应阻断核心 CLOCK/TIMER 功能，但应通过串口日志可见。

### 低功耗

R-82：Light Sleep 只允许在 CLOCK 页面进入。

R-83：后台正计时或倒计时运行时，不得进入 Light Sleep。

R-84：显示内容待刷新、任意按钮按下、WS2812 反馈未结束或唤醒保持窗口未结束时，不得进入 Light Sleep。

R-85：Light Sleep 唤醒源应包含定时器、Mode、Confirm、Cancel 和旋钮旋转输入。

R-86：SETTING 页面不得进入 Light Sleep。

## 非功能需求

NFR-01：重构后生产代码应放在标准 PlatformIO 结构中，确保 `pio run` 可构建。

NFR-02：实现不得把 `backup/` 当作生产源码。

NFR-03：硬件常量应集中管理。

NFR-04：状态转换应足够明确，便于对照本需求文档审查。

NFR-05：除非另行确认，不应升级依赖、修改锁文件、调整 CI、Docker 或部署配置。

NFR-06：除非用户明确批准，不应更改当前接线和 GPIO 分配。

NFR-07：实现应保守使用动态内存，并避免在主循环中引入不必要的阻塞延迟。

## 验收标准

AC-01：`pio run` 能在 `esp32-c3-zero` 环境下完成构建。

AC-02：硬件上电后默认进入 CLOCK，并显示有效 RTC 时间/日期或正确的 RTC 错误/初始化状态。

AC-03：Mode 短按能在 CLOCK 和 TIMER 之间切换。

AC-04：TIMER 支持设置、正计时、倒计时、暂停/恢复、重置和完成状态。

AC-05：计时器运行时进入 SETTING，后台计时应继续。

AC-06：从 CLOCK 或 TIMER 长按 Mode 3 秒进入 SETTING，释放后不触发页面切换。

AC-07：SETTING 菜单选择、BrightnessEdit 和 TimeEdit 流程能通过旋钮、Confirm 和 Cancel 操作。

AC-08：亮度预览能立即改变 OLED 对比度，并在调节过程中实时持久化；Cancel 只返回 SETTING 菜单，不回滚亮度。

AC-09：TIME SET 能写入小时/分钟，秒清零，并保留日期和星期；写入成功后返回 SETTING 一级菜单选取状态。

AC-10：输入反馈颜色和持续时间符合需求。

AC-11：CLOCK 空闲 Light Sleep 可由定时器、按钮和旋钮唤醒，且不破坏短按或长按处理。

AC-12：TIME SET 写入失败时显示提示框效果，提示框内容为失败内容。

AC-13：页面切换不残留旧 OLED 文本。

## 范围外

OOS-01：更改硬件接线或 GPIO 分配。

OOS-02：新增 Wi-Fi、蓝牙、网络校时或手机配置能力。

OOS-03：新增中文字库渲染。

OOS-04：重启后恢复计时器状态。

OOS-05：新增闹钟、声音、动画或更多设置项。

OOS-06：构建桌面模拟器或完整单元测试框架，除非后续单独要求。

OOS-07：在与本规格冲突时，把旧备份行为视为自动正确。

## 已确认决策

D-01：TIME SET 写入成功后，UI 返回 SETTING 一级菜单选取状态。

D-02：TIME SET 写入失败时，OLED 直接展示提示框效果，提示框内容为失败内容。

D-03：亮度调节过程中实时应用并实时保存；BrightnessEdit 中点击 Cancel 只返回 SETTING 一级菜单选取状态，不回滚亮度。

D-04：WS2812 输入反馈是临时功能，后续可能移除；实现时需要隔离调用点，便于后续割接。

D-05：RTC 正常状态下没有必要固定每 1000 ms 读取。设计阶段应采用“分钟边界优先 + 最长 30 秒兜底”的读取策略；RTC 异常、自动初始化等待或写入确认阶段使用 1 秒短周期。
