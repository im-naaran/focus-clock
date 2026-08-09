#include "wifi_portal.h"

#include <WiFi.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "json_writer.h"
#include "persistence.h"
#include "portal_validation.h"
#include "rtc.h"
#include "wifi_logic.h"

using namespace AppConfig;

namespace {

// PlatformIO embed_txtfiles appends one NUL byte and exposes linker symbols.
extern const uint8_t WIFI_PORTAL_PAGE_START[]
    asm("_binary_web_wifi_portal_html_start");
extern const uint8_t WIFI_PORTAL_PAGE_END[]
    asm("_binary_web_wifi_portal_html_end");

bool requestArrivedViaSoftAp(WebServer &server) {
  // WebServer listens on every active interface, so localIP is the boundary.
  return server.client().localIP() == WiFi.softAPIP();
}

void sendJsonError(WebServer &server,
                   int status,
                   const char *code,
                   const char *message) {
  char response[256];
  if (!jsonWriteErrorEnvelope(response, sizeof(response), code, message)) {
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ERROR\",\"message\":\"Response overflow\"}}");
    return;
  }
  server.send(status, "application/json", response);
}

bool guardSoftAp(WebServer &server) {
  if (requestArrivedViaSoftAp(server)) {
    return true;
  }
  sendJsonError(server, 403, "INTERFACE_FORBIDDEN",
                "Configuration is available only through the Focus Clock AP");
  return false;
}

bool validatePost(WebServer &server) {
  if (!guardSoftAp(server)) {
    return false;
  }
  const int contentLength = server.clientContentLength();
  if (contentLength < 0 ||
      static_cast<size_t>(contentLength) > HTTP_MAX_BODY_BYTES) {
    sendJsonError(server, 413, "BODY_TOO_LARGE", "Request body is too large");
    return false;
  }
  String contentType = server.header("Content-Type");
  contentType.toLowerCase();
  if (!contentType.startsWith("application/x-www-form-urlencoded")) {
    sendJsonError(server, 415, "UNSUPPORTED_CONTENT_TYPE",
                  "Use application/x-www-form-urlencoded");
    return false;
  }
  return true;
}

bool appendNumber(JsonWriter &writer, uint32_t value) {
  char text[12];
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(value));
  return jsonWriterAppend(writer, text);
}

bool appendSignedNumber(JsonWriter &writer, int32_t value) {
  char text[12];
  snprintf(text, sizeof(text), "%ld", static_cast<long>(value));
  return jsonWriterAppend(writer, text);
}

bool appendBool(JsonWriter &writer, bool value) {
  return jsonWriterAppend(writer, value ? "true" : "false");
}

const char *connectionStateName(WifiConnectionState state) {
  switch (state) {
    case WifiConnectionState::Disabled: return "disabled";
    case WifiConnectionState::NotConfigured: return "notConfigured";
    case WifiConnectionState::Starting: return "starting";
    case WifiConnectionState::Connecting: return "connecting";
    case WifiConnectionState::Connected: return "connected";
    case WifiConnectionState::Failed: return "failed";
  }
  return "disabled";
}

const char *scanStateName(WifiScanState state) {
  switch (state) {
    case WifiScanState::Idle: return "idle";
    case WifiScanState::Running: return "running";
    case WifiScanState::Complete: return "complete";
    case WifiScanState::Failed: return "failed";
  }
  return "idle";
}

const char *testStateName(WifiTestState state) {
  switch (state) {
    case WifiTestState::Idle: return "idle";
    case WifiTestState::Connecting: return "connecting";
    case WifiTestState::Succeeded: return "succeeded";
    case WifiTestState::Failed: return "failed";
    case WifiTestState::TimedOut: return "timedOut";
  }
  return "idle";
}

bool appendQuoted(JsonWriter &writer, const char *value) {
  return jsonWriterAppend(writer, "\"") &&
         jsonWriterAppendEscaped(writer, value, strlen(value)) &&
         jsonWriterAppend(writer, "\"");
}

