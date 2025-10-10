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
#include <ESP32-targz.h>

#include "binaries.h"
#include "buttonhandler.h"
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
#define SYSTEM_RESTART_DELAY (3 * 1000) // The delay before restarting the system after a restart request, needed to allow the system to finish the current operations (like flushing logs)
#define MINIMUM_FIRMWARE_SIZE (100 * 1024) // Minimum firmware size in bytes (100KB) - prevents empty/invalid uploads
#define STOP_SERVICES_TASK_NAME "stop_services_task"
#define STOP_SERVICES_TASK_STACK_SIZE (4 * 1024)
#define STOP_SERVICES_TASK_PRIORITY 10

// Restart infos
#define FUNCTION_NAME_BUFFER_SIZE 32
#define REASON_BUFFER_SIZE 128
#define JSON_STRING_PRINT_BUFFER_SIZE 512 // For JSON strings (print only, needed usually for debugging - Avoid being too large to prevent stack overflow)

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

// Validation utilities
inline bool isChannelValid(uint8_t channel) {return channel < CHANNEL_COUNT;}

// Mathematical utilities
uint64_t calculateExponentialBackoff(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval, uint64_t multiplier);
inline float roundToDecimals(float value, uint8_t decimals = 3) {
    float factor = powf(10.0f, decimals);
    return (long)(value * factor + 0.5f) / factor;
}

// Device identification
void getDeviceId(char* deviceId, size_t maxLength);

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
void setRestartSystem(const char* reason, bool factoryReset = false);

// JSON utilities
bool safeSerializeJson(JsonDocument &jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);

// Preferences management
bool isFirstBootDone();
void setFirstBootDone();
void createAllNamespaces();
void clearAllPreferences(bool nuclearOption = false); // No real function passes true to this function, but maybe it will be useful in the future

// LittleFS file operations
bool listLittleFsFiles(JsonDocument &doc);
bool getLittleFsFileContent(const char* filepath, char* buffer, size_t bufferSize);
const char* getContentTypeFromFilename(const char* filename);
bool compressFile(const char* filepath);
void migrateCsvToGzip(const char* dirPath, const char* excludePrefix = nullptr); // Migrates CSV files to gzip, excluding files with the specified prefix (optional)

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