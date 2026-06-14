#include "feedback.h"

#include <Adafruit_NeoPixel.h>

#include "config.h"

using namespace AppConfig;

static Adafruit_NeoPixel inputLed(WS2812_PIXEL_COUNT, PIN_WS2812, NEO_GRB + NEO_KHZ800);

void feedbackBegin() {
  inputLed.begin();
  inputLed.clear();
  inputLed.show();
}

void feedbackFlash(FeedbackEvent event) {
  (void)event;
}

void feedbackSetHeld(bool active, FeedbackEvent event) {
  (void)active;
  (void)event;
}

void feedbackUpdate(uint32_t nowMs) {
  (void)nowMs;
}

bool feedbackActive(uint32_t nowMs) {
  (void)nowMs;
  return false;
}
