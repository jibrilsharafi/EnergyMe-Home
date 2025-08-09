#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <rom/rtc.h>
#include <vector>
#include <esp_mac.h> // For the MAC address
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <nvs.h> // For low-level NVS statistics
#if ENV_DEV
#include <nvs_flash.h> // For erasing ALL the NVS
#endif
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
#define TASK_RESTART_STACK_SIZE (8 * 1024)
#define TASK_RESTART_PRIORITY 5

#define TASK_MAINTENANCE_NAME "maintenance_task"
#define TASK_MAINTENANCE_STACK_SIZE (8 * 1024)
#define TASK_MAINTENANCE_PRIORITY 3
#define MAINTENANCE_CHECK_INTERVAL (60 * 1000) // Interval to check main parameters, to avoid overloading the loop

// System restart thresholds
#define MINIMUM_FREE_HEAP_SIZE (1 * 1024) // Below this value (in bytes), the system will restart. This value can get very low due to the presence of the PSRAM to support
#define MINIMUM_FREE_PSRAM_SIZE (10 * 1024) // Below this value (in bytes), the system will restart
#define MINIMUM_FREE_LITTLEFS_SIZE (10 * 1024) // Below this value (in bytes), the system will clear the log
#define SYSTEM_RESTART_DELAY (1 * 1000) // The delay before restarting the system after a restart request, needed to allow the system to finish the current operations
#define MINIMUM_FIRMWARE_SIZE (100 * 1024) // Minimum firmware size in bytes (100KB) - prevents empty/invalid uploads

// Restart infos
#define FUNCTION_NAME_BUFFER_SIZE 32
#define REASON_BUFFER_SIZE 128
#define JSON_STRING_PRINT_BUFFER_SIZE 512 // For JSON strings (print only, needed usually for debugging - Avoid being too large to prevent stack overflow)

// First boot
#define IS_FIRST_BOOT_DONE_KEY "first_boot"

// Time utilities (high precision 64-bit alternatives)
// Come one, on this ESP32S3 and in 2025 can we still use 32bits millis
// that will overflow in just 49 days?
// esp_timer_get_time() returns microseconds since boot in 64-bit format,
inline uint64_t millis64() {
    return esp_timer_get_time() / 1000ULL;
}

// Validation utilities
inline bool isChannelValid(uint8_t channel) {return channel < CHANNEL_COUNT;}

// Mathematical utilities
uint64_t calculateExponentialBackoff(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval, uint64_t multiplier);

// Device identification
void getDeviceId(char* deviceId, size_t maxLength);

// System information and monitoring
void populateSystemStaticInfo(SystemStaticInfo& info);
void populateSystemDynamicInfo(SystemDynamicInfo& info);
void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument& doc);
void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument& doc);
void getJsonDeviceStaticInfo(JsonDocument& doc);
void getJsonDeviceDynamicInfo(JsonDocument& doc);

// Statistics management
void updateStatistics();
void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument);
void printStatistics();

// System status printing
void printDeviceStatusStatic();
void printDeviceStatusDynamic();

// FreeRTOS task management
void stopTaskGracefully(TaskHandle_t* taskHandle, const char* taskName);
void startMaintenanceTask();
void stopMaintenanceTask();

// System restart and maintenance
void setRestartSystem(const char* reason, bool factoryReset = false);

// JSON utilities
bool safeSerializeJson(JsonDocument& jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);

// Preferences management
bool isFirstBootDone();
void setFirstBootDone();
void createAllNamespaces();
void clearAllPreferences();

// LittleFS file operations
bool listLittleFsFiles(JsonDocument& doc);
bool getLittleFsFileContent(const char* filepath, char* buffer, size_t bufferSize);
const char* getContentTypeFromFilename(const char* filename);

// String utilities
bool endsWith(const char* s, const char* suffix);

// Mutex utilities
inline bool acquireMutex(SemaphoreHandle_t* mutex, uint64_t timeout) {
    if (mutex && xSemaphoreTake(*mutex, pdMS_TO_TICKS(timeout)) != pdTRUE) {
        return false;
    } else {
        return true;
    }
}

inline void releaseMutex(SemaphoreHandle_t* mutex) {
    if (mutex) xSemaphoreGive(*mutex);
}