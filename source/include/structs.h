#pragma once

#include <Arduino.h>

#include "constants.h"

struct Statistics {
  uint32_t ade7953TotalInterrupts;
  uint32_t ade7953TotalHandledInterrupts;
  uint32_t ade7953ReadingCount;
  uint32_t ade7953ReadingCountFailure;

  uint32_t mqttMessagesPublished;
  uint32_t mqttMessagesPublishedError;
  
  uint32_t customMqttMessagesPublished; // TODO: add also connection error and not
  uint32_t customMqttMessagesPublishedError;
  
  uint32_t modbusRequests;
  uint32_t modbusRequestsError;
  
  uint32_t influxdbUploadCount;
  uint32_t influxdbUploadCountError;

  uint32_t wifiConnection;
  uint32_t wifiConnectionError;

  uint32_t webServerRequests;
  uint32_t webServerRequestsError;

  uint32_t logVerbose;
  uint32_t logDebug;
  uint32_t logInfo;
  uint32_t logWarning;
  uint32_t logError;
  uint32_t logFatal;

  Statistics() 
    : ade7953TotalInterrupts(0), ade7953TotalHandledInterrupts(0), ade7953ReadingCount(0), ade7953ReadingCountFailure(0), 
    mqttMessagesPublished(0), mqttMessagesPublishedError(0), customMqttMessagesPublished(0), customMqttMessagesPublishedError(0), modbusRequests(0), modbusRequestsError(0), 
    influxdbUploadCount(0), influxdbUploadCountError(0), wifiConnection(0), wifiConnectionError(0),
    logVerbose(0), logDebug(0), logInfo(0), logWarning(0), logError(0), logFatal(0) {}
};

// Static system information (rarely changes, only with firmware updates)
struct SystemStaticInfo {
    // Product & Company
    char companyName[NAME_BUFFER_SIZE];
    char productName[NAME_BUFFER_SIZE];
    char fullProductName[NAME_BUFFER_SIZE];
    char productDescription[STATUS_BUFFER_SIZE];
    char githubUrl[URL_BUFFER_SIZE];
    char author[NAME_BUFFER_SIZE];
    char authorEmail[NAME_BUFFER_SIZE];
    
    // Firmware & Build
    char buildVersion[VERSION_BUFFER_SIZE];
    char buildDate[TIMESTAMP_BUFFER_SIZE];
    char buildTime[TIMESTAMP_BUFFER_SIZE];
    char sketchMD5[MD5_BUFFER_SIZE];  // MD5 hash (32 chars + null terminator)
    char partitionAppName[NAME_BUFFER_SIZE]; // Name of the partition for the app (e.g., "app0", "app1")
    
    // Hardware & Chip (mostly static)
    char chipModel[NAME_BUFFER_SIZE];        // ESP32, ESP32-S3, etc.
    uint16_t chipRevision;      // Hardware revision
    uint8_t chipCores;         // Number of CPU cores
    uint64_t chipId;           // Unique chip ID
    uint32_t flashChipSizeBytes;
    uint32_t flashChipSpeedHz;
    uint32_t psramSizeBytes;   // Total PSRAM (if available)
    uint32_t cpuFrequencyMHz;  // CPU frequency
    
    // SDK versions
    char sdkVersion[NAME_BUFFER_SIZE];
    char coreVersion[NAME_BUFFER_SIZE];
    
    // Crash and reset monitoring
    uint32_t crashCount;                    // Total crashes since last manual reset
    uint32_t consecutiveCrashCount;         // Consecutive crashes since last reset
    uint32_t resetCount;                    // Total resets since first boot
    uint32_t consecutiveResetCount;         // Consecutive resets since last manual reset
    uint32_t lastResetReason;               // ESP reset reason code
    char lastResetReasonString[STATUS_BUFFER_SIZE];         // Human readable reset reason
    bool lastResetWasCrash;                 // True if last reset was due to crash
    
    // Device configuration
    char deviceId[DEVICE_ID_BUFFER_SIZE];
    