void handleConfigGet(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!guardSoftAp(server)) return;
  if (portal.app == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "Application state is not ready");
    return;
  }
  const AppState &app = *portal.app;
  char response[1536];
  JsonWriter writer;
  jsonWriterBegin(writer, response, sizeof(response));
  bool ok = jsonWriterAppend(writer, "{\"ok\":true,\"data\":{\"brightness\":") &&
            appendNumber(writer, app.config.brightnessLevel) &&
            jsonWriterAppend(writer, ",\"rtc\":{\"valid\":") &&
            appendBool(writer, app.rtcOk && app.rtcTime.valid) &&
            jsonWriterAppend(writer, ",\"year\":") && appendNumber(writer, app.rtcTime.year) &&
            jsonWriterAppend(writer, ",\"month\":") && appendNumber(writer, app.rtcTime.month) &&
            jsonWriterAppend(writer, ",\"date\":") && appendNumber(writer, app.rtcTime.date) &&
            jsonWriterAppend(writer, ",\"hour\":") && appendNumber(writer, app.rtcTime.hour) &&
            jsonWriterAppend(writer, ",\"minute\":") && appendNumber(writer, app.rtcTime.minute) &&
            jsonWriterAppend(writer, ",\"second\":") && appendNumber(writer, app.rtcTime.second) &&
            jsonWriterAppend(writer, "},\"night\":{\"enabled\":") &&
            appendBool(writer, app.config.nightScreenOffEnabled) &&
            jsonWriterAppend(writer, ",\"offMinute\":") && appendNumber(writer, app.config.nightScreenOffMinute) &&
            jsonWriterAppend(writer, ",\"onMinute\":") && appendNumber(writer, app.config.nightScreenOnMinute) &&
            jsonWriterAppend(writer, "},\"wifi\":{\"policy\":") &&
            appendQuoted(writer, app.networkConfig.policy == WifiPolicy::Auto ? "AUTO" : "OFF") &&
            jsonWriterAppend(writer, ",\"ssid\":") && appendQuoted(writer, app.networkConfig.staSsid) &&
            jsonWriterAppend(writer, ",\"passwordConfigured\":") &&
            appendBool(writer, app.networkConfig.staPassword[0] != '\0') &&
            jsonWriterAppend(writer, "},\"runtime\":{\"configMode\":") &&
            appendBool(writer, app.wifiRuntime.configModeRunning) &&
            jsonWriterAppend(writer, ",\"apClient\":") &&
            appendBool(writer, app.wifiRuntime.apClientConnected) &&
            jsonWriterAppend(writer, ",\"connection\":") &&
            appendQuoted(writer, connectionStateName(app.wifiRuntime.connectionState)) &&
            jsonWriterAppend(writer, ",\"scan\":") &&
            appendQuoted(writer, scanStateName(app.wifiRuntime.scanState)) &&
            jsonWriterAppend(writer, ",\"test\":") &&
            appendQuoted(writer, testStateName(app.wifiRuntime.testState)) &&
            jsonWriterAppend(writer, ",\"staIp\":") && appendQuoted(writer, app.wifiRuntime.staIp) &&
            jsonWriterAppend(writer, "}}}");
  if (!ok) {
    sendJsonError(server, 500, "INTERNAL_ERROR", "Configuration response overflow");
    return;
  }
  server.send(200, "application/json", response);
}

bool readUnsignedArg(WebServer &server,
                     const char *name,
                     uint16_t minimum,
                     uint16_t maximum,
                     uint16_t &value) {
  if (!server.hasArg(name)) return false;
  const String text = server.arg(name);
  return portalParseUnsigned(text.c_str(), text.length(), minimum, maximum, value);
}

struct ConfigSubmission {
  uint8_t brightness = 0;
  NightScreenOffConfig night;
  NetworkConfig network;
  bool setRtc = false;
  RtcTime rtc;
};

