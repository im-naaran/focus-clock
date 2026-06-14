#pragma once

#include <Arduino.h>

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

class LeobogRotationDecoder {
 public:
  static constexpr uint8_t CHANNEL_NONE = 0x00;
  static constexpr uint8_t CHANNEL_W = 0x01;
  static constexpr uint8_t CHANNEL_V = 0x02;
  static constexpr uint8_t CHANNEL_BOTH = 0x03;

  struct Step {
    int8_t delta = 0;
    uint8_t first = CHANNEL_NONE;
    uint8_t fromState = 0;
    uint8_t middleState = 0;
    uint8_t state = 0;
  };

  void IRAM_ATTR begin(uint8_t initialState, uint32_t nowUs,
                       int8_t stepFromVFirst = 1,
                       uint32_t rotationSettleUs = 2000);
  void IRAM_ATTR reset(uint8_t state, uint32_t nowUs);
  void IRAM_ATTR setDirection(int8_t stepFromVFirst);
  void IRAM_ATTR setRotationSettleUs(uint32_t settleUs);
  bool IRAM_ATTR updateIdle(uint8_t currentState, uint32_t nowUs);
  bool IRAM_ATTR process(uint8_t previousState, uint8_t currentState,
                         uint32_t nowUs, Step &step);

  uint8_t IRAM_ATTR anchorState() const;
  bool IRAM_ATTR armed() const;
  bool IRAM_ATTR active() const;

  static bool IRAM_ATTR isAnchorState(uint8_t state);
  static bool IRAM_ATTR isMiddleState(uint8_t state);
  static uint8_t IRAM_ATTR firstChannelForPath(uint8_t anchor, uint8_t middle);

 private:
  void IRAM_ATTR resetTo(uint8_t anchor, uint32_t nowUs, bool armed);
  int8_t IRAM_ATTR deltaForPath(uint8_t anchor, uint8_t middle,
                                uint8_t target) const;

  volatile int8_t stepFromVFirst_ = 1;
  volatile uint32_t rotationSettleUs_ = 2000;
  volatile uint8_t anchorState_ = CHANNEL_NONE;
  volatile uint8_t middleState_ = CHANNEL_NONE;
  volatile uint32_t anchorSinceUs_ = 0;
  volatile bool armed_ = false;
  volatile bool active_ = false;
};
