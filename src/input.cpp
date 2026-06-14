#include "input.h"

#include <LeobogKnob.h>

#include "config.h"

using namespace AppConfig;

static constexpr uint8_t EVENT_QUEUE_SIZE = 24;
static constexpr uint32_t KNOB_ROTATION_END_IDLE_MS = 80;

struct InputEventQueue {
  InputEvent events[EVENT_QUEUE_SIZE];
  uint8_t head = 0;
  uint8_t tail = 0;

  bool push(const InputEvent &event) {
    const uint8_t next = (head + 1) % EVENT_QUEUE_SIZE;
    if (next == tail) {
      return false;
    }
    events[head] = event;
    head = next;
    return true;
  }

  bool pushPriority(const InputEvent &event) {
    const uint8_t previousTail = tail == 0 ? EVENT_QUEUE_SIZE - 1 : tail - 1;
    if (previousTail == head) {
      return false;
    }
    tail = previousTail;
    events[tail] = event;
    return true;
  }

  bool pop(InputEvent &event) {
    if (tail == head) {
      return false;
    }
    event = events[tail];
    tail = (tail + 1) % EVENT_QUEUE_SIZE;
    return true;
  }
};

static InputEvent makeButtonEvent(ButtonId button, ButtonEventType eventType) {
  InputEvent event;
  event.kind = InputEventKind::Button;
  event.button = button;
  event.buttonEvent = eventType;
  event.knobSteps = 0;
  return event;
}

static InputEvent makeKnobEvent(int8_t steps) {
  InputEvent event;
  event.kind = InputEventKind::KnobStep;
  event.button = ButtonId::Mode;
  event.buttonEvent = ButtonEventType::Pressed;
  event.knobSteps = steps;
  return event;
}

static InputEvent makeKnobRawEvent() {
  InputEvent event;
  event.kind = InputEventKind::KnobRaw;
  event.button = ButtonId::Mode;
  event.buttonEvent = ButtonEventType::Pressed;
  event.knobSteps = 0;
  return event;
}

static InputEvent makeKnobRotationEvent(InputEventKind kind) {
  InputEvent event;
  event.kind = kind;
  event.button = ButtonId::Mode;
  event.buttonEvent = ButtonEventType::Pressed;
  event.knobSteps = 0;
  return event;
}

struct DebouncedPressButton {
  uint8_t pin = 0;
  bool stableState = HIGH;
  bool lastRaw = HIGH;
  uint32_t lastRawChangeMs = 0;

  void begin(uint8_t assignedPin, uint32_t nowMs) {
    pin = assignedPin;
    stableState = digitalRead(pin);
    lastRaw = stableState;
    lastRawChangeMs = nowMs;
  }

  bool updatePressed(uint32_t nowMs) {
    const bool raw = digitalRead(pin);
    if (raw != lastRaw) {
      lastRaw = raw;
      lastRawChangeMs = nowMs;
    }
    if (static_cast<uint32_t>(nowMs - lastRawChangeMs) >= BUTTON_DEBOUNCE_MS &&
        stableState != raw) {
      stableState = raw;
      return stableState == LOW;
    }
    return false;
  }

  void primeHeldLow(uint32_t nowMs) {
    stableState = LOW;
    lastRaw = LOW;
    lastRawChangeMs = nowMs;
  }

  bool pendingDebounce() const {
    return stableState != lastRaw;
  }
};

struct ModeButtonState {
  uint8_t pin = 0;
  bool stableState = HIGH;
  bool lastRaw = HIGH;
  bool longConsumed = false;
  uint32_t lastRawChangeMs = 0;
  uint32_t pressedAtMs = 0;

  void begin(uint8_t assignedPin, uint32_t nowMs) {
    pin = assignedPin;
    stableState = digitalRead(pin);
    lastRaw = stableState;
    lastRawChangeMs = nowMs;
    pressedAtMs = stableState == LOW ? nowMs : 0;
    longConsumed = false;
  }

