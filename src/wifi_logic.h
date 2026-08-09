#pragma once

#include <stddef.h>
#include <stdint.h>

#include "network_types.h"

enum class NetworkConfigValidationError : uint8_t {
  None,
  InvalidPolicy,
  InvalidSsid,
  InvalidPassword,
};

bool wifiPolicyFromValue(uint8_t value, WifiPolicy &policy);
WifiTargetMode wifiTargetModeFor(const WifiModeInputs &inputs);
bool wifiStaConnectionNeededFor(const WifiModeInputs &inputs);
WifiTransitionPlan wifiTransitionPlanFor(WifiTargetMode current,
                                         WifiTargetMode target);
bool wifiSsidIsValid(const char *ssid, size_t length);
bool wifiPasswordIsValid(const char *password, size_t length);
size_t wifiNormalizeScanResults(const WifiScanCandidate *candidates,
                                size_t candidateCount,
                                WifiScanResult *results,
                                size_t resultCapacity);
size_t wifiMergeScanCandidate(const WifiScanCandidate &candidate,
                              WifiScanResult *results,
                              size_t resultCount,
                              size_t resultCapacity);

NetworkConfigValidationError makeNetworkConfigCandidate(
    const NetworkConfig &current,
    WifiPolicy submittedPolicy,
    const char *submittedSsid,
    size_t submittedSsidLength,
    const char *submittedPassword,
    size_t submittedPasswordLength,
    NetworkConfig &candidate);

bool wifiDeadlineReached(uint32_t nowMs, uint32_t deadlineMs);
WifiTestState wifiTestStateAdvance(WifiTestState current,
                                   bool connected,
                                   bool terminalFailure,
                                   bool deadlineReached);
