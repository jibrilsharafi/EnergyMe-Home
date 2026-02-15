// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

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
#include <nvs_flash.h> // For erasing ALL the NVS
#include <esp_ota_ops.h>
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include <ESP32-targz.h>

#include "binaries.h"
#include "buttonhandler.h"
#include "constants.h"
#include "customlog.h"
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
#define TASK_RESTART_STACK_SIZE (6 * 1024)
#define TASK_RESTART_PRIORITY 5

#define TASK_MAINTENANCE_NAME "maintenance_task"
#define TASK_MAINTENANCE_STACK_SIZE (5 * 1024)     // Maximum usage close to 5 kB
#define TASK_MAINTENANCE_PRIORITY 3
#define MAINTENANCE_CHECK_INTERVAL (60 * 1000)

// System restart thresholds
#define MINIMUM_FREE_HEAP_SIZE (1 * 1024) // Below this value (in bytes), the system will restart. This value can get very low due to the presence of the PSRAM to support
#define MINIMUM_FREE_PSRAM_SIZE (10 * 1024) // Below this value (in bytes), the system will restart
#define MINIMUM_FREE_LITTLEFS_SIZE (10 * 1024) // Below this value (in bytes), the system will clear the log
#define SYSTEM_RESTART_FAILSAFE_TIMER_NAME "restart_failsafe"
#define SYSTEM_RESTART_FAILSAFE_TIMEOUT (10 * 1000) // Failsafe timeout - if restart doesn't complete within this time, force restart via timer
#define MINIMUM_FIRMWARE_SIZE (100 * 1024) // Minimum firmware size in bytes (100KB) - prevents empty/invalid uploads

// First boot
#define IS_FIRST_BOOT_DONE_KEY "first_boot"

// Stringify macro helper for BUILD_ENV_NAME - If you try to concatenate directly, it will crash the build
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Even though the ArduinoJson automatically allocates the JSON documents to PSRAM when the heap is exhausted,
// it still leads to defragmentation here. Thus, to avoid this, we explicitly use a custom allocator.
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    return ps_malloc(size);
  }

  void deallocate(void* pointer) override {
    free(pointer);
  }

  void* reallocate(void* ptr, size_t new_size) override {
    return ps_realloc(ptr, new_size);
  }
};

// Time utilities (high precision 64-bit alternatives)
// Come one, on this ESP32S3 and in 2025 can we still use 32bits millis
// that will overflow in just 49 days?
// esp_timer_get_time() returns microseconds since boot in 64-bit format,
inline uint64_t millis64() {
    return esp_timer_get_time() / 1000ULL;
}

// Same reason as above
inline uint64_t micros64() {
    return esp_timer_get_time();
}

// Validation utilities
inline bool isChannelValid(uint8_t channel) {return channel < CHANNEL_COUNT;}

// String validation utilities
inline bool isStringLengthValid(const char* str, size_t minLength, size_t maxLength) {
    if (str == nullptr) return false;
    size_t len = strlen(str);
    return len >= minLength && len <= maxLength;
}

// Numeric range validation utilities
inline bool isValueInRange(float value, float min, float max) {
    return value >= min && value <= max;
}

inline bool isValueInRange(int32_t value, int32_t min, int32_t max) {
    return value >= min && value <= max;
}

// Mathematical utilities
uint64_t calculateExponentialBackoff(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval, uint64_t multiplier);

inline float roundToDecimals(float value, uint8_t decimals = 3) {
    float factor = powf(10.0f, decimals);
    return roundf(value * factor) / factor;
}

inline double roundToDecimals(double value, uint8_t decimals = 3) {
    double factor = pow(10.0, decimals);
    return round(value * factor) / factor;
}

// Device identification
void getDeviceId(char* deviceId, size_t maxLength);
bool readEfuseProvisioningData(EfuseProvisioningData& data);

// System information and monitoring
void populateSystemStaticInfo(SystemStaticInfo& info);
void populateSystemDynamicInfo(SystemDynamicInfo& info);
void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument &doc);
void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument &doc);
void getJsonDeviceStaticInfo(JsonDocument &doc);
void getJsonDeviceDynamicInfo(JsonDocument &doc);

// Statistics management
void updateStatistics();
void statisticsToJson(Statistics& statistics, JsonDocument &jsonDocument);
void printStatistics();

// System status printing
void printDeviceStatusStatic();
void printDeviceStatusDynamic();