  void update(uint32_t nowMs, InputEventQueue &queue) {
    const bool raw = digitalRead(pin);
    if (raw != lastRaw) {
      lastRaw = raw;
      lastRawChangeMs = nowMs;
    }

    if (static_cast<uint32_t>(nowMs - lastRawChangeMs) >= BUTTON_DEBOUNCE_MS &&
        stableState != raw) {
      stableState = raw;
      if (stableState == LOW) {
        pressedAtMs = nowMs;
        longConsumed = false;
        queue.pushPriority(makeButtonEvent(ButtonId::Mode, ButtonEventType::Pressed));
      } else {
        if (!longConsumed) {
          queue.pushPriority(makeButtonEvent(ButtonId::Mode, ButtonEventType::ShortReleased));
        }
        pressedAtMs = 0;
        longConsumed = false;
      }
    }

    if (stableState == LOW && !longConsumed &&
        static_cast<uint32_t>(nowMs - pressedAtMs) >= MODE_LONG_PRESS_MS) {
      longConsumed = true;
      queue.pushPriority(makeButtonEvent(ButtonId::Mode, ButtonEventType::LongPressed));
    }
  }

  void primeHeldLow(uint32_t nowMs) {
    stableState = LOW;
    lastRaw = LOW;
    lastRawChangeMs = nowMs;
    pressedAtMs = nowMs;
    longConsumed = false;
  }

  bool pendingDebounce() const {
    return stableState != lastRaw;
  }
};

static InputEventQueue eventQueue;
static ModeButtonState modeButton;
static DebouncedPressButton cancelButton;
static DebouncedPressButton confirmButton;
static LeobogKnob knob;
static bool rotationSessionActive = false;
static bool rotationCandidateActive = false;
static uint8_t rotationAnchorState = 0;
static uint32_t lastRotationActivityMs = 0;

static uint8_t readKnobPinsState() {
  return (digitalRead(PIN_KNOB_V) ? LeobogRotationDecoder::CHANNEL_V : 0x00) |
         (digitalRead(PIN_KNOB_W) ? LeobogRotationDecoder::CHANNEL_W : 0x00);
}

static uint8_t buttonPin(ButtonId button) {
  switch (button) {
    case ButtonId::Mode:
      return PIN_MODE;
    case ButtonId::Confirm:
      return PIN_KNOB_X;
    case ButtonId::Cancel:
      return PIN_CANCEL;
  }
  return PIN_MODE;
}

static void beginKnobHardware() {
  knob.begin(PIN_KNOB_V, PIN_KNOB_W, KNOB_STEP_FROM_V_FIRST);
  knob.setRotationSettleMs(KNOB_ROTATION_SETTLE_MS);
}

static void beginKnobRotation() {
  beginKnobHardware();
  rotationAnchorState = knob.state() & LeobogRotationDecoder::CHANNEL_BOTH;
  rotationCandidateActive = false;
  rotationSessionActive = false;
  lastRotationActivityMs = millis();
}

static bool isAnchorState(uint8_t state) {
  return LeobogRotationDecoder::isAnchorState(state);
}

static bool isMiddleState(uint8_t state) {
  return LeobogRotationDecoder::isMiddleState(state);
}

static void markRotationActivity(uint32_t nowMs) {
  lastRotationActivityMs = nowMs;
}

static void startRotationSession(uint32_t nowMs) {
  markRotationActivity(nowMs);
  if (!rotationSessionActive) {
    rotationSessionActive = true;
    eventQueue.push(makeKnobRotationEvent(InputEventKind::KnobRotationStart));
  }
}

