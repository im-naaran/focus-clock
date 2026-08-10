#pragma once

#include <stddef.h>
#include <stdint.h>

#include "network_types.h"
#include "scheduled_task_logic.h"

static constexpr uint8_t NETWORK_CONFIG_BLOB_VERSION = 1;
static constexpr uint8_t SCHEDULED_TASK_RECORDS_BLOB_VERSION = 1;
static constexpr uint8_t TIME_SYNC_RESULT_BLOB_VERSION = 1;

struct PersistedNetworkConfigV1 {
  uint8_t version;
  uint8_t policy;
  char ssid[AppConfig::WIFI_SSID_MAX_BYTES + 1];
  char password[AppConfig::WIFI_PASSWORD_MAX_BYTES + 1];
};

static_assert(sizeof(PersistedNetworkConfigV1) == 100,
              "Network config V1 blob layout must remain stable");

struct ScheduledTaskRecords {
  uint32_t lastAttemptDateKeys[SCHEDULED_TASK_SLOT_COUNT] = {};
};

// V1 reserves four task slots so adding a task does not resize stored data.
struct PersistedScheduledTaskRecordsV1 {
  uint8_t version;
  uint8_t slotCount;
  uint8_t reserved[2];
  uint32_t lastAttemptDateKeys[SCHEDULED_TASK_SLOT_COUNT];
};

struct PersistedTimeSyncResultV1 {
  uint8_t version;
  uint8_t reserved[3];
  uint32_t lastSuccessEpoch;
};

static_assert(sizeof(PersistedScheduledTaskRecordsV1) == 20,
              "Scheduled task records V1 layout must remain stable");
static_assert(sizeof(PersistedTimeSyncResultV1) == 8,
              "Time sync result V1 layout must remain stable");

enum class NetworkConfigBlobError : uint8_t {
  None,
  InvalidSize,
  InvalidVersion,
  InvalidPolicy,
  InvalidSsid,
  InvalidPassword,
};

enum class TaskPersistenceBlobError : uint8_t {
  None,
  InvalidSize,
  InvalidVersion,
  InvalidSlotCount,
  InvalidDateKey,
  InvalidEpoch,
};

bool persistenceEncodeNetworkConfig(const NetworkConfig &config,
                                    PersistedNetworkConfigV1 &blob);
NetworkConfigBlobError persistenceDecodeNetworkConfig(const void *data,
                                                       size_t size,
                                                       NetworkConfig &config);
bool persistenceEncodeScheduledTaskRecords(
    const ScheduledTaskRecords &records,
    PersistedScheduledTaskRecordsV1 &blob);
TaskPersistenceBlobError persistenceDecodeScheduledTaskRecords(
    const void *data,
    size_t size,
    ScheduledTaskRecords &records);
bool persistenceEncodeTimeSyncResult(uint32_t lastSuccessEpoch,
                                     PersistedTimeSyncResultV1 &blob);
TaskPersistenceBlobError persistenceDecodeTimeSyncResult(
    const void *data,
    size_t size,
    uint32_t &lastSuccessEpoch);
