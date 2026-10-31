// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the gateway receive side (openspec gateway-modules): the
// incremental chunked decoder and the raw TCP read termination rule.

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "gateway_stream.h"

using namespace GatewayLogic;

// ============================================================================
// Helpers
// ============================================================================

static uint8_t pattern(size_t i) { return static_cast<uint8_t>((i * 13 + 7) & 0xFF); }

// Chunked encoding of `total` pattern bytes in `chunk`-byte chunks, plus the last chunk.
static std::vector<uint8_t> chunked(size_t total, size_t chunk, const char *trailer = "") {
    std::vector<uint8_t> v;
    size_t sent = 0;
    while (sent < total) {
        size_t n = total - sent < chunk ? total - sent : chunk;
        char line[16];
        int len = snprintf(line, sizeof(line), "%zx\r\n", n);
        v.insert(v.end(), line, line + len);
        for (size_t i = 0; i < n; i++) v.push_back(pattern(sent + i));
        v.push_back('\r');
        v.push_back('\n');
        sent += n;
    }
    const char *last = "0\r\n";
    v.insert(v.end(), last, last + 3);
    v.insert(v.end(), trailer, trailer + strlen(trailer));
    v.push_back('\r');
    v.push_back('\n');
    return v;
}

static std::vector<uint8_t> bytes(const std::string &s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// Feeds `in` in pieces whose sizes cycle through `splits` (0-terminated list,
// null for one shot) until the decoder leaves MORE or the input runs out.
static ChunkDecoder::Status decode(const std::vector<uint8_t> &in, const size_t *splits, uint8_t *out, size_t cap,
                                   size_t *outLength) {
    ChunkDecoder d;
    *outLength = 0;
    size_t pos = 0;
    size_t s = 0;
    ChunkDecoder::Status status = ChunkDecoder::Status::MORE;
    while (pos < in.size()) {
        size_t piece = in.size() - pos;
        if (splits) {
            if (splits[s] == 0) s = 0;
            if (splits[s] < piece) piece = splits[s];
            s++;
        }
        size_t consumed = 0;
        status = d.feed(in.data() + pos, piece, out, cap, outLength, &consumed);
        if (status != ChunkDecoder::Status::MORE) return status;
        TEST_ASSERT_EQUAL_size_t(piece, consumed); // MORE consumes the whole piece
        pos += piece;
    }
    return status;
}

static void assertPattern(const uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (out[i] != pattern(i)) {
            char msg[48];
            snprintf(msg, sizeof(msg), "byte %zu differs", i);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

// ============================================================================
// ChunkDecoder
// ============================================================================

void test_chunk_decoder_simple(void) {
    uint8_t out[64];
    size_t n;
    std::vector<uint8_t> in = bytes("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(11, n);
    TEST_ASSERT_EQUAL_MEMORY("hello world", out, 11);

    // Uppercase and lowercase hex sizes, leading zeros
    in = bytes("A\r\n0123456789\r\n000b\r\nabcdefghijk\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(21, n);
    in = bytes("a\r\n0123456789\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(10, n);

    // Empty body
    in = bytes("0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_chunk_decoder_incomplete_is_more(void) {
    uint8_t out[64];
    size_t n;
    const char *prefixes[] = {"5", "5\r", "5\r\n", "5\r\nhel", "5\r\nhello", "5\r\nhello\r\n", "5\r\nhello\r\n0\r\n",
                              "5\r\nhello\r\n0\r\n\r"};
    for (const char *p : prefixes) {
        TEST_ASSERT_TRUE_MESSAGE(decode(bytes(p), nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::MORE, p);
    }
    ChunkDecoder d;
    n = 0;
    const char *all = "3\r\nabc\r\n0\r\n\r\n";
    TEST_ASSERT_FALSE(d.done());
    TEST_ASSERT_TRUE(d.feed(reinterpret_cast<const uint8_t *>(all), strlen(all), out, sizeof(out), &n) ==
                     ChunkDecoder::Status::DONE);
    TEST_ASSERT_TRUE(d.done());
}

void test_chunk_decoder_extensions_and_trailers(void) {
    uint8_t out[64];
    size_t n;
    std::vector<uint8_t> in = bytes("5;name=value\r\nhello\r\n3;a;b=\"c d\"\r\nabc\r\n0;last\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_MEMORY("helloabc", out, 8);

    in = bytes("5\r\nhello\r\n0\r\nX-Checksum: abc\r\nX-Other: 1\r\n\r\n");
    TEST_ASSERT_TRUE(decode(in, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(5, n);

    // Trailers split byte by byte
    const size_t one[] = {1, 0};
    TEST_ASSERT_TRUE(decode(in, one, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(5, n);
}

void test_chunk_decoder_splits_equal_one_shot(void) {
    std::vector<uint8_t> in = chunked(3000, 97, "X-T: 1\r\n");
    static uint8_t oneShot[4096], split[4096];
    size_t n1, n2;
    TEST_ASSERT_TRUE(decode(in, nullptr, oneShot, sizeof(oneShot), &n1) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(3000, n1);
    assertPattern(oneShot, n1);

    const size_t pattern1[] = {1, 0};
    const size_t pattern2[] = {2, 3, 5, 7, 11, 13, 0};
    const size_t pattern3[] = {64, 65, 1, 4, 0};
    const size_t pattern4[] = {99, 0};
    const size_t *patterns[] = {pattern1, pattern2, pattern3, pattern4};
    for (const size_t *p : patterns) {
        memset(split, 0, sizeof(split));
        TEST_ASSERT_TRUE(decode(in, p, split, sizeof(split), &n2) == ChunkDecoder::Status::DONE);
        TEST_ASSERT_EQUAL_size_t(n1, n2);
        TEST_ASSERT_EQUAL_MEMORY(oneShot, split, n1);
    }
}

void test_chunk_decoder_max_bytes_small(void) {
    static uint8_t out[4096];
    size_t n;
    const size_t one[] = {1, 0};
    // 64-byte chunks: size lines do not count against max_bytes
    TEST_ASSERT_TRUE(decode(chunked(4095, 64), nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(4095, n);
    assertPattern(out, n);
    TEST_ASSERT_TRUE(decode(chunked(4095, 64), one, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(4095, n);

    TEST_ASSERT_TRUE(decode(chunked(4096, 64), nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(4096, n);

    TEST_ASSERT_TRUE(decode(chunked(4097, 64), nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::OVERFLOW);
    TEST_ASSERT_EQUAL_size_t(4096, n);
    assertPattern(out, n);
    TEST_ASSERT_TRUE(decode(chunked(4097, 64), one, out, sizeof(out), &n) == ChunkDecoder::Status::OVERFLOW);
    TEST_ASSERT_EQUAL_size_t(4096, n);
}

void test_chunk_decoder_max_bytes_ceiling(void) {
    std::vector<uint8_t> out(GATEWAY_BODY_MAX);
    size_t n;
    std::vector<uint8_t> under = chunked(131071, 64);
    TEST_ASSERT_TRUE(under.size() > 131072 + 8000); // the size lines add several KB on the wire
    TEST_ASSERT_TRUE(decode(under, nullptr, out.data(), out.size(), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(131071, n);
    assertPattern(out.data(), n);

    const size_t mtu[] = {1460, 0};
    TEST_ASSERT_TRUE(decode(chunked(131073, 64), mtu, out.data(), out.size(), &n) == ChunkDecoder::Status::OVERFLOW);
    TEST_ASSERT_EQUAL_size_t(131072, n);
    assertPattern(out.data(), n);
}

void test_chunk_decoder_size_line_limit(void) {
    uint8_t out[64];
    size_t n;
    const size_t one[] = {1, 0};
    // 128 bytes including the CRLF is accepted
    std::string line128 = "1;" + std::string(124, 'x') + "\r\n";
    TEST_ASSERT_EQUAL_size_t(128, line128.size());
    std::vector<uint8_t> ok = bytes(line128 + "a\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(ok, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_TRUE(decode(ok, one, out, sizeof(out), &n) == ChunkDecoder::Status::DONE);

    std::string line129 = "1;" + std::string(125, 'x') + "\r\n";
    std::vector<uint8_t> bad = bytes(line129 + "a\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(bad, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED);
    TEST_ASSERT_TRUE(decode(bad, one, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED);

    // Spec scenario: a size line of 200 bytes of extensions
    std::vector<uint8_t> ext200 = bytes("1;" + std::string(200, 'e') + "\r\na\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(ext200, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED);
    // An endless size line never terminated by CRLF is refused once past the limit
    std::vector<uint8_t> endless = bytes("1;" + std::string(300, 'e'));
    TEST_ASSERT_TRUE(decode(endless, one, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED);
    // Second chunk's size line too long
    std::vector<uint8_t> later = bytes("1\r\na\r\n1;" + std::string(200, 'e') + "\r\nb\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(decode(later, nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED);
}

void test_chunk_decoder_malformed(void) {
    uint8_t out[64];
    size_t n;
    const char *bad[] = {"zz\r\nhello\r\n0\r\n\r\n",  "\r\nhello\r\n0\r\n\r\n",   "-5\r\nhello\r\n0\r\n\r\n",
                         "g\r\nhello\r\n0\r\n\r\n",   "5\r\nhelloXX\r\n0\r\n\r\n", "0x5\r\nhello\r\n0\r\n\r\n", ";ext\r\nhello\r\n0\r\n\r\n"};
    for (const char *b : bad) {
        TEST_ASSERT_TRUE_MESSAGE(decode(bytes(b), nullptr, out, sizeof(out), &n) == ChunkDecoder::Status::MALFORMED, b);
    }
}

// ============================================================================
// Raw TCP termination
// ============================================================================

void test_clamp_idle_ms(void) {
    TEST_ASSERT_EQUAL_UINT32(300, clampIdleMs(0));
    TEST_ASSERT_EQUAL_UINT32(300, clampIdleMs(-1));
    TEST_ASSERT_EQUAL_UINT32(50, clampIdleMs(1));
    TEST_ASSERT_EQUAL_UINT32(50, clampIdleMs(49));
    TEST_ASSERT_EQUAL_UINT32(50, clampIdleMs(50));
    TEST_ASSERT_EQUAL_UINT32(1000, clampIdleMs(1000));
    TEST_ASSERT_EQUAL_UINT32(5000, clampIdleMs(5000));
    TEST_ASSERT_EQUAL_UINT32(5000, clampIdleMs(5001));
    TEST_ASSERT_EQUAL_UINT32(5000, clampIdleMs(INT64_MAX));
}

void test_tcp_read_stop(void) {
    // Nothing yet, peer silent: keep waiting (idle only counts after the first byte)
    TEST_ASSERT_TRUE(tcpReadStop(false, 0, 4096, 10000, 300, false) == TcpReadStop::CONTINUE);
    TEST_ASSERT_TRUE(tcpReadStop(false, 80, 4096, 100, 300, false) == TcpReadStop::CONTINUE);
    TEST_ASSERT_TRUE(tcpReadStop(false, 80, 4096, 301, 300, false) == TcpReadStop::IDLE);
    TEST_ASSERT_TRUE(tcpReadStop(true, 80, 4096, 0, 300, false) == TcpReadStop::CLOSED);
    TEST_ASSERT_TRUE(tcpReadStop(true, 0, 4096, 0, 300, false) == TcpReadStop::CLOSED);
    TEST_ASSERT_TRUE(tcpReadStop(false, 4096, 4096, 0, 300, false) == TcpReadStop::CAP);
    TEST_ASSERT_TRUE(tcpReadStop(false, 0, 4096, 0, 300, true) == TcpReadStop::DEADLINE);
    TEST_ASSERT_TRUE(tcpReadStop(false, 80, 4096, 10, 300, true) == TcpReadStop::DEADLINE);
}

void test_tcp_reply_truncated(void) {
    TEST_ASSERT_TRUE(tcpReplyTruncated(TcpReadStop::CAP, 4096));
    TEST_ASSERT_TRUE(tcpReplyTruncated(TcpReadStop::DEADLINE, 80)); // cut while still arriving
    TEST_ASSERT_FALSE(tcpReplyTruncated(TcpReadStop::DEADLINE, 0)); // silent peer: NO_REPLY, not truncated
    TEST_ASSERT_FALSE(tcpReplyTruncated(TcpReadStop::IDLE, 80));
    TEST_ASSERT_FALSE(tcpReplyTruncated(TcpReadStop::CLOSED, 80));
}

void run_gateway_stream_tests(void) {
    RUN_TEST(test_chunk_decoder_simple);
    RUN_TEST(test_chunk_decoder_incomplete_is_more);
    RUN_TEST(test_chunk_decoder_extensions_and_trailers);
    RUN_TEST(test_chunk_decoder_splits_equal_one_shot);
    RUN_TEST(test_chunk_decoder_max_bytes_small);
    RUN_TEST(test_chunk_decoder_max_bytes_ceiling);
    RUN_TEST(test_chunk_decoder_size_line_limit);
    RUN_TEST(test_chunk_decoder_malformed);
    RUN_TEST(test_clamp_idle_ms);
    RUN_TEST(test_tcp_read_stop);
    RUN_TEST(test_tcp_reply_truncated);
}
