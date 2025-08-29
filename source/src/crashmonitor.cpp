#include "crashmonitor.h"

namespace CrashMonitor
{
    // Static state variables
    static TaskHandle_t _crashResetTaskHandle = NULL;

    // Private function declarations
    static void _handleCounters();
    static void _crashResetTask(void *parameter);
    static void _checkAndPrintCoreDump();
    static void _logCompleteCrashData();

    RTC_NOINIT_ATTR uint32_t _magicWord = MAGIC_WORD_RTC; // Magic word to check RTC data validity
    RTC_NOINIT_ATTR uint32_t _resetCount = 0; // Reset counter in RTC memory
    RTC_NOINIT_ATTR uint32_t _crashCount = 0; // Crash counter in RTC memory
    RTC_NOINIT_ATTR uint32_t _consecutiveCrashCount = 0; // Consecutive crash counter in RTC memory
    RTC_NOINIT_ATTR uint32_t _consecutiveResetCount = 0; // Consecutive reset counter in RTC memory
    RTC_NOINIT_ATTR bool _rollbackTried = false; // Flag to indicate if rollback was attempted

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

    void begin() {
        LOG_DEBUG("Setting up crash monitor...");

        if (_magicWord != MAGIC_WORD_RTC) {
            LOG_DEBUG("RTC magic word is invalid, resetting crash counters");
            _magicWord = MAGIC_WORD_RTC;
            _resetCount = 0;
            _crashCount = 0;
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;
            _rollbackTried = false;
        }

        esp_core_dump_init();        

        // If it was a crash, increment counter
        if (isLastResetDueToCrash()) {
            _crashCount++;
            _consecutiveCrashCount++;
            _checkAndPrintCoreDump();
        }

        // Regardless if last crash was due to a reset or crash, send this data if available (maybe previously we didn't have time to send it to the cloud)
        if (hasCoreDump()) Mqtt::requestCrashPublish();

        // Increment reset count (always)
        _resetCount++;
        _consecutiveResetCount++;

        LOG_DEBUG(
            "Crash count: %d (consecutive: %d), Reset count: %d (consecutive: %d)", 
            _crashCount, _consecutiveCrashCount, _resetCount, _consecutiveResetCount
        );
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

    // Very simple task, no need for complex stuff
    static void _crashResetTask(void *parameter)
    {
        LOG_DEBUG("Starting crash reset task...");

        delay(COUNTERS_RESET_TIMEOUT);
        
        if (_consecutiveCrashCount > 0 || _consecutiveResetCount > 0){
            LOG_DEBUG("Consecutive crash and reset counters reset to 0");
        }
        _consecutiveCrashCount = 0;
        _consecutiveResetCount = 0;

        _crashResetTaskHandle = nullptr;
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
        if (_consecutiveCrashCount >= MAX_CRASH_COUNT || _consecutiveResetCount >= MAX_RESET_COUNT) {
            LOG_ERROR("The consecutive crash count limit (%d) or the reset count limit (%d) has been reached", MAX_CRASH_COUNT, MAX_RESET_COUNT);
            
            // Reset both counters before restart since we're either formatting or rolling back 
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;

            // If we can rollback, but most importantly, if we have not tried it yet (to avoid infinite rollback loops - IT CAN HAPPEN!)
            if (Update.canRollBack() && !_rollbackTried) {
                LOG_WARNING("Rolling back to previous firmware version");
                if (Update.rollBack()) {
                    _rollbackTried = true; // Indicate rollback was attempted

                    // Immediate reset to avoid any further issues
                    LOG_INFO("Rollback successful, restarting system");
                    ESP.restart();
                }
            }

            // If we got here, it means the rollback could not be executed, so we try at least to format everything
            LOG_FATAL("Could not rollback, performing factory reset");
            setRestartSystem("Consecutive crash/reset count limit reached", true);
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