#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "constants.h"
#include "globals.h"
#include "led.h"

#define WIFI_TASK_NAME "wifi_task"
#define WIFI_TASK_STACK_SIZE (5 * 1024) // WiFiManager now allocated on heap, not stack
#define WIFI_TASK_PRIORITY 5

#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"

#define WIFI_LOOP_INTERVAL (1 * 1000)
#define WIFI_CONNECT_TIMEOUT_SECONDS 10
#define WIFI_PORTAL_TIMEOUT_SECONDS (5 * 60)
#define WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS 3       // How many times to try connecting (with timeout) before giving up
#define WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS 5   // Maximum WiFi reconnection attempts before restart
#define WIFI_DISCONNECT_DELAY (15 * 1000)           // Delay after WiFi disconnected to allow automatic reconnection
#define WIFI_RECONNECT_DELAY_BASE (5 * 1000)        // Base delay for exponential backoff
#define WIFI_STABLE_CONNECTION_DURATION (5 * 60 * 1000)    // Duration of uninterrupted WiFi connection to reset the reconnection counter
#define WIFI_PERIODIC_CHECK_INTERVAL (30 * 1000)    // Interval to check WiFi connection status (does not need to be too frequent since we have an event-based system)
#define WIFI_FORCE_RECONNECT_DELAY (2 * 1000)      // Delay after forcing reconnection
#define WIFI_LWIP_STABILIZATION_DELAY (1 * 1000)    // Delay after WiFi connection to allow lwIP network stack to stabilize (prevents DNS/UDP crashes)

// Connectivity test parameters
#define CONNECTIVITY_TEST_TIMEOUT_MS (3 * 1000)           // Timeout for connectivity tests
#define CONNECTIVITY_TEST_HOST "google.com"         // Host to test connectivity against
#define CONNECTIVITY_TEST_PORT 80                   // Port for connectivity test

#define MDNS_HOSTNAME "energyme"
#define MDNS_QUERY_TIMEOUT (5 * 1000)

#define OCTET_BUFFER_SIZE 16       // For IPv4-like strings (xxx.xxx.xxx.xxx + null terminator)
#define MAC_ADDRESS_BUFFER_SIZE 18 // For MAC addresses (xx:xx:xx:xx:xx:xx + null terminator)
#define WIFI_STATUS_BUFFER_SIZE 18 // For connection status messages (longest expected is "Connection Failed" + null terminator)
#define WIFI_SSID_BUFFER_SIZE 64  // For WiFi SSID

namespace CustomWifi
{
    bool begin();
    void stop();
    
    bool isFullyConnected();
    bool testConnectivity(); // Test actual network connectivity (check gateway and DNS)
    void forceReconnect();   // Force immediate WiFi reconnection

    void resetWifi();

    // Task information
    TaskInfo getTaskInfo();
}