bool parseConfigSubmission(WebServer &server,
                           const AppState &app,
                           ConfigSubmission &submission) {
  uint16_t brightness;
  uint16_t nightEnabled;
  uint16_t nightOff;
  uint16_t nightOn;
  uint16_t setRtc;
  if (!readUnsignedArg(server, "brightness", MIN_BRIGHTNESS_LEVEL,
                       MAX_BRIGHTNESS_LEVEL, brightness) ||
      !readUnsignedArg(server, "nightEnabled", 0, 1, nightEnabled) ||
      !readUnsignedArg(server, "nightOffMinute", 0, MINUTES_PER_DAY - 1, nightOff) ||
      !readUnsignedArg(server, "nightOnMinute", 0, MINUTES_PER_DAY - 1, nightOn) ||
      !readUnsignedArg(server, "setRtc", 0, 1, setRtc) ||
      !server.hasArg("wifiPolicy") || !server.hasArg("ssid") ||
      !server.hasArg("password")) {
    return false;
  }

  const String policyText = server.arg("wifiPolicy");
  WifiPolicy policy;
  if (policyText == "OFF") {
    policy = WifiPolicy::Off;
  } else if (policyText == "AUTO") {
    policy = WifiPolicy::Auto;
  } else {
    return false;
  }
  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  NetworkConfig candidate;
  if (makeNetworkConfigCandidate(app.networkConfig, policy,
                                 ssid.c_str(), ssid.length(),
                                 password.c_str(), password.length(),
                                 candidate) != NetworkConfigValidationError::None) {
    return false;
  }

  submission.brightness = static_cast<uint8_t>(brightness);
  submission.night.enabled = nightEnabled != 0;
  submission.night.offMinute = nightOff;
  submission.night.onMinute = nightOn;
  submission.network = candidate;
  submission.setRtc = setRtc != 0;
  if (!submission.setRtc) {
    return true;
  }

  uint16_t year, month, date, hour, minute, second;
  if (!readUnsignedArg(server, "rtcYear", 2000, 2099, year) ||
      !readUnsignedArg(server, "rtcMonth", 1, 12, month) ||
      !readUnsignedArg(server, "rtcDate", 1, 31, date) ||
      !readUnsignedArg(server, "rtcHour", 0, 23, hour) ||
      !readUnsignedArg(server, "rtcMinute", 0, 59, minute) ||
      !readUnsignedArg(server, "rtcSecond", 0, 59, second) ||
      !portalRtcDateTimeIsValid(year, month, date, hour, minute, second)) {
    return false;
  }
  submission.rtc.year = year;
  submission.rtc.month = month;
  submission.rtc.date = date;
  submission.rtc.hour = hour;
  submission.rtc.minute = minute;
  submission.rtc.second = second;
  submission.rtc.day = portalWeekdayFromDate(year, month, date);
  submission.rtc.valid = true;
  return true;
}

void sendApplyResult(WebServer &server,
                     bool brightnessOk,
                     bool nightOk,
                     bool networkOk,
                     bool rtcOk) {
  if (brightnessOk && nightOk && networkOk && rtcOk) {
    server.send(200, "application/json",
                "{\"ok\":true,\"data\":{\"applied\":true}}");
    return;
  }
  char response[384];
  JsonWriter writer;
  jsonWriterBegin(writer, response, sizeof(response));
  const bool ok = jsonWriterAppend(
                      writer,
                      "{\"ok\":false,\"error\":{\"code\":\"APPLY_PARTIAL\",\"message\":\"One or more sections failed\"},\"data\":{\"brightness\":") &&
                  appendBool(writer, brightnessOk) &&
                  jsonWriterAppend(writer, ",\"night\":") &&
                  appendBool(writer, nightOk) &&
                  jsonWriterAppend(writer, ",\"network\":") &&
                  appendBool(writer, networkOk) &&
                  jsonWriterAppend(writer, ",\"rtc\":") &&
                  appendBool(writer, rtcOk) &&
                  jsonWriterAppend(writer, "}}");
  if (!ok) {
    sendJsonError(server, 500, "INTERNAL_ERROR", "Apply response overflow");
    return;
  }
  server.send(500, "application/json", response);
}

