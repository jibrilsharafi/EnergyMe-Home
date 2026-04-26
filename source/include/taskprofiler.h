// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "constants.h"

// Lightweight task-load profiler.
//
// - Per-core CPU%: one FreeRTOS idle hook per core increments a counter; a 1 Hz
//   sampler (driven by the existing maintenance task) computes load against an
//   auto-calibrated baseline (the highest idle-rate ever observed).
// - Per-task heartbeat: each task writes lastTickMillis + loopCount via the
//   TASK_HEARTBEAT macro at the top of its main loop. A stale heartbeat means
//   the task is starved or stuck.
//
// Works under framework=arduino with no sdkconfig changes (uses
// esp_register_freertos_idle_hook_for_cpu, available out of the box).

namespace TaskProfiler
{
    // Register idle hooks on both cores. Call once from setup() before any task
    // is created. Safe to call multiple times (no-op after the first success).
    void begin();

    // Sample idle deltas and update per-core load percentages. Call ~once per
    // second from a long-lived task (the maintenance task in this firmware).
    void sample();

    // Returns 0..100. Returns 0 if begin() has not been called or if not enough
    // samples are available yet (first sample establishes the baseline).
    uint8_t getCoreLoadPercent(uint8_t coreId);

    // Wall-clock milliseconds covered by the most recent sample window. Useful
    // for sanity-checking the value (sample() called too rarely → window too
    // long → values smoothed).
    uint32_t getLastSampleWindowMs();
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
