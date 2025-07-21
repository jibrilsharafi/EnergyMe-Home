#include "crashmonitor.h"
#include <esp_core_dump.h>

static const char *TAG = "crashmonitor";

namespace CrashMonitor
{
    // Static state variables
    static bool _isFirmwareUpdate = false;
    static bool _isCrashCounterReset = false;
    static FirmwareState _firmwareStatus = FirmwareState::STABLE;

    // Private function declarations
    static void _initializeCrashData();
    static const char* _getResetReasonString(esp_reset_reason_t reason);
    static void _logCrashInfo();
    static void _saveCrashData();
    static void _handleCrashCounter();
    static void _handleFirmwareTesting();
    static void _exportCoreDump();
    static void _exportCoreDumpSerial();
    RTC_NOINIT_ATTR CrashData _crashData; 

    void _initializeCrashData() {
        memset(&_crashData, 0, sizeof(_crashData));
        _crashData.signature = CRASH_SIGNATURE;
        _crashData.crashCount = 0;
        _crashData.resetCount = 0;
        _crashData.lastResetReason = ESP_RST_UNKNOWN;
    }   
    
    bool isLastResetDueToCrash() {
        // Only case in which it is not crash is when the reset reason is not
        // due to software reset (ESP.restart()), power on, or deep sleep (unused here)
        return _crashData.lastResetReason != ESP_RST_SW && 
                _crashData.lastResetReason != ESP_RST_POWERON && 
                _crashData.lastResetReason != ESP_RST_DEEPSLEEP;
    }

    void begin() {
        // Initialize crash data if needed
        if (_crashData.signature != CRASH_SIGNATURE) {
            _initializeCrashData();
        }

        logger.debug("Setting up crash monitor...", TAG);

        // Get last reset reason
        esp_reset_reason_t _hwResetReason = esp_reset_reason();
        _crashData.lastResetReason = (uint32_t)_hwResetReason;

        // If it was a crash, increment counter
        if (isLastResetDueToCrash()) {
            _crashData.crashCount++;
            publishMqtt.crash = true;
            _logCrashInfo();
            _saveCrashData();
        }

        // Increment reset count
        _crashData.resetCount++;

        _handleCrashCounter();
        _handleFirmwareTesting();

        // Removed watchdog (wdt)

        logger.debug("Crash monitor setup done", TAG);
    }

    void reboot(const char* reason) {
        logger.warning("System reboot requested: %s", TAG, reason);
        delay(100); // Give time for log message to be sent

        ESP.restart();
    }

    bool checkIfCrashDataExists() {
        return _crashData.signature == CRASH_SIGNATURE;
    }

    void clearCrashData() {
        memset(&_crashData, 0, sizeof(_crashData));
    }

    bool getSavedCrashData(CrashData& crashDataSaved) {
        if (checkIfCrashDataExists()) {
            crashDataSaved = _crashData;
            return true;
        }
        return false;
    }

    uint32_t getCrashCount() {
        return _crashData.crashCount;
    }

    uint32_t getResetCount() {
        return _crashData.resetCount;
    }

    void resetCrashCount() {
        _crashData.crashCount = 0;
        _saveCrashData();
    }

    const char* _getResetReasonString(esp_reset_reason_t reason) {
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

    void _saveCrashData() {
        // Save crash data to persistent storage
        // Note: Assumes RTC memory persists through most resets
        // For full persistence, could use NVS/EEPROM instead
        logger.debug("Crash data saved to memory", TAG);
    }

    void _logCrashInfo() {
        const char* resetReasonStr = _getResetReasonString((esp_reset_reason_t)_crashData.lastResetReason);
        logger.error("System was restarted due to: %s", TAG, resetReasonStr);

        // Check if core dump is available
        if (esp_core_dump_image_check() == ESP_OK) {
            logger.info("Core dump available in flash", TAG);
            _exportCoreDump();
        }
    }

    void _exportCoreDump() {
        logger.info("Attempting to export core dump...", TAG);
        
        size_t core_dump_size = 0;
        if (esp_core_dump_image_get(nullptr, &core_dump_size) == ESP_OK && core_dump_size > 0) {
            logger.info("Core dump size: %d bytes", TAG, core_dump_size);
            
            // Set flag for MQTT export
            publishMqtt.crash = true;
        } else {
            logger.warning("Failed to get core dump info", TAG);
        }
    }

    bool getJsonReport(JsonDocument& _jsonDocument) {
        if (_crashData.signature != CRASH_SIGNATURE) return false;

        _jsonDocument["crashCount"] = _crashData.crashCount;
        _jsonDocument["lastResetReason"] = _getResetReasonString((esp_reset_reason_t)_crashData.lastResetReason);
        _jsonDocument["lastUnixTime"] = _crashData.lastUnixTime;
        _jsonDocument["resetCount"] = _crashData.resetCount;

        // Add core dump info if available
        bool coreDumpAvailable = hasCoreDump();
        _jsonDocument["hasCoreDump"] = coreDumpAvailable;
        if (coreDumpAvailable) {
            _jsonDocument["coreDumpSize"] = getCoreDumpSize();
        }

        return true;
    }

    void _handleCrashCounter() {
        logger.info("Crash count: %d, Reset count: %d", TAG, _crashData.crashCount, _crashData.resetCount);

        if (_crashData.crashCount >= MAX_CRASH_COUNT) {
            logger.error("Too many crashes! Clearing configuration...", TAG);
            // TODO: Clear configuration
        }
    }

    void crashCounterLoop() {
        if (_isCrashCounterReset) return;

        if (millis() > CRASH_COUNTER_TIMEOUT) {
            _isCrashCounterReset = true;
            logger.debug("Timeout reached. Resetting crash counter...", TAG);

            _crashData.crashCount = 0;
        }
    }

    void _handleFirmwareTesting() {
        if (publishMqtt.crash) {
            logger.info("Firmware testing: OK", TAG);
            publishMqtt.crash = false; // Clear the flag after handling
        }
    }

    void firmwareTestingLoop() {
        // This function is called in the loop to handle firmware testing
        // Currently empty, but can be extended for additional testing logic
    }

    bool setFirmwareStatus(FirmwareState status) { // TODO: finish fixing this
        _firmwareStatus = status;
        
        // Save to preferences if needed
        // TODO: implement reading/writing from nvs with preferences
        return true;
    }

    FirmwareState getFirmwareStatus() {
        return _firmwareStatus;
    }

    void getFirmwareStatusString(FirmwareState status, char* buffer, size_t bufferSize) {
        switch (status) {
            case FirmwareState::STABLE: snprintf(buffer, bufferSize, "Stable"); break;
            case FirmwareState::NEW_TO_TEST: snprintf(buffer, bufferSize, "New to test"); break;
            case FirmwareState::TESTING: snprintf(buffer, bufferSize, "Testing"); break;
            case FirmwareState::ROLLBACK: snprintf(buffer, bufferSize, "Rollback"); break;
            default: snprintf(buffer, bufferSize, "Unknown"); break;
        }
    }

    // Core dump utility functions
    bool hasCoreDump() {
        return esp_core_dump_image_check() == ESP_OK;
    }

    void clearCoreDump() {
        if (hasCoreDump()) {
            esp_core_dump_image_erase();
            logger.info("Core dump cleared from flash", TAG);
        }
    }

    size_t getCoreDumpSize() {
        size_t size = 0;
        if (esp_core_dump_image_get(nullptr, &size) == ESP_OK) {
            return size;
        } else {
            logger.error("Failed to get core dump size", TAG);
            return 0;
        }
    }
}