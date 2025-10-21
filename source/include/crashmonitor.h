#pragma once

#include <esp_attr.h>
#include <esp_system.h>
#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "esp_core_dump.h"

#include "constants.h"
#include "globals.h"
#include "mqtt.h"
#include "structs.h"
#include "utils.h"

#ifdef ENV_DEV
#define MAX_CRASH_COUNT 10     // Higher limits in development
#define MAX_RESET_COUNT 30
#define MAX_QUICK_RESTARTS 30
#else
#define MAX_CRASH_COUNT 3      // Production defaults
#define MAX_RESET_COUNT 10
#define MAX_QUICK_RESTARTS 5
#endif

#define COUNTERS_RESET_TIMEOUT (180 * 1000) // Timeout for the consecutive crash counter to reset

// Safe mode protection against infinite restart loops
#define QUICK_RESTART_THRESHOLD (60 * 1000) // Restart is considered "quick" if it happens within this time (1 minute)
#define SAFE_MODE_MIN_UPTIME (5 * 60 * 1000) // Minimum uptime in safe mode before allowing restarts (5 minutes)
#define SAFE_MODE_DISABLE_TIMEOUT (30 * 60 * 1000) // Automatically disable safe mode after this time if stable (30 minutes)
#define MIN_UPTIME_BEFORE_RESTART (30 * 1000) // Minimum uptime required before allowing any restart (30 seconds)

#define CRASH_RESET_TASK_NAME "crash_reset_task"
#define CRASH_RESET_TASK_STACK_SIZE (6 * 1024) // PLEASE: never put below this as even a single log will exceed 1024 kB easily.. We don't need to optimize so much :)
#define CRASH_RESET_TASK_PRIORITY 1 // This does not need to be high priority since it will only reset a counter and not do any heavy work

#define ELF_LOCATION ".pio/build/" TOSTRING(BUILD_ENV_NAME) "/firmware.elf" // Location of the ELF file for backtrace decoding
#define BACKTRACE_DECODE_CMD "xtensa-esp32-elf-addr2line -pfC -e " ELF_LOCATION " %s" // Command to decode backtrace addresses, where the %s will be replaced with the addresses
#define BACKTRACE_DECODE_CMD_SIZE 1024 // Size of the command buffer, should be enough for most backtraces

namespace CrashMonitor {
    void begin();
    // No need to stop anything here since once it executes at the beginning, there is no other use for this

    bool isLastResetDueToCrash();
    uint32_t getCrashCount();
    uint32_t getConsecutiveCrashCount();
    uint32_t getResetCount();
    uint32_t getConsecutiveResetCount();
    const char* getResetReasonString(esp_reset_reason_t reason);

    void clearConsecutiveCrashCount(); // Useful for avoiding crash loops (e.g. during factory reset)
    
    // Safe mode protection
    bool isInSafeMode(); // Returns true if device is in safe mode (rapid restart protection)
    bool canRestartNow(); // Returns true if enough time has passed to allow restart
    uint32_t getMinimumUptimeRemaining(); // Returns milliseconds remaining before restart is allowed
    void clearSafeModeCounters(); // Manually reset safe mode (useful for testing)
    
    // Core dump data access functions
    bool hasCoreDump();
    size_t getCoreDumpSize();
    bool getCoreDumpInfo(size_t* size, size_t* address);
    bool getCoreDumpChunk(uint8_t* buffer, size_t offset, size_t chunkSize, size_t* bytesRead);
    bool getFullCoreDump(uint8_t* buffer, size_t bufferSize, size_t* actualSize);
    void clearCoreDump();

    // Comprehensive crash info with backtrace
    bool getCoreDumpInfoJson(JsonDocument &doc);
    // Core dump chunk as base64
    bool getCoreDumpChunkJson(JsonDocument &doc, size_t offset, size_t chunkSize);

    // Task information
    TaskInfo getTaskInfo();
}