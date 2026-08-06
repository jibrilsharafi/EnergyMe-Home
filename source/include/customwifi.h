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
#include <DNSServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "awsconfig.h"
#include "constants.h"
#include "factory_keys.h"
#include "globals.h"
#include "led.h"
#include "utils.h"
#include "wifi_provisioning.h" // Pure provisioning decision logic (Arduino-free, host-testable)

#define WIFI_TASK_NAME "wifi_task"
#define WIFI_TASK_STACK_SIZE (8 * 1024) // Week-long high-water mark hit 5580 bytes (only 564 bytes free at 6 KB) on the pre-rework task, so 8 KB for margin. Those were the WiFiManager portal paths and they are gone; the AP lifecycle, DNS and scan paths that replaced them have not been measured on hardware
#define WIFI_TASK_PRIORITY 5

#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_HOSTNAME_PREFIX "energyme-home"

#define WIFI_CONNECT_TIMEOUT_SECONDS 10
#define WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS (5 * 60)  // Extended timeout for the FIRST attempt after a power reset only (router likely rebooting)
#define WIFI_CREDENTIAL_WRITE_RETRY_DELAY_MS 250    // Settle time between a disconnect and retrying esp_wifi_set_config(), which is refused while the STA is connecting
#define WIFI_DISCONNECT_DELAY (15 * 1000)           // Delay after WiFi disconnected to allow automatic reconnection
#define WIFI_AP_LIFECYCLE_TICK_MS (10 * 1000)       // How often the AP lifetime/grace predicates are evaluated while the SoftAP is up
#define WIFI_SCAN_MS_PER_CHANNEL 120                // Per-channel dwell. The default (~300 ms) makes a full scan long enough that a phone on the SoftAP times out waiting
#define WIFI_SCAN_MAX_RESULTS 30                    // Cap the JSON response; a dense apartment block can see far more than a user will scroll
#define WIFI_SCAN_RESULTS_TTL_MS (2 * 60 * 1000)    // How long a completed result set is served before it is freed and re-scanned. The driver holds the full set in internal RAM, so it must not be kept for the rest of the uptime
#define WIFI_STABLE_CONNECTION_DURATION (5 * 60 * 1000)    // Duration of uninterrupted WiFi connection to reset the reconnection counter
#define WIFI_PERIODIC_CHECK_INTERVAL (30 * 1000)    // Interval to check WiFi connection status (does not need to be too frequent since we have an event-based system)
#define WIFI_FORCE_RECONNECT_DELAY (2 * 1000)      // Delay after forcing reconnection
#define WIFI_LWIP_STABILIZATION_DELAY (1 * 1000)    // Delay after WiFi connection to allow lwIP network stack to stabilize (prevents DNS/UDP crashes)
#define SETUP_NETWORK_WAIT_TIMEOUT_MS (60 * 1000)   // How long setup() waits for either interface before starting the rest of the system anyway. Bounds the case where no STA link is possible and no SoftAP subnet is available, which would otherwise block the web server from ever starting

// Connectivity test parameters - lightweight TCP connect to public DNS (no DNS lookup needed, rarely blocked)
#define CONNECTIVITY_TEST_TIMEOUT_MS (3 * 1000)           // Timeout for connectivity tests
#define CONNECTIVITY_TEST_IP "8.8.8.8"  // Google Public DNS - stable, reliable, no DNS lookup needed
#define CONNECTIVITY_TEST_PORT 53                   // DNS port - rarely blocked by firewalls

#define MDNS_HOSTNAME "energyme"

// Network configuration (static IP)
// =====================================================
// Defaults are chosen so a freshly provisioned device behaves exactly as before: DHCP.
#define WIFI_USE_STATIC_IP_DEFAULT false
#define WIFI_FALLBACK_TO_DHCP_DEFAULT true          // Auto-revert to DHCP if a configured static IP can't reach the internet

