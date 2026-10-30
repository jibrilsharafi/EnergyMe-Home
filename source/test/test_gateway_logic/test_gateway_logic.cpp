// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for GatewayLogic. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include <cstring>
#include "gateway_logic.h"

using namespace GatewayLogic;

void setUp(void) {}
void tearDown(void) {}

static uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | d;
}

// ============================================================================
// Ops
// ============================================================================

void test_parse_op_known_and_unknown(void) {
    TEST_ASSERT_TRUE(parseOp("http_request") == Op::HTTP_REQUEST);
    TEST_ASSERT_TRUE(parseOp("net_scan") == Op::NET_SCAN);
    TEST_ASSERT_TRUE(parseOp("udp_send") == Op::UDP_SEND);
    TEST_ASSERT_TRUE(parseOp("tunnel_open") == Op::TUNNEL_OPEN);
    TEST_ASSERT_TRUE(parseOp("tunnel_close") == Op::TUNNEL_CLOSE);
    TEST_ASSERT_TRUE(parseOp("telnet") == Op::UNKNOWN);
    TEST_ASSERT_TRUE(parseOp("HTTP_REQUEST") == Op::UNKNOWN);
    TEST_ASSERT_TRUE(parseOp(nullptr) == Op::UNKNOWN);
    TEST_ASSERT_EQUAL_STRING("modbus_write", opName(Op::MODBUS_WRITE));
}

// ============================================================================
// IPv4 parsing and private ranges
// ============================================================================

void test_parse_ipv4_valid(void) {
    uint8_t o[4];
    TEST_ASSERT_TRUE(parseIpv4("192.168.2.72", o));
    TEST_ASSERT_EQUAL_UINT8(192, o[0]);
    TEST_ASSERT_EQUAL_UINT8(72, o[3]);
    TEST_ASSERT_TRUE(parseIpv4("0.0.0.0", o));
    TEST_ASSERT_TRUE(parseIpv4("255.255.255.255", o));
}

void test_parse_ipv4_rejects_malformed(void) {
    uint8_t o[4];
    const char *bad[] = {"", "192.168.1", "192.168.1.1.1", "192.168.1.256", "192.168.01.1", "0x7f.0.0.1",
                         " 192.168.1.1", "192.168.1.1 ", "192.168.1.1/24", "192.168.1.-1", "+1.2.3.4",
                         "1..2.3", "1234.1.1.1", "192.168.1.1.evil.com", "router.local", "2130706433"};
    for (const char *s : bad) {
        TEST_ASSERT_FALSE_MESSAGE(parseIpv4(s, o), s);
    }
    TEST_ASSERT_FALSE(parseIpv4(nullptr, o));
}

void test_private_ranges_accepted(void) {
    TEST_ASSERT_TRUE(isPrivateIpv4("10.0.0.1"));
    TEST_ASSERT_TRUE(isPrivateIpv4("10.255.255.254"));
    TEST_ASSERT_TRUE(isPrivateIpv4("172.16.0.1"));
    TEST_ASSERT_TRUE(isPrivateIpv4("172.31.255.1"));
    TEST_ASSERT_TRUE(isPrivateIpv4("192.168.2.72"));
}

void test_non_private_rejected(void) {
    const char *bad[] = {"8.8.8.8", "127.0.0.1", "169.254.1.1", "172.15.0.1", "172.32.0.1", "192.169.1.1",
                         "224.0.0.251", "255.255.255.255", "0.0.0.0", "100.64.0.1", "11.0.0.1"};
    for (const char *s : bad) {
        TEST_ASSERT_FALSE_MESSAGE(isPrivateIpv4(s), s);
    }
}

// ============================================================================
// Clamps and HTTP validation
// ============================================================================

void test_timeout_clamp(void) {
    TEST_ASSERT_EQUAL_UINT32(TIMEOUT_DEFAULT_MS, clampTimeoutMs(0));
    TEST_ASSERT_EQUAL_UINT32(TIMEOUT_DEFAULT_MS, clampTimeoutMs(-5));
    TEST_ASSERT_EQUAL_UINT32(TIMEOUT_MIN_MS, clampTimeoutMs(10));
    TEST_ASSERT_EQUAL_UINT32(2500, clampTimeoutMs(2500));
    TEST_ASSERT_EQUAL_UINT32(TIMEOUT_MAX_MS, clampTimeoutMs(60000));
}

