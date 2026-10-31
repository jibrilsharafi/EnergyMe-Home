// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Scheduling, backoff, window, step outcomes, the part header encoder, Modbus
// byte packing, the out-queue, the token bucket and job health.

#include <cstdio>
#include <cstring>

#include "collector_logic.h"

namespace CollectorLogic {

using GatewayLogic::Module;

// ---------------------------------------------------------------------------
// Scheduling, backoff, window
// ---------------------------------------------------------------------------

namespace {
constexpr uint32_t FIRST_SLOT_SPREAD_MAX_S = 60;
constexpr uint32_t BACKOFF_CAP_S = 3600;
} // namespace

uint64_t firstSlotS(uint64_t nowS, uint32_t intervalS, uint32_t randomDelayS) {
    uint32_t spread = intervalS < FIRST_SLOT_SPREAD_MAX_S ? intervalS : FIRST_SLOT_SPREAD_MAX_S;
    return nowS + (randomDelayS < spread ? randomDelayS : spread);
}

uint64_t nextSlotS(uint64_t t0S, uint32_t intervalS, uint64_t nowS) {
    if (nowS < t0S || intervalS == 0) return t0S > nowS ? t0S : nowS + 1;
    return t0S + ((nowS - t0S) / intervalS + 1) * intervalS;
}

uint64_t backoffDelayS(uint32_t intervalS, uint32_t failures) {
    uint64_t cap = intervalS > BACKOFF_CAP_S ? intervalS : BACKOFF_CAP_S;
    uint64_t delay = intervalS;
    for (uint32_t i = 0; i < failures && delay < cap; i++) delay *= 2; // at most ~16 doublings before the cap
    return delay < cap ? delay : cap;
}

Window computeWindow(uint64_t nowS, uint64_t cursorS, bool cursorSet, uint32_t overlapS, uint32_t backfillS, uint32_t windowS) {
    uint64_t floor = nowS > backfillS ? nowS - backfillS : 0;
    if (!cursorSet || cursorS > nowS) cursorS = floor;
    uint64_t start = cursorS > overlapS ? cursorS - overlapS : 0;
    if (start < floor) start = floor;
    uint64_t end = start + windowS;
    if (end > nowS) end = nowS;
    return Window{start, end};
}

uint32_t halveWindow(uint32_t windowS) {
    uint32_t half = windowS / 2;
    return half < WINDOW_MIN_S ? WINDOW_MIN_S : half;
}

uint32_t doubleWindow(uint32_t windowS, uint32_t windowMaxS) {
    uint64_t twice = static_cast<uint64_t>(windowS) * 2;
    if (twice > windowMaxS) twice = windowMaxS;
    return twice < WINDOW_MIN_S ? WINDOW_MIN_S : static_cast<uint32_t>(twice);
}

int64_t toSourceClock(uint64_t deviceUtcS, int64_t clockOffsetS) { return static_cast<int64_t>(deviceUtcS) - clockOffsetS; }

// ---------------------------------------------------------------------------
// Step outcomes
// ---------------------------------------------------------------------------

StepFate stepFate(Module module, bool ok, const char *reasonCode, bool connected) {
    if (ok) return StepFate::PUBLISH_CONTINUE;
    const char *code = reasonCode != nullptr ? reasonCode : "";
    bool noAnswer = strcmp(code, "TIMEOUT") == 0 || strcmp(code, "NO_REPLY") == 0;
    bool badResponse = strcmp(code, "BAD_RESPONSE") == 0;
    switch (module) {
        case Module::MODBUS_TCP:
            // A dead unit behind a TCP-to-RTU gateway times out on an established connection.
            if (strcmp(code, "MODBUS_EXCEPTION") == 0) return StepFate::PART_CONTINUE;
            if (noAnswer || badResponse) return connected ? StepFate::PART_CONTINUE : StepFate::STOP;
            return StepFate::STOP;
        case Module::UDP:
            // The spec stops on TIMEOUT / NO_REPLY only for http, https and tcp.
            return noAnswer || badResponse ? StepFate::PART_CONTINUE : StepFate::STOP;
        case Module::HTTP:
        case Module::HTTPS:
        case Module::TCP: return badResponse ? StepFate::PART_CONTINUE : StepFate::STOP;
    }
    return StepFate::STOP;
}

// ---------------------------------------------------------------------------
// Part header
// ---------------------------------------------------------------------------

namespace {

constexpr size_t CONTENT_TYPE_MAX = 64;
constexpr size_t DATE_MAX = 40;

struct Out {
    char *buffer;
    size_t cap; // includes the terminator
    size_t used;
    bool ok;

