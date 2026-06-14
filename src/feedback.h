#pragma once

#include <Arduino.h>

enum class FeedbackEvent : uint8_t {
  Mode,
  Confirm,
  Cancel,
  Knob,
};

void feedbackBegin();
void feedbackFlash(FeedbackEvent event);
void feedbackSetHeld(bool active, FeedbackEvent event);
void feedbackUpdate(uint32_t nowMs);
bool feedbackActive(uint32_t nowMs);
