// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "crashmonitor.h"
#include "duration_format.h"

#include <LittleFS.h>

#include "crash_archive_policy.h"

// miniz lives in the ESP32-S3 mask ROM (see esp32s3.rom.ld, "Group miniz"), so
// deflate costs no flash and adds no dependency. The header unconditionally
// defines MINIZ_NO_ZLIB_APIS and MINIZ_NO_ZLIB_COMPATIBLE_NAMES, so it leaks no
// compress()/crc32() macros into anything included after it.
#include "esp_rom_crc.h"
#include "mbedtls/sha256.h"
#include "miniz.h"

namespace CrashMonitor
{
    // Static state variables
    static TaskHandle_t _crashResetTaskHandle = NULL;
    static TaskHandle_t _safeModeTaskHandle = NULL;

    // gzip container (RFC 1952) around miniz's raw deflate output: a fixed
    // 10-byte header, then CRC-32 and the uncompressed length as little-endian
    // 32-bit values.
    #define GZIP_HEADER_SIZE 10
    #define GZIP_TRAILER_SIZE 8

    // A record on disk, as found by a directory scan. The timestamp is parsed
    // from the name purely to order the archive; identity is the crash id.
    struct ArchivedCrashEntry {
        char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
        uint64_t timestamp;
        size_t dumpBytes;
    };

    // Private function declarations
    static void _handleCounters();
    static void _crashResetTask(void *parameter);
    static void _safeModeTask(void *parameter);
    static void _checkAndPrintCoreDump();
    static void _logCompleteCrashData();
    static esp_reset_reason_t _getCrashResetReason();
    static size_t _gzipCompress(const uint8_t* source, size_t sourceSize, uint8_t* destination, size_t destinationSize);
    static void _buildArchivePath(const char* baseName, const char* suffix, char* path, size_t pathSize);
    static uint32_t _scanArchive(ArchivedCrashEntry* entries, uint32_t maxEntries);
    static bool _enforceArchiveRetention(size_t incomingBytes);
    static bool _archiveCoreDump();
    static void _archiveCoreDumpTask(void *parameter);
    static void _archivePendingCoreDump();

    // RTC memory variables (persist across restarts)
    RTC_NOINIT_ATTR uint32_t _magicWord = MAGIC_WORD_RTC; // Magic word to check RTC data validity
    
    // Tier 2: Crash/Reset tracking (long-term failure detection)
    RTC_NOINIT_ATTR uint32_t _resetCount = 0; // Total resets since first boot
    RTC_NOINIT_ATTR uint32_t _crashCount = 0; // Total crashes since first boot
    RTC_NOINIT_ATTR uint32_t _consecutiveCrashCount = 0; // Consecutive crashes (resets after 180s stable)
    RTC_NOINIT_ATTR uint32_t _consecutiveResetCount = 0; // Consecutive resets (resets after 180s stable)
    RTC_NOINIT_ATTR bool _rollbackTried = false; // Rollback attempted flag (prevents infinite rollback loops)
    
    // Tier 1: Quick restart tracking (rapid loop detection for safe mode)
    RTC_NOINIT_ATTR uint32_t _quickRestartCount = 0; // Number of quick consecutive restarts (resets when restart > 60s uptime)
    RTC_NOINIT_ATTR uint64_t _lastBootTimestamp = 0; // Unix timestamp (ms) when last boot occurred
    RTC_NOINIT_ATTR bool _safeModeActive = false; // Whether safe mode is currently active

    // Archive attempts already spent on the dump currently on the partition.
    // Bounds a pre-WiFi path that can panic: see MAX_CRASH_ARCHIVE_ATTEMPTS.
    RTC_NOINIT_ATTR uint32_t _archiveAttemptCount = 0;

    // Reset reason of the boot that crashed, frozen while a dump is pending.
    // esp_reset_reason() describes the CURRENT boot, so reading it when the dump
    // is finally archived or published reports whatever caused that later boot -
    // in production this surfaced as panics logged with "resetReason":"Software".
    // Archiving normally freezes the reason into the record the same boot, so
    // this only has to survive the case where the archive write itself fails and
    // the dump waits on the partition for another boot.
    RTC_NOINIT_ATTR uint32_t _crashResetReason = 0;
    RTC_NOINIT_ATTR bool _crashResetReasonValid = false;

    bool isLastResetDueToCrash() {
        // Only case in which it is not crash is when the reset reason is not
        // due to software reset (ESP.restart()), power on, or deep sleep (unused here)
        esp_reset_reason_t _hwResetReason = esp_reset_reason();

        return (uint32_t)_hwResetReason != ESP_RST_SW && 
                (uint32_t)_hwResetReason != ESP_RST_POWERON && 
                (uint32_t)_hwResetReason != ESP_RST_DEEPSLEEP;
    }

    void clearConsecutiveCrashCount() {
        _consecutiveCrashCount = 0;
    }

    void clearRollbackTried() {
        if (_rollbackTried) {
            LOG_DEBUG("Clearing rollback-tried flag, re-arming rollback eligibility");
            _rollbackTried = false;
        }
    }

    bool isInSafeMode() {
        return _safeModeActive;
    }

    bool canRestartNow() {
        uint64_t uptime = millis64();
        
        // If in safe mode, require minimum safe mode uptime
        if (_safeModeActive) return uptime >= SAFE_MODE_MIN_UPTIME;
        
        // Otherwise require minimum normal uptime
        return uptime >= MIN_UPTIME_BEFORE_RESTART;
    }

    uint32_t getMinimumUptimeRemaining() {
        uint64_t uptime = millis64();
        uint64_t requiredUptime = _safeModeActive ? SAFE_MODE_MIN_UPTIME : MIN_UPTIME_BEFORE_RESTART;
        
        if (uptime >= requiredUptime) return 0;
        return (uint32_t)(requiredUptime - uptime);
    }

    void clearSafeModeCounters() {
        LOG_INFO("Manually clearing safe mode counters and state");
        
        _quickRestartCount = 0;
        _safeModeActive = false;
        _lastBootTimestamp = 0;
        
        // Notify safe mode task to exit if it's running
        if (_safeModeTaskHandle != NULL) {
            xTaskNotifyGive(_safeModeTaskHandle);
            LOG_DEBUG("Notified safe mode task to exit");
        }
    }

