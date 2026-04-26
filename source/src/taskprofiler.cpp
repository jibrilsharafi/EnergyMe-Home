// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "taskprofiler.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>
#include <esp_freertos_hooks.h>
#include <esp_timer.h>
#include <time.h>

#include "utils.h"

namespace TaskProfiler
{
    static constexpr uint8_t CORE_COUNT = 2;
    static constexpr uint32_t SAMPLE_INTERVAL_US = 1000000; // 1 Hz

    // Idle hook state - bumped in IRAM context, non-blocking.
    static volatile uint64_t _idleCount[CORE_COUNT] = {0, 0};

    // Sampler state.
    static uint64_t _lastSampleIdleCount[CORE_COUNT] = {0, 0};
    static uint64_t _lastSampleMicros = 0;
    static uint64_t _baselineIdlePerSecond[CORE_COUNT] = {0, 0};
    static uint8_t  _lastLoadPercent[CORE_COUNT] = {0, 0};
    static uint32_t _lastSampleWindowMs = 0;
    static bool     _begun = false;
    static esp_timer_handle_t _samplerTimer = NULL;

    // Ring buffer (PSRAM).
    static CpuSample*       _ring      = nullptr;
    static size_t           _ringHead  = 0;    // next write index
    static size_t           _ringFilled = 0;   // valid entries [0..CPU_RING_CAPACITY_SECONDS]
    static SemaphoreHandle_t _ringMutex = nullptr;

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    static void _samplerTimerCallback(void* arg) { sample(); }

    static bool IRAM_ATTR _idleHookCore0() { _idleCount[0]++; return true; }
    static bool IRAM_ATTR _idleHookCore1() { _idleCount[1]++; return true; }

