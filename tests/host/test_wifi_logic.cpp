#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wifi_logic.h"
#include "persistence_codec.h"

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    ++failures;
    fprintf(stderr, "FAIL: %s\n", message);
  }
}

void testPolicyValues() {
  WifiPolicy policy = WifiPolicy::Auto;
  expect(wifiPolicyFromValue(0, policy) && policy == WifiPolicy::Off,
         "policy value 0 maps to OFF");
  expect(wifiPolicyFromValue(1, policy) && policy == WifiPolicy::Auto,
         "policy value 1 maps to AUTO");
  expect(!wifiPolicyFromValue(2, policy) && policy == WifiPolicy::Off,
         "unknown policy falls back to OFF");
  expect(!wifiPolicyFromValue(255, policy) && policy == WifiPolicy::Off,
         "maximum invalid policy falls back to OFF");
}

void testTargetModes() {
  WifiModeInputs inputs;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::Off,
         "no demands select WIFI_OFF");

  inputs.configModeRequested = true;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::Ap,
         "config mode selects WIFI_AP");

  inputs.configModeRequested = false;
  inputs.policy = WifiPolicy::Auto;
  inputs.autoTaskDemand = true;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::Sta,
         "AUTO task selects WIFI_STA");

  inputs.configModeRequested = true;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::ApSta,
         "config mode plus AUTO task selects WIFI_AP_STA");

  inputs.policy = WifiPolicy::Off;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::Ap,
         "OFF rejects ordinary AUTO task demand");

  inputs.connectionTestDemand = true;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::ApSta,
         "connection test can add STA while policy is OFF");

  inputs.configModeRequested = false;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::Sta,
         "connection test demand independently maps to STA");

  inputs = WifiModeInputs{};
  inputs.configModeRequested = true;
  inputs.portalScanDemand = true;
  expect(wifiTargetModeFor(inputs) == WifiTargetMode::ApSta,
         "portal scan enables the STA interface");
  expect(!wifiStaConnectionNeededFor(inputs),
         "portal scan does not connect saved credentials");

  inputs.portalScanDemand = false;
  inputs.connectionTestDemand = true;
  expect(wifiStaConnectionNeededFor(inputs),
         "connection test establishes a STA connection");

  inputs.connectionTestDemand = false;
  inputs.policy = WifiPolicy::Auto;
  inputs.autoTaskDemand = true;
  expect(wifiStaConnectionNeededFor(inputs),
         "AUTO task establishes a STA connection");
}

void testModeTransitions() {
  WifiTransitionPlan plan =
      wifiTransitionPlanFor(WifiTargetMode::Off, WifiTargetMode::Ap);
  expect(plan.setMode && plan.startAp && !plan.stopAp && !plan.stopSta,
         "OFF to AP sets mode and starts only AP");

  plan = wifiTransitionPlanFor(WifiTargetMode::Ap, WifiTargetMode::ApSta);
  expect(plan.setMode && !plan.startAp && !plan.stopAp,
         "AP to AP_STA preserves AP without deciding whether to connect STA");

  plan = wifiTransitionPlanFor(WifiTargetMode::ApSta, WifiTargetMode::Ap);
  expect(plan.setMode && plan.stopSta && !plan.stopAp && !plan.startAp,
         "AP_STA to AP releases STA without restarting AP");

  plan = wifiTransitionPlanFor(WifiTargetMode::Sta, WifiTargetMode::Off);
  expect(plan.setMode && plan.stopSta && !plan.stopAp && !plan.startAp,
         "STA to OFF releases STA and disables the mode");

  plan = wifiTransitionPlanFor(WifiTargetMode::ApSta, WifiTargetMode::Off);
  expect(plan.setMode && plan.stopSta && plan.stopAp,
         "AP_STA to OFF releases both interfaces");

  plan = wifiTransitionPlanFor(WifiTargetMode::Ap, WifiTargetMode::Ap);
  expect(!plan.setMode && !plan.stopSta && !plan.stopAp && !plan.startAp,
         "unchanged target produces an empty transition plan");
}

void testScanNormalization() {
  WifiScanCandidate candidates[5] = {};
  strcpy(candidates[0].ssid, "weak");
  candidates[0].rssi = -80;
  candidates[0].secure = true;
  strcpy(candidates[1].ssid, "strong");
  candidates[1].rssi = -40;
  strcpy(candidates[2].ssid, "weak");
  candidates[2].rssi = -30;
  strcpy(candidates[3].ssid, "other");
  candidates[3].rssi = -60;
  candidates[4].rssi = -10;
  WifiScanResult results[3] = {};
  const size_t count = wifiNormalizeScanResults(candidates, 5, results, 3);
  expect(count == 3, "scan results are capped after filtering");
  expect(strcmp(results[0].ssid, "weak") == 0 && results[0].rssi == -30,
         "duplicate SSID keeps strongest RSSI");
  expect(strcmp(results[1].ssid, "strong") == 0,
         "results are sorted by descending RSSI");
  expect(strcmp(results[2].ssid, "other") == 0,
         "unique visible networks are retained");
  expect(!results[0].secure, "duplicate security follows strongest result");

  WifiScanCandidate cappedCandidates[4] = {};
  strcpy(cappedCandidates[0].ssid, "first");
  cappedCandidates[0].rssi = -80;
  strcpy(cappedCandidates[1].ssid, "second");
  cappedCandidates[1].rssi = -70;
  strcpy(cappedCandidates[2].ssid, "late-strong");
  cappedCandidates[2].rssi = -20;
  strcpy(cappedCandidates[3].ssid, "late-weak");
  cappedCandidates[3].rssi = -90;
  WifiScanResult cappedResults[2] = {};
  const size_t cappedCount = wifiNormalizeScanResults(
      cappedCandidates, 4, cappedResults, 2);
  expect(cappedCount == 2, "full scan result set keeps its capacity");
  expect(strcmp(cappedResults[0].ssid, "late-strong") == 0 &&
             strcmp(cappedResults[1].ssid, "second") == 0,
         "strong late network replaces the weakest retained result");
}

