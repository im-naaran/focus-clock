#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "input.h"
#include "sleep_manager.h"

bool displayPowerMinuteInNightWindow(uint16_t minute, uint16_t offMinute, uint16_t onMinute);
void displayPowerBegin(AppState &app, uint32_t nowMs);
bool displayPowerHandleInput(AppState &app, const InputEvent &event, uint32_t nowMs);
void displayPowerUpdate(AppState &app, const SleepManagerState &sleepState, uint32_t nowMs);
