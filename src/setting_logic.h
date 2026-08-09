#pragma once

#include <stdint.h>

#include "network_types.h"

enum class SettingMenuItem : uint8_t {
  Brightness,
  TimeSet,
  NightScreenOff,
  WifiConfig,
  WifiPolicy,
};

static constexpr uint8_t SETTING_MENU_ITEM_COUNT = 5;
static constexpr uint8_t SETTING_MENU_VISIBLE_ROWS = 3;

enum class WifiConfigPortalPhase : uint8_t {
  Starting,
  ConnectPhone,
  ShowAddress,
};

SettingMenuItem settingMenuMove(SettingMenuItem current, int8_t steps);
uint8_t settingMenuWindowStart(SettingMenuItem selected);
WifiPolicy settingWifiPolicyMove(WifiPolicy current, int8_t steps);
WifiConfigPortalPhase settingWifiConfigPortalPhase(bool configModeRunning,
                                                   bool apClientConnected);
