#include "crashmonitor.h"

CrashMonitor::CrashMonitor(AdvancedLogger& logger, CrashData& crashData) : _logger(logger), _crashData(crashData) {}
    

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
        case CustomModule::MULTIPLER: return "MULTIPLER";
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

void CrashMonitor::leaveBreadcrumb(CustomModule module, uint8_t id) {
    uint8_t idx = _crashData.currentIndex % 32;
    
    _crashData.breadcrumbs[idx].module = module;
    _crashData.breadcrumbs[idx].id = id;
    _crashData.breadcrumbs[idx].timestamp = millis();
    _crashData.breadcrumbs[idx].freeHeap = ESP.getFreeHeap();

    _crashData.currentIndex++;
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
    
    // Print last 32 breadcrumbs
    _logger.error("Last Breadcrumbs (most recent first):", "rtc::logCrashInfo");
    for (int i = 0; i < 32; i++) {
        uint8_t idx = (_crashData.currentIndex - 1 - i) % 32;
        const Breadcrumb& crumb = _crashData.breadcrumbs[idx];
        if (crumb.timestamp != 0) {  // Check if breadcrumb is valid
            _logger.error("[%d] Module: %s, ID: %d, Time: %d ms, Heap: %d bytes",
                "rtc::logCrashInfo",
                i,
                _getModuleName(crumb.module),
                crumb.id,
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
    for (int i = 0; i < 32; i++) {
        uint8_t idx = (_crashData.currentIndex - 1 - i) % 32;
        const Breadcrumb& crumb = _crashData.breadcrumbs[idx];
        if (crumb.timestamp != 0) {
            JsonObject _jsonObject = breadcrumbs.add<JsonObject>();
            _jsonObject["module"] = _getModuleName(crumb.module);
            _jsonObject["id"] = crumb.id;
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