#include "wifi_logic.h"

#include <string.h>

namespace {

bool containsNullByte(const char *value, size_t length) {
  return value != nullptr && memchr(value, '\0', length) != nullptr;
}

bool isHexDigit(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

bool isPrintableAscii(char value) {
  const uint8_t byte = static_cast<uint8_t>(value);
  return byte >= 0x20 && byte <= 0x7E;
}

}  // namespace

bool wifiPolicyFromValue(uint8_t value, WifiPolicy &policy) {
  switch (value) {
    case static_cast<uint8_t>(WifiPolicy::Off):
      policy = WifiPolicy::Off;
      return true;
    case static_cast<uint8_t>(WifiPolicy::Auto):
      policy = WifiPolicy::Auto;
      return true;
    default:
      policy = WifiPolicy::Off;
      return false;
  }
}

WifiTargetMode wifiTargetModeFor(const WifiModeInputs &inputs) {
  const bool apNeeded = inputs.configModeRequested;
  const bool staNeeded = inputs.portalScanDemand ||
                         wifiStaConnectionNeededFor(inputs);

  if (apNeeded && staNeeded) {
    return WifiTargetMode::ApSta;
  }
  if (apNeeded) {
    return WifiTargetMode::Ap;
  }
  if (staNeeded) {
    return WifiTargetMode::Sta;
  }
  return WifiTargetMode::Off;
}

bool wifiStaConnectionNeededFor(const WifiModeInputs &inputs) {
  // Scanning needs the STA interface, but must not connect saved credentials.
  return inputs.connectionTestDemand ||
         (inputs.policy == WifiPolicy::Auto && inputs.autoTaskDemand);
}

size_t wifiMergeScanCandidate(const WifiScanCandidate &candidate,
                              WifiScanResult *results,
                              size_t resultCount,
                              size_t resultCapacity) {
  if (results == nullptr || resultCapacity == 0 ||
      resultCount > resultCapacity) {
    return resultCount;
  }
  const size_t length =
      strnlen(candidate.ssid, AppConfig::WIFI_SSID_MAX_BYTES + 1);
  // Empty SSIDs represent hidden networks and are intentionally omitted.
  if (length == 0 || length > AppConfig::WIFI_SSID_MAX_BYTES) {
    return resultCount;
  }

  size_t destination = resultCount;
  for (size_t index = 0; index < resultCount; ++index) {
    if (strcmp(results[index].ssid, candidate.ssid) == 0) {
      if (candidate.rssi <= results[index].rssi) {
        return resultCount;
      }
      destination = index;
      break;
    }
  }
  if (destination == resultCount) {
    if (resultCount < resultCapacity) {
      ++resultCount;
    } else {
      destination = resultCount - 1;
      if (candidate.rssi <= results[destination].rssi) {
        return resultCount;
      }
    }
  }
  strcpy(results[destination].ssid, candidate.ssid);
  results[destination].rssi = candidate.rssi;
  results[destination].secure = candidate.secure;

  // Keep the bounded set sorted so its last slot is always the weakest.
  for (size_t i = 1; i < resultCount; ++i) {
    WifiScanResult item = results[i];
    size_t position = i;
    while (position > 0 && results[position - 1].rssi < item.rssi) {
      results[position] = results[position - 1];
      --position;
    }
    results[position] = item;
  }
  return resultCount;
}

size_t wifiNormalizeScanResults(const WifiScanCandidate *candidates,
                                size_t candidateCount,
                                WifiScanResult *results,
                                size_t resultCapacity) {
  if (candidates == nullptr || results == nullptr || resultCapacity == 0) {
    return 0;
  }
  size_t resultCount = 0;
  for (size_t index = 0; index < candidateCount; ++index) {
    resultCount = wifiMergeScanCandidate(
        candidates[index], results, resultCount, resultCapacity);
  }
  return resultCount;
}

WifiTransitionPlan wifiTransitionPlanFor(WifiTargetMode current,
                                         WifiTargetMode target) {
  const bool currentHasAp = current == WifiTargetMode::Ap ||
                            current == WifiTargetMode::ApSta;
  const bool currentHasSta = current == WifiTargetMode::Sta ||
                             current == WifiTargetMode::ApSta;
  const bool targetHasAp = target == WifiTargetMode::Ap ||
                           target == WifiTargetMode::ApSta;
  const bool targetHasSta = target == WifiTargetMode::Sta ||
                            target == WifiTargetMode::ApSta;

  WifiTransitionPlan plan;
  plan.stopSta = currentHasSta && !targetHasSta;
  plan.stopAp = currentHasAp && !targetHasAp;
  plan.setMode = current != target;
  plan.startAp = !currentHasAp && targetHasAp;
  return plan;
}

bool wifiSsidIsValid(const char *ssid, size_t length) {
  if (length > AppConfig::WIFI_SSID_MAX_BYTES) {
    return false;
  }
  return length == 0 || (ssid != nullptr && !containsNullByte(ssid, length));
}

bool wifiPasswordIsValid(const char *password, size_t length) {
  if (length == 0) {
    return true;
  }
  if (password == nullptr || containsNullByte(password, length)) {
    return false;
  }

  if (length == AppConfig::WIFI_PASSWORD_MAX_BYTES) {
    for (size_t index = 0; index < length; ++index) {
      if (!isHexDigit(password[index])) {
        return false;
      }
    }
    return true;
  }

  if (length < 8 || length > 63) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    if (!isPrintableAscii(password[index])) {
      return false;
    }
  }
  return true;
}

