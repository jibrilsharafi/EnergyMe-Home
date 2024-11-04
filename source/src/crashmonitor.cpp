#include "crashmonitor.h"

CrashMonitor::CrashMonitor(AdvancedLogger& logger, CrashData& crashData) : _logger(logger), _crashData(crashData) {
        if (_crashData.signature != CRASH_DATA_INITIALIZED_FLAG) {
        memset(&_crashData, 0, sizeof(_crashData));
        _crashData.signature = CRASH_DATA_INITIALIZED_FLAG;
    }
}

void CrashMonitor::begin() { // FIXME: not working properly, the values are random
    // Get last reset reason
    esp_reset_reason_t hwResetReason = esp_reset_reason();
    _crashData.lastResetReason = (uint32_t)hwResetReason;

    // If it was a crash, increment counter
    if (hwResetReason != ESP_RST_SW && hwResetReason != ESP_RST_POWERON && hwResetReason != ESP_RST_DEEPSLEEP) {
        _crashData.crashCount++;

            // For watchdog resets, we can add specific handling
        if (hwResetReason == ESP_RST_TASK_WDT || hwResetReason == ESP_RST_WDT) {
            _crashData.lastExceptionCause = 0xDEAD; // Custom code for watchdog
            _crashData.lastFaultPC = nullptr;
            _crashData.lastFaultAddress = nullptr;
        }

        logCrashInfo();
    }

    // Increment reset count
    _crashData.resetCount++;

    // Enable watchdog
    esp_task_wdt_init(CRASH_MONITOR_WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(NULL);

    saveJsonReport();

    handleCrashCounter();
    handleFirmwareTesting();
}

const char* CrashMonitor::_getModuleName(CustomModule module) {
    switch(module) {
        case CustomModule::ADE7953: return "ADE7953";
        case CustomModule::CUSTOM_MQTT: return "CUSTOM_MQTT";
        case CustomModule::CUSTOM_SERVER: return "CUSTOM_SERVER";
        case CustomModule::CUSTOM_TIME: return "CUSTOM_TIME";
        case CustomModule::CUSTOM_WIFI: return "CUSTOM_WIFI";
        case CustomModule::LED: return "LED";
        case CustomModule::MAIN: return "MAIN";
        case CustomModule::MODBUS_TCP: return "MODBUS_TCP";
        case CustomModule::MQTT: return "MQTT";
        case CustomModule::MULTIPLEXER: return "MULTIPLER";
        case CustomModule::UTILS: return "UTILS";
        default: return "UNKNOWN";
    }
}

const char* CrashMonitor::_getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "Unknown reset";
        case ESP_RST_POWERON: return "Power-on reset";
        case ESP_RST_EXT: return "External pin reset";
        case ESP_RST_SW: return "Software reset";
        case ESP_RST_PANIC: return "Exception/Panic reset";
        case ESP_RST_INT_WDT: return "Interrupt watchdog reset";
        case ESP_RST_TASK_WDT: return "Task watchdog reset";
        case ESP_RST_WDT: return "Other watchdog reset";
        case ESP_RST_DEEPSLEEP: return "Deep sleep reset";
        case ESP_RST_BROWNOUT: return "Brownout reset";
        case ESP_RST_SDIO: return "SDIO reset";
        default: return "Unknown";
    }
}

void CrashMonitor::leaveBreadcrumb(const char* functionName, int lineNumber) {
    uint8_t idx = _crashData.currentIndex % MAX_BREADCRUMBS;

    _crashData.breadcrumbs[idx].functionName = functionName;
    _crashData.breadcrumbs[idx].lineNumber = lineNumber;
    _crashData.breadcrumbs[idx].timestamp = millis();
    _crashData.breadcrumbs[idx].freeHeap = ESP.getFreeHeap();

    _crashData.currentIndex = (_crashData.currentIndex + 1) % MAX_BREADCRUMBS;
    _crashData.lastUptime = millis();

    // Pat the watchdog
    esp_task_wdt_reset();
}

void CrashMonitor::logCrashInfo() {
    _logger.error("*** Crash Report ***", "rtc::logCrashInfo");
    _logger.error("Reset Count: %d", "rtc::logCrashInfo", _crashData.resetCount);
    _logger.error("Crash Count: %d", "rtc::logCrashInfo", _crashData.crashCount);
    _logger.error("Last Reset Reason: %s (%d)", "rtc::logCrashInfo", _getResetReasonString((esp_reset_reason_t)_crashData.lastResetReason), _crashData.lastResetReason);
    _logger.error("Last Exception Cause: 0x%x", "rtc::logCrashInfo", _crashData.lastExceptionCause);
    _logger.error("Last Fault PC: 0x%x", "rtc::logCrashInfo", (uint32_t)_crashData.lastFaultPC);
    _logger.error("Last Fault Address: 0x%x", "rtc::logCrashInfo", (uint32_t)_crashData.lastFaultAddress);
    _logger.error("Last Uptime: %d ms", "rtc::logCrashInfo", _crashData.lastUptime);
    
    _logger.error("Last Breadcrumbs (most recent first):", "rtc::logCrashInfo");
    for (int i = 0; i < MAX_BREADCRUMBS; i++) {
        uint8_t idx = (_crashData.currentIndex - 1 - i) % MAX_BREADCRUMBS;
        const Breadcrumb& crumb = _crashData.breadcrumbs[idx];
        if (crumb.timestamp != 0) {  // Check if breadcrumb is valid
            _logger.error("[%d] Function: %s, Line: %d, Time: %d ms, Heap: %d bytes",
                "rtc::logCrashInfo",
                i,
                crumb.functionName ? crumb.functionName : "Unknown",
                crumb.lineNumber,
                crumb.timestamp,
                crumb.freeHeap
            );
        }
    }
}

