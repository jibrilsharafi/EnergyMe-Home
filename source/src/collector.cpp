// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "collector.h"

#include <atomic>

#include <AdvancedLogger.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#include "constants.h"
#include "customtime.h"
#include "gateway.h"
#include "gateway_executor.h"
#include "globals.h"
#include "secrets.h"
#include "tunnel.h"
#include "utils.h"

using namespace CollectorLogic;

namespace Collector
{
    // Switch and cross-task flags
    // ===========================

    static std::atomic<bool> _enabled{false};
    static std::atomic<bool> _otaBusy{false};
    static std::atomic<bool> _cloudConnected{false};
    static char _boot[17] = ""; // dedup id, new at every boot

    static SemaphoreHandle_t _makeMutex() { return xSemaphoreCreateMutex(); }
    static SemaphoreHandle_t _mailboxMutex() {
        static SemaphoreHandle_t m = _makeMutex();
        return m;
    }
    static SemaphoreHandle_t _snapshotMutex() {
        static SemaphoreHandle_t m = _makeMutex();
        return m;
    }
    static SemaphoreHandle_t _queueMutex() {
        static SemaphoreHandle_t m = _makeMutex();
        return m;
    }
    static bool _take(SemaphoreHandle_t m, uint32_t ms = 1000) { return m != nullptr && xSemaphoreTake(m, pdMS_TO_TICKS(ms)) == pdTRUE; }
    static void _give(SemaphoreHandle_t m) { xSemaphoreGive(m); }

    static uint64_t _uptimeS() { return millis64() / 1000; }

    static void _sha256(const uint8_t* data, size_t length, uint8_t out[32]) { mbedtls_sha256(data, length, out, 0); }

    static int _targetCheck(const uint8_t octets[4]) {
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
        uint8_t parsed[4];
        switch (Gateway::validateTarget(ip, parsed)) {
            case Gateway::TargetCheck::NOT_PRIVATE: return 1;
            case Gateway::TargetCheck::NOT_ALLOWED: return 2;
            default: return 0;
        }
    }

    // Tables
    // ======

    struct Table {
        JobTable* table;
        char* pool;
    };

    static Table _newTable() {
        Table t = {(JobTable*)ps_calloc(1, sizeof(JobTable)), (char*)ps_calloc(1, POOL_BYTES)};
        if (t.table == nullptr || t.pool == nullptr) {
            free(t.table);
            free(t.pool);
            return {nullptr, nullptr};
        }
        t.table->pool = t.pool;
        t.table->poolCap = POOL_BYTES;
        return t;
    }

    static void _freeTable(Table& t) {
        if (t.pool != nullptr) mbedtls_platform_zeroize(t.pool, POOL_BYTES); // vars may hold nothing secret, but cheap
        free(t.pool);
        free(t.table);
        t = {nullptr, nullptr};
    }

    // MQTT task: what is applied (persisted), for same-hash and stale checks.
    struct Applied {
        uint32_t rev;
        bool hasSha;
        uint8_t sha[32];
        // apply_error, always reported with all four keys
        bool hasError;
        uint32_t errorRev;
        char errorCode[CODE_MAX + 1];
        char errorJob[ID_MAX + 1];
        char errorField[48];
    };
    static Applied _applied = {};

    // Mailbox: MQTT task posts, gateway task adopts between steps.
    static Table _pending = {nullptr, nullptr};

    // Gateway task only.
    static Table _active = {nullptr, nullptr};

    // Out-queue
    // =========

    static PartQueue _queue;
    static uint8_t* _ring = nullptr;
    static uint64_t _ringRetryMs = 0;
    static uint64_t _lastDrainMs = 0;
    static TokenBucket _bucket = {};

    // Parts of a run whose cursor waits on publish, per job slot (guarded by the queue mutex).
    struct Pending {
        bool active;
        uint32_t seq;
        uint32_t outstanding;
        bool dropped;
    };
    static Pending _pendingParts[JOBS_MAX] = {};

    // Called with the queue mutex held (push from the gateway task, endMessage from the MQTT task).
    static void _onPart(const PartTag& tag, bool published, void* context) {
        (void)context;
        if (tag.job >= JOBS_MAX) return;
        Pending& p = _pendingParts[tag.job];
        if (!p.active || p.seq != tag.seq) return;
        if (!published) p.dropped = true;
        if (p.outstanding > 0) p.outstanding--;
    }

    // Called from the gateway, MQTT and REST tasks: check and allocation under the queue mutex.
    static bool _allocateRing() {
        if (!_take(_queueMutex())) return false;
        bool ready = _ring != nullptr;
        if (!ready && millis64() >= _ringRetryMs) {
            _ring = (uint8_t*)ps_malloc(COLLECTOR_RING_BYTES);
            if (_ring != nullptr) _queue.init(_ring, COLLECTOR_RING_BYTES);
            else _ringRetryMs = millis64() + COLLECTOR_RING_RETRY_MS;
            ready = _ring != nullptr;
            if (!ready) LOG_WARNING("Collector out-queue (256 KB) not allocated, retrying in 60 s");
        }
        _give(_queueMutex());
        return ready;
    }

    // Health snapshot (gateway task writes, MQTT task reads)
    // ======================================================

    struct JobSnapshot {
        char id[ID_MAX + 1];
        uint32_t rev;
        Health health;
        uint32_t runs;
        uint32_t fails;
        uint64_t lastOkS;
        uint64_t lastErrS;
        char lastErr[CODE_MAX + 1];
        char lastErrDetail[CODE_MAX + 1];
        uint64_t nextRunS; // uptime
        bool cursorSet;
        uint64_t cursorS;
        uint32_t lastDurMs;
        uint32_t lastBytes;
    };

    struct Snapshot {
        uint8_t count;
        bool runActive;
        uint32_t budgetLimitH;
        uint32_t budgetTokens;
        JobSnapshot jobs[JOBS_MAX];
    };

    static Snapshot _snapshot = {};       // live, under the snapshot mutex
    static Snapshot _reportedJobs = {};   // what the shadow last reported (MQTT task)
    static std::atomic<bool> _healthChanged{false};
    static std::atomic<bool> _urgentReport{false};
    static uint64_t _lastHealthReportS = 0;

    // Run state (gateway task)
    // ========================

    struct JobState {
        uint64_t nextS; // uptime seconds
        uint64_t t0S;
        uint32_t failures; // consecutive, for backoff
        bool sessionHeld;
        uint64_t loginAtS;
        uint8_t* captures[CAPTURES_MAX];
        uint16_t captureLength[CAPTURES_MAX];
        bool cursorSet;
        uint64_t cursorS; // device UTC
        uint32_t windowS;
        uint32_t seq;
        uint64_t advanceTo; // cursor value waiting for the publish of run `advanceSeq`
        bool advancePending;
        JobSnapshot stats;
    };
    static JobState _states[JOBS_MAX] = {};

    struct StagedPart {
        char* header;
        size_t headerLength;
        uint8_t* payload;
        size_t payloadLength;
        PartTag tag;
    };

    struct Run {
        bool active;
        uint8_t job;
        uint32_t seq;
        uint64_t startMs;
        uint32_t excludedMs;
        uint64_t nowS;  // device UTC at run start, the window end unless capped
        Window window;
        bool login;
        uint8_t index;
        bool retrying;     // re-login after an expiry, then retry `retryStep`
        uint8_t retryStep;
        uint8_t okSteps;
        bool stopped;
        bool failedStep;
        char code[CODE_MAX + 1];
        char detail[CODE_MAX + 1];
        bool truncated;
        uint32_t body;
        uint32_t bytes;
        uint8_t staged;
        StagedPart stage[STEPS_MAX + LOGIN_MAX + 8];
    };
    static Run _run = {};

