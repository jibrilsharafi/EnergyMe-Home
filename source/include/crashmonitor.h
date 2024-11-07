#pragma once

#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <Update.h>
#include <Preferences.h>

#include "constants.h"
#include "structs.h"
#include "utils.h"

extern CrashData crashData;

class CrashMonitor {
public:
    CrashMonitor(AdvancedLogger& logger);

    void begin();
    void leaveBreadcrumb(const char* filename, const char* functionName, unsigned int lineNumber, unsigned int coreId);

    void crashCounterLoop();
    void firmwareTestingLoop();

    static bool setFirmwareStatus(FirmwareState status);
    static FirmwareState getFirmwareStatus();

    static bool getJsonReport(JsonDocument& _jsonDocument, CrashData& crashDataReport);
    static bool getSavedCrashData(CrashData& crashDataSaved);

    static bool isLastResetDueToCrash();

    static String getFirmwareStatusString(FirmwareState status);
    
private:
    static const char* _getResetReasonString(esp_reset_reason_t reason);
    
    void _logCrashInfo();
    void _saveCrashData();
    
    void _handleCrashCounter();
    void _handleFirmwareTesting();
    
    void _initializeCrashData();
    static bool _isValidBreadcrumb(const Breadcrumb& crumb);
    
    bool _isFirmwareUpdate = false;
    bool _isCrashCounterReset = false;

    AdvancedLogger& _logger;
};

extern CrashMonitor crashMonitor;
#define TRACE crashMonitor.leaveBreadcrumb(pathToFileName(__FILE__), __FUNCTION__, __LINE__, xPortGetCoreID());