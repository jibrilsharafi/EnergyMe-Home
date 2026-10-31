// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "collector_logic.h"

// LAN collector (openspec capability: lan-collector): scheduled polling jobs
// defined by the cloud in the `collector` shadow, persisted on LittleFS, run
// one step at a time in the gateway task through the executor, and published
// raw (one JSON header line plus the reply bytes per part) for cloud decoding.
//
// Threading: the MQTT task validates and persists job sets (apply), reports
// health from a snapshot, answers collector_status and drains the out-queue.
// The gateway task owns the active job table and every run state (captures,
// cursors, sessions) and adopts a new table from a one-slot mailbox between
// steps. The out-queue and the snapshot are guarded by short copy-only mutexes.

#define COLLECTOR_ENABLED_KEY "collector_en" // in gateway_ns
#define COLLECTOR_DIR "/collector"
#define COLLECTOR_JOBSET_PATH "/collector/jobset.json"
#define COLLECTOR_JOBSET_TMP_PATH "/collector/jobset.json.tmp"
#define COLLECTOR_RING_BYTES (256 * 1024)
#define COLLECTOR_RING_RETRY_MS (60 * 1000)
#define COLLECTOR_DRAIN_INTERVAL_MS 1000
#define COLLECTOR_LOW_HEAP_FREE (40 * 1024)
#define COLLECTOR_LOW_HEAP_BLOCK (16 * 1024)
#define COLLECTOR_STATUS_MAX_BYTES 3500

namespace Collector
{
    void begin(); // loads the switch and the stored set; allocates the ring when enabled

    bool isEnabled();
    bool setEnabled(bool enabled); // NVS; shadow and REST use the same setter

    // MQTT task: the `collector` shadow.
    void reportToJson(JsonObject reported);
    bool applyDelta(JsonObjectConst delta, JsonObject reported);

    enum class StatusResult { OK, NO_SUCH_JOB, BUSY };
    // MQTT task: collector_status op. Fills `result` (IoT command result map).
    StatusResult statusToResult(const char* job, JsonObject result);
    // The same status as plain JSON (REST).
    StatusResult statusToJson(const char* job, JsonObject out);

    // MQTT task: out-queue drain, at most one message per call.
    // `publish` streams header+payload parts on the source topic; returns true
    // when the whole message was written.
    struct Message {
        const char* sourceId;
        size_t bytes;
        size_t count;
        const uint8_t* parts[16];
        size_t partBytes[16];
        size_t headerLength[16];
    };
    void drain(bool (*publish)(const Message& message, void* context), void* context);
    bool drainAllowed(); // no tunnel session, no OTA, enough internal heap

    // Gateway task.
    uint32_t nextWaitMs(uint32_t maxMs); // until the next due job, at most maxMs
    void step();                          // adopts a pending set, then runs at most one step
    void noteCommandMs(uint32_t ms);      // command time excluded from run deadlines

    // Set by the MQTT layer (OTA download or reboot pending).
    void setOtaBusy(bool busy);
    void setCloudConnected(bool connected);
}
