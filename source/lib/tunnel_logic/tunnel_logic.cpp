// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "tunnel_logic.h"

#include <cstring>

namespace TunnelLogic {

namespace {

constexpr int VARIABLE = -1;

// Expected payload size per type, VARIABLE for DATA.
int payloadSize(Type type) {
    switch (type) {
        case Type::DATA: return VARIABLE;
        case Type::CH_OPEN:
        case Type::CH_CREDIT: return 4;
        case Type::CH_ERR:
        case Type::RST:
        case Type::CLOSE:
        case Type::CLOSED: return 1;
        case Type::OPENED: return static_cast<int>(OPENED_SIZE);
        default: return 0;
    }
}

bool isKnownType(uint8_t type) { return type >= static_cast<uint8_t>(Type::DATA) && type <= static_cast<uint8_t>(Type::PONG); }

} // namespace

void putU16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void putU32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t getU16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t getU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool isSequenced(Type type) { return type != Type::ACK && type != Type::PING && type != Type::PONG; }

bool isChannelType(Type type) {
    switch (type) {
        case Type::DATA:
        case Type::CH_OPEN:
        case Type::CH_OK:
        case Type::CH_ERR:
        case Type::CH_CREDIT:
        case Type::FIN:
        case Type::RST: return true;
        default: return false;
    }
}

void encodeHeader(const Header &header, uint8_t out[HEADER_SIZE]) {
    out[0] = header.version;
    out[1] = static_cast<uint8_t>(header.type);
    out[2] = header.channel;
    out[3] = header.flags;
    putU32(out + 4, header.seq);
    putU32(out + 8, header.ack);
    putU16(out + 12, header.sack);
    putU16(out + 14, header.length);
}

Decode decodeFrame(const uint8_t *frame, size_t len, Header *header) {
    if (frame == nullptr || len < HEADER_SIZE) return Decode::SHORT;
    if (frame[0] != VERSION) return Decode::BAD_VERSION;
    if (!isKnownType(frame[1])) return Decode::BAD_TYPE;
    Header h;
    h.version = frame[0];
    h.type = static_cast<Type>(frame[1]);
    h.channel = frame[2];
    h.flags = frame[3];
    h.seq = getU32(frame + 4);
    h.ack = getU32(frame + 8);
    h.sack = getU16(frame + 12);
    h.length = getU16(frame + 14);

    if (isChannelType(h.type) ? (h.channel == 0 || h.channel > CHANNELS_MAX) : h.channel != 0) return Decode::BAD_CHANNEL;
    if (h.length > DATA_MAX) return Decode::TOO_LONG;
    if (HEADER_SIZE + h.length != len) return Decode::LENGTH_MISMATCH;
    int expected = payloadSize(h.type);
    if (expected == VARIABLE ? h.length == 0 : h.length != expected) return Decode::BAD_PAYLOAD;
    if (isSequenced(h.type) ? h.seq == 0 : h.seq != 0) return Decode::BAD_PAYLOAD;

    *header = h;
    return Decode::OK;
}

size_t buildFrame(uint8_t *out, size_t cap, Header header, const uint8_t *payload, size_t payloadLen) {
    if (payloadLen > DATA_MAX || cap < HEADER_SIZE + payloadLen) return 0;
    header.length = static_cast<uint16_t>(payloadLen);
    encodeHeader(header, out);
    if (payloadLen > 0) memcpy(out + HEADER_SIZE, payload, payloadLen);
    return HEADER_SIZE + payloadLen;
}

void encodeOpened(const Opened &opened, uint8_t out[OPENED_SIZE]) {
    putU16(out, opened.frameMax);
    out[2] = opened.window;
    out[3] = opened.channelsMax;
    putU32(out + 4, opened.channelCredit);
    putU32(out + 8, opened.maxDurationS);
    putU32(out + 12, opened.idleS);
}

Opened decodeOpened(const uint8_t in[OPENED_SIZE]) {
    Opened opened;
    opened.frameMax = getU16(in);
    opened.window = in[2];
    opened.channelsMax = in[3];
    opened.channelCredit = getU32(in + 4);
    opened.maxDurationS = getU32(in + 8);
    opened.idleS = getU32(in + 12);
    return opened;
}

// ArqReceiver
// ===========

void ArqReceiver::resetAt(uint32_t ack) {
    _ack = ack;
    memset(_have, 0, sizeof(_have));
}

ArqReceiver::Verdict ArqReceiver::onFrame(uint32_t seq, uint8_t *slot) {
    uint32_t offset = seq - _ack; // 1 = next in order
    if (offset == 0 || static_cast<int32_t>(offset) < 0) return Verdict::DUPLICATE;
    if (offset > WINDOW) return Verdict::OUT_OF_WINDOW;
    if (offset == 1) {
        _ack = seq;
        return Verdict::DELIVER;
    }
    uint8_t index = static_cast<uint8_t>(seq % WINDOW);
    if (_have[index]) return Verdict::DUPLICATE;
    _have[index] = true;
    *slot = index;
    return Verdict::BUFFER;
}

bool ArqReceiver::popReady(uint8_t *slot) {
    uint8_t index = static_cast<uint8_t>((_ack + 1) % WINDOW);
    if (!_have[index]) return false;
    _have[index] = false;
    _ack++;
    *slot = index;
    return true;
}

uint16_t ArqReceiver::sack() const {
    uint16_t bits = 0;
    for (uint32_t i = 0; i + 2 <= WINDOW; i++) {
        if (_have[(_ack + 2 + i) % WINDOW]) bits |= static_cast<uint16_t>(1u << i);
    }
    return bits;
}

// RtoEstimator
// ============

static uint32_t _clampRto(uint32_t value) {
    if (value < RTO_MIN_MS) return RTO_MIN_MS;
    if (value > RTO_MAX_MS) return RTO_MAX_MS;
    return value;
}

void RtoEstimator::reset() {
    _hasSample = false;
    _srtt = _rttvar = 0;
    _rto = RTO_INITIAL_MS;
}

void RtoEstimator::sample(uint32_t rttMs) {
    if (!_hasSample) {
        _srtt = rttMs;
        _rttvar = rttMs / 2;
        _hasSample = true;
    } else {
        uint32_t delta = _srtt > rttMs ? _srtt - rttMs : rttMs - _srtt;
        _rttvar = (3 * _rttvar + delta) / 4;
        _srtt = (7 * _srtt + rttMs) / 8;
    }
    _rto = _clampRto(_srtt + 4 * _rttvar);
}

void RtoEstimator::backoff() { _rto = _clampRto(_rto * 2); }

// ArqSender
// =========

void ArqSender::resetAt(uint32_t nextSeq) {
    memset(_slots, 0, sizeof(_slots));
    _nextSeq = nextSeq;
    _inFlight = 0;
    _timeouts = 0;
    _rto.reset();
}

uint32_t ArqSender::allocate(uint32_t nowMs, uint8_t *slot) {
    uint32_t seq = _nextSeq++;
    uint8_t index = static_cast<uint8_t>(seq % WINDOW);
    _slots[index] = {true, false, false, false, false, 1, seq, nowMs};
    _inFlight++;
    *slot = index;
    return seq;
}

uint8_t ArqSender::onAck(uint32_t ack, uint16_t sack, uint32_t nowMs) {
    if (seqBefore(_nextSeq - 1, ack)) return 0; // acks something never sent
    uint8_t freed = 0;
    bool sampled = false;
    // Newest freed first-transmission frame gives the freshest RTT sample.
    uint32_t sampleSeq = 0;
    uint32_t sampleSentMs = 0;
    for (auto &s : _slots) {
        if (!s.used || seqBefore(ack, s.seq)) continue;
        if (s.transmissions == 1 && (!sampled || seqBefore(sampleSeq, s.seq))) {
            sampled = true;
            sampleSeq = s.seq;
            sampleSentMs = s.sentMs;
        }
        s.used = false;
        _inFlight--;
        freed++;
    }
    if (sampled) _rto.sample(nowMs - sampleSentMs);
    if (freed > 0) _timeouts = 0;

    for (uint32_t i = 0; i < 16; i++) {
        if (!(sack & (1u << i))) continue;
        Slot &s = _slots[(ack + 2 + i) % WINDOW];
        if (s.used && s.seq == ack + 2 + i) s.sacked = true;
    }
    for (auto &s : _slots) {
        if (!s.used || s.sacked || s.fastDone) continue;
        uint8_t later = 0;
        for (const auto &o : _slots) {
            if (o.used && o.sacked && seqBefore(s.seq, o.seq)) later++;
        }
        if (later >= FAST_RETRANSMIT_SACKS) s.fastPending = true;
    }
    return freed;
}

bool ArqSender::nextRetransmit(uint32_t nowMs, uint8_t *slot, bool *timeout) {
    for (uint8_t i = 0; i < WINDOW; i++) {
        if (_slots[i].used && (_slots[i].fastPending || _slots[i].rearmed)) {
            *slot = i;
            *timeout = false;
            return true;
        }
    }
    int best = -1;
    for (uint8_t i = 0; i < WINDOW; i++) {
        const Slot &s = _slots[i];
        if (!s.used || s.sacked || nowMs - s.sentMs < _rto.rtoMs()) continue;
        if (best < 0 || seqBefore(s.seq, _slots[best].seq)) best = i;
    }
    if (best < 0) return false;
    *slot = static_cast<uint8_t>(best);
    *timeout = true;
    return true;
}

void ArqSender::markRetransmitted(uint8_t slot, uint32_t nowMs, bool timeout) {
    Slot &s = _slots[slot];
    if (s.fastPending) {
        s.fastPending = false;
        s.fastDone = true;
    }
    s.rearmed = false;
    if (s.transmissions < 255) s.transmissions++;
    s.sentMs = nowMs;
    if (!timeout) return;
    // One backoff per RTO event: only the oldest unacked frame counts.
    for (const auto &o : _slots) {
        if (o.used && seqBefore(o.seq, s.seq)) return;
    }
    _rto.backoff();
    if (_timeouts < 255) _timeouts++;
}

void ArqSender::rearmAll() {
    for (auto &s : _slots) {
        if (s.used && !s.sacked) s.rearmed = true;
    }
}

// ByteRing
// ========

void ByteRing::init(uint8_t *buffer, size_t capacity) {
    _buffer = buffer;
    _capacity = capacity;
    _head = _size = 0;
}

size_t ByteRing::write(const uint8_t *data, size_t len) {
    if (len > space()) len = space();
    size_t tail = (_head + _size) % (_capacity == 0 ? 1 : _capacity);
    size_t first = _capacity - tail < len ? _capacity - tail : len;
    memcpy(_buffer + tail, data, first);
    memcpy(_buffer, data + first, len - first);
    _size += len;
    return len;
}

size_t ByteRing::peek(const uint8_t **data) const {
    *data = _buffer + _head;
    size_t run = _capacity - _head;
    return run < _size ? run : _size;
}

void ByteRing::consume(size_t len) {
    if (len > _size) len = _size;
    _head = (_head + len) % (_capacity == 0 ? 1 : _capacity);
    _size -= len;
    if (_size == 0) _head = 0;
}

// TokenBucket
// ===========

void TokenBucket::reset(uint32_t ratePerSecond, uint32_t burst, uint32_t nowMs) {
    _rate = ratePerSecond;
    _burstMilli = burst * 1000;
    _milliTokens = _burstMilli;
    _lastMs = nowMs;
}

void TokenBucket::_refill(uint32_t nowMs) {
    uint32_t elapsed = nowMs - _lastMs;
    _lastMs = nowMs;
    uint64_t tokens = static_cast<uint64_t>(_milliTokens) + static_cast<uint64_t>(elapsed) * _rate;
    _milliTokens = tokens > _burstMilli ? _burstMilli : static_cast<uint32_t>(tokens);
}

bool TokenBucket::ready(uint32_t nowMs) const {
    uint64_t tokens = static_cast<uint64_t>(_milliTokens) + static_cast<uint64_t>(nowMs - _lastMs) * _rate;
    return tokens >= 1000;
}

bool TokenBucket::take(uint32_t nowMs) {
    _refill(nowMs);
    if (_milliTokens < 1000) return false;
    _milliTokens -= 1000;
    return true;
}

// Arguments and close reasons
// ===========================

bool isValidSid(const char *sid) {
    if (sid == nullptr) return false;
    size_t i = 0;
    for (; sid[i] != '\0'; i++) {
        if (i >= SID_LENGTH) return false;
        char c = sid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return i == SID_LENGTH;
}

static uint32_t _clamp(int64_t requested, uint32_t fallback, uint32_t lo, uint32_t hi) {
    if (requested <= 0) return fallback;
    if (requested < lo) return lo;
    if (requested > hi) return hi;
    return static_cast<uint32_t>(requested);
}

uint32_t clampMaxDurationS(int64_t requested) {
    return _clamp(requested, MAX_DURATION_DEFAULT_S, MAX_DURATION_MIN_S, MAX_DURATION_MAX_S);
}

uint32_t clampIdleS(int64_t requested) { return _clamp(requested, IDLE_DEFAULT_S, IDLE_MIN_S, IDLE_MAX_S); }

bool isPortAllowed(uint16_t port, const uint16_t *ports, size_t count) {
    if (port == 0) return false;
    if (count == 0) return true;
    for (size_t i = 0; i < count; i++) {
        if (ports[i] == port) return true;
    }
    return false;
}

const char *closeReasonName(CloseReason reason) {
    switch (reason) {
        case CloseReason::OPERATOR_CLOSED: return "OPERATOR_CLOSED";
        case CloseReason::CLOUD_CLOSED: return "CLOUD_CLOSED";
        case CloseReason::IDLE_TIMEOUT: return "IDLE_TIMEOUT";
        case CloseReason::MAX_DURATION: return "MAX_DURATION";
        case CloseReason::OPERATOR_LOST: return "OPERATOR_LOST";
        case CloseReason::TUNNEL_DISABLED: return "TUNNEL_DISABLED";
        case CloseReason::OTA_STARTED: return "OTA_STARTED";
        case CloseReason::LOW_MEMORY: return "LOW_MEMORY";
        case CloseReason::CLOUD_DISCONNECTED: return "CLOUD_DISCONNECTED";
        case CloseReason::LINK_FAILED: return "LINK_FAILED";
        case CloseReason::BYTE_CAP: return "BYTE_CAP";
        case CloseReason::SHUTDOWN: return "SHUTDOWN";
        default: return "NONE";
    }
}

bool closeReasonSucceeded(CloseReason reason) {
    return reason == CloseReason::OPERATOR_CLOSED || reason == CloseReason::CLOUD_CLOSED ||
           reason == CloseReason::IDLE_TIMEOUT || reason == CloseReason::MAX_DURATION;
}

CloseReason checkClose(const SessionState &s, uint32_t nowMs) {
    if (!s.enabled) return CloseReason::TUNNEL_DISABLED;
    if (!s.cloudConnected && s.disconnectedSinceMs != 0 && elapsedMs(nowMs, s.disconnectedSinceMs) >= CLOUD_GRACE_MS)
        return CloseReason::CLOUD_DISCONNECTED;
    if (s.lowHeapSinceMs != 0 && elapsedMs(nowMs, s.lowHeapSinceMs) >= LOW_HEAP_GRACE_MS) return CloseReason::LOW_MEMORY;
    if (s.consecutiveTimeouts >= LINK_FAILED_TIMEOUTS) return CloseReason::LINK_FAILED;
    if (s.channelBytes >= BYTE_CAP) return CloseReason::BYTE_CAP;
    // Operator silence only counts while we can hear the operator at all.
    if (s.cloudConnected && elapsedMs(nowMs, s.lastDownlinkMs) >= OPERATOR_SILENCE_MS) return CloseReason::OPERATOR_LOST;
    if (elapsedMs(nowMs, s.openedMs) >= s.maxDurationMs) return CloseReason::MAX_DURATION;
    if (elapsedMs(nowMs, s.lastDataMs) >= s.idleMs) return CloseReason::IDLE_TIMEOUT;
    return CloseReason::NONE;
}

bool shouldReadNow(size_t available, size_t limit, uint32_t readableSinceMs, uint32_t nowMs, uint32_t coalesceMs) {
    if (limit == 0) return false;
    if (available == 0) return true; // EOF or error: recv() reports it
    return available >= limit || elapsedMs(nowMs, readableSinceMs) >= coalesceMs;
}

} // namespace TunnelLogic
