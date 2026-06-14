#include "LeobogKnob.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

LeobogKnob *LeobogKnob::activeInstance_ = nullptr;

void LeobogKnob::begin(uint8_t pinV, uint8_t pinW, int8_t stepFromVFirst,
                       uint8_t pinModeValue) {
  end();

  pinV_ = pinV;
  pinW_ = pinW;
  stepFromVFirst_ = stepFromVFirst >= 0 ? 1 : -1;

  pinMode(pinV_, pinModeValue);
  pinMode(pinW_, pinModeValue);

  noInterrupts();
  rawHead_ = 0;
  rawTail_ = 0;
  stepHead_ = 0;
  stepTail_ = 0;
  droppedRawEvents_ = 0;
  droppedStepEvents_ = 0;
  lastIsrState_ = readPins();
  rotationDecoder_.begin(lastIsrState_, micros(), stepFromVFirst_,
                         rotationSettleUs_);
  interrupts();

  activeInstance_ = this;
  begun_ = true;
  attachInterrupt(digitalPinToInterrupt(pinV_), handleInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinW_), handleInterrupt, CHANGE);
}

void LeobogKnob::setRotationSettleMs(uint32_t settleMs) {
  rotationSettleUs_ = settleMs * 1000UL;
  noInterrupts();
  rotationDecoder_.setRotationSettleUs(rotationSettleUs_);
  interrupts();
}

void LeobogKnob::beginButton(uint8_t pinButton, bool activeLow,
                             uint32_t debounceMs, uint8_t pinModeValue) {
  pinButton_ = pinButton;
  buttonActiveLow_ = activeLow;
  buttonDebounceMs_ = debounceMs;
  pinMode(pinButton_, pinModeValue);

  buttonStablePressed_ = readButtonPressed();
  buttonLastRawPressed_ = buttonStablePressed_;
  buttonLastRawChangeMs_ = millis();
  buttonHead_ = 0;
  buttonTail_ = 0;
  droppedButtonEvents_ = 0;
  buttonBegun_ = true;
}

void LeobogKnob::end() {
  if (!begun_) {
    return;
  }

  detachInterrupt(digitalPinToInterrupt(pinV_));
  detachInterrupt(digitalPinToInterrupt(pinW_));
  if (activeInstance_ == this) {
    activeInstance_ = nullptr;
  }
  begun_ = false;
  buttonBegun_ = false;
}

void LeobogKnob::update() {
  updateRotation();
  updateButton();
  dispatchCallbacks();
}

void LeobogKnob::onStep(StepCallback callback) {
  stepCallback_ = callback;
}

void LeobogKnob::onButton(ButtonCallback callback) {
  buttonCallback_ = callback;
}

bool LeobogKnob::hasStep() {
  noInterrupts();
  const bool available = stepTail_ != stepHead_;
  interrupts();
  return available;
}

int8_t LeobogKnob::step() {
  StepEvent event;
  if (!popStepEvent(event)) {
    return 0;
  }
  return event.delta;
}

bool LeobogKnob::popStepEvent(StepEvent &event) {
  noInterrupts();
  if (stepTail_ == stepHead_) {
    interrupts();
    return false;
  }

  event.tUs = stepEvents_[stepTail_].tUs;
  event.delta = stepEvents_[stepTail_].delta;
  event.first = stepEvents_[stepTail_].first;
  event.fromState = stepEvents_[stepTail_].fromState;
  event.middleState = stepEvents_[stepTail_].middleState;
  event.state = stepEvents_[stepTail_].state;
  stepTail_ = (stepTail_ + 1) % STEP_EVENT_BUFFER_SIZE;
  interrupts();
  return true;
}

bool LeobogKnob::popRawEvent(RawEvent &event) {
  noInterrupts();
  if (rawTail_ == rawHead_) {
    interrupts();
    return false;
  }

  event.tUs = rawEvents_[rawTail_].tUs;
  event.state = rawEvents_[rawTail_].state;
  event.changed = rawEvents_[rawTail_].changed;
  rawTail_ = (rawTail_ + 1) % RAW_EVENT_BUFFER_SIZE;
  interrupts();
  return true;
}

bool LeobogKnob::popButtonEvent(ButtonEvent &event) {
  if (buttonTail_ == buttonHead_) {
    return false;
  }

  event = buttonEvents_[buttonTail_];
  buttonTail_ = (buttonTail_ + 1) % BUTTON_EVENT_BUFFER_SIZE;
  return true;
}

int32_t LeobogKnob::read() {
  noInterrupts();
  const int32_t value = position_;
  interrupts();
  return value;
}

void LeobogKnob::reset(int32_t value) {
  noInterrupts();
  position_ = value;
  stepHead_ = 0;
  stepTail_ = 0;
  rotationDecoder_.reset(lastIsrState_, micros());
  interrupts();
}

uint8_t LeobogKnob::state() {
  noInterrupts();
  const uint8_t value = lastIsrState_;
  interrupts();
  return value;
}

