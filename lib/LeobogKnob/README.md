# LeobogKnob

`LeobogKnob` 是这颗键盘定制旋钮的专用 V/W 解码组件。它不是标准 EC11 正交解码器。

## 引脚接入

旋转相关引脚是 `V`、`W` 和公共端 `Y`：

| 旋钮端 | 连接方式 |
| --- | --- |
| `V` | MCU 输入脚，建议使用 `INPUT_PULLUP` |
| `W` | MCU 输入脚，建议使用 `INPUT_PULLUP` |
| `Y` | `GND` |

当前临时测试固件为了方便排线，把 `Y` 接到一个输出 `LOW` 的 GPIO。正式硬件优先接真实 `GND`。

按压开关是 `X` / `Z`：

| 旋钮端 | 连接方式 |
| --- | --- |
| `X` | MCU 输入脚，建议使用 `INPUT_PULLUP` |
| `Z` | `GND` |

当前临时测试固件为了方便排线，把 `Z` 接到一个输出 `LOW` 的 GPIO。正式硬件优先接真实 `GND`。

## 方向判断

组件使用 `V` / `W` 的 LIVE 电平序列判断方向，不依赖单次边沿的 `changed channel`。`00` 和 `11` 都被视为稳定锚点：

1. 从稳定锚点离开，进入 `01` 或 `10` 中间态。
2. 到达相反锚点后输出一个 step。
3. 新锚点稳定一小段时间后，允许下一次 step。

默认方向规则如下：

| LIVE 序列 | `delta` |
| --- | ---: |
| `00 -> 10 -> 11` | `+1`，顺时针 |
| `11 -> 01 -> 00` | `+1`，顺时针 |
| `00 -> 01 -> 11` | `-1`，逆时针 |
| `11 -> 10 -> 00` | `-1`，逆时针 |
| `00 -> 10 -> 00` | 取消，不计数 |
| `11 -> 01 -> 11` | 取消，不计数 |

这套规则来自 `knob_debug` 测试固件的硬件验证结论：稳定锚点是 `00` 和 `11`；从锚点离开后，首位先变化为顺时针，末位先变化为逆时针；只有到达相反锚点才改变数值。

如果后续接线或安装方向反了，可以在 `begin()` 第三个参数传 `-1`：

```cpp
knob.begin(PIN_V, PIN_W, -1);
```

旋转解码已单独封装在 `LeobogRotationDecoder` 中。其他固件如果不需要 GPIO 中断、按钮防抖或事件队列，可以直接复用这个类，只向它输入上一帧和当前帧的 V/W 两位 LIVE 状态。

## 最小接入方式

把库放在 PlatformIO 工程的 `lib/LeobogKnob` 下，然后在固件里引用。最简单的方式是在 `loop()` 里轮询事件：

```cpp
#include <Arduino.h>
#include <LeobogKnob.h>

static constexpr uint8_t PIN_V = 6;
static constexpr uint8_t PIN_W = 7;
static constexpr uint8_t PIN_X = 8;

LeobogKnob knob;
int32_t value = 0;

void setup() {
  knob.begin(PIN_V, PIN_W);
  knob.beginButton(PIN_X);
}

void loop() {
  knob.update();

  while (knob.hasStep()) {
    value += knob.step();
  }

  LeobogKnob::ButtonEvent button;
  while (knob.popButtonEvent(button)) {
    if (button.pressed) {
      // button down
    } else {
      // button up
    }
  }
}
```

也可以注册回调。回调在 `knob.update()` 内派发，不在 GPIO 中断里执行：

```cpp
void handleStep(const LeobogKnob::StepEvent &event) {
  value += event.delta;
}

void handleButton(const LeobogKnob::ButtonEvent &event) {
  if (event.pressed) {
    // button down
  }
}

void setup() {
  knob.begin(PIN_V, PIN_W);
  knob.beginButton(PIN_X);
  knob.onStep(handleStep);
  knob.onButton(handleButton);
}

void loop() {
  knob.update();
}
```

注意：回调会消费对应事件队列。对同一种事件，建议二选一使用“轮询”或“回调”。

按键默认按低电平为按下处理，防抖时间默认 `35ms`：

```cpp
knob.beginButton(PIN_X, true, 35);
```

旋转到达新锚点后默认稳定 `2ms` 才允许下一次计数，可按需要调整：

```cpp
knob.setRotationSettleMs(2);
```

## 调试事件

如果需要串口 trace，可以在上层固件读取 raw event 或 step event：

```cpp
LeobogKnob::StepEvent step;
while (knob.popStepEvent(step)) {
  Serial.printf("step=%d first=%c path=%u%u>%u%u>%u%u\n",
                step.delta,
                LeobogKnob::channelName(step.first),
                (step.fromState >> 1) & 1,
                step.fromState & 1,
                (step.middleState >> 1) & 1,
                step.middleState & 1,
                (step.state >> 1) & 1,
                step.state & 1);
}

LeobogKnob::RawEvent raw;
while (knob.popRawEvent(raw)) {
  Serial.printf("state=V%uW%u changed=%u\n",
                (raw.state >> 1) & 1,
                raw.state & 1,
                raw.changed);
}
```

核心组件不直接写串口，也不依赖 OLED。显示、日志、业务值加减都应放在上层固件里。
