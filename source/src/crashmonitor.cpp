#include "crashmonitor.h"

static const char *TAG = "crashmonitor";

namespace CrashMonitor
{
    // Static state variables
    static TaskHandle_t _crashResetTaskHandle = NULL;

    // Private function declarations
    static void _handleCounters();
    static void _crashResetTask(void *parameter);

    RTC_NOINIT_ATTR unsigned int _magicWord = MAGIC_WORD_RTC; // Magic word to check RTC data validity
    RTC_NOINIT_ATTR unsigned int _resetCount = 0; // Reset counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _crashCount = 0; // Crash counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _consecutiveCrashCount = 0; // Consecutive crash counter in RTC memory
    RTC_NOINIT_ATTR unsigned int _consecutiveResetCount = 0; // Consecutive reset counter in RTC memory

    bool isLastResetDueToCrash() {
        // Only case in which it is not crash is when the reset reason is not
        // due to software reset (ESP.restart()), power on, or deep sleep (unused here)
        esp_reset_reason_t _hwResetReason = esp_reset_reason();

        return (unsigned long)_hwResetReason != ESP_RST_SW && 
                (unsigned long)_hwResetReason != ESP_RST_POWERON && 
                (unsigned long)_hwResetReason != ESP_RST_DEEPSLEEP;
    }

    void begin() {
        logger.debug("Setting up crash monitor...", TAG);

        if (_magicWord != MAGIC_WORD_RTC) {
            logger.debug("RTC magic word is invalid, resetting crash counters", TAG);
            _magicWord = MAGIC_WORD_RTC;
            _resetCount = 0;
            _crashCount = 0;
            _consecutiveCrashCount = 0;
            _consecutiveResetCount = 0;
        }

        // If it was a crash, increment counter
        if (isLastResetDueToCrash()) {
            _crashCount++;
            _consecutiveCrashCount++;
        }

        // Increment reset count (always)
        _resetCount++;
        _consecutiveResetCount++;

        logger.debug("Crash count: %d (consecutive: %d), Reset count: %d (consecutive: %d)", TAG, _crashCount, _consecutiveCrashCount, _resetCount, _consecutiveResetCount);
        _handleCounters();

        // Create task to handle the crash reset
        xTaskCreate(_crashResetTask, CRASH_RESET_TASK_NAME, CRASH_RESET_TASK_STACK_SIZE, NULL, CRASH_RESET_TASK_PRIORITY, &_crashResetTaskHandle);

        logger.debug("Crash monitor setup done", TAG);
    }

    static void _crashResetTask(void *parameter)
    {
        logger.debug("Starting crash reset task...", TAG);
        delay(CRASH_COUNTER_TIMEOUT);
        if (_consecutiveCrashCount > 0 || _consecutiveResetCount > 0){
            logger.info("Consecutive crash and reset counters reset to 0", TAG);
        }
        _consecutiveCrashCount = 0;
        _consecutiveResetCount = 0;
        vTaskDelete(NULL);
    }

    unsigned long getCrashCount() {
        return _crashCount;
    }

    unsigned long getResetCount() {
        return _resetCount;
    }

    const char* getResetReasonString(esp_reset_reason_t reason) {
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

    void _handleCounters() {
        if (_consecutiveCrashCount >= MAX_CRASH_COUNT || _consecutiveResetCount >= MAX_RESET_COUNT) {
            logger.fatal("The consecutive crash count limit (%d) or the reset count limit (%d) has been reached", TAG, MAX_CRASH_COUNT, MAX_RESET_COUNT);

            if (Update.canRollBack()) {
                logger.fatal("Rolling back to previous firmware version", TAG);
                if (Update.rollBack()) {
                    // Reset both counters before restart since we're trying a different firmware
                    _consecutiveCrashCount = 0;
                    _consecutiveResetCount = 0;

                    // Immediate reset to avoid any further issues
                    logger.info("Rollback successful, restarting system", TAG);
                    ESP.restart();
                }
            }

            // If we got here, it means the rollback could not be executed, so we try at least to format everything
            logger.fatal("Could not rollback, performing factory reset", TAG);
            factoryReset();
        }
    }
}