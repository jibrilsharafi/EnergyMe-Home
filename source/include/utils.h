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
#include "customwifi.h"
#include "crashmonitor.h"
#include "custommqtt.h"
#include "influxdbclient.h"
#include "customserver.h"
#include "modbustcp.h"
#include "led.h"
#include "structs.h"
#include "globals.h"

#define TASK_RESTART_NAME "restart_task"
#define TASK_RESTART_STACK_SIZE 4096
#define TASK_RESTART_PRIORITY 5

#define TASK_MAINTENANCE_NAME "maintenance_task"
#define TASK_MAINTENANCE_STACK_SIZE 4096
#define TASK_MAINTENANCE_PRIORITY 3
#define MAINTENANCE_CHECK_INTERVAL (10 * 1000) // Interval to check main parameters, to avoid overloading the loop

#define FUNCTION_NAME_BUFFER_SIZE 32
#define REASON_BUFFER_SIZE 128

#define JSON_STRING_PRINT_BUFFER_SIZE 512 // For JSON strings (print only, needed usually for debugging - Avoid being too large to prevent stack overflow)

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

void printDeviceStatusStatic();
void printDeviceStatusDynamic();

void updateStatistics();
void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument);
void printStatistics();

// Convenience functions for JSON API endpoints
void getJsonDeviceStaticInfo(JsonDocument& doc);
void getJsonDeviceDynamicInfo(JsonDocument& doc);

void setRestartSystem(const char* functionName, const char* reason);
void restartSystem();

void startMaintenanceTask();
void stopMaintenanceTask();

// General task management
void stopTaskGracefully(TaskHandle_t* taskHandle, const char* taskName);
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

void factoryReset();
void clearAllPreferences();

bool isLatestFirmwareInstalled();
void updateJsonFirmwareStatus(const char *status, const char *reason);

void getDeviceId(char* deviceId, size_t maxLength);


const char* getMqttStateReason(int state);

unsigned long long calculateExponentialBackoff(unsigned long long attempt, unsigned long long initialInterval, unsigned long long maxInterval, unsigned long long multiplier);

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds = true);