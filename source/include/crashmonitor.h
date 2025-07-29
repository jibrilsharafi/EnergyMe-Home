#pragma once

#include <esp_attr.h>
#include <esp_system.h>
#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Update.h>

#include "constants.h"
#include "globals.h"
#include "mqtt.h"
#include "structs.h"
#include "utils.h"

#define MAGIC_WORD_RTC 0xDEADBEEF // This is crucial to ensure that the RTC data has sensible values or it is just some garbage after reboot
#define MAX_CRASH_COUNT 10 // Maximum amount of consecutive crashes before triggering a rollback
#define CRASH_COUNTER_TIMEOUT (180 * 1000) // Timeout for the crash counter to reset
#define CRASH_RESET_TASK_NAME "crash_reset_task"
#define CRASH_RESET_TASK_STACK_SIZE 4096 // PLEASE: never put below this as even a single log will exceed 1024 kB easily.. We don't need to optimize so much :)
#define CRASH_RESET_TASK_PRIORITY 1

namespace CrashMonitor {
    void begin();

    bool isLastResetDueToCrash();
    unsigned long getCrashCount();
    unsigned long getResetCount();
    const char* getResetReasonString(esp_reset_reason_t reason);
}