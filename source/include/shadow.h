// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "constants.h"

// AWS IoT Named Device Shadow module (issue #159).
//
// Implements the publish-reported-first / no-GET / no-accepted protocol (see
// docs/feature/2026-06-16-shadow-v2-config/00-overview-and-contract.md) for any
// number of named shadows. Each shadow registers a Descriptor with a ReportFn
// (serialize current state) and, if writable, an ApplyFn (apply a cloud delta).
//
// Threading model (no PubSubClient access happens off the MQTT task):
//  - routeMessage()  runs inside the PubSubClient callback (MQTT task): it only
//    copies the inbound payload out and flags it, never applies or publishes.
//  - checkPublish()  runs in the MQTT task body: it drains pending deltas
//    (apply -> ack publish) and pending reports, and republishes any writable
//    shadow whose reported state drifted from its last publish (so a local
//    config change from any source reaches the cloud without a per-call hook).
//  - requestReport() may be called from other tasks (issue registry, esp_timer,
//    web server); it only sets a flag - the MQTT task does the actual publishing.
namespace Shadow {

// Fill doc["state"]["reported"] with the shadow's full current state.
using ReportFn = void (*)(JsonDocument& doc);

// Apply one cloud delta. `delta` holds the changed desired fields (the AWS
// /update/delta "state" object). Fill `reported` with the values actually
// applied (echoed back to confirm). The envelope auto-nulls every top-level
// delta key in `desired` after this returns, so the delta clears - callbacks
// never touch desired (whole-key nulling avoids a re-trigger storm for fields
// the device cannot converge). Return true if at least one field was applied
// (informational only).
using ApplyFn = bool (*)(JsonObjectConst delta, JsonObject reported);

struct Descriptor {
    const char* name;     // "info", "system", ... (named-shadow name)
    bool        writable; // false => reported-only, no delta/rejected subscribe
    ReportFn    report;
    ApplyFn     apply;    // nullptr when !writable
};

// Create the mutex and register all shadow descriptors. Call once from Mqtt::begin().
void begin();

// Subscribe each writable shadow's delta/rejected topics and queue an initial
// reported publish for every shadow. Call from the MQTT task after (re)connect.
void onMqttConnected();

// Inspect an inbound MQTT topic; if it is a shadow topic, copy the payload out,
// flag it for the task-body drain, and return true (handled). Never applies or
// publishes - safe to call from the PubSubClient callback. Returns false if the
// topic is not a shadow topic.
bool routeMessage(const char* topic, const char* payload);

// Drain pending deltas, local edits and reports for all shadows. Call from the
// MQTT task body (after _clientMqtt.loop()).
void checkPublish();

// Queue a full reported publish for one shadow (flag only; cross-task safe).
void requestReport(const char* name);

#ifdef ENV_DEV
// Dev-only: feed a synthetic delta document (e.g. {"version":1,"state":{...}})
// through the real inbound path (routeMessage -> drain -> apply -> ack) without
// a cloud writer. Returns true if the named shadow accepted it for processing.
bool injectDelta(const char* name, const char* payload);
#endif

} // namespace Shadow
