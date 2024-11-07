#include "crashmonitor.h"

CrashMonitor::CrashMonitor(AdvancedLogger& logger) : _logger(logger) {
    if (crashData.signature != CRASH_SIGNATURE) {
        _initializeCrashData();
    }
}

void CrashMonitor::_initializeCrashData() {
    memset(&crashData, 0, sizeof(crashData));
    crashData.signature = CRASH_SIGNATURE;
    crashData.currentIndex = 0;
    crashData.crashCount = 0;
    crashData.resetCount = 0;
    crashData.lastUnixTime = 0;
    crashData.lastResetReason = 0;

    for (int i = 0; i < MAX_BREADCRUMBS; i++) {
        crashData.breadcrumbs[i].file = "";
        crashData.breadcrumbs[i].function = "";
        crashData.breadcrumbs[i].line = 0;
        crashData.breadcrumbs[i].micros = 0;
        crashData.breadcrumbs[i].freeHeap = 0;
        crashData.breadcrumbs[i].coreId = 0;
    }
}

bool CrashMonitor::isLastResetDueToCrash() {
    return crashData.lastResetReason != ESP_RST_SW && 
            crashData.lastResetReason != ESP_RST_POWERON && 
            crashData.lastResetReason != ESP_RST_DEEPSLEEP;
}

void CrashMonitor::begin() {
    _logger.debug("Setting up crash monitor...", "crashmonitor::begin");

    // Get last reset reason
    esp_reset_reason_t _hwResetReason = esp_reset_reason();
    crashData.lastResetReason = (uint32_t)_hwResetReason;

    // If it was a crash, increment counter
    if (isLastResetDueToCrash()) {
        crashData.crashCount++;
        publishMqtt.crash = true;
        _logCrashInfo();
        _saveCrashData();
    }

    // Increment reset count
    crashData.resetCount++;

    _handleCrashCounter();
    _handleFirmwareTesting();

    // Enable watchdog
    esp_task_wdt_init(WATCHDOG_TIMER, true);
    esp_task_wdt_add(NULL);

    _logger.debug("Crash monitor setup done", "crashmonitor::begin");
}

void CrashMonitor::_saveCrashData() {
    _logger.debug("Saving crash data...", "crashmonitor::_saveCrashData");

    Preferences _preferences;
    if (!_preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false)) {
        _logger.error("Failed to open preferences", "crashmonitor::_saveCrashData");
        _preferences.clear();
        _preferences.end();
        return;
    }
    size_t _len = _preferences.putBytes(PREFERENCES_DATA_KEY, &crashData, sizeof(crashData));
    if (_len != sizeof(crashData)) {
        _logger.error("Failed to save crash data", "crashmonitor::_saveCrashData");
        _preferences.clear();
        _preferences.end();
        return;
    }

    _preferences.end();

    _logger.debug("Crash data saved", "crashmonitor::_saveCrashData");
}

bool CrashMonitor::getSavedCrashData(CrashData& crashDataSaved) {
    Preferences _preferences;
    if (!_preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, true)) {
        _preferences.end();
        return false;
    }
    size_t dataSize = _preferences.getBytesLength(PREFERENCES_DATA_KEY);
    if (dataSize != sizeof(CrashData)) {
        _preferences.end();
        return false;
    }
    uint8_t buffer[dataSize];
    size_t bytesRead = _preferences.getBytes(PREFERENCES_DATA_KEY, buffer, dataSize);
    _preferences.end();
    if (bytesRead != dataSize) {
        return false;
    }
    memcpy(&crashDataSaved, buffer, dataSize);
    return true;
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

void CrashMonitor::leaveBreadcrumb(const char* filename, const char* functionName, unsigned int lineNumber, unsigned int coreId) {
    unsigned int idx = crashData.currentIndex % MAX_BREADCRUMBS;
    
    crashData.breadcrumbs[idx].file = filename;
    crashData.breadcrumbs[idx].function = functionName;
    crashData.breadcrumbs[idx].line = lineNumber;
    crashData.breadcrumbs[idx].micros = micros();
    crashData.breadcrumbs[idx].freeHeap = ESP.getFreeHeap();
    crashData.breadcrumbs[idx].coreId = coreId;

    crashData.currentIndex = (crashData.currentIndex + 1) % MAX_BREADCRUMBS;
    crashData.lastUnixTime = CustomTime::getUnixTime();

    esp_task_wdt_reset();
}

bool CrashMonitor::_isValidBreadcrumb(const Breadcrumb& crumb) {
    return crumb.line != 0;
}

void CrashMonitor::_logCrashInfo() {
    _logger.error("Crash Report | Timestamp: %s - Reason: %s - Reset Count %d - Crash Count: %d", "crashmonitor::_logCrashInfo", 
        CustomTime::timestampFromUnix(crashData.lastUnixTime, DEFAULT_TIMESTAMP_FORMAT).c_str(),
        _getResetReasonString((esp_reset_reason_t)crashData.lastResetReason), 
        crashData.resetCount, 
        crashData.crashCount
    );
    
    // Print only most recent breadcrumb
    const Breadcrumb& lastCrumb = crashData.breadcrumbs[(crashData.currentIndex - 1 + MAX_BREADCRUMBS) % MAX_BREADCRUMBS];
    if (_isValidBreadcrumb(lastCrumb)) {
        _logger.error("Last Function | %s:%s:%d (Core %d) Heap: %d bytes", "crashmonitor::_logCrashInfo",
            lastCrumb.file,
            lastCrumb.function,
            lastCrumb.line, 
            lastCrumb.coreId,
            lastCrumb.freeHeap
        );
    }
}

