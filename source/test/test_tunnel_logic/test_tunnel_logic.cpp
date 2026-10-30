// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for TunnelLogic. Run with:
//   pio test -e native          (from WSL - Windows native compile is broken)

#include <unity.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "tunnel_logic.h"
#include "golden_frames.h"

using namespace TunnelLogic;

void setUp(void) {}
void tearDown(void) {}

static std::vector<uint8_t> fromHex(const char *hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        char byte[3] = {hex[i], hex[i + 1], 0};
        out.push_back(static_cast<uint8_t>(strtoul(byte, nullptr, 16)));
    }
    return out;
}

static std::vector<uint8_t> frame(Type type, uint8_t ch, uint32_t seq, size_t payloadLen) {
    std::vector<uint8_t> payload(payloadLen, 0xAB);
    std::vector<uint8_t> out(HEADER_SIZE + payloadLen);
    Header h = {VERSION, type, ch, FLAG_ACK_VALID, seq, 0, 0, 0};
    TEST_ASSERT_EQUAL(out.size(), buildFrame(out.data(), out.size(), h, payload.data(), payloadLen));
    return out;
}

// ============================================================================
// Codec
// ============================================================================

struct Golden {
    const char *name;
    Type type;
    uint8_t channel, flags;
    uint32_t seq, ack;
    uint16_t sack;
    const char *payloadHex, *frameHex;
};

#define GOLDEN_ROW(n, t, c, f, s, a, k, p, fr) {#n, Type::t, c, f, s, a, k, p, fr},
static const Golden GOLDEN[] = {TUNNEL_GOLDEN_FRAMES(GOLDEN_ROW)};

void test_golden_frames_encode_and_decode(void) {
    for (const auto &g : GOLDEN) {
        std::vector<uint8_t> payload = fromHex(g.payloadHex);
        std::vector<uint8_t> expected = fromHex(g.frameHex);
        uint8_t out[FRAME_MAX];
        Header h = {VERSION, g.type, g.channel, g.flags, g.seq, g.ack, g.sack, 0};
        size_t len = buildFrame(out, sizeof(out), h, payload.data(), payload.size());
        TEST_ASSERT_EQUAL_MESSAGE(expected.size(), len, g.name);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected.data(), out, len, g.name);

        Header d;
        TEST_ASSERT_TRUE_MESSAGE(decodeFrame(expected.data(), expected.size(), &d) == Decode::OK, g.name);
        TEST_ASSERT_TRUE(d.type == g.type);
        TEST_ASSERT_EQUAL(g.channel, d.channel);
        TEST_ASSERT_EQUAL(g.flags, d.flags);
        TEST_ASSERT_EQUAL_UINT32(g.seq, d.seq);
        TEST_ASSERT_EQUAL_UINT32(g.ack, d.ack);
        TEST_ASSERT_EQUAL_UINT16(g.sack, d.sack);
        TEST_ASSERT_EQUAL(payload.size(), d.length);
    }
}

void test_opened_payload_round_trip(void) {
    Opened o = {4096, 8, 4, 8192, 3000, 300};
    uint8_t buf[OPENED_SIZE];
    encodeOpened(o, buf);
    Opened d = decodeOpened(buf);
    TEST_ASSERT_EQUAL(4096, d.frameMax);
    TEST_ASSERT_EQUAL(8, d.window);
    TEST_ASSERT_EQUAL(4, d.channelsMax);
    TEST_ASSERT_EQUAL_UINT32(8192, d.channelCredit);
    TEST_ASSERT_EQUAL_UINT32(3000, d.maxDurationS);
    TEST_ASSERT_EQUAL_UINT32(300, d.idleS);
}

