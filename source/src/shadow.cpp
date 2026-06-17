// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "shadow.h"

#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "ade7953.h"
#include "awsconfig.h"
#include "factory_keys.h"
#include "globals.h"
#include "hardware_profile.h"
#include "issueregistry.h"
#include "led.h"
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
static void _reportIssues(JsonDocument& doc);
static void _onIssuesChanged();
static void _reportSystem(JsonDocument& doc);
static bool _applySystem(JsonObjectConst delta, JsonObject reported, JsonObject desired);
static void _reportMeter(JsonDocument& doc);
static bool _applyMeter(JsonObjectConst delta, JsonObject reported, JsonObject desired);
static void _reportChannels(JsonDocument& doc);
static bool _applyChannels(JsonObjectConst delta, JsonObject reported, JsonObject desired);

// system shadow: transient (VERBOSE/DEBUG) mqtt_log_level auto-revert. The timer
// callback (esp_timer task) only flips a flag; the MQTT task does the revert +
// report (PubSubClient is single-task). _logLevelBaseline is the persisted level
// to restore to.
#define SHADOW_LOG_LEVEL_REVERT_INTERVAL_US (5ULL * 60ULL * 1000000ULL) // 5 min
static esp_timer_handle_t _logLevelRevertTimer = nullptr;
static volatile bool _logLevelRevertPending = false;
static int _logLevelBaseline = 2; // INFO
static void _logLevelRevertCallback(void* arg);
static void _checkSystemLogLevelRevert();

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
    _registerShadow({"issues", false, _reportIssues, nullptr});
    _registerShadow({"system", true, _reportSystem, _applySystem});
    _registerShadow({"meter", true, _reportMeter, _applyMeter});
    _registerShadow({"channels", true, _reportChannels, _applyChannels});

    // Refresh the issues shadow on every registry transition/ack (flag only).
    IssueRegistry::setChangeCallback(_onIssuesChanged);

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

    // One-shot timer for the transient mqtt_log_level auto-revert (created idle).
    if (_logLevelRevertTimer == nullptr) {
        const esp_timer_create_args_t revertArgs = {
            .callback = _logLevelRevertCallback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "shadow_loglevel_revert",
            .skip_unhandled_events = true};
        if (esp_timer_create(&revertArgs, &_logLevelRevertTimer) != ESP_OK) {
            LOG_WARNING("Failed to create mqtt_log_level revert timer");
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
    _checkSystemLogLevelRevert(); // transient mqtt_log_level revert (flag set by timer task)
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
    // Before begin() registers the shadows (_count == 0) callers like the
    // ADE7953 channel-config load fire this during setup; ignore quietly - the
    // shadow reports its full state on MQTT connect anyway, so a dropped early
    // flag costs nothing. After init an unknown name is a real bug, so warn.
    if (_count == 0) return;
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

// ============================================================================
// issues shadow (reported-only): runtime issue registry mirror
// ============================================================================

static void _onIssuesChanged() {
    requestReport("issues"); // flag only; the MQTT task publishes
}

static void _reportIssues(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    rep["active_count"] = IssueRegistry::activeCount();

    SpiRamAllocator allocator;
    JsonDocument tmp(&allocator);
    if (IssueRegistry::issuesToJson(tmp)) {
        rep["issues"] = tmp["issues"]; // deep-copied into doc's pool
    } else {
        rep["issues"].to<JsonArray>(); // empty array if the registry was busy
    }
}

// ============================================================================
// system shadow (writable): behavioural config + mqtt_log_level auto-revert
// ============================================================================

// esp_timer task: flag only. PubSubClient is single-task, so the MQTT task does
// the actual revert + report (in _checkSystemLogLevelRevert).
static void _logLevelRevertCallback(void* arg) {
    (void)arg;
    _logLevelRevertPending = true;
}

static void _checkSystemLogLevelRevert() {
    if (!_logLevelRevertPending) return;
    _logLevelRevertPending = false;
    Mqtt::setRuntimeLogLevel(_logLevelBaseline);
    LOG_INFO("mqtt_log_level auto-reverted to baseline %s", ShadowLogic::logLevelToString(_logLevelBaseline));
    requestReport("system");
}

static void _applyMqttLogLevel(int level) {
    if (ShadowLogic::isTransientLogLevel(level)) {
        // Capture the persisted baseline once: if the current runtime level is
        // itself transient we are already inside a window, so keep the stored
        // baseline rather than overwriting it with another transient.
        int current = Mqtt::getMqttLogLevel();
        if (!ShadowLogic::isTransientLogLevel(current)) _logLevelBaseline = current;
        _logLevelRevertPending = false;       // (re)entering transient
        Mqtt::setRuntimeLogLevel(level);       // runtime only, not persisted
        if (_logLevelRevertTimer != nullptr) {
            esp_timer_stop(_logLevelRevertTimer); // harmless if not running
            esp_timer_start_once(_logLevelRevertTimer, SHADOW_LOG_LEVEL_REVERT_INTERVAL_US);
        }
        LOG_INFO("mqtt_log_level transient %s (auto-revert in 5 min to %s)",
                 ShadowLogic::logLevelToString(level), ShadowLogic::logLevelToString(_logLevelBaseline));
    } else {
        if (_logLevelRevertTimer != nullptr) esp_timer_stop(_logLevelRevertTimer);
        _logLevelBaseline = level;    // track the new baseline so a late timer flag is a no-op
        _logLevelRevertPending = false;
        Mqtt::setMqttLogLevel(ShadowLogic::logLevelToString(level)); // persisted baseline
    }
}

static void _reportSystem(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    rep["led_brightness"] = Led::getBrightness();
    rep["send_power_data"] = Mqtt::getSendPowerData();
    rep["mqtt_log_level"] = ShadowLogic::logLevelToString(Mqtt::getMqttLogLevel());
    rep["log_level_print"] = AdvancedLogger::logLevelToString(AdvancedLogger::getPrintLevel());
    rep["log_level_save"] = AdvancedLogger::logLevelToString(AdvancedLogger::getSaveLevel());
}

static bool _applySystem(JsonObjectConst delta, JsonObject reported, JsonObject desired) {
    (void)desired; // every top-level delta key is auto-nulled by the envelope backstop
    bool applied = false;

    JsonVariantConst v = delta["led_brightness"];
    if (!v.isNull()) {
        if (v.is<int>()) {
            int b = v.as<int>();
            if (b >= 0 && b <= 0xFF && Led::isBrightnessValid((uint8_t)b)) {
                Led::setBrightness((uint8_t)b); // persists
                reported["led_brightness"] = Led::getBrightness();
                applied = true;
            } else {
                LOG_WARNING("Rejected led_brightness %d (out of range)", b);
            }
        } else {
            LOG_WARNING("Rejected led_brightness: not an integer");
        }
    }

    v = delta["send_power_data"];
    if (!v.isNull()) {
        if (v.is<bool>()) {
            bool e = v.as<bool>();
            Mqtt::setSendPowerData(e); // persists
            reported["send_power_data"] = e;
            applied = true;
        } else {
            LOG_WARNING("Rejected send_power_data: not a boolean");
        }
    }

    v = delta["mqtt_log_level"];
    if (!v.isNull()) {
        int level = ShadowLogic::logLevelFromString(v.as<const char*>());
        if (level < 0) {
            LOG_WARNING("Rejected mqtt_log_level: unknown level");
        } else {
            _applyMqttLogLevel(level);
            reported["mqtt_log_level"] = ShadowLogic::logLevelToString(Mqtt::getMqttLogLevel());
            applied = true;
        }
    }

    v = delta["log_level_print"];
    if (!v.isNull()) {
        int level = ShadowLogic::logLevelFromString(v.as<const char*>());
        if (level < 0) {
            LOG_WARNING("Rejected log_level_print: unknown level");
        } else {
            AdvancedLogger::setPrintLevel((LogLevel)level); // persists (lib-internal)
            reported["log_level_print"] = AdvancedLogger::logLevelToString(AdvancedLogger::getPrintLevel());
            applied = true;
        }
    }

    v = delta["log_level_save"];
    if (!v.isNull()) {
        int level = ShadowLogic::logLevelFromString(v.as<const char*>());
        if (level < 0) {
            LOG_WARNING("Rejected log_level_save: unknown level");
        } else {
            AdvancedLogger::setSaveLevel((LogLevel)level); // persists (lib-internal)
            reported["log_level_save"] = AdvancedLogger::logLevelToString(AdvancedLogger::getSaveLevel());
            applied = true;
        }
    }

    return applied;
}

// ============================================================================
// meter shadow (writable): ADE7953 calibration + sample time
// ============================================================================

// The 19 calibration keys (camelCase) as serialized by getConfigurationAsJson.
static const char* const METER_CAL_KEYS[] = {
    "aVGain", "aIGain", "bIGain", "aIRmsOs", "bIRmsOs",
    "aWGain", "bWGain", "aWattOs", "bWattOs",
    "aVarGain", "bVarGain", "aVarOs", "bVarOs",
    "aVaGain", "bVaGain", "aVaOs", "bVaOs",
    "phCalA", "phCalB"};
static constexpr size_t METER_CAL_KEY_COUNT = sizeof(METER_CAL_KEYS) / sizeof(METER_CAL_KEYS[0]);

static void _reportMeter(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    SpiRamAllocator allocator;
    JsonDocument cfg(&allocator);
    Ade7953::getConfigurationAsJson(cfg);
    for (JsonPairConst kv : cfg.as<JsonObjectConst>()) rep[kv.key()] = kv.value();
    rep["sample_time"] = Ade7953::getSampleTime();
}

static bool _applyMeter(JsonObjectConst delta, JsonObject reported, JsonObject desired) {
    (void)desired; // top-level delta keys auto-nulled by the envelope backstop
    bool applied = false;

    // Collect the calibration fields present in the delta into a partial config.
    SpiRamAllocator allocator;
    JsonDocument cfg(&allocator);
    for (size_t i = 0; i < METER_CAL_KEY_COUNT; i++) {
        JsonVariantConst v = delta[METER_CAL_KEYS[i]];
        if (v.isNull()) continue;
        if (v.is<int>()) {
            cfg[METER_CAL_KEYS[i]] = v.as<int32_t>();
        } else {
            LOG_WARNING("Rejected meter calibration '%s': not an integer", METER_CAL_KEYS[i]);
        }
    }
    if (cfg.size() > 0) { // at least one calibration field present
        if (Ade7953::setConfigurationFromJson(cfg, /*partial=*/true)) {
            for (JsonPairConst kv : cfg.as<JsonObjectConst>()) reported[kv.key()] = kv.value();
            applied = true;
        } else {
            LOG_WARNING("Meter calibration apply rejected by ADE7953");
        }
    }

    // sample_time is not part of the calibration struct - applied separately.
    JsonVariantConst st = delta["sample_time"];
    if (!st.isNull()) {
        if (st.is<int>() && st.as<int>() > 0 && Ade7953::setSampleTime((uint64_t)st.as<int>())) {
            reported["sample_time"] = Ade7953::getSampleTime();
            applied = true;
        } else {
            LOG_WARNING("Rejected sample_time (not a positive integer >= minimum)");
        }
    }

    return applied;
}

// ============================================================================
// channels shadow (writable): per-channel config, object-keyed by index
// ============================================================================

// Overlay src onto dst, recursing into nested objects (e.g. ctSpecification) so
// a delta touching one subfield does not drop the channel's other CT params.
static void _deepMerge(JsonObject dst, JsonObjectConst src) {
    for (JsonPairConst kv : src) {
        if (kv.value().is<JsonObjectConst>()) {
            JsonObject child = dst[kv.key()].is<JsonObject>() ? dst[kv.key()].as<JsonObject>()
                                                              : dst[kv.key()].to<JsonObject>();
            _deepMerge(child, kv.value().as<JsonObjectConst>());
        } else {
            dst[kv.key()] = kv.value();
        }
    }
}

static void _reportChannels(JsonDocument& doc) {
    JsonObject rep = doc["state"]["reported"].to<JsonObject>();
    char key[4]; // up to "16" + null
    for (uint8_t i = 0; i < globalHwProfile->totalChannelCount; i++) {
        SpiRamAllocator allocator;
        JsonDocument ch(&allocator);
        if (!Ade7953::getChannelDataAsJson(ch, i)) continue;
        ch.remove("index"); // index is the object key, not a field
        snprintf(key, sizeof(key), "%u", i);
        rep[key] = ch; // object-keyed so the cloud can patch one channel
    }
}

static bool _applyChannels(JsonObjectConst delta, JsonObject reported, JsonObject desired) {
    (void)desired; // each top-level (channel) key auto-nulled by the envelope backstop
    bool applied = false;
    uint8_t channelCount = globalHwProfile->totalChannelCount;

    for (JsonPairConst kv : delta) {
        uint8_t idx = 0;
        if (!ShadowLogic::parseChannelIndex(kv.key().c_str(), channelCount, &idx)) {
            LOG_WARNING("Rejected channel key '%s' (not a valid index)", kv.key().c_str());
            continue;
        }
        if (!kv.value().is<JsonObjectConst>()) {
            LOG_WARNING("Rejected channel %u delta (value is not an object)", idx);
            continue;
        }

        // Start from the current channel, overlay the changed subfields, apply.
        SpiRamAllocator mergeAlloc;
        JsonDocument merged(&mergeAlloc);
        if (!Ade7953::getChannelDataAsJson(merged, idx)) {
            LOG_WARNING("Failed to read channel %u for merge", idx);
            continue;
        }
        JsonObject mergedObj = merged.as<JsonObject>();
        _deepMerge(mergedObj, kv.value().as<JsonObjectConst>());
        merged["index"] = idx; // setChannelDataFromJson keys off this; never let a delta change it

        bool roleChanged = false;
        if (!Ade7953::setChannelDataFromJson(merged, /*partial=*/true, &roleChanged)) {
            LOG_WARNING("Channel %u apply rejected by ADE7953", idx);
            continue;
        }

        // Echo the channel as actually applied (e.g. channel 0 stays active even
        // if a delta tried to disable it), keyed by index, index field dropped.
        SpiRamAllocator afterAlloc;
        JsonDocument after(&afterAlloc);
        if (Ade7953::getChannelDataAsJson(after, idx)) {
            after.remove("index");
            char key[4];
            snprintf(key, sizeof(key), "%u", idx);
            reported[key] = after;
        }
        applied = true;
    }

    return applied;
}

} // namespace Shadow