void test_udp_wait_and_ping_clamp(void) {
    TEST_ASSERT_EQUAL_UINT32(0, clampUdpWaitMs(-1));
    TEST_ASSERT_EQUAL_UINT32(1000, clampUdpWaitMs(1000));
    TEST_ASSERT_EQUAL_UINT32(UDP_WAIT_MAX_MS, clampUdpWaitMs(99999));
    TEST_ASSERT_EQUAL_UINT32(PING_COUNT_DEFAULT, clampPingCount(0));
    TEST_ASSERT_EQUAL_UINT32(1, clampPingCount(1));
    TEST_ASSERT_EQUAL_UINT32(PING_COUNT_MAX, clampPingCount(100));
}

void test_port_and_method(void) {
    TEST_ASSERT_TRUE(isValidPort(80));
    TEST_ASSERT_TRUE(isValidPort(65535));
    TEST_ASSERT_FALSE(isValidPort(0));
    TEST_ASSERT_FALSE(isValidPort(65536));
    TEST_ASSERT_TRUE(isAllowedHttpMethod("GET"));
    TEST_ASSERT_TRUE(isAllowedHttpMethod("DELETE"));
    TEST_ASSERT_FALSE(isAllowedHttpMethod("get"));
    TEST_ASSERT_FALSE(isAllowedHttpMethod("CONNECT"));
    TEST_ASSERT_FALSE(isAllowedHttpMethod(nullptr));
}

void test_http_path(void) {
    TEST_ASSERT_TRUE(isValidHttpPath("/"));
    TEST_ASSERT_TRUE(isValidHttpPath("/rpc/Switch.Set?id=0&on=true"));
    TEST_ASSERT_FALSE(isValidHttpPath(""));
    TEST_ASSERT_FALSE(isValidHttpPath("rpc"));
    TEST_ASSERT_FALSE(isValidHttpPath("http://8.8.8.8/"));
    TEST_ASSERT_FALSE(isValidHttpPath("/a b"));
    TEST_ASSERT_FALSE(isValidHttpPath("/a\r\nHost: evil"));
    TEST_ASSERT_FALSE(isValidHttpPath(nullptr));
    char longPath[HTTP_PATH_MAX_LEN + 2];
    memset(longPath, 'a', sizeof(longPath));
    longPath[0] = '/';
    longPath[sizeof(longPath) - 1] = '\0';
    TEST_ASSERT_FALSE(isValidHttpPath(longPath));
}

void test_headers(void) {
    TEST_ASSERT_TRUE(isValidHeaderName("Content-Type"));
    TEST_ASSERT_TRUE(isValidHeaderName("X-Auth_1"));
    TEST_ASSERT_FALSE(isValidHeaderName(""));
    TEST_ASSERT_FALSE(isValidHeaderName("Bad Name"));
    TEST_ASSERT_FALSE(isValidHeaderName("Bad:Name"));
    TEST_ASSERT_TRUE(isValidHeaderValue("application/json"));
    TEST_ASSERT_TRUE(isValidHeaderValue(""));
    TEST_ASSERT_FALSE(isValidHeaderValue("x\r\nHost: evil"));
}

// ============================================================================
// Modbus TCP framing
// ============================================================================

void test_modbus_counts(void) {
    TEST_ASSERT_TRUE(isValidModbusReadCount(3, 125));
    TEST_ASSERT_FALSE(isValidModbusReadCount(3, 126));
    TEST_ASSERT_TRUE(isValidModbusReadCount(1, 2000));
    TEST_ASSERT_FALSE(isValidModbusReadCount(1, 0));
    TEST_ASSERT_FALSE(isValidModbusReadCount(5, 1));
    TEST_ASSERT_TRUE(isValidModbusWriteCount(6, 1));
    TEST_ASSERT_FALSE(isValidModbusWriteCount(6, 2));
    TEST_ASSERT_TRUE(isValidModbusWriteCount(16, 123));
    TEST_ASSERT_FALSE(isValidModbusWriteCount(16, 124));
    TEST_ASSERT_TRUE(isValidModbusWriteCount(15, 1968));
    TEST_ASSERT_FALSE(isValidModbusWriteValue(5, 2));
    TEST_ASSERT_TRUE(isValidModbusWriteValue(16, 65535));
    TEST_ASSERT_FALSE(isValidModbusWriteValue(16, 65536));
    TEST_ASSERT_FALSE(isValidModbusWriteValue(6, -1));
}

