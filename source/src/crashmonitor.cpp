#include "crashmonitor.h"

namespace CrashMonitor
{
    // Static state variables
    static TaskHandle_t _crashResetTaskHandle = NULL;
    static TaskHandle_t _safeModeTaskHandle = NULL;

    // Private function declarations
    static void _handleCounters();
    static void _crashResetTask(void *parameter);
    static void _safeModeTask(void *parameter);
    static void _checkAndPrintCoreDump();
    static void _logCompleteCrashData();

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
            LOG_WARNING("Quick restart detected (#%lu): only %llu ms since last boot (threshold: %lu ms)", 
                _quickRestartCount, timeSinceLastBoot, QUICK_RESTART_THRESHOLD);
            
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
                LOG_DEBUG("Stable boot detected (%llu ms since last boot), resetting quick restart counter (was %lu)", 
                    timeSinceLastBoot, _quickRestartCount);
                _quickRestartCount = 0;
                // Note: We intentionally keep _consecutiveCrashCount and _consecutiveResetCount
                // Those are reset by the separate _crashResetTask after COUNTERS_RESET_TIMEOUT
            }
            
            // Exit safe mode if we had a stable restart (user fixed the issue)
            if (_safeModeActive && timeSinceLastBoot >= SAFE_MODE_MIN_UPTIME) {
                LOG_INFO("Exiting safe mode due to stable restart (%llu ms uptime)", timeSinceLastBoot);
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
            _checkAndPrintCoreDump();
        }

        // Regardless if last crash was due to a reset or crash, send this data if available (maybe previously we didn't have time to send it to the cloud)
        #ifdef HAS_SECRETS // MQTT is not available without secrets
        if (hasCoreDump()) Mqtt::requestCrashPublish();
        #endif

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
        // This handles the case where crashes/resets keep happening even after safe mode
        _handleCounters();

        // Create task to handle the crash reset
        LOG_DEBUG("Starting crash reset task with %d bytes stack in internal RAM", CRASH_RESET_TASK_STACK_SIZE);

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

    const char* getResetReasonString(esp_reset_reason_t reason) {
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
        esp_reset_reason_t resetReason = esp_reset_reason();
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
                
                // The key data for debugging - backtrace addresses
                if (summary->exc_bt_info.depth > 0) {
                    // Log all addresses in one line for easy copy-paste
                    char btAddresses[512] = "";
                    for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                        char addr[12];
                        snprintf(addr, sizeof(addr), "0x%08lx ", (uint32_t)summary->exc_bt_info.bt[i]);
                        strncat(btAddresses, addr, sizeof(btAddresses) - strlen(btAddresses) - 1);
                    }

                    // Ready-to-use command for debugging
                    char debugCommand[BACKTRACE_DECODE_CMD_SIZE];
                    snprintf(debugCommand, sizeof(debugCommand), BACKTRACE_DECODE_CMD, btAddresses);
                    LOG_WARNING("Command: %s", debugCommand);
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
        // Basic crash information
        esp_reset_reason_t resetReason = esp_reset_reason();
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
                    
                    // Backtrace addresses array
                    if (summary->exc_bt_info.depth > 0) {
                        JsonArray addresses = backtrace["addresses"].to<JsonArray>();
                        for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                            addresses.add((uint32_t)summary->exc_bt_info.bt[i]);
                        }
                        
                        // Command for debugging
                        char btAddresses[512] = "";
                        for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                            char addr[12];
                            snprintf(addr, sizeof(addr), "0x%08lx ", (uint32_t)summary->exc_bt_info.bt[i]);
                            strncat(btAddresses, addr, sizeof(btAddresses) - strlen(btAddresses) - 1);
                        }
                        
                        char debugCommand[600];
                        snprintf(debugCommand, sizeof(debugCommand), 
                                BACKTRACE_DECODE_CMD, 
                                btAddresses);
                        backtrace["debugCommand"] = debugCommand;
                    }
                } else {
                    doc["summaryError"] = err;
                }
                free(summary);
            }
        }

        return true;
    }

    bool getCoreDumpChunkJson(JsonDocument &doc, size_t offset, size_t chunkSize) {
        if (!hasCoreDump()) {
            doc["error"] = "No core dump available";
            return false;
        }

        // Get the raw size first
        size_t rawTotalSize = getCoreDumpSize();
        
        // Allocate buffer for the chunk to get the actual size after processing
        uint8_t* buffer = (uint8_t*)ps_malloc(chunkSize);
        if (!buffer) {
            doc["error"] = "Failed to allocate buffer";
            return false;
        }

        size_t bytesRead = 0;
        bool success = getCoreDumpChunk(buffer, offset, chunkSize, &bytesRead);
        
        if (success) {            
            // If this is the first chunk and we successfully read data, 
            // we can calculate the actual ELF size by checking how the chunk function processed it
            static bool elfSizeCalculated = false;
            
            if (!elfSizeCalculated && offset == 0 && bytesRead > 0) {
                // The getCoreDumpChunk function has found the ELF offset and adjusted the size
                // We can't directly access the static variables, so we'll estimate
                // For now, use a safer approach: when bytesRead < chunkSize at offset 0, 
                // it means we've hit the end, so totalSize = offset + bytesRead
                elfSizeCalculated = true;
            }
            
            doc["totalSize"] = rawTotalSize; // Keep reporting raw size for now
            doc["offset"] = offset;
            doc["requestedChunkSize"] = chunkSize;
            doc["actualChunkSize"] = bytesRead;
            
            // Fix hasMore calculation: if we read less than requested, we're at the end
            bool hasMore = (bytesRead == chunkSize); // If we got full chunk, there might be more
            if (hasMore) {
                // Double-check by trying to read one more byte at the next offset
                uint8_t testByte;
                size_t testBytesRead = 0;
                bool testSuccess = getCoreDumpChunk(&testByte, offset + bytesRead, 1, &testBytesRead);
                hasMore = (testSuccess && testBytesRead > 0);
            }
            
            doc["hasMore"] = hasMore;

            if (bytesRead > 0) {
                // Encode binary data as base64 using mbedtls
                size_t base64Length = 0;
                
                // First call to get required buffer size
                int32_t ret = mbedtls_base64_encode(NULL, 0, &base64Length, buffer, bytesRead);
                if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
                    uint8_t* base64Buffer = (uint8_t*)ps_malloc(base64Length + 1); // +1 for null terminator
                    if (base64Buffer) {
                        size_t actualLength = 0;
                        ret = mbedtls_base64_encode(base64Buffer, base64Length, &actualLength, buffer, bytesRead);
                        if (ret == 0) {
                            base64Buffer[actualLength] = '\0'; // Null terminate
                            doc["data"] = base64Buffer;
                            doc["encoding"] = "base64";
                        } else {
                            doc["error"] = "Base64 encoding failed";
                        }
                        free(base64Buffer);
                    } else {
                        doc["error"] = "Failed to allocate base64 buffer";
                    }
                } else {
                    doc["error"] = "Failed to calculate base64 buffer size";
                }
            } else {
                doc["error"] = "No data read";
            }
        } else {
            doc["totalSize"] = rawTotalSize;
            doc["offset"] = offset;
            doc["requestedChunkSize"] = chunkSize;
            doc["actualChunkSize"] = 0;
            doc["hasMore"] = false;
            doc["error"] = "Failed to read core dump chunk";
        }

        free(buffer);
        return success;
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_crashResetTaskHandle, CRASH_RESET_TASK_STACK_SIZE);
    }
}