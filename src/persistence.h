#pragma once

#include <Arduino.h>

struct NightScreenOffConfig {
  bool enabled = true;
  uint16_t offMinute = 0;
  uint16_t onMinute = 0;
};

bool persistenceBegin();
uint8_t persistenceLoadBrightness();
bool persistenceSaveBrightness(uint8_t level);
NightScreenOffConfig persistenceLoadNightScreenOff();
bool persistenceSaveNightScreenOff(const NightScreenOffConfig &config);
void persistenceEnd();