void test_modbus_build_read(void) {
    uint8_t buf[MODBUS_MAX_FRAME];
    size_t n = buildModbusReadRequest(buf, sizeof(buf), 0x1234, 1, 3, 0x0010, 2);
    const uint8_t expected[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x10, 0x00, 0x02};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, n);
    TEST_ASSERT_EQUAL_size_t(0, buildModbusReadRequest(buf, sizeof(buf), 1, 1, 3, 0, 200));
    TEST_ASSERT_EQUAL_size_t(0, buildModbusReadRequest(buf, 5, 1, 1, 3, 0, 1));
}

void test_modbus_build_write_single(void) {
    uint8_t buf[MODBUS_MAX_FRAME];
    uint16_t on = 1;
    size_t n = buildModbusWriteRequest(buf, sizeof(buf), 7, 1, 5, 0x0003, &on, 1);
    const uint8_t coil[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x01, 0x05, 0x00, 0x03, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(coil), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(coil, buf, n);

    uint16_t reg = 0xABCD;
    n = buildModbusWriteRequest(buf, sizeof(buf), 8, 2, 6, 0x0001, &reg, 1);
    const uint8_t single[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x02, 0x06, 0x00, 0x01, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(single, buf, n);
}

void test_modbus_build_write_multiple(void) {
    uint8_t buf[MODBUS_MAX_FRAME];
    uint16_t regs[] = {0x0001, 0x0203};
    size_t n = buildModbusWriteRequest(buf, sizeof(buf), 1, 1, 16, 0x0100, regs, 2);
    const uint8_t expected[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x01, 0x00,
                                0x00, 0x02, 0x04, 0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf, n);

    uint16_t coils[] = {1, 0, 1, 1, 0, 0, 0, 0, 1, 1};
    n = buildModbusWriteRequest(buf, sizeof(buf), 2, 1, 15, 0x0013, coils, 10);
    const uint8_t expectedCoils[] = {0x00, 0x02, 0x00, 0x00, 0x00, 0x09, 0x01, 0x0F, 0x00, 0x13,
                                     0x00, 0x0A, 0x02, 0x0D, 0x03};
    TEST_ASSERT_EQUAL_size_t(sizeof(expectedCoils), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedCoils, buf, n);

    uint16_t badCoil = 2;
    TEST_ASSERT_EQUAL_size_t(0, buildModbusWriteRequest(buf, sizeof(buf), 1, 1, 15, 0, &badCoil, 1));
}

void test_modbus_frame_length(void) {
    const uint8_t hdr[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x07};
    TEST_ASSERT_EQUAL_size_t(13, modbusFrameLength(hdr, 6));
    const uint8_t wrongProto[] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x07};
    TEST_ASSERT_EQUAL_size_t(0, modbusFrameLength(wrongProto, 6));
    const uint8_t tooLong[] = {0x00, 0x01, 0x00, 0x00, 0x10, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, modbusFrameLength(tooLong, 6));
    TEST_ASSERT_EQUAL_size_t(0, modbusFrameLength(hdr, 5));
}

void test_modbus_parse_registers(void) {
    const uint8_t resp[] = {0x12, 0x34, 0x00, 0x00, 0x00, 0x07, 0x01, 0x03, 0x04, 0x00, 0x2A, 0xFF, 0xFF};
    uint16_t values[4];
    size_t count = 0;
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 0x1234, 1, 3, 2, values, 4, &count, nullptr) == ModbusParse::OK);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_EQUAL_UINT16(42, values[0]);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, values[1]);

    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 0x9999, 1, 3, 2, values, 4, &count, nullptr) == ModbusParse::MALFORMED);
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 0x1234, 2, 3, 2, values, 4, &count, nullptr) == ModbusParse::MALFORMED);
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 0x1234, 1, 3, 3, values, 4, &count, nullptr) == ModbusParse::MALFORMED);
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp) - 1, 0x1234, 1, 3, 2, values, 4, &count, nullptr) == ModbusParse::MALFORMED);
}