    static void _zeroCaptures(JobState& js) {
        for (size_t c = 0; c < CAPTURES_MAX; c++) {
            if (js.captures[c] != nullptr) {
                mbedtls_platform_zeroize(js.captures[c], js.captureLength[c]);
                free(js.captures[c]);
            }
            js.captures[c] = nullptr;
            js.captureLength[c] = 0;
        }
        js.sessionHeld = false;
    }

    static void _freeStage() {
        for (size_t i = 0; i < _run.staged; i++) {
            free(_run.stage[i].header);
            free(_run.stage[i].payload);
        }
        _run.staged = 0;
    }

    static void _publishSnapshot() {
        uint32_t limitH = 0, tokens = 0;
        if (_take(_queueMutex(), 50)) {
            limitH = _bucket.limitH;
            tokens = _bucket.tokens > 0 ? (uint32_t)_bucket.tokens : 0;
            _give(_queueMutex());
        }
        if (!_take(_snapshotMutex(), 50)) return;
        uint8_t count = _active.table != nullptr ? _active.table->jobCount : 0;
        _snapshot.count = count;
        _snapshot.runActive = _run.active;
        _snapshot.budgetLimitH = limitH;
        _snapshot.budgetTokens = tokens;
        for (uint8_t i = 0; i < count; i++) _snapshot.jobs[i] = _states[i].stats;
        _give(_snapshotMutex());
    }

    // Health transitions; `changed` marks a report as due within 300 s.
    static void _noteHealth(bool changed) {
        if (changed) _healthChanged = true;
    }

    // Persistence
    // ===========

    static bool _persist(uint32_t rev, const char* shaHex, const char* doc, size_t length) {
        SpiRamAllocator allocator;
        JsonDocument file(&allocator);
        file["rev"] = rev;
        file["sha256"] = shaHex;
        file["doc"] = JsonString(doc, length);
        if (!LittleFS.exists(COLLECTOR_DIR)) LittleFS.mkdir(COLLECTOR_DIR);
        File out = LittleFS.open(COLLECTOR_JOBSET_TMP_PATH, FILE_WRITE);
        if (!out) return false;
        size_t written = serializeJson(file, out);
        out.flush();
        out.close();
        if (written == 0) return false;
        return LittleFS.rename(COLLECTOR_JOBSET_TMP_PATH, COLLECTOR_JOBSET_PATH); // lfs_rename replaces the old file atomically
    }

    static void _setError(uint32_t rev, const char* code, const char* job, const char* field) {
        _applied.hasError = code != nullptr;
        _applied.errorRev = rev;
        snprintf(_applied.errorCode, sizeof(_applied.errorCode), "%s", code ? code : "");
        snprintf(_applied.errorJob, sizeof(_applied.errorJob), "%s", job ? job : "");
        snprintf(_applied.errorField, sizeof(_applied.errorField), "%s", field ? field : "");
    }

    static void _post(Table& table) {
        if (!_take(_mailboxMutex())) {
            _freeTable(table);
            return;
        }
        if (_pending.table != nullptr) _freeTable(_pending); // a newer post replaces an unadopted one
        _pending = table;
        table = {nullptr, nullptr};
        _give(_mailboxMutex());
    }

    // Validates `doc` into a new table. False with `error` set.
    static bool _validate(const char* doc, size_t length, bool classify, Table* out, ApplyError* error) {
        *out = _newTable();
        if (out->table == nullptr) {
            error->code = "PERSIST_FAILED";
            return false;
        }
        GatewayLogic::GrantEntry* grants = (GatewayLogic::GrantEntry*)ps_calloc(GatewayLogic::GRANT_MAX_ENTRIES, sizeof(GatewayLogic::GrantEntry));
        size_t grantCount = grants != nullptr ? GatewayExec::copyGrants(grants) : 0;
        SpiRamAllocator allocator;
        ValidationContext context = {grants, grantCount, _sha256, _targetCheck, GatewayExec::httpsAvailable(), classify, &allocator};
        bool ok = validateJobSet(doc, length, context, out->table, error);
        free(grants);
        if (!ok) _freeTable(*out);
        return ok;
    }

    static void _loadStored() {
        File in = LittleFS.open(COLLECTOR_JOBSET_PATH, FILE_READ);
        if (!in) return; // no stored set: no jobs, rev 0
        SpiRamAllocator allocator;
        JsonDocument file(&allocator);
        bool parsed = !deserializeJson(file, in);
        in.close();
        uint8_t sha[32], stored[32];
        JsonString doc = file["doc"].as<JsonString>();
        bool ok = parsed && file["rev"].is<uint32_t>() && GatewayLogic::parseHexDigest(file["sha256"] | (const char*)nullptr, stored) &&
                  !doc.isNull() && doc.size() <= DOC_MAX;
        if (ok) {
            _sha256((const uint8_t*)doc.c_str(), doc.size(), sha);
            ok = GatewayLogic::digestEquals(sha, stored);
        }
        Table table = {nullptr, nullptr};
        ApplyError error = {};
        if (ok) ok = _validate(doc.c_str(), doc.size(), false, &table, &error);
        if (!ok) {
            LOG_WARNING("Stored collector job set is unusable (BAD_STORED_SET), no job runs");
            _applied.rev = 0;
            _applied.hasSha = false;
            _setError(0, "BAD_STORED_SET", nullptr, nullptr);
            return;
        }
        table.table->rev = file["rev"].as<uint32_t>();
        memcpy(table.table->sha256, sha, 32);
        _applied.rev = table.table->rev;
        _applied.hasSha = true;
        memcpy(_applied.sha, sha, 32);
        LOG_INFO("Collector job set rev %u loaded: %u jobs", (unsigned)_applied.rev, (unsigned)table.table->jobCount);
        _post(table);
    }

