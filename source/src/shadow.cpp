// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "shadow.h"

#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "awsconfig.h"
#include "factory_keys.h"
#include "globals.h"
#include "hardware_profile.h"
#include "mqtt.h"
#include "shadow_logic.h"
#include "utils.h"

namespace Shadow {

// Up to 5 named shadows: info, issues, system, meter, channels.
#define SHADOW_MAX_COUNT 5
#define SHADOW_NAME_PREFIX "shadow/name/"
#define SHADOW_INFO_REFRESH_INTERVAL_US (24ULL * 60ULL * 60ULL * 1000000ULL) // 24 h

// Per-shadow runtime state.
//  - reportPending: benign volatile flag; any task may set it true, the MQTT
//    task clears it in checkPublish(). No mutex needed (idempotent set; a lost
//    race just defers one publish by one loop).
//  - pendingDelta / pendingLocalEdit: ps_malloc'd payload copies handed from the
//    RX callback / web task to the MQTT task. Pointer ownership is swapped under
//    _mutex (the only genuinely cross-task shared state here).
struct ShadowEntry {
    Descriptor    desc;
    uint32_t      version;
    volatile bool reportPending;
    char*         pendingDelta;
    char*         pendingLocalEdit;
};

static ShadowEntry _shadows[SHADOW_MAX_COUNT];
static uint8_t _count = 0;
static SemaphoreHandle_t _mutex = nullptr;
static esp_timer_handle_t _infoRefreshTimer = nullptr;

// Report/apply callbacks (defined further down, registered in begin()).
static void _reportInfo(JsonDocument& doc);

// ============================================================================
// Registration
// ============================================================================

static void _registerShadow(const Descriptor& desc) {
    if (_count >= SHADOW_MAX_COUNT) {
        LOG_ERROR("Shadow table full, cannot register '%s'", desc.name);
        return;
    }
    if (desc.writable && desc.apply == nullptr) {
        LOG_ERROR("Writable shadow '%s' has no apply callback", desc.name);
        return;
    }
    _shadows[_count] = {desc, 0, false, nullptr, nullptr};
    _count++;
    LOG_DEBUG("Registered shadow '%s' (%s)", desc.name, desc.writable ? "writable" : "reported-only");
}

static int _findShadow(const char* name, size_t nameLen) {
    for (uint8_t i = 0; i < _count; i++) {
        if (strlen(_shadows[i].desc.name) == nameLen &&
            strncmp(_shadows[i].desc.name, name, nameLen) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void _infoRefreshCallback(void* arg) {
    (void)arg;
    requestReport("info"); // flag only; MQTT task publishes
}

void begin() {
    if (!createMutexIfNeeded(&_mutex)) {
        LOG_ERROR("Failed to create shadow mutex");
        return;
    }

    _count = 0;
    _registerShadow({"info", false, _reportInfo, nullptr});

    // Low-priority periodic refresh so identity shadow's lastUpdated stays fresh
    // even though the values rarely change. Callback only sets a flag.
    if (_infoRefreshTimer == nullptr) {
        const esp_timer_create_args_t timerArgs = {
            .callback = _infoRefreshCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "shadow_info_refresh",
            .skip_unhandled_events = true};
        if (esp_timer_create(&timerArgs, &_infoRefreshTimer) == ESP_OK) {
            esp_timer_start_periodic(_infoRefreshTimer, SHADOW_INFO_REFRESH_INTERVAL_US);
        } else {
            LOG_WARNING("Failed to create info shadow refresh timer");
        }
    }

    LOG_DEBUG("Shadow module initialized with %u shadow(s)", _count);
}

// ============================================================================
// Topic helpers
// ============================================================================

static void _topicUpdate(const char* name, char* out, size_t outSize) {
    snprintf(out, outSize, SHADOW_NAME_PREFIX "%s/update", name);
}

// ============================================================================
// Connect: subscribe writable shadows, queue initial reports
// ============================================================================

void onMqttConnected() {
    char suffix[MQTT_TOPIC_BUFFER_SIZE];
    for (uint8_t i = 0; i < _count; i++) {
        if (_shadows[i].desc.writable) {
            snprintf(suffix, sizeof(suffix), SHADOW_NAME_PREFIX "%s/update/delta", _shadows[i].desc.name);
            Mqtt::subscribeReservedThings(suffix);
            snprintf(suffix, sizeof(suffix), SHADOW_NAME_PREFIX "%s/update/rejected", _shadows[i].desc.name);
            Mqtt::subscribeReservedThings(suffix);
        }
        // Publish-reported-first on every (re)connect. A still-pending cloud
        // desired survives (we send no desired here) -> AWS re-sends the delta.
        _shadows[i].reportPending = true;
    }
    LOG_DEBUG("Shadow subscriptions set; initial reports queued for %u shadow(s)", _count);
}

// ============================================================================
// Inbound routing (RX callback context: copy + flag only, never apply/publish)
// ============================================================================

bool routeMessage(const char* topic, const char* payload) {
    const char* marker = strstr(topic, "/" SHADOW_NAME_PREFIX);
    if (marker == nullptr) return false; // not a shadow topic

    const char* nameStart = marker + strlen("/" SHADOW_NAME_PREFIX);
    const char* nameEnd = strchr(nameStart, '/');
    if (nameEnd == nullptr) {
        LOG_WARNING("Malformed shadow topic: %s", topic);
        return true; // it is a shadow topic, just unusable - do not fall through
    }

    int idx = _findShadow(nameStart, (size_t)(nameEnd - nameStart));
    if (idx < 0) {
        LOG_WARNING("Delta for unknown shadow in topic: %s", topic);
        return true;
    }

    if (endsWith(topic, "/update/delta")) {
        size_t len = strlen(payload) + 1;
        char* copy = (char*)ps_malloc(len);
        if (copy == nullptr) {
            LOG_ERROR("Failed to allocate shadow delta buffer (%zu bytes)", len);
            return true;
        }
        memcpy(copy, payload, len);
        if (acquireMutex(&_mutex)) {
            if (_shadows[idx].pendingDelta != nullptr) free(_shadows[idx].pendingDelta); // newer delta wins
            _shadows[idx].pendingDelta = copy;
            releaseMutex(&_mutex);
            LOG_DEBUG("Queued delta for shadow '%s' (%zu bytes)", _shadows[idx].desc.name, len - 1);
        } else {
            free(copy);
            LOG_ERROR("Failed to acquire shadow mutex queuing delta for '%s'", _shadows[idx].desc.name);
        }
    } else if (endsWith(topic, "/update/rejected")) {
        // 409 (version conflict) recovery is a reported-first re-publish (carries
        // no version); for any rejection that is a safe resync. Log the payload.
        LOG_WARNING("Shadow '%s' update rejected: %s", _shadows[idx].desc.name, payload);
        _shadows[idx].reportPending = true;
    } else {
        LOG_DEBUG("Ignoring shadow topic (not delta/rejected): %s", topic);
    }
    return true;
}

// ============================================================================
// Publishing primitives (MQTT task context only)
// ============================================================================

static void _publishReported(uint8_t idx) {
    SpiRamAllocator allocator;
    JsonDocument doc(&allocator);
    _shadows[idx].desc.report(doc); // fills doc["state"]["reported"]

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    _topicUpdate(_shadows[idx].desc.name, topic, sizeof(topic));
    if (Mqtt::publishReservedThings(doc, topic)) {
        LOG_DEBUG("Published reported state for shadow '%s'", _shadows[idx].desc.name);
    } else {
        LOG_WARNING("Failed to publish reported state for shadow '%s'", _shadows[idx].desc.name);
    }
}

static void _addClientToken(JsonDocument& doc) {
    char token[24];
    if (ShadowLogic::formatClientToken(token, sizeof(token), esp_random(), esp_random()) > 0) {
        doc["clientToken"] = token; // copied (mutable buffer)
    }
}

static void _applyDelta(uint8_t idx, const char* payload) {
    if (_shadows[idx].desc.apply == nullptr) {
        LOG_WARNING("Delta for reported-only shadow '%s' ignored", _shadows[idx].desc.name);
        return;
    }

    SpiRamAllocator inAllocator;
    JsonDocument inDoc(&inAllocator);
    DeserializationError error = deserializeJson(inDoc, payload);
    if (error) {
        LOG_ERROR("Failed to parse delta for shadow '%s' (%s)", _shadows[idx].desc.name, error.c_str());
        return;
    }

    uint32_t version = inDoc["version"] | 0u;
    _shadows[idx].version = version; // MQTT-task-only field

    JsonObjectConst delta = inDoc["state"].as<JsonObjectConst>();
    if (delta.isNull()) {
        LOG_WARNING("Delta for shadow '%s' has no 'state' object", _shadows[idx].desc.name);
        return;
    }

    SpiRamAllocator outAllocator;
    JsonDocument outDoc(&outAllocator);
    JsonObject reported = outDoc["state"]["reported"].to<JsonObject>();
    JsonObject desired = outDoc["state"]["desired"].to<JsonObject>();

    _shadows[idx].desc.apply(delta, reported, desired);

    // Ack/clear invariant: every top-level field the cloud sent must be nulled
    // in desired. The apply callback may have nulled keys granularly (nested
    // object) - leave those; null any it did not touch.
    for (JsonPairConst kv : delta) {
        if (!desired[kv.key()].is<JsonObject>()) desired[kv.key()] = nullptr;
    }

    if (ShadowLogic::shouldSendVersion(version)) outDoc["version"] = version;
    _addClientToken(outDoc);

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    _topicUpdate(_shadows[idx].desc.name, topic, sizeof(topic));
    if (Mqtt::publishReservedThings(outDoc, topic)) {
        LOG_INFO("Applied delta for shadow '%s' (version %lu)", _shadows[idx].desc.name, (unsigned long)version);
    } else {
        LOG_WARNING("Failed to publish delta ack for shadow '%s'", _shadows[idx].desc.name);
    }
}

static void _publishLocalEditDoc(uint8_t idx, const char* payload) {
    SpiRamAllocator inAllocator;
    JsonDocument changed(&inAllocator);
    if (deserializeJson(changed, payload)) {
        LOG_WARNING("Failed to parse staged local edit for shadow '%s'", _shadows[idx].desc.name);
        return;
    }

    SpiRamAllocator outAllocator;
    JsonDocument outDoc(&outAllocator);
    JsonObject reported = outDoc["state"]["reported"].to<JsonObject>();
    JsonObject desired = outDoc["state"]["desired"].to<JsonObject>();
    for (JsonPairConst kv : changed.as<JsonObjectConst>()) {
        reported[kv.key()] = kv.value(); // local value wins
        desired[kv.key()] = nullptr;     // clear any pending cloud intent
    }
    // No version: a local edit is an unconditional report+clear (never 409).
    _addClientToken(outDoc);

    char topic[MQTT_TOPIC_BUFFER_SIZE];
    _topicUpdate(_shadows[idx].desc.name, topic, sizeof(topic));
    if (Mqtt::publishReservedThings(outDoc, topic)) {
        LOG_DEBUG("Published local edit for shadow '%s'", _shadows[idx].desc.name);
    } else {
        LOG_WARNING("Failed to publish local edit for shadow '%s'", _shadows[idx].desc.name);
    }
}

// ============================================================================
// Drain (MQTT task body)
// ============================================================================

void checkPublish() {
    for (uint8_t i = 0; i < _count; i++) {
        char* delta = nullptr;
        char* localEdit = nullptr;
        bool report = false;

        if (acquireMutex(&_mutex)) {
            delta = _shadows[i].pendingDelta;
            _shadows[i].pendingDelta = nullptr;
            localEdit = _shadows[i].pendingLocalEdit;
            _shadows[i].pendingLocalEdit = nullptr;
            releaseMutex(&_mutex);
        }
        // reportPending is a benign volatile flag; read+clear without the mutex.
        report = _shadows[i].reportPending;
        if (report) _shadows[i].reportPending = false;

        if (delta != nullptr) {
            _applyDelta(i, delta);
            free(delta);
        }
        if (localEdit != nullptr) {
            _publishLocalEditDoc(i, localEdit);
            free(localEdit);
        }
        if (report) _publishReported(i);
    }
}

// ============================================================================
// Public cross-task entry points
// ============================================================================

void requestReport(const char* name) {
    int idx = _findShadow(name, strlen(name));
    if (idx >= 0) _shadows[idx].reportPending = true; // benign volatile set
    else LOG_WARNING("requestReport for unknown shadow '%s'", name);
}

void publishLocalEdit(const char* name, JsonObjectConst changedFields) {
    int idx = _findShadow(name, strlen(name));
    if (idx < 0) {
        LOG_WARNING("publishLocalEdit for unknown shadow '%s'", name);
        return;
    }

    size_t len = measureJson(changedFields) + 1;
    char* copy = (char*)ps_malloc(len);
    if (copy == nullptr) {
        LOG_ERROR("Failed to allocate local edit buffer for shadow '%s'", name);
        return;
    }
    serializeJson(changedFields, copy, len);

    if (acquireMutex(&_mutex)) {
        if (_shadows[idx].pendingLocalEdit != nullptr) free(_shadows[idx].pendingLocalEdit);
        _shadows[idx].pendingLocalEdit = copy;
        releaseMutex(&_mutex);
    } else {
        free(copy);
        LOG_ERROR("Failed to acquire shadow mutex staging local edit for '%s'", name);
    }
}

#ifdef ENV_DEV
bool injectDelta(const char* name, const char* payload) {
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    snprintf(topic, sizeof(topic), "%s/%s/" SHADOW_NAME_PREFIX "%s/update/delta", MQTT_THINGS, DEVICE_ID, name);
    LOG_INFO("Injecting synthetic delta for shadow '%s'", name);
    return routeMessage(topic, payload);
}
#endif

// ============================================================================
// info shadow (reported-only): static device identity
// ============================================================================

static void _reportInfo(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();

    rep["firmware_version"] = FIRMWARE_BUILD_VERSION; // string literal: linked
    rep["firmware_build_date"] = FIRMWARE_BUILD_DATE;

    char md5[MD5_BUFFER_SIZE];
    snprintf(md5, sizeof(md5), "%s", ESP.getSketchMD5().c_str());
    rep["sketch_md5"] = md5; // mutable buffer: copied

    char hwProfile[VERSION_BUFFER_SIZE];
    uint8_t v = (globalHwProfile != nullptr) ? globalHwProfile->version : 0;
    snprintf(hwProfile, sizeof(hwProfile), "v%u.%u", v / 10, v % 10);
    rep["hardware_profile"] = hwProfile;

    rep["community_mode"] = globalCommunityMode;
    rep["device_id"] = DEVICE_ID;

    char serial[NAME_BUFFER_SIZE] = {0};
    char pcbRev[VERSION_BUFFER_SIZE] = {0};
    uint64_t mfgTs = 0;
    Preferences prefs;
    if (prefs.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
        prefs.getString(FACTORY_KEY_SERIAL_NUMBER, serial, sizeof(serial));
        prefs.getString(FACTORY_KEY_PCB_REVISION, pcbRev, sizeof(pcbRev));
        mfgTs = prefs.getULong64(FACTORY_KEY_MFG_TS, 0);
        prefs.end();
    }
    // Assign the bare char[] (ArduinoJson copies it). A ternary with the
    // "Unknown" literal would decay to const char* and be stored by pointer,
    // leaving a dangling pointer into this stack frame once we return.
    if (serial[0] == '\0') snprintf(serial, sizeof(serial), "Unknown");
    if (pcbRev[0] == '\0') snprintf(pcbRev, sizeof(pcbRev), "Unknown");
    rep["serial_number"] = serial;
    rep["pcb_revision"] = pcbRev;
    rep["manufacturing_unix"] = mfgTs;
}

} // namespace Shadow
