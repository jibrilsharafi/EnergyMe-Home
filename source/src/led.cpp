// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "led.h"
#include "taskprofiler.h"

namespace Led
{
    static TaskHeartbeat _heartbeat;

    // Hardware pins
    static uint8_t _redPin = INVALID_PIN;
    static uint8_t _greenPin = INVALID_PIN;
    static uint8_t _bluePin = INVALID_PIN;
    static uint8_t _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;

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

    static bool _lock()
    {
        if (_tableMutex == nullptr) { return false; }
        return xSemaphoreTake(_tableMutex, pdMS_TO_TICKS(LED_MUTEX_TIMEOUT_MS)) == pdTRUE;
    }

    static void _unlock() { xSemaphoreGive(_tableMutex); }

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

        if (_tableMutex == nullptr)
        {
            _tableMutex = xSemaphoreCreateMutex();
            if (_tableMutex == nullptr)
            {
                LOG_ERROR("Failed to create LED mutex");
                return;
            }
        }

        LedState::releaseAll(_table);
        _setHardwareColor(LedState::Rgb{}); // Deterministic pins before the first render

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

    void stop()
    {
        stopTaskGracefully(&_ledTaskHandle, "LED task");

        if (_lock())
        {
            LedState::releaseAll(_table);
            _unlock();
        }

        _setHardwareColor(LedState::Rgb{});
    }

    static void _ledTask(void *parameter)
    {
        _ledTaskShouldRun = true;
        while (_ledTaskShouldRun)
        {
            TASK_HEARTBEAT(_heartbeat);

            const uint64_t currentTime = millis64();

            // Copy under the mutex, render outside it.
            if (_lock())
            {
                LedState::expire(_table, currentTime);
                const LedState::Table snapshot = _table;
                _unlock();

                const LedState::Active active = LedState::resolve(snapshot, currentTime);
                _setHardwareColor(
                    active.any
                        ? LedState::render(active.pattern, active.color, active.elapsedMs,
                                           LedState::effectiveBrightness(active.layer, _brightness))
                        : LedState::Rgb{});
            }

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_TASK_DELAY_MS)) > 0)
            {
                _ledTaskShouldRun = false;
            }
        }

        _ledTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void resetToDefaults()
    {
        _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;
        _saveConfiguration();
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

        _brightness = preferences.getUChar(PREFERENCES_BRIGHTNESS_KEY, DEFAULT_LED_BRIGHTNESS_PERCENT);
        preferences.end();

        _brightness = min(_brightness, (uint8_t)LED_MAX_BRIGHTNESS_PERCENT);
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
        _brightness = min(brightness, (uint8_t)LED_MAX_BRIGHTNESS_PERCENT);
        _saveConfiguration();
    }

    uint8_t getBrightness() { return _brightness; }

    LedState::Layer layerForPriority(LedPriority priority)
    {
        if (priority <= PRIO_USER) { return LedState::Layer::USER; }
        if (priority <= PRIO_NORMAL) { return LedState::Layer::STATUS; }
        if (priority <= PRIO_MEDIUM) { return LedState::Layer::NETWORK; }
        if (priority <= PRIO_URGENT) { return LedState::Layer::ALERT; }
        return LedState::Layer::CRITICAL;
    }

    void setPattern(LedState::Layer layer, LedPattern pattern, Color color, uint64_t durationMs)
    {
        if (!_lock()) { return; }
        LedState::set(_table, layer, pattern, color, millis64(), durationMs);
        _unlock();
    }

    void setPattern(LedPattern pattern, Color color, LedPriority priority, uint64_t durationMs)
    {
        setPattern(layerForPriority(priority), pattern, color, durationMs);
    }

    void clearLayer(LedState::Layer layer)
    {
        if (!_lock()) { return; }
        LedState::release(_table, layer);
        _unlock();
    }

    void clearPattern(LedPriority priority) { clearLayer(layerForPriority(priority)); }

    void clearAllPatterns()
    {
        if (!_lock()) { return; }
        LedState::releaseAll(_table);
        _unlock();
    }

    Snapshot getState()
    {
        Snapshot snapshot;
        snapshot.brightness = _brightness;

        if (!_lock()) { return snapshot; }
        const uint64_t currentTime = millis64();
        LedState::expire(_table, currentTime);
        const LedState::Active active = LedState::resolve(_table, currentTime);
        _unlock();

        if (!active.any) { return snapshot; }

        snapshot.any = true;
        snapshot.layer = active.layer;
        snapshot.pattern = active.pattern;
        snapshot.color = active.color;
        snapshot.remainingMs = active.remainingMs;
        snapshot.indefinite = active.indefinite;

        const LedState::Rgb rendered = LedState::render(
            active.pattern, active.color, active.elapsedMs,
            LedState::effectiveBrightness(active.layer, snapshot.brightness));
        snapshot.isLit = rendered != LedState::Rgb{};

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
