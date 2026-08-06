// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

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

// On-flash crash archive
// ----------------------
// The coredump partition is a single-slot, fixed-size buffer that the next
// crash overwrites, so a dump left there is lost the moment the device crashes
// again or is asked for the dump after a reboot. Every record is therefore
// copied to LittleFS the boot it is detected and the partition is erased, which
// is also what lets the metadata be frozen while esp_reset_reason() still
// describes the crashing boot. Records live as a pair - readable metadata and
// the gzipped dump - and are removed together once published.
#define CRASH_ARCHIVE_DIR "/crashes"
#define CRASH_ARCHIVE_META_SUFFIX ".json"
#define CRASH_ARCHIVE_DUMP_SUFFIX ".bin.gz"
#define CRASH_ARCHIVE_MAX_RECORDS 10 // Oldest evicted first once reached
#define CRASH_ARCHIVE_MAX_BYTES (500 * 1024) // Ceiling on the whole directory
// Records examined per directory scan, above the cap so leftovers are still
// seen. Kept small because a scan puts an entry array on the caller's stack and
// runs from the MQTT task and from AsyncTCP request handlers.
#define CRASH_ARCHIVE_SCAN_MAX 16
// A record is named "<unix ms>_<crash id>", which separates the two jobs the
// name has to do. The timestamp orders the archive, and only that: it is minted
// in CrashMonitor::begin(), before WiFi and therefore before NTP, so on a cold
// boot it is epoch plus a second or two and is not unique. Identity is the
// crash id, a prefix of the SHA-256 of the dump itself, which is why a
// duplicate is impossible even when two cold-boot records share a timestamp.
//
// Hashing the content rather than drawing a random id also makes a retry
// idempotent: a record that failed partway through is rewritten under the same
// name on the next attempt instead of being duplicated beside itself. (Drawing
// a random id would also mean calling esp_random() before RF is up, where it is
// only pseudo-random.)
#define CRASH_ARCHIVE_ID_HEX_CHARS 16 // 64 bits of the digest, ample for the handful of records a device holds
#define CRASH_ARCHIVE_ID_BUFFER_SIZE (CRASH_ARCHIVE_ID_HEX_CHARS + 1)
// "<unix ms>_<crash id>" plus the longer of the two suffixes and a null.
// LittleFS caps a name at CONFIG_LITTLEFS_OBJ_NAME_LEN (64); this is 37 today.
#define CRASH_ARCHIVE_NAME_BUFFER_SIZE (20 + 1 + CRASH_ARCHIVE_ID_HEX_CHARS + sizeof(CRASH_ARCHIVE_DUMP_SUFFIX))
#define CRASH_ARCHIVE_PATH_BUFFER_SIZE (sizeof(CRASH_ARCHIVE_DIR) + CRASH_ARCHIVE_NAME_BUFFER_SIZE)

// Archiving runs on a task of its own rather than on loopTask. Deflate is a
// heavy stack user - miniz's Huffman optimisation alone puts a pair of
// 288-entry symbol tables on the caller's stack - and that does not fit in what
// setup() leaves of CONFIG_ARDUINO_LOOP_STACK_SIZE (8 kB). Doing it inline
// tripped the stack canary and panicked, which wrote a fresh dump for the next
// boot to trip over in exactly the same place: an unrecoverable boot loop that
// never reaches WiFi (see the v5 bench device, 2026-08-06).
#define CRASH_ARCHIVE_TASK_NAME "crash_archive_task"
#define CRASH_ARCHIVE_TASK_STACK_SIZE (16 * 1024)
#define CRASH_ARCHIVE_TASK_PRIORITY 1
#define CRASH_ARCHIVE_TASK_TIMEOUT (30 * 1000) // Bound on the wait, so a wedged archive cannot hold up boot

// A dump that cannot be archived must never be able to hold the device in a
// boot loop. Attempts are counted in RTC memory and spent before the work
// starts, so a panic inside the archive still burns one; once the budget is
// gone the dump is dropped and the device boots. Losing one dump is always
// preferable to losing the device.
#define MAX_CRASH_ARCHIVE_ATTEMPTS 3

namespace CrashMonitor {
    void begin();
    // No need to stop anything here since once it executes at the beginning, there is no other use for this

    bool isLastResetDueToCrash();
    uint32_t getCrashCount();
    uint32_t getConsecutiveCrashCount();
    uint32_t getResetCount();
    uint32_t getConsecutiveResetCount();

    void clearConsecutiveCrashCount(); // Useful for avoiding crash loops (e.g. during factory reset)
    void clearRollbackTried(); // Re-arm rollback eligibility: call after stable operation, and after every successful OTA flash (new partition contents = new image, deserves its own rollback chance)

    // Safe mode protection
    bool isInSafeMode(); // Returns true if device is in safe mode (rapid restart protection)
    bool canRestartNow(); // Returns true if enough time has passed to allow restart
    uint32_t getMinimumUptimeRemaining(); // Returns milliseconds remaining before restart is allowed
    void clearSafeModeCounters(); // Manually reset safe mode (useful for testing)
    
    // Core dump data access functions (coredump partition)
    bool hasCoreDump();
    size_t getCoreDumpSize();
    bool getCoreDumpInfo(size_t* size, size_t* address);
    bool getCoreDumpChunk(uint8_t* buffer, size_t offset, size_t chunkSize, size_t* bytesRead);
    bool getFullCoreDump(uint8_t* buffer, size_t bufferSize, size_t* actualSize);
    void clearCoreDump();

    // Comprehensive crash info with backtrace. Reports the reset reason frozen
    // at detection while a dump is pending, since esp_reset_reason() describes
    // the current boot and a dump may be read boots after the crash.
    bool getCoreDumpInfoJson(JsonDocument &doc);

    // On-flash crash archive
    // ----------------------
    // Records are identified by their base name, "<timestamp>_<crashId>".
    // The crashId alone is the identity carried in the published payload.
    bool hasArchivedCrash();
    uint32_t getArchivedCrashCount();
    // Base name of the oldest record, the one published next
    bool getOldestArchivedCrash(char* baseName, size_t baseNameSize);
    // Base name of the record whose crashId matches, for HTTP lookups
    bool findArchivedCrashById(const char* crashId, char* baseName, size_t baseNameSize);
    // Stored metadata: the published envelope minus the dump itself
    bool getArchivedCrashMetadata(const char* baseName, JsonDocument &doc);
    // Metadata of every record, oldest first, under "crashes"
    bool listArchivedCrashes(JsonDocument &doc);
    size_t getArchivedCrashDumpSize(const char* baseName);
    bool readArchivedCrashDump(const char* baseName, uint8_t* buffer, size_t bufferSize, size_t* actualSize);
    void buildArchivedCrashDumpPath(const char* baseName, char* path, size_t pathSize);
    bool removeArchivedCrash(const char* baseName);
    uint32_t clearArchivedCrashes();

    // Task information
    TaskInfo getTaskInfo();
}