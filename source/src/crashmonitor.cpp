#include "crashmonitor.h"
#include "esp_core_dump.h"
#include "mbedtls/base64.h"

static const char *TAG = "crashmonitor";

namespace CrashMonitor
{
    // Static state variables
    static TaskHandle_t _crashResetTaskHandle = NULL;

    // Private function declarations
    static void _handleCounters();
    static void _crashResetTask(void *parameter);
    static void _checkAndPrintCoreDump();
    static void _logCompleteCrashData();

    RTC_NOINIT_ATTR unsigned int _magicWord = MAGIC_WORD_RTC; // Magic word to check RTC data validity
    RTC_NOINIT_ATTR unsigned int _resetCount = 0; // Reset counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _crashCount = 0; // Crash counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _consecutiveCrashCount = 0; // Consecutive crash counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _consecutiveResetCount = 0; // Consecutive reset counter in RTC memory

    bool isLastResetDueToCrash() {
        // Only case in which it is not crash is when the reset reason is not
        // due to software reset (ESP.restart()), power on, or deep sleep (unused here)
        esp_reset_reason_t _hwResetReason = esp_reset_reason();

        return (unsigned long)_hwResetReason != ESP_RST_SW && 
                (unsigned long)_hwResetReason != ESP_RST_POWERON && 
                (unsigned long)_hwResetReason != ESP_RST_DEEPSLEEP;
    }

    void clearCrashCount() {
        _crashCount = 0;
        _consecutiveCrashCount = 0;
    }

    void begin() {
        logger.debug("Setting up crash monitor...", TAG);

        if (_magicWord != MAGIC_WORD_RTC) {
            logger.debug("RTC magic word is invalid, resetting crash counters", TAG);
            _magicWord = MAGIC_WORD_RTC;
            _resetCount = 0;
            _crashCount = 0;
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;
        }

        // If it was a crash, increment counter
        if (isLastResetDueToCrash()) {
            _crashCount++;
            _consecutiveCrashCount++;
        }

        // Increment reset count (always)
        _resetCount++;
        _consecutiveResetCount++;

        esp_core_dump_init();        
        _checkAndPrintCoreDump();

        logger.debug("Crash count: %d (consecutive: %d), Reset count: %d (consecutive: %d)", TAG, _crashCount, _consecutiveCrashCount, _resetCount, _consecutiveResetCount);
        _handleCounters();

        // Create task to handle the crash reset
        xTaskCreate(_crashResetTask, CRASH_RESET_TASK_NAME, CRASH_RESET_TASK_STACK_SIZE, NULL, CRASH_RESET_TASK_PRIORITY, &_crashResetTaskHandle);

        logger.debug("Crash monitor setup done", TAG);
    }

    // Very simple task, no need for complex stuff
    static void _crashResetTask(void *parameter)
    {
        logger.debug("Starting crash reset task...", TAG);

        delay(CRASH_COUNTER_TIMEOUT);
        
        if (_consecutiveCrashCount > 0 || _consecutiveResetCount > 0){
            logger.debug("Consecutive crash and reset counters reset to 0", TAG);
        }
        _consecutiveCrashCount = 0;
        _consecutiveResetCount = 0;
        
        vTaskDelete(NULL);
    }

    unsigned long getCrashCount() {
        return _crashCount;
    }

    unsigned long getResetCount() {
        return _resetCount;
    }

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
            logger.fatal("The consecutive crash count limit (%d) or the reset count limit (%d) has been reached", TAG, MAX_CRASH_COUNT, MAX_RESET_COUNT);

            if (Update.canRollBack()) {
                logger.fatal("Rolling back to previous firmware version", TAG);
                if (Update.rollBack()) {
                    // Reset both counters before restart since we're trying a different firmware
                    _consecutiveCrashCount = 0;
                    _consecutiveResetCount = 0;

                    // Immediate reset to avoid any further issues
                    logger.info("Rollback successful, restarting system", TAG);
                    ESP.restart();
                }
            }