void test_modbus_parse_bits(void) {
    const uint8_t resp[] = {0x00, 0x05, 0x00, 0x00, 0x00, 0x05, 0x01, 0x01, 0x02, 0xCD, 0x01};
    uint16_t values[16];
    size_t count = 0;
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 5, 1, 1, 10, values, 16, &count, nullptr) == ModbusParse::OK);
    TEST_ASSERT_EQUAL_size_t(10, count);
    const uint16_t expected[] = {1, 0, 1, 1, 0, 0, 1, 1, 1, 0};
    TEST_ASSERT_EQUAL_UINT16_ARRAY(expected, values, 10);
}

void test_modbus_parse_exception(void) {
    const uint8_t resp[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01, 0x83, 0x02};
    uint16_t values[2];
    size_t count = 0;
    uint8_t exc = 0;
    TEST_ASSERT_TRUE(parseModbusResponse(resp, sizeof(resp), 1, 1, 3, 2, values, 2, &count, &exc) == ModbusParse::EXCEPTION);
    TEST_ASSERT_EQUAL_UINT8(2, exc);
}

void test_modbus_parse_write_echo(void) {
    const uint8_t resp16[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x10, 0x01, 0x00, 0x00, 0x02};
    size_t count = 0;
    TEST_ASSERT_TRUE(parseModbusResponse(resp16, sizeof(resp16), 1, 1, 16, 2, nullptr, 0, &count, nullptr) == ModbusParse::OK);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_TRUE(parseModbusResponse(resp16, sizeof(resp16), 1, 1, 16, 3, nullptr, 0, &count, nullptr) == ModbusParse::MALFORMED);

    const uint8_t resp6[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x06, 0x02, 0x06, 0x00, 0x01, 0xAB, 0xCD};
    TEST_ASSERT_TRUE(parseModbusResponse(resp6, sizeof(resp6), 8, 2, 6, 1, nullptr, 0, &count, nullptr) == ModbusParse::OK);
    TEST_ASSERT_EQUAL_size_t(1, count);
}

// ============================================================================
// Scan range
// ============================================================================

void test_scan_targets_slash24(void) {
    uint32_t hosts[256];
    size_t n = buildScanTargets(ip(192, 168, 2, 155), 0xFFFFFF00u, hosts, 256);
    TEST_ASSERT_EQUAL_size_t(253, n);
    TEST_ASSERT_EQUAL_UINT32(ip(192, 168, 2, 1), hosts[0]);
    TEST_ASSERT_EQUAL_UINT32(ip(192, 168, 2, 254), hosts[n - 1]);
    for (size_t i = 0; i < n; i++) TEST_ASSERT_NOT_EQUAL(ip(192, 168, 2, 155), hosts[i]);
}

void test_scan_targets_wider_subnet_capped_to_slash24(void) {
    uint32_t hosts[256];
    size_t n = buildScanTargets(ip(10, 1, 7, 20), 0xFFFF0000u, hosts, 256);
    TEST_ASSERT_EQUAL_size_t(253, n);
    TEST_ASSERT_EQUAL_UINT32(ip(10, 1, 7, 1), hosts[0]);
}

void test_scan_targets_narrow_subnet(void) {
    uint32_t hosts[256];
    size_t n = buildScanTargets(ip(192, 168, 1, 10), 0xFFFFFFF8u, hosts, 256);
    TEST_ASSERT_EQUAL_size_t(5, n); // .9-.14 minus self
    TEST_ASSERT_EQUAL_UINT32(ip(192, 168, 1, 9), hosts[0]);
}

// ============================================================================
// Private octets, reserved headers, HTTP/1.0 response parsing
// ============================================================================

void test_private_octets_overload(void) {
    const uint8_t a[4] = {192, 168, 2, 72};
    const uint8_t b[4] = {8, 8, 8, 8};
    const uint8_t c[4] = {172, 31, 0, 1};
    TEST_ASSERT_TRUE(isPrivateIpv4(a));
    TEST_ASSERT_FALSE(isPrivateIpv4(b));
    TEST_ASSERT_TRUE(isPrivateIpv4(c));
}

