#include "persistence.h"

#include <Preferences.h>

#include <string.h>

#include "config.h"
#include "persistence_codec.h"

using namespace AppConfig;

static constexpr const char *PREFERENCES_NAMESPACE = "focusClock";
static constexpr const char *KEY_BRIGHTNESS = "bright";
static constexpr const char *KEY_NIGHT_SCREEN_OFF_ENABLED = "nightOffEn";
static constexpr const char *KEY_NIGHT_SCREEN_OFF_MINUTE = "nightOffMin";
static constexpr const char *KEY_NIGHT_SCREEN_ON_MINUTE = "nightOnMin";
static constexpr const char *KEY_NETWORK_CONFIG = "netCfg";
static constexpr const char *KEY_SCHEDULED_TASK_RECORDS = "taskRuns";
static constexpr const char *KEY_TIME_SYNC_RESULT = "timeSync";

static Preferences preferences;
static bool preferencesOpen = false;
static uint8_t lastSavedBrightness = 0;
static NightScreenOffConfig lastSavedNightScreenOff;
static NetworkConfig lastSavedNetworkConfig;
static ScheduledTaskRecords lastSavedScheduledTaskRecords;
static uint32_t lastSavedTimeSyncSuccessEpoch = 0;

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

NetworkConfig persistenceLoadNetworkConfig() {
  NetworkConfig config;
  if (!persistenceBegin()) {
    lastSavedNetworkConfig = config;
    return config;
  }

  const size_t storedSize = preferences.getBytesLength(KEY_NETWORK_CONFIG);
  if (storedSize == 0) {
    lastSavedNetworkConfig = config;
    return config;
  }
  if (storedSize != sizeof(PersistedNetworkConfigV1)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid network config size in preferences: %u\n",
                    static_cast<unsigned int>(storedSize));
    }
    lastSavedNetworkConfig = config;
    return config;
  }

  PersistedNetworkConfigV1 blob;
  const size_t readSize = preferences.getBytes(KEY_NETWORK_CONFIG, &blob,
                                                sizeof(blob));
  const NetworkConfigBlobError error =
      persistenceDecodeNetworkConfig(&blob, readSize, config);
  if (error != NetworkConfigBlobError::None) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid network config in preferences: error=%u\n",
                    static_cast<unsigned int>(error));
    }
    // Any invalid V1 field discards the whole blob to avoid mixed credentials.
    config = NetworkConfig();
  }

  lastSavedNetworkConfig = config;
  return config;
}

bool persistenceSaveNetworkConfig(const NetworkConfig &config) {
  PersistedNetworkConfigV1 blob;
  if (!persistenceEncodeNetworkConfig(config, blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Skip invalid network config save");
    }
    return false;
  }

  if (config.policy == lastSavedNetworkConfig.policy &&
      strcmp(config.staSsid, lastSavedNetworkConfig.staSsid) == 0 &&
      strcmp(config.staPassword, lastSavedNetworkConfig.staPassword) == 0) {
    return true;
  }
  if (!persistenceBegin()) {
    return false;
  }

  // A single blob write prevents policy, SSID, and password from mixing versions.
  const size_t written = preferences.putBytes(KEY_NETWORK_CONFIG, &blob,
                                               sizeof(blob));
  if (written != sizeof(blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Network config save failed");
    }
    return false;
  }

  lastSavedNetworkConfig = config;
  return true;
}

ScheduledTaskRecords persistenceLoadScheduledTaskRecords() {
  ScheduledTaskRecords records;
  if (!persistenceBegin()) {
    lastSavedScheduledTaskRecords = records;
    return records;
  }

  const size_t storedSize =
      preferences.getBytesLength(KEY_SCHEDULED_TASK_RECORDS);
  if (storedSize == 0) {
    lastSavedScheduledTaskRecords = records;
    return records;
  }

  PersistedScheduledTaskRecordsV1 blob;
  const size_t readSize = storedSize == sizeof(blob)
                              ? preferences.getBytes(
                                    KEY_SCHEDULED_TASK_RECORDS,
                                    &blob,
                                    sizeof(blob))
                              : 0;
  const TaskPersistenceBlobError error =
      persistenceDecodeScheduledTaskRecords(&blob, readSize, records);
  if (error != TaskPersistenceBlobError::None) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid scheduled task records: error=%u\n",
                    static_cast<unsigned int>(error));
    }
    records = ScheduledTaskRecords{};
  }
  lastSavedScheduledTaskRecords = records;
  return records;
}

bool persistenceSaveScheduledTaskRecords(const ScheduledTaskRecords &records) {
  PersistedScheduledTaskRecordsV1 blob;
  if (!persistenceEncodeScheduledTaskRecords(records, blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Skip invalid scheduled task records save");
    }
    return false;
  }
  if (memcmp(records.lastAttemptDateKeys,
             lastSavedScheduledTaskRecords.lastAttemptDateKeys,
             sizeof(records.lastAttemptDateKeys)) == 0) {
    return true;
  }
  if (!persistenceBegin()) {
    return false;
  }

  const size_t written = preferences.putBytes(
      KEY_SCHEDULED_TASK_RECORDS, &blob, sizeof(blob));
  if (written != sizeof(blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Scheduled task records save failed");
    }
    return false;
  }

  // Cache only a complete blob write so RAM never claims an unsaved record.
  lastSavedScheduledTaskRecords = records;
  return true;
}

uint32_t persistenceLoadLastTimeSyncSuccessEpoch() {
  uint32_t epoch = 0;
  if (!persistenceBegin()) {
    lastSavedTimeSyncSuccessEpoch = 0;
    return 0;
  }

  const size_t storedSize = preferences.getBytesLength(KEY_TIME_SYNC_RESULT);
  if (storedSize == 0) {
    lastSavedTimeSyncSuccessEpoch = 0;
    return 0;
  }

  PersistedTimeSyncResultV1 blob;
  const size_t readSize = storedSize == sizeof(blob)
                              ? preferences.getBytes(KEY_TIME_SYNC_RESULT,
                                                     &blob,
                                                     sizeof(blob))
                              : 0;
  const TaskPersistenceBlobError error =
      persistenceDecodeTimeSyncResult(&blob, readSize, epoch);
  if (error != TaskPersistenceBlobError::None) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.printf("Invalid time sync result: error=%u\n",
                    static_cast<unsigned int>(error));
    }
    epoch = 0;
  }
  lastSavedTimeSyncSuccessEpoch = epoch;
  return epoch;
}

bool persistenceSaveLastTimeSyncSuccessEpoch(uint32_t epoch) {
  PersistedTimeSyncResultV1 blob;
  if (!persistenceEncodeTimeSyncResult(epoch, blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Skip invalid time sync result save");
    }
    return false;
  }
  if (epoch == lastSavedTimeSyncSuccessEpoch) {
    return true;
  }
  if (!persistenceBegin()) {
    return false;
  }

  const size_t written =
      preferences.putBytes(KEY_TIME_SYNC_RESULT, &blob, sizeof(blob));
  if (written != sizeof(blob)) {
    if (ENABLE_SERIAL_LOGGING) {
      Serial.println("Time sync result save failed");
    }
    return false;
  }
  lastSavedTimeSyncSuccessEpoch = epoch;
  return true;
}

void persistenceEnd() {
  if (!preferencesOpen) {
    return;
  }
  preferences.end();
  preferencesOpen = false;
}
