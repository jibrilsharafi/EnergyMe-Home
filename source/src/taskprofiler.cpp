// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "taskprofiler.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>
#include <esp_freertos_hooks.h>
#include <esp_timer.h>

#include "utils.h"

namespace TaskProfiler
{
    static constexpr uint8_t CORE_COUNT = 2;

    // Idle hook just bumps a counter. IRAM-safe, non-blocking.
    static volatile uint64_t _idleCount[CORE_COUNT] = {0, 0};

    // Sampler state.
    static uint64_t _lastSampleIdleCount[CORE_COUNT] = {0, 0};
    static uint64_t _lastSampleMicros = 0;
    static uint64_t _baselineIdlePerSecond[CORE_COUNT] = {0, 0}; // Auto-calibrated max
    static uint8_t _lastLoadPercent[CORE_COUNT] = {0, 0};
    static uint32_t _lastSampleWindowMs = 0;
    static bool _begun = false;
    static esp_timer_handle_t _samplerTimer = NULL;
    static constexpr uint32_t SAMPLE_INTERVAL_US = 1000000; // 1 Hz

    static void _samplerTimerCallback(void* arg) { sample(); }

    static bool IRAM_ATTR _idleHookCore0() { _idleCount[0]++; return true; }
    static bool IRAM_ATTR _idleHookCore1() { _idleCount[1]++; return true; }

    void begin()
    {
        if (_begun) return;

        esp_err_t r0 = esp_register_freertos_idle_hook_for_cpu(_idleHookCore0, 0);
        esp_err_t r1 = esp_register_freertos_idle_hook_for_cpu(_idleHookCore1, 1);

        if (r0 != ESP_OK || r1 != ESP_OK) {
            // Don't LOG_ here - we may be called before logging is fully up.
            // Failure just means CPU% reports 0; heartbeats still work.
            Serial.printf("[WARN] TaskProfiler: idle hook registration failed (core0=%d, core1=%d)\n", r0, r1);
            return;
        }

        _lastSampleMicros = esp_timer_get_time();
        _lastSampleIdleCount[0] = _idleCount[0];
        _lastSampleIdleCount[1] = _idleCount[1];
        _begun = true;

        // Start a periodic 1 Hz sampler so CPU% updates without any caller
        // having to plumb sample() into their task loop. The esp_timer task
        // dispatches the callback; sample() itself is non-blocking.
        const esp_timer_create_args_t timerArgs = {
            .callback = _samplerTimerCallback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "task_profiler",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&timerArgs, &_samplerTimer) == ESP_OK) {
            esp_timer_start_periodic(_samplerTimer, SAMPLE_INTERVAL_US);
        } else {
            Serial.println("[WARN] TaskProfiler: esp_timer_create failed; sample() must be called manually");
        }
    }

    void sample()
    {
        if (!_begun) return;

        uint64_t nowMicros = esp_timer_get_time();
        uint64_t windowMicros = nowMicros - _lastSampleMicros;
        if (windowMicros == 0) return; // Called too fast (clock didn't advance)

        _lastSampleWindowMs = (uint32_t)(windowMicros / 1000ULL);

        for (uint8_t c = 0; c < CORE_COUNT; c++) {
            uint64_t cur = _idleCount[c];
            uint64_t delta = cur - _lastSampleIdleCount[c];
            _lastSampleIdleCount[c] = cur;

            // Normalize to per-second so the baseline is comparable across windows.
            uint64_t idlePerSec = (delta * 1000000ULL) / windowMicros;

            // Auto-calibrate the baseline: the highest per-second idle rate ever
            // observed is treated as 100% idle (0% load). New peaks ratchet up.
            if (idlePerSec > _baselineIdlePerSecond[c]) {
                _baselineIdlePerSecond[c] = idlePerSec;
            }

            if (_baselineIdlePerSecond[c] == 0) {
                _lastLoadPercent[c] = 0; // No baseline yet
                continue;
            }

            // load = 100 * (1 - idle/baseline), clamped to [0, 100]
            uint64_t loadX100 = 0;
            if (idlePerSec < _baselineIdlePerSecond[c]) {
                loadX100 = 100ULL - ((idlePerSec * 100ULL) / _baselineIdlePerSecond[c]);
            }
            if (loadX100 > 100) loadX100 = 100;
            _lastLoadPercent[c] = (uint8_t)loadX100;
        }

        _lastSampleMicros = nowMicros;
    }

    uint8_t getCoreLoadPercent(uint8_t coreId)
    {
        if (coreId >= CORE_COUNT) return 0;
        return _lastLoadPercent[coreId];
    }

    uint32_t getLastSampleWindowMs()
    {
        return _lastSampleWindowMs;
    }
}
