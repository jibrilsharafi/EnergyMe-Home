#ifndef CRASHMONITOR_H
#define CRASHMONITOR_H

#include <esp_attr.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <rom/rtc.h>
#include <esp_task_wdt.h>
#include <esp_debug_helpers.h>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <Arduino.h>

#include "constants.h"
#include "structs.h"

class CrashMonitor {
public:
    CrashMonitor(AdvancedLogger& logger, CrashData& crashData);

    void begin();
    void leaveBreadcrumb(CustomModule module, uint8_t id);
    void logCrashInfo();
    void getJsonReport(JsonDocument& _jsonDocument);
    void saveJsonReport();
    
private:
    const size_t MAX_BACKTRACE = 32;

    const char* _getModuleName(CustomModule module);
    const char* _getResetReasonString(esp_reset_reason_t reason);
    
    CrashData& _crashData;
    AdvancedLogger& _logger;
};

#endif