    void raw(const char *text, size_t length) {
        if (!ok || length >= cap - used) {
            ok = false;
            return;
        }
        memcpy(buffer + used, text, length);
        used += length;
        buffer[used] = '\0';
    }
    void text(const char *s) { raw(s, strlen(s)); }
    void key(const char *name) {
        raw(used > 1 ? ",\"" : "\"", used > 1 ? 2 : 1);
        text(name);
        raw("\":", 2);
    }
    void u(const char *name, uint64_t value) {
        char number[24];
        key(name);
        snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(value));
        text(number);
    }
    void s(const char *name, int64_t value) {
        char number[24];
        key(name);
        snprintf(number, sizeof(number), "%lld", static_cast<long long>(value));
        text(number);
    }
    // Printable ASCII only, with `"` and `\` escaped.
    void str(const char *name, const char *value) {
        key(name);
        raw("\"", 1);
        for (const char *p = value; *p != '\0' && ok; p++) {
            char c = *p;
            if (c < 0x20 || c > 0x7E) {
                ok = false;
                return;
            }
            if (c == '"' || c == '\\') raw("\\", 1);
            raw(&c, 1);
        }
        raw("\"", 1);
    }
};

bool isPrintableAscii(const char *text, size_t minLength, size_t maxLength) {
    if (text == nullptr) return false;
    size_t n = 0;
    for (const char *p = text; *p != '\0'; p++, n++) {
        if (n >= maxLength || *p < 0x20 || *p > 0x7E) return false;
    }
    return n >= minLength;
}

} // namespace

bool isCodeToken(const char *text) {
    if (text == nullptr || text[0] == '\0') return false;
    size_t n = 0;
    for (const char *p = text; *p != '\0'; p++, n++) {
        if (n >= CODE_MAX || !((*p >= 'A' && *p <= 'Z') || *p == '_')) return false;
    }
    return true;
}

size_t encodePartHeader(const PartHeader &h, char *out, size_t cap) {
    if (out == nullptr || cap == 0) return 0;
    size_t limit = cap < PART_HEADER_MAX + 1 ? cap : PART_HEADER_MAX + 1;
    Out o{out, limit, 0, true};
    if (h.job == nullptr || h.src == nullptr || h.boot == nullptr || h.step == nullptr || h.st == nullptr) return 0;
    bool isOk = strcmp(h.st, "OK") == 0;
    bool http = h.module == Module::HTTP || h.module == Module::HTTPS;

    o.raw("{", 1);
    o.u("v", 1);
    o.str("job", h.job);
    o.u("rev", h.rev);
    o.u("set", h.set);
    o.str("src", h.src);
    o.str("boot", h.boot);
    o.u("seq", h.seq);
    o.str("step", h.step);
    o.str("ph", h.login ? "login" : "step");
    o.str("mod", GatewayLogic::moduleName(h.module));
    o.u("i", h.i);
    o.u("n", h.n);
    o.u("off", h.off);
    o.str("st", h.st);
    if (h.tsKnown) {
        o.u("ts", h.tsMs);
    } else {
        o.key("ts");
        o.text("null");
    }
    o.u("up", h.upMs);
    o.u("dq", h.dq);
    if (h.hasPs) o.s("ps", h.ps);
    if (h.hasCo) o.s("co", h.co);
    if (h.module == Module::MODBUS_TCP && h.addr != nullptr) {
        o.str("addr", h.addr);
        o.u("cnt", h.cnt);
    }
    if (http && isPrintableAscii(h.ct, 1, CONTENT_TYPE_MAX)) o.str("ct", h.ct);
    if (h.hasWin) {
        char window[48];
        o.key("win");
        snprintf(window, sizeof(window), "[%lld,%lld]", static_cast<long long>(h.winFrom), static_cast<long long>(h.winTo));
        o.text(window);
    }
    if (http && !h.login && isPrintableAscii(h.date, 1, DATE_MAX)) o.str("date", h.date);
    if (h.tr) {
        o.key("tr");
        o.text("true");
    }
    if (!isOk && h.rc != nullptr) o.str("rc", h.rc);
    if (!isOk && isCodeToken(h.rdt)) o.str("rdt", h.rdt);
    if (h.rd) {
        o.key("rd");
        o.text("true");
    }
    o.u("len", h.len);
    o.u("total", h.total);
    if (h.sha != nullptr && h.len > 0 && !h.login) o.str("sha", h.sha);
    if (h.hasDur) o.u("dur", h.durMs);
    o.raw("}", 1);
    return o.ok && o.used <= PART_HEADER_MAX ? o.used : 0;
}

