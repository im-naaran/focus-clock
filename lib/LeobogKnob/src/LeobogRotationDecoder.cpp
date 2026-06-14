#include "LeobogRotationDecoder.h"

void IRAM_ATTR LeobogRotationDecoder::begin(uint8_t initialState,
                                            uint32_t nowUs,
                                            int8_t stepFromVFirst,
                                            uint32_t rotationSettleUs) {
  setDirection(stepFromVFirst);
  setRotationSettleUs(rotationSettleUs);
  anchorState_ = initialState & CHANNEL_BOTH;
  middleState_ = anchorState_;
  anchorSinceUs_ = nowUs;
  active_ = false;
  armed_ = isAnchorState(anchorState_);
}

void IRAM_ATTR LeobogRotationDecoder::reset(uint8_t state, uint32_t nowUs) {
  begin(state, nowUs, stepFromVFirst_, rotationSettleUs_);
}

void IRAM_ATTR LeobogRotationDecoder::setDirection(int8_t stepFromVFirst) {
  stepFromVFirst_ = stepFromVFirst >= 0 ? 1 : -1;
}

void IRAM_ATTR LeobogRotationDecoder::setRotationSettleUs(uint32_t settleUs) {
  rotationSettleUs_ = settleUs;
}

bool IRAM_ATTR LeobogRotationDecoder::updateIdle(uint8_t currentState,
                                                 uint32_t nowUs) {
  currentState &= CHANNEL_BOTH;
  const bool shouldArm = !armed_ && !active_ && currentState == anchorState_ &&
                         isAnchorState(anchorState_) &&
                         nowUs - anchorSinceUs_ >= rotationSettleUs_;
  if (shouldArm) {
    armed_ = true;
  }
  return armed_;
}

bool IRAM_ATTR LeobogRotationDecoder::process(uint8_t previousState,
                                              uint8_t currentState,
                                              uint32_t nowUs, Step &step) {
  previousState &= CHANNEL_BOTH;
  currentState &= CHANNEL_BOTH;
  step = Step{};

  if (!armed_ && !active_ && previousState == anchorState_ &&
      updateIdle(previousState, nowUs)) {
    armed_ = true;
  }

  if (!armed_) {
    if (isAnchorState(currentState)) {
      resetTo(currentState, nowUs, false);
    }
    return false;
  }

  if (!active_) {
    if (previousState == anchorState_ && isMiddleState(currentState)) {
      middleState_ = currentState;
      active_ = true;
    } else if (isAnchorState(currentState)) {
      resetTo(currentState, nowUs, false);
    }
    return false;
  }

  if (currentState == anchorState_) {
    resetTo(anchorState_, nowUs, true);
    return false;
  }

  if (isMiddleState(currentState)) {
    if (currentState != middleState_) {
      active_ = false;
      middleState_ = anchorState_;
    }
    return false;
  }

  const int8_t delta = deltaForPath(anchorState_, middleState_, currentState);
  if (delta == 0) {
    resetTo(currentState, nowUs, false);
    return false;
  }

  step.delta = delta;
  step.first = firstChannelForPath(anchorState_, middleState_);
  step.fromState = anchorState_;
  step.middleState = middleState_;
  step.state = currentState;
  resetTo(currentState, nowUs, false);
  return true;
}

uint8_t IRAM_ATTR LeobogRotationDecoder::anchorState() const {
  return anchorState_;
}

bool IRAM_ATTR LeobogRotationDecoder::armed() const {
  return armed_;
}

bool IRAM_ATTR LeobogRotationDecoder::active() const {
  return active_;
}

bool IRAM_ATTR LeobogRotationDecoder::isAnchorState(uint8_t state) {
  const uint8_t masked = state & CHANNEL_BOTH;
  return masked == CHANNEL_NONE || masked == CHANNEL_BOTH;
}

bool IRAM_ATTR LeobogRotationDecoder::isMiddleState(uint8_t state) {
  const uint8_t masked = state & CHANNEL_BOTH;
  return masked == CHANNEL_W || masked == CHANNEL_V;
}

uint8_t IRAM_ATTR LeobogRotationDecoder::firstChannelForPath(uint8_t anchor,
                                                             uint8_t middle) {
  const uint8_t changed = (anchor ^ middle) & CHANNEL_BOTH;
  if (changed == CHANNEL_W || changed == CHANNEL_V) {
    return changed;
  }
  return CHANNEL_NONE;
}

void IRAM_ATTR LeobogRotationDecoder::resetTo(uint8_t anchor, uint32_t nowUs,
                                              bool armed) {
  anchorState_ = anchor & CHANNEL_BOTH;
  middleState_ = anchorState_;
  anchorSinceUs_ = nowUs;
  active_ = false;
  armed_ = armed;
}

int8_t IRAM_ATTR LeobogRotationDecoder::deltaForPath(uint8_t anchor,
                                                     uint8_t middle,
                                                     uint8_t target) const {
  anchor &= CHANNEL_BOTH;
  middle &= CHANNEL_BOTH;
  target &= CHANNEL_BOTH;

  if (anchor == CHANNEL_NONE && middle == CHANNEL_V && target == CHANNEL_BOTH) {
    return stepFromVFirst_;
  }
  if (anchor == CHANNEL_BOTH && middle == CHANNEL_W && target == CHANNEL_NONE) {
    return stepFromVFirst_;
  }
  if (anchor == CHANNEL_NONE && middle == CHANNEL_W && target == CHANNEL_BOTH) {
    return -stepFromVFirst_;
  }
  if (anchor == CHANNEL_BOTH && middle == CHANNEL_V && target == CHANNEL_NONE) {
    return -stepFromVFirst_;
  }
  return 0;
}
