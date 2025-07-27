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
#include "globals.h"
#include "mqtt.h"
#include "structs.h"
#include "utils.h"

#define PREFERENCES_DATA_KEY "crashdata"
#define CRASH_SIGNATURE 0xDEADBEEF // A signature to identify whether we have or not data in RTC
#define MAX_BREADCRUMBS 8
#define WATCHDOG_TIMER (30 * 1000) // If the esp_task_wdt_reset() is not called within this time, the ESP32 panics - TODO: remove me, unused
#define PREFERENCES_FIRMWARE_STATUS_KEY "fw_status"
#define ROLLBACK_TESTING_TIMEOUT (60 * 1000) // Interval in which the firmware is being tested. If the ESP32 reboots unexpectedly, the firmware will be rolled back
#define MAX_CRASH_COUNT 5 // Maximum amount of consecutive crashes before triggering a rollback
#define CRASH_COUNTER_TIMEOUT (180 * 1000) // Timeout for the crash counter to reset

struct CrashData {
    unsigned int crashCount;             // Number of consecutive crashes
    unsigned int resetCount;             // Total number of resets
    unsigned int lastResetReason;        // Last reset reason from ESP32
    unsigned int signature;              // To verify RTC data validity (0xDEADBEEF)
    unsigned int lastUnixTime;          // Last Unix time when the crash occurred
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
    uint32_t getCrashCount();
    uint32_t getResetCount();
    void resetCrashCount();
    const char* getResetReasonString(esp_reset_reason_t reason);
    
    // Core dump functions
    bool hasCoreDump();
    void clearCoreDump();
    size_t getCoreDumpSize();

    const char* getFirmwareStatusString(FirmwareState status);
}