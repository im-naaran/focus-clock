#pragma once

#include <Arduino.h>

bool persistenceBegin();
uint8_t persistenceLoadBrightness();
bool persistenceSaveBrightness(uint8_t level);
void persistenceEnd();