void test_decode_rejects_malformed(void) {
    Header h;
    auto ok = frame(Type::DATA, 1, 1, 10);
    TEST_ASSERT_TRUE(decodeFrame(ok.data(), ok.size(), &h) == Decode::OK);
    TEST_ASSERT_TRUE(decodeFrame(ok.data(), 15, &h) == Decode::SHORT);
    TEST_ASSERT_TRUE(decodeFrame(nullptr, 0, &h) == Decode::SHORT);
    TEST_ASSERT_TRUE(decodeFrame(ok.data(), ok.size() - 1, &h) == Decode::LENGTH_MISMATCH);

    auto bad = ok;
    bad[0] = 2;
    TEST_ASSERT_TRUE(decodeFrame(bad.data(), bad.size(), &h) == Decode::BAD_VERSION);
    bad = ok;
    bad[1] = 0;
    TEST_ASSERT_TRUE(decodeFrame(bad.data(), bad.size(), &h) == Decode::BAD_TYPE);
    bad[1] = 14;
    TEST_ASSERT_TRUE(decodeFrame(bad.data(), bad.size(), &h) == Decode::BAD_TYPE);
    bad = ok;
    bad[2] = 5;
    TEST_ASSERT_TRUE(decodeFrame(bad.data(), bad.size(), &h) == Decode::BAD_CHANNEL);
    bad[2] = 0;
    TEST_ASSERT_TRUE(decodeFrame(bad.data(), bad.size(), &h) == Decode::BAD_CHANNEL);

    // len above DATA_MAX, even if the buffer is that long
    std::vector<uint8_t> big(HEADER_SIZE + 5000, 0);
    memcpy(big.data(), ok.data(), HEADER_SIZE);
    putU16(big.data() + 14, 5000);
    TEST_ASSERT_TRUE(decodeFrame(big.data(), big.size(), &h) == Decode::TOO_LONG);
}

void test_decode_checks_payload_and_seq_rules(void) {
    Header h;
    auto emptyData = frame(Type::DATA, 1, 1, 0);
    TEST_ASSERT_TRUE(decodeFrame(emptyData.data(), emptyData.size(), &h) == Decode::BAD_PAYLOAD);
    auto shortCredit = frame(Type::CH_CREDIT, 1, 1, 3);
    TEST_ASSERT_TRUE(decodeFrame(shortCredit.data(), shortCredit.size(), &h) == Decode::BAD_PAYLOAD);
    auto ackWithPayload = frame(Type::ACK, 0, 0, 1);
    TEST_ASSERT_TRUE(decodeFrame(ackWithPayload.data(), ackWithPayload.size(), &h) == Decode::BAD_PAYLOAD);
    auto sequencedZero = frame(Type::FIN, 1, 0, 0);
    TEST_ASSERT_TRUE(decodeFrame(sequencedZero.data(), sequencedZero.size(), &h) == Decode::BAD_PAYLOAD);
    auto pingWithSeq = frame(Type::PING, 0, 4, 0);
    TEST_ASSERT_TRUE(decodeFrame(pingWithSeq.data(), pingWithSeq.size(), &h) == Decode::BAD_PAYLOAD);
    auto openedOnChannel = frame(Type::OPENED, 1, 1, OPENED_SIZE);
    TEST_ASSERT_TRUE(decodeFrame(openedOnChannel.data(), openedOnChannel.size(), &h) == Decode::BAD_CHANNEL);
    auto maxData = frame(Type::DATA, 4, 1, DATA_MAX);
    TEST_ASSERT_TRUE(decodeFrame(maxData.data(), maxData.size(), &h) == Decode::OK);
}

