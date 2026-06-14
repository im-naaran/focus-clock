#include "persistence.h"

#include <Preferences.h>

#include "config.h"

using namespace AppConfig;

static constexpr const char *PREFERENCES_NAMESPACE = "focusClock";
static constexpr const char *KEY_BRIGHTNESS = "bright";

static Preferences preferences;
static bool preferencesOpen = false;
static uint8_t lastSavedBrightness = 0;

bool persistenceBegin() {
  if (preferencesOpen) {
    return true;
  }
  preferencesOpen = preferences.begin(PREFERENCES_NAMESPACE, false);
  if (!preferencesOpen) {
    Serial.println("Preferences begin failed");
  }
  return preferencesOpen;
}

uint8_t persistenceLoadBrightness() {
  if (!persistenceBegin()) {
    return DEFAULT_BRIGHTNESS_LEVEL;
  }

  const uint8_t stored = preferences.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS_LEVEL);
  if (!isValidBrightnessLevel(stored)) {
    Serial.printf("Invalid brightness level in preferences: %u\n", stored);
    lastSavedBrightness = DEFAULT_BRIGHTNESS_LEVEL;
    return DEFAULT_BRIGHTNESS_LEVEL;
  }
  lastSavedBrightness = stored;
  return stored;
}

bool persistenceSaveBrightness(uint8_t level) {
  if (!isValidBrightnessLevel(level)) {
    Serial.printf("Skip invalid brightness save: %u\n", level);
    return false;
  }
  if (level == lastSavedBrightness) {
    return true;
  }
  if (!persistenceBegin()) {
    return false;
  }
  const size_t written = preferences.putUChar(KEY_BRIGHTNESS, level);
  if (written == 0) {
    Serial.printf("Brightness save failed: %u\n", level);
    return false;
  }
  lastSavedBrightness = level;
  return true;
}

void persistenceEnd() {
  if (!preferencesOpen) {
    return;
  }
  preferences.end();
  preferencesOpen = false;
}
