#pragma once

#include <stddef.h>
#include <stdint.h>

#include "network_types.h"

static constexpr uint8_t NETWORK_CONFIG_BLOB_VERSION = 1;

struct PersistedNetworkConfigV1 {
  uint8_t version;
  uint8_t policy;
  char ssid[AppConfig::WIFI_SSID_MAX_BYTES + 1];
  char password[AppConfig::WIFI_PASSWORD_MAX_BYTES + 1];
};

static_assert(sizeof(PersistedNetworkConfigV1) == 100,
              "Network config V1 blob layout must remain stable");

enum class NetworkConfigBlobError : uint8_t {
  None,
  InvalidSize,
  InvalidVersion,
  InvalidPolicy,
  InvalidSsid,
  InvalidPassword,
};

bool persistenceEncodeNetworkConfig(const NetworkConfig &config,
                                    PersistedNetworkConfigV1 &blob);
NetworkConfigBlobError persistenceDecodeNetworkConfig(const void *data,
                                                       size_t size,
                                                       NetworkConfig &config);
