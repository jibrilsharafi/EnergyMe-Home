// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "constants.h"

// Lightweight task-load profiler.
//
// - Per-core CPU%: one FreeRTOS idle hook per core increments a counter; a 1 Hz
//   sampler driven by esp_timer computes load% against an auto-calibrated baseline.
// - Spike-aware ring buffer: 1 hour of 1 Hz samples in PSRAM; on-demand
//   percentile (p50/p90/p95/p99) + min/max/avg computed via in-place histogram.
// - Per-task heartbeat: each task writes lastTickMillis + loopCount via the
//   TASK_HEARTBEAT macro at the top of its main loop.
//
// Works under framework=arduino with no sdkconfig changes.

namespace TaskProfiler
{
    // Register idle hooks on both cores, allocate the PSRAM ring buffer, and
    // start a periodic esp_timer that drives sample() at 1 Hz. Call once from
    // setup() before any task is created. Idempotent (no-op on subsequent calls).
    void begin();

    // Sample idle deltas, update per-core load%, and push one entry to the ring.
    // Driven automatically by the esp_timer registered in begin(); exposed for
    // tests / diagnostics only - normal code should not call this.
    void sample();

    // Returns 0..100. Returns 0 if begin() has not been called or not enough
    // samples are available yet (first sample establishes the baseline).
    uint8_t getCoreLoadPercent(uint8_t coreId);

    // Wall-clock milliseconds covered by the most recent sample window.
    uint32_t getLastSampleWindowMs();

    static constexpr size_t   CPU_RING_CAPACITY_SECONDS     = 3600; // 1 hour at 1 Hz
    static constexpr uint8_t  CPU_SPIKE_THRESHOLD           = 80;   // % above which a sample counts as a spike
    static constexpr uint32_t CPU_DEFAULT_FETCH_LAST_SECONDS = 600; // default window for getRawSamples
    static constexpr uint32_t RING_MUTEX_TIMEOUT_MS         = 50;

    // One ring entry: 8 bytes, written once per second by sample().
    struct CpuSample {
        uint32_t epochSecond;  // Unix timestamp of the sample
        uint8_t  core0Percent;
        uint8_t  core1Percent;
        uint16_t _reserved;   // padding to 8 bytes
    };

    // Aggregated stats for one core over a window of samples.
    struct CpuStats {
        uint8_t  current;              // most recent instantaneous value
        uint8_t  min, max, avg;
        uint8_t  p50, p90, p95, p99;
        uint16_t samplesInWindow;
        uint16_t samplesAboveThreshold; // samples where value >= CPU_SPIKE_THRESHOLD
        uint32_t windowSeconds;        // actual seconds covered by the window
    };

    // Returns aggregated stats for coreId over the last lastSeconds seconds of
    // ring history. lastSeconds=0 means use the full ring (up to
    // CPU_RING_CAPACITY_SECONDS). Returns zero-initialized struct on error.
    CpuStats getCpuStats(uint8_t coreId, uint32_t lastSeconds = 0);

    // Copies up to maxCount raw samples (oldest-first within the window) into
    // out[]. Returns actual count written. lastSeconds=0 means full ring.
    size_t getRawSamples(CpuSample* out, size_t maxCount,
                         uint32_t lastSeconds = CPU_DEFAULT_FETCH_LAST_SECONDS);
}

// Heartbeat helper. Place at the very top of each task loop body so that
// "task hasn't ticked in N ms" is observable from /system/info.
//
// Cost: one millis64() call + one increment, no mutex. Acceptable for any task
// that loops at 1 Hz or slower; for high-frequency loops the cost is still
// negligible (millis64 is a couple of register reads).
//
// Usage:
//   static TaskHeartbeat _myTaskHeartbeat;
//   void _myTask(void*) {
//       while (_taskShouldRun) {
//           TASK_HEARTBEAT(_myTaskHeartbeat);
//           // ... task body ...
//       }
//   }
//   TaskInfo getMyTaskInfo() { return getTaskInfoSafely(_taskHandle, STACK, &_myTaskHeartbeat); }
#define TASK_HEARTBEAT(hb) do { \
    (hb).lastTickMillis = millis64(); \
    (hb).loopCount++; \
} while (0)
