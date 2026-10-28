// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

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
#define GATEWAY_TASK_STACK_SIZE (8 * 1024) // lwIP sockets + ArduinoJson; profile before trimming
#define GATEWAY_TASK_PRIORITY 1

#define GATEWAY_SCAN_MAX_HOSTS 254
#define GATEWAY_SCAN_BATCH_SIZE 8           // Below lwIP's 10-entry ARP table so a batch is read before eviction
#define GATEWAY_SCAN_BATCH_WAIT_MS 300
#define GATEWAY_SCAN_MAX_DURATION_MS (30 * 1000)
#define GATEWAY_SCAN_LIST_BUDGET 3500       // Serialized bytes per result list, so the response fits the 9 KB MQTT buffer
// Decoded args. The inbound MQTT buffer (9 KB) is the real ceiling: 6 KB of
// JSON is 8 KB once base64-encoded, plus the command envelope.
#define GATEWAY_ARGS_MAX_BYTES (6 * 1024)

namespace Gateway
{
    void begin();
    void stop();

    bool isEnabled();
    void setEnabled(bool enabled); // Persists to NVS; used by the system shadow, REST and UI alike

    enum class SubmitResult { ACCEPTED, NOT_ENABLED, BUSY, BAD_ARGS, NO_MEMORY };

    // Hands a request to the gateway task. `args` is the JSON object itself
    // (dev inject harness) or a string holding its base64 encoding (cloud
    // template). Copied, so the caller's document can go away.
    SubmitResult submit(const char* executionId, GatewayLogic::Op op, JsonVariantConst args);

    // If the running request has finished, copies its execution id into
    // `executionId` and hands over a PSRAM-allocated JSON document with the full
    // command response ({status, statusReason?, result?}), which the caller
    // frees with free(). Clears the busy state.
    bool takeResult(char* executionId, size_t executionIdSize, char** responseJson);
}
