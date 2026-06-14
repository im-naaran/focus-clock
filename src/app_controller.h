#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "input.h"
#include "rtc_service.h"

void appHandleInput(AppState &app, RtcServiceState &rtcService, const InputEvent &event, uint32_t nowMs);
bool appUpdateSettingBlink(AppState &app, uint32_t nowMs);