    void begin() {
        LOG_DEBUG("Setting up crash monitor...");

        // Get current Unix timestamp in milliseconds from RTC
        uint64_t currentBootTimestamp = CustomTime::getUnixTimeMilliseconds();

        if (_magicWord != MAGIC_WORD_RTC) {
            LOG_DEBUG("RTC magic word is invalid, resetting crash counters");
            _magicWord = MAGIC_WORD_RTC;
            _resetCount = 0;
            _crashCount = 0;
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;
            _rollbackTried = false;
            _quickRestartCount = 0;
            _lastBootTimestamp = 0;
            _safeModeActive = false;
            _crashResetReason = 0;
            _crashResetReasonValid = false;
            _archiveAttemptCount = 0;
        }

        // === SAFE MODE: First line of defense against rapid restart loops ===
        // Quick restart detection: compare current boot time with last boot time
        bool isQuickRestart = false;
        uint64_t timeSinceLastBoot = 0;
        
        if (_lastBootTimestamp > 0 && currentBootTimestamp > _lastBootTimestamp) {
            timeSinceLastBoot = currentBootTimestamp - _lastBootTimestamp;
            isQuickRestart = (timeSinceLastBoot < QUICK_RESTART_THRESHOLD);
        }
        
        if (isQuickRestart) {
            _quickRestartCount++;
            char timeSinceLastBootHuman[DURATION_FORMAT_BUFFER_SIZE], thresholdHuman[DURATION_FORMAT_BUFFER_SIZE];
            DurationFormat::humanizeDuration(timeSinceLastBoot, timeSinceLastBootHuman, sizeof(timeSinceLastBootHuman));
            DurationFormat::humanizeDuration(QUICK_RESTART_THRESHOLD, thresholdHuman, sizeof(thresholdHuman));
            LOG_WARNING("Quick restart detected (#%lu): only %s since last boot (threshold: %s)",
                _quickRestartCount, timeSinceLastBootHuman, thresholdHuman);
            
            if (_quickRestartCount >= MAX_QUICK_RESTARTS) {
                _safeModeActive = true;
                LOG_FATAL(
                    "SAFE MODE ACTIVATED: %lu quick restarts detected (threshold: %lu). "
                    "Restart attempts blocked for %lu min. All systems operational.",
                    _quickRestartCount, (uint32_t)MAX_QUICK_RESTARTS, SAFE_MODE_MIN_UPTIME / (60 * 1000)
                );
            }
        } else {
            // Not a quick restart - reset quick restart counter (but keep crash/reset counters)
            if (_quickRestartCount > 0) {
                char timeSinceLastBootHuman[DURATION_FORMAT_BUFFER_SIZE];
                DurationFormat::humanizeDuration(timeSinceLastBoot, timeSinceLastBootHuman, sizeof(timeSinceLastBootHuman));
                LOG_DEBUG("Stable boot detected (%s since last boot), resetting quick restart counter (was %lu)",
                    timeSinceLastBootHuman, _quickRestartCount);
                _quickRestartCount = 0;
                // Note: We intentionally keep _consecutiveCrashCount and _consecutiveResetCount
                // Those are reset by the separate _crashResetTask after COUNTERS_RESET_TIMEOUT
            }
            
            // Exit safe mode if we had a stable restart (user fixed the issue)
            if (_safeModeActive && timeSinceLastBoot >= SAFE_MODE_MIN_UPTIME) {
                char timeSinceLastBootHuman[DURATION_FORMAT_BUFFER_SIZE];
                DurationFormat::humanizeDuration(timeSinceLastBoot, timeSinceLastBootHuman, sizeof(timeSinceLastBootHuman));
                LOG_INFO("Exiting safe mode due to stable restart (%s uptime)", timeSinceLastBootHuman);
                _safeModeActive = false;
            }
        }
        
        // Store current boot timestamp for next boot comparison
        _lastBootTimestamp = currentBootTimestamp;

        esp_core_dump_init();

        // If it was a crash, increment counter
        if (isLastResetDueToCrash()) {
            _crashCount++;
            _consecutiveCrashCount++;
            // The only moment esp_reset_reason() describes the crash rather than
            // some later boot, so freeze it before anything can defer the read
            _crashResetReason = (uint32_t)esp_reset_reason();
            _crashResetReasonValid = true;
            _checkAndPrintCoreDump();
        }

        // Increment reset count (always)
        _resetCount++;
        _consecutiveResetCount++;

        LOG_INFO(
            "Boot tracking - Crashes: %d (%d consecutive), Resets: %d (%d consecutive), Quick restarts: %lu",
            _crashCount, _consecutiveCrashCount, _resetCount, _consecutiveResetCount, _quickRestartCount
        );

        if (_safeModeActive) {
            LOG_INFO("Safe mode monitoring started - restarts blocked for %lu min, auto-clears after %lu min stable",
                        SAFE_MODE_MIN_UPTIME / (60 * 1000), SAFE_MODE_DISABLE_TIMEOUT / (60 * 1000));
            // Create safe mode monitoring task
            LOG_DEBUG("Starting safe mode task with %d bytes stack", CRASH_RESET_TASK_STACK_SIZE);
            
            BaseType_t result = xTaskCreate(
                _safeModeTask,
                "safe_mode_task",
                CRASH_RESET_TASK_STACK_SIZE,
                NULL,
                CRASH_RESET_TASK_PRIORITY,
                &_safeModeTaskHandle);
                
            if (result != pdPASS) {
                LOG_ERROR("Failed to create safe mode task");
                _safeModeTaskHandle = NULL;
            }
        }
        
        // === ROLLBACK/FACTORY RESET: Last resort for persistent failures ===
        // This handles the case where crashes/resets keep happening even after safe mode.
        // Deliberately ahead of the archive below: the archive is the riskiest
        // thing this function does and runs before WiFi, so the escape hatch has
        // to get its turn first. Behind it, a bug in the archive path would take
        // the recovery down with it on every boot.
        _handleCounters();

        // Move any dump off the partition and onto the filesystem. Runs on every
        // boot, not just the crashing one, so a dump left behind by a failed
        // archive is retried rather than lost to the next crash overwriting it.
        if (hasCoreDump()) _archivePendingCoreDump();

        // Publish whatever is archived, including records from earlier boots
        // that never reached the cloud
        if (!globalCommunityMode && hasArchivedCrash()) Mqtt::requestCrashPublish();

        // Create task to handle the crash reset
        LOG_DEBUG("Starting crash reset task with %d bytes stack", CRASH_RESET_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _crashResetTask, 
            CRASH_RESET_TASK_NAME, 
            CRASH_RESET_TASK_STACK_SIZE, 
            NULL, 
            CRASH_RESET_TASK_PRIORITY, 
            &_crashResetTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create crash reset task");
            _crashResetTaskHandle = NULL;
        }

        LOG_DEBUG("Crash monitor setup done");
    }

