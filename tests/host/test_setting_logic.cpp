#include <stdio.h>

#include "setting_logic.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}

void testMenuNavigation() {
  expect(settingMenuMove(SettingMenuItem::Brightness, 1) ==
             SettingMenuItem::TimeSet,
         "forward navigation advances one item");
  expect(settingMenuMove(SettingMenuItem::TimeSet, 1) ==
             SettingMenuItem::TimeSync,
         "forward navigation reaches time sync");
  expect(settingMenuMove(SettingMenuItem::WifiPolicy, 1) ==
             SettingMenuItem::Brightness,
         "forward navigation wraps after the sixth item");
  expect(settingMenuMove(SettingMenuItem::Brightness, -1) ==
             SettingMenuItem::WifiPolicy,
         "reverse navigation wraps to the sixth item");
  expect(settingMenuMove(SettingMenuItem::WifiConfig, -2) ==
             SettingMenuItem::TimeSync,
         "aggregated reverse steps preserve their magnitude");
  expect(settingMenuMove(SettingMenuItem::TimeSync, 4) ==
             SettingMenuItem::Brightness,
         "aggregated forward steps wrap across the menu");
}

void testMenuWindow() {
  expect(settingMenuWindowStart(SettingMenuItem::Brightness) == 0,
         "first item uses the first window");
  expect(settingMenuWindowStart(SettingMenuItem::TimeSet) == 0,
         "second item remains in the first window");
  expect(settingMenuWindowStart(SettingMenuItem::TimeSync) == 1,
         "time sync is centered in the second window");
  expect(settingMenuWindowStart(SettingMenuItem::NightScreenOff) == 2,
         "night screen item is centered");
  expect(settingMenuWindowStart(SettingMenuItem::WifiConfig) == 3,
         "wifi config advances to the last window");
  expect(settingMenuWindowStart(SettingMenuItem::WifiPolicy) == 3,
         "last item remains visible in the last window");
}

void testPolicyEditing() {
  expect(settingWifiPolicyMove(WifiPolicy::Off, 1) == WifiPolicy::Auto,
         "policy advances from OFF to AUTO");
  expect(settingWifiPolicyMove(WifiPolicy::Auto, 1) == WifiPolicy::Off,
         "policy wraps from AUTO to OFF");
  expect(settingWifiPolicyMove(WifiPolicy::Off, -1) == WifiPolicy::Auto,
         "reverse policy editing wraps");
  expect(settingWifiPolicyMove(WifiPolicy::Auto, 2) == WifiPolicy::Auto,
         "two policy steps return to the original value");
}

void testWifiConfigPortalPhase() {
  expect(settingWifiConfigPortalPhase(false, false) ==
             WifiConfigPortalPhase::Starting,
         "portal waits while AP is starting");
  expect(settingWifiConfigPortalPhase(true, false) ==
             WifiConfigPortalPhase::ConnectPhone,
         "running AP without clients shows connection instructions");
  expect(settingWifiConfigPortalPhase(true, true) ==
             WifiConfigPortalPhase::ShowAddress,
         "connected AP client shows the portal address");
}

}  // namespace

int main() {
  testMenuNavigation();
  testMenuWindow();
  testPolicyEditing();
  testWifiConfigPortalPhase();

  if (failures != 0) {
    fprintf(stderr, "%d setting logic test(s) failed\n", failures);
    return 1;
  }
  printf("setting logic tests passed\n");
  return 0;
}