void handleConfigPost(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!validatePost(server)) return;
  if (portal.app == nullptr || portal.rtcService == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "Application state is not ready");
    return;
  }
  if (portal.wifiService != nullptr &&
      wifiServiceConnectionTestState(*portal.wifiService) ==
          WifiTestState::Connecting) {
    sendJsonError(server, 409, "TEST_RUNNING",
                  "Configuration cannot be saved during a connection test");
    return;
  }

  AppState &app = *portal.app;
  ConfigSubmission submission;
  if (!parseConfigSubmission(server, app, submission)) {
    sendJsonError(server, 400, "INVALID_REQUEST",
                  "One or more configuration fields are invalid");
    return;
  }

  // Every field is validated before the first NVS or RTC write occurs.
  const bool brightnessOk = persistenceSaveBrightness(submission.brightness);
  if (brightnessOk) {
    app.config.brightnessLevel = submission.brightness;
    displaySetContrast(brightnessLevelToContrast(submission.brightness));
  }
  const bool nightOk = persistenceSaveNightScreenOff(submission.night);
  if (nightOk) {
    app.config.nightScreenOffEnabled = submission.night.enabled;
    app.config.nightScreenOffMinute = submission.night.offMinute;
    app.config.nightScreenOnMinute = submission.night.onMinute;
  }
  const bool networkOk = persistenceSaveNetworkConfig(submission.network);
  if (networkOk) {
    app.networkConfig = submission.network;
  }

  bool rtcOk = true;
  if (submission.setRtc) {
    rtcOk = rtcSetTime(submission.rtc) &&
            rtcServiceForceRead(*portal.rtcService, app, millis());
  }
  app.displayDirty = true;
  sendApplyResult(server, brightnessOk, nightOk, networkOk, rtcOk);
}

void handleWifiClear(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!validatePost(server)) return;
  if (portal.app == nullptr || portal.wifiService == nullptr ||
      !server.hasArg("confirm") || server.arg("confirm") != "1") {
    sendJsonError(server, 400, "CONFIRM_REQUIRED",
                  "Explicit clear confirmation is required");
    return;
  }
  AppState &app = *portal.app;
  if (wifiServiceConnectionTestState(*portal.wifiService) ==
      WifiTestState::Connecting) {
    sendJsonError(server, 409, "TEST_RUNNING",
                  "WiFi configuration cannot be cleared during a test");
    return;
  }
  NetworkConfig cleared = app.networkConfig;
  cleared.staSsid[0] = '\0';
  cleared.staPassword[0] = '\0';
  if (!persistenceSaveNetworkConfig(cleared)) {
    sendJsonError(server, 500, "PERSISTENCE_FAILED",
                  "WiFi configuration could not be cleared");
    return;
  }
  app.networkConfig = cleared;
  wifiServiceCancelConnectionTest(*portal.wifiService);
  app.displayDirty = true;
  server.send(200, "application/json",
              "{\"ok\":true,\"data\":{\"cleared\":true,\"passwordConfigured\":false}}");
}

void handleScanPost(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!validatePost(server)) return;
  if (portal.app == nullptr || portal.wifiService == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "WiFi service is not ready");
    return;
  }
  if (!wifiServiceStartScan(*portal.wifiService,
                            portal.app->configModeRequested, millis())) {
    sendJsonError(server, 409, "SCAN_CONFLICT",
                  "A scan or connection test is already running");
    return;
  }
  portal.app->displayDirty = true;
  server.send(202, "application/json",
              "{\"ok\":true,\"data\":{\"state\":\"running\"}}");
}

void handleScanGet(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!guardSoftAp(server)) return;
  if (portal.wifiService == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "WiFi service is not ready");
    return;
  }
  char response[3072];
  JsonWriter writer;
  jsonWriterBegin(writer, response, sizeof(response));
  bool ok = jsonWriterAppend(writer, "{\"ok\":true,\"data\":{\"state\":") &&
            appendQuoted(writer, scanStateName(portal.wifiService->scanState)) &&
            jsonWriterAppend(writer, ",\"results\":[");
  const uint8_t count = wifiServiceScanResultCount(*portal.wifiService);
  for (uint8_t index = 0; ok && index < count; ++index) {
    const WifiScanResult *result =
        wifiServiceScanResultAt(*portal.wifiService, index);
    ok = result != nullptr && (index == 0 || jsonWriterAppend(writer, ",")) &&
         jsonWriterAppend(writer, "{\"ssid\":") &&
         appendQuoted(writer, result->ssid) &&
         jsonWriterAppend(writer, ",\"rssi\":") &&
         appendSignedNumber(writer, result->rssi) &&
         jsonWriterAppend(writer, ",\"secure\":") &&
         appendBool(writer, result->secure) && jsonWriterAppend(writer, "}");
  }
  ok = ok && jsonWriterAppend(writer, "]}}");
  if (!ok) {
    sendJsonError(server, 500, "INTERNAL_ERROR", "Scan response overflow");
    return;
  }
  server.send(200, "application/json", response);
}