bool CrashMonitor::getJsonReport(JsonDocument& _jsonDocument, CrashData& crashDataReport) {
    if (crashDataReport.signature != CRASH_SIGNATURE) return false;

    _jsonDocument["crashCount"] = crashDataReport.crashCount;
    _jsonDocument["lastResetReason"] = _getResetReasonString((esp_reset_reason_t)crashDataReport.lastResetReason);
    _jsonDocument["lastUnixTime"] = crashDataReport.lastUnixTime;
    _jsonDocument["resetCount"] = crashDataReport.resetCount;

    JsonArray breadcrumbs = _jsonDocument["breadcrumbs"].to<JsonArray>();
    for (int i = 0; i < MAX_BREADCRUMBS; i++) {
        int idx = (crashDataReport.currentIndex - 1 - i + MAX_BREADCRUMBS) % MAX_BREADCRUMBS;
        const Breadcrumb& crumb = crashDataReport.breadcrumbs[idx];
        if (_isValidBreadcrumb(crumb)) {
            JsonObject _jsonObject = breadcrumbs.add<JsonObject>();
            _jsonObject["file"] = crumb.file;
            _jsonObject["function"] = crumb.function;
            _jsonObject["line"] = crumb.line;
            _jsonObject["micros"] = crumb.micros;
            _jsonObject["heap"] = crumb.freeHeap;
            _jsonObject["core"] = crumb.coreId;
        }
    }

    return true;
}

void CrashMonitor::_handleCrashCounter() {
    _logger.debug("Handling crash counter...", "crashmonitor::_handleCrashCounter");

    if (crashData.crashCount >= MAX_CRASH_COUNT) {
        _logger.fatal("Crash counter reached the maximum allowed crashes. Rolling back to stable firmware...", "crashmonitor::_handleCrashCounter");
        
        if (!Update.rollBack()) {
            _logger.error("No firmware to rollback available. Keeping current firmware", "crashmonitor::_handleCrashCounter");
        }

        SPIFFS.format();
        crashData.crashCount = 0;

        ESP.restart();
    } else {
        _logger.debug("Crash counter incremented to %d", "crashmonitor::_handleCrashCounter", crashData.crashCount);
    }
}

void CrashMonitor::crashCounterLoop() {
    if (_isCrashCounterReset) return;

    if (millis() > CRASH_COUNTER_TIMEOUT) {
        _isCrashCounterReset = true;
        _logger.debug("Timeout reached. Resetting crash counter...", "crashmonitor::crashCounterLoop");

        crashData.crashCount = 0;
    }
}

void CrashMonitor::_handleFirmwareTesting() {
    _logger.debug("Checking if rollback is needed...", "crashmonitor::_handleFirmwareTesting");

    FirmwareState _firmwareStatus = getFirmwareStatus();

    _logger.debug("Rollback status: %s", "crashmonitor::_handleFirmwareTesting", getFirmwareStatusString(_firmwareStatus));
    
    if (_firmwareStatus == NEW_TO_TEST) {
        _logger.info("Testing new firmware", "crashmonitor::_handleFirmwareTesting");

        setFirmwareStatus(TESTING);
        _isFirmwareUpdate = true;
        return;
    } else if (_firmwareStatus == TESTING) {
        _logger.fatal("Testing new firmware failed. Rolling back to stable firmware", "crashmonitor::_handleFirmwareTesting");

        if (!Update.rollBack()) {
            _logger.error("No firmware to rollback available. Keeping current firmware", "crashmonitor::_handleFirmwareTesting");
            return;
        }

        setFirmwareStatus(STABLE);

        setRestartEsp32("crashmonitor::_handleFirmwareTesting", "Testing new firmware failed. Rolling back to stable firmware");
    } else {
        _logger.debug("No rollback needed", "crashmonitor::_handleFirmwareTesting");
    }
}

void CrashMonitor::firmwareTestingLoop() {
    if (!_isFirmwareUpdate) return;

    if (millis() > ROLLBACK_TESTING_TIMEOUT) {
        _logger.info("Testing period of new firmware has passed. Keeping current firmware", "crashmonitor::firmwareTestingLoop");
        _isFirmwareUpdate = false;
        setFirmwareStatus(STABLE);
    }
}

bool CrashMonitor::setFirmwareStatus(FirmwareState status) {
    Preferences _preferences;
    if (!_preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false)) return false;
    
    bool success = _preferences.putInt(PREFERENCES_FIRMWARE_STATUS_KEY, static_cast<int>(status));
    _preferences.end();

    return success;
}

FirmwareState CrashMonitor::getFirmwareStatus() {
    Preferences _preferences;
    if (!_preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, true)) {
        _preferences.clear();
        _preferences.end();
        return FirmwareState::STABLE;
    }

    int status = _preferences.getInt(PREFERENCES_FIRMWARE_STATUS_KEY, static_cast<int>(FirmwareState::STABLE));
    _preferences.end();
    return static_cast<FirmwareState>(status);
}

String CrashMonitor::getFirmwareStatusString(FirmwareState status) {
    switch (status) {
        case FirmwareState::STABLE: return "Stable";
        case FirmwareState::NEW_TO_TEST: return "New to test";
        case FirmwareState::TESTING: return "Testing";
        case FirmwareState::ROLLBACK: return "Rollback";
        default: return "Unknown";
    }
}