size_t packRegisters(const uint16_t *values, size_t count, bool bits, uint8_t *out, size_t cap) {
    if (values == nullptr || out == nullptr) return 0;
    if (bits) {
        size_t bytes = (count + 7) / 8;
        if (bytes > cap) return 0;
        memset(out, 0, bytes);
        for (size_t i = 0; i < count; i++) {
            if (values[i] != 0) out[i / 8] = static_cast<uint8_t>(out[i / 8] | (1u << (i % 8)));
        }
        return bytes;
    }
    if (count * 2 > cap) return 0;
    for (size_t i = 0; i < count; i++) {
        out[2 * i] = static_cast<uint8_t>(values[i] >> 8);
        out[2 * i + 1] = static_cast<uint8_t>(values[i] & 0xFF);
    }
    return count * 2;
}

// ---------------------------------------------------------------------------
// Out-queue
// ---------------------------------------------------------------------------

namespace {

struct Record {
    uint32_t size; // whole record, padded to 4
    uint32_t payloadLength;
    uint16_t headerLength;
    uint8_t job;
    uint8_t reserved;
    uint32_t seq;
    uint32_t body;
    char sourceId[SOURCE_ID_MAX + 1];
    uint8_t alone;
};

constexpr size_t RECORD_BYTES = (sizeof(Record) + 3) & ~static_cast<size_t>(3);

Record readRecord(const uint8_t *storage, size_t offset) {
    Record r;
    memcpy(&r, storage + offset, sizeof(r));
    return r;
}

size_t partBytes(const Record &r) { return static_cast<size_t>(r.headerLength) + 1 + r.payloadLength; }

bool sameTag(const PartTag &a, const PartTag &b) { return a.job == b.job && a.seq == b.seq && a.body == b.body && a.gen == b.gen; }

PartTag tagOf(const Record &r) { return PartTag{r.job, r.seq, r.body, r.reserved, r.alone != 0}; }

bool sameBody(const Record &r, const PartTag &tag) { return sameTag(tagOf(r), tag); }

} // namespace

void PartQueue::init(uint8_t *storage, size_t capacity) {
    _storage = storage;
    _capacity = storage != nullptr ? capacity : 0;
    _used = 0;
    _parts = 0;
    _pinned = 0;
    _pinnedBytes = 0;
    _droppedParts = 0;
    _droppedBytes = 0;
    _hasDroppedBody = false;
    _droppedBody = PartTag{};
}

void PartQueue::_removeBody(size_t firstRecord, const PartTag &tag, void (*notify)(const PartTag &tag, bool published, void *context),
                            void *context) {
    size_t read = firstRecord, write = firstRecord;
    size_t removed = 0;
    while (read < _used) {
        Record r = readRecord(_storage, read);
        if (sameBody(r, tag)) {
            _droppedParts++;
            _droppedBytes += partBytes(r);
            removed++;
            if (notify != nullptr) notify(tagOf(r), false, context);
        } else {
            if (write != read) memmove(_storage + write, _storage + read, r.size);
            write += r.size;
        }
        read += r.size;
    }
    _used = write;
    _parts -= removed;
}