void test_reserved_headers(void) {
    TEST_ASSERT_TRUE(isReservedHeaderName("Host"));
    TEST_ASSERT_TRUE(isReservedHeaderName("content-length"));
    TEST_ASSERT_TRUE(isReservedHeaderName("Content-Length"));
    TEST_ASSERT_TRUE(isReservedHeaderName("TRANSFER-ENCODING"));
    TEST_ASSERT_TRUE(isReservedHeaderName("Connection"));
    TEST_ASSERT_TRUE(isReservedHeaderName("Expect"));
    TEST_ASSERT_TRUE(isReservedHeaderName(nullptr));
    TEST_ASSERT_FALSE(isReservedHeaderName("Content-Type"));
    TEST_ASSERT_FALSE(isReservedHeaderName("Authorization"));
    TEST_ASSERT_FALSE(isReservedHeaderName("Hosts"));
}

static const uint8_t *u(const char *s) { return reinterpret_cast<const uint8_t *>(s); }

void test_http_header_end_and_status(void) {
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    size_t end = httpHeaderEnd(u(resp), strlen(resp));
    TEST_ASSERT_EQUAL_size_t(strlen(resp) - 5, end);
    int code = 0;
    TEST_ASSERT_TRUE(httpStatusCode(u(resp), end, &code));
    TEST_ASSERT_EQUAL_INT(200, code);
    TEST_ASSERT_EQUAL_size_t(0, httpHeaderEnd(u("HTTP/1.1 200 OK\r\nX: y\r\n"), 24));

    const char *noReason = "HTTP/1.0 204\r\n\r\n";
    TEST_ASSERT_TRUE(httpStatusCode(u(noReason), strlen(noReason), &code));
    TEST_ASSERT_EQUAL_INT(204, code);
    TEST_ASSERT_FALSE(httpStatusCode(u("HTTP/2 200 OK\r\n\r\n"), 17, &code));
    TEST_ASSERT_FALSE(httpStatusCode(u("HTTP/1.1 2x0 OK\r\n\r\n"), 19, &code));
    TEST_ASSERT_FALSE(httpStatusCode(u("HTTP/1.1 2000 OK\r\n\r\n"), 20, &code));
    TEST_ASSERT_FALSE(httpStatusCode(u("garbage"), 7, &code));
}

void test_http_content_length(void) {
    const char *h = "HTTP/1.1 200 OK\r\ncontent-LENGTH:  1234\r\n\r\n";
    TEST_ASSERT_EQUAL_INT64(1234, httpContentLength(u(h), strlen(h)));
    const char *none = "HTTP/1.1 200 OK\r\nX-Content-Length: 9\r\n\r\n";
    TEST_ASSERT_EQUAL_INT64(-1, httpContentLength(u(none), strlen(none)));
    const char *bad = "HTTP/1.1 200 OK\r\nContent-Length: 12a\r\n\r\n";
    TEST_ASSERT_EQUAL_INT64(-1, httpContentLength(u(bad), strlen(bad)));
}

void test_http_chunked_detection(void) {
    const char *h = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, Chunked\r\n\r\n";
    TEST_ASSERT_TRUE(httpIsChunked(u(h), strlen(h)));
    const char *n = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n";
    TEST_ASSERT_FALSE(httpIsChunked(u(n), strlen(n)));
}

void test_decode_chunked(void) {
    const char *body = "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n";
    uint8_t out[32];
    size_t n = 0;
    TEST_ASSERT_TRUE(decodeChunked(u(body), strlen(body), out, sizeof(out), &n) == Chunked::COMPLETE);
    TEST_ASSERT_EQUAL_size_t(9, n);
    TEST_ASSERT_EQUAL_MEMORY("Wikipedia", out, 9);

    TEST_ASSERT_TRUE(decodeChunked(u(body), 12, out, sizeof(out), &n) == Chunked::INCOMPLETE);
    TEST_ASSERT_EQUAL_size_t(4, n);

    TEST_ASSERT_TRUE(decodeChunked(u(body), strlen(body), out, 6, &n) == Chunked::INCOMPLETE);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_MEMORY("Wikipe", out, 6);

    const char *bad = "4\r\nWikiXX5\r\npedia\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(decodeChunked(u(bad), strlen(bad), out, sizeof(out), &n) == Chunked::MALFORMED);
    const char *huge = "FFFFFFFF\r\n";
    TEST_ASSERT_TRUE(decodeChunked(u(huge), strlen(huge), out, sizeof(out), &n) == Chunked::MALFORMED);
}