void testConnectionTestState() {
  expect(wifiTestStateAdvance(WifiTestState::Connecting, false, false, false) ==
             WifiTestState::Connecting,
         "connection test remains connecting while pending");
  expect(wifiTestStateAdvance(WifiTestState::Connecting, true, false, false) ==
             WifiTestState::Succeeded,
         "connected test succeeds");
  expect(wifiTestStateAdvance(WifiTestState::Connecting, false, true, false) ==
             WifiTestState::Failed,
         "terminal WiFi failure ends test");
  expect(wifiTestStateAdvance(WifiTestState::Connecting, false, false, true) ==
             WifiTestState::TimedOut,
         "deadline ends test as timed out");
}

void testCredentialBoundaries() {
  const char ssid32[] = "12345678901234567890123456789012";
  const char ssid33[] = "123456789012345678901234567890123";
  const char utf8Ssid[] = "Focus-\xE6\x97\xB6\xE9\x92\x9F";
  const char embeddedNull[] = {'a', '\0', 'b'};
  const char password8[] = "12345678";
  const char password7[] = "1234567";
  const char password63[] =
      "123456789012345678901234567890123456789012345678901234567890123";
  const char hexPassword64[] =
      "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF";
  const char nonHexPassword64[] =
      "g123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF";
  const char nonPrintablePassword[] = {'1', '2', '3', '4', '5', '6', '7', '\n'};

  expect(wifiSsidIsValid(nullptr, 0), "empty SSID is valid");
  expect(wifiSsidIsValid(ssid32, 32), "32-byte SSID is valid");
  expect(!wifiSsidIsValid(ssid33, 33), "33-byte SSID is invalid");
  expect(wifiSsidIsValid(utf8Ssid, strlen(utf8Ssid)),
         "UTF-8 SSID is measured in bytes");
  expect(!wifiSsidIsValid(embeddedNull, sizeof(embeddedNull)),
         "SSID cannot contain an embedded null");

  expect(wifiPasswordIsValid(nullptr, 0), "empty password is valid");
  expect(!wifiPasswordIsValid(password7, 7), "7-byte password is invalid");
  expect(wifiPasswordIsValid(password8, 8), "8-byte password is valid");
  expect(wifiPasswordIsValid(password63, 63), "63-byte password is valid");
  expect(wifiPasswordIsValid(hexPassword64, 64),
         "64-byte hexadecimal PSK is valid");
  expect(!wifiPasswordIsValid(nonHexPassword64, 64),
         "64-byte non-hexadecimal PSK is invalid");
  expect(!wifiPasswordIsValid(nonPrintablePassword,
                              sizeof(nonPrintablePassword)),
         "passphrase must use printable ASCII");
}

void testCandidateUpdates() {
  NetworkConfig current;
  current.policy = WifiPolicy::Off;
  strcpy(current.staSsid, "old-network");
  strcpy(current.staPassword, "old-password");

  NetworkConfig candidate;
  strcpy(candidate.staSsid, "unchanged-on-error");
  const char newSsid[] = "new-network";

  expect(makeNetworkConfigCandidate(
             current, WifiPolicy::Auto, newSsid, strlen(newSsid), nullptr, 0,
             candidate) == NetworkConfigValidationError::None,
         "candidate accepts a non-empty SSID with blank password submission");
  expect(candidate.policy == WifiPolicy::Auto,
         "candidate applies submitted policy");
  expect(strcmp(candidate.staSsid, newSsid) == 0,
         "candidate applies submitted SSID");
  expect(strcmp(candidate.staPassword, "old-password") == 0,
         "blank submitted password preserves saved password");

  const char newPassword[] = "new-password";
  expect(makeNetworkConfigCandidate(
             current, WifiPolicy::Auto, newSsid, strlen(newSsid), newPassword,
             strlen(newPassword), candidate) ==
             NetworkConfigValidationError::None,
         "candidate accepts a replacement password");
  expect(strcmp(candidate.staPassword, newPassword) == 0,
         "non-empty submitted password replaces saved password");

  const char ignoredInvalidPassword[] = "short";
  expect(makeNetworkConfigCandidate(
             current, WifiPolicy::Auto, nullptr, 0, ignoredInvalidPassword,
             strlen(ignoredInvalidPassword), candidate) ==
             NetworkConfigValidationError::None,
         "empty SSID ignores submitted password");
  expect(candidate.staSsid[0] == '\0' && candidate.staPassword[0] == '\0',
         "empty SSID clears both saved credential fields");

  strcpy(candidate.staSsid, "unchanged-on-error");
  expect(makeNetworkConfigCandidate(
             current, WifiPolicy::Auto, newSsid, strlen(newSsid),
             ignoredInvalidPassword, strlen(ignoredInvalidPassword),
             candidate) == NetworkConfigValidationError::InvalidPassword,
         "invalid replacement password is rejected");
  expect(strcmp(candidate.staSsid, "unchanged-on-error") == 0,
         "candidate output is not modified on validation failure");

  expect(makeNetworkConfigCandidate(
             current, static_cast<WifiPolicy>(5), newSsid, strlen(newSsid),
             nullptr, 0, candidate) ==
             NetworkConfigValidationError::InvalidPolicy,
         "candidate rejects an invalid policy enum value");
}

