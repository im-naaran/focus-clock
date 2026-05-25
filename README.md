# ESP32-C3 Focus Timer

一个基于 ESP32-C3-Zero 的桌面学习计时器 / 时钟。项目使用 SSD1306 OLED 显示、DS1302 RTC 保持时间，并通过 EC11 旋转编码器和独立按钮完成计时设置与模式切换。

## 项目目标

这个设备面向低频但稳定的学习计时场景：

- 默认显示 CLOCK 页面，可通过 Mode 切换到 TIMER 页面。
- 支持时钟、倒计时和正计时。
- 计时运行时优先保证显示与计时稳定。
- TIMER 空闲页进入 Light Sleep，降低待机功耗。
- OLED 常亮，避免使用时需要额外唤醒屏幕。

## 硬件组成

- ESP32-C3-Zero
- SSD1306 128x64 OLED
- DS1302 RTC
- EC11 旋转编码器
- Mode / Confirm / Cancel 按钮

引脚定义集中在 `src/config.h`：

- OLED I2C：`GPIO0` SDA，`GPIO1` SCL
- DS1302：`GPIO3` CE，`GPIO4` SCLK，`GPIO5` I/O
- EC11：`GPIO6` A，`GPIO7` B，`GPIO8` Confirm
- 独立按钮：`GPIO20` Mode，`GPIO21` Cancel

所有按钮统一使用内部上拉，按下接 GND。

## UI 设计

OLED 按 8 个 page 绘制。顶部是状态栏，中间是主显示区，第 5 行用于状态提示。

顶部状态栏：

- 左侧显示当前页面或计时子状态：`CLOCK`、`TIMER`、`COUNTDOWN`、`STOPWATCH`
- 非 `CLOCK` 页面右侧显示当前时间，格式为 `HH:MM`
- `CLOCK` 页面中间已经显示大号时间，顶部不重复显示右侧时间

`CLOCK` 页面：

- 中间大字显示当前时间，格式为 `HH:MM`
- 不显示秒
- 日期显示在下方

`TIMER` 页面：

- 空闲状态显示可设置计时值，默认 `00:00:00`
- 旋转 EC11 直接调整倒计时时长
- Confirm 启动计时
- Cancel 重置计时器

计时状态：

- 正计时：顶部 `STOPWATCH`，状态行显示 `RUNNING` 或 `PAUSED`
- 倒计时：顶部 `COUNTDOWN`，状态行显示 `REMAINING`、`PAUSED` 或 `TIME'S UP`
- 倒计时完成后标题保持 `COUNTDOWN`，避免页面语义跳变

## 交互逻辑

- Mode：在 `CLOCK` 和 `TIMER` 页面之间切换
- Confirm：
  - TIMER 空闲或已调整时长：启动计时
  - 正计时 / 倒计时运行中：暂停
  - 暂停状态：恢复
  - 完成状态：重置
- Cancel：在 TIMER 页面重置计时器
- EC11：仅在 TIMER 空闲或已调整时长时调整倒计时时长

如果设置值为 `00:00:00` 后按 Confirm，会启动正计时；如果设置值大于 0，会启动倒计时。

## 低功耗策略

项目采用保守的低功耗方案：

- CPU 启动后降频到 `80MHz`
- 启动时关闭 Wi-Fi 和蓝牙控制器
- 仅在 `TIMER + Idle` 页面进入 Light Sleep
- 倒计时、正计时、暂停、调整时长和 CLOCK 页面不进入 Light Sleep

这样可以避免计时运行时出现跳秒、显示残缺或按键响应不稳定，同时仍降低 TIMER 空闲页的功耗。

Light Sleep 唤醒源：

- 1 秒定时唤醒，用于检查 RTC 分钟变化
- EC11 A/B、Confirm、Mode、Cancel 的 GPIO 低电平唤醒

Mode / Confirm / Cancel 在 Light Sleep 唤醒后有专门的一次性消费逻辑，避免短按被普通 40ms 消抖采样漏掉。

## 代码结构

- `src/main.cpp`
  - 应用状态机
  - UI 渲染布局
  - 按钮和编码器交互
  - 计时逻辑
  - 低功耗进入和唤醒处理

- `src/config.h`
  - 硬件引脚
  - OLED / RTC / 交互刷新参数
  - 低功耗参数
  - 计时器步进和编码器参数

- `src/display.cpp` / `src/display.h`
  - SSD1306 初始化
  - 5x7 字库绘制
  - 行缓存
  - 普通文本、居中文本和 2 倍放大文本绘制

- `src/rtc.cpp` / `src/rtc.h`
  - DS1302 初始化
  - RTC 寄存器读取
  - BCD 转换与时间有效性校验

## 设计取舍

当前方案没有追求运行时极限省电，而是把省电限制在默认空闲页。原因是计时器的核心体验是准确、稳定、响应直接；运行计时时持续唤醒更简单也更可靠。由于实际计时使用时间通常远少于待机时间，这个取舍能在续航和稳定性之间取得更好的平衡。

OLED 保持常亮，不使用息屏策略。这样设备始终可读，符合桌面计时器的使用方式。
