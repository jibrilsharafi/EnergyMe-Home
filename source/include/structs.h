#pragma once

#include <Arduino.h>

#include "constants.h"

// Enumeration for different types of ADE7953 interrupts | TODO: move to ade7953.h
enum class Ade7953InterruptType {
  NONE,           // No interrupt or unknown
  CYCEND,         // Line cycle end - normal meter reading
  RESET,          // Device reset detected
  CRC_CHANGE,     // CRC register change detected
  OTHER           // Other interrupts (SAG, etc.)
};

struct Statistics {
  unsigned long ade7953TotalInterrupts;
  unsigned long ade7953TotalHandledInterrupts;
  unsigned long ade7953ReadingCount;
  unsigned long ade7953ReadingCountFailure;

  unsigned long mqttMessagesPublished;
  unsigned long mqttMessagesPublishedError;
  
  unsigned long customMqttMessagesPublished;
  unsigned long customMqttMessagesPublishedError;
  
  unsigned long modbusRequests;
  unsigned long modbusRequestsError;
  
  unsigned long influxdbUploadCount;
  unsigned long influxdbUploadCountError;

  unsigned long wifiConnection;
  unsigned long wifiConnectionError;

  unsigned long logVerbose;
  unsigned long logDebug;
  unsigned long logInfo;
  unsigned long logWarning;
  unsigned long logError;
  unsigned long logFatal;

  Statistics() 
    : ade7953TotalInterrupts(0), ade7953TotalHandledInterrupts(0), ade7953ReadingCount(0), ade7953ReadingCountFailure(0), 
    mqttMessagesPublished(0), mqttMessagesPublishedError(0), customMqttMessagesPublished(0), customMqttMessagesPublishedError(0), modbusRequests(0), modbusRequestsError(0), 
    influxdbUploadCount(0), influxdbUploadCountError(0), wifiConnection(0), wifiConnectionError(0),
    logVerbose(0), logDebug(0), logInfo(0), logWarning(0), logError(0), logFatal(0) {}
};

// Static system information (rarely changes, only with firmware updates)
struct SystemStaticInfo {
    // Product & Company
    char companyName[64];
    char productName[64];
    char fullProductName[128];
    char productDescription[256];
    char githubUrl[128];
    char author[64];
    char authorEmail[64];
    
    // Firmware & Build
    char buildVersion[32];
    char buildDate[32];
    char buildTime[32];
    char sketchMD5[33];  // MD5 hash (32 chars + null terminator)
    char partitionAppName[8]; // Name of the partition for the app (e.g., "app0", "app1")
    
    // Hardware & Chip (mostly static)
    char chipModel[32];        // ESP32, ESP32-S3, etc.
    unsigned char chipRevision;      // Hardware revision
    unsigned char chipCores;         // Number of CPU cores
    unsigned long long chipId;           // Unique chip ID
    unsigned long flashChipSizeBytes;
    unsigned long flashChipSpeedHz;
    unsigned long psramSizeBytes;   // Total PSRAM (if available)
    unsigned long cpuFrequencyMHz;  // CPU frequency
    
    // SDK versions
    char sdkVersion[32];
    char coreVersion[32];
    
    // Crash and reset monitoring
    unsigned long crashCount;                    // Total crashes since last manual reset
    unsigned long resetCount;                    // Total resets since first boot
    unsigned long lastResetReason;               // ESP reset reason code
    char lastResetReasonString[32];         // Human readable reset reason
    bool lastResetWasCrash;                 // True if last reset was due to crash
    
    // Device configuration
    char deviceId[32];
    
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
    unsigned long long uptimeMilliseconds;
    unsigned long uptimeSeconds;
    char currentTimestamp[32];
    
    // Memory - Heap (DRAM)
    unsigned long heapTotalBytes;
    unsigned long heapFreeBytes;
    unsigned long heapUsedBytes;
    unsigned long heapMinFreeBytes;      // Lowest since boot
    unsigned long heapMaxAllocBytes;     // Largest allocatable block
    float heapFreePercentage;
    float heapUsedPercentage;
    
    // Memory - PSRAM
    unsigned long psramFreeBytes;
    unsigned long psramUsedBytes;
    unsigned long psramMinFreeBytes;
    unsigned long psramMaxAllocBytes;
    float psramFreePercentage;
    float psramUsedPercentage;
    
    // Storage - SPIFFS
    unsigned long spiffsTotalBytes;
    unsigned long spiffsUsedBytes;
    unsigned long spiffsFreeBytes;
    float spiffsFreePercentage;
    float spiffsUsedPercentage;
    
    // Performance
    float temperatureCelsius;
    
