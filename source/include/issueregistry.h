// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include "issue_logic.h" // lib/issue_logic - pure lifecycle + predicates (host-tested)
#include "constants.h"
#include "structs.h"

// Device issue registry (issue #145). One periodic tick derives every issue
// from current runtime facts (counters, flags, RTC state) through the pure
// predicates in lib/issue_logic - nothing is persisted, so a stale issue is
// impossible by construction. Modules never call into the registry; the
// dependency arrow points one way, registry -> facts.

// Task
#define ISSUE_REGISTRY_TASK_NAME "issue_reg_task"
#define ISSUE_REGISTRY_TASK_STACK_SIZE (6 * 1024) // Internal RAM: the tick reads LittleFS usage (flash I/O)
#define ISSUE_REGISTRY_TASK_PRIORITY 1
#define ISSUE_REGISTRY_TICK_INTERVAL (15 * 1000)

// Instance table
#define ISSUE_MAX_INSTANCES 32
#define ISSUE_MESSAGE_BUFFER_SIZE 192 // Longest catalog message (channel mismatch with label) is ~160 chars
#define ISSUE_GLOBAL_SCOPE 255 // channel value for globally-scoped codes

// Hysteresis (windows are one tick long, ~15 s)
#define ISSUE_STREAK_TO_RAISE 4              // ~1 min of sustained bad evidence
#define ISSUE_CONNECTIVITY_STREAK_TO_RAISE 8 // ~2 min: rides out boot/reconnect blips

// Per-code thresholds
#define ISSUE_MISMATCH_MIN_EVIDENCE_READS 4
#define ISSUE_MISMATCH_MIN_CLAMPED_FRACTION 0.9f
#define ISSUE_HEAP_LOW_THRESHOLD_BYTES (25 * 1024)
#define ISSUE_LITTLEFS_USED_FRACTION_RAISE 0.90f
#define ISSUE_LITTLEFS_USED_FRACTION_CLEAR 0.85f
#define ISSUE_VOLTAGE_TOLERANCE_FRACTION 0.10f
#define ISSUE_FREQUENCY_TOLERANCE_HZ 1.0f
#define ISSUE_ADE7953_FAILURES_PER_WINDOW 20
#define ISSUE_NTP_BOOT_GRACE_MS (5 * 60 * 1000)

namespace IssueRegistry
{
    void begin();
    void stop();

    // REST support. issuesToJson fills {"issues":[...],"summary":{...}}.
    bool issuesToJson(JsonDocument &doc);

    // Acknowledge one instance (channel = ISSUE_GLOBAL_SCOPE for global codes).
    // Returns false if the code string is unknown or no such instance exists.
    bool ack(const char *codeStr, uint8_t channel);

    // Acknowledge every visible instance. Returns the number acked.
    uint32_t ackAll();

    // Task information
    TaskInfo getTaskInfo();
}
