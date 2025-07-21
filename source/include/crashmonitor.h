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

extern AdvancedLogger logger;

struct CrashData {
    unsigned int currentIndex;            // Current position in circular buffer
    unsigned int crashCount;             // Number of crashes detected
    unsigned int lastResetReason;        // Last reset reason from ESP32
    unsigned int resetCount;             // Number of resets
    unsigned long lastUnixTime;          // Last unix time before crash
    unsigned int signature;              // To verify RTC data validity
    // Since this struct will be used in an RTC_NOINIT_ATTR, we cannot initialize it in the constructor
};

namespace CrashMonitor {
    void begin();

    void crashCounterLoop();
    void firmwareTestingLoop();

    bool setFirmwareStatus(FirmwareState status);
    FirmwareState getFirmwareStatus();

    bool checkIfCrashDataExists();
    bool getSavedCrashData(CrashData& crashDataSaved);
    bool getJsonReport(JsonDocument& _jsonDocument);

    bool isLastResetDueToCrash();
    
    // Core dump functions
    bool hasCoreDump();
    void clearCoreDump();
    size_t getCoreDumpSize();

    void getFirmwareStatusString(FirmwareState status, char* buffer, size_t bufferSize);
}