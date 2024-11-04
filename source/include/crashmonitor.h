#pragma once

#include <esp_attr.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <rom/rtc.h>
#include <esp_task_wdt.h>
#include <esp_debug_helpers.h>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <Update.h>

#include "constants.h"
#include "structs.h"
#include "utils.h"

class CrashMonitor {
public:
    CrashMonitor(AdvancedLogger& logger, CrashData& crashData);

    void begin();
    void leaveBreadcrumb(CustomModule module, uint8_t id);
    void leaveBreadcrumb(const char* functionName, int lineNumber);
    void logCrashInfo();
    void getJsonReport(JsonDocument& _jsonDocument);
    void saveJsonReport();

    void handleCrashCounter();
    void crashCounterLoop();
    void handleFirmwareTesting();
    void firmwareTestingLoop();
    
private:
    const size_t MAX_BACKTRACE = MAX_BREADCRUMBS;

    const char* _getModuleName(CustomModule module);
    const char* _getResetReasonString(esp_reset_reason_t reason);
    
    CrashData _crashData;
    AdvancedLogger& _logger;

    bool isFirmwareUpdate = false;
    bool isCrashCounterReset = false;
};

#define TRACE crashMonitor.leaveBreadcrumb(__func__, __LINE__);