void test_build_frame_bounds(void) {
    uint8_t out[FRAME_MAX];
    uint8_t payload[DATA_MAX + 1] = {};
    Header h = {VERSION, Type::DATA, 1, 0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL(0, buildFrame(out, sizeof(out), h, payload, DATA_MAX + 1));
    TEST_ASSERT_EQUAL(0, buildFrame(out, 20, h, payload, 5));
    TEST_ASSERT_EQUAL(21, buildFrame(out, 21, h, payload, 5));
}

void test_seq_serial_arithmetic(void) {
    TEST_ASSERT_TRUE(seqBefore(1, 2));
    TEST_ASSERT_FALSE(seqBefore(2, 2));
    TEST_ASSERT_TRUE(seqBefore(0xFFFFFFFFu, 0));
    TEST_ASSERT_TRUE(seqBefore(0xFFFFFFF0u, 5));
    TEST_ASSERT_FALSE(seqBefore(5, 0xFFFFFFF0u));
}

// ============================================================================
// ARQ receiver
// ============================================================================

void test_receiver_in_order(void) {
    ArqReceiver r;
    r.reset();
    uint8_t slot;
    for (uint32_t s = 1; s <= 20; s++) {
        TEST_ASSERT_TRUE(r.onFrame(s, &slot) == ArqReceiver::Verdict::DELIVER);
        TEST_ASSERT_FALSE(r.popReady(&slot));
    }
    TEST_ASSERT_EQUAL_UINT32(20, r.ack());
    TEST_ASSERT_EQUAL_UINT16(0, r.sack());
}

void test_receiver_reorder_and_sack(void) {
    ArqReceiver r;
    r.reset();
    uint8_t slot3, slot4, slot;
    TEST_ASSERT_TRUE(r.onFrame(3, &slot3) == ArqReceiver::Verdict::BUFFER);
    TEST_ASSERT_TRUE(r.onFrame(4, &slot4) == ArqReceiver::Verdict::BUFFER);
    TEST_ASSERT_EQUAL_UINT32(0, r.ack());
    // bit0 = ack+2 = 2 (missing), bit1 = 3, bit2 = 4
    TEST_ASSERT_EQUAL_UINT16(0x0006, r.sack());
    TEST_ASSERT_TRUE(r.onFrame(1, &slot) == ArqReceiver::Verdict::DELIVER);
    TEST_ASSERT_FALSE(r.popReady(&slot));
    TEST_ASSERT_EQUAL_UINT16(0x0003, r.sack()); // ack=1: 3 is bit0, 4 is bit1
    TEST_ASSERT_TRUE(r.onFrame(2, &slot) == ArqReceiver::Verdict::DELIVER);
    TEST_ASSERT_TRUE(r.popReady(&slot));
    TEST_ASSERT_EQUAL(slot3, slot);
    TEST_ASSERT_TRUE(r.popReady(&slot));
    TEST_ASSERT_EQUAL(slot4, slot);
    TEST_ASSERT_FALSE(r.popReady(&slot));
    TEST_ASSERT_EQUAL_UINT32(4, r.ack());
}

// Every permutation of a window delivers each seq exactly once, in order.
void test_receiver_permutations(void) {
    const uint32_t n = 6;
    uint32_t perm[n] = {1, 2, 3, 4, 5, 6};
    int permutations = 0;
    do {
        ArqReceiver r;
        r.reset();
        std::vector<uint32_t> delivered;
        uint32_t slotSeq[WINDOW] = {};
        for (uint32_t s : perm) {
            uint8_t slot;
            auto v = r.onFrame(s, &slot);
            if (v == ArqReceiver::Verdict::BUFFER) slotSeq[slot] = s;
            if (v == ArqReceiver::Verdict::DELIVER) {
                delivered.push_back(s);
                while (r.popReady(&slot)) delivered.push_back(slotSeq[slot]);
            }
            // Duplicate of every frame seen so far is rejected
            TEST_ASSERT_TRUE(r.onFrame(s, &slot) == ArqReceiver::Verdict::DUPLICATE);
        }
        TEST_ASSERT_EQUAL(n, delivered.size());
        for (uint32_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT32(i + 1, delivered[i]);
        permutations++;
    } while (std::next_permutation(perm, perm + n));
    TEST_ASSERT_EQUAL(720, permutations);
}

void test_receiver_window_and_duplicates(void) {
    ArqReceiver r;
    r.reset();
    uint8_t slot;
    TEST_ASSERT_TRUE(r.onFrame(0, &slot) == ArqReceiver::Verdict::DUPLICATE);
    TEST_ASSERT_TRUE(r.onFrame(WINDOW + 1, &slot) == ArqReceiver::Verdict::OUT_OF_WINDOW);
    TEST_ASSERT_TRUE(r.onFrame(WINDOW, &slot) == ArqReceiver::Verdict::BUFFER);
    TEST_ASSERT_TRUE(r.onFrame(WINDOW, &slot) == ArqReceiver::Verdict::DUPLICATE);
    TEST_ASSERT_TRUE(r.onFrame(0x7FFFFFFFu, &slot) == ArqReceiver::Verdict::OUT_OF_WINDOW);
}

void test_receiver_wraps_at_2_32(void) {
    ArqReceiver r;
    r.resetAt(0xFFFFFFFDu);
    uint8_t slot, slotWrapped;
    TEST_ASSERT_TRUE(r.onFrame(0x00000001u, &slotWrapped) == ArqReceiver::Verdict::BUFFER);
    TEST_ASSERT_TRUE(r.onFrame(0xFFFFFFFEu, &slot) == ArqReceiver::Verdict::DELIVER);
    TEST_ASSERT_TRUE(r.onFrame(0xFFFFFFFFu, &slot) == ArqReceiver::Verdict::DELIVER);
    TEST_ASSERT_TRUE(r.onFrame(0x00000000u, &slot) == ArqReceiver::Verdict::DELIVER);
    TEST_ASSERT_TRUE(r.popReady(&slot));
    TEST_ASSERT_EQUAL(slotWrapped, slot);
    TEST_ASSERT_EQUAL_UINT32(1, r.ack());
    TEST_ASSERT_TRUE(r.onFrame(0xFFFFFFFFu, &slot) == ArqReceiver::Verdict::DUPLICATE);
}

void test_sender_wraps_at_2_32(void) {
    ArqSender s;
    s.resetAt(0xFFFFFFFEu);
    uint8_t slot;
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFEu, s.allocate(0, &slot));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, s.allocate(0, &slot));
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, s.allocate(0, &slot));
    TEST_ASSERT_EQUAL_UINT32(0x00000001u, s.allocate(0, &slot));
    TEST_ASSERT_EQUAL(3, s.onAck(0x00000000u, 0, 10));
    TEST_ASSERT_EQUAL(1, s.inFlight());
    TEST_ASSERT_EQUAL(1, s.onAck(0x00000001u, 0, 10));
}