void test_http_method_has_body(void) {
    TEST_ASSERT_TRUE(httpMethodHasBody("POST"));
    TEST_ASSERT_TRUE(httpMethodHasBody("PUT"));
    TEST_ASSERT_TRUE(httpMethodHasBody("PATCH"));
    TEST_ASSERT_FALSE(httpMethodHasBody("GET"));
    TEST_ASSERT_FALSE(httpMethodHasBody("DELETE"));
    TEST_ASSERT_FALSE(httpMethodHasBody(nullptr));
}

void test_http_content_length_digit_cap(void) {
    const char *nine = "HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n";
    TEST_ASSERT_EQUAL_INT64(999999999, httpContentLength(u(nine), strlen(nine)));
    const char *ten = "HTTP/1.1 200 OK\r\nContent-Length: 4294967296\r\n\r\n";
    TEST_ASSERT_EQUAL_INT64(-1, httpContentLength(u(ten), strlen(ten)));
}

void test_http_chunked_ended(void) {
    const char *done = "4\r\nWiki\r\n0\r\n\r\n";
    TEST_ASSERT_TRUE(httpChunkedEnded(u(done), strlen(done)));
    TEST_ASSERT_TRUE(httpChunkedEnded(u("0\r\n\r\n"), 5));
    const char *open = "4\r\nWiki\r\n";
    TEST_ASSERT_FALSE(httpChunkedEnded(u(open), strlen(open)));
    const char *tenChunk = "a\r\n0123456780\r\n\r\n"; // data ending in "0" + CRLF is not a last chunk
    TEST_ASSERT_FALSE(httpChunkedEnded(u(tenChunk), strlen(tenChunk)));
    TEST_ASSERT_FALSE(httpChunkedEnded(nullptr, 0));
}

// ============================================================================
// UTF-8 text bodies
// ============================================================================

static bool text(const uint8_t *data, size_t len, bool truncated, size_t *textLen) {
    *textLen = 12345;
    return utf8TextLength(data, len, truncated, textLen);
}

void test_utf8_ascii_and_json(void) {
    size_t n;
    const char *json = "{\"ok\":true,\"rows\":[1,2]}\r\n\tend";
    TEST_ASSERT_TRUE(text(u(json), strlen(json), false, &n));
    TEST_ASSERT_EQUAL_size_t(strlen(json), n);
}

void test_utf8_multibyte(void) {
    size_t n;
    const char *mixed = "\xC3\xA8 \xE2\x82\xAC \xF0\x9F\x98\x80 \xED\x9F\xBF \xF4\x8F\xBF\xBF"; // e-grave, euro, emoji, U+D7FF, U+10FFFF
    TEST_ASSERT_TRUE(text(u(mixed), strlen(mixed), false, &n));
    TEST_ASSERT_EQUAL_size_t(strlen(mixed), n);
}

void test_utf8_rejects_invalid(void) {
    size_t n;
    const uint8_t bad[][4] = {
        {0x80, 0, 0, 0},             // stray continuation
        {0xC0, 0xAF, 0, 0},          // overlong '/'
        {0xC1, 0xBF, 0, 0},          // overlong
        {0xE0, 0x9F, 0xBF, 0},       // overlong 3-byte
        {0xED, 0xA0, 0x80, 0},       // surrogate U+D800
        {0xF0, 0x8F, 0xBF, 0xBF},    // overlong 4-byte
        {0xF4, 0x90, 0x80, 0x80},    // above U+10FFFF
        {0xF5, 0x80, 0x80, 0x80},    // F5-FF never valid
        {0xE2, 0x28, 0xA1, 0},       // bad continuation
    };
    const size_t lens[] = {1, 2, 2, 3, 3, 4, 4, 4, 3};
    for (size_t i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        TEST_ASSERT_FALSE_MESSAGE(text(bad[i], lens[i], false, &n), "untruncated");
        TEST_ASSERT_FALSE_MESSAGE(text(bad[i], lens[i], true, &n), "truncated");
    }
    const uint8_t latin1[] = {'c', 'a', 'f', 0xE9}; // ISO-8859-1, not UTF-8
    TEST_ASSERT_FALSE(text(latin1, sizeof(latin1), false, &n));
}

