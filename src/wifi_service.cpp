#include "wifi_service.h"

#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "wifi_logic.h"

using namespace AppConfig;

namespace {

bool modeHasAp(WifiTargetMode mode) {
  return mode == WifiTargetMode::Ap || mode == WifiTargetMode::ApSta;
}

bool modeHasSta(WifiTargetMode mode) {
  return mode == WifiTargetMode::Sta || mode == WifiTargetMode::ApSta;
}

wifi_mode_t arduinoModeFor(WifiTargetMode mode) {
  switch (mode) {
    case WifiTargetMode::Off:
      return WIFI_OFF;
    case WifiTargetMode::Ap:
      return WIFI_AP;
    case WifiTargetMode::Sta:
      return WIFI_STA;
    case WifiTargetMode::ApSta:
      return WIFI_AP_STA;
  }
  return WIFI_OFF;
}

bool sameCredentials(const NetworkConfig &left, const NetworkConfig &right) {
  return strcmp(left.staSsid, right.staSsid) == 0 &&
         strcmp(left.staPassword, right.staPassword) == 0;
}

bool startOpenAp(WifiServiceState &state) {
  const IPAddress ip(WIFI_CONFIG_AP_IP[0], WIFI_CONFIG_AP_IP[1],
                     WIFI_CONFIG_AP_IP[2], WIFI_CONFIG_AP_IP[3]);
  const IPAddress gateway(WIFI_CONFIG_AP_GATEWAY[0], WIFI_CONFIG_AP_GATEWAY[1],
                          WIFI_CONFIG_AP_GATEWAY[2], WIFI_CONFIG_AP_GATEWAY[3]);
  const IPAddress subnet(WIFI_CONFIG_AP_SUBNET[0], WIFI_CONFIG_AP_SUBNET[1],
                         WIFI_CONFIG_AP_SUBNET[2], WIFI_CONFIG_AP_SUBNET[3]);
  if (!WiFi.softAPConfig(ip, gateway, subnet)) {
    return false;
  }
  // Omitting a password is deliberate: the AP exists only after local consent.
  state.apStarted = WiFi.softAP(state.apSsid, nullptr,
                               WIFI_CONFIG_AP_CHANNEL, false);
  return state.apStarted;
}

void stopSta(WifiServiceState &state) {
  WiFi.disconnect(false, false);
  state.staAttemptStarted = false;
  state.nextStaRetryMs = 0;
  state.activeStaConfig = NetworkConfig{};
}

void startSta(WifiServiceState &state,
              const NetworkConfig &config,
              uint32_t nowMs);

void clearScanState(WifiServiceState &state) {
  if (state.scanStarted || state.scanPending) {
    WiFi.scanDelete();
  }
  state.scanPending = false;
  state.scanStarted = false;
  state.portalScanDemand = false;
}

void finishScan(WifiServiceState &state, bool failed) {
  if (failed) {
    state.scanState = WifiScanState::Failed;
  }
  WiFi.scanDelete();
  state.scanStarted = false;
  state.scanPending = false;
  state.portalScanDemand = false;
}

void startPendingScan(WifiServiceState &state) {
  WiFi.scanDelete();
  const int16_t status = WiFi.scanNetworks(true, false);
  if (status == WIFI_SCAN_RUNNING) {
    state.scanStarted = true;
    return;
  }
  state.scanState = WifiScanState::Failed;
  state.scanPending = false;
  state.portalScanDemand = false;
}

void updateScan(WifiServiceState &state) {
  if (!state.scanStarted) {
    return;
  }
  const int16_t status = WiFi.scanComplete();
  if (status == WIFI_SCAN_RUNNING) {
    return;
  }
  if (status < 0) {
    finishScan(state, true);
    return;
  }

  const uint16_t driverCount = static_cast<uint16_t>(status);
  state.scanResultCount = 0;
  for (uint16_t index = 0; index < driverCount; ++index) {
    WifiScanCandidate candidate;
    const String ssid = WiFi.SSID(index);
    const size_t length = ssid.length();
    if (length > WIFI_SSID_MAX_BYTES) {
      continue;
    }
    memcpy(candidate.ssid, ssid.c_str(), length);
    candidate.ssid[length] = '\0';
    candidate.rssi = WiFi.RSSI(index);
    candidate.secure = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    state.scanResultCount = static_cast<uint8_t>(wifiMergeScanCandidate(
        candidate, state.scanResults, state.scanResultCount,
        WIFI_SCAN_MAX_RESULTS));
  }
  state.scanState = WifiScanState::Complete;
  finishScan(state, false);
}

bool advanceConnectionTest(WifiServiceState &state,
                           const NetworkConfig &config,
                           uint32_t nowMs) {
  if (!state.connectionTestDemand) {
    return false;
  }
  if (!modeHasSta(state.currentMode)) {
    return false;
  }
  if (!state.staAttemptStarted) {
    startSta(state, config, nowMs);
  }
  const wl_status_t status = WiFi.status();
  const WifiTestState next = wifiTestStateAdvance(
      state.testState, status == WL_CONNECTED,
      status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED,
      wifiDeadlineReached(nowMs, state.testDeadlineMs));
  state.testState = next;
  if (next == WifiTestState::Succeeded || next == WifiTestState::Failed ||
      next == WifiTestState::TimedOut) {
    state.connectionTestDemand = false;
    stopSta(state);
    return true;
  }
  return false;
}

void startSta(WifiServiceState &state,
              const NetworkConfig &config,
              uint32_t nowMs) {
  if (config.staSsid[0] == '\0') {
    state.staAttemptStarted = false;
    state.activeStaConfig = NetworkConfig{};
    return;
  }
  WiFi.begin(config.staSsid,
             config.staPassword[0] == '\0' ? nullptr : config.staPassword);
  state.activeStaConfig = config;
  state.staAttemptStarted = true;
  state.nextStaRetryMs = nowMs + WIFI_STA_RECONNECT_MS;
}

bool reconcileMode(WifiServiceState &state, uint32_t nowMs) {
  if (state.currentMode == state.targetMode) {
    return true;
  }
  if (!wifiDeadlineReached(nowMs, state.nextModeRetryMs)) {
    return false;
  }

  const WifiTransitionPlan plan =
      wifiTransitionPlanFor(state.currentMode, state.targetMode);
  // STA is released before reducing the mode; an existing AP is retained for
  // Ap <-> ApSta transitions so portal clients are not dropped unnecessarily.
  if (plan.stopSta) {
    stopSta(state);
  }
  if (plan.stopAp) {
    WiFi.softAPdisconnect(false);
    state.apStarted = false;
    state.apClientCount = 0;
  }
  if (plan.setMode && !WiFi.mode(arduinoModeFor(state.targetMode))) {
    state.nextModeRetryMs = nowMs + WIFI_MODE_RETRY_MS;
    return false;
  }
  if (plan.startAp && !startOpenAp(state)) {
    state.nextModeRetryMs = nowMs + WIFI_MODE_RETRY_MS;
    return false;
  }
  state.currentMode = state.targetMode;
  state.nextModeRetryMs = 0;
  return true;
}

WifiConnectionState updateSta(WifiServiceState &state,
                              const NetworkConfig &config,
                              uint32_t nowMs) {
  if (!modeHasSta(state.currentMode)) {
    return WifiConnectionState::Disabled;
  }
  if (config.staSsid[0] == '\0') {
    if (state.staAttemptStarted) {
      stopSta(state);
    }
    return WifiConnectionState::NotConfigured;
  }
  if (!sameCredentials(state.activeStaConfig, config)) {
    stopSta(state);
    startSta(state, config, nowMs);
    return WifiConnectionState::Starting;
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    return WifiConnectionState::Connected;
  }
  if (!state.staAttemptStarted ||
      wifiDeadlineReached(nowMs, state.nextStaRetryMs)) {
    WiFi.disconnect(false, false);
    startSta(state, config, nowMs);
    return WifiConnectionState::Starting;
  }
  if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
    return WifiConnectionState::Failed;
  }
  return WifiConnectionState::Connecting;
}

