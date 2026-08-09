#pragma once

#include <stdint.h>

#include "config_network.h"

enum class WifiPolicy : uint8_t {
  Off = 0,
  Auto = 1,
};

enum class WifiTargetMode : uint8_t {
  Off,
  Ap,
  Sta,
  ApSta,
};

enum class WifiConnectionState : uint8_t {
  Disabled,
  NotConfigured,
  Starting,
  Connecting,
  Connected,
  Failed,
};

enum class WifiScanState : uint8_t {
  Idle,
  Running,
  Complete,
  Failed,
};

enum class WifiTestState : uint8_t {
  Idle,
  Connecting,
  Succeeded,
  Failed,
  TimedOut,
};

struct WifiRuntimeView {
  bool configModeRunning = false;
  bool apClientConnected = false;
  bool networkTaskActive = false;
  WifiConnectionState connectionState = WifiConnectionState::Disabled;
  WifiScanState scanState = WifiScanState::Idle;
  WifiTestState testState = WifiTestState::Idle;
  char staIp[16] = {};
  char apSsid[18] = {};
  char portalUrl[24] = {};
};

struct WifiScanCandidate {
  char ssid[AppConfig::WIFI_SSID_MAX_BYTES + 1] = {};
  int32_t rssi = 0;
  bool secure = false;
};

struct WifiScanResult {
  char ssid[AppConfig::WIFI_SSID_MAX_BYTES + 1] = {};
  int32_t rssi = 0;
  bool secure = false;
};

struct NetworkConfig {
  WifiPolicy policy = WifiPolicy::Off;
  char staSsid[AppConfig::WIFI_SSID_MAX_BYTES + 1] = {};
  char staPassword[AppConfig::WIFI_PASSWORD_MAX_BYTES + 1] = {};
};

struct WifiModeInputs {
  bool configModeRequested = false;
  WifiPolicy policy = WifiPolicy::Off;
  bool autoTaskDemand = false;
  bool portalScanDemand = false;
  bool connectionTestDemand = false;
};

struct WifiTransitionPlan {
  bool stopSta = false;
  bool stopAp = false;
  bool setMode = false;
  bool startAp = false;
};
