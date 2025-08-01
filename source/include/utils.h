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
#define TASK_RESTART_STACK_SIZE (4 * 1024)
#define TASK_RESTART_PRIORITY 5

#define TASK_MAINTENANCE_NAME "maintenance_task"
#define TASK_MAINTENANCE_STACK_SIZE (4 * 1024)
#define TASK_MAINTENANCE_PRIORITY 3
#define MAINTENANCE_CHECK_INTERVAL (10 * 1000) // Interval to check main parameters, to avoid overloading the loop

// System restart thresholds
#define MINIMUM_FREE_HEAP_SIZE (1 * 1024) // Below this value (in bytes), the system will restart. This value can get very low due to the presence of the PSRAM to support
#define MINIMUM_FREE_PSRAM_SIZE (10 * 1024) // Below this value (in bytes), the system will restart
#define MINIMUM_FREE_SPIFFS_SIZE (10 * 1024) // Below this value (in bytes), the system will clear the log
#define SYSTEM_RESTART_DELAY (3 * 1000) // The delay before restarting the system after a restart request, needed to allow the system to finish the current operations
#define MINIMUM_FIRMWARE_SIZE (100 * 1024) // Minimum firmware size in bytes (100KB) - prevents empty/invalid uploads

// Restart infos
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

void setRestartSystem(const char* functionName, const char* reason, bool factoryReset = false);

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
void createDefaultCalibrationFile();
void createDefaultDailyEnergyFile();

void clearAllPreferences();

void startMaintenanceTask();
void stopMaintenanceTask();

bool isLatestFirmwareInstalled();
void updateJsonFirmwareStatus(const char *status, const char *reason);

void getDeviceId(char* deviceId, size_t maxLength);

const char* getMqttStateReason(int state);

unsigned long long calculateExponentialBackoff(unsigned long long attempt, unsigned long long initialInterval, unsigned long long maxInterval, unsigned long long multiplier);