bool runtimeViewsEqual(const WifiRuntimeView &left,
                       const WifiRuntimeView &right) {
  return left.configModeRunning == right.configModeRunning &&
         left.apClientConnected == right.apClientConnected &&
         left.networkTaskActive == right.networkTaskActive &&
         left.connectionState == right.connectionState &&
         left.scanState == right.scanState &&
         left.testState == right.testState &&
         strcmp(left.staIp, right.staIp) == 0 &&
         strcmp(left.apSsid, right.apSsid) == 0 &&
         strcmp(left.portalUrl, right.portalUrl) == 0;
}

void updateStaIp(WifiRuntimeView &view) {
  view.staIp[0] = '\0';
  if (view.connectionState != WifiConnectionState::Connected) {
    return;
  }
  const IPAddress ip = WiFi.localIP();
  snprintf(view.staIp, sizeof(view.staIp), "%u.%u.%u.%u",
           ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

void wifiServiceBegin(WifiServiceState &state, uint32_t nowMs) {
  state = WifiServiceState{};
  const uint16_t suffix = static_cast<uint16_t>(ESP.getEfuseMac());
  snprintf(state.apSsid, sizeof(state.apSsid), "%s%04X",
           WIFI_CONFIG_AP_SSID_PREFIX, suffix);
  state.nextApClientPollMs = nowMs;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool wifiServiceUpdate(WifiServiceState &state,
                       const NetworkConfig &config,
                       bool configModeRequested,
                       WifiRuntimeView &view,
                       uint32_t nowMs) {
  WifiModeInputs inputs;
  inputs.configModeRequested = configModeRequested;
  inputs.policy = config.policy;
  inputs.autoTaskDemand = state.autoConsumerMask != 0;
  inputs.portalScanDemand = state.portalScanDemand;
  inputs.connectionTestDemand = state.connectionTestDemand;
  if (!configModeRequested && state.portalScanDemand) {
    clearScanState(state);
    inputs.portalScanDemand = false;
  }
  if (!configModeRequested && state.connectionTestDemand) {
    wifiServiceCancelConnectionTest(state);
    inputs.connectionTestDemand = false;
  }
  state.targetMode = wifiTargetModeFor(inputs);

  reconcileMode(state, nowMs);
  if (state.scanPending && modeHasSta(state.currentMode) &&
      !state.scanStarted) {
    startPendingScan(state);
  }
  updateScan(state);
  const bool testFinished = advanceConnectionTest(state, config, nowMs);
  WifiConnectionState connectionState = WifiConnectionState::Disabled;
  if (state.connectionTestDemand || testFinished) {
    connectionState = WifiConnectionState::Starting;
  } else {
    inputs.connectionTestDemand = false;
    if (wifiStaConnectionNeededFor(inputs)) {
      connectionState = updateSta(state, config, nowMs);
    }
  }

  if (modeHasAp(state.currentMode) && state.apStarted &&
      wifiDeadlineReached(nowMs, state.nextApClientPollMs)) {
    state.apClientCount = WiFi.softAPgetStationNum();
    state.nextApClientPollMs = nowMs + WIFI_AP_CLIENT_POLL_MS;
  } else if (!modeHasAp(state.currentMode)) {
    state.apClientCount = 0;
  }

  WifiRuntimeView next = view;
  next.configModeRunning = modeHasAp(state.currentMode) && state.apStarted;
  next.apClientConnected = state.apClientCount > 0;
  next.networkTaskActive = state.autoConsumerMask != 0 ||
                           state.connectionTestDemand || state.portalScanDemand;
  next.connectionState = connectionState;
  next.scanState = state.scanState;
  next.testState = state.testState;
  snprintf(next.apSsid, sizeof(next.apSsid), "%s", state.apSsid);
  wifiServicePortalUrl(next.portalUrl, sizeof(next.portalUrl));
  updateStaIp(next);

  if (runtimeViewsEqual(view, next)) {
    return false;
  }
  view = next;
  return true;
}

bool wifiServiceRequestAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer,
                                   WifiPolicy policy,
                                   uint32_t nowMs) {
  if (policy != WifiPolicy::Auto) {
    return false;
  }
  state.autoConsumerMask |= static_cast<uint8_t>(consumer);
  state.nextStaRetryMs = nowMs;
  return true;
}

void wifiServiceReleaseAutoNetwork(WifiServiceState &state,
                                   WifiConsumer consumer) {
  state.autoConsumerMask &= ~static_cast<uint8_t>(consumer);
}

bool wifiServiceStartScan(WifiServiceState &state,
                          bool configModeRequested,
                          uint32_t nowMs) {
  if (!configModeRequested || state.scanPending || state.scanStarted ||
      state.connectionTestDemand) {
    return false;
  }
  state.portalScanDemand = true;
  state.scanPending = true;
  state.scanState = WifiScanState::Running;
  state.scanResultCount = 0;
  memset(state.scanResults, 0, sizeof(state.scanResults));
  state.nextModeRetryMs = nowMs;
  return true;
}

void wifiServiceCancelScan(WifiServiceState &state) {
  clearScanState(state);
  state.scanState = WifiScanState::Idle;
  state.scanResultCount = 0;
  memset(state.scanResults, 0, sizeof(state.scanResults));
}

bool wifiServiceStartConnectionTest(WifiServiceState &state,
                                    const NetworkConfig &savedConfig,
                                    bool configModeRequested,
                                    uint32_t nowMs) {
  if (!configModeRequested || savedConfig.staSsid[0] == '\0' ||
      state.scanPending || state.scanStarted ||
      state.connectionTestDemand) {
    return false;
  }
  state.connectionTestDemand = true;
  state.testState = WifiTestState::Connecting;
  state.testDeadlineMs = nowMs + WIFI_STA_CONNECT_TIMEOUT_MS;
  state.nextModeRetryMs = nowMs;
  return true;
}

void wifiServiceCancelConnectionTest(WifiServiceState &state) {
  if (state.connectionTestDemand) {
    state.connectionTestDemand = false;
    stopSta(state);
  }
  state.testState = WifiTestState::Idle;
}

WifiTestState wifiServiceConnectionTestState(const WifiServiceState &state) {
  return state.testState;
}

uint8_t wifiServiceScanResultCount(const WifiServiceState &state) {
  return state.scanResultCount;
}

const WifiScanResult *wifiServiceScanResultAt(const WifiServiceState &state,
                                              uint8_t index) {
  return index < state.scanResultCount ? &state.scanResults[index] : nullptr;
}

void wifiServicePortalUrl(char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return;
  }
  snprintf(buffer, bufferSize, "http://%u.%u.%u.%u/",
           WIFI_CONFIG_AP_IP[0], WIFI_CONFIG_AP_IP[1],
           WIFI_CONFIG_AP_IP[2], WIFI_CONFIG_AP_IP[3]);
}