    // Task that resets consecutive crash/reset counters after stable operation
    static void _crashResetTask(void *parameter)
    {
        LOG_DEBUG("Starting crash/reset counter task (will reset after %lu seconds of stable operation)", COUNTERS_RESET_TIMEOUT / 1000);

        delay(COUNTERS_RESET_TIMEOUT);
        
        // After stable operation, reset the consecutive counters
        bool hadCounters = _consecutiveCrashCount > 0 || _consecutiveResetCount > 0;
        if (hadCounters) {
            LOG_INFO("Stable operation achieved (%lu seconds), resetting consecutive crash/reset counters", COUNTERS_RESET_TIMEOUT / 1000);
            LOG_DEBUG("Was: crashes=%d, resets=%d", _consecutiveCrashCount, _consecutiveResetCount);
        }

        _consecutiveCrashCount = 0;
        _consecutiveResetCount = 0;
        // Note: Don't reset _quickRestartCount here - it's managed separately based on restart timing

        // Stable operation proves this boot isn't the tail of a crash loop, so
        // rollback eligibility can be re-armed without waiting for a power cycle
        // (RTC_NOINIT_ATTR _rollbackTried otherwise stays stuck true across every
        // reset until power is physically cut - see issue found 2026-07-23).
        clearRollbackTried();

        _crashResetTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // Safe mode monitoring task - ensures minimum uptime and auto-recovery
    static void _safeModeTask(void *parameter)
    {
        LOG_DEBUG("Starting safe mode monitoring task...");
        
        // === Phase 1: Enforce minimum uptime (restart protection) ===
        LOG_INFO("Safe mode enforcing minimum uptime: %lu seconds", SAFE_MODE_MIN_UPTIME / 1000);
        uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAFE_MODE_MIN_UPTIME));
        
        if (notificationValue > 0) {
            // Task was stopped early (manual clear or system shutdown)
            LOG_DEBUG("Safe mode task stopped before minimum uptime reached");
            _safeModeTaskHandle = nullptr;
            vTaskDelete(NULL);
            return;
        }
        
        LOG_INFO("Safe mode minimum uptime reached (%lu seconds) - restarts now allowed", SAFE_MODE_MIN_UPTIME / 1000);
        
        // === Phase 2: Monitor for auto-disable timeout ===
        LOG_DEBUG("Monitoring for safe mode auto-disable (%lu minutes)", SAFE_MODE_DISABLE_TIMEOUT / (60 * 1000));
        uint64_t safeModeStartTime = millis64();
        
