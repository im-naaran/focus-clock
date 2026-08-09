#pragma once

#include <WebServer.h>

#include "app_state.h"
#include "network_types.h"
#include "rtc_service.h"
#include "wifi_service.h"

struct WifiPortalState {
  WifiPortalState() : server(80) {}

  WebServer server;
  bool routesConfigured = false;
  bool running = false;
  AppState *app = nullptr;
  RtcServiceState *rtcService = nullptr;
  WifiServiceState *wifiService = nullptr;
};

void wifiPortalBegin(WifiPortalState &state,
                     AppState &app,
                     RtcServiceState &rtcService,
                     WifiServiceState &wifiService);
void wifiPortalUpdate(WifiPortalState &state,
                      bool configModeRequested,
                      const WifiRuntimeView &runtime);
void wifiPortalStop(WifiPortalState &state);