// ============================================================================
// RTO estimator and ARQ sender
// ============================================================================

void test_rto_rfc6298(void) {
    RtoEstimator e;
    e.reset();
    TEST_ASSERT_EQUAL_UINT32(1000, e.rtoMs());
    e.sample(200); // srtt 200, rttvar 100 -> 600
    TEST_ASSERT_EQUAL_UINT32(200, e.srttMs());
    TEST_ASSERT_EQUAL_UINT32(600, e.rtoMs());
    e.sample(200); // rttvar 75, srtt 200 -> 500
    TEST_ASSERT_EQUAL_UINT32(500, e.rtoMs());
    for (int i = 0; i < 30; i++) e.sample(10);
    TEST_ASSERT_EQUAL_UINT32(RTO_MIN_MS, e.rtoMs());
    e.backoff();
    TEST_ASSERT_EQUAL_UINT32(600, e.rtoMs());
    for (int i = 0; i < 10; i++) e.backoff();
    TEST_ASSERT_EQUAL_UINT32(RTO_MAX_MS, e.rtoMs());
}

void test_sender_window_blocks_and_ack_frees(void) {
    ArqSender s;
    s.reset();
    uint8_t slot;
    for (uint32_t i = 1; i <= WINDOW; i++) {
        TEST_ASSERT_TRUE(s.canSend());
        TEST_ASSERT_EQUAL_UINT32(i, s.allocate(0, &slot));
        TEST_ASSERT_EQUAL(i % WINDOW, slot);
    }
    TEST_ASSERT_FALSE(s.canSend());
    TEST_ASSERT_EQUAL(3, s.onAck(3, 0, 100));
    TEST_ASSERT_EQUAL(WINDOW - 3, s.inFlight());
    TEST_ASSERT_TRUE(s.canSend());
    TEST_ASSERT_EQUAL(0, s.onAck(3, 0, 100)); // duplicate ack frees nothing
    TEST_ASSERT_EQUAL(0, s.onAck(50, 0, 100)); // ack beyond anything sent is ignored
    TEST_ASSERT_EQUAL(WINDOW - 3, s.inFlight());
    TEST_ASSERT_EQUAL_UINT32(100, s.rto().srttMs());
}