// FreeRTOS task management
void stopTaskGracefully(TaskHandle_t* taskHandle, const char* taskName);
void startMaintenanceTask();
void stopMaintenanceTask();
size_t getLogFileSize();

// Task information utilities
inline TaskInfo getTaskInfoSafely(TaskHandle_t taskHandle, uint32_t stackSize)
{
    // Defensive check against corrupted or invalid task handles
    if (taskHandle != NULL && eTaskGetState(taskHandle) != eInvalid) {
        return TaskInfo(stackSize, uxTaskGetStackHighWaterMark(taskHandle));
    } else {
        return TaskInfo(); // Return empty/default TaskInfo if task is not running or invalid
    }
}
TaskInfo getMaintenanceTaskInfo();

// System restart and maintenance
bool setRestartSystem(const char* reason, bool factoryReset = false);
inline const char* getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "Unknown";
        case ESP_RST_POWERON: return "Power on";
        case ESP_RST_EXT: return "External pin";
        case ESP_RST_SW: return "Software";
        case ESP_RST_PANIC: return "Exception/panic";
        case ESP_RST_INT_WDT: return "Interrupt watchdog";
        case ESP_RST_TASK_WDT: return "Task watchdog";
        case ESP_RST_WDT: return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep";
        case ESP_RST_BROWNOUT: return "Brownout";
        case ESP_RST_SDIO: return "SDIO";
        default: return "Undefined";
    }
}

// JSON utilities
bool safeSerializeJson(JsonDocument &jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);

// Preferences management
bool isFirstBootDone();
void setFirstBootDone();
void createAllNamespaces();
void clearAllPreferences(bool nuclearOption = false); // No real function passes true to this function, but maybe it will be useful in the future

// LittleFS file operations
bool listLittleFsFiles(JsonDocument &doc, const char* folderPath = nullptr); // Optional folder path to filter files
bool getLittleFsFileContent(const char* filepath, char* buffer, size_t bufferSize);
const char* getContentTypeFromFilename(const char* filename);
bool compressFile(const char* filepath);
void migrateCsvToGzip(const char* dirPath, const char* excludePrefix = nullptr); // Migrates CSV files to gzip, excluding files with the specified prefix (optional)

// Energy file consolidation
bool migrateEnergyFilesToDailyFolder(); // One-time migration of existing /energy/*.csv.gz to /energy/daily/
bool consolidateDailyFilesToMonthly(const char* yearMonth, const char* excludeDate = nullptr); // Consolidate daily files for YYYY-MM into monthly archive (optionally exclude a specific date)
bool consolidateMonthlyFilesToYearly(const char* year, const char* excludeMonth = nullptr); // Consolidate monthly files for YYYY into yearly archive (optionally exclude a specific month)

// String utilities
inline bool endsWith(const char* s, const char* suffix) {
    size_t ls = strlen(s), lsf = strlen(suffix);
    return lsf <= ls && strcmp(s + ls - lsf, suffix) == 0;
}

inline bool startsWith(const char* s, const char* prefix) {
    size_t ls = strlen(s), lsp = strlen(prefix);
    return lsp <= ls && strncmp(s, prefix, lsp) == 0;
}

// Mutex utilities
inline bool createMutexIfNeeded(SemaphoreHandle_t* mutex) {
    if (!mutex) return false;
    
    if (*mutex == nullptr) {
        *mutex = xSemaphoreCreateMutex();
        if (*mutex == nullptr) {
            LOG_ERROR("Failed to create mutex");
            return false;
        }
    }
    return true;
}

inline void deleteMutex(SemaphoreHandle_t* mutex) {
    if (mutex && *mutex) {
        vSemaphoreDelete(*mutex);
        *mutex = nullptr;
    }
}

inline bool acquireMutex(SemaphoreHandle_t* mutex, uint64_t timeout = CONFIG_MUTEX_TIMEOUT_MS) {
    return mutex && *mutex && xSemaphoreTake(*mutex, pdMS_TO_TICKS(timeout)) == pdTRUE;
}

inline void releaseMutex(SemaphoreHandle_t* mutex) {
    if (mutex && *mutex) xSemaphoreGive(*mutex);
}

inline static void* ota_calloc_psram(size_t n, size_t size) {
    // Use SPIRAM; still 8-bit addressable.
    return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

inline static void ota_free_psram(void* p) {
    heap_caps_free(p);
}