    static uint8_t _percentileFromHist(const uint16_t* hist, uint32_t total, uint8_t p)
    {
        uint32_t target = ((uint32_t)total * p + 50) / 100;
        uint32_t cum = 0;
        for (uint16_t i = 0; i <= 100; i++) {
            cum += hist[i];
            if (cum >= target) return (uint8_t)i;
        }
        return 100;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void begin()
    {
        if (_begun) return;

        esp_err_t r0 = esp_register_freertos_idle_hook_for_cpu(_idleHookCore0, 0);
        esp_err_t r1 = esp_register_freertos_idle_hook_for_cpu(_idleHookCore1, 1);

        if (r0 != ESP_OK || r1 != ESP_OK) {
            Serial.printf("[WARN] TaskProfiler: idle hook registration failed (core0=%d, core1=%d)\n", r0, r1);
            return;
        }

        // Allocate ring in PSRAM (28.8 KB, trivial vs. available PSRAM).
        _ring = (CpuSample*)ps_malloc(CPU_RING_CAPACITY_SECONDS * sizeof(CpuSample));
        if (_ring == nullptr) {
            Serial.println("[WARN] TaskProfiler: ps_malloc for ring failed; spike stats unavailable");
        } else {
            memset(_ring, 0, CPU_RING_CAPACITY_SECONDS * sizeof(CpuSample));
            _ringMutex = xSemaphoreCreateMutex();
            if (_ringMutex == nullptr) {
                Serial.println("[WARN] TaskProfiler: mutex creation failed; spike stats unavailable");
                free(_ring);
                _ring = nullptr;
            }
        }

        _lastSampleMicros = esp_timer_get_time();
        _lastSampleIdleCount[0] = _idleCount[0];
        _lastSampleIdleCount[1] = _idleCount[1];
        _begun = true;

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
        if (windowMicros == 0) return;

        _lastSampleWindowMs = (uint32_t)(windowMicros / 1000ULL);

        for (uint8_t c = 0; c < CORE_COUNT; c++) {
            uint64_t cur = _idleCount[c];
            uint64_t delta = cur - _lastSampleIdleCount[c];
            _lastSampleIdleCount[c] = cur;

            uint64_t idlePerSec = (delta * 1000000ULL) / windowMicros;

            if (idlePerSec > _baselineIdlePerSecond[c]) {
                _baselineIdlePerSecond[c] = idlePerSec;
            }

            if (_baselineIdlePerSecond[c] == 0) {
                _lastLoadPercent[c] = 0;
                continue;
            }

            uint64_t loadX100 = 0;
            if (idlePerSec < _baselineIdlePerSecond[c]) {
                loadX100 = 100ULL - ((idlePerSec * 100ULL) / _baselineIdlePerSecond[c]);
            }
            if (loadX100 > 100) loadX100 = 100;
            _lastLoadPercent[c] = (uint8_t)loadX100;
        }

        _lastSampleMicros = nowMicros;

        // Push one entry to the ring buffer under mutex.
        if (_ring != nullptr && _ringMutex != nullptr) {
            if (xSemaphoreTake(_ringMutex, pdMS_TO_TICKS(RING_MUTEX_TIMEOUT_MS)) == pdTRUE) {
                _ring[_ringHead].epochSecond  = (uint32_t)time(nullptr);
                _ring[_ringHead].core0Percent = _lastLoadPercent[0];
                _ring[_ringHead].core1Percent = _lastLoadPercent[1];
                _ring[_ringHead]._reserved    = 0;
                _ringHead = (_ringHead + 1) % CPU_RING_CAPACITY_SECONDS;
                if (_ringFilled < CPU_RING_CAPACITY_SECONDS) _ringFilled++;
                xSemaphoreGive(_ringMutex);
            }
        }
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

    CpuStats getCpuStats(uint8_t coreId, uint32_t lastSeconds)
    {
        CpuStats s = {};
        if (coreId >= CORE_COUNT || _ring == nullptr || _ringMutex == nullptr) return s;

        uint16_t hist[101] = {0};
        uint8_t  minV = 100, maxV = 0;
        uint32_t sum = 0, count = 0, aboveThr = 0;

        if (xSemaphoreTake(_ringMutex, pdMS_TO_TICKS(RING_MUTEX_TIMEOUT_MS)) != pdTRUE) return s;

        size_t window = (lastSeconds == 0 || (size_t)lastSeconds > _ringFilled)
                        ? _ringFilled
                        : (size_t)lastSeconds;

        for (size_t i = 0; i < window; i++) {
            // Oldest entry in the window is at offset (capacity - window + i) from _ringHead.
            size_t idx = (_ringHead + CPU_RING_CAPACITY_SECONDS - window + i) % CPU_RING_CAPACITY_SECONDS;
            uint8_t v = (coreId == 0) ? _ring[idx].core0Percent : _ring[idx].core1Percent;
            hist[v]++;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
            sum += v;
            count++;
            if (v >= CPU_SPIKE_THRESHOLD) aboveThr++;
        }

        xSemaphoreGive(_ringMutex);

        if (count == 0) return s;

        s.current              = _lastLoadPercent[coreId];
        s.min                  = minV;
        s.max                  = maxV;
        s.avg                  = (uint8_t)(sum / count);
        s.samplesInWindow      = (uint16_t)count;
        s.samplesAboveThreshold = (uint16_t)aboveThr;
        s.windowSeconds        = (uint32_t)count;
        s.p50 = _percentileFromHist(hist, count, 50);
        s.p90 = _percentileFromHist(hist, count, 90);
        s.p95 = _percentileFromHist(hist, count, 95);
        s.p99 = _percentileFromHist(hist, count, 99);
        return s;
    }

    size_t getRawSamples(CpuSample* out, size_t maxCount, uint32_t lastSeconds)
    {
        if (_ring == nullptr || _ringMutex == nullptr || out == nullptr || maxCount == 0) return 0;

        if (xSemaphoreTake(_ringMutex, pdMS_TO_TICKS(RING_MUTEX_TIMEOUT_MS)) != pdTRUE) return 0;

        size_t window = (lastSeconds == 0 || (size_t)lastSeconds > _ringFilled)
                        ? _ringFilled
                        : (size_t)lastSeconds;
        if (window > maxCount) window = maxCount;

        for (size_t i = 0; i < window; i++) {
            size_t idx = (_ringHead + CPU_RING_CAPACITY_SECONDS - window + i) % CPU_RING_CAPACITY_SECONDS;
            out[i] = _ring[idx];
        }

        xSemaphoreGive(_ringMutex);
        return window;
    }
}
