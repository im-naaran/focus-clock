#include "persistence.h"

#include <Preferences.h>

#include "config.h"

using namespace AppConfig;

static constexpr const char *PREFERENCES_NAMESPACE = "focusClock";
static constexpr const char *KEY_BRIGHTNESS = "bright";
static constexpr const char *KEY_NIGHT_SCREEN_OFF_ENABLED = "nightOffEn";
static constexpr const char *KEY_NIGHT_SCREEN_OFF_MINUTE = "nightOffMin";
static constexpr const char *KEY_NIGHT_SCREEN_ON_MINUTE = "nightOnMin";

static Preferences preferences;
static bool preferencesOpen = false;
static uint8_t lastSavedBrightness = 0;
static NightScreenOffConfig lastSavedNightScreenOff;

static NightScreenOffConfig defaultNightScreenOffConfig() {
  NightScreenOffConfig config;
  config.enabled = DEFAULT_NIGHT_SCREEN_OFF_ENABLED;
  config.offMinute = DEFAULT_NIGHT_SCREEN_OFF_MINUTE;
  config.onMinute = DEFAULT_NIGHT_SCREEN_ON_MINUTE;
  return config;
}

bool persistenceBegin() {
  if (preferencesOpen) {
    return true;
  }
  preferencesOpen = preferences.begin(PREFERENCES_NAMESPACE, false);
  if (!preferencesOpen) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Preferences begin failed");
    }
  }
  return preferencesOpen;
}

uint8_t persistenceLoadBrightness() {
  if (!persistenceBegin()) {
    return DEFAULT_BRIGHTNESS_LEVEL;
  }

  const uint8_t stored = preferences.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS_LEVEL);
  if (!isValidBrightnessLevel(stored)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid brightness level in preferences: %u\n", stored);
    }
    lastSavedBrightness = DEFAULT_BRIGHTNESS_LEVEL;
    return DEFAULT_BRIGHTNESS_LEVEL;
  }
  lastSavedBrightness = stored;
  return stored;
}

bool persistenceSaveBrightness(uint8_t level) {
  if (!isValidBrightnessLevel(level)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Skip invalid brightness save: %u\n", level);
    }
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
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Brightness save failed: %u\n", level);
    }
    return false;
  }
  lastSavedBrightness = level;
  return true;
}

NightScreenOffConfig persistenceLoadNightScreenOff() {
  NightScreenOffConfig config = defaultNightScreenOffConfig();
  if (!persistenceBegin()) {
    lastSavedNightScreenOff = config;
    return config;
  }

  config.enabled = preferences.getBool(KEY_NIGHT_SCREEN_OFF_ENABLED,
                                       DEFAULT_NIGHT_SCREEN_OFF_ENABLED);
  config.offMinute = preferences.getUShort(KEY_NIGHT_SCREEN_OFF_MINUTE,
                                           DEFAULT_NIGHT_SCREEN_OFF_MINUTE);
  config.onMinute = preferences.getUShort(KEY_NIGHT_SCREEN_ON_MINUTE,
                                          DEFAULT_NIGHT_SCREEN_ON_MINUTE);

  if (!isValidMinuteOfDay(config.offMinute)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid night screen off minute in preferences: %u\n", config.offMinute);
    }
    config.offMinute = DEFAULT_NIGHT_SCREEN_OFF_MINUTE;
  }
  if (!isValidMinuteOfDay(config.onMinute)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid night screen on minute in preferences: %u\n", config.onMinute);
    }
    config.onMinute = DEFAULT_NIGHT_SCREEN_ON_MINUTE;
  }

  lastSavedNightScreenOff = config;
  return config;
}

bool persistenceSaveNightScreenOff(const NightScreenOffConfig &config) {
  if (!isValidMinuteOfDay(config.offMinute) || !isValidMinuteOfDay(config.onMinute)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Skip invalid night screen off save: off=%u on=%u\n",
                    config.offMinute,
                    config.onMinute);
    }
    return false;
  }
  if (config.enabled == lastSavedNightScreenOff.enabled &&
      config.offMinute == lastSavedNightScreenOff.offMinute &&
      config.onMinute == lastSavedNightScreenOff.onMinute) {
    return true;
  }
  if (!persistenceBegin()) {
    return false;
  }

  const bool enabledOk = preferences.putBool(KEY_NIGHT_SCREEN_OFF_ENABLED,
                                             config.enabled) != 0;
  const bool offOk = preferences.putUShort(KEY_NIGHT_SCREEN_OFF_MINUTE,
                                           config.offMinute) != 0;
  const bool onOk = preferences.putUShort(KEY_NIGHT_SCREEN_ON_MINUTE,
                                          config.onMinute) != 0;
  if (!enabledOk || !offOk || !onOk) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Night screen off save failed: en=%u off=%u on=%u\n",
                    config.enabled ? 1 : 0,
                    config.offMinute,
                    config.onMinute);
    }
    return false;
  }

  lastSavedNightScreenOff = config;
  return true;
}

void persistenceEnd() {
  if (!preferencesOpen) {
    return;
  }
  preferences.end();
  preferencesOpen = false;
}