    void begin() {
        generateHexToken(_boot, sizeof(_boot));
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, true)) {
            _enabled = preferences.getBool(COLLECTOR_ENABLED_KEY, false);
            preferences.end();
        }
        _loadStored();
        if (_enabled) _allocateRing(); // at boot, before PSRAM fragments
        bucketInit(&_bucket, BUDGET_DEFAULT, millis64());
        LOG_INFO("Collector ready (%s)", _enabled ? "enabled" : "disabled");
    }

    bool isEnabled() { return _enabled; }

    bool setEnabled(bool enabled) {
        Preferences preferences;
        bool ok = preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false) && preferences.putBool(COLLECTOR_ENABLED_KEY, enabled) > 0;
        preferences.end();
        if (!ok) {
            LOG_ERROR("Failed to persist the collector switch");
            return false;
        }
        if (_enabled != enabled) LOG_INFO("Collector %s", enabled ? "enabled" : "disabled");
        _enabled = enabled;
        if (enabled) _allocateRing();
        _urgentReport = true;
        return true;
    }

    void setOtaBusy(bool busy) { _otaBusy = busy; }
    void setCloudConnected(bool connected) { _cloudConnected = connected; }

    // Shadow (MQTT task)
    // ==================

    static void _reportError(JsonObject reported) {
        if (!_applied.hasError) {
            reported["apply_error"] = nullptr;
            return;
        }
        JsonObject e = reported["apply_error"].to<JsonObject>();
        e["rev"] = _applied.errorRev;
        e["code"] = _applied.errorCode;
        if (_applied.errorJob[0] != '\0') e["job"] = _applied.errorJob;
        else e["job"] = nullptr;
        if (_applied.errorField[0] != '\0') e["field"] = _applied.errorField;
        else e["field"] = nullptr;
    }

    void reportToJson(JsonObject reported) {
        reported["enabled"] = (bool)_enabled;
        reported["rev"] = _applied.rev;
        if (_applied.hasSha) {
            char hex[65];
            GatewayLogic::formatHexDigest(_applied.sha, hex, sizeof(hex));
            reported["sha256"] = hex;
        } else {
            reported["sha256"] = nullptr;
        }
        _reportError(reported);

        // Health changes are coalesced to one report per 300 s; apply results and switches go at once.
        uint64_t nowS = _uptimeS();
        bool urgent = _urgentReport.exchange(false);
        if (reportDue(_healthChanged, urgent, _lastHealthReportS, nowS)) {
            if (_take(_snapshotMutex(), 50)) {
                _reportedJobs = _snapshot;
                _give(_snapshotMutex());
            }
            _healthChanged = false;
            _lastHealthReportS = nowS;
        }
        JsonArray jobs = reported["jobs"].to<JsonArray>();
        for (uint8_t i = 0; i < _reportedJobs.count; i++) {
            const JobSnapshot& j = _reportedJobs.jobs[i];
            JsonObject e = jobs.add<JsonObject>();
            e["id"] = j.id;
            e["rev"] = j.rev;
            e["st"] = healthStateName(j.health.st);
            e["since"] = j.health.since;
            if (j.health.code[0] != '\0') e["code"] = j.health.code;
            else e["code"] = nullptr;
            if (j.health.detail[0] != '\0') e["detail"] = j.health.detail;
            else e["detail"] = nullptr;
        }
    }

    // Applies a `jobset` delta. Fills the error state; true when the applied set changed.
    static bool _applyJobset(JsonObjectConst jobset) {
        int64_t rev = jobset["rev"] | (int64_t)-1;
        const char* shaHex = jobset["sha256"] | (const char*)nullptr;
        JsonString doc = jobset["doc"].as<JsonString>();
        uint8_t sha[32], claimed[32];
        uint32_t newRev = rev > 0 && rev <= 0x7FFFFFFF ? (uint32_t)rev : 0;
        if (newRev == 0 || doc.isNull() || !GatewayLogic::parseHexDigest(shaHex, claimed)) {
            _setError(newRev, "BAD_JOB", nullptr, newRev == 0 ? "rev" : (doc.isNull() ? "doc" : "sha256"));
            return false;
        }
        if (doc.size() > DOC_MAX) {
            _setError(newRev, "TOO_LARGE", nullptr, nullptr);
            return false;
        }
        _sha256((const uint8_t*)doc.c_str(), doc.size(), sha);
        if (!GatewayLogic::digestEquals(sha, claimed)) {
            _setError(newRev, "BAD_HASH", nullptr, nullptr);
            return false;
        }
        if (_applied.hasSha && GatewayLogic::digestEquals(sha, _applied.sha)) {
            // Same content: no restart; a higher rev is persisted so it never goes back after a reboot.
            if (newRev > _applied.rev) {
                if (!_persist(newRev, shaHex, doc.c_str(), doc.size())) {
                    _setError(newRev, "PERSIST_FAILED", nullptr, nullptr);
                    return false;
                }
                _applied.rev = newRev;
                if (_take(_mailboxMutex())) {
                    if (_pending.table != nullptr) _pending.table->rev = newRev;
                    _give(_mailboxMutex());
                }
                _setError(0, nullptr, nullptr, nullptr);
                return true;
            }
            return false;
        }
        if (newRev <= _applied.rev) {
            _setError(newRev, "STALE_REV", nullptr, nullptr);
            return false;
        }
        Table table = {nullptr, nullptr};
        ApplyError error = {};
        if (!_validate(doc.c_str(), doc.size(), true, &table, &error)) {
            _setError(newRev, error.code ? error.code : "BAD_JOB", error.job, error.field);
            LOG_WARNING("Collector job set rev %u refused: %s %s %s", (unsigned)newRev, error.code ? error.code : "", error.job, error.field);
            return false;
        }
        if (!_persist(newRev, shaHex, doc.c_str(), doc.size())) {
            _freeTable(table);
            _setError(newRev, "PERSIST_FAILED", nullptr, nullptr);
            return false;
        }
        table.table->rev = newRev;
        memcpy(table.table->sha256, sha, 32);
        LOG_INFO("Collector job set rev %u applied: %u jobs", (unsigned)newRev, (unsigned)table.table->jobCount);
        _post(table);
        _applied.rev = newRev;
        _applied.hasSha = true;
        memcpy(_applied.sha, sha, 32);
        _setError(0, nullptr, nullptr, nullptr);
        return true;
    }

    bool applyDelta(JsonObjectConst delta, JsonObject reported) {
        bool applied = false;
        JsonVariantConst v = delta["enabled"];
        if (!v.isNull()) {
            if (v.is<bool>()) applied = setEnabled(v.as<bool>()) || applied;
            else LOG_WARNING("Rejected collector enabled: not a boolean");
        }
        v = delta["jobset"];
        if (!v.isNull()) {
            if (v.is<JsonObjectConst>()) _applyJobset(v.as<JsonObjectConst>());
            else _setError(0, "BAD_JOB", nullptr, "jobset");
            applied = true; // apply_error or the new rev is reported at once
        }
        _urgentReport = true;
        reportToJson(reported);
        return applied;
    }

    // collector_status (MQTT task)
    // ============================

    bool statusToJson(const char* job, JsonObject out) {
        Snapshot* snap = (Snapshot*)ps_malloc(sizeof(Snapshot));
        if (snap == nullptr || !_take(_snapshotMutex(), 100)) {
            free(snap);
            return false;
        }
        *snap = _snapshot;
        _give(_snapshotMutex());
        int only = -1;
        if (job != nullptr) {
            for (uint8_t i = 0; i < snap->count && only < 0; i++) {
                if (strcmp(snap->jobs[i].id, job) == 0) only = i;
            }
            if (only < 0) {
                free(snap);
                return false;
            }
        }
        out["enabled"] = (bool)_enabled;
        out["active"] = snap->runActive;
        out["rev"] = _applied.rev;
        out["boot"] = _boot;
        JsonObject queue = out["queue"].to<JsonObject>();
        if (_take(_queueMutex(), 100)) {
            queue["bytes"] = _queue.ready() ? _queue.bytes() : 0;
            queue["parts"] = _queue.ready() ? _queue.parts() : 0;
            queue["dropped_parts"] = _queue.ready() ? _queue.droppedParts() : 0;
            queue["dropped_bytes"] = _queue.ready() ? _queue.droppedBytes() : 0;
            _give(_queueMutex());
        }
        JsonObject budget = out["budget"].to<JsonObject>();
        budget["limit_h"] = snap->budgetLimitH;
        budget["tokens"] = snap->budgetTokens;
        JsonArray jobs = out["jobs"].to<JsonArray>();
        uint64_t upS = _uptimeS();
        for (uint8_t i = 0; i < snap->count; i++) {
            if (only >= 0 && i != only) continue;
            const JobSnapshot& j = snap->jobs[i];
            JsonObject e = jobs.add<JsonObject>();
            e["id"] = j.id;
            e["rev"] = j.rev;
            e["st"] = healthStateName(j.health.st);
            e["code"] = j.health.code[0] ? j.health.code : nullptr;
            e["last_ok"] = j.lastOkS;
            e["last_err"] = j.lastErr[0] ? j.lastErr : nullptr;
            e["last_err_detail"] = j.lastErrDetail[0] ? j.lastErrDetail : nullptr;
            e["last_err_ts"] = j.lastErrS;
            e["runs"] = j.runs;
            e["fails"] = j.fails;
            e["consec_fails"] = j.health.consecutiveFailures;
            e["next_run"] = j.nextRunS > upS ? j.nextRunS - upS : 0; // seconds from now
            e["last_dur_ms"] = j.lastDurMs;
            e["last_bytes"] = j.lastBytes;
            if (j.cursorSet) e["cursor"] = j.cursorS;
            else e["cursor"] = nullptr;
        }
        free(snap);
        return true;
    }

    bool statusToResult(const char* job, JsonObject result) {
        SpiRamAllocator allocator;
        JsonDocument lists(&allocator);
        if (!statusToJson(job, lists.to<JsonObject>())) return false;
        JsonArray jobs = lists["jobs"].as<JsonArray>();
        auto setText = [&](const char* key, JsonVariantConst value) {
            size_t length = measureJson(value) + 1;
            char* text = (char*)ps_malloc(length);
            if (text == nullptr) return;
            serializeJson(value, text, length);
            result[key]["s"] = (const char*)text;
            free(text);
        };
        result["enabled"]["b"] = lists["enabled"].as<bool>();
        result["active"]["b"] = lists["active"].as<bool>();
        char number[16];
        snprintf(number, sizeof(number), "%u", lists["rev"].as<unsigned>());
        result["rev"]["s"] = number;
        result["boot"]["s"] = _boot;
        setText("queue", lists["queue"]);
        setText("budget", lists["budget"]);
        // The cut applies to the whole result as published (the jobs text is escaped inside it).
        result["truncated"]["b"] = false;
        setText("jobs", lists["jobs"]);
        for (uint32_t loops = 0; measureJson(result) > COLLECTOR_STATUS_MAX_BYTES && jobs.size() > 0 && loops < JOBS_MAX; loops++) {
            jobs.remove(jobs.size() - 1);
            setText("jobs", lists["jobs"]);
            result["truncated"]["b"] = true;
        }
        return true;
    }

    // Drain (MQTT task)
    // =================

    bool drainAllowed() {
        if (Tunnel::isActive() || _otaBusy) return false;
        return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= COLLECTOR_LOW_HEAP_FREE &&
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= COLLECTOR_LOW_HEAP_BLOCK;
    }

    void drain(bool (*publish)(const Message& message, void* context), void* context) {
        if (_ring == nullptr || millis64() - _lastDrainMs < COLLECTOR_DRAIN_INTERVAL_MS || !drainAllowed()) return;
        PartQueue::View view;
        if (!_take(_queueMutex(), 20)) return;
        bool have = _queue.beginMessage(&view);
        _give(_queueMutex());
        if (!have) return;
        _lastDrainMs = millis64();
        Message message = {view.sourceId, view.bytes, view.count, {}, {}, {}};
        for (size_t i = 0; i < view.count && i < 16; i++) {
            message.parts[i] = view.parts[i];
            message.partBytes[i] = view.partBytes[i];
            message.headerLength[i] = view.headerLength[i];
        }
        // Pinned parts never move, so the stream reads them without the mutex.
        bool published = publish(message, context);
        if (_take(_queueMutex(), 1000)) {
            _queue.endMessage(published, _onPart, nullptr);
            if (published) bucketSpend(&_bucket, view.bytes);
            _give(_queueMutex());
        }
    }

    // Resolvers (gateway task, per step)
    // ==================================

    struct CapToken {
        uint8_t capture;
        uint8_t* encoded;
        size_t encodedLength;
    };

    struct StepContext {
        const Job* job;
        JobState* state;
        int64_t nowSource;
        int64_t cursorSource;
        bool hasCursor;
        uint8_t tokenCount;
        CapToken tokens[CAPTURES_MAX * 2];
    };

    static int _captureIndex(const Job& job, const char* name) {
        for (uint8_t c = 0; c < job.captureCount; c++) {
            if (strcmp(job.captures[c].name, name) == 0) return c;
        }
        return -1;
    }

    static GatewayLogic::ResolveStatus _resolve(void* context, const GatewayLogic::ResolveContext& request, const char* name, const char* arg,
                                                GatewayLogic::ResolveOutput* out) {
        (void)request;
        StepContext* ctx = (StepContext*)context;
        int n = -1;
        if (strcmp(name, "now") == 0 || strcmp(name, "cursor") == 0) {
            bool ms = strcmp(arg, "ms") == 0;
            if (arg[0] != '\0' && !ms) return GatewayLogic::ResolveStatus::UNKNOWN;
            int64_t value = name[0] == 'n' ? ctx->nowSource : ctx->cursorSource;
            n = ms ? snprintf((char*)out->value, out->cap, "%lld000", (long long)value) : snprintf((char*)out->value, out->cap, "%lld", (long long)value);
        } else if (strcmp(name, "var") == 0) {
            for (uint8_t v = 0; v < ctx->job->varCount && n < 0; v++) {
                if (strcmp(ctx->job->vars[v].name, arg) == 0) n = snprintf((char*)out->value, out->cap, "%s", ctx->job->vars[v].text);
            }
        } else if (strcmp(name, "cap") == 0) {
            int c = _captureIndex(*ctx->job, arg);
            if (c < 0 || ctx->state->captures[c] == nullptr) {
                out->reasonCode = "CAPTURE_MISSING";
                snprintf(out->reasonDetail, sizeof(out->reasonDetail), "No captured value");
                return GatewayLogic::ResolveStatus::FAILED;
            }
            if (ctx->state->captureLength[c] > out->cap) return GatewayLogic::ResolveStatus::FAILED;
            memcpy(out->value, ctx->state->captures[c], ctx->state->captureLength[c]);
            out->length = ctx->state->captureLength[c];
            out->sensitive = true;
            return GatewayLogic::ResolveStatus::OK;
        } else {
            return GatewayLogic::ResolveStatus::UNKNOWN; // falls through to the secret resolver
        }
        if (n < 0 || (size_t)n >= out->cap) return GatewayLogic::ResolveStatus::FAILED;
        out->length = (size_t)n;
        return GatewayLogic::ResolveStatus::OK;
    }

    // Records the form of each expanded capture actually sent, for the capture scrub.
    static bool _expanded(void* context, const char* name, const char* arg, const uint8_t* bytes, size_t length, bool final) {
        StepContext* ctx = (StepContext*)context;
        if (!final || strcmp(name, "cap") != 0) return true;
        if (ctx->tokenCount >= sizeof(ctx->tokens) / sizeof(ctx->tokens[0])) return false;
        CapToken& t = ctx->tokens[ctx->tokenCount];
        t.encoded = (uint8_t*)ps_malloc(length);
        if (t.encoded == nullptr) return false;
        memcpy(t.encoded, bytes, length);
        t.encodedLength = length;
        t.capture = (uint8_t)_captureIndex(*ctx->job, arg);
        ctx->tokenCount++;
        return true;
    }

    static void _freeTokens(StepContext& ctx) {
        for (uint8_t i = 0; i < ctx.tokenCount; i++) {
            mbedtls_platform_zeroize(ctx.tokens[i].encoded, ctx.tokens[i].encodedLength);
            free(ctx.tokens[i].encoded);
        }
        ctx.tokenCount = 0;
    }

    // Parts
    // =====

    // Case-insensitive header lookup in a raw header block; copies the trimmed value.
    static bool _headerValue(const char* block, size_t length, const char* name, char* out, size_t cap) {
        size_t nameLength = strlen(name);
        const char* end = block + length;
        const char* line = block;
        for (uint32_t loops = 0; line < end && loops < 256; loops++) {
            const char* eol = (const char*)memchr(line, '\n', (size_t)(end - line));
            if (eol == nullptr) eol = end;
            if ((size_t)(eol - line) > nameLength && line[nameLength] == ':' && strncasecmp(line, name, nameLength) == 0) {
                const char* v = line + nameLength + 1;
                while (v < eol && (*v == ' ' || *v == '\t')) v++;
                const char* ve = eol;
                while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ')) ve--;
                snprintf(out, cap, "%.*s", (int)(ve - v), v);
                return true;
            }
            line = eol + 1;
        }
        return false;
    }

    static bool _enqueue(const char* sourceId, const PartTag& tag, const char* header, size_t headerLength, const uint8_t* payload,
                         size_t payloadLength) {
        if (!_take(_queueMutex(), 1000)) return false;
        bool ok = _queue.push(sourceId, tag, header, headerLength, payload, payloadLength, _onPart, nullptr);
        _give(_queueMutex());
        return ok;
    }

    // Queues (or stages, for `publish: batch`) every chunk of one part.
    static void _emitPart(const Job& job, uint8_t jobIndex, PartHeader& h, const uint8_t* body, size_t total) {
        static char header[PART_HEADER_MAX + 1]; // gateway task only
        uint32_t chunks = chunkCount(total);
        if (body != nullptr) h.total = (uint32_t)total; // header-only parts keep the caller's total (login replies)
        h.n = chunks;
        h.dq = _queue.ready() ? _queue.droppedParts() : 0;
        PartTag tag = {jobIndex, _run.seq, _run.body++};
        for (uint32_t i = 0; i < chunks; i++) {
            size_t offset = (size_t)i * CHUNK_BYTES;
            size_t length = total > offset ? (total - offset < CHUNK_BYTES ? total - offset : CHUNK_BYTES) : 0;
            h.i = i + 1;
            h.off = (uint32_t)offset;
            h.len = (uint32_t)length;
            size_t headerLength = encodePartHeader(h, header, sizeof(header));
            if (headerLength == 0) {
                LOG_WARNING("Collector part header for %s does not fit", job.id);
                return;
            }
            if (_states[jobIndex].advancePending || job.usesCursor) {
                if (_take(_queueMutex(), 1000)) {
                    _pendingParts[jobIndex].outstanding++;
                    _give(_queueMutex());
                }
            }
            if (job.batch && _run.staged < sizeof(_run.stage) / sizeof(_run.stage[0])) {
                StagedPart& s = _run.stage[_run.staged];
                s.header = (char*)ps_malloc(headerLength);
                s.payload = length > 0 ? (uint8_t*)ps_malloc(length) : nullptr;
                if (s.header != nullptr && (length == 0 || s.payload != nullptr)) {
                    memcpy(s.header, header, headerLength);
                    if (length > 0) memcpy(s.payload, body + offset, length);
                    s.headerLength = headerLength;
                    s.payloadLength = length;
                    s.tag = tag;
                    _run.staged++;
                    continue;
                }
                free(s.header);
                free(s.payload);
            }
            _enqueue(job.sourceId, tag, header, headerLength, body + offset, length);
        }
        _run.bytes += (uint32_t)total;
    }

    static void _flushStage(const Job& job) {
        for (uint8_t i = 0; i < _run.staged; i++) {
            StagedPart& s = _run.stage[i];
            _enqueue(job.sourceId, s.tag, s.header, s.headerLength, s.payload, s.payloadLength);
        }
        _freeStage();
    }

    static void _baseHeader(const Job& job, const Step& step, bool login, PartHeader* h) {
        memset(h, 0, sizeof(*h));
        h->job = job.id;
        h->rev = job.rev;
        h->set = _active.table->rev;
        h->src = job.sourceId;
        h->boot = _boot;
        h->seq = _run.seq;
        h->step = step.id;
        h->login = login;
        h->module = job.module;
        h->st = "OK";
        h->tsKnown = CustomTime::isTimeSynched();
        h->tsMs = h->tsKnown ? CustomTime::getUnixTimeMilliseconds() : 0;
        h->upMs = millis64();
        h->hasCo = true;
        h->co = job.clockOffsetS;
        h->hasWin = job.usesTime;
        if (job.usesTime) {
            h->winFrom = toSourceClock(_run.window.from, job.clockOffsetS);
            h->winTo = toSourceClock(_run.window.to, job.clockOffsetS);
        }
    }

    // Run control (gateway task)
    // ==========================

    static void _fail(const char* code, const char* detail) {
        if (_run.code[0] == '\0') {
            snprintf(_run.code, sizeof(_run.code), "%s", code ? code : "");
            snprintf(_run.detail, sizeof(_run.detail), "%s", detail != nullptr && isCodeToken(detail) ? detail : "");
        }
    }

    static void _scheduleNext(uint8_t jobIndex, bool failed) {
        const Job& job = _active.table->jobs[jobIndex];
        JobState& js = _states[jobIndex];
        uint64_t nowS = _uptimeS();
        uint32_t jitter = job.jitterS > 0 ? esp_random() % (job.jitterS + 1) : 0;
        if (failed) {
            js.failures++;
            js.nextS = nowS + backoffDelayS(job.intervalS, js.failures) + jitter;
        } else {
            js.failures = 0;
            js.nextS = nextSlotS(js.t0S, job.intervalS, nowS) + jitter;
        }
        js.stats.nextRunS = js.nextS;
    }

    static void _endRun(bool abandoned) {
        if (!_run.active) return;
        uint8_t jobIndex = _run.job;
        const Job& job = _active.table->jobs[jobIndex];
        JobState& js = _states[jobIndex];
        if (abandoned) {
            _freeStage();
            if (_take(_queueMutex(), 1000)) {
                _pendingParts[jobIndex].active = false;
                _give(_queueMutex());
            }
            _run.active = false;
            _publishSnapshot();
            return;
        }
        _flushStage(job);
        bool failed = _run.stopped || _run.okSteps == 0;
        uint64_t nowUtc = CustomTime::isTimeSynched() ? CustomTime::getUnixTime() : 0;
        js.stats.runs++;
        js.stats.lastDurMs = (uint32_t)(millis64() - _run.startMs);
        js.stats.lastBytes = _run.bytes;
        const char* code = _run.code[0] ? _run.code : nullptr;
        if (failed) {
            js.stats.fails++;
            js.stats.lastErrS = nowUtc;
            snprintf(js.stats.lastErr, sizeof(js.stats.lastErr), "%s", code ? code : "");
            snprintf(js.stats.lastErrDetail, sizeof(js.stats.lastErrDetail), "%s", _run.detail);
            LOG_WARNING("Collector job %s run %u failed: %s", job.id, (unsigned)_run.seq, code ? code : "NO_DATA");
        } else {
            js.stats.lastOkS = nowUtc;
            LOG_DEBUG("Collector job %s run %u ok, %u bytes", job.id, (unsigned)_run.seq, (unsigned)_run.bytes);
        }
        _noteHealth(healthOnRun(&js.stats.health, failed, failed ? (code ? code : "NO_DATA") : code, _run.detail[0] ? _run.detail : nullptr,
                                nowUtc));
        if (job.usesCursor) {
            if (_run.truncated) js.windowS = halveWindow(js.windowS);
            if (!failed) {
                js.advancePending = true;
                js.advanceTo = _run.window.to;
            }
        }
        if (!job.usesCursor || failed) {
            if (_take(_queueMutex(), 1000)) {
                _pendingParts[jobIndex].active = false;
                _give(_queueMutex());
            }
        }
        _scheduleNext(jobIndex, failed);
        _run.active = false;
        _publishSnapshot();
    }

    // The cursor of a successful run advances once all its parts were published.
    static void _settleCursor(uint8_t jobIndex) {
        JobState& js = _states[jobIndex];
        if (!js.advancePending) return;
        if (!_take(_queueMutex(), 50)) return;
        Pending p = _pendingParts[jobIndex];
        if (p.active && (p.dropped || p.outstanding == 0)) _pendingParts[jobIndex].active = false;
        _give(_queueMutex());
        if (!p.active) {
            js.advancePending = false;
            return;
        }
        if (p.dropped) {
            js.advancePending = false; // the next window re-covers the gap
        } else if (p.outstanding == 0) {
            js.cursorS = js.advanceTo;
            js.cursorSet = true;
            js.windowS = doubleWindow(js.windowS, _active.table->jobs[jobIndex].windowMaxS);
            js.advancePending = false;
            js.stats.cursorSet = true;
            js.stats.cursorS = js.cursorS;
        }
    }

    // Why a job cannot run now (nullptr when it can).
    static const char* _waitReason(const Job& job) {
        if (Tunnel::isActive()) return "PAUSED_TUNNEL";
        if (_otaBusy) return "PAUSED_OTA";
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < COLLECTOR_LOW_HEAP_FREE ||
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < COLLECTOR_LOW_HEAP_BLOCK) {
            return "LOW_HEAP";
        }
        if (!_allocateRing()) return "NO_MEMORY";
        if (job.usesTime && !CustomTime::isTimeSynched()) return "WAIT_CLOCK";
        if (job.usesCursor && !_cloudConnected) return "OFFLINE";
        bool tokens = false;
        if (_take(_queueMutex(), 50)) {
            bucketRefill(&_bucket, millis64());
            tokens = bucketHasTokens(_bucket);
            _give(_queueMutex());
        }
        return tokens ? nullptr : "BUDGET";
    }

    static void _startRun(uint8_t jobIndex) {
        const Job& job = _active.table->jobs[jobIndex];
        JobState& js = _states[jobIndex];
        memset(&_run, 0, sizeof(_run));
        _run.active = true;
        _run.job = jobIndex;
        _run.seq = ++js.seq;
        _run.startMs = millis64();
        if (job.usesTime) {
            uint64_t nowUtc = CustomTime::getUnixTime();
            if (!js.cursorSet || js.cursorS > nowUtc) {
                js.cursorSet = false; // first run after NTP, a change, or a clock step back
                if (js.windowS == 0) js.windowS = job.windowMaxS;
            }
            _run.window = computeWindow(nowUtc, js.cursorS, js.cursorSet, job.overlapS, job.backfillS, js.windowS ? js.windowS : job.windowMaxS);
            _run.nowS = _run.window.to;
        }
        if (job.usesCursor && _take(_queueMutex(), 1000)) {
            _pendingParts[jobIndex] = {true, _run.seq, 0, false};
            _give(_queueMutex());
        }
        uint64_t upS = _uptimeS();
        bool sessionValid = js.sessionHeld && (job.ttlS == 0 || upS - js.loginAtS < job.ttlS);
        if (!sessionValid) _zeroCaptures(js);
        _run.login = job.hasSession && job.loginCount > 0 && !sessionValid;
        _run.index = 0;
        LOG_DEBUG("Collector job %s run %u start", job.id, (unsigned)_run.seq);
        _publishSnapshot();
    }

    static void _storeCapture(JobState& js, uint8_t c, const uint8_t* value, size_t length) {
        if (js.captures[c] != nullptr) {
            mbedtls_platform_zeroize(js.captures[c], js.captureLength[c]);
            free(js.captures[c]);
        }
        js.captures[c] = (uint8_t*)ps_malloc(length);
        if (js.captures[c] == nullptr) {
            js.captureLength[c] = 0;
            return;
        }
        memcpy(js.captures[c], value, length);
        js.captureLength[c] = (uint16_t)length;
    }

    static size_t _capturesHeld(const Job& job, const JobState& js) {
        size_t total = 0;
        for (uint8_t c = 0; c < job.captureCount; c++) total += js.captureLength[c];
        return total;
    }

    // Extracts the captures of one step; false when one is missing or over the limits.
    static bool _capture(const Job& job, JobState& js, bool login, uint8_t index, const GatewayExec::Result& result) {
        uint8_t* value = (uint8_t*)ps_malloc(CAPTURE_VALUE_MAX);
        if (value == nullptr) return false;
        bool ok = true;
        for (uint8_t c = 0; c < job.captureCount && ok; c++) {
            const Capture& cap = job.captures[c];
            if (cap.login != login || cap.step != index) continue;
            size_t length = 0;
            ok = extractCapture(cap, result.headers, result.headersLength, result.body, result.bodyLength, value, CAPTURE_VALUE_MAX, &length) ==
                 CaptureStatus::OK;
            if (ok) {
                _storeCapture(js, c, value, length);
                ok = js.captures[c] != nullptr && _capturesHeld(job, js) <= CAPTURES_TOTAL_MAX;
            }
        }
        mbedtls_platform_zeroize(value, CAPTURE_VALUE_MAX);
        free(value);
        return ok;
    }

    // Capture echo scrub of a data step's body: the forms it expanded plus every
    // value it captured from its own reply, raw and in each encoding the job uses.
    // Gateway task only, so the form table is static rather than on the task stack.
    static constexpr size_t SCRUB_FORMS_MAX = CAPTURES_MAX * (CHAINS_PER_CAPTURE + 1) + CAPTURES_MAX * 2 * 2;
    static SecretsLogic::ScrubForm _forms[SCRUB_FORMS_MAX];
    static uint8_t* _encoded[CAPTURES_MAX * CHAINS_PER_CAPTURE];

    static void _addForm(size_t* count, const uint8_t* bytes, size_t length, const char* name) {
        if (length == 0 || *count >= SCRUB_FORMS_MAX) return;
        for (size_t i = 0; i < *count; i++) {
            if (_forms[i].length == length && memcmp(_forms[i].bytes, bytes, length) == 0) return;
        }
        _forms[(*count)++] = {bytes, length, name};
    }

    static size_t _scrubBody(const Job& job, const JobState& js, uint8_t index, StepContext& ctx, uint8_t* body, size_t length, size_t cap,
                             bool* truncated, bool* redacted) {
        bool own = false;
        for (uint8_t c = 0; c < job.captureCount && !own; c++) {
            own = !job.captures[c].login && job.captures[c].step == index && js.captures[c] != nullptr;
        }
        if (ctx.tokenCount == 0 && !own) return length; // nothing this step could echo

        size_t count = 0;
        size_t encodedCount = 0;
        for (uint8_t t = 0; t < ctx.tokenCount; t++) {
            uint8_t c = ctx.tokens[t].capture;
            if (c >= job.captureCount || js.captures[c] == nullptr) continue;
            _addForm(&count, js.captures[c], js.captureLength[c], job.captures[c].name);
            _addForm(&count, ctx.tokens[t].encoded, ctx.tokens[t].encodedLength, job.captures[c].name);
        }
        size_t scratchBytes = cap > CAPTURE_VALUE_MAX * 3 ? cap : CAPTURE_VALUE_MAX * 3;
        uint8_t* scratch = (uint8_t*)ps_malloc(scratchBytes);
        for (uint8_t c = 0; c < job.captureCount && scratch != nullptr; c++) {
            const Capture& capture = job.captures[c];
            if (capture.login || capture.step != index || js.captures[c] == nullptr) continue;
            _addForm(&count, js.captures[c], js.captureLength[c], capture.name);
            for (uint8_t k = 0; k < capture.chainCount && encodedCount < sizeof(_encoded) / sizeof(_encoded[0]); k++) {
                uint8_t* out = (uint8_t*)ps_malloc(CAPTURE_VALUE_MAX * 3);
                if (out == nullptr) continue;
                _encoded[encodedCount++] = out;
                size_t n = applyChain(capture.chains[k], js.captures[c], js.captureLength[c], out, CAPTURE_VALUE_MAX * 3, scratch, scratchBytes);
                _addForm(&count, out, n, capture.name);
            }
        }
        size_t result = length;
        if (scratch == nullptr) {
            *truncated = true; // fail closed: nothing published rather than an echo
            *redacted = true;
            result = 0;
        } else if (count > 0) {
            result = scrubCaptures(body, length, cap, scratch, _forms, count, truncated, redacted);
        }
        for (size_t i = 0; i < encodedCount; i++) {
            mbedtls_platform_zeroize(_encoded[i], CAPTURE_VALUE_MAX * 3);
            free(_encoded[i]);
            _encoded[i] = nullptr;
        }
        memset(_forms, 0, sizeof(_forms));
        if (scratch != nullptr) {
            mbedtls_platform_zeroize(scratch, scratchBytes);
            free(scratch);
        }
        return result;
    }

    static void _buildRequest(const Job& job, const Step& step, GatewayExec::Request& rq, GatewayExec::Header* headers) {
        rq.module = job.module;
        rq.ip = job.ip;
        rq.port = job.port;
        rq.writes = GatewayExec::Writes::DENY; // jobs are read-only whatever the grants
        rq.timeoutMs = step.timeoutMs;
        rq.maxBytes = step.maxBytes;
        rq.callerId = job.id;
        const StepArgs& a = step.args;
        rq.method = a.method ? a.method : "GET";
        rq.path = a.path ? a.path : "/";
        for (uint8_t i = 0; i < a.headerCount; i++) headers[i] = {a.headers[i].name, a.headers[i].value};
        rq.headers = headers;
        rq.headerCount = a.headerCount;
        rq.body = a.body;
        rq.bodyBytes = a.bodyBytes;
        rq.bodyBytesLength = a.bodyBytesLength;
        rq.certSha256 = a.certSha256;
        rq.address = a.address;
        rq.modbusWrite = false;
        rq.count = a.count;
        rq.payload = a.payload;
        rq.payloadBytes = a.payloadBytes;
        rq.payloadBytesLength = a.payloadBytesLength;
        rq.idleMs = a.idleMs;
        rq.waitMs = a.waitMs;
    }

    static const char* _status(GatewayExec::Status status) {
        return status == GatewayExec::Status::OK ? "OK" : (status == GatewayExec::Status::REJECTED ? "REJECTED" : "FAILED");
    }

    static void _runStep() {
        uint8_t jobIndex = _run.job;
        const Job& job = _active.table->jobs[jobIndex];
        JobState& js = _states[jobIndex];
        uint64_t elapsed = millis64() - _run.startMs - _run.excludedMs;
        if (elapsed > (uint64_t)job.deadlineS * 1000) {
            _fail("DEADLINE", nullptr);
            _run.stopped = true;
            _endRun(false);
            return;
        }
        bool login = _run.login;
        const Step& step = login ? job.login[_run.index] : job.steps[_run.index];

        uint8_t octets[4];
        Gateway::TargetCheck target = Gateway::validateTarget(job.ip, octets);
        if (target != Gateway::TargetCheck::OK) {
            PartHeader h;
            _baseHeader(job, step, login, &h);
            h.st = "REJECTED";
            h.rc = Gateway::targetCheckReasonCode(target);
            _emitPart(job, jobIndex, h, nullptr, 0);
            _fail(h.rc, nullptr);
            _run.stopped = true;
            _endRun(false);
            return;
        }

        StepContext ctx = {};
        ctx.job = &job;
        ctx.state = &js;
        ctx.nowSource = job.usesTime ? toSourceClock(_run.window.to, job.clockOffsetS) : 0;
        ctx.cursorSource = job.usesTime ? toSourceClock(_run.window.from, job.clockOffsetS) : 0;
        Secrets::RequestState secretState;
        GatewayLogic::Resolver chain[] = {{_resolve, &ctx, _expanded}, Secrets::resolver(&secretState)};
        GatewayExec::Header headers[STEP_HEADERS_MAX];
        GatewayExec::Request rq;
        _buildRequest(job, step, rq, headers);
        rq.resolvers = chain;
        rq.resolverCount = 2;
        GatewayExec::Result result;
        GatewayExec::run(rq, result);

        PartHeader h;
        _baseHeader(job, step, login, &h);
        h.hasDur = true;
        h.durMs = result.totalMs;
        if (job.module == GatewayLogic::Module::HTTP || job.module == GatewayLogic::Module::HTTPS) {
            h.hasPs = result.protoStatus != 0;
            h.ps = result.protoStatus;
        } else if (result.protoStatus != 0) {
            h.hasPs = true; // Modbus exception code
            h.ps = result.protoStatus;
        }
        if (job.module == GatewayLogic::Module::MODBUS_TCP) {
            h.addr = result.address;
            h.cnt = step.args.count;
        }
        bool ok = result.status == GatewayExec::Status::OK;
        const char* detail = result.reasonDetail[0] ? result.reasonDetail : nullptr;
        if (!ok) {
            h.st = _status(result.status);
            h.rc = result.reasonCode;
            h.rdt = detail;
        }

        if (login) {
            if (!ok) {
                _emitPart(job, jobIndex, h, nullptr, 0);
                _fail(h.rc, detail);
                _run.stopped = true;
                _freeTokens(ctx);
                _endRun(false);
                return;
            }
            bool captured = _capture(job, js, true, _run.index, result);
            h.total = (uint32_t)result.bodyLength;
            if (!captured) {
                h.st = "FAILED";
                h.rc = "LOGIN_FAILED";
                _zeroCaptures(js);
                _emitPart(job, jobIndex, h, nullptr, 0);
                _fail("LOGIN_FAILED", nullptr);
                _run.stopped = true;
                _freeTokens(ctx);
                _endRun(false);
                return;
            }
            _emitPart(job, jobIndex, h, nullptr, 0); // login replies: status and length only
            _run.index++;
            if (_run.index >= job.loginCount) {
                js.sessionHeld = true;
                js.loginAtS = _uptimeS();
                _run.login = false;
                _run.index = _run.retrying ? _run.retryStep : 0;
            }
            _freeTokens(ctx);
            return;
        }

        StepFate fate = stepFate(job.module, ok, result.reasonCode, result.connected);
        if (ok && job.hasSession && isExpired(job, result.protoStatus, result.body, result.bodyLength)) {
            if (!_run.retrying) {
                _zeroCaptures(js); // expired: the reply is not published, log in once and retry the step
                _run.retrying = true;
                _run.retryStep = _run.index;
                _run.login = true;
                _run.index = 0;
                _freeTokens(ctx);
                return;
            }
            h.st = "FAILED";
            h.rc = "SESSION_EXPIRED";
            _zeroCaptures(js);
            _emitPart(job, jobIndex, h, nullptr, 0);
            _fail("SESSION_EXPIRED", nullptr);
            _run.stopped = true;
            _freeTokens(ctx);
            _endRun(false);
            return;
        }

        if (fate == StepFate::PUBLISH_CONTINUE) {
            if (!_capture(job, js, false, _run.index, result)) {
                h.st = "FAILED";
                h.rc = "CAPTURE_MISSING";
                _emitPart(job, jobIndex, h, nullptr, 0);
                _fail("CAPTURE_MISSING", nullptr);
                _run.stopped = true;
                _freeTokens(ctx);
                _endRun(false);
                return;
            }
            uint8_t* body = result.body;
            size_t length = result.bodyLength;
            uint8_t* packed = nullptr;
            if (job.module == GatewayLogic::Module::MODBUS_TCP) {
                bool bits = rq.address.table == GatewayLogic::ModbusTable::COILS || rq.address.table == GatewayLogic::ModbusTable::DISCRETE_INPUTS;
                packed = (uint8_t*)ps_malloc(result.registerCount * 2 + 1);
                length = packed ? packRegisters(result.registers, result.registerCount, bits, packed, result.registerCount * 2 + 1) : 0;
                body = packed;
            } else if (body != nullptr) {
                bool truncated = result.truncated;
                bool redacted = result.redacted;
                length = _scrubBody(job, js, _run.index, ctx, body, length, step.maxBytes, &truncated, &redacted);
                result.truncated = truncated;
                h.rd = redacted;
            }
            h.tr = result.truncated;
            if (job.module == GatewayLogic::Module::HTTP || job.module == GatewayLogic::Module::HTTPS) {
                static char contentType[65], date[41];
                h.ct = result.headers && _headerValue(result.headers, result.headersLength, "content-type", contentType, sizeof(contentType)) ? contentType
                                                                                                                                            : nullptr;
                h.date = result.headers && _headerValue(result.headers, result.headersLength, "date", date, sizeof(date)) ? date : nullptr;
            }
            char sha[65] = "";
            if (length > 0) {
                uint8_t digest[32];
                _sha256(body, length, digest);
                GatewayLogic::formatHexDigest(digest, sha, sizeof(sha));
                h.sha = sha;
            }
            _emitPart(job, jobIndex, h, body, length);
            free(packed);
            _run.okSteps++;
            if (job.usesCursor && result.truncated) {
                _run.truncated = true;
                _fail("TRUNCATED", nullptr);
                _run.stopped = true;
            }
        } else {
            _emitPart(job, jobIndex, h, nullptr, 0);
            _fail(result.reasonCode, detail);
            if (fate == StepFate::STOP) _run.stopped = true;
        }
        _freeTokens(ctx);
        _run.index++;
        if (_run.stopped || _run.index >= job.stepCount) _endRun(false);
    }

    // Adoption (gateway task)
    // =======================

    static void _adopt() {
        if (!_take(_mailboxMutex(), 0)) return;
        Table incoming = _pending;
        _pending = {nullptr, nullptr};
        _give(_mailboxMutex());
        if (incoming.table == nullptr) return;

        if (_run.active) _endRun(true); // abandoned: no cursor advance, nothing more published
        static JobState carried[JOBS_MAX]; // gateway task only, off the task stack
        memset(carried, 0, sizeof(carried));
        uint64_t nowS = _uptimeS();
        for (uint8_t n = 0; n < incoming.table->jobCount; n++) {
            const Job& job = incoming.table->jobs[n];
            JobState& next = carried[n];
            next.t0S = firstSlotS(nowS, job.intervalS, esp_random() % ((job.intervalS < 60 ? job.intervalS : 60) + 1));
            next.nextS = next.t0S;
            next.windowS = job.windowMaxS;
            snprintf(next.stats.id, sizeof(next.stats.id), "%s", job.id);
            next.stats.rev = job.rev;
            next.stats.nextRunS = next.nextS;
            next.stats.health.st = job.enabled ? HealthState::OK : HealthState::OFF;
            next.stats.health.since = CustomTime::isTimeSynched() ? CustomTime::getUnixTime() : 0;
            for (uint8_t o = 0; _active.table != nullptr && o < _active.table->jobCount; o++) {
                const Job& old = _active.table->jobs[o];
                if (strcmp(old.id, job.id) != 0) continue;
                JobState& prev = _states[o];
                if (old.rev == job.rev && memcmp(old.bindingHash, job.bindingHash, 32) == 0) {
                    // Same job and binding: backoff, session and captures carry over.
                    next.failures = prev.failures;
                    next.t0S = prev.t0S;
                    next.nextS = prev.nextS;
                    next.sessionHeld = prev.sessionHeld;
                    next.loginAtS = prev.loginAtS;
                    for (uint8_t c = 0; c < CAPTURES_MAX; c++) {
                        next.captures[c] = prev.captures[c];
                        next.captureLength[c] = prev.captureLength[c];
                        prev.captures[c] = nullptr;
                        prev.captureLength[c] = 0;
                    }
                    next.seq = prev.seq;
                    next.stats = prev.stats;
                    if (old.clockOffsetS == job.clockOffsetS) {
                        next.cursorSet = prev.cursorSet;
                        next.cursorS = prev.cursorS;
                        next.windowS = prev.windowS;
                    } else {
                        next.stats.cursorSet = false; // a corrected offset re-requests the last backfill
                    }
                }
                break;
            }
        }
        for (uint8_t o = 0; _active.table != nullptr && o < _active.table->jobCount; o++) _zeroCaptures(_states[o]);
        memcpy(_states, carried, sizeof(_states));
        if (_take(_queueMutex(), 1000)) {
            memset(_pendingParts, 0, sizeof(_pendingParts));
            bucketInit(&_bucket, incoming.table->budgetBytesH, millis64());
            _give(_queueMutex());
        }
        _freeTable(_active);
        _active = incoming;
        _urgentReport = true;
        _publishSnapshot();
    }

    // Gateway task entry points
    // =========================

    void noteCommandMs(uint32_t ms) {
        if (_run.active) _run.excludedMs += ms;
    }

    uint32_t nextWaitMs(uint32_t maxMs) {
        if (_run.active) return 0;
        if (_active.table == nullptr || !_enabled || !Gateway::isEnabled()) return maxMs;
        uint64_t nowMs = millis64();
        uint64_t best = UINT64_MAX;
        for (uint8_t i = 0; i < _active.table->jobCount; i++) {
            if (!_active.table->jobs[i].enabled) continue;
            uint64_t due = _states[i].nextS * 1000;
            if (due < best) best = due;
        }
        if (best == UINT64_MAX) return maxMs;
        return best <= nowMs ? 0 : (uint32_t)((best - nowMs) < maxMs ? best - nowMs : maxMs);
    }

    void step() {
        _adopt();
        if (_active.table == nullptr) return;
        uint64_t nowS = _uptimeS();
        bool on = _enabled && Gateway::isEnabled();
        for (uint8_t i = 0; i < _active.table->jobCount; i++) _settleCursor(i);

        if (_run.active) {
            const Job& job = _active.table->jobs[_run.job];
            const char* wait = on ? _waitReason(job) : nullptr;
            if (!on || wait != nullptr) {
                JobState& js = _states[_run.job];
                _endRun(true); // switch-off or pause: abandon before the next step
                _noteHealth(on ? healthOnWait(&js.stats.health, wait, CustomTime::getUnixTime())
                               : healthOnOff(&js.stats.health, Gateway::isEnabled() ? nullptr : "GATEWAY_DISABLED", CustomTime::getUnixTime()));
                _scheduleNext(_run.job, false);
                _publishSnapshot();
                return;
            }
            _runStep();
            return;
        }

        uint64_t nowUtc = CustomTime::isTimeSynched() ? CustomTime::getUnixTime() : 0;
        bool changed = false;
        for (uint8_t i = 0; i < _active.table->jobCount; i++) {
            const Job& job = _active.table->jobs[i];
            JobState& js = _states[i];
            if (!job.enabled || !on) {
                changed = healthOnOff(&js.stats.health, !job.enabled || _enabled ? nullptr : (Gateway::isEnabled() ? nullptr : "GATEWAY_DISABLED"), nowUtc) ||
                          changed;
                if (!Gateway::isEnabled() && job.enabled && _enabled) changed = healthOnOff(&js.stats.health, "GATEWAY_DISABLED", nowUtc) || changed;
                continue;
            }
            if (js.nextS > nowS) continue;
            const char* wait = _waitReason(job);
            if (wait != nullptr) {
                changed = healthOnWait(&js.stats.health, wait, nowUtc) || changed;
                js.nextS = nextSlotS(js.t0S, job.intervalS, nowS); // missed slots are skipped, never caught up
                js.stats.nextRunS = js.nextS;
                continue;
            }
            _startRun(i);
            _runStep();
            break;
        }
        if (changed) _noteHealth(true);
        _publishSnapshot();
    }
}