void test_sender_rto_retransmit_backoff_and_link_failed(void) {
    ArqSender s;
    s.reset();
    uint8_t slot, a, b;
    bool timeout;
    s.allocate(0, &a);
    s.allocate(0, &b);
    TEST_ASSERT_FALSE(s.nextRetransmit(999, &slot, &timeout));
    TEST_ASSERT_TRUE(s.nextRetransmit(1000, &slot, &timeout));
    TEST_ASSERT_TRUE(timeout);
    TEST_ASSERT_EQUAL(a, slot); // oldest first
    s.markRetransmitted(slot, 1000, true);
    TEST_ASSERT_EQUAL(1, s.consecutiveTimeouts());
    TEST_ASSERT_EQUAL_UINT32(2000, s.rto().rtoMs());
    // The second frame expired in the same event: resent at once, no second backoff.
    TEST_ASSERT_TRUE(s.nextRetransmit(1000, &slot, &timeout));
    TEST_ASSERT_EQUAL(b, slot);
    TEST_ASSERT_FALSE(timeout);
    s.markRetransmitted(slot, 1000, timeout);
    TEST_ASSERT_EQUAL(1, s.consecutiveTimeouts());
    TEST_ASSERT_EQUAL_UINT32(2000, s.rto().rtoMs());
    TEST_ASSERT_FALSE(s.nextRetransmit(1001, &slot, &timeout));

    uint32_t now = 2000;
    for (int i = 0; i < 20 && s.consecutiveTimeouts() < LINK_FAILED_TIMEOUTS; i++) {
        now += RTO_MAX_MS;
        while (s.nextRetransmit(now, &slot, &timeout)) s.markRetransmitted(slot, now, timeout);
    }
    TEST_ASSERT_EQUAL(LINK_FAILED_TIMEOUTS, s.consecutiveTimeouts());
    // Karn: an ack of retransmitted frames gives no RTT sample, but resets the streak.
    uint32_t srtt = s.rto().srttMs();
    s.onAck(2, 0, now + 10);
    TEST_ASSERT_EQUAL_UINT32(srtt, s.rto().srttMs());
    TEST_ASSERT_EQUAL(0, s.consecutiveTimeouts());
}

void test_sender_sack_fast_retransmit(void) {
    ArqSender s;
    s.reset();
    uint8_t slot;
    bool timeout;
    for (int i = 0; i < 5; i++) s.allocate(0, &slot);
    // Peer has 1, misses 2, has 3 and 4 -> ack 1, sack bit0 = 3, bit1 = 4. RTT 50 -> hole wait 62.
    s.onAck(1, 0x0003, 50);
    TEST_ASSERT_FALSE(s.nextRetransmit(61, &slot, &timeout));
    TEST_ASSERT_TRUE(s.nextRetransmit(62, &slot, &timeout));
    TEST_ASSERT_FALSE(timeout);
    TEST_ASSERT_EQUAL_UINT32(2, s.seqOf(slot));
    s.markRetransmitted(slot, 62, false);
    TEST_ASSERT_FALSE(s.nextRetransmit(100, &slot, &timeout));
    // Still a hole one SRTT later (the resend was lost too): resent again, no backoff.
    TEST_ASSERT_TRUE(s.nextRetransmit(124, &slot, &timeout));
    TEST_ASSERT_FALSE(timeout);
    TEST_ASSERT_EQUAL_UINT32(2, s.seqOf(slot));
    s.markRetransmitted(slot, 124, false);
    TEST_ASSERT_EQUAL(0, s.consecutiveTimeouts());
    TEST_ASSERT_EQUAL_UINT32(300, s.rto().rtoMs());
    // SACKed frames (3, 4) are never resent; seq 5 only on RTO.
    for (uint32_t now = 200; now < 20000; now += 50) {
        while (s.nextRetransmit(now, &slot, &timeout)) {
            TEST_ASSERT_TRUE(s.seqOf(slot) == 2 || s.seqOf(slot) == 5);
            s.markRetransmitted(slot, now, timeout);
        }
    }
    // A hole that never fills ends up on the counted RTO path.
    TEST_ASSERT_TRUE(s.consecutiveTimeouts() > 0);
}

void test_sender_rearm_after_reconnect(void) {
    ArqSender s;
    s.reset();
    uint8_t slot;
    bool timeout;
    s.allocate(1000, &slot);
    TEST_ASSERT_FALSE(s.nextRetransmit(1100, &slot, &timeout));
    for (int reconnect = 0; reconnect < 10; reconnect++) {
        s.rearmAll();
        TEST_ASSERT_TRUE(s.nextRetransmit(1100, &slot, &timeout));
        TEST_ASSERT_FALSE(timeout);
        s.markRetransmitted(slot, 1100, timeout);
        TEST_ASSERT_FALSE(s.nextRetransmit(1100, &slot, &timeout)); // once per rearm
    }
    TEST_ASSERT_EQUAL(0, s.consecutiveTimeouts());
    TEST_ASSERT_EQUAL_UINT32(RTO_INITIAL_MS, s.rto().rtoMs());
}