void test_utf8_rejects_nul_and_controls(void) {
    size_t n;
    const uint8_t nul[] = {'a', 0x00, 'b'};
    TEST_ASSERT_FALSE(text(nul, sizeof(nul), false, &n));
    const uint8_t esc[] = {'a', 0x1B, 'b'};
    TEST_ASSERT_FALSE(text(esc, sizeof(esc), false, &n));
    const uint8_t del[] = {'a', 0x7F, 'b'};
    TEST_ASSERT_TRUE(text(del, sizeof(del), false, &n));
}

void test_utf8_empty_is_not_text(void) {
    size_t n;
    TEST_ASSERT_FALSE(text(u(""), 0, false, &n));
    TEST_ASSERT_FALSE(utf8TextLength(nullptr, 0, false, &n));
    TEST_ASSERT_FALSE(utf8TextLength(u("a"), 1, false, nullptr));
}

void test_utf8_truncated_at_cap(void) {
    static uint8_t body[RESPONSE_CAP_BYTES];
    memset(body, 'x', sizeof(body));
    body[sizeof(body) - 2] = 0xE2; // euro sign cut after its second byte
    body[sizeof(body) - 1] = 0x82;
    size_t n;
    TEST_ASSERT_TRUE(text(body, sizeof(body), true, &n));
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 2, n);
    TEST_ASSERT_FALSE(text(body, sizeof(body), false, &n)); // the same bytes in a complete reply are invalid

    body[sizeof(body) - 1] = 0xF0; // lead byte alone at the very end
    body[sizeof(body) - 2] = 'x';
    TEST_ASSERT_TRUE(text(body, sizeof(body), true, &n));
    TEST_ASSERT_EQUAL_size_t(sizeof(body) - 1, n);

    body[sizeof(body) - 2] = 0xE2; // invalid prefix: a lead byte cannot follow a lead byte
    TEST_ASSERT_FALSE(text(body, sizeof(body), true, &n));
}

void test_utf8_truncated_to_nothing_is_not_text(void) {
    size_t n;
    const uint8_t lone[] = {0xE2};
    TEST_ASSERT_FALSE(text(lone, sizeof(lone), true, &n));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_op_known_and_unknown);
    RUN_TEST(test_parse_ipv4_valid);
    RUN_TEST(test_parse_ipv4_rejects_malformed);
    RUN_TEST(test_private_ranges_accepted);
    RUN_TEST(test_non_private_rejected);
    RUN_TEST(test_timeout_clamp);
    RUN_TEST(test_udp_wait_and_ping_clamp);
    RUN_TEST(test_port_and_method);
    RUN_TEST(test_http_path);
    RUN_TEST(test_headers);
    RUN_TEST(test_modbus_counts);
    RUN_TEST(test_modbus_build_read);
    RUN_TEST(test_modbus_build_write_single);
    RUN_TEST(test_modbus_build_write_multiple);
    RUN_TEST(test_modbus_frame_length);
    RUN_TEST(test_modbus_parse_registers);
    RUN_TEST(test_modbus_parse_bits);
    RUN_TEST(test_modbus_parse_exception);
    RUN_TEST(test_modbus_parse_write_echo);
    RUN_TEST(test_scan_targets_slash24);
    RUN_TEST(test_scan_targets_wider_subnet_capped_to_slash24);
    RUN_TEST(test_scan_targets_narrow_subnet);
    RUN_TEST(test_private_octets_overload);
    RUN_TEST(test_reserved_headers);
    RUN_TEST(test_http_header_end_and_status);
    RUN_TEST(test_http_content_length);
    RUN_TEST(test_http_chunked_detection);
    RUN_TEST(test_decode_chunked);
    RUN_TEST(test_http_method_has_body);
    RUN_TEST(test_http_content_length_digit_cap);
    RUN_TEST(test_http_chunked_ended);
    RUN_TEST(test_utf8_ascii_and_json);
    RUN_TEST(test_utf8_multibyte);
    RUN_TEST(test_utf8_rejects_invalid);
    RUN_TEST(test_utf8_rejects_nul_and_controls);
    RUN_TEST(test_utf8_empty_is_not_text);
    RUN_TEST(test_utf8_truncated_at_cap);
    RUN_TEST(test_utf8_truncated_to_nothing_is_not_text);
    return UNITY_END();
}
