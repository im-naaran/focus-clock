#include "persistence_codec.h"

#include <string.h>

#include "wifi_logic.h"

namespace {

size_t terminatedLength(const char *value, size_t capacity) {
  const void *terminator = memchr(value, '\0', capacity);
  if (terminator == nullptr) {
    return capacity;
  }
  return static_cast<const char *>(terminator) - value;
}

}  // namespace

bool persistenceEncodeNetworkConfig(const NetworkConfig &config,
                                    PersistedNetworkConfigV1 &blob) {
  WifiPolicy policy;
  if (!wifiPolicyFromValue(static_cast<uint8_t>(config.policy), policy)) {
    return false;
  }

  const size_t ssidLength = terminatedLength(config.staSsid,
                                              sizeof(config.staSsid));
  const size_t passwordLength = terminatedLength(config.staPassword,
                                                  sizeof(config.staPassword));
  if (!wifiSsidIsValid(config.staSsid, ssidLength) ||
      !wifiPasswordIsValid(config.staPassword, passwordLength) ||
      (ssidLength == 0 && passwordLength != 0)) {
    return false;
  }

  PersistedNetworkConfigV1 next = {};
  next.version = NETWORK_CONFIG_BLOB_VERSION;
  next.policy = static_cast<uint8_t>(policy);
  memcpy(next.ssid, config.staSsid, ssidLength);
  memcpy(next.password, config.staPassword, passwordLength);
  blob = next;
  return true;
}

NetworkConfigBlobError persistenceDecodeNetworkConfig(const void *data,
                                                       size_t size,
                                                       NetworkConfig &config) {
  if (data == nullptr || size != sizeof(PersistedNetworkConfigV1)) {
    return NetworkConfigBlobError::InvalidSize;
  }

  PersistedNetworkConfigV1 blob;
  memcpy(&blob, data, sizeof(blob));
  if (blob.version != NETWORK_CONFIG_BLOB_VERSION) {
    return NetworkConfigBlobError::InvalidVersion;
  }

  WifiPolicy policy;
  if (!wifiPolicyFromValue(blob.policy, policy)) {
    return NetworkConfigBlobError::InvalidPolicy;
  }

  const size_t ssidLength = terminatedLength(blob.ssid, sizeof(blob.ssid));
  if (!wifiSsidIsValid(blob.ssid, ssidLength)) {
    return NetworkConfigBlobError::InvalidSsid;
  }

  const size_t passwordLength = terminatedLength(blob.password,
                                                  sizeof(blob.password));
  if (!wifiPasswordIsValid(blob.password, passwordLength) ||
      (ssidLength == 0 && passwordLength != 0)) {
    return NetworkConfigBlobError::InvalidPassword;
  }

  NetworkConfig decoded;
  decoded.policy = policy;
  memcpy(decoded.staSsid, blob.ssid, ssidLength);
  decoded.staSsid[ssidLength] = '\0';
  memcpy(decoded.staPassword, blob.password, passwordLength);
  decoded.staPassword[passwordLength] = '\0';
  config = decoded;
  return NetworkConfigBlobError::None;
}

