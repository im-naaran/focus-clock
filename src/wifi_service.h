#pragma once

#include <Arduino.h>

#include "network_types.h"

// Concrete consumers are added with their business feature, not predeclared here.
enum class WifiConsumer : uint8_t {};

struct WifiServiceState {
  WifiTargetMode currentMode = WifiTargetMode::Off;
  WifiTargetMode targetMode = WifiTargetMode::Off;
  uint8_t autoConsumerMask = 0;
  bool connectionTestDemand = false;
  bool scanPending = false;
  bool scanStarted = false;
  bool portalScanDemand = false;
  WifiTestState testState = WifiTestState::Idle;
  uint32_t testDeadlineMs = 0;
  WifiScanState scanState = WifiScanState::Idle;
  uint8_t scanResultCount = 0;
  WifiScanResult scanResults[AppConfig::WIFI_SCAN_MAX_RESULTS] = {};
  bool apStarted = false;
  bool staAttemptStarted = false;
  uint32_t nextModeRetryMs = 0;
  uint32_t nextStaRetryMs = 0;
  uint32_t nextApClientPollMs = 0;
  uint8_t apClientCount = 0;
  NetworkConfig activeStaConfig;
  char apSsid[18] = {};
};

void wifiServiceBegin(WifiServiceState &state, uint32_t nowMs);
bool wifiServiceUpdate(WifiServiceState &state,
                       const NetworkConfig &config,
                       bool configModeRequested,
                       WifiRuntimeView &view,
                       uint32_t nowMs);

bool wifiServiceRequestAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer,
                                   WifiPolicy policy,
                                   uint32_t nowMs);
void wifiServiceReleaseAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer);
bool wifiServiceStartScan(WifiServiceState &state,
                          bool configModeRequested,
                          uint32_t nowMs);
void wifiServiceCancelScan(WifiServiceState &state);
uint8_t wifiServiceScanResultCount(const WifiServiceState &state);
const WifiScanResult *wifiServiceScanResultAt(const WifiServiceState &state,
                                              uint8_t index);
bool wifiServiceStartConnectionTest(WifiServiceState &state,
                                    const NetworkConfig &savedConfig,
                                    bool configModeRequested,
                                    uint32_t nowMs);
void wifiServiceCancelConnectionTest(WifiServiceState &state);
WifiTestState wifiServiceConnectionTestState(const WifiServiceState &state);

void wifiServicePortalUrl(char *buffer, size_t bufferSize);
