#pragma once

#include <Arduino.h>

#include "network_types.h"
#include "persistence_codec.h"

struct NightScreenOffConfig {
  bool enabled = true;
  uint16_t offMinute = 0;
  uint16_t onMinute = 0;
};

bool persistenceBegin();
uint8_t persistenceLoadBrightness();
bool persistenceSaveBrightness(uint8_t level);
NightScreenOffConfig persistenceLoadNightScreenOff();
bool persistenceSaveNightScreenOff(const NightScreenOffConfig &config);
NetworkConfig persistenceLoadNetworkConfig();
bool persistenceSaveNetworkConfig(const NetworkConfig &config);
ScheduledTaskRecords persistenceLoadScheduledTaskRecords();
bool persistenceSaveScheduledTaskRecords(const ScheduledTaskRecords &records);
uint32_t persistenceLoadLastTimeSyncSuccessEpoch();
bool persistenceSaveLastTimeSyncSuccessEpoch(uint32_t epoch);
void persistenceEnd();