            // If we got here, it means the rollback could not be executed, so we try at least to format everything
            logger.fatal("Could not rollback, performing factory reset", TAG);
            factoryReset();
        }
    }

    static void _checkAndPrintCoreDump() {
        logger.debug("Checking for core dump from previous crash...", TAG);
        
        // Check if a core dump image exists
        esp_err_t image_check = esp_core_dump_image_check();
        if (image_check != ESP_OK) {
            logger.debug("No core dump found (error: %d)", TAG, image_check);
            return;
        }

        logger.info("Core dump found from previous crash, retrieving summary...", TAG);
        
        // Log only essential crash data for analysis
        _logCompleteCrashData();
        
        // Skip verbose output - core dump remains available in flash for future transmission
        // Raw core dump can be accessed via esp_core_dump_image_get() when needed
        
        // Clear the core dump after reading (comment out to keep for transmission)
        // esp_core_dump_image_erase();
    }

    bool hasCoreDump() {
        return esp_core_dump_image_check() == ESP_OK;
    }

    size_t getCoreDumpSize() {
        size_t size = 0;
        size_t address = 0;
        if (esp_core_dump_image_get(&address, &size) == ESP_OK) {
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
            logger.error("Core dump partition not found", TAG);
            return false;
        }

        // Get total core dump size to validate offset
        size_t totalSize = getCoreDumpSize();
        if (totalSize == 0 || offset >= totalSize) {
            *bytesRead = 0;
            return false;
        }

        // Adjust chunk size if it would exceed the available data
        size_t availableBytes = totalSize - offset;
        size_t actualChunkSize = (chunkSize > availableBytes) ? availableBytes : chunkSize;

        esp_err_t err = esp_partition_read(pt, offset, buffer, actualChunkSize);
        if (err == ESP_OK) {
            *bytesRead = actualChunkSize;
            return true;
        } else {
            logger.error("Failed to read core dump chunk at offset %zu (error: %d)", TAG, offset, err);
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
            logger.error("Buffer too small for core dump: need %zu bytes, have %zu", TAG, totalSize, bufferSize);
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
        logger.debug("Core dump cleared from flash", TAG);
    }

    static void _logCompleteCrashData() {
        logger.warning("=== Crash Analysis ===", TAG);
        
        // Get reset reason and counters
        esp_reset_reason_t resetReason = esp_reset_reason();
        logger.warning("Reset reason: %s (%d) | crashes: %lu, consecutive: %lu", TAG,
                    getResetReasonString(resetReason), (int)resetReason, 
                    _crashCount, _consecutiveCrashCount);
        
        // Get core dump summary
        esp_core_dump_summary_t *summary = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
        if (summary) {
            esp_err_t err = esp_core_dump_get_summary(summary);
            if (err == ESP_OK) {                
                // Essential crash info
                logger.warning("Task: %s | PC: 0x%08x | TCB: 0x%08x | SHA256 (partial): %s", TAG,
                            summary->exc_task, (unsigned int)summary->exc_pc, 
                            (unsigned int)summary->exc_tcb, summary->app_elf_sha256);
                
                // Backtrace info
                logger.warning("Backtrace depth: %d | Corrupted: %s", TAG, 
                            summary->exc_bt_info.depth, summary->exc_bt_info.corrupted ? "yes" : "no");
                
                // The key data for debugging - backtrace addresses
                if (summary->exc_bt_info.depth > 0 && summary->exc_bt_info.bt != NULL) {
                    // Log all addresses in one line for easy copy-paste
                    char btAddresses[512] = "";
                    for (int i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                        char addr[12];
                        snprintf(addr, sizeof(addr), "0x%08x ", (unsigned int)summary->exc_bt_info.bt[i]);
                        strncat(btAddresses, addr, sizeof(btAddresses) - strlen(btAddresses) - 1);
                    }
                    logger.warning("Backtrace addresses: %s", TAG, btAddresses);

                    // Ready-to-use command for debugging
                    logger.warning("Command: xtensa-esp32-elf-addr2line -pfC -e .pio/build/esp32dev/firmware.elf %s", TAG, btAddresses);
                }
                
                // Core dump availability info
                size_t dumpSize = 0;
                size_t dumpAddress = 0;
                if (esp_core_dump_image_get(&dumpAddress, &dumpSize) == ESP_OK) {
                    logger.warning("Core dump available: %zu bytes at 0x%08x", TAG,
                                dumpSize, (unsigned int)dumpAddress);
                }
            } else {
                logger.warning("Crash summary error: %d", TAG, err);
            }
            free(summary);
        }
        
        logger.warning("=== End Crash Analysis ===", TAG);
    }

    bool getCoreDumpInfoJson(JsonDocument& doc) {
        doc.clear();

        // Basic crash information
        esp_reset_reason_t resetReason = esp_reset_reason();
        doc["resetReason"] = getResetReasonString(resetReason);
        doc["resetReasonCode"] = (int)resetReason;
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
            esp_core_dump_summary_t *summary = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
            if (summary) {
                esp_err_t err = esp_core_dump_get_summary(summary);
                if (err == ESP_OK) {
                    doc["taskName"] = summary->exc_task;
                    doc["programCounter"] = (unsigned int)summary->exc_pc;
                    doc["taskControlBlock"] = (unsigned int)summary->exc_tcb;
                    doc["appElfSha256"] = summary->app_elf_sha256;
                    
                    // Backtrace information
                    JsonObject backtrace = doc["backtrace"].to<JsonObject>();
                    backtrace["depth"] = summary->exc_bt_info.depth;
                    backtrace["corrupted"] = summary->exc_bt_info.corrupted;
                    
                    // Backtrace addresses array
                    if (summary->exc_bt_info.depth > 0 && summary->exc_bt_info.bt != NULL) {
                        JsonArray addresses = backtrace["addresses"].to<JsonArray>();
                        for (int i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                            addresses.add((unsigned int)summary->exc_bt_info.bt[i]);
                        }
                        
                        // Command for debugging
                        char btAddresses[512] = "";
                        for (int i = 0; i < summary->exc_bt_info.depth && i < 16; i++) {
                            char addr[12];
                            snprintf(addr, sizeof(addr), "0x%08x ", (unsigned int)summary->exc_bt_info.bt[i]);
                            strncat(btAddresses, addr, sizeof(btAddresses) - strlen(btAddresses) - 1);
                        }
                        
                        char debugCommand[600];
                        snprintf(debugCommand, sizeof(debugCommand), 
                                "xtensa-esp32-elf-addr2line -pfC -e .pio/build/esp32dev/firmware.elf %s", 
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

    bool getCoreDumpChunkJson(JsonDocument& doc, size_t offset, size_t chunkSize) {
        doc.clear();

        if (!hasCoreDump()) {
            doc["error"] = "No core dump available";
            return false;
        }

        size_t totalSize = getCoreDumpSize();
        doc["totalSize"] = totalSize;
        doc["offset"] = offset;
        doc["requestedChunkSize"] = chunkSize;

        if (offset >= totalSize) {
            doc["error"] = "Offset beyond core dump size";
            return false;
        }

        // Allocate buffer for the chunk
        uint8_t* buffer = (uint8_t*)malloc(chunkSize);
        if (!buffer) {
            doc["error"] = "Failed to allocate buffer";
            return false;
        }

        size_t bytesRead = 0;
        bool success = getCoreDumpChunk(buffer, offset, chunkSize, &bytesRead);
        
        if (success && bytesRead > 0) {
            doc["actualChunkSize"] = bytesRead;
            doc["hasMore"] = (offset + bytesRead) < totalSize;
            
            // Encode binary data as base64 using mbedtls
            size_t base64Length = 0;
            
            // First call to get required buffer size
            int ret = mbedtls_base64_encode(NULL, 0, &base64Length, buffer, bytesRead);
            if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
                char* base64Buffer = (char*)malloc(base64Length + 1); // +1 for null terminator
                if (base64Buffer) {
                    size_t actualLength = 0;
                    ret = mbedtls_base64_encode((unsigned char*)base64Buffer, base64Length, 
                                             &actualLength, buffer, bytesRead);
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
            doc["error"] = "Failed to read core dump chunk";
        }

        free(buffer);
        return success;
    }
}