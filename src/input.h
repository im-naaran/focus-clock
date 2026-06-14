#pragma once

#include <Arduino.h>

#include "app_state.h"

enum class InputEventKind : uint8_t {
  Button,
  KnobRaw,
  KnobRotationStart,
  KnobRotationEnd,
  KnobStep,
};

struct InputEvent {
  InputEventKind kind = InputEventKind::Button;
  ButtonId button = ButtonId::Mode;
  ButtonEventType buttonEvent = ButtonEventType::Pressed;
  int8_t knobSteps = 0;
};

void inputBegin();
void inputUpdate(uint32_t nowMs);
bool inputPopEvent(InputEvent &event);
bool inputAnyButtonHeldLow();
bool inputButtonHeldLow(ButtonId button);
bool inputHasPendingDebounce();
void inputPrimeWakeButton(ButtonId button, uint32_t nowMs);
void inputSyncKnobRotationState();
uint8_t inputKnobRotationState();
void inputHandleKnobWakeEdge(uint8_t previousState, uint32_t nowUs);