void CrashMonitor::getJsonReport(JsonDocument& _jsonDocument) {
    _jsonDocument["crashCount"] = _crashData.crashCount;
    _jsonDocument["lastResetReason"] = _getResetReasonString((esp_reset_reason_t)_crashData.lastResetReason);
    _jsonDocument["lastExceptionCause"] = (uint32_t)_crashData.lastExceptionCause;
    _jsonDocument["lastFaultPC"] = (uint32_t)_crashData.lastFaultPC;
    _jsonDocument["lastFaultAddress"] = (uint32_t)_crashData.lastFaultAddress;
    _jsonDocument["lastUptime"] = _crashData.lastUptime;
    _jsonDocument["resetCount"] = _crashData.resetCount;

    JsonArray breadcrumbs = _jsonDocument["breadcrumbs"].to<JsonArray>();
    _logger.debug("Looping through breadcrumbs", "rtc::getJsonReport");
    for (int i = 0; i < MAX_BREADCRUMBS; i++) {
        uint8_t idx = (_crashData.currentIndex - 1 - i) % MAX_BREADCRUMBS;
        const Breadcrumb& crumb = _crashData.breadcrumbs[idx];
        if (crumb.timestamp != 0) {
            JsonObject _jsonObject = breadcrumbs.add<JsonObject>();
            _jsonObject["function"] = crumb.functionName ? crumb.functionName : "Unknown";
            _jsonObject["line"] = crumb.lineNumber;
            _jsonObject["time"] = crumb.timestamp;
            _jsonObject["heap"] = crumb.freeHeap;
        }
    }
}

void CrashMonitor::saveJsonReport() {
    File file = SPIFFS.open(CRASH_DATA_JSON, "w");
    if (!file) {
        _logger.error("Failed to open crash report file for writing", "rtc::saveJsonReport");
        return;
    }

    JsonDocument _jsonDocument;
    getJsonReport(_jsonDocument);
    serializeJson(_jsonDocument, file);
    file.close();
}

void CrashMonitor::handleCrashCounter() {
    _logger.debug("Handling crash counter...", "CrashMonitor::handleCrashCounter");

    if (_crashData.crashCount >= MAX_CRASH_COUNT) {
        _logger.fatal("Crash counter reached the maximum allowed crashes. Rolling back to stable firmware...", "CrashMonitor::handleCrashCounter");

        if (!Update.rollBack()) {
            _logger.error("No firmware to rollback available. Keeping current firmware", "CrashMonitor::handleCrashCounter");
        }

        SPIFFS.format();

        ESP.restart();
    } else {
        _logger.debug("Crash counter incremented to %d", "CrashMonitor::handleCrashCounter", _crashData.crashCount);
    }
}

void CrashMonitor::crashCounterLoop() {
    if (isCrashCounterReset) return;

    if (millis() > CRASH_COUNTER_TIMEOUT) {
        isCrashCounterReset = true;
        _logger.debug("Timeout reached. Resetting crash counter...", "CrashMonitor::crashCounterLoop");

        _crashData.crashCount = 0;
    }
}

void CrashMonitor::handleFirmwareTesting() {
    _logger.debug("Checking if rollback is needed...", "CrashMonitor::handleFirmwareTesting");

    String _rollbackStatus;
    File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_READ);
    if (!_file) {
        _logger.error("Failed to open firmware rollback file", "CrashMonitor::handleFirmwareTesting");
        return;
    } else {
        _rollbackStatus = _file.readString();
        _file.close();
    }

    _logger.debug("Rollback status: %s", "CrashMonitor::handleFirmwareTesting", _rollbackStatus.c_str());
    if (_rollbackStatus == NEW_FIRMWARE_TO_BE_TESTED) {
        _logger.info("Testing new firmware", "CrashMonitor::handleFirmwareTesting");

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(NEW_FIRMWARE_TESTING);
            _file.close();
        }
        isFirmwareUpdate = true;
        return;
    } else if (_rollbackStatus == NEW_FIRMWARE_TESTING) {
        _logger.fatal("Testing new firmware failed. Rolling back to stable firmware", "CrashMonitor::handleFirmwareTesting");

        if (!Update.rollBack()) {
            _logger.error("No firmware to rollback available. Keeping current firmware", "CrashMonitor::handleFirmwareTesting");
        }

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(STABLE_FIRMWARE);
            _file.close();
        }

        setRestartEsp32("CrashMonitor::handleFirmwareTesting", "Testing new firmware failed. Rolling back to stable firmware");
    } else {
        _logger.debug("No rollback needed", "CrashMonitor::handleFirmwareTesting");
    }
}

void CrashMonitor::firmwareTestingLoop() {
    if (!isFirmwareUpdate) return;

    _logger.verbose("Checking if firmware has passed the testing period...", "CrashMonitor::firmwareTestingLoop");

    if (millis() > ROLLBACK_TESTING_TIMEOUT) {
        _logger.info("Testing period of new firmware has passed. Keeping current firmware", "CrashMonitor::firmwareTestingLoop");
        isFirmwareUpdate = false;

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(STABLE_FIRMWARE);
            _file.close();
        }
    }
}