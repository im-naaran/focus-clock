#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AppConfig {

static constexpr const char *WIFI_CONFIG_AP_SSID_PREFIX = "FocusClock-";
static constexpr uint8_t WIFI_CONFIG_AP_CHANNEL = 6;

static constexpr uint8_t WIFI_CONFIG_AP_IP[] = {192, 168, 4, 1};
static constexpr uint8_t WIFI_CONFIG_AP_GATEWAY[] = {192, 168, 4, 1};
static constexpr uint8_t WIFI_CONFIG_AP_SUBNET[] = {255, 255, 255, 0};

static constexpr uint32_t WIFI_AP_CLIENT_POLL_MS = 250;
static constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_STA_RECONNECT_MS = 30000;
static constexpr uint32_t WIFI_MODE_RETRY_MS = 1000;
static constexpr uint8_t WIFI_SCAN_MAX_RESULTS = 20;

static constexpr size_t WIFI_SSID_MAX_BYTES = 32;
static constexpr size_t WIFI_PASSWORD_MAX_BYTES = 64;
static constexpr size_t HTTP_MAX_BODY_BYTES = 1024;

}  // namespace AppConfig
