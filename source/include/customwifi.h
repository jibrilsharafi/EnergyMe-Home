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
#define WIFI_TASK_STACK_SIZE 8192
#define WIFI_TASK_PRIORITY 5

#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_LOOP_INTERVAL (1 * 1000)
#define WIFI_CONNECT_TIMEOUT 60                   // In seconds
#define WIFI_PORTAL_TIMEOUT (3 * 60)              // In seconds
#define WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS 1     // One is enough since there is already quite a lot of retries in the lower layers
#define WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS 5 // Maximum WiFi reconnection attempts before restart
#define WIFI_RECONNECT_DELAY_BASE (5 * 1000)      // Base delay for exponential backoff
#define WIFI_STABLE_CONNECTION (5 * 60 * 1000)    // Duration of uninterrupted WiFi connection to reset the reconnection counter
#define MDNS_HOSTNAME "energyme"

#define OCTET_BUFFER_SIZE 16       // For IPv4-like strings (xxx.xxx.xxx.xxx + null terminator)
#define MAC_ADDRESS_BUFFER_SIZE 18 // For MAC addresses (xx:xx:xx:xx:xx:xx + null terminator)
#define WIFI_STATUS_BUFFER_SIZE 18 // For connection status messages (longest expected is "Connection Failed" + null terminator)
#define WIFI_SSID_BUFFER_SIZE 128  // For WiFi SSID

struct WifiInfo
{
    char macAddress[MAC_ADDRESS_BUFFER_SIZE];
    char localIp[OCTET_BUFFER_SIZE];
    char subnetMask[OCTET_BUFFER_SIZE];
    char gatewayIp[OCTET_BUFFER_SIZE];
    char dnsIp[OCTET_BUFFER_SIZE];
    char status[WIFI_STATUS_BUFFER_SIZE];
    char ssid[WIFI_SSID_BUFFER_SIZE];
    char bssid[MAC_ADDRESS_BUFFER_SIZE];
    int rssi;

    WifiInfo() : rssi(0)
    {
        snprintf(macAddress, sizeof(macAddress), "%s", "");
        snprintf(localIp, sizeof(localIp), "%s", "");
        snprintf(subnetMask, sizeof(subnetMask), "%s", "");
        snprintf(gatewayIp, sizeof(gatewayIp), "%s", "");
        snprintf(dnsIp, sizeof(dnsIp), "%s", "");
        snprintf(status, sizeof(status), "%s", "Unknown");
        snprintf(ssid, sizeof(ssid), "%s", "Unknown");
        snprintf(bssid, sizeof(bssid), "%s", "");
    }
};

namespace CustomWifi
{
    bool begin();
    void resetWifi();
    bool isFullyConnected();

    void getWifiInfoJson(JsonDocument &jsonDocument);
    void printWifiInfo();
}