#include "persistence_codec.h"

#include <string.h>

#include "wifi_logic.h"

namespace {

size_t terminatedLength(const char *value, size_t capacity) {
  const void *terminator = memchr(value, '\0', capacity);
  if (terminator == nullptr) {
    return capacity;
  }
  return static_cast<const char *>(terminator) - value;
}

bool dateKeyIsValid(uint32_t dateKey) {
  if (dateKey == 0) {
    return true;
  }
  const uint16_t year = static_cast<uint16_t>(dateKey / 10000UL);
  const uint8_t month = static_cast<uint8_t>((dateKey / 100UL) % 100UL);
  const uint8_t date = static_cast<uint8_t>(dateKey % 100UL);
  uint32_t rebuilt = 0;
  return scheduledTaskDateKey(year, month, date, rebuilt) && rebuilt == dateKey;
}

bool timeSyncEpochIsValid(uint32_t epoch) {
  if (epoch == 0) {
    return true;
  }
  static constexpr uint32_t FIRST_SUPPORTED_UTC_EPOCH = 946656000UL;
  static constexpr uint32_t FIRST_UNSUPPORTED_UTC_EPOCH = 4102416000UL;
  return epoch >= FIRST_SUPPORTED_UTC_EPOCH &&
         epoch < FIRST_UNSUPPORTED_UTC_EPOCH;
}

}  // namespace

bool persistenceEncodeNetworkConfig(const NetworkConfig &config,
                                    PersistedNetworkConfigV1 &blob) {
  WifiPolicy policy;
  if (!wifiPolicyFromValue(static_cast<uint8_t>(config.policy), policy)) {
    return false;
  }

  const size_t ssidLength = terminatedLength(config.staSsid,
                                              sizeof(config.staSsid));
  const size_t passwordLength = terminatedLength(config.staPassword,
                                                  sizeof(config.staPassword));
  if (!wifiSsidIsValid(config.staSsid, ssidLength) ||
      !wifiPasswordIsValid(config.staPassword, passwordLength) ||
      (ssidLength == 0 && passwordLength != 0)) {
    return false;
  }

  PersistedNetworkConfigV1 next = {};
  next.version = NETWORK_CONFIG_BLOB_VERSION;
  next.policy = static_cast<uint8_t>(policy);
  memcpy(next.ssid, config.staSsid, ssidLength);
  memcpy(next.password, config.staPassword, passwordLength);
  blob = next;
  return true;
}

NetworkConfigBlobError persistenceDecodeNetworkConfig(const void *data,
                                                       size_t size,
                                                       NetworkConfig &config) {
  if (data == nullptr || size != sizeof(PersistedNetworkConfigV1)) {
    return NetworkConfigBlobError::InvalidSize;
  }

  PersistedNetworkConfigV1 blob;
  memcpy(&blob, data, sizeof(blob));
  if (blob.version != NETWORK_CONFIG_BLOB_VERSION) {
    return NetworkConfigBlobError::InvalidVersion;
  }

  WifiPolicy policy;
  if (!wifiPolicyFromValue(blob.policy, policy)) {
    return NetworkConfigBlobError::InvalidPolicy;
  }

  const size_t ssidLength = terminatedLength(blob.ssid, sizeof(blob.ssid));
  if (!wifiSsidIsValid(blob.ssid, ssidLength)) {
    return NetworkConfigBlobError::InvalidSsid;
  }

  const size_t passwordLength = terminatedLength(blob.password,
                                                  sizeof(blob.password));
  if (!wifiPasswordIsValid(blob.password, passwordLength) ||
      (ssidLength == 0 && passwordLength != 0)) {
    return NetworkConfigBlobError::InvalidPassword;
  }

  NetworkConfig decoded;
  decoded.policy = policy;
  memcpy(decoded.staSsid, blob.ssid, ssidLength);
  decoded.staSsid[ssidLength] = '\0';
  memcpy(decoded.staPassword, blob.password, passwordLength);
  decoded.staPassword[passwordLength] = '\0';
  config = decoded;
  return NetworkConfigBlobError::None;
}

bool persistenceEncodeScheduledTaskRecords(
    const ScheduledTaskRecords &records,
    PersistedScheduledTaskRecordsV1 &blob) {
  for (size_t index = 0; index < SCHEDULED_TASK_SLOT_COUNT; ++index) {
    if (!dateKeyIsValid(records.lastAttemptDateKeys[index])) {
      return false;
    }
  }

  PersistedScheduledTaskRecordsV1 next = {};
  next.version = SCHEDULED_TASK_RECORDS_BLOB_VERSION;
  next.slotCount = SCHEDULED_TASK_SLOT_COUNT;
  memcpy(next.lastAttemptDateKeys,
         records.lastAttemptDateKeys,
         sizeof(next.lastAttemptDateKeys));
  blob = next;
  return true;
}

TaskPersistenceBlobError persistenceDecodeScheduledTaskRecords(
    const void *data,
    size_t size,
    ScheduledTaskRecords &records) {
  if (data == nullptr || size != sizeof(PersistedScheduledTaskRecordsV1)) {
    return TaskPersistenceBlobError::InvalidSize;
  }

  PersistedScheduledTaskRecordsV1 blob;
  memcpy(&blob, data, sizeof(blob));
  if (blob.version != SCHEDULED_TASK_RECORDS_BLOB_VERSION) {
    return TaskPersistenceBlobError::InvalidVersion;
  }
  if (blob.slotCount != SCHEDULED_TASK_SLOT_COUNT) {
    return TaskPersistenceBlobError::InvalidSlotCount;
  }
  for (size_t index = 0; index < SCHEDULED_TASK_SLOT_COUNT; ++index) {
    if (!dateKeyIsValid(blob.lastAttemptDateKeys[index])) {
      return TaskPersistenceBlobError::InvalidDateKey;
    }
  }

  ScheduledTaskRecords decoded;
  memcpy(decoded.lastAttemptDateKeys,
         blob.lastAttemptDateKeys,
         sizeof(decoded.lastAttemptDateKeys));
  records = decoded;
  return TaskPersistenceBlobError::None;
}

bool persistenceEncodeTimeSyncResult(uint32_t lastSuccessEpoch,
                                     PersistedTimeSyncResultV1 &blob) {
  if (!timeSyncEpochIsValid(lastSuccessEpoch)) {
    return false;
  }
  PersistedTimeSyncResultV1 next = {};
  next.version = TIME_SYNC_RESULT_BLOB_VERSION;
  next.lastSuccessEpoch = lastSuccessEpoch;
  blob = next;
  return true;
}

TaskPersistenceBlobError persistenceDecodeTimeSyncResult(
    const void *data,
    size_t size,
    uint32_t &lastSuccessEpoch) {
  if (data == nullptr || size != sizeof(PersistedTimeSyncResultV1)) {
    return TaskPersistenceBlobError::InvalidSize;
  }

  PersistedTimeSyncResultV1 blob;
  memcpy(&blob, data, sizeof(blob));
  if (blob.version != TIME_SYNC_RESULT_BLOB_VERSION) {
    return TaskPersistenceBlobError::InvalidVersion;
  }
  if (!timeSyncEpochIsValid(blob.lastSuccessEpoch)) {
    return TaskPersistenceBlobError::InvalidEpoch;
  }
  lastSuccessEpoch = blob.lastSuccessEpoch;
  return TaskPersistenceBlobError::None;
}