void test_token_bucket_ready_peeks(void) {
    TokenBucket t;
    t.reset(25, 1, 0);
    TEST_ASSERT_TRUE(t.ready(0));
    TEST_ASSERT_TRUE(t.ready(0)); // peeking does not consume
    TEST_ASSERT_TRUE(t.take(0));
    TEST_ASSERT_FALSE(t.ready(39));
    TEST_ASSERT_TRUE(t.ready(40));
}

// ============================================================================
// Credit, ring, token bucket
// ============================================================================

void test_send_credit(void) {
    SendCredit c;
    c.onGrant(100);
    TEST_ASSERT_EQUAL_UINT32(100, c.available());
    c.consume(60);
    TEST_ASSERT_EQUAL_UINT32(40, c.available());
    c.onGrant(80); // stale/duplicate grant never lowers it
    TEST_ASSERT_EQUAL_UINT32(40, c.available());
    c.onGrant(200);
    TEST_ASSERT_EQUAL_UINT32(140, c.available());
    c.consume(140);
    TEST_ASSERT_EQUAL_UINT32(0, c.available());
}

void test_receive_credit_batching(void) {
    ReceiveCredit c;
    c.reset(8192);
    TEST_ASSERT_TRUE(c.accepts(8192));
    TEST_ASSERT_FALSE(c.accepts(8193));
    c.onReceived(8000);
    TEST_ASSERT_FALSE(c.accepts(193));
    c.onDrained(1000);
    TEST_ASSERT_FALSE(c.shouldAdvertise()); // under 2 KB freed, buffer not empty
    c.onDrained(1100);
    TEST_ASSERT_TRUE(c.shouldAdvertise());
    c.advertised = c.grant();
    TEST_ASSERT_EQUAL_UINT32(8192 + 2100, c.advertised);
    TEST_ASSERT_FALSE(c.shouldAdvertise());
    c.onDrained(5900); // buffer now empty with 5900 unannounced
    TEST_ASSERT_TRUE(c.shouldAdvertise());
    c.advertised = c.grant();
    TEST_ASSERT_TRUE(c.accepts(8192));
}

void test_byte_ring_wraps(void) {
    uint8_t mem[8];
    ByteRing r;
    r.init(mem, sizeof(mem));
    const uint8_t a[] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL(6, r.write(a, 6));
    r.consume(4);
    const uint8_t b[] = {7, 8, 9, 10, 11, 12, 13};
    TEST_ASSERT_EQUAL(6, r.write(b, 7)); // only 6 free
    TEST_ASSERT_EQUAL(8, r.size());
    const uint8_t *p;
    size_t run = r.peek(&p);
    TEST_ASSERT_EQUAL(4, run); // 5,6,7,8 up to the end of memory
    TEST_ASSERT_EQUAL(5, p[0]);
    r.consume(run);
    run = r.peek(&p);
    TEST_ASSERT_EQUAL(4, run);
    TEST_ASSERT_EQUAL(9, p[0]);
    TEST_ASSERT_EQUAL(12, p[3]);
    r.consume(10);
    TEST_ASSERT_EQUAL(0, r.size());
}

void test_token_bucket(void) {
    TokenBucket t;
    t.reset(25, 8, 0);
    int taken = 0;
    while (t.take(0)) taken++;
    TEST_ASSERT_EQUAL(8, taken);
    TEST_ASSERT_FALSE(t.take(39));
    TEST_ASSERT_TRUE(t.take(40)); // 25/s = one per 40 ms
    taken = 0;
    for (uint32_t ms = 40; ms <= 1040; ms++) {
        if (t.take(ms)) taken++;
    }
    TEST_ASSERT_EQUAL(25, taken);
    // Long idle refills to the burst only.
    taken = 0;
    while (t.take(100000)) taken++;
    TEST_ASSERT_EQUAL(8, taken);
}

// ============================================================================
// Arguments, close reasons, coalescing
// ============================================================================

void test_sid_validation(void) {
    TEST_ASSERT_TRUE(isValidSid("0123456789abcdef"));
    TEST_ASSERT_FALSE(isValidSid("0123456789ABCDEF"));
    TEST_ASSERT_FALSE(isValidSid("0123456789abcde"));
    TEST_ASSERT_FALSE(isValidSid("0123456789abcdef0"));
    TEST_ASSERT_FALSE(isValidSid("0123456789abcdeg"));
    TEST_ASSERT_FALSE(isValidSid("01234567/9abcdef"));
    TEST_ASSERT_FALSE(isValidSid(""));
    TEST_ASSERT_FALSE(isValidSid(nullptr));
}

