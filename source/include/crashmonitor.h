#pragma once

#include <esp_attr.h>
#include <esp_system.h>
#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

#include "constants.h"
#include "globals.h"
#include "mqtt.h"
#include "structs.h"
#include "utils.h"

#define MAGIC_WORD_RTC 0xDEADBEEF // This is crucial to ensure that the RTC data has sensible values or it is just some garbage after reboot
#define MAX_CRASH_COUNT 3 // Maximum amount of consecutive crashes before triggering a rollback
#define MAX_RESET_COUNT 10 // Maximum amount of consecutive resets before triggering a rollback
#define CRASH_COUNTER_TIMEOUT (180 * 1000) // Timeout for the crash counter to reset
#define CRASH_RESET_TASK_NAME "crash_reset_task"
#define CRASH_RESET_TASK_STACK_SIZE 4096 // PLEASE: never put below this as even a single log will exceed 1024 kB easily.. We don't need to optimize so much :)
#define CRASH_RESET_TASK_PRIORITY 1 // This does not need to be high priority since it will only reset a counter and not do any heavy work

namespace CrashMonitor {
    void begin();
    // No need to stop anything here since once it executes at the beginning, there is no other use for this

    bool isLastResetDueToCrash();
    unsigned long getCrashCount();
    unsigned long getResetCount();
    const char* getResetReasonString(esp_reset_reason_t reason);

    void clearCrashCount(); // Useful for avoiding crash loops (e.g. during factory reset)
    
    // Core dump data access functions
    bool hasCoreDump();
    size_t getCoreDumpSize();
    bool getCoreDumpInfo(size_t* size, size_t* address);
    bool getCoreDumpChunk(uint8_t* buffer, size_t offset, size_t chunkSize, size_t* bytesRead);
    bool getFullCoreDump(uint8_t* buffer, size_t bufferSize, size_t* actualSize);
    void clearCoreDump();

    // JSON payload functions
    bool getCoreDumpInfoJson(JsonDocument& doc);                                      // Comprehensive crash info with backtrace
    bool getCoreDumpChunkJson(JsonDocument& doc, size_t offset, size_t chunkSize);    // Core dump chunk as base64
}