    // Network status
    long wifiRssi;
    bool wifiConnected;
    char wifiSsid[128];
    char wifiMacAddress[18];
    char wifiLocalIp[16];
    char wifiGatewayIp[16];
    char wifiSubnetMask[16];
    char wifiDnsIp[16];
    char wifiBssid[18];

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

enum Phase : int {
    PHASE_1 = 1,
    PHASE_2 = 2,
    PHASE_3 = 3,
};

enum Channel : int {
    CHANNEL_A,
    CHANNEL_B,
};

enum ChannelNumber : int {
  CHANNEL_INVALID = -1,
  CHANNEL_0 = 0,
  CHANNEL_1 = 1,
  CHANNEL_2 = 2,
  CHANNEL_3 = 3,
  CHANNEL_4 = 4,
  CHANNEL_5 = 5,
  CHANNEL_6 = 6,
  CHANNEL_7 = 7,
  CHANNEL_8 = 8,
  CHANNEL_9 = 9,
  CHANNEL_10 = 10,
  CHANNEL_11 = 11,
  CHANNEL_12 = 12,
  CHANNEL_13 = 13,
  CHANNEL_14 = 14,
  CHANNEL_15 = 15,
  CHANNEL_16 = 16,
  CHANNEL_COUNT = 17
};

enum Measurement : int {
    VOLTAGE,
    CURRENT,
    ACTIVE_POWER,
    REACTIVE_POWER,
    APPARENT_POWER,
    POWER_FACTOR,
};

/*
 * Struct to hold the real-time meter values for a specific channel
  * Contains:
  * - voltage: Voltage in Volts
  * - current: Current in Amperes
  * - activePower: Active power in Watts
  * - reactivePower: Reactive power in VAR
  * - apparentPower: Apparent power in VA
  * - powerFactor: Power factor (-1 to 1, where negative values indicate capacitive load while positive values indicate inductive load)
  * - activeEnergyImported: Active energy imported in Wh
  * - activeEnergyExported: Active energy exported in Wh
  * - reactiveEnergyImported: Reactive energy imported in VArh
  * - reactiveEnergyExported: Reactive energy exported in VArh
  * - apparentEnergy: Apparent energy in VAh (only absolute value)
  * - lastUnixTimeMilliseconds: Last time the values were updated in milliseconds since epoch. Useful for absolute time tracking
 */
struct MeterValues
{
  float voltage;
  float current;
  float activePower;
  float reactivePower;
  float apparentPower;
  float powerFactor;
  float activeEnergyImported;
  float activeEnergyExported;
  float reactiveEnergyImported;
  float reactiveEnergyExported;
  float apparentEnergy;
  unsigned long long lastUnixTimeMilliseconds;
  unsigned long lastMillis;

  MeterValues()
    : voltage(230.0), current(0.0f), activePower(0.0f), reactivePower(0.0f), apparentPower(0.0f), powerFactor(0.0f),
      activeEnergyImported(0.0f), activeEnergyExported(0.0f), reactiveEnergyImported(0.0f), 
      reactiveEnergyExported(0.0f), apparentEnergy(0.0f), lastUnixTimeMilliseconds(0), lastMillis(0) {}
};

struct PayloadMeter
{
  int channel;
  unsigned long long unixTimeMs;
  float activePower;
  float powerFactor;

  PayloadMeter() : channel(0), unixTimeMs(0), activePower(0.0f), powerFactor(0.0f) {}

  PayloadMeter(int channel, unsigned long long unixTimeMs, float activePower, float powerFactor)
      : channel(channel), unixTimeMs(unixTimeMs), activePower(activePower), powerFactor(powerFactor) {}
};

struct CalibrationValues
{
  char label[CALIBRATION_LABEL_BUFFER_SIZE];
  float vLsb;
  float aLsb;
  float wLsb;
  float varLsb;
  float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CalibrationValues()
    : vLsb(1.0), aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {
      snprintf(label, sizeof(label), "Calibration");
    }
};


struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  char label[CHANNEL_LABEL_BUFFER_SIZE];
  Phase phase;
  CalibrationValues calibrationValues;

  ChannelData()
    : index(0), active(false), reverse(false), phase(PHASE_1), calibrationValues(CalibrationValues()) {
      snprintf(label, sizeof(label), "Channel");
    }
};

// Used to track consecutive zero energy readings for channel 0
struct ChannelState { // TODO: what the heck was i thinking with this?
    unsigned long consecutiveZeroCount = 0;
};

struct PublicLocation // TODO: Move to utils or time or where needed
{
  char country[COUNTRY_BUFFER_SIZE];
  char city[CITY_BUFFER_SIZE];
  float latitude;
  float longitude;

  PublicLocation() : country("Unknown"), city("Unknown"), latitude(45.0), longitude(9.0) {} // Default to Milan coordinates
};

struct RestartConfiguration // TODO: really needed?
{
  bool isRequired;
  unsigned long requiredAt;
  char functionName[FUNCTION_NAME_BUFFER_SIZE];
  char reason[REASON_BUFFER_SIZE];

  RestartConfiguration() : isRequired(false), requiredAt(0xFFFFFFFF) {
    snprintf(functionName, sizeof(functionName), "Unknown");
    snprintf(reason, sizeof(reason), "Unknown");
  }
};

struct CustomMqttConfiguration { // TODO: deprecate this
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    char clientid[CLIENT_ID_BUFFER_SIZE];
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    int frequency;
    bool useCredentials;
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    char lastConnectionStatus[STATUS_BUFFER_SIZE];
    char lastConnectionAttemptTimestamp[TIMESTAMP_STRING_BUFFER_SIZE];

    CustomMqttConfiguration() 
        : enabled(DEFAULT_IS_CUSTOM_MQTT_ENABLED), 
          port(MQTT_CUSTOM_PORT_DEFAULT),
          frequency(MQTT_CUSTOM_FREQUENCY_DEFAULT),
          useCredentials(MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT) {
      snprintf(server, sizeof(server), "%s", MQTT_CUSTOM_SERVER_DEFAULT);
      snprintf(clientid, sizeof(clientid), "%s", MQTT_CUSTOM_CLIENTID_DEFAULT);
      snprintf(topic, sizeof(topic), "%s", MQTT_CUSTOM_TOPIC_DEFAULT);
      snprintf(username, sizeof(username), "%s", MQTT_CUSTOM_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", MQTT_CUSTOM_PASSWORD_DEFAULT);
      snprintf(lastConnectionStatus, sizeof(lastConnectionStatus), "Never attempted");
      snprintf(lastConnectionAttemptTimestamp, sizeof(lastConnectionAttemptTimestamp), "Never attempted");
    }
};