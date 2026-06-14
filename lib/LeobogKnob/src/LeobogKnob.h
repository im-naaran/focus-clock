#pragma once

#include <Arduino.h>

#include "LeobogRotationDecoder.h"

class LeobogKnob {
 public:
  struct StepEvent;
  struct ButtonEvent;

  using StepCallback = void (*)(const StepEvent &event);
  using ButtonCallback = void (*)(const ButtonEvent &event);

  enum Channel : uint8_t {
    CHANNEL_NONE = 0x00,
    CHANNEL_W = 0x01,
    CHANNEL_V = 0x02,
    CHANNEL_BOTH = 0x03,
  };

  struct RawEvent {
    uint32_t tUs = 0;
    uint8_t state = 0;
    uint8_t changed = 0;
  };

  struct StepEvent {
    uint32_t tUs = 0;
    int8_t delta = 0;
    Channel first = CHANNEL_NONE;
    uint8_t fromState = 0;
    uint8_t middleState = 0;
    uint8_t state = 0;
  };

  struct ButtonEvent {
    uint32_t tMs = 0;
    bool pressed = false;
  };

  LeobogKnob() = default;

  void begin(uint8_t pinV, uint8_t pinW, int8_t stepFromVFirst = 1,
             uint8_t pinModeValue = INPUT_PULLUP);
  void setRotationSettleMs(uint32_t settleMs);
  void beginButton(uint8_t pinButton, bool activeLow = true,
                   uint32_t debounceMs = 35,
                   uint8_t pinModeValue = INPUT_PULLUP);
  void end();

  // Calls registered callbacks and updates button debouncing. Call this once
  // per loop; rotation edges are still captured immediately by GPIO interrupts.
  void update();
  void onStep(StepCallback callback);
  void onButton(ButtonCallback callback);
  bool hasStep();
  int8_t step();
  bool popStepEvent(StepEvent &event);
  bool popRawEvent(RawEvent &event);
  bool popButtonEvent(ButtonEvent &event);

  int32_t read();
  void reset(int32_t value = 0);

  uint8_t state();
  bool isPressed();
  uint32_t droppedRawEvents();
  uint32_t droppedStepEvents();
  uint32_t droppedButtonEvents();

  static char channelName(Channel channel);

 private:
  static constexpr uint8_t RAW_EVENT_BUFFER_SIZE = 64;
  static constexpr uint8_t STEP_EVENT_BUFFER_SIZE = 16;
  static constexpr uint8_t BUTTON_EVENT_BUFFER_SIZE = 8;

  static LeobogKnob *activeInstance_;
  static void IRAM_ATTR handleInterrupt();

  uint8_t IRAM_ATTR readPins() const;
  void IRAM_ATTR handleEdge();
  void IRAM_ATTR pushRawEvent(uint32_t nowUs, uint8_t state, uint8_t changed);
  void IRAM_ATTR pushStepEvent(uint32_t nowUs, int8_t delta, Channel first,
                               uint8_t fromState, uint8_t middleState,
                               uint8_t state);
  void updateRotation();
  void updateButton();
  // Callback dispatch drains the same event queues as popStepEvent() and
  // popButtonEvent(). Use callbacks or manual polling for each event type.
  void dispatchCallbacks();
  bool readButtonPressed() const;
  void pushButtonEvent(uint32_t nowMs, bool pressed);

  uint8_t pinV_ = 0;
  uint8_t pinW_ = 0;
  uint8_t pinButton_ = 0;
  int8_t stepFromVFirst_ = 1;
  bool begun_ = false;
  bool buttonBegun_ = false;
  bool buttonActiveLow_ = true;
  uint32_t rotationSettleUs_ = 2000;
  uint32_t buttonDebounceMs_ = 35;

  volatile uint8_t lastIsrState_ = 0;
  LeobogRotationDecoder rotationDecoder_;
  volatile int32_t position_ = 0;
  StepCallback stepCallback_ = nullptr;
  ButtonCallback buttonCallback_ = nullptr;

  volatile uint8_t rawHead_ = 0;
  volatile uint8_t rawTail_ = 0;
  volatile uint32_t droppedRawEvents_ = 0;
  volatile RawEvent rawEvents_[RAW_EVENT_BUFFER_SIZE];

  volatile uint8_t stepHead_ = 0;
  volatile uint8_t stepTail_ = 0;
  volatile uint32_t droppedStepEvents_ = 0;
  volatile StepEvent stepEvents_[STEP_EVENT_BUFFER_SIZE];

  bool buttonStablePressed_ = false;
  bool buttonLastRawPressed_ = false;
  uint32_t buttonLastRawChangeMs_ = 0;

  uint8_t buttonHead_ = 0;
  uint8_t buttonTail_ = 0;
  uint32_t droppedButtonEvents_ = 0;
  ButtonEvent buttonEvents_[BUTTON_EVENT_BUFFER_SIZE];
};