bool LeobogKnob::isPressed() {
  return buttonStablePressed_;
}

uint32_t LeobogKnob::droppedRawEvents() {
  noInterrupts();
  const uint32_t value = droppedRawEvents_;
  interrupts();
  return value;
}

uint32_t LeobogKnob::droppedStepEvents() {
  noInterrupts();
  const uint32_t value = droppedStepEvents_;
  interrupts();
  return value;
}

uint32_t LeobogKnob::droppedButtonEvents() {
  return droppedButtonEvents_;
}

char LeobogKnob::channelName(Channel channel) {
  if (channel == CHANNEL_V) {
    return 'V';
  }
  if (channel == CHANNEL_W) {
    return 'W';
  }
  if (channel == CHANNEL_BOTH) {
    return 'B';
  }
  return '-';
}

void IRAM_ATTR LeobogKnob::handleInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->handleEdge();
  }
}

uint8_t IRAM_ATTR LeobogKnob::readPins() const {
  return (digitalRead(pinV_) ? CHANNEL_V : 0x00) |
         (digitalRead(pinW_) ? CHANNEL_W : 0x00);
}

void IRAM_ATTR LeobogKnob::handleEdge() {
  const uint32_t nowUs = micros();
  const uint8_t newState = readPins();
  const uint8_t changed = (lastIsrState_ ^ newState) & CHANNEL_BOTH;
  if (changed == CHANNEL_NONE) {
    return;
  }

  lastIsrState_ = newState;
  pushRawEvent(nowUs, newState, changed);

  const uint8_t previousState = (newState ^ changed) & CHANNEL_BOTH;
  LeobogRotationDecoder::Step step;
  if (rotationDecoder_.process(previousState, newState, nowUs, step)) {
    pushStepEvent(nowUs, step.delta, static_cast<Channel>(step.first),
                  step.fromState, step.middleState, step.state);
  }
}

void IRAM_ATTR LeobogKnob::pushRawEvent(uint32_t nowUs, uint8_t state,
                                        uint8_t changed) {
  const uint8_t nextHead = (rawHead_ + 1) % RAW_EVENT_BUFFER_SIZE;
  if (nextHead == rawTail_) {
    droppedRawEvents_++;
    return;
  }

  rawEvents_[rawHead_].tUs = nowUs;
  rawEvents_[rawHead_].state = state;
  rawEvents_[rawHead_].changed = changed;
  rawHead_ = nextHead;
}

void IRAM_ATTR LeobogKnob::pushStepEvent(uint32_t nowUs, int8_t delta,
                                         Channel first, uint8_t fromState,
                                         uint8_t middleState, uint8_t state) {
  position_ += delta;

  const uint8_t nextHead = (stepHead_ + 1) % STEP_EVENT_BUFFER_SIZE;
  if (nextHead == stepTail_) {
    droppedStepEvents_++;
    return;
  }

  stepEvents_[stepHead_].tUs = nowUs;
  stepEvents_[stepHead_].delta = delta;
  stepEvents_[stepHead_].first = first;
  stepEvents_[stepHead_].fromState = fromState;
  stepEvents_[stepHead_].middleState = middleState;
  stepEvents_[stepHead_].state = state;
  stepHead_ = nextHead;
}

void LeobogKnob::updateRotation() {
  noInterrupts();
  rotationDecoder_.updateIdle(lastIsrState_, micros());
  interrupts();
}

void LeobogKnob::updateButton() {
  if (!buttonBegun_) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool rawPressed = readButtonPressed();
  if (rawPressed != buttonLastRawPressed_) {
    buttonLastRawPressed_ = rawPressed;
    buttonLastRawChangeMs_ = nowMs;
    return;
  }

  if (rawPressed == buttonStablePressed_ ||
      nowMs - buttonLastRawChangeMs_ < buttonDebounceMs_) {
    return;
  }

  buttonStablePressed_ = rawPressed;
  pushButtonEvent(nowMs, buttonStablePressed_);
}

void LeobogKnob::dispatchCallbacks() {
  if (stepCallback_ != nullptr) {
    StepEvent event;
    while (popStepEvent(event)) {
      stepCallback_(event);
    }
  }

  if (buttonCallback_ != nullptr) {
    ButtonEvent event;
    while (popButtonEvent(event)) {
      buttonCallback_(event);
    }
  }
}

bool LeobogKnob::readButtonPressed() const {
  const bool level = digitalRead(pinButton_);
  return buttonActiveLow_ ? level == LOW : level == HIGH;
}

void LeobogKnob::pushButtonEvent(uint32_t nowMs, bool pressed) {
  const uint8_t nextHead = (buttonHead_ + 1) % BUTTON_EVENT_BUFFER_SIZE;
  if (nextHead == buttonTail_) {
    droppedButtonEvents_++;
    return;
  }

  buttonEvents_[buttonHead_].tMs = nowMs;
  buttonEvents_[buttonHead_].pressed = pressed;
  buttonHead_ = nextHead;
}