// Two independent safety nets protect against a bad static IP locking the device out. Both recover
// only by restarting onto DHCP - never by reconfiguring the live netif, which races lwIP (an in-flight
// UDP send asserts in ip4_route during the IP-less window):
//   1. Boot-fail backstop (always on): a persistent counter is incremented BEFORE the static IP is
//      applied on each boot and cleared once the device proves the static IP is not a boot-loop
//      offender (it stays connected past the early crash window). After this many consecutive failed
//      boots the static IP is ignored at startup and the device comes up on DHCP - so a static IP
//      that crashes or fails to associate can never brick it.
//   2. DHCP auto-recovery (only when fallbackToDhcp is set): if a static IP that DID come up has no
//      internet for several consecutive periodic checks, force the backstop and restart once; the
//      next boot comes up clean on DHCP. Disarms after the first successful check so a later internet
//      blip on a good static IP never triggers it. LAN-only users leave fallbackToDhcp off.
#define WIFI_STATIC_IP_MAX_BOOT_FAILS 3
#define WIFI_STATIC_IP_FAIL_STREAK 2               // Consecutive periodic internet failures before DHCP auto-recovery

// Preferences keys for persistent storage (wifi_ns). Max 15 chars each.
#define WIFI_CONFIG_USE_STATIC_KEY "useStatic"
#define WIFI_CONFIG_IP_KEY "ip"
#define WIFI_CONFIG_GATEWAY_KEY "gateway"
#define WIFI_CONFIG_SUBNET_KEY "subnet"
#define WIFI_CONFIG_DNS1_KEY "dns1"
#define WIFI_CONFIG_DNS2_KEY "dns2"
#define WIFI_CONFIG_FALLBACK_KEY "fallback"
#define WIFI_CONFIG_STATIC_FAILS_KEY "staticFails" // Consecutive failed static-IP boots (backstop)

// Network configuration: static IP. Stored in NVS (wifi_ns) and applied at WiFi init.
// All addresses are stored as strings for easy validation.
struct WifiConfiguration {
    bool useStaticIp;
    char ip[IP_ADDRESS_BUFFER_SIZE];
    char gateway[IP_ADDRESS_BUFFER_SIZE];
    char subnet[IP_ADDRESS_BUFFER_SIZE];
    char dns1[IP_ADDRESS_BUFFER_SIZE];
    char dns2[IP_ADDRESS_BUFFER_SIZE];
    bool fallbackToDhcp; // When static IP is enabled, auto-revert to DHCP if the gateway is unreachable

    WifiConfiguration()
        : useStaticIp(WIFI_USE_STATIC_IP_DEFAULT),
          fallbackToDhcp(WIFI_FALLBACK_TO_DHCP_DEFAULT) {
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

    // True when the SoftAP is up and has an address, so the web server is reachable
    // on it regardless of the STA link.
    bool isApServing();

    // Lock-free check against the SoftAP's own address, published by the WiFi task when the AP
    // is raised and cleared when it comes down. Safe from any task, including the AsyncTCP task
    // inside a request filter: a plain volatile load and a compare, with no esp_netif call,
    // unlike WiFi.softAPIP(). Always false while no AP is up.
    bool isApAddress(const IPAddress &address);

    // STA connected OR serving on the SoftAP. This, not isFullyConnected(), is what
    // callers should gate on when the question is "can anyone reach this device":
    // a device serving on the AP with no upstream network is working as intended,
    // and treating it as unhealthy restarts it every ~150 s.
    bool isNetworkServiceable();

    // Lock-free snapshot of the provisioning state. Safe to call from any task, including
    // the AsyncTCP task inside a request filter: it is a plain load of a uint8_t-backed
    // enum, no mutex and no NVS. The Phase 4 auth carve-out gates on this.
    WifiProvisioning::State getProvisioningState();

    // SSID the driver has stored, i.e. what WiFi.begin() would try. Empty when unprovisioned.
    void getStoredSsid(char *out, size_t outSize);

    // True when the last credential submission could not be written to the driver.
    // setCredentials() answers as soon as the request is queued, so the HTTP 200 is sent
    // before the write is attempted; without surfacing this, a rejected write looks like
    // success to the user and the password is silently discarded.
    bool lastCredentialWriteFailed();

    // Async network scan for the provisioning UI. startScan() returns false when a scan
    // cannot be started right now (an association attempt is in flight); the results call
    // reports status "running" / "complete" / "unavailable" so the client can poll.
    // `forceRescan` is the explicit "Scan again" press: it retires a cached set that would
    // otherwise be re-served unchanged. Polling must leave it false, or a client would keep
    // restarting the scan it is waiting on.
    bool startScan();
    void getScanResultsAsJson(JsonDocument &jsonDocument, bool forceRescan = false);

    // Last association failure: reason code and string, SSID, BSSID, RSSI. These were only
    // ever visible through the removed WiFiManager /diagnostic page (D11).
    void getDisconnectDiagnosticsAsJson(JsonDocument &jsonDocument);
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