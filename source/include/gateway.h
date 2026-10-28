// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "gateway_logic.h"

// LAN gateway (openspec capability: lan-gateway). The cloud sends one generic
// primitive per IoT Command (ping, net_scan, http_request, modbus_read/write,
// udp_send) and the device runs it against private IPv4 targets. All device
// knowledge lives in the cloud.
//
// Threading: submit() and takeResult() are called only by the MQTT task, which
// owns every publish. The primitives run in the gateway task, one at a time.

#define GATEWAY_ENABLED_KEY "enabled"
#define GATEWAY_ENABLED_DEFAULT false

#define GATEWAY_TASK_NAME "gateway_task"
#define GATEWAY_TASK_STACK_SIZE (8 * 1024) // HTTPClient + lwIP sockets; profile before trimming
#define GATEWAY_TASK_PRIORITY 1

#define GATEWAY_SCAN_BATCH_SIZE 8           // Below lwIP's 10-entry ARP table so a batch is read before eviction
#define GATEWAY_SCAN_BATCH_WAIT_MS 300
#define GATEWAY_SCAN_MAX_DURATION_MS (30 * 1000)
#define GATEWAY_ARGS_MAX_BYTES (8 * 1024) // Decoded args; the largest legitimate payload is an HTTP body

namespace Gateway
{
    void begin();
    void stop();

    bool isEnabled();
    void setEnabled(bool enabled); // Persists to NVS; used by the system shadow, REST and UI alike

    enum class SubmitResult { ACCEPTED, NOT_ENABLED, BUSY, NO_MEMORY };

    // Hands a request to the gateway task. `argsJson` is the args object as
    // compact JSON (already base64-decoded); it is copied.
    SubmitResult submit(const char* executionId, GatewayLogic::Op op, const char* argsJson);

    // If the running request has finished, returns true and gives the caller a
    // PSRAM-allocated, null-terminated JSON document holding the full command
    // response ({status, statusReason?, result?}). The caller frees both buffers
    // with free(). Clears the busy state.
    bool takeResult(char** executionId, char** responseJson);
}
