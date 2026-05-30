// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <mbedtls/sha256.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>

#include "awsconfig.h"
#include "constants.h"
#include "factory_keys.h"
#include "globals.h"
#include "led.h"
#include "utils.h"

#define WIFI_TASK_NAME "wifi_task"
#define WIFI_TASK_STACK_SIZE (8 * 1024) // Week-long high-water mark hit 5580 bytes (only 564 bytes free at 6 KB) - bumped to 8 KB for ~30% safety margin on portal/credential paths
#define WIFI_TASK_PRIORITY 5

#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_HOSTNAME_PREFIX "energyme-home"

#define WIFI_CONNECT_TIMEOUT_SECONDS 10
#define WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS (5 * 60)  // Extended timeout after power reset (router likely rebooting)
#define WIFI_PORTAL_TIMEOUT_SECONDS (5 * 60)        // Leave enough time to avoid the user being locked out while providing the credentials, but not too high to ensure we retry the connection to the saved WiFi
#define WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS 3       // How many times to try connecting (with timeout) before giving up
#define WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS 5   // Maximum WiFi reconnection attempts before restart
#define WIFI_DISCONNECT_DELAY (15 * 1000)           // Delay after WiFi disconnected to allow automatic reconnection
#define WIFI_STABLE_CONNECTION_DURATION (5 * 60 * 1000)    // Duration of uninterrupted WiFi connection to reset the reconnection counter
#define WIFI_PERIODIC_CHECK_INTERVAL (30 * 1000)    // Interval to check WiFi connection status (does not need to be too frequent since we have an event-based system)
#define WIFI_FORCE_RECONNECT_DELAY (2 * 1000)      // Delay after forcing reconnection
#define WIFI_LWIP_STABILIZATION_DELAY (1 * 1000)    // Delay after WiFi connection to allow lwIP network stack to stabilize (prevents DNS/UDP crashes)
#define MAX_LOG_SIZE_DIAGNOSTIC_FALLBACK_PAGE (8 * 1024) // Maximum log size to include in diagnostic fallback page

// Connectivity test parameters - lightweight TCP connect to public DNS (no DNS lookup needed, rarely blocked)
#define CONNECTIVITY_TEST_TIMEOUT_MS (3 * 1000)           // Timeout for connectivity tests
#define CONNECTIVITY_TEST_IP "8.8.8.8"  // Google Public DNS - stable, reliable, no DNS lookup needed
#define CONNECTIVITY_TEST_PORT 53                   // DNS port - rarely blocked by firewalls

#define MDNS_HOSTNAME "energyme"

// Network configuration (static IP / channel / TX power)
// =====================================================
// Defaults are chosen so a freshly provisioned device behaves exactly as before:
// DHCP, automatic channel, maximum TX power.
#define WIFI_USE_STATIC_IP_DEFAULT false
#define WIFI_CHANNEL_DEFAULT 0                      // 0 = automatic (station follows the AP)
#define WIFI_CHANNEL_MAX 13                         // 2.4 GHz channels 1-13 (14 is region-restricted)
#define WIFI_TX_POWER_DEFAULT WIFI_POWER_19_5dBm    // Maximum TX power (raw wifi_power_t, quarter-dBm units)

// Preferences keys for persistent storage (wifi_ns). Max 15 chars each.
#define WIFI_CONFIG_USE_STATIC_KEY "useStatic"
#define WIFI_CONFIG_IP_KEY "ip"
#define WIFI_CONFIG_GATEWAY_KEY "gateway"
#define WIFI_CONFIG_SUBNET_KEY "subnet"
#define WIFI_CONFIG_DNS1_KEY "dns1"
#define WIFI_CONFIG_DNS2_KEY "dns2"
#define WIFI_CONFIG_CHANNEL_KEY "channel"
#define WIFI_CONFIG_TXPOWER_KEY "txPower"

// Network configuration: static IP, fixed channel and TX power. Stored in NVS (wifi_ns)
// and applied at WiFi init. txPower holds the raw wifi_power_t value (quarter-dBm); the
// JSON API exposes it as dBm. All addresses are stored as strings for easy validation.
struct WifiConfiguration {
    bool useStaticIp;
    char ip[IP_ADDRESS_BUFFER_SIZE];
    char gateway[IP_ADDRESS_BUFFER_SIZE];
    char subnet[IP_ADDRESS_BUFFER_SIZE];
    char dns1[IP_ADDRESS_BUFFER_SIZE];
    char dns2[IP_ADDRESS_BUFFER_SIZE];
    uint8_t channel;    // 0 = automatic, 1-13 for a fixed channel
    int8_t txPower;     // Raw wifi_power_t value (e.g. 78 = 19.5 dBm)

    WifiConfiguration()
        : useStaticIp(WIFI_USE_STATIC_IP_DEFAULT),
          channel(WIFI_CHANNEL_DEFAULT),
          txPower(WIFI_TX_POWER_DEFAULT) {
      ip[0] = '\0';
      gateway[0] = '\0';
      subnet[0] = '\0';
      dns1[0] = '\0';
      dns2[0] = '\0';
    }
};

// Open Source Telemetry
// =====================
// NOTE: Build-time flag ENABLE_OPEN_SOURCE_TELEMETRY controls whether telemetry is sent.
//       Set -DENABLE_OPEN_SOURCE_TELEMETRY=0 or remove the define to disable.
#ifdef ENV_DEV
#define TELEMETRY_URL "5jyfvyfmubfr6rw7tx7ozb4foq0hstkk.lambda-url.eu-west-1.on.aws"
#else
#define TELEMETRY_URL "vd2obqbugurdyhbf4iaxrzmk4i0njltb.lambda-url.eu-west-1.on.aws"
#endif
#define TELEMETRY_PORT 443
#define TELEMETRY_PATH "/"
#define TELEMETRY_TIMEOUT_MS (1 * 1000) // Very short timeout since we don't really care about the response
#define TELEMETRY_JSON_BUFFER_SIZE 512 // Sufficient for {hashed_device_id, firmware_version, sketch_md5}

namespace CustomWifi
{
    bool begin();
    void stop();
    
    bool isFullyConnected(bool requireInternet = false);
    bool testConnectivity(); // Test actual network connectivity (check gateway and DNS)
    void forceReconnect();   // Force immediate WiFi reconnection

    void resetWifi();
    bool setCredentials(const char* ssid, const char* password); // Set new WiFi credentials and trigger reconnection

    // Network configuration management - direct struct operations
    bool getConfiguration(WifiConfiguration &config);
    bool setConfiguration(const WifiConfiguration &config); // Persists to NVS; caller restarts to apply
    bool resetConfiguration();

    // Network configuration management - JSON operations
    bool getConfigurationAsJson(JsonDocument &jsonDocument);
    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial = false);
    void configurationToJson(const WifiConfiguration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, WifiConfiguration &config, bool partial = false);

    // Task information
    TaskInfo getTaskInfo();
}