static void processKnobRawEvent(const LeobogKnob::RawEvent &rawEvent, uint32_t nowMs) {
  const uint8_t previous = (rawEvent.state ^ rawEvent.changed) &
                           LeobogRotationDecoder::CHANNEL_BOTH;
  const uint8_t state = rawEvent.state & LeobogRotationDecoder::CHANNEL_BOTH;
  if (previous == state) {
    return;
  }

  if (!rotationCandidateActive) {
    if (previous == rotationAnchorState && isAnchorState(previous) && isMiddleState(state)) {
      rotationCandidateActive = true;
      startRotationSession(nowMs);
      return;
    }
    if (isAnchorState(state)) {
      rotationAnchorState = state;
    }
    return;
  }

  markRotationActivity(nowMs);

  if (isAnchorState(state)) {
    rotationAnchorState = state;
    rotationCandidateActive = false;
  }
}

static void updateRotationEnd(uint32_t nowMs) {
  if (!rotationSessionActive || rotationCandidateActive) {
    return;
  }
  if (!isAnchorState(readKnobPinsState())) {
    return;
  }
  if (static_cast<uint32_t>(nowMs - lastRotationActivityMs) < KNOB_ROTATION_END_IDLE_MS) {
    return;
  }

  rotationSessionActive = false;
  eventQueue.push(makeKnobRotationEvent(InputEventKind::KnobRotationEnd));
}

void inputBegin() {
  const uint32_t nowMs = millis();
  pinMode(PIN_KNOB_V, INPUT_PULLUP);
  pinMode(PIN_KNOB_W, INPUT_PULLUP);
  pinMode(PIN_KNOB_X, INPUT_PULLUP);
  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_CANCEL, INPUT_PULLUP);

  modeButton.begin(PIN_MODE, nowMs);
  cancelButton.begin(PIN_CANCEL, nowMs);
  confirmButton.begin(PIN_KNOB_X, nowMs);
  beginKnobRotation();
}

void inputUpdate(uint32_t nowMs) {
  knob.update();

  LeobogKnob::RawEvent rawEvent;
  while (knob.popRawEvent(rawEvent)) {
    processKnobRawEvent(rawEvent, nowMs);
  }

  LeobogKnob::StepEvent stepEvent;
  while (knob.popStepEvent(stepEvent)) {
    if (stepEvent.delta != 0) {
      eventQueue.push(makeKnobEvent(stepEvent.delta > 0 ? 1 : -1));
    }
  }

  updateRotationEnd(nowMs);

  modeButton.update(nowMs, eventQueue);

  if (confirmButton.updatePressed(nowMs)) {
    eventQueue.pushPriority(makeButtonEvent(ButtonId::Confirm, ButtonEventType::Pressed));
  }

  if (cancelButton.updatePressed(nowMs)) {
    eventQueue.pushPriority(makeButtonEvent(ButtonId::Cancel, ButtonEventType::Pressed));
  }
}

bool inputPopEvent(InputEvent &event) {
  return eventQueue.pop(event);
}

bool inputAnyButtonHeldLow() {
  return digitalRead(PIN_MODE) == LOW ||
         digitalRead(PIN_KNOB_X) == LOW ||
         digitalRead(PIN_CANCEL) == LOW;
}

bool inputButtonHeldLow(ButtonId button) {
  return digitalRead(buttonPin(button)) == LOW;
}

bool inputHasPendingDebounce() {
  return modeButton.pendingDebounce() ||
         cancelButton.pendingDebounce() ||
         confirmButton.pendingDebounce();
}

void inputPrimeWakeButton(ButtonId button, uint32_t nowMs) {
  if (button == ButtonId::Mode) {
    modeButton.primeHeldLow(nowMs);
  } else if (button == ButtonId::Confirm) {
    confirmButton.primeHeldLow(nowMs);
  } else if (button == ButtonId::Cancel) {
    cancelButton.primeHeldLow(nowMs);
  }
}

void inputSyncKnobRotationState() {
  beginKnobRotation();
}

uint8_t inputKnobRotationState() {
  return readKnobPinsState();
}

void inputHandleKnobWakeEdge(uint8_t previousState, uint32_t nowUs) {
  (void)nowUs;
  if ((previousState & LeobogRotationDecoder::CHANNEL_BOTH) != readKnobPinsState()) {
    eventQueue.push(makeKnobRawEvent());
  }
  beginKnobHardware();
}
