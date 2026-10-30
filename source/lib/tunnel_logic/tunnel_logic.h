// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free logic for the LAN tunnel (openspec capability:
// lan-tunnel): frame codec, ARQ sender/receiver, RTO estimator, channel credit,
// byte ring, token bucket, argument checks and the session close triggers.
// Sockets, tasks and MQTT live in tunnel.cpp. source/utils/lan_tunnel/protocol.py
// is the Python twin; both are pinned by test/test_tunnel_logic/golden_frames.h.

namespace TunnelLogic {

// ---------------------------------------------------------------------------
// Wire format (16-byte little-endian header + payload)
// ---------------------------------------------------------------------------

inline constexpr uint8_t VERSION = 1;
inline constexpr size_t HEADER_SIZE = 16;
inline constexpr size_t DATA_MAX = 4096;                  // One frame = one 5 KB AWS billing unit
inline constexpr size_t FRAME_MAX = HEADER_SIZE + DATA_MAX;
inline constexpr uint8_t CHANNELS_MAX = 4;                // Channel ids 1..4, 0 = session control
inline constexpr uint8_t WINDOW = 8;                      // Unacked frames per direction
inline constexpr uint8_t FLAG_ACK_VALID = 0x01;
inline constexpr uint8_t FLAG_RETX = 0x02;                // Stats only

enum class Type : uint8_t {
    DATA = 1, ACK = 2, CH_OPEN = 3, CH_OK = 4, CH_ERR = 5, CH_CREDIT = 6, FIN = 7, RST = 8,
    OPENED = 9, CLOSE = 10, CLOSED = 11, PING = 12, PONG = 13,
};

enum class ChannelError : uint8_t { CONNECT_FAILED = 1, TIMEOUT = 2, REFUSED = 3, NO_SOCKET = 4, BUSY = 5 };

enum class RstReason : uint8_t { ABORT = 0, LAN_RESET = 1, WRITE_STALL = 2, NOT_OPEN = 3, PROTOCOL = 4, SESSION_CLOSED = 5 };

struct Header {
    uint8_t version;
    Type type;
    uint8_t channel;
    uint8_t flags;
    uint32_t seq;
    uint32_t ack;
    uint16_t sack;
    uint16_t length;
};

enum class Decode { OK, SHORT, BAD_VERSION, BAD_TYPE, BAD_CHANNEL, TOO_LONG, LENGTH_MISMATCH, BAD_PAYLOAD };

// Every type except ACK, PING and PONG consumes a sequence number.
bool isSequenced(Type type);

// Channel-scoped types must name channel 1..4; session types must use channel 0.
bool isChannelType(Type type);

void encodeHeader(const Header &header, uint8_t out[HEADER_SIZE]);

// Validates the whole frame (header, length against `len`, payload size per
// type, channel range) and fills `header`. The payload starts at HEADER_SIZE.
Decode decodeFrame(const uint8_t *frame, size_t len, Header *header);

// Header + payload into `out`; returns the frame size, 0 if `cap` is too small
// or `payloadLen` exceeds DATA_MAX. `header.length` is set from `payloadLen`.
size_t buildFrame(uint8_t *out, size_t cap, Header header, const uint8_t *payload, size_t payloadLen);

void putU16(uint8_t *p, uint16_t v);
void putU32(uint8_t *p, uint32_t v);
uint16_t getU16(const uint8_t *p);
uint32_t getU32(const uint8_t *p);

inline constexpr size_t OPENED_SIZE = 16;

struct Opened {
    uint16_t frameMax;
    uint8_t window;
    uint8_t channelsMax;
    uint32_t channelCredit;
    uint32_t maxDurationS;
    uint32_t idleS;
};

void encodeOpened(const Opened &opened, uint8_t out[OPENED_SIZE]);
Opened decodeOpened(const uint8_t in[OPENED_SIZE]);

// millis() elapsed since `since`, 0 if `since` is (slightly) in the future.
inline uint32_t elapsedMs(uint32_t nowMs, uint32_t sinceMs) {
    return static_cast<int32_t>(nowMs - sinceMs) > 0 ? nowMs - sinceMs : 0;
}

// 32-bit serial arithmetic: true when `a` comes before `b`.
inline bool seqBefore(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

// ---------------------------------------------------------------------------
// ARQ receiver: in-order delivery, dedupe, cumulative ack + SACK
// ---------------------------------------------------------------------------

class ArqReceiver {
public:
    enum class Verdict { DELIVER, BUFFER, DUPLICATE, OUT_OF_WINDOW };

    void reset() { resetAt(0); }
    void resetAt(uint32_t ack); // tests start near the 2^32 wrap

    // DELIVER: `seq` is the next in-order frame (ack advanced). Call popReady()
    // afterwards to deliver frames it unblocked. BUFFER: keep the frame in reorder
    // slot `*slot` (0..WINDOW-1) until popReady() hands that slot back.
    Verdict onFrame(uint32_t seq, uint8_t *slot);

    // Next buffered frame that is now in order; advances ack.
    bool popReady(uint8_t *slot);

    uint32_t ack() const { return _ack; }

    // Bit i: seq ack + 2 + i was received out of order.
    uint16_t sack() const;

private:
    uint32_t _ack = 0;
    bool _have[WINDOW] = {};
};

// ---------------------------------------------------------------------------
// RTO estimator (RFC 6298, integer milliseconds)
// ---------------------------------------------------------------------------

inline constexpr uint32_t RTO_INITIAL_MS = 1000;
inline constexpr uint32_t RTO_MIN_MS = 300;
inline constexpr uint32_t RTO_MAX_MS = 5000;

class RtoEstimator {
public:
    void reset();
    void sample(uint32_t rttMs);
    void backoff();
    // Ack progress proves the path works again: drop the backoff even without a
    // Karn-valid sample (at 20 % loss most acked frames were resends, so the RTO
    // otherwise stayed at 5 s and a lost tail frame waited that long).
    void clearBackoff();
    uint32_t rtoMs() const { return _rto; }
    uint32_t srttMs() const { return _srtt; }
    bool hasSample() const { return _hasSample; }

private:
    bool _hasSample = false;
    uint32_t _srtt = 0;
    uint32_t _rttvar = 0;
    uint32_t _rto = RTO_INITIAL_MS;
};

// ---------------------------------------------------------------------------
// ARQ sender: window of WINDOW slots indexed by seq % WINDOW
// ---------------------------------------------------------------------------

inline constexpr uint8_t LINK_FAILED_TIMEOUTS = 8;
// A frame below a SACKed one is resent after about one SRTT, without backoff,
// and again every SRTT while the hole stays open. On a sparse, lossy link there
// are rarely enough later frames for a duplicate-ack count, and waiting for the
// backed-off RTO let a lost CH_OK sit for 12 s (dev bench, 20 % loss).
inline constexpr uint8_t FAST_RETRANSMIT_SACKS = 1;
inline constexpr uint32_t FAST_RETRANSMIT_MIN_MS = 20;
inline constexpr uint8_t HOLE_TRANSMISSIONS_MAX = 6; // then the counted RTO path takes over (LINK_FAILED)

class ArqSender {
public:
    void reset() { resetAt(1); }
    void resetAt(uint32_t nextSeq); // tests start near the 2^32 wrap

    bool canSend() const { return _inFlight < WINDOW; }
    uint8_t inFlight() const { return _inFlight; }
    uint32_t nextSeq() const { return _nextSeq; }

    // Claims the next seq, records its first transmission at `nowMs` and returns
    // the slot that must hold its copy until acked. Call only when canSend().
    uint32_t allocate(uint32_t nowMs, uint8_t *slot);

    // Applies a peer's cumulative ack + SACK. Frees acked slots, takes an RTT
    // sample (Karn: first transmissions only), resets the timeout streak on
    // progress and arms fast retransmit. Returns the number of slots freed.
    uint8_t onAck(uint32_t ack, uint16_t sack, uint32_t nowMs);

    // A slot due for retransmission: reconnect rearm and SACK holes first (not RTO
    // events), then RTO expiry.
    // `*timeout` tells the caller whether this counts as an RTO event.
    bool nextRetransmit(uint32_t nowMs, uint8_t *slot, bool *timeout);

    // Records the retransmission of `slot`; an RTO event backs the timer off
    // (once per event, on the oldest unacked frame) and extends the streak.
    void markRetransmitted(uint8_t slot, uint32_t nowMs, bool timeout);

    // After an MQTT reconnect: everything unacked is due now, without counting
    // a timeout (nothing could be delivered while disconnected).
    void rearmAll();

    uint32_t seqOf(uint8_t slot) const { return _slots[slot].seq; }
    bool isUsed(uint8_t slot) const { return _slots[slot].used; }
    uint8_t consecutiveTimeouts() const { return _timeouts; }
    const RtoEstimator &rto() const { return _rto; }

private:
    struct Slot {
        bool used;
        bool sacked;
        bool hole; // a later frame was SACKed: this one is most likely lost
        bool rearmed; // resend after a reconnect: not an RTO event
        uint8_t transmissions;
        uint32_t seq;
        uint32_t sentMs;
    };

    Slot _slots[WINDOW] = {};
    uint32_t _nextSeq = 1;
    uint8_t _inFlight = 0;
    uint8_t _timeouts = 0;
    RtoEstimator _rto;
};

// ---------------------------------------------------------------------------
// Channel credit and buffering
// ---------------------------------------------------------------------------

inline constexpr uint32_t CREDIT_UPDATE_BYTES = 2048;

// Our right to send on a channel: the peer's absolute cumulative grant.
struct SendCredit {
    uint32_t grant = 0;
    uint32_t sent = 0;
    uint32_t available() const { return seqBefore(sent, grant) ? grant - sent : 0; }
    void onGrant(uint32_t cumulative) {
        if (seqBefore(grant, cumulative)) grant = cumulative;
    }
    void consume(uint32_t n) { sent += n; }
};

// The peer's right to send to us, backed by a buffer of `bufferSize` bytes.
struct ReceiveCredit {
    uint32_t bufferSize = 0;
    uint32_t received = 0;   // bytes accepted into the buffer
    uint32_t drained = 0;    // bytes written out of the buffer
    uint32_t advertised = 0; // last grant sent to the peer

    void reset(uint32_t size) { bufferSize = size; received = drained = 0; advertised = size; }
    bool accepts(uint32_t n) const { return n <= advertised - received; }
    void onReceived(uint32_t n) { received += n; }
    void onDrained(uint32_t n) { drained += n; }
    uint32_t grant() const { return drained + bufferSize; }
    // At least CREDIT_UPDATE_BYTES freed, or the buffer emptied with an unannounced grant.
    bool shouldAdvertise() const {
        uint32_t fresh = grant() - advertised;
        return fresh >= CREDIT_UPDATE_BYTES || (fresh > 0 && received == drained);
    }
};

// FIFO byte ring over caller-owned memory.
class ByteRing {
public:
    void init(uint8_t *buffer, size_t capacity);
    size_t size() const { return _size; }
    size_t space() const { return _capacity - _size; }
    size_t write(const uint8_t *data, size_t len); // returns bytes stored
    // Longest contiguous readable run starting at the head.
    size_t peek(const uint8_t **data) const;
    void consume(size_t len);
    void clear() { _head = _size = 0; }

private:
    uint8_t *_buffer = nullptr;
    size_t _capacity = 0;
    size_t _head = 0;
    size_t _size = 0;
};

// ---------------------------------------------------------------------------
// Rate limit, arguments, close reasons
// ---------------------------------------------------------------------------

class TokenBucket {
public:
    void reset(uint32_t ratePerSecond, uint32_t burst, uint32_t nowMs);
    bool take(uint32_t nowMs);
    bool ready(uint32_t nowMs) const; // a token is available, without taking it

private:
    void _refill(uint32_t nowMs);
    uint32_t _rate = 0;
    uint32_t _burstMilli = 0;
    uint32_t _milliTokens = 0;
    uint32_t _lastMs = 0;
};

inline constexpr size_t SID_LENGTH = 16;
inline constexpr uint32_t MAX_DURATION_DEFAULT_S = 3000;
inline constexpr uint32_t MAX_DURATION_MIN_S = 60;
inline constexpr uint32_t MAX_DURATION_MAX_S = 3300; // AWS may drop terminal statuses past 60 min
inline constexpr uint32_t IDLE_DEFAULT_S = 300;
inline constexpr uint32_t IDLE_MIN_S = 30;
inline constexpr uint32_t IDLE_MAX_S = 1800;
inline constexpr size_t PORTS_MAX = 16;
inline constexpr uint16_t DEFAULT_PORTS[] = {22, 23, 80, 443, 502, 8080, 8443};

bool isValidSid(const char *sid);           // exactly 16 lowercase hex chars
uint32_t clampMaxDurationS(int64_t requested); // <=0 -> default
uint32_t clampIdleS(int64_t requested);        // <=0 -> default
bool isPortAllowed(uint16_t port, const uint16_t *ports, size_t count); // count 0 -> any

enum class CloseReason : uint8_t {
    NONE = 0, OPERATOR_CLOSED, CLOUD_CLOSED, IDLE_TIMEOUT, MAX_DURATION, OPERATOR_LOST, TUNNEL_DISABLED,
    OTA_STARTED, LOW_MEMORY, CLOUD_DISCONNECTED, LINK_FAILED, BYTE_CAP, SHUTDOWN,
};

const char *closeReasonName(CloseReason reason);
bool closeReasonSucceeded(CloseReason reason); // terminal SUCCEEDED vs FAILED

inline constexpr uint32_t OPERATOR_SILENCE_MS = 30 * 1000;
inline constexpr uint32_t CLOUD_GRACE_MS = 30 * 1000;
inline constexpr uint32_t LOW_HEAP_GRACE_MS = 5 * 1000;
inline constexpr uint64_t BYTE_CAP = 256ULL * 1024 * 1024;

// Snapshot of everything the close triggers look at. Times are millis();
// a *SinceMs of 0 means "not in that state".
struct SessionState {
    bool enabled;
    bool cloudConnected;
    uint32_t openedMs;
    uint32_t lastDownlinkMs;
    uint32_t lastDataMs;
    uint32_t disconnectedSinceMs;
    uint32_t lowHeapSinceMs;
    uint32_t maxDurationMs;
    uint32_t idleMs;
    uint64_t channelBytes;
    uint8_t consecutiveTimeouts;
};

// First close trigger that fires, in priority order, or NONE.
CloseReason checkClose(const SessionState &state, uint32_t nowMs);

// Read a LAN socket now, or wait up to `coalesceMs` for more bytes to fill
// a frame. `limit` is min(credit, DATA_MAX).
bool shouldReadNow(size_t available, size_t limit, uint32_t readableSinceMs, uint32_t nowMs, uint32_t coalesceMs);

} // namespace TunnelLogic
