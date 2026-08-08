// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "led.h"
#include "taskprofiler.h"

namespace Led
{
    static TaskHeartbeat _heartbeat;

    static uint8_t _redPin = INVALID_PIN;
    static uint8_t _greenPin = INVALID_PIN;
    static uint8_t _bluePin = INVALID_PIN;

    // Written by the web/shadow/button tasks, read every tick by the render task.
    // A byte cannot tear on Xtensa; volatile is here so the read is not hoisted out
    // of the render loop.
    static volatile uint8_t _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;

    static TaskHandle_t _ledTaskHandle = nullptr;
    static bool _ledTaskShouldRun = false;

    // Written by any task, read by the render task. Created in begin() and never
    // deleted: teardown paths (_restartTask, _factoryReset) still set indications
    // while services are being stopped, and they must not fault.
    static LedState::Table _table;
    static SemaphoreHandle_t _tableMutex = nullptr;

    static void _setHardwareColor(const LedState::Rgb &color);
    static void _ledTask(void *parameter);
    static bool _loadConfiguration();
    static void _saveConfiguration();

    static LedState::Layer _layerForPriority(LedPriority priority)
    {
        if (priority <= PRIO_NORMAL) { return LedState::Layer::STATUS; }
        if (priority <= PRIO_MEDIUM) { return LedState::Layer::NETWORK; }
        if (priority <= PRIO_URGENT) { return LedState::Layer::ALERT; }
        return LedState::Layer::CRITICAL;
    }

    void begin(uint8_t redPin, uint8_t greenPin, uint8_t bluePin)
    {
        if (_ledTaskHandle != nullptr) { return; }

        _redPin = redPin;
        _greenPin = greenPin;
        _bluePin = bluePin;

        pinMode(_redPin, OUTPUT);
        pinMode(_greenPin, OUTPUT);
        pinMode(_bluePin, OUTPUT);

        ledcAttach(_redPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_greenPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_bluePin, LED_FREQUENCY, LED_RESOLUTION);

        _loadConfiguration();

        if (!createMutexIfNeeded(&_tableMutex)) { return; }

        LedState::releaseAll(_table);
        _setHardwareColor(LedState::Colors::OFF); // Deterministic pins before the first render

        LOG_DEBUG("Starting LED task with %d bytes stack", LED_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _ledTask,
            LED_TASK_NAME,
            LED_TASK_STACK_SIZE,
            nullptr,
            LED_TASK_PRIORITY,
            &_ledTaskHandle);

        if (result != pdPASS)
        {
            LOG_ERROR("Failed to create LED task");
            _ledTaskHandle = nullptr;
        }
    }

    static void _ledTask(void *parameter)
    {
        _ledTaskShouldRun = true;
        while (_ledTaskShouldRun)
        {
            TASK_HEARTBEAT(_heartbeat);

            // Advance and copy under the mutex, drive the pins outside it.
            if (acquireMutex(&_tableMutex, LED_MUTEX_TIMEOUT_MS))
            {
                const LedState::Frame frame = LedState::step(_table, millis64(), _brightness);
                releaseMutex(&_tableMutex);

                _setHardwareColor(frame.output);
            }

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_TASK_DELAY_MS)) > 0)
            {
                _ledTaskShouldRun = false;
            }
        }

        _ledTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    bool _loadConfiguration()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, true))
        {
            _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;
            _saveConfiguration();
            return false;
        }

        const uint8_t stored = preferences.getUChar(PREFERENCES_BRIGHTNESS_KEY, DEFAULT_LED_BRIGHTNESS_PERCENT);
        preferences.end();

        _brightness = isBrightnessValid(stored) ? stored : LED_MAX_BRIGHTNESS_PERCENT;
        return true;
    }

    void _saveConfiguration()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, false)) { return; }
        preferences.putUChar(PREFERENCES_BRIGHTNESS_KEY, _brightness);
        preferences.end();
    }

    void setBrightness(uint8_t brightness)
    {
        _brightness = isBrightnessValid(brightness) ? brightness : LED_MAX_BRIGHTNESS_PERCENT;
        _saveConfiguration();
    }

    uint8_t getBrightness() { return _brightness; }

    void setPattern(LedState::Layer layer, LedPattern pattern, Color color, uint64_t durationMs,
                    uint32_t seed)
    {
        if (!acquireMutex(&_tableMutex, LED_MUTEX_TIMEOUT_MS)) { return; }
        LedState::set(_table, layer, pattern, color, millis64(), durationMs, seed);
        releaseMutex(&_tableMutex);
    }

    void setPattern(LedPattern pattern, Color color, LedPriority priority, uint64_t durationMs)
    {
        setPattern(_layerForPriority(priority), pattern, color, durationMs);
    }

    void clearLayer(LedState::Layer layer)
    {
        if (!acquireMutex(&_tableMutex, LED_MUTEX_TIMEOUT_MS)) { return; }
        LedState::release(_table, layer);
        releaseMutex(&_tableMutex);
    }

    void clearPattern(LedPriority priority) { clearLayer(_layerForPriority(priority)); }

    Snapshot getState()
    {
        Snapshot snapshot;
        snapshot.brightness = _brightness;

        // valid stays false - the caller must not read a failed read as "off"
        if (!acquireMutex(&_tableMutex, LED_MUTEX_TIMEOUT_MS)) { return snapshot; }
        const LedState::Frame frame = LedState::step(_table, millis64(), snapshot.brightness);
        releaseMutex(&_tableMutex);

        snapshot.valid = true;
        snapshot.active = frame.active;
        snapshot.isLit = frame.isOn && frame.output != LedState::Colors::OFF;

        return snapshot;
    }

    // Writes the already-rendered colour. Brightness is applied by LedState::render()
    // and must not be applied again here.
    static void _setHardwareColor(const LedState::Rgb &color)
    {
        if (_redPin == INVALID_PIN || _greenPin == INVALID_PIN || _bluePin == INVALID_PIN) { return; }

        ledcWrite(_redPin, color.red);
        ledcWrite(_greenPin, color.green);
        ledcWrite(_bluePin, color.blue);
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_ledTaskHandle, LED_TASK_STACK_SIZE, &_heartbeat);
    }
}