bool PartQueue::push(const char *sourceId, const PartTag &tag, const char *header, size_t headerLength, const uint8_t *payload,
                     size_t payloadLength, void (*notify)(const PartTag &tag, bool published, void *context), void *context) {
    size_t need = (RECORD_BYTES + headerLength + 1 + payloadLength + 3) & ~static_cast<size_t>(3);
    auto dropNew = [&]() {
        _droppedParts++;
        _droppedBytes += headerLength + 1 + payloadLength;
        if (notify != nullptr) notify(tag, false, context);
        return false;
    };
    if (_storage == nullptr || sourceId == nullptr || header == nullptr || headerLength > UINT16_MAX || (payload == nullptr && payloadLength > 0))
        return dropNew();
    if (_hasDroppedBody && sameTag(_droppedBody, tag)) return dropNew();

    if (need > _capacity - _used) {
        // Whole bodies behind the pinned prefix can go, except bodies with a pinned part and this part's own body.
        auto evictable = [&](const Record &r) {
            if (sameBody(r, tag)) return false;
            for (size_t offset = 0; offset < _pinnedBytes;) {
                Record p = readRecord(_storage, offset);
                if (sameBody(p, tagOf(r))) return false;
                offset += p.size;
            }
            return true;
        };
        size_t reclaimable = 0;
        for (size_t offset = _pinnedBytes; offset < _used;) {
            Record r = readRecord(_storage, offset);
            if (evictable(r)) reclaimable += r.size;
            offset += r.size;
        }
        if (need > _capacity - _used + reclaimable) {
            // Not enough room even after eviction: the new part goes, with the queued rest of its body.
            _hasDroppedBody = true;
            _droppedBody = tag;
            _removeBody(_pinnedBytes, tag, notify, context);
            return dropNew();
        }
        size_t offset = _pinnedBytes;
        while (need > _capacity - _used && offset < _used) {
            Record r = readRecord(_storage, offset);
            if (!evictable(r)) {
                offset += r.size;
                continue;
            }
            _removeBody(offset, tagOf(r), notify, context); // the record at `offset` is replaced by the next one
        }
        if (need > _capacity - _used) return dropNew();
    }

    Record r{};
    r.size = static_cast<uint32_t>(need);
    r.payloadLength = static_cast<uint32_t>(payloadLength);
    r.headerLength = static_cast<uint16_t>(headerLength);
    r.job = tag.job;
    r.reserved = tag.gen;
    r.alone = tag.alone ? 1 : 0;
    r.seq = tag.seq;
    r.body = tag.body;
    snprintf(r.sourceId, sizeof(r.sourceId), "%s", sourceId);
    uint8_t *p = _storage + _used;
    memcpy(p, &r, sizeof(r));
    memcpy(p + RECORD_BYTES, header, headerLength);
    p[RECORD_BYTES + headerLength] = '\n';
    if (payloadLength > 0) memcpy(p + RECORD_BYTES + headerLength + 1, payload, payloadLength);
    _used += need;
    _parts++;
    return true;
}

bool PartQueue::beginMessage(View *view) {
    _pinned = 0;
    _pinnedBytes = 0;
    if (_storage == nullptr || _parts == 0 || view == nullptr) return false;
    Record first = readRecord(_storage, 0);
    view->sourceId = reinterpret_cast<const char *>(_storage + offsetof(Record, sourceId));
    view->count = 0;
    view->bytes = 0;
    size_t offset = 0;
    size_t limit = sizeof(view->parts) / sizeof(view->parts[0]);
    while (offset < _used && view->count < limit) {
        Record r = readRecord(_storage, offset);
        size_t bytes = partBytes(r);
        if (view->count > 0 && (r.alone || strcmp(r.sourceId, first.sourceId) != 0 || view->bytes + bytes > COALESCE_BYTES)) break;
        view->parts[view->count] = _storage + offset + RECORD_BYTES;
        view->partBytes[view->count] = bytes;
        view->headerLength[view->count] = r.headerLength;
        view->count++;
        view->bytes += bytes;
        offset += r.size;
        if (view->bytes > COALESCE_BYTES || r.alone) break; // a large part or a chunk travels alone
    }
    _pinned = view->count;
    _pinnedBytes = offset;
    return true;
}