void testDeadlineWraparound() {
  expect(!wifiDeadlineReached(999, 1000), "deadline is pending before target");
  expect(wifiDeadlineReached(1000, 1000), "deadline is reached at target");
  expect(wifiDeadlineReached(1001, 1000), "deadline is reached after target");

  const uint32_t deadline = 5;
  expect(!wifiDeadlineReached(UINT32_MAX - 2, deadline),
         "wrapped deadline is pending before counter rollover");
  expect(wifiDeadlineReached(5, deadline),
         "wrapped deadline is reached after counter rollover");
}

void testNetworkConfigBlob() {
  NetworkConfig original;
  original.policy = WifiPolicy::Auto;
  strcpy(original.staSsid, "saved-network");
  strcpy(original.staPassword, "saved-password");

  PersistedNetworkConfigV1 blob;
  expect(persistenceEncodeNetworkConfig(original, blob),
         "valid network config encodes to V1 blob");
  expect(blob.version == NETWORK_CONFIG_BLOB_VERSION,
         "encoded blob contains current version");

  NetworkConfig decoded;
  expect(persistenceDecodeNetworkConfig(&blob, sizeof(blob), decoded) ==
             NetworkConfigBlobError::None,
         "valid V1 blob decodes");
  expect(decoded.policy == original.policy &&
             strcmp(decoded.staSsid, original.staSsid) == 0 &&
             strcmp(decoded.staPassword, original.staPassword) == 0,
         "decoded config matches encoded values");

  expect(persistenceDecodeNetworkConfig(&blob, sizeof(blob) - 1, decoded) ==
             NetworkConfigBlobError::InvalidSize,
         "truncated blob is rejected");

  PersistedNetworkConfigV1 invalid = blob;
  invalid.version = NETWORK_CONFIG_BLOB_VERSION + 1;
  expect(persistenceDecodeNetworkConfig(&invalid, sizeof(invalid), decoded) ==
             NetworkConfigBlobError::InvalidVersion,
         "unknown blob version is rejected");

  invalid = blob;
  invalid.policy = 9;
  expect(persistenceDecodeNetworkConfig(&invalid, sizeof(invalid), decoded) ==
             NetworkConfigBlobError::InvalidPolicy,
         "invalid stored policy is rejected");

  invalid = blob;
  memset(invalid.ssid, 's', sizeof(invalid.ssid));
  expect(persistenceDecodeNetworkConfig(&invalid, sizeof(invalid), decoded) ==
             NetworkConfigBlobError::InvalidSsid,
         "SSID without terminator is rejected");

  invalid = blob;
  memset(invalid.password, 'a', sizeof(invalid.password));
  expect(persistenceDecodeNetworkConfig(&invalid, sizeof(invalid), decoded) ==
             NetworkConfigBlobError::InvalidPassword,
         "password without terminator is rejected");

  invalid = {};
  invalid.version = NETWORK_CONFIG_BLOB_VERSION;
  invalid.policy = static_cast<uint8_t>(WifiPolicy::Auto);
  strcpy(invalid.password, "orphan-password");
  expect(persistenceDecodeNetworkConfig(&invalid, sizeof(invalid), decoded) ==
             NetworkConfigBlobError::InvalidPassword,
         "password without SSID is rejected");

  NetworkConfig invalidConfig = original;
  memset(invalidConfig.staSsid, 'x', sizeof(invalidConfig.staSsid));
  expect(!persistenceEncodeNetworkConfig(invalidConfig, blob),
         "invalid runtime config cannot be persisted");
}

}  // namespace

int main() {
  testPolicyValues();
  testTargetModes();
  testModeTransitions();
  testScanNormalization();
  testConnectionTestState();
  testCredentialBoundaries();
  testCandidateUpdates();
  testDeadlineWraparound();
  testNetworkConfigBlob();

  if (failures != 0) {
    fprintf(stderr, "%d wifi logic test(s) failed\n", failures);
    return 1;
  }
  printf("wifi logic tests passed\n");
  return 0;
}