NetworkConfigValidationError makeNetworkConfigCandidate(
    const NetworkConfig &current,
    WifiPolicy submittedPolicy,
    const char *submittedSsid,
    size_t submittedSsidLength,
    const char *submittedPassword,
    size_t submittedPasswordLength,
    NetworkConfig &candidate) {
  WifiPolicy validatedPolicy;
  if (!wifiPolicyFromValue(static_cast<uint8_t>(submittedPolicy),
                           validatedPolicy)) {
    return NetworkConfigValidationError::InvalidPolicy;
  }
  if (!wifiSsidIsValid(submittedSsid, submittedSsidLength)) {
    return NetworkConfigValidationError::InvalidSsid;
  }

  NetworkConfig next = current;
  next.policy = validatedPolicy;

  if (submittedSsidLength == 0) {
    // An empty SSID intentionally ignores the submitted password and clears both.
    next.staSsid[0] = '\0';
    next.staPassword[0] = '\0';
  } else {
    if (submittedPasswordLength > 0 &&
        !wifiPasswordIsValid(submittedPassword, submittedPasswordLength)) {
      return NetworkConfigValidationError::InvalidPassword;
    }

    memcpy(next.staSsid, submittedSsid, submittedSsidLength);
    next.staSsid[submittedSsidLength] = '\0';

    // A blank password means "keep the saved password", not "make it open".
    if (submittedPasswordLength > 0) {
      memcpy(next.staPassword, submittedPassword, submittedPasswordLength);
      next.staPassword[submittedPasswordLength] = '\0';
    }
  }

  candidate = next;
  return NetworkConfigValidationError::None;
}

bool wifiDeadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  // Deadlines are valid within half the uint32_t range, including millis() wrap.
  return static_cast<uint32_t>(nowMs - deadlineMs) < 0x80000000UL;
}

WifiTestState wifiTestStateAdvance(WifiTestState current,
                                   bool connected,
                                   bool terminalFailure,
                                   bool deadlineReached) {
  if (current != WifiTestState::Connecting) {
    return current;
  }
  if (connected) {
    return WifiTestState::Succeeded;
  }
  if (terminalFailure) {
    return WifiTestState::Failed;
  }
  if (deadlineReached) {
    return WifiTestState::TimedOut;
  }
  return WifiTestState::Connecting;
}