void handleTestPost(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!validatePost(server)) return;
  if (portal.app == nullptr || portal.wifiService == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "WiFi service is not ready");
    return;
  }
  if (portal.app->networkConfig.staSsid[0] == '\0') {
    sendJsonError(server, 409, "SSID_NOT_CONFIGURED",
                  "Save a WiFi network before testing");
    return;
  }
  if (!wifiServiceStartConnectionTest(*portal.wifiService,
                                      portal.app->networkConfig,
                                      portal.app->configModeRequested,
                                      millis())) {
    sendJsonError(server, 409, "TEST_CONFLICT",
                  "A scan or connection test is already running");
    return;
  }
  portal.app->displayDirty = true;
  server.send(202, "application/json",
              "{\"ok\":true,\"data\":{\"state\":\"connecting\"}}");
}

void handleTestGet(WifiPortalState &portal) {
  WebServer &server = portal.server;
  if (!guardSoftAp(server)) return;
  if (portal.wifiService == nullptr) {
    sendJsonError(server, 503, "NOT_READY", "WiFi service is not ready");
    return;
  }
  char response[160];
  JsonWriter writer;
  jsonWriterBegin(writer, response, sizeof(response));
  const bool ok = jsonWriterAppend(writer, "{\"ok\":true,\"data\":{\"state\":") &&
                  appendQuoted(writer, testStateName(portal.wifiService->testState)) &&
                  jsonWriterAppend(writer, "}}");
  if (!ok) {
    sendJsonError(server, 500, "INTERNAL_ERROR", "Test response overflow");
    return;
  }
  server.send(200, "application/json", response);
}

void configureRoutes(WifiPortalState &state) {
  WebServer &server = state.server;
  WifiPortalState *portal = &state;
  const char *headers[] = {"Content-Type"};
  server.collectHeaders(headers, 1);

  server.on("/", HTTP_GET, [&server]() {
    if (!guardSoftAp(server)) return;
    const size_t embeddedSize = static_cast<size_t>(
        WIFI_PORTAL_PAGE_END - WIFI_PORTAL_PAGE_START);
    const size_t pageSize = embeddedSize > 0 ? embeddedSize - 1 : 0;
    server.send_P(200, PSTR("text/html; charset=utf-8"),
                  reinterpret_cast<PGM_P>(WIFI_PORTAL_PAGE_START), pageSize);
  });
  server.on("/api/config", HTTP_GET, [portal]() {
    handleConfigGet(*portal);
  });
  server.on("/api/config", HTTP_POST, [portal]() {
    handleConfigPost(*portal);
  });
  server.on("/api/wifi/clear", HTTP_POST, [portal]() {
    handleWifiClear(*portal);
  });
  server.on("/api/wifi/scan", HTTP_POST, [portal]() {
    handleScanPost(*portal);
  });
  server.on("/api/wifi/scan", HTTP_GET, [portal]() {
    handleScanGet(*portal);
  });
  server.on("/api/wifi/test", HTTP_POST, [portal]() {
    handleTestPost(*portal);
  });
  server.on("/api/wifi/test", HTTP_GET, [portal]() {
    handleTestGet(*portal);
  });
  server.onNotFound([&server]() {
    if (!guardSoftAp(server)) return;
    sendJsonError(server, 404, "NOT_FOUND", "Route not found");
  });
  state.routesConfigured = true;
}

}  // namespace

void wifiPortalBegin(WifiPortalState &state,
                     AppState &app,
                     RtcServiceState &rtcService,
                     WifiServiceState &wifiService) {
  state.app = &app;
  state.rtcService = &rtcService;
  state.wifiService = &wifiService;
  if (!state.routesConfigured) {
    configureRoutes(state);
  }
}

void wifiPortalUpdate(WifiPortalState &state,
                      bool configModeRequested,
                      const WifiRuntimeView &runtime) {
  const bool shouldRun = configModeRequested && runtime.configModeRunning;
  if (shouldRun && !state.running) {
    state.server.begin();
    state.running = true;
  } else if (!shouldRun && state.running) {
    wifiPortalStop(state);
  }
  if (state.running) {
    state.server.handleClient();
  }
}

void wifiPortalStop(WifiPortalState &state) {
  if (!state.running) {
    return;
  }
  state.server.stop();
  state.running = false;
}
