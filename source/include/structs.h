// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "constants.h"

struct Statistics { // This will be global and we accept the very small race condition as we only do ++ or read the value (almost atomic?)
  uint64_t ade7953TotalInterrupts;
  uint64_t ade7953TotalHandledInterrupts;
  uint64_t ade7953ReadingCount;
  uint64_t ade7953ReadingCountFailure;

  uint64_t mqttMessagesPublished;
  uint64_t mqttMessagesPublishedError;
  uint64_t mqttConnections;
  uint64_t mqttConnectionErrors;
  
  uint64_t customMqttMessagesPublished;
  uint64_t customMqttMessagesPublishedError;
  
  uint64_t modbusRequests;
  uint64_t modbusRequestsError;
  
  uint64_t influxdbUploadCount;
  uint64_t influxdbUploadCountError;

  uint64_t wifiConnection;
  uint64_t wifiConnectionError;

  uint64_t webServerRequests;
  uint64_t webServerRequestsError;

  uint64_t logVerbose;
  uint64_t logDebug;
  uint64_t logInfo;
  uint64_t logWarning;
  uint64_t logError;
  uint64_t logFatal;
  uint64_t logDropped;

  Statistics() 
    : ade7953TotalInterrupts(0), ade7953TotalHandledInterrupts(0), ade7953ReadingCount(0), ade7953ReadingCountFailure(0), 
    mqttMessagesPublished(0), mqttMessagesPublishedError(0), mqttConnections(0), mqttConnectionErrors(0), 
    customMqttMessagesPublished(0), customMqttMessagesPublishedError(0), modbusRequests(0), modbusRequestsError(0), 
    influxdbUploadCount(0), influxdbUploadCountError(0), wifiConnection(0), wifiConnectionError(0),
    webServerRequests(0), webServerRequestsError(0),
    logVerbose(0), logDebug(0), logInfo(0), logWarning(0), logError(0), logFatal(0), logDropped(0) {}
};

struct TaskInfo {
  uint32_t allocatedStack;
  uint32_t minimumFreeStack;
  float freePercentage;
  float usedPercentage;

  TaskInfo() : allocatedStack(0), minimumFreeStack(0), freePercentage(0.0f), usedPercentage(0.0f) {}
  TaskInfo(uint32_t allocated, uint32_t minimum) : allocatedStack(allocated), minimumFreeStack(minimum) {
    freePercentage = (allocatedStack > 0) ? (100.0f * (float)(minimumFreeStack) / (float)(allocatedStack)) : 0.0f;
    usedPercentage = (allocatedStack > 0) ? (100.0f * (float)(allocatedStack - minimumFreeStack) / (float)(allocatedStack)) : 0.0f;
  }
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
    char buildDate[TIMESTAMP_ISO_BUFFER_SIZE];
    char buildTime[TIMESTAMP_ISO_BUFFER_SIZE];
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
    uint32_t psramTotalBytes;
    uint32_t psramFreeBytes;
    uint32_t psramUsedBytes;
    uint32_t psramMinFreeBytes;
    uint32_t psramMaxAllocBytes;
    float psramFreePercentage;
    float psramUsedPercentage;
    
    // Storage - LittleFS
    uint32_t littlefsTotalBytes;
    uint32_t littlefsUsedBytes;
    uint32_t littlefsFreeBytes;
    float littlefsFreePercentage;
    float littlefsUsedPercentage;

    // Storage - NVS
    uint32_t totalUsableEntries;
    uint32_t usedEntries;
    uint32_t availableEntries;
    float usedEntriesPercentage;
    float availableEntriesPercentage;
    uint32_t namespaceCount;

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
    
    // Tasks
    TaskInfo mqttTaskInfo;
    TaskInfo mqttOtaTaskInfo;
    TaskInfo customMqttTaskInfo;
    TaskInfo customServerHealthCheckTaskInfo;
    TaskInfo customServerOtaTimeoutTaskInfo;
    TaskInfo ledTaskInfo;
    TaskInfo influxDbTaskInfo;
    TaskInfo crashMonitorTaskInfo;
    TaskInfo buttonHandlerTaskInfo;
    TaskInfo udpLogTaskInfo;
    TaskInfo customWifiTaskInfo;
    TaskInfo ade7953MeterReadingTaskInfo;
    TaskInfo ade7953EnergySaveTaskInfo;
    TaskInfo ade7953HourlyCsvTaskInfo;
    TaskInfo maintenanceTaskInfo;

    SystemDynamicInfo() {
        memset(this, 0, sizeof(*this));
        snprintf(wifiSsid, sizeof(wifiSsid), "Unknown");
        snprintf(wifiMacAddress, sizeof(wifiMacAddress), "00:00:00:00:00:00");
        snprintf(wifiLocalIp, sizeof(wifiLocalIp), "0.0.0.0");
        snprintf(wifiGatewayIp, sizeof(wifiGatewayIp), "0.0.0.0");
        snprintf(wifiSubnetMask, sizeof(wifiSubnetMask), "0.0.0.0");
        snprintf(wifiDnsIp, sizeof(wifiDnsIp), "0.0.0.0");
        snprintf(wifiBssid, sizeof(wifiBssid), "00:00:00:00:00:00");
    }
};

struct EfuseProvisioningData {
    bool isProvisioned;
    uint32_t serial;
    uint64_t manufacturingDate;
    uint16_t hardwareVersion;

    EfuseProvisioningData() : isProvisioned(false), serial(0), manufacturingDate(0), hardwareVersion(0) {}
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
