// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "setupcheck.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <cmath>
#include <new>

#include "ade7953.h"
#include "bootrecovery.h"
#include "constants.h"
#include "phase_check.h"
#include "utils.h"

namespace SetupCheck {

namespace {

using PhaseCheck::Complex;

constexpr uint8_t MAX_CHANNELS = MAX_CHANNEL_COUNT;
constexpr uint8_t MAX_LINES = MAX_CHANNELS / 3;

struct LineSlot {
    PhaseCheck::LineId id;
    ChannelRole role;
    float ratingA[3];
    PhaseCheck::LineEvidence evidence;
    PhaseCheck::MinuteBucket bucket[3];
    PhaseCheck::StableVerdict verdict;
    uint32_t evaluatedGeneration;  // the line generation the shown verdict was computed for
};

struct SingleSlot {
    uint8_t channel;
    ChannelRole role;
    float ratingA;
    PhaseCheck::ChannelEvidence evidence;
    PhaseCheck::MinuteBucket bucket;
    PhaseCheck::SingleResult result;
};

// Allocated in PSRAM when the window opens, freed when it closes.
struct Runtime {
    LineSlot lines[MAX_LINES];
    uint8_t lineCount = 0;
    SingleSlot singles[MAX_CHANNELS];
    uint8_t singleCount = 0;
    uint64_t minuteStart = 0;
    // Scratch for rebuildLocked (meter task only), here so it lives in PSRAM, not on the stack.
    LineSlot oldLines[MAX_LINES];
    SingleSlot oldSingles[MAX_CHANNELS];
};

// A copy of the configuration the check needs, taken outside our mutex.
struct ConfigSnapshot {
    uint8_t count = 0;
    Phase voltagePhase = PHASE_1;
    ChannelData ch[MAX_CHANNELS];
    bool threePhase = false;
};

SemaphoreHandle_t _mutex = nullptr;
Runtime *_rt = nullptr;
volatile bool _setupDone = true;
uint64_t _rerunUntil = 0;           // written by the web task, read by the meter task: under _windowMux
portMUX_TYPE _windowMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool _holdFlips = false;
volatile bool _pendingWrite = true;  // under _mutex, like _dirty and _generation
uint32_t _dirty = 0xFFFFFFFFu;       // channels written since the last rebuild (bit per channel)
uint32_t _generation[MAX_CHANNELS] = {};
// The meter task never waits long on the web task: losing one reading or one tick is harmless.
constexpr uint32_t METER_TASK_LOCK_MS = 5;
float _current[MAX_CHANNELS] = {};

// Configuration copies (16 channels, a few KB) kept in PSRAM, one per calling task:
// the meter task (onTick) and the web server task (toJson, apply), which never run the same
// one concurrently.
ConfigSnapshot *snapshotBuffer(uint8_t which) {
    static ConfigSnapshot *buffers[2] = {nullptr, nullptr};
    if (!buffers[which]) {
        void *mem = heap_caps_malloc(sizeof(ConfigSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) mem = heap_caps_malloc(sizeof(ConfigSnapshot), MALLOC_CAP_8BIT);
        if (mem) buffers[which] = new (mem) ConfigSnapshot();
    }
    return buffers[which];
}
constexpr uint8_t METER_TASK = 0;
constexpr uint8_t WEB_TASK = 1;

bool windowOpenAt(uint64_t now) {
    taskENTER_CRITICAL(&_windowMux);
    const uint64_t until = _rerunUntil;
    taskEXIT_CRITICAL(&_windowMux);
    return !_setupDone || (until != 0 && now < until);
}

void setRerunUntil(uint64_t until) {
    taskENTER_CRITICAL(&_windowMux);
    _rerunUntil = until;
    taskEXIT_CRITICAL(&_windowMux);
}

uint32_t channelBits(const PhaseCheck::LineId &id) {
    return (1UL << id.ch[0]) | (1UL << id.ch[1]) | (1UL << id.ch[2]);
}

void takeSnapshot(ConfigSnapshot &s) {
    s.count = min<uint8_t>(globalHwProfile->totalChannelCount, MAX_CHANNELS);
    s.voltagePhase = Ade7953::getVoltagePhase();
    s.threePhase = s.voltagePhase != PHASE_1;
    for (uint8_t i = 0; i < s.count; i++) {
        Ade7953::getChannelData(s.ch[i], i);
        if (s.ch[i].active && (s.ch[i].phase == PHASE_2 || s.ch[i].phase == PHASE_3)) s.threePhase = true;
    }
}

PhaseCheck::Hypothesis configuredOf(const ConfigSnapshot &s, const PhaseCheck::LineId &id) {
    PhaseCheck::Hypothesis h{};
    for (int i = 0; i < 3; i++) {
        h.phase[i] = static_cast<uint8_t>(s.ch[id.ch[i]].phase);
        h.reverse[i] = s.ch[id.ch[i]].reverse;
    }
    return h;
}

uint32_t lineGeneration(const PhaseCheck::LineId &id) {
    return _generation[id.ch[0]] + _generation[id.ch[1]] + _generation[id.ch[2]];
}

// Rebuilds lines and single-phase checks from the configuration, keeping the evidence of
// those that survive (same channels, role and CT rating). Caller holds _mutex.
void rebuildLocked(const ConfigSnapshot &s) {
    PhaseCheck::ChannelView views[MAX_CHANNELS];
    for (uint8_t i = 0; i < s.count; i++) views[i] = {s.ch[i].active, s.ch[i].role, static_cast<uint32_t>(s.ch[i].phase), s.ch[i].groupLabel};
    PhaseCheck::LineId previous[MAX_LINES], next[MAX_LINES];
    for (uint8_t i = 0; i < _rt->lineCount; i++) previous[i] = _rt->lines[i].id;
    const size_t n = PhaseCheck::detectLines(views, s.count, previous, _rt->lineCount, next, MAX_LINES);

    LineSlot *oldLines = _rt->oldLines;
    const uint8_t oldCount = _rt->lineCount;
    for (uint8_t i = 0; i < oldCount; i++) oldLines[i] = _rt->lines[i];
    bool inLine[MAX_CHANNELS] = {};
    for (size_t k = 0; k < n; k++) {
        LineSlot slot{};
        slot.id = next[k];
        slot.role = s.ch[next[k].ch[0]].role;
        for (int i = 0; i < 3; i++) {
            slot.ratingA[i] = s.ch[next[k].ch[i]].ctSpecification.currentRating;
            inLine[next[k].ch[i]] = true;
        }
        for (uint8_t o = 0; o < oldCount; o++) {
            const LineSlot &old = oldLines[o];
            if (memcmp(old.id.ch, slot.id.ch, sizeof(slot.id.ch)) != 0 || old.role != slot.role ||
                memcmp(old.ratingA, slot.ratingA, sizeof(slot.ratingA)) != 0) continue;
            slot.evidence = old.evidence;
            memcpy(slot.bucket, old.bucket, sizeof(slot.bucket));
            slot.verdict = old.verdict;
            slot.evaluatedGeneration = old.evaluatedGeneration;
        }
        for (int i = 0; i < 3; i++) slot.evidence.ch[i].ratingA = slot.ratingA[i];
        _rt->lines[k] = slot;
    }
    _rt->lineCount = static_cast<uint8_t>(n);

    // Single-phase checks: load, grid and PV channels outside lines, on a three-phase install.
    SingleSlot *oldSingles = _rt->oldSingles;
    const uint8_t oldSingleCount = _rt->singleCount;
    memcpy(oldSingles, _rt->singles, sizeof(SingleSlot) * oldSingleCount);
    uint8_t m = 0;
    for (uint8_t i = 0; s.threePhase && i < s.count; i++) {
        const ChannelData &c = s.ch[i];
        if (!c.active || inLine[i] || c.phase == PHASE_SPLIT_240 || !PhaseCheck::hasRoleBand(c.role)) continue;
        SingleSlot slot{};
        slot.channel = i;
        slot.role = c.role;
        slot.ratingA = c.ctSpecification.currentRating;
        for (uint8_t o = 0; o < oldSingleCount; o++) {
            if (oldSingles[o].channel == i && oldSingles[o].role == slot.role && oldSingles[o].ratingA == slot.ratingA) {
                slot.evidence = oldSingles[o].evidence;
                slot.bucket = oldSingles[o].bucket;
            }
        }
        slot.evidence.ratingA = slot.ratingA;
        _rt->singles[m++] = slot;
    }
    _rt->singleCount = m;
}

// Evaluates the lines whose channels were written (shown at once) and, when a minute closed,
// every line (shown once two consecutive minutes agree). A line touched by neither keeps its
// verdict: re-evaluating unchanged evidence would count as a second agreeing minute.
void evaluateLocked(const ConfigSnapshot &s, uint32_t dirty, bool minuteClosed) {
    for (uint8_t k = 0; k < _rt->lineCount; k++) {
        LineSlot &l = _rt->lines[k];
        const bool written = (dirty & channelBits(l.id)) != 0;
        if (!written && !minuteClosed) continue;
        l.verdict.push(PhaseCheck::evaluate(l.evidence, s.voltagePhase, l.role, configuredOf(s, l.id)), written);
        l.evaluatedGeneration = lineGeneration(l.id);
    }
    for (uint8_t k = 0; k < _rt->singleCount; k++) {
        SingleSlot &c = _rt->singles[k];
        c.result = PhaseCheck::checkSingle(c.evidence, s.voltagePhase, c.role, static_cast<uint8_t>(s.ch[c.channel].phase), s.ch[c.channel].reverse);
    }
}

void closeMinuteLocked() {
    for (uint8_t k = 0; k < _rt->lineCount; k++) {
        LineSlot &l = _rt->lines[k];
        Complex avg[3];
        bool read[3];
        for (int i = 0; i < 3; i++) {
            avg[i] = l.bucket[i].average();
            read[i] = l.bucket[i].read();
            l.bucket[i].clear();
        }
        l.evidence.addMinute(avg, read);
    }
    for (uint8_t k = 0; k < _rt->singleCount; k++) {
        SingleSlot &c = _rt->singles[k];
        PhaseCheck::addMinuteSingle(c.evidence, c.bucket.average(), c.bucket.read());
        c.bucket.clear();
    }
}

void freeRuntimeLocked() {
    if (!_rt) return;
    _rt->~Runtime();
    heap_caps_free(_rt);
    _rt = nullptr;
    LOG_INFO("Setup phase check stopped");
}

bool allocRuntimeLocked() {
    void *mem = heap_caps_malloc(sizeof(Runtime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) mem = heap_caps_malloc(sizeof(Runtime), MALLOC_CAP_8BIT);
    if (!mem) {
        LOG_ERROR("Setup phase check: no memory for %u bytes", static_cast<unsigned>(sizeof(Runtime)));
        return false;
    }
    _rt = new (mem) Runtime();
    LOG_INFO("Setup phase check started (%u bytes)", static_cast<unsigned>(sizeof(Runtime)));
    return true;
}

// Reading under a hypothesis from the evidence: the channel's mean conducting apparent
// power at the evidence angle under that phase and reverse flag.
void readingJson(JsonObject o, const PhaseCheck::ChannelEvidence &e, Phase vp, uint8_t phase, bool reverse) {
    if (e.minutes <= 0.0) {
        o["power"] = 0;
        o["pf"] = 0;
        return;
    }
    const double s = std::abs(e.e) / e.minutes * 60.0;
    const double a = PhaseCheck::channelAngleDeg(e, vp, phase, reverse) * DEG_TO_RAD;
    o["power"] = static_cast<int32_t>(lround(s * cos(a)));
    o["pf"] = roundf(static_cast<float>(fabs(cos(a)) * (sin(a) >= 0 ? 1.0 : -1.0)) * 100.0f) / 100.0f;
}

void lineJson(JsonObject o, const LineSlot &l, const ConfigSnapshot &s) {
    const PhaseCheck::Result &r = l.verdict.shown();
    o["lowestChannel"] = l.id.ch[0];
    o["groupLabel"] = s.ch[l.id.ch[0]].groupLabel;
    o["role"] = Ade7953::channelRoleToString(l.role);
    o["state"] = PhaseCheck::stateName(r.state);
    o["generation"] = l.evaluatedGeneration;
    JsonArray chans = o["channels"].to<JsonArray>();
    bool evidence = true;
    for (int i = 0; i < 3; i++) {
        const ChannelData &c = s.ch[l.id.ch[i]];
        JsonObject ch = chans.add<JsonObject>();
        ch["index"] = c.index;
        ch["label"] = c.label;
        ch["phase"] = static_cast<uint8_t>(c.phase);
        ch["reverse"] = c.reverse;
        ch["current"] = roundf(_current[c.index] * 10.0f) / 10.0f;
        evidence = evidence && l.evidence.ch[i].minutes > 0.0;
    }
    if (evidence && r.state != PhaseCheck::State::Collecting) {
        JsonArray readings = o["readings"].to<JsonArray>();
        for (int i = 0; i < 3; i++) {
            const ChannelData &c = s.ch[l.id.ch[i]];
            readingJson(readings.add<JsonObject>(), l.evidence.ch[i], s.voltagePhase, static_cast<uint8_t>(c.phase), c.reverse);
        }
    } else {
        o["readings"] = nullptr;
    }
    JsonArray cands = o["candidates"].to<JsonArray>();
    for (uint8_t p = 0; p < r.proposalCount; p++) {
        const PhaseCheck::Hypothesis &h = r.proposals[p];
        JsonObject c = cands.add<JsonObject>();
        JsonArray phases = c["phases"].to<JsonArray>();
        JsonArray rev = c["reverse"].to<JsonArray>();
        JsonArray readings = c["readings"].to<JsonArray>();
        for (int i = 0; i < 3; i++) {
            phases.add(h.phase[i]);
            rev.add(h.reverse[i]);
            readingJson(readings.add<JsonObject>(), l.evidence.ch[i], s.voltagePhase, h.phase[i], h.reverse[i]);
        }
    }
}

void singleJson(JsonObject o, const SingleSlot &c, const ConfigSnapshot &s) {
    const ChannelData &ch = s.ch[c.channel];
    o["index"] = c.channel;
    o["label"] = ch.label;
    o["groupLabel"] = ch.groupLabel;
    o["role"] = Ade7953::channelRoleToString(c.role);
    o["phase"] = static_cast<uint8_t>(ch.phase);
    o["reverse"] = ch.reverse;
    o["current"] = roundf(_current[c.channel] * 10.0f) / 10.0f;
    o["state"] = PhaseCheck::singleStateName(c.result.state);
    if (c.result.state == PhaseCheck::SingleState::Collecting) {
        o["reading"] = nullptr;
    } else {
        readingJson(o["reading"].to<JsonObject>(), c.evidence, s.voltagePhase, static_cast<uint8_t>(ch.phase), ch.reverse);
    }
    JsonArray opts = o["options"].to<JsonArray>();
    for (uint8_t k = 0; k < c.result.optionCount; k++) {
        JsonObject op = opts.add<JsonObject>();
        op["phase"] = c.result.options[k].phase;
        op["reverse"] = c.result.options[k].reverse;
        readingJson(op, c.evidence, s.voltagePhase, c.result.options[k].phase, c.result.options[k].reverse);
    }
}

} // namespace

void begin(bool firstBootThisBoot) {
    createMutexIfNeeded(&_mutex);
    // A recovery boot leaves the meter passive: no setup page, no window, nothing written
    if (BootRecovery::meterPassive()) {
        _setupDone = true;
        return;
    }
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
        LOG_ERROR("Setup state: cannot open %s, treating the device as set up", PREFERENCES_NAMESPACE_GENERAL);
        _setupDone = true;
        return;
    }
    if (prefs.isKey(PREFERENCES_SETUP_DONE_KEY)) {
        _setupDone = prefs.getBool(PREFERENCES_SETUP_DONE_KEY, true);
    } else {
        // Absent key: a factory-fresh device starts the setup; a device that already booted
        // older firmware is in service and counts as set up. Stored energy also marks a
        // device in service (firmware older than the first-boot flag), so finishing a setup
        // can never wipe a live meter's counters.
        bool hasEnergy = false;
        Preferences energy;
        if (energy.begin(PREFERENCES_NAMESPACE_ENERGY, true)) {
            char key[16];
            snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, 0U);
            hasEnergy = energy.isKey(key);
            energy.end();
        }
        _setupDone = !firstBootThisBoot || hasEnergy;
        prefs.putBool(PREFERENCES_SETUP_DONE_KEY, _setupDone);
        LOG_INFO("Setup state initialised: %s", _setupDone ? "done (existing device)" : "pending (new device)");
    }
    prefs.end();
}

bool isSetupDone() { return _setupDone; }

bool completeSetup(bool &firstSetup) {
    const bool first = !_setupDone;
    firstSetup = first;
    Preferences prefs;
    bool stored = prefs.begin(PREFERENCES_NAMESPACE_GENERAL, false);
    if (stored) {
        stored = prefs.putBool(PREFERENCES_SETUP_DONE_KEY, true) > 0;
        prefs.end();
    }
    if (!stored) {
        // Nothing changes: a completion that is not stored would come back after a reboot and
        // reset the energy a second time.
        LOG_ERROR("Setup state: cannot store completion");
        return false;
    }
    _setupDone = true;
    setRerunUntil(0);
    if (first) {
        // The install starts from zero: bench tests and a wrong configuration during the
        // visit leave nothing behind. Before cloud services start, so the cloud never sees a drop.
        Ade7953::resetEnergyValues();
        if (!globalCommunityMode) Mqtt::discardQueuedMeterData();  // queued while held, before the reset
        LOG_INFO("First setup completed: energy counters reset");
    } else {
        LOG_INFO("Setup completed again");
    }
    return true;
}

void startRerun() {
    setRerunUntil(millis64() + SETUP_RERUN_WINDOW_MS);
    onConfigWrite(0xFF);  // fresh verdicts on every line
    LOG_INFO("Setup run again: phase check window open for %llu minutes", SETUP_RERUN_WINDOW_MS / 60000ULL);
}

bool isWindowOpen() { return windowOpenAt(millis64()); }

bool holdPolarityFlips() { return _holdFlips; }

void onConfigWrite(uint8_t channelIndex) {
    if (!acquireMutex(&_mutex)) {
        _pendingWrite = true;
        return;
    }
    if (channelIndex == 0xFF) {
        for (uint32_t &g : _generation) g++;
        _dirty = 0xFFFFFFFFu;
    } else if (channelIndex < MAX_CHANNELS) {
        _generation[channelIndex]++;
        _dirty |= 1UL << channelIndex;
    }
    _pendingWrite = true;
    releaseMutex(&_mutex);
}

void onReading(uint8_t channelIndex, float rawActivePower, float rawReactivePower, uint64_t deltaMillis, float currentRms) {
    if (!_rt || channelIndex >= MAX_CHANNELS) return;  // unlocked read: _rt only changes in onTick, same task
    if (!acquireMutex(&_mutex, METER_TASK_LOCK_MS)) return;
    _current[channelIndex] = currentRms;
    const Complex va{rawActivePower, rawReactivePower};
    for (uint8_t k = 0; k < _rt->lineCount; k++) {
        for (int i = 0; i < 3; i++) {
            if (_rt->lines[k].id.ch[i] == channelIndex) _rt->lines[k].bucket[i].add(va, deltaMillis);
        }
    }
    for (uint8_t k = 0; k < _rt->singleCount; k++) {
        if (_rt->singles[k].channel == channelIndex) _rt->singles[k].bucket.add(va, deltaMillis);
    }
    releaseMutex(&_mutex);
}

void onTick(uint64_t now) {
    const bool open = windowOpenAt(now);
    if (!open && !_rt) {
        _holdFlips = false;
        return;
    }
    const bool minuteDue = _rt && now - _rt->minuteStart >= SETUP_CHECK_MINUTE_MS;
    if (open && _rt && !_pendingWrite && !minuteDue) return;  // the common case, every meter cycle

    ConfigSnapshot *snapPtr = snapshotBuffer(METER_TASK);
    if (!snapPtr) return;
    ConfigSnapshot &snap = *snapPtr;
    // Consume the pending write BEFORE the snapshot: a write landing after this point sets it
    // again and is rebuilt on the next tick, never against a snapshot that predates it.
    if (!acquireMutex(&_mutex, METER_TASK_LOCK_MS)) return;
    bool write = _pendingWrite;
    uint32_t dirty = _dirty;
    _pendingWrite = false;
    _dirty = 0;
    releaseMutex(&_mutex);
    if (open) takeSnapshot(snap);
    _holdFlips = open && snap.threePhase;  // also when the runtime cannot be allocated
    if (!acquireMutex(&_mutex, METER_TASK_LOCK_MS)) {
        if (write) {  // retried on the next tick
            _pendingWrite = true;
            _dirty |= dirty;
        }
        return;
    }
    if (!open) {
        freeRuntimeLocked();
        _holdFlips = false;
        releaseMutex(&_mutex);
        return;
    }
    if (!_rt) {
        if (!allocRuntimeLocked()) {
            releaseMutex(&_mutex);
            return;
        }
        _rt->minuteStart = now;
        write = true;
        dirty = 0xFFFFFFFFu;
    }
    if (minuteDue) {
        closeMinuteLocked();
        _rt->minuteStart = now;
    }
    if (write) rebuildLocked(snap);
    if (write || minuteDue) evaluateLocked(snap, dirty, minuteDue);
    releaseMutex(&_mutex);
}

void toJson(JsonDocument &doc) {
    const bool open = isWindowOpen();
    doc["window"] = open;
    JsonArray lines = doc["lines"].to<JsonArray>();
    JsonArray singles = doc["singles"].to<JsonArray>();
    ConfigSnapshot *snapPtr = snapshotBuffer(WEB_TASK);
    if (!open || !snapPtr) return;
    ConfigSnapshot &snap = *snapPtr;
    takeSnapshot(snap);
    if (!acquireMutex(&_mutex)) return;
    if (_rt) {
        for (uint8_t k = 0; k < _rt->lineCount; k++) lineJson(lines.add<JsonObject>(), _rt->lines[k], snap);
        for (uint8_t k = 0; k < _rt->singleCount; k++) singleJson(singles.add<JsonObject>(), _rt->singles[k], snap);
    }
    releaseMutex(&_mutex);
}

ApplyResult apply(uint8_t lowestChannel, uint32_t generation, const uint8_t channels[3], const uint8_t phases[3], const bool reverse[3]) {
    if (!isWindowOpen()) return ApplyResult::WindowClosed;
    ConfigSnapshot *snapPtr = snapshotBuffer(WEB_TASK);
    if (!snapPtr) return ApplyResult::Failed;
    ConfigSnapshot &snap = *snapPtr;
    takeSnapshot(snap);
    uint8_t idx[3];
    Phase newPhase[3];
    bool newReverse[3];
    if (!acquireMutex(&_mutex)) return ApplyResult::Failed;
    const LineSlot *line = nullptr;
    for (uint8_t k = 0; _rt && k < _rt->lineCount; k++) {
        if (_rt->lines[k].id.ch[0] == lowestChannel) line = &_rt->lines[k];
    }
    if (!line) {
        releaseMutex(&_mutex);
        return ApplyResult::NotFound;
    }
    // The candidate is keyed by channel index; map it onto the line's order.
    PhaseCheck::Hypothesis h{};
    for (int i = 0; i < 3; i++) {
        int found = -1;
        for (int j = 0; j < 3; j++) if (channels[j] == line->id.ch[i]) found = j;
        if (found < 0 || phases[found] < 1 || phases[found] > 3) {
            releaseMutex(&_mutex);
            return ApplyResult::NotFound;
        }
        h.phase[i] = phases[found];
        h.reverse[i] = reverse[found];
        idx[i] = line->id.ch[i];
        newPhase[i] = static_cast<Phase>(h.phase[i]);
        newReverse[i] = h.reverse[i];
    }
    ApplyResult result = ApplyResult::Applied;
    if (h == configuredOf(snap, line->id)) {
        result = ApplyResult::NoChange;  // already applied: a repeat changes nothing
    } else if (_pendingWrite || generation != line->evaluatedGeneration || generation != lineGeneration(line->id)) {
        result = ApplyResult::Stale;  // the proposal was computed for another configuration
    } else {
        bool offered = false;
        const PhaseCheck::Result &r = line->verdict.shown();
        for (uint8_t p = 0; p < r.proposalCount; p++) offered = offered || r.proposals[p] == h;
        if (!offered) result = ApplyResult::NotOffered;
    }
    releaseMutex(&_mutex);
    if (result != ApplyResult::Applied) return result;
    return Ade7953::applyLineConfiguration(idx, newPhase, newReverse) ? ApplyResult::Applied : ApplyResult::Failed;
}

const char *applyResultMessage(ApplyResult r) {
    switch (r) {
        case ApplyResult::Applied: return "Applied";
        case ApplyResult::NoChange: return "Already applied";
        case ApplyResult::Stale: return "The proposal changed, reload it";
        case ApplyResult::NotOffered: return "The candidate is not one the line offers";
        case ApplyResult::NotFound: return "No such line, or channels that are not the line's";
        case ApplyResult::WindowClosed: return "The phase check runs only during setup";
        default: return "Could not write the channels";
    }
}

} // namespace SetupCheck