void test_clamps(void) {
    TEST_ASSERT_EQUAL_UINT32(3000, clampMaxDurationS(0));
    TEST_ASSERT_EQUAL_UINT32(3000, clampMaxDurationS(-5));
    TEST_ASSERT_EQUAL_UINT32(60, clampMaxDurationS(1));
    TEST_ASSERT_EQUAL_UINT32(3300, clampMaxDurationS(36000));
    TEST_ASSERT_EQUAL_UINT32(1200, clampMaxDurationS(1200));
    TEST_ASSERT_EQUAL_UINT32(300, clampIdleS(0));
    TEST_ASSERT_EQUAL_UINT32(30, clampIdleS(5));
    TEST_ASSERT_EQUAL_UINT32(1800, clampIdleS(99999));
}

void test_port_allowlist(void) {
    TEST_ASSERT_TRUE(isPortAllowed(80, DEFAULT_PORTS, sizeof(DEFAULT_PORTS) / sizeof(DEFAULT_PORTS[0])));
    TEST_ASSERT_TRUE(isPortAllowed(502, DEFAULT_PORTS, sizeof(DEFAULT_PORTS) / sizeof(DEFAULT_PORTS[0])));
    TEST_ASSERT_FALSE(isPortAllowed(3389, DEFAULT_PORTS, sizeof(DEFAULT_PORTS) / sizeof(DEFAULT_PORTS[0])));
    TEST_ASSERT_TRUE(isPortAllowed(3389, nullptr, 0)); // empty list = any port
    TEST_ASSERT_FALSE(isPortAllowed(0, nullptr, 0));
}

