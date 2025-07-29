#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include <esp_system.h>
#include <rom/rtc.h>
#include <vector>
#include <esp_mac.h> // For the MAC address
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

#include "binaries.h"
#include "constants.h"
#include "customtime.h"
#include "crashmonitor.h"
#include "led.h"
#include "structs.h"
#include "globals.h"

#define TASK_RESTART_NAME "restart_task"
#define TASK_RESTART_STACK_SIZE 4096
#define TASK_RESTART_PRIORITY 5

#define TASK_MAINTENANCE_NAME "maintenance_task"
#define TASK_MAINTENANCE_STACK_SIZE 4096
#define TASK_MAINTENANCE_PRIORITY 2
#define MAINTENANCE_CHECK_INTERVAL (10 * 1000) // Interval to check main parameters, to avoid overloading the loop

// Come one, on this ESP32S3 and in 2025 can we still use 32bits millis
// that will overflow in just 49 days?
// esp_timer_get_time() returns microseconds since boot in 64-bit format,
inline unsigned long long millis64() {
    return esp_timer_get_time() / 1000ULL;
}

inline unsigned long long micros64() {
    return esp_timer_get_time();
}

// New system info functions
void populateSystemStaticInfo(SystemStaticInfo& info);
void populateSystemDynamicInfo(SystemDynamicInfo& info);
void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument& doc);
void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument& doc);

// Convenience functions for JSON API endpoints
void getJsonDeviceStaticInfo(JsonDocument& doc);
void getJsonDeviceDynamicInfo(JsonDocument& doc);

void setRestartSystem(const char* functionName, const char* reason);
void restartSystem();

void startMaintenanceTask();

void printMeterValues(MeterValues* meterValues, ChannelData* channelData);
void printDeviceStatus();
void printDeviceStatusStatic();
void printDeviceStatusDynamic();
void updateStatistics();
void printStatistics();

bool safeSerializeJson(JsonDocument& jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);

// Legacy SPIFFS functions for backward compatibility during transition
bool deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);
void createEmptyJsonFile(const char* path);

std::vector<const char*> checkMissingFiles();
void createDefaultFilesForMissingFiles(const std::vector<const char*>& missingFiles);
bool checkAllFiles();
void createDefaultCustomMqttConfigurationFile();
void createDefaultChannelDataFile();
void createDefaultCalibrationFile();
void createDefaultAde7953ConfigurationFile();
void createDefaultEnergyFile();
void createDefaultDailyEnergyFile();

bool getPublicLocation(PublicLocation* publicLocation);
bool getPublicTimezone(int* gmtOffset, int* dstOffset);

void factoryReset();
void clearAllPreferences();

bool isLatestFirmwareInstalled();
void updateJsonFirmwareStatus(const char *status, const char *reason);

void getDeviceId(char* deviceId, size_t maxLength);

void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument);

const char* getMqttStateReason(int state);

void decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize);
void readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* value, size_t valueSize);
void writeEncryptedPreferences(const char* preference_key, const char* value);
void clearCertificates();
bool checkCertificatesExist();

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds = true);