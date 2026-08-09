#include "setting_logic.h"

namespace {

uint8_t wrapIndex(int value, uint8_t count) {
  value %= count;
  if (value < 0) {
    value += count;
  }
  return static_cast<uint8_t>(value);
}

}  // namespace

SettingMenuItem settingMenuMove(SettingMenuItem current, int8_t steps) {
  return static_cast<SettingMenuItem>(
      wrapIndex(static_cast<int>(current) + steps, SETTING_MENU_ITEM_COUNT));
}

uint8_t settingMenuWindowStart(SettingMenuItem selected) {
  const uint8_t selectedIndex = static_cast<uint8_t>(selected);
  const uint8_t maxStart = SETTING_MENU_ITEM_COUNT - SETTING_MENU_VISIBLE_ROWS;
  if (selectedIndex == 0) {
    return 0;
  }
  const uint8_t centeredStart = selectedIndex - 1;
  return centeredStart < maxStart ? centeredStart : maxStart;
}

WifiPolicy settingWifiPolicyMove(WifiPolicy current, int8_t steps) {
  return static_cast<WifiPolicy>(
      wrapIndex(static_cast<int>(current) + steps, 2));
}

WifiConfigPortalPhase settingWifiConfigPortalPhase(bool configModeRunning,
                                                   bool apClientConnected) {
  if (!configModeRunning) {
    return WifiConfigPortalPhase::Starting;
  }
  return apClientConnected ? WifiConfigPortalPhase::ShowAddress
                           : WifiConfigPortalPhase::ConnectPhone;
}