        while (millis64() - safeModeStartTime < SAFE_MODE_DISABLE_TIMEOUT) {
            // Wait 1 minute or until task stop notification
            notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60 * 1000));
            
            if (notificationValue > 0) {
                LOG_DEBUG("Safe mode manually disabled, exiting monitoring task");
                _safeModeTaskHandle = nullptr;
                vTaskDelete(NULL);
                return;
            }
            
            if (!_safeModeActive) {
                LOG_DEBUG("Safe mode was externally disabled, exiting monitoring task");
                _safeModeTaskHandle = nullptr;
                vTaskDelete(NULL);
                return;
            }
        }
        
        // === Phase 3: Auto-disable after stable operation ===
        if (_safeModeActive) {
            LOG_INFO("Safe mode auto-disabled after %lu minutes; safe mode will be disabled on next restart", SAFE_MODE_DISABLE_TIMEOUT / (60 * 1000));
            
            _safeModeActive = false;
            _quickRestartCount = 0;
        }

        _safeModeTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    uint32_t getCrashCount() {return _crashCount;}
    uint32_t getConsecutiveCrashCount() {return _consecutiveCrashCount;}
    uint32_t getResetCount() {return _resetCount;}
    uint32_t getConsecutiveResetCount() {return _consecutiveResetCount;}

    void _handleCounters() {
        // This is the LAST RESORT - only triggered after safe mode failed to resolve the issue
        // Safe mode should have already prevented most rapid restart loops
        if (_consecutiveCrashCount >= MAX_CRASH_COUNT || _consecutiveResetCount >= MAX_RESET_COUNT) {
            LOG_FATAL("Recovery mode triggered: crashes=%d, resets=%d", _consecutiveCrashCount, _consecutiveResetCount);
            LOG_FATAL("Safe mode insufficient, attempting rollback/factory reset");
            
            // Reset counters before recovery attempt to prevent infinite loops
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;
            _quickRestartCount = 0; // Also clear quick restart counter

            // Try rollback first (if available and not already tried)
            if (Update.canRollBack() && !_rollbackTried) {
                LOG_WARNING("Attempting firmware rollback...");
                if (Update.rollBack()) {
                    _rollbackTried = true;
                    LOG_INFO("Rollback successful, restarting");
                    ESP.restart(); // Immediate restart, bypass safe mode checks
                    return;
                }
                LOG_ERROR("Rollback failed: %s", Update.errorString());
            } else if (_rollbackTried) {
                LOG_WARNING("Rollback already attempted");
            } else {
                LOG_WARNING("No rollback partition available");
            }

            // Last resort: factory reset
            LOG_FATAL("Initiating factory reset (last recovery option)");
            _safeModeActive = false;
            setRestartSystem("Persistent failures - factory reset required", true);
        }
    }

    static void _checkAndPrintCoreDump() {
        LOG_DEBUG("Checking for core dump from previous crash...");

        // Check if a core dump image exists
        esp_err_t image_check = esp_core_dump_image_check();
        if (image_check != ESP_OK) {
            LOG_DEBUG("No core dump found (esp_err: %s)", esp_err_to_name(image_check));
            return;
        }

        LOG_INFO("Core dump found from previous crash, retrieving summary...");

        // Log only essential crash data for analysis
        _logCompleteCrashData();
    }

    // Reset reason of the boot that produced the pending dump. Falls back to the
    // live reason only when nothing has been frozen yet, which means no crash has
    // been seen since the RTC state was last cleared.
    static esp_reset_reason_t _getCrashResetReason() {
        if (_crashResetReasonValid) return (esp_reset_reason_t)_crashResetReason;
        return esp_reset_reason();
    }

    bool hasCoreDump() {
        return esp_core_dump_image_check() == ESP_OK;
    }

    size_t getCoreDumpSize() {
        size_t size = 0;
        size_t address = 0;
        if (esp_core_dump_image_get(&address, &size) == ESP_OK) {
            // Note: This returns the total size including ESP-IDF headers
            // The actual ELF size will be determined when we find the ELF offset
            return size;
        }
        return 0;
    }

    bool getCoreDumpInfo(size_t* size, size_t* address) {
        return esp_core_dump_image_get(address, size) == ESP_OK;
    }

    bool getCoreDumpChunk(uint8_t* buffer, size_t offset, size_t chunkSize, size_t* bytesRead) {
        if (!buffer || !bytesRead) {
            return false;
        }

        const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");
        if (!pt) {
            LOG_ERROR("Core dump partition not found");
            return false;
        }

        // Get total core dump size to validate offset
        size_t totalSize = getCoreDumpSize();
        if (totalSize == 0) {
            *bytesRead = 0;
            return false;
        }
        
        // Find ELF header offset (only for first chunk)
        static size_t elfOffset = 0;
        static bool elfOffsetFound = false;
        
        if (!elfOffsetFound && offset == 0) {
            // Search for ELF header in the first 1KB of the partition
            uint8_t *searchBuffer = (uint8_t*)ps_malloc(1024);
            if (!searchBuffer) {
                LOG_ERROR("Failed to allocate search buffer in PSRAM");
                *bytesRead = 0;
                return false;
            }
            
            esp_err_t err = esp_partition_read(pt, 0, searchBuffer, 1024);
            if (err == ESP_OK) {
                for (size_t i = 0; i < 1024 - 4; i++) {
                    if (searchBuffer[i] == 0x7f && searchBuffer[i+1] == 'E' && 
                        searchBuffer[i+2] == 'L' && searchBuffer[i+3] == 'F') {
                        elfOffset = i;
                        elfOffsetFound = true;
                        LOG_DEBUG("Found ELF header at offset %zu in core dump partition", elfOffset);
                        break;
                    }
                }
            }
            
            free(searchBuffer);
            
            if (!elfOffsetFound) {
                LOG_ERROR("Could not find ELF header in core dump partition");
                *bytesRead = 0;
                return false;
            }
        }
        
        // Adjust total size to account for ELF offset
        size_t adjustedTotalSize = totalSize - elfOffset;
        if (offset >= adjustedTotalSize) {
            *bytesRead = 0;
            return false;
        }

        // Adjust chunk size if it would exceed the available data
        size_t availableBytes = adjustedTotalSize - offset;
        size_t actualChunkSize = (chunkSize > availableBytes) ? availableBytes : chunkSize;

        // Read from partition with ELF offset
        esp_err_t err = esp_partition_read(pt, elfOffset + offset, buffer, actualChunkSize);
        if (err == ESP_OK) {
            *bytesRead = actualChunkSize;
            LOG_DEBUG("Read core dump chunk: offset=%zu, size=%zu from partition offset=%zu", 
                        offset, actualChunkSize, elfOffset + offset);
            return true;
        } else {
            LOG_ERROR("Failed to read core dump chunk at offset %zu (error: %d)", offset, err);
            *bytesRead = 0;
            return false;
        }
    }

    bool getFullCoreDump(uint8_t* buffer, size_t bufferSize, size_t* actualSize) {
        if (!buffer || !actualSize) {
            return false;
        }

        size_t totalSize = getCoreDumpSize();
        if (totalSize == 0) {
            *actualSize = 0;
            return false;
        }

        if (bufferSize < totalSize) {
            LOG_ERROR("Buffer too small for core dump: need %zu bytes, have %zu", totalSize, bufferSize);
            *actualSize = totalSize; // Return required size
            return false;
        }

        size_t bytesRead = 0;
        bool success = getCoreDumpChunk(buffer, 0, totalSize, &bytesRead);
        *actualSize = bytesRead;
        return success;
    }

    void clearCoreDump() {
        esp_core_dump_image_erase();
        LOG_DEBUG("Core dump cleared from flash");
    }

    static void _logCompleteCrashData() {
        LOG_WARNING("=== Crash Analysis ===");
        
        // Get reset reason and counters
        esp_reset_reason_t resetReason = _getCrashResetReason();
        LOG_WARNING("Reset reason: %s (%d) | crashes: %lu, consecutive: %lu",
                    getResetReasonString(resetReason), (int32_t)resetReason, 
                    _crashCount, _consecutiveCrashCount);
        
        // Get core dump summary
        esp_core_dump_summary_t *summary = (esp_core_dump_summary_t*)ps_malloc(sizeof(esp_core_dump_summary_t));
        if (summary) {
            esp_err_t err = esp_core_dump_get_summary(summary);
            if (err == ESP_OK) {                
                // Essential crash info
                LOG_WARNING("Task: %s | PC: 0x%08x | TCB: 0x%08x | SHA256 (partial): %s",
                            summary->exc_task, (uint32_t)summary->exc_pc, 
                            (uint32_t)summary->exc_tcb, summary->app_elf_sha256);
                
                // Backtrace info
                LOG_WARNING("Backtrace depth: %d | Corrupted: %s", 
                            summary->exc_bt_info.depth, summary->exc_bt_info.corrupted ? "yes" : "no");
                
                // The key data for debugging - backtrace addresses, on one line
                // for easy copy-paste. Just the addresses: pairing them with an
                // addr2line command naming a build-path ELF only ever produced
                // a line that was wrong everywhere except one machine.
                if (summary->exc_bt_info.depth > 0) {
                    char btAddresses[512] = "";
                    for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                        char addr[12];
                        snprintf(addr, sizeof(addr), "0x%08lx ", (uint32_t)summary->exc_bt_info.bt[i]);
                        strncat(btAddresses, addr, sizeof(btAddresses) - strlen(btAddresses) - 1);
                    }
                    LOG_WARNING("Backtrace: %s", btAddresses);
                }
                
                // Core dump availability info
                size_t dumpSize = 0;
                size_t dumpAddress = 0;
                if (esp_core_dump_image_get(&dumpAddress, &dumpSize) == ESP_OK) {
                    LOG_WARNING("Core dump available: %zu bytes at 0x%08x",
                                dumpSize, (uint32_t)dumpAddress);
                }
            } else {
                LOG_WARNING("Crash summary error: %d", err);
            }
            free(summary);
        }
        
        LOG_WARNING("=== End Crash Analysis ===");
    }

    bool getCoreDumpInfoJson(JsonDocument &doc) {
        // Basic crash information. The reason is the frozen one: this document is
        // built when the dump is archived, which can be a later boot than the
        // crash if an earlier archive attempt failed.
        esp_reset_reason_t resetReason = _getCrashResetReason();
        doc["resetReason"] = getResetReasonString(resetReason);
        doc["resetReasonCode"] = (int32_t)resetReason;
        doc["crashCount"] = _crashCount;
        doc["consecutiveCrashCount"] = _consecutiveCrashCount;
        doc["resetCount"] = _resetCount;
        doc["consecutiveResetCount"] = _consecutiveResetCount;
        
        bool hasDump = hasCoreDump();
        doc["hasCoreDump"] = hasDump;

        if (hasDump) {
            // Core dump size and address info
            size_t dumpSize = 0;
            size_t dumpAddress = 0;
            if (getCoreDumpInfo(&dumpSize, &dumpAddress)) {
                doc["coreDumpSize"] = dumpSize;
                doc["coreDumpAddress"] = dumpAddress;
            }

            // Get detailed crash summary if available
            esp_core_dump_summary_t *summary = (esp_core_dump_summary_t*)ps_malloc(sizeof(esp_core_dump_summary_t));
            if (summary) {
                esp_err_t err = esp_core_dump_get_summary(summary);
                if (err == ESP_OK) {
                    doc["taskName"] = summary->exc_task;
                    doc["programCounter"] = (uint32_t)summary->exc_pc;
                    doc["taskControlBlock"] = (uint32_t)summary->exc_tcb;
                    doc["appElfSha256"] = summary->app_elf_sha256;
                    
                    // Backtrace information
                    JsonObject backtrace = doc["backtrace"].to<JsonObject>();
                    backtrace["depth"] = summary->exc_bt_info.depth;
                    backtrace["corrupted"] = summary->exc_bt_info.corrupted;
                    
                    // Backtrace addresses array. Only the addresses: the
                    // addr2line invocation that used to ride along named an ELF
                    // by its build path, which is meaningless off the machine
                    // that produced it - and this record is built to be read
                    // elsewhere. Whoever holds the right ELF can form the
                    // command from these addresses.
                    if (summary->exc_bt_info.depth > 0) {
                        JsonArray addresses = backtrace["addresses"].to<JsonArray>();
                        for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                            addresses.add((uint32_t)summary->exc_bt_info.bt[i]);
                        }
                    }
                } else {
                    doc["summaryError"] = err;
                }
                free(summary);
            }
        }

        return true;
    }

    // On-flash crash archive
    // ======================

    static void _buildArchivePath(const char* baseName, const char* suffix, char* path, size_t pathSize) {
        snprintf(path, pathSize, "%s/%s%s", CRASH_ARCHIVE_DIR, baseName, suffix);
    }

    void buildArchivedCrashDumpPath(const char* baseName, char* path, size_t pathSize) {
        _buildArchivePath(baseName, CRASH_ARCHIVE_DUMP_SUFFIX, path, pathSize);
    }

    // Fills `entries` with the records on disk, oldest first. Records are found
    // by their dump file; the metadata sidecar is derived from the base name
    // rather than scanned for separately, so a stray .json cannot be mistaken
    // for a record.
    static uint32_t _scanArchive(ArchivedCrashEntry* entries, uint32_t maxEntries) {
        if (entries == nullptr || maxEntries == 0) return 0;
        if (!LittleFS.exists(CRASH_ARCHIVE_DIR)) return 0;

        File dir = LittleFS.open(CRASH_ARCHIVE_DIR);
        if (!dir) return 0;
        if (!dir.isDirectory()) {
            dir.close();
            return 0;
        }

        const size_t suffixLength = strlen(CRASH_ARCHIVE_DUMP_SUFFIX);
        uint32_t found = 0;
        uint32_t examined = 0;

        File entry = dir.openNextFile();
        while (entry && examined < CRASH_ARCHIVE_SCAN_MAX && found < maxEntries) {
            examined++;

            const char* name = entry.name(); // Leaf name, not the full path
            size_t nameLength = strlen(name);

            if (endsWith(name, CRASH_ARCHIVE_DUMP_SUFFIX) && nameLength > suffixLength) {
                size_t baseLength = nameLength - suffixLength;
                if (baseLength < CRASH_ARCHIVE_NAME_BUFFER_SIZE) {
                    ArchivedCrashEntry& slot = entries[found];
                    // %.*s takes its precision as an int specifically, not int32_t
                    snprintf(slot.baseName, sizeof(slot.baseName), "%.*s", (int)baseLength, name);
                    slot.timestamp = strtoull(slot.baseName, nullptr, 10);
                    slot.dumpBytes = entry.size();
                    found++;
                }
            }

            entry.close();
            entry = dir.openNextFile();
        }

        if (entry) entry.close();
        dir.close();

        // Insertion sort by timestamp. Bounded by CRASH_ARCHIVE_SCAN_MAX
        // entries, so the simple algorithm is the right one here. Records
        // written before the clock was set share a near-identical timestamp and
        // so have no meaningful order between them - they still all get
        // published, just not necessarily in the order they happened.
        for (uint32_t i = 1; i < found; i++) {
            ArchivedCrashEntry key = entries[i];
            uint32_t j = i;
            while (j > 0 && entries[j - 1].timestamp > key.timestamp) {
                entries[j] = entries[j - 1];
                j--;
            }
            entries[j] = key;
        }

        return found;
    }

    bool removeArchivedCrash(const char* baseName) {
        if (baseName == nullptr || baseName[0] == '\0') return false;

        char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];

        _buildArchivePath(baseName, CRASH_ARCHIVE_DUMP_SUFFIX, path, sizeof(path));
        bool removedDump = LittleFS.exists(path) ? LittleFS.remove(path) : true;

        _buildArchivePath(baseName, CRASH_ARCHIVE_META_SUFFIX, path, sizeof(path));
        bool removedMeta = LittleFS.exists(path) ? LittleFS.remove(path) : true;

        if (!removedDump || !removedMeta) {
            LOG_ERROR("Failed to fully remove archived crash %s (dump: %s, metadata: %s)",
                      baseName, removedDump ? "ok" : "failed", removedMeta ? "ok" : "failed");
            return false;
        }

        LOG_DEBUG("Removed archived crash %s", baseName);
        return true;
    }

    uint32_t clearArchivedCrashes() {
        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        uint32_t count = _scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX);

        uint32_t removed = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (removeArchivedCrash(entries[i].baseName)) removed++;
        }

        if (removed > 0) LOG_INFO("Cleared %lu archived crash record(s)", removed);
        return removed;
    }

    uint32_t getArchivedCrashCount() {
        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        return _scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX);
    }

    // Deliberately does not go through _scanArchive: this is called from the MQTT
    // task after every crash publish and from AsyncTCP request handlers, and a
    // scan would put a ~900 byte entry array on those stacks just to answer a
    // yes/no question. Stops at the first dump file it sees.
    bool hasArchivedCrash() {
        if (!LittleFS.exists(CRASH_ARCHIVE_DIR)) return false;

        File dir = LittleFS.open(CRASH_ARCHIVE_DIR);
        if (!dir) return false;
        if (!dir.isDirectory()) {
            dir.close();
            return false;
        }

        bool found = false;
        uint32_t examined = 0;

        File entry = dir.openNextFile();
        while (entry && !found && examined < CRASH_ARCHIVE_SCAN_MAX) {
            examined++;
            found = endsWith(entry.name(), CRASH_ARCHIVE_DUMP_SUFFIX);
            entry.close();
            if (!found) entry = dir.openNextFile();
        }

        if (entry) entry.close();
        dir.close();

        return found;
    }

    bool getOldestArchivedCrash(char* baseName, size_t baseNameSize) {
        if (baseName == nullptr || baseNameSize == 0) return false;

        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        if (_scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX) == 0) return false;

        snprintf(baseName, baseNameSize, "%s", entries[0].baseName);
        return true;
    }

    // The crash id is the part of the name after the ordering timestamp, so a
    // lookup compares that rather than the whole name: a caller holding a
    // published payload knows the id and has no reason to know the timestamp it
    // happens to be filed under.
    bool findArchivedCrashById(const char* crashId, char* baseName, size_t baseNameSize) {
        if (crashId == nullptr || crashId[0] == '\0') return false;
        if (baseName == nullptr || baseNameSize == 0) return false;

        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        uint32_t count = _scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX);

        for (uint32_t i = 0; i < count; i++) {
            const char* separator = strchr(entries[i].baseName, '_');
            if (separator == nullptr) continue;

            if (strcmp(separator + 1, crashId) == 0) {
                snprintf(baseName, baseNameSize, "%s", entries[i].baseName);
                return true;
            }
        }

        return false;
    }

    size_t getArchivedCrashDumpSize(const char* baseName) {
        if (baseName == nullptr) return 0;

        char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];
        _buildArchivePath(baseName, CRASH_ARCHIVE_DUMP_SUFFIX, path, sizeof(path));

        File file = LittleFS.open(path, FILE_READ);
        if (!file) return 0;

        size_t size = file.size();
        file.close();
        return size;
    }

    bool readArchivedCrashDump(const char* baseName, uint8_t* buffer, size_t bufferSize, size_t* actualSize) {
        if (baseName == nullptr || buffer == nullptr || actualSize == nullptr) return false;
        *actualSize = 0;

        char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];
        _buildArchivePath(baseName, CRASH_ARCHIVE_DUMP_SUFFIX, path, sizeof(path));

        File file = LittleFS.open(path, FILE_READ);
        if (!file) {
            LOG_ERROR("Archived crash dump not readable: %s", path);
            return false;
        }

        size_t size = file.size();
        if (size > bufferSize) {
            LOG_ERROR("Buffer too small for archived dump %s: need %zu bytes, have %zu", baseName, size, bufferSize);
            file.close();
            return false;
        }

        size_t read = file.read(buffer, size);
        file.close();

        if (read != size) {
            LOG_ERROR("Short read on archived dump %s: expected %zu bytes, got %zu", baseName, size, read);
            return false;
        }

        *actualSize = read;
        return true;
    }

    bool getArchivedCrashMetadata(const char* baseName, JsonDocument &doc) {
        if (baseName == nullptr) return false;

        char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];
        _buildArchivePath(baseName, CRASH_ARCHIVE_META_SUFFIX, path, sizeof(path));

        File file = LittleFS.open(path, FILE_READ);
        if (!file) {
            LOG_ERROR("Archived crash metadata not readable: %s", path);
            return false;
        }

        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            LOG_ERROR("Archived crash metadata %s is malformed: %s", baseName, error.c_str());
            return false;
        }

        return true;
    }

    bool listArchivedCrashes(JsonDocument &doc) {
        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        uint32_t count = _scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX);

        JsonArray crashes = doc["crashes"].to<JsonArray>();
        for (uint32_t i = 0; i < count; i++) {
            SpiRamAllocator allocator;
            JsonDocument metadata(&allocator);

            if (getArchivedCrashMetadata(entries[i].baseName, metadata)) {
                crashes.add(metadata);
            } else {
                // A dump with unreadable metadata is still worth surfacing, since
                // the dump itself can be fetched and analysed offline. Both
                // fields come from the name, which is all that is trustworthy
                // here - the record is otherwise exactly the one that failed.
                JsonObject partial = crashes.add<JsonObject>();
                const char* separator = strchr(entries[i].baseName, '_');
                if (separator != nullptr) partial["crashId"] = separator + 1;
                partial["timestamp"] = entries[i].timestamp;
                partial["coreDumpCompressedSize"] = entries[i].dumpBytes;
                partial["metadataError"] = "unreadable";
            }
        }

        doc["count"] = count;
        return true;
    }

    // Compresses `source` into a gzip container (RFC 1952) in `destination`,
    // returning the total gzip length or 0 on failure.
    //
    // miniz's low-level API never allocates - the ~164 kB tdefl_compressor is
    // caller-owned - so the state goes in PSRAM instead of the internal heap
    // that tdefl_compress_mem_to_mem() would have used. Flags are built by hand
    // because tdefl_create_comp_flags_from_zip_params() is not among the ROM
    // exports; omitting TDEFL_WRITE_ZLIB_HEADER yields the raw deflate stream a
    // gzip container wants.
    static size_t _gzipCompress(const uint8_t* source, size_t sourceSize, uint8_t* destination, size_t destinationSize) {
        if (source == nullptr || destination == nullptr) return 0;
        if (destinationSize <= GZIP_HEADER_SIZE + GZIP_TRAILER_SIZE) return 0;

        tdefl_compressor* compressor = (tdefl_compressor*)ps_malloc(sizeof(tdefl_compressor));
        if (compressor == nullptr) {
            LOG_ERROR("Failed to allocate %zu bytes in PSRAM for the gzip compressor", sizeof(tdefl_compressor));
            return 0;
        }

        size_t deflateSize = 0;
        tdefl_status status = tdefl_init(compressor, NULL, NULL, TDEFL_DEFAULT_MAX_PROBES);
        if (status == TDEFL_STATUS_OKAY) {
            size_t consumed = sourceSize;
            deflateSize = destinationSize - GZIP_HEADER_SIZE - GZIP_TRAILER_SIZE;

            status = tdefl_compress(compressor, source, &consumed,
                                    destination + GZIP_HEADER_SIZE, &deflateSize, TDEFL_FINISH);

            // TDEFL_STATUS_OKAY here means "call me again", which for a
            // single-shot compress means the output buffer was too small
            if (status != TDEFL_STATUS_DONE || consumed != sourceSize) {
                LOG_ERROR("Deflate failed (status %d, consumed %zu of %zu bytes)", (int32_t)status, consumed, sourceSize);
                deflateSize = 0;
            }
        } else {
            LOG_ERROR("Failed to initialise the gzip compressor (status %d)", (int32_t)status);
        }

        free(compressor);
        if (deflateSize == 0) return 0;

        // Header: magic, deflate, no flags, no mtime, no extra flags, unknown OS
        destination[0] = 0x1f;
        destination[1] = 0x8b;
        destination[2] = 0x08;
        destination[3] = 0x00;
        destination[4] = 0x00;
        destination[5] = 0x00;
        destination[6] = 0x00;
        destination[7] = 0x00;
        destination[8] = 0x00;
        destination[9] = 0xff;

        // gzip wants CRC-32 with init 0xFFFFFFFF and xorout 0xFFFFFFFF. Following
        // the ~-convention documented in esp_rom_crc.h, that is
        // ~esp_rom_crc32_le(~0xFFFFFFFF, ...) ^ 0xFFFFFFFF, which reduces to a
        // plain call with init 0.
        uint32_t crc = esp_rom_crc32_le(0, source, (uint32_t)sourceSize);
        uint32_t isize = (uint32_t)sourceSize;

        uint8_t* trailer = destination + GZIP_HEADER_SIZE + deflateSize;
        for (uint8_t i = 0; i < 4; i++) trailer[i] = (uint8_t)((crc >> (8 * i)) & 0xff);
        for (uint8_t i = 0; i < 4; i++) trailer[4 + i] = (uint8_t)((isize >> (8 * i)) & 0xff);

        return GZIP_HEADER_SIZE + deflateSize + GZIP_TRAILER_SIZE;
    }

    // Drops the oldest records until `incomingBytes` fits under both caps.
    // Returns false only when the incoming record could never fit.
    static bool _enforceArchiveRetention(size_t incomingBytes) {
        if (!CrashArchivePolicy::canStore(incomingBytes, CRASH_ARCHIVE_MAX_BYTES)) {
            LOG_ERROR("Crash record of %zu bytes exceeds the %d byte archive budget, refusing to store it",
                      incomingBytes, CRASH_ARCHIVE_MAX_BYTES);
            return false;
        }

        ArchivedCrashEntry entries[CRASH_ARCHIVE_SCAN_MAX];
        uint32_t count = _scanArchive(entries, CRASH_ARCHIVE_SCAN_MAX);

        size_t sizes[CRASH_ARCHIVE_SCAN_MAX];
        for (uint32_t i = 0; i < count; i++) sizes[i] = entries[i].dumpBytes;

        uint32_t toEvict = (uint32_t)CrashArchivePolicy::evictionCount(
            sizes, count, incomingBytes, CRASH_ARCHIVE_MAX_RECORDS, CRASH_ARCHIVE_MAX_BYTES);

        for (uint32_t i = 0; i < toEvict; i++) {
            LOG_INFO("Evicting oldest archived crash %s to make room (%lu of %lu)",
                     entries[i].baseName, i + 1, toEvict);
            removeArchivedCrash(entries[i].baseName);
        }

        return true;
    }

    // Copies the pending dump off the coredump partition and onto LittleFS as a
    // metadata/dump pair, then erases the partition. The partition is only
    // erased once both files are written and closed, so a failure anywhere here
    // leaves the dump where the next boot will retry it rather than losing it.
    static bool _archiveCoreDump() {
        size_t rawSize = getCoreDumpSize();
        if (rawSize == 0) {
            LOG_WARNING("Core dump image present but reports zero size, skipping archive");
            return false;
        }

        if (!LittleFS.exists(CRASH_ARCHIVE_DIR) && !LittleFS.mkdir(CRASH_ARCHIVE_DIR)) {
            LOG_ERROR("Failed to create %s, leaving the dump on the partition", CRASH_ARCHIVE_DIR);
            return false;
        }

        SpiRamAllocator allocator;
        JsonDocument metadata(&allocator);

        SpiRamAllocator infoAllocator;
        JsonDocument crashInfo(&infoAllocator);
        getCoreDumpInfoJson(crashInfo);

        uint8_t* raw = (uint8_t*)ps_malloc(rawSize);
        if (raw == nullptr) {
            LOG_ERROR("Failed to allocate %zu bytes in PSRAM for the core dump", rawSize);
            return false;
        }

        size_t elfSize = 0;
        if (!getFullCoreDump(raw, rawSize, &elfSize) || elfSize == 0) {
            LOG_ERROR("Failed to read the core dump off the partition");
            free(raw);
            return false;
        }

        // Identity comes from the dump, not from the clock: see
        // CRASH_ARCHIVE_ID_HEX_CHARS. Taken over the raw dump rather than the
        // gzip so the id describes the crash and not the compressor.
        uint8_t digest[32];
        char crashId[CRASH_ARCHIVE_ID_BUFFER_SIZE];
        if (mbedtls_sha256(raw, elfSize, digest, 0) != 0) {
            LOG_ERROR("Failed to hash the core dump, leaving it on the partition");
            free(raw);
            return false;
        }
        for (size_t i = 0; i < CRASH_ARCHIVE_ID_HEX_CHARS / 2; i++) {
            snprintf(crashId + (i * 2), 3, "%02x", digest[i]);
        }

        // The timestamp only orders the archive, so an unsynced clock costs
        // ordering rather than identity
        uint64_t timestamp = CustomTime::getUnixTimeMilliseconds();

        char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
        snprintf(baseName, sizeof(baseName), "%llu_%s", timestamp, crashId);

        // Worst case for incompressible input, per miniz's own bound
        size_t compressedCapacity = elfSize + (elfSize / 8) + GZIP_HEADER_SIZE + GZIP_TRAILER_SIZE + 64;
        uint8_t* compressed = (uint8_t*)ps_malloc(compressedCapacity);
        if (compressed == nullptr) {
            LOG_ERROR("Failed to allocate %zu bytes in PSRAM for the compressed dump", compressedCapacity);
            free(raw);
            return false;
        }

        size_t compressedSize = _gzipCompress(raw, elfSize, compressed, compressedCapacity);
        free(raw);

        if (compressedSize == 0) {
            LOG_ERROR("Failed to compress the core dump, leaving it on the partition");
            free(compressed);
            return false;
        }

        LOG_INFO("Core dump compressed: %zu bytes -> %zu bytes (%.1f%% of original)",
                 elfSize, compressedSize, (float)compressedSize * 100.0f / (float)elfSize);

        if (!_enforceArchiveRetention(compressedSize)) {
            free(compressed);
            return false;
        }

        metadata["crashId"] = crashId;
        // Unix ms at the moment the record was written. Separate from the id
        // because the clock is not yet set on a cold boot, so this can be near
        // epoch - honest, and useless as an identity, which is the point.
        metadata["timestamp"] = timestamp;
        // The build that crashed, which is what the cloud needs to fetch the
        // right ELF - not necessarily the build running when this is published
        metadata["firmwareVersion"] = FIRMWARE_BUILD_VERSION;
        metadata["coreDumpRawSize"] = elfSize;
        metadata["coreDumpCompressedSize"] = compressedSize;
        metadata["crashInfo"] = crashInfo;

        char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];

        // Metadata first: it is small, so a full filesystem fails here rather
        // than after a 12 kB dump write
        _buildArchivePath(baseName, CRASH_ARCHIVE_META_SUFFIX, path, sizeof(path));
        File metaFile = LittleFS.open(path, FILE_WRITE);
        if (!metaFile) {
            LOG_ERROR("Failed to open %s for writing", path);
            free(compressed);
            return false;
        }

        size_t metaWritten = serializeJson(metadata, metaFile);
        metaFile.close();

        if (metaWritten == 0) {
            LOG_ERROR("Failed to write crash metadata to %s", path);
            removeArchivedCrash(baseName);
            free(compressed);
            return false;
        }

        _buildArchivePath(baseName, CRASH_ARCHIVE_DUMP_SUFFIX, path, sizeof(path));
        File dumpFile = LittleFS.open(path, FILE_WRITE);
        if (!dumpFile) {
            LOG_ERROR("Failed to open %s for writing", path);
            removeArchivedCrash(baseName);
            free(compressed);
            return false;
        }

        size_t dumpWritten = dumpFile.write(compressed, compressedSize);
        dumpFile.close();
        free(compressed);

        if (dumpWritten != compressedSize) {
            LOG_ERROR("Short write on %s: expected %zu bytes, wrote %zu", path, compressedSize, dumpWritten);
            removeArchivedCrash(baseName);
            return false;
        }

        // Both files are down. Only now is it safe to give up the partition copy.
        clearCoreDump();

        // The reason is durable in the record, so the RTC copy has done its job
        _crashResetReasonValid = false;
        _crashResetReason = 0;

        LOG_INFO("Archived crash %s (%zu bytes on disk), core dump partition cleared", baseName, compressedSize);
        return true;
    }

    // Completion signal for the archive task. Both are file-static rather than
    // locals of the caller: on a timeout the task is left running, so anything
    // it still writes to must outlive the frame that started it.
    static bool _archiveTaskResult = false;
    static SemaphoreHandle_t _archiveTaskDone = NULL;

    static void _archiveCoreDumpTask(void *parameter) {
        (void)parameter;
        _archiveTaskResult = _archiveCoreDump();
        xSemaphoreGive(_archiveTaskDone);
        vTaskDelete(NULL);
    }

    // Moves the pending dump off the partition, on a stack that fits deflate and
    // under an attempt budget that guarantees the device still boots if it
    // cannot. Runs before WiFi, so anything that can panic or block here is a
    // brick risk: every failure path leaves the device able to continue.
    static void _archivePendingCoreDump() {
        if (_archiveAttemptCount >= MAX_CRASH_ARCHIVE_ATTEMPTS) {
            LOG_ERROR("Core dump could not be archived in %lu attempts, discarding it to break the loop",
                      _archiveAttemptCount);
            clearCoreDump();
            _archiveAttemptCount = 0;
            _crashResetReasonValid = false;
            _crashResetReason = 0;
            return;
        }

        // Spent before the work starts: a panic inside the archive must still
        // cost an attempt, otherwise the budget never runs down and the loop
        // this bounds is exactly the one that cannot be recovered from.
        _archiveAttemptCount++;

        if (_archiveTaskDone == NULL) _archiveTaskDone = xSemaphoreCreateBinary();
        if (_archiveTaskDone == NULL) {
            LOG_ERROR("Failed to create the crash archive semaphore, leaving the dump on the partition");
            return;
        }

        LOG_DEBUG("Starting crash archive task with %d bytes stack (attempt %lu of %d)",
                  CRASH_ARCHIVE_TASK_STACK_SIZE, _archiveAttemptCount, MAX_CRASH_ARCHIVE_ATTEMPTS);

        _archiveTaskResult = false;
        BaseType_t created = xTaskCreate(
            _archiveCoreDumpTask,
            CRASH_ARCHIVE_TASK_NAME,
            CRASH_ARCHIVE_TASK_STACK_SIZE,
            NULL,
            CRASH_ARCHIVE_TASK_PRIORITY,
            NULL);

        if (created != pdPASS) {
            LOG_ERROR("Failed to create the crash archive task, leaving the dump on the partition");
            return;
        }

        if (xSemaphoreTake(_archiveTaskDone, pdMS_TO_TICKS(CRASH_ARCHIVE_TASK_TIMEOUT)) != pdTRUE) {
            // Deliberately not deleted: it may be mid-write on LittleFS, and
            // killing it there would leave a half-written record behind. Boot
            // continues without it; the attempt has already been counted.
            LOG_ERROR("Crash archive did not finish within %d ms, continuing boot without it",
                      CRASH_ARCHIVE_TASK_TIMEOUT);
            return;
        }

        if (_archiveTaskResult) _archiveAttemptCount = 0;
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_crashResetTaskHandle, CRASH_RESET_TASK_STACK_SIZE);
    }
}