    SystemStaticInfo() {
        // Initialize with safe defaults
        memset(this, 0, sizeof(*this));
        snprintf(companyName, sizeof(companyName), "Unknown");
        snprintf(productName, sizeof(productName), "Unknown");
        snprintf(fullProductName, sizeof(fullProductName), "Unknown");
        snprintf(productDescription, sizeof(productDescription), "Unknown");
        snprintf(githubUrl, sizeof(githubUrl), "Unknown");
        snprintf(author, sizeof(author), "Unknown");
        snprintf(authorEmail, sizeof(authorEmail), "Unknown");
        snprintf(buildVersion, sizeof(buildVersion), "Unknown");
        snprintf(buildDate, sizeof(buildDate), "Unknown");
        snprintf(buildTime, sizeof(buildTime), "Unknown");
        snprintf(sketchMD5, sizeof(sketchMD5), "Unknown");
        snprintf(partitionAppName, sizeof(partitionAppName), "Unknown");
        snprintf(chipModel, sizeof(chipModel), "Unknown");
        snprintf(sdkVersion, sizeof(sdkVersion), "Unknown");
        snprintf(coreVersion, sizeof(coreVersion), "Unknown");
        snprintf(lastResetReasonString, sizeof(lastResetReasonString), "Unknown");
        snprintf(deviceId, sizeof(deviceId), "Unknown");
    }
};

// Dynamic system information (changes frequently)
struct SystemDynamicInfo {
    // Time & Uptime
    uint64_t uptimeMilliseconds;
    uint64_t uptimeSeconds;
    char currentTimestamp[TIMESTAMP_BUFFER_SIZE];
    char currentTimestampIso[TIMESTAMP_ISO_BUFFER_SIZE];
    
    // Memory - Heap (DRAM)
    uint32_t heapTotalBytes;
    uint32_t heapFreeBytes;
    uint32_t heapUsedBytes;
    uint32_t heapMinFreeBytes;
    uint32_t heapMaxAllocBytes;
    float heapFreePercentage;
    float heapUsedPercentage;
    
    // Memory - PSRAM
    uint32_t psramFreeBytes;
    uint32_t psramUsedBytes;
    uint32_t psramMinFreeBytes;
    uint32_t psramMaxAllocBytes;
    float psramFreePercentage;
    float psramUsedPercentage;
    
    // Storage - SPIFFS
    uint32_t spiffsTotalBytes;
    uint32_t spiffsUsedBytes;
    uint32_t spiffsFreeBytes;
    float spiffsFreePercentage;
    float spiffsUsedPercentage;
    
    // Performance
    float temperatureCelsius;
    
    // Network status
    int32_t wifiRssi;
    bool wifiConnected;
    char wifiSsid[NAME_BUFFER_SIZE];
    char wifiMacAddress[MAC_ADDRESS_BUFFER_SIZE];
    char wifiLocalIp[IP_ADDRESS_BUFFER_SIZE];
    char wifiGatewayIp[IP_ADDRESS_BUFFER_SIZE];
    char wifiSubnetMask[IP_ADDRESS_BUFFER_SIZE];
    char wifiDnsIp[IP_ADDRESS_BUFFER_SIZE];
    char wifiBssid[MAC_ADDRESS_BUFFER_SIZE];

    SystemDynamicInfo() {
        memset(this, 0, sizeof(*this));
        temperatureCelsius = -273.15f; // Invalid temp indicator
        wifiRssi = -100; // Invalid RSSI indicator
        snprintf(wifiSsid, sizeof(wifiSsid), "Unknown");
        snprintf(wifiMacAddress, sizeof(wifiMacAddress), "00:00:00:00:00:00");
        snprintf(wifiLocalIp, sizeof(wifiLocalIp), "0.0.0.0");
        snprintf(wifiGatewayIp, sizeof(wifiGatewayIp), "0.0.0.0");
        snprintf(wifiSubnetMask, sizeof(wifiSubnetMask), "0.0.0.0");
        snprintf(wifiDnsIp, sizeof(wifiDnsIp), "0.0.0.0");
        snprintf(wifiBssid, sizeof(wifiBssid), "00:00:00:00:00:00");
    }
};

struct PayloadMeter
{
  uint32_t channel;
  uint64_t unixTimeMs;
  float activePower;
  float powerFactor;

  PayloadMeter() : channel(0), unixTimeMs(0), activePower(0.0f), powerFactor(0.0f) {}

  PayloadMeter(
    uint32_t channel, 
    uint64_t unixTimeMs, 
    float activePower, 
    float powerFactor
  ) : channel(channel), 
      unixTimeMs(unixTimeMs), 
      activePower(activePower), 
      powerFactor(powerFactor) {}
};