void PartQueue::endMessage(bool published, void (*notify)(const PartTag &tag, bool published, void *context), void *context) {
    if (_storage == nullptr || _pinned == 0) {
        _pinned = 0;
        _pinnedBytes = 0;
        return;
    }
    if (published) {
        for (size_t offset = 0; offset < _pinnedBytes;) {
            Record r = readRecord(_storage, offset);
            if (notify != nullptr) notify(tagOf(r), true, context);
            offset += r.size;
        }
        memmove(_storage, _storage + _pinnedBytes, _used - _pinnedBytes);
        _used -= _pinnedBytes;
        _parts -= _pinned;
    }
    _pinned = 0;
    _pinnedBytes = 0;
}

// ---------------------------------------------------------------------------
// Token bucket
// ---------------------------------------------------------------------------

void bucketInit(TokenBucket *bucket, uint32_t limitH, uint64_t nowMs) {
    bucket->limitH = limitH;
    bucket->tokens = limitH;
    bucket->lastMs = nowMs;
}

void bucketRefill(TokenBucket *bucket, uint64_t nowMs) {
    if (nowMs > bucket->lastMs) {
        bucket->tokens += static_cast<double>(nowMs - bucket->lastMs) * bucket->limitH / 3600000.0;
        if (bucket->tokens > bucket->limitH) bucket->tokens = bucket->limitH;
    }
    bucket->lastMs = nowMs;
}

bool bucketHasTokens(const TokenBucket &bucket) { return bucket.tokens >= 1.0; }

void bucketSpend(TokenBucket *bucket, size_t bytes) {
    // A message is published whole, so the bucket may go into debt, bounded by one hour.
    bucket->tokens -= static_cast<double>(bytes);
    double floor = -static_cast<double>(bucket->limitH);
    if (bucket->tokens < floor) bucket->tokens = floor;
}

// ---------------------------------------------------------------------------
// Health
// ---------------------------------------------------------------------------

const char *healthStateName(HealthState state) {
    switch (state) {
        case HealthState::OK: return "ok";
        case HealthState::ERR: return "err";
        case HealthState::WAIT: return "wait";
        case HealthState::OFF: return "off";
    }
    return "off";
}

namespace {

bool setHealth(Health *health, HealthState st, const char *code, const char *detail, uint64_t nowS) {
    char newCode[CODE_MAX + 1];
    char newDetail[CODE_MAX + 1];
    snprintf(newCode, sizeof(newCode), "%s", code != nullptr ? code : "");
    snprintf(newDetail, sizeof(newDetail), "%s", newCode[0] != '\0' && isCodeToken(detail) ? detail : "");
    bool stOrCode = st != health->st || strcmp(newCode, health->code) != 0;
    bool changed = stOrCode || strcmp(newDetail, health->detail) != 0;
    if (stOrCode) health->since = nowS;
    health->st = st;
    memcpy(health->code, newCode, sizeof(newCode));
    memcpy(health->detail, newDetail, sizeof(newDetail));
    return changed;
}

} // namespace

bool healthOnRun(Health *health, bool failed, const char *code, const char *detail, uint64_t nowS) {
    HealthState st = HealthState::OK;
    if (failed) {
        if (health->consecutiveFailures < UINT32_MAX) health->consecutiveFailures++;
        if (health->consecutiveFailures >= ERR_AFTER_FAILURES) st = HealthState::ERR;
    } else {
        health->consecutiveFailures = 0;
    }
    return setHealth(health, st, code, detail, nowS);
}

bool healthOnWait(Health *health, const char *code, uint64_t nowS) { return setHealth(health, HealthState::WAIT, code, nullptr, nowS); }

bool healthOnOff(Health *health, const char *code, uint64_t nowS) { return setHealth(health, HealthState::OFF, code, nullptr, nowS); }

bool reportDue(bool healthChanged, bool urgent, uint64_t lastReportS, uint64_t nowS) {
    if (urgent) return true;
    return healthChanged && (nowS < lastReportS || nowS - lastReportS >= HEALTH_REPORT_INTERVAL_S);
}

} // namespace CollectorLogic