void test_close_reason_names_and_status(void) {
    TEST_ASSERT_EQUAL_STRING("IDLE_TIMEOUT", closeReasonName(CloseReason::IDLE_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("OPERATOR_LOST", closeReasonName(CloseReason::OPERATOR_LOST));
    TEST_ASSERT_TRUE(closeReasonSucceeded(CloseReason::OPERATOR_CLOSED));
    TEST_ASSERT_TRUE(closeReasonSucceeded(CloseReason::MAX_DURATION));
    TEST_ASSERT_FALSE(closeReasonSucceeded(CloseReason::TUNNEL_DISABLED));
    TEST_ASSERT_FALSE(closeReasonSucceeded(CloseReason::LINK_FAILED));
    TEST_ASSERT_EQUAL(3, static_cast<int>(CloseReason::IDLE_TIMEOUT)); // golden closed_idle
}

static SessionState _healthy(uint32_t now) {
    SessionState s = {};
    s.enabled = true;
    s.cloudConnected = true;
    s.openedMs = now;
    s.lastDownlinkMs = now;
    s.lastDataMs = now;
    s.maxDurationMs = 3000 * 1000;
    s.idleMs = 300 * 1000;
    return s;
}

void test_check_close_triggers(void) {
    const uint32_t t0 = 1000;
    SessionState s = _healthy(t0);
    TEST_ASSERT_TRUE(checkClose(s, t0 + 1000) == CloseReason::NONE);

    s.enabled = false;
    TEST_ASSERT_TRUE(checkClose(s, t0) == CloseReason::TUNNEL_DISABLED);

    s = _healthy(t0);
    TEST_ASSERT_TRUE(checkClose(s, t0 + OPERATOR_SILENCE_MS) == CloseReason::OPERATOR_LOST);
    s.cloudConnected = false; // silence ignored while we cannot hear the operator
    s.disconnectedSinceMs = t0 + 20000;
    TEST_ASSERT_TRUE(checkClose(s, t0 + OPERATOR_SILENCE_MS) == CloseReason::NONE);
    TEST_ASSERT_TRUE(checkClose(s, t0 + 20000 + CLOUD_GRACE_MS) == CloseReason::CLOUD_DISCONNECTED);

    s = _healthy(t0);
    s.lowHeapSinceMs = t0;
    TEST_ASSERT_TRUE(checkClose(s, t0 + 4999) == CloseReason::NONE);
    TEST_ASSERT_TRUE(checkClose(s, t0 + 5000) == CloseReason::LOW_MEMORY);

    s = _healthy(t0);
    s.consecutiveTimeouts = LINK_FAILED_TIMEOUTS;
    TEST_ASSERT_TRUE(checkClose(s, t0) == CloseReason::LINK_FAILED);

    s = _healthy(t0);
    s.channelBytes = BYTE_CAP;
    TEST_ASSERT_TRUE(checkClose(s, t0) == CloseReason::BYTE_CAP);

    s = _healthy(t0);
    s.idleMs = 60000;
    s.maxDurationMs = 90000;
    uint32_t now = t0;
    for (; now < t0 + 59000; now += 10000) s.lastDownlinkMs = now; // pings keep it alive
    s.lastDownlinkMs = t0 + 59000;
    TEST_ASSERT_TRUE(checkClose(s, t0 + 60000) == CloseReason::IDLE_TIMEOUT);
    s.lastDataMs = t0 + 59000;
    s.lastDownlinkMs = t0 + 89000;
    TEST_ASSERT_TRUE(checkClose(s, t0 + 90000) == CloseReason::MAX_DURATION);

    // A start stamp 1 ms in the future (stamped just before now was sampled) is not "forever ago"
    s = _healthy(t0);
    s.cloudConnected = false;
    s.disconnectedSinceMs = t0 + 1;
    s.lowHeapSinceMs = t0 + 1;
    TEST_ASSERT_TRUE(checkClose(s, t0) == CloseReason::NONE);
    TEST_ASSERT_EQUAL_UINT32(0, elapsedMs(t0, t0 + 1));
    TEST_ASSERT_EQUAL_UINT32(5, elapsedMs(0x00000002u, 0xFFFFFFFDu));

    // millis() wrap does not trip timers
    s = _healthy(0xFFFFFF00u);
    TEST_ASSERT_TRUE(checkClose(s, 0x00000100u) == CloseReason::NONE);
}

void test_should_read_now(void) {
    TEST_ASSERT_FALSE(shouldReadNow(100, 0, 0, 100, 10));     // no credit
    TEST_ASSERT_TRUE(shouldReadNow(0, 4096, 0, 0, 10));       // EOF/error
    TEST_ASSERT_TRUE(shouldReadNow(4096, 4096, 0, 0, 10));    // full frame
    TEST_ASSERT_TRUE(shouldReadNow(500, 400, 0, 0, 10));      // credit-limited
    TEST_ASSERT_FALSE(shouldReadNow(10, 4096, 100, 105, 10)); // coalescing
    TEST_ASSERT_TRUE(shouldReadNow(10, 4096, 100, 110, 10));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_golden_frames_encode_and_decode);
    RUN_TEST(test_opened_payload_round_trip);
    RUN_TEST(test_decode_rejects_malformed);
    RUN_TEST(test_decode_checks_payload_and_seq_rules);
    RUN_TEST(test_build_frame_bounds);
    RUN_TEST(test_seq_serial_arithmetic);
    RUN_TEST(test_receiver_in_order);
    RUN_TEST(test_receiver_reorder_and_sack);
    RUN_TEST(test_receiver_permutations);
    RUN_TEST(test_receiver_window_and_duplicates);
    RUN_TEST(test_receiver_wraps_at_2_32);
    RUN_TEST(test_sender_wraps_at_2_32);
    RUN_TEST(test_rto_rfc6298);
    RUN_TEST(test_sender_window_blocks_and_ack_frees);
    RUN_TEST(test_sender_rto_retransmit_backoff_and_link_failed);
    RUN_TEST(test_sender_sack_fast_retransmit);
    RUN_TEST(test_sender_rearm_after_reconnect);
    RUN_TEST(test_send_credit);
    RUN_TEST(test_receive_credit_batching);
    RUN_TEST(test_byte_ring_wraps);
    RUN_TEST(test_token_bucket);
    RUN_TEST(test_token_bucket_ready_peeks);
    RUN_TEST(test_sid_validation);
    RUN_TEST(test_clamps);
    RUN_TEST(test_port_allowlist);
    RUN_TEST(test_close_reason_names_and_status);
    RUN_TEST(test_check_close_triggers);
    RUN_TEST(test_should_read_now);
    return UNITY_END();
}
