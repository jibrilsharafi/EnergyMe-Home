// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for SecretsLogic (openspec capability: gateway-secrets),
// derived from the spec. Run with:
//   pio test -e native -f test_secrets_logic   (from WSL)

#include <unity.h>
#include "secrets_logic.h"
#include <cstdio>
#include <cstring>
#include "gateway_template.h"

using namespace SecretsLogic;
using GatewayLogic::ExpandResult;
using GatewayLogic::ExpandStatus;
using GatewayLogic::Module;
using GatewayLogic::ResolveContext;
using GatewayLogic::ResolveOutput;
using GatewayLogic::ResolveStatus;
using GatewayLogic::Resolver;

void setUp(void) {}
void tearDown(void) {}

static const uint64_t NOW = 1793450000ULL;
static const uint8_t IP_XWEB[4] = {192, 168, 1, 50};
static const uint8_t MAC_XWEB[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t IP_OTHER[4] = {192, 168, 1, 99};
static const uint8_t IP_ROUTED[4] = {10, 20, 0, 5};

static SlotMeta meta(const char *name, const uint8_t ip[4], uint16_t port, const uint8_t *mac, uint64_t setAt) {
    SlotMeta m{};
    snprintf(m.name, sizeof(m.name), "%s", name);
    memcpy(m.ip, ip, 4);
    m.port = port;
    m.hasMac = mac != nullptr;
    if (mac != nullptr) memcpy(m.mac, mac, 6);
    m.setAt = setAt;
    return m;
}

static const uint8_t *u8(const char *s) { return reinterpret_cast<const uint8_t *>(s); }

// ============================================================================
// Name and value rules
// ============================================================================

void test_name_rule(void) {
    TEST_ASSERT_TRUE(isValidName("a"));
    TEST_ASSERT_TRUE(isValidName("xweb_admin"));
    TEST_ASSERT_TRUE(isValidName("a-b_c9"));
    TEST_ASSERT_TRUE(isValidName("abcdefghijklmnopqrstuvwx"));   // 24
    TEST_ASSERT_FALSE(isValidName("abcdefghijklmnopqrstuvwxy")); // 25
    TEST_ASSERT_FALSE(isValidName("XWEB admin"));
    TEST_ASSERT_FALSE(isValidName("Xweb"));
    TEST_ASSERT_FALSE(isValidName("xweB"));
    TEST_ASSERT_FALSE(isValidName("1abc"));
    TEST_ASSERT_FALSE(isValidName("_abc"));
    TEST_ASSERT_FALSE(isValidName("-abc"));
    TEST_ASSERT_FALSE(isValidName("a.b"));
    TEST_ASSERT_FALSE(isValidName("a b"));
    TEST_ASSERT_FALSE(isValidName(""));
    TEST_ASSERT_FALSE(isValidName(nullptr));
}

void test_value_rule_lengths(void) {
    uint8_t v[258];
    memset(v, 'a', sizeof(v));
    TEST_ASSERT_FALSE(isValidValue(v, 0));
    TEST_ASSERT_TRUE(isValidValue(v, 1));
    TEST_ASSERT_TRUE(isValidValue(v, 256));
    TEST_ASSERT_FALSE(isValidValue(v, 257));
    TEST_ASSERT_FALSE(isValidValue(nullptr, 4));
}

void test_value_rule_forbidden_bytes(void) {
    TEST_ASSERT_FALSE(isValidValue(u8("ab\0cd"), 5));
    TEST_ASSERT_FALSE(isValidValue(u8("ab\rcd"), 5));
    TEST_ASSERT_FALSE(isValidValue(u8("ab\ncd"), 5));
    TEST_ASSERT_TRUE(isValidValue(u8("ab\tcd"), 5));
    TEST_ASSERT_TRUE(isValidValue(u8("${secret:other}"), 15));
}

void test_value_rule_strict_utf8(void) {
    const uint8_t euro[] = {0xE2, 0x82, 0xAC};
    const uint8_t emoji[] = {0xF0, 0x9F, 0x98, 0x80};
    const uint8_t maxCp[] = {0xF4, 0x8F, 0xBF, 0xBF}; // U+10FFFF
    const uint8_t twoByte[] = {0xC3, 0xA9};           // e acute
    TEST_ASSERT_TRUE(isValidValue(euro, 3));
    TEST_ASSERT_TRUE(isValidValue(emoji, 4));
    TEST_ASSERT_TRUE(isValidValue(maxCp, 4));
    TEST_ASSERT_TRUE(isValidValue(twoByte, 2));

    const uint8_t overlong2[] = {0xC0, 0x80};
    const uint8_t overlong2b[] = {0xC1, 0xBF};
    const uint8_t overlong3[] = {0xE0, 0x80, 0x80};
    const uint8_t overlong4[] = {0xF0, 0x80, 0x80, 0x80};
    const uint8_t surrogateLow[] = {0xED, 0xA0, 0x80};  // U+D800
    const uint8_t surrogateHigh[] = {0xED, 0xBF, 0xBF}; // U+DFFF
    const uint8_t aboveMax[] = {0xF4, 0x90, 0x80, 0x80}; // U+110000
    const uint8_t f5[] = {0xF5, 0x80, 0x80, 0x80};
    const uint8_t truncated3[] = {'a', 0xE2, 0x82};
    const uint8_t truncated4[] = {0xF0, 0x9F, 0x98};
    const uint8_t loneContinuation[] = {'a', 0x80, 'b'};
    const uint8_t badContinuation[] = {0xE2, 0x41, 0xAC};
    const uint8_t ff[] = {0xFF};
    TEST_ASSERT_FALSE(isValidValue(overlong2, 2));
    TEST_ASSERT_FALSE(isValidValue(overlong2b, 2));
    TEST_ASSERT_FALSE(isValidValue(overlong3, 3));
    TEST_ASSERT_FALSE(isValidValue(overlong4, 4));
    TEST_ASSERT_FALSE(isValidValue(surrogateLow, 3));
    TEST_ASSERT_FALSE(isValidValue(surrogateHigh, 3));
    TEST_ASSERT_FALSE(isValidValue(aboveMax, 4));
    TEST_ASSERT_FALSE(isValidValue(f5, 4));
    TEST_ASSERT_FALSE(isValidValue(truncated3, 3));
    TEST_ASSERT_FALSE(isValidValue(truncated4, 3));
    TEST_ASSERT_FALSE(isValidValue(loneContinuation, 3));
    TEST_ASSERT_FALSE(isValidValue(badContinuation, 3));
    TEST_ASSERT_FALSE(isValidValue(ff, 1));
}

// ============================================================================
// Slot blob
// ============================================================================

void test_slot_encode_exact_offsets(void) {
    uint8_t mac[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    SlotMeta m = meta("xweb_admin", IP_XWEB, 8080, mac, 0x0102030405060708ULL);
    uint8_t blob[SLOT_MAX_BYTES + 8];
    memset(blob, 0xEE, sizeof(blob));
    size_t n = encodeSlot(m, u8("s3cret"), 6, blob, sizeof(blob));
    TEST_ASSERT_EQUAL_size_t(48 + 6, n);
    TEST_ASSERT_EQUAL_UINT8(1, blob[0]);
    TEST_ASSERT_EQUAL_UINT8(10, blob[1]);
    TEST_ASSERT_EQUAL_MEMORY("xweb_admin", blob + 2, 10);
    for (size_t i = 2 + 10; i < 26; i++) TEST_ASSERT_EQUAL_UINT8(0, blob[i]);
    TEST_ASSERT_EQUAL_MEMORY(IP_XWEB, blob + 26, 4);
    TEST_ASSERT_EQUAL_UINT8(0x90, blob[30]); // 8080 = 0x1F90 LE
    TEST_ASSERT_EQUAL_UINT8(0x1F, blob[31]);
    TEST_ASSERT_EQUAL_MEMORY(mac, blob + 32, 6);
    const uint8_t setAt[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_MEMORY(setAt, blob + 38, 8);
    TEST_ASSERT_EQUAL_UINT8(6, blob[46]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[47]);
    TEST_ASSERT_EQUAL_MEMORY("s3cret", blob + 48, 6);
    TEST_ASSERT_EQUAL_UINT8(0xEE, blob[54]); // nothing past 48 + len
}

void test_slot_encode_unbound_mac_and_256_value(void) {
    SlotMeta m = meta("abcdefghijklmnopqrstuvwx", IP_XWEB, 80, nullptr, NOW);
    uint8_t value[256];
    memset(value, 'v', sizeof(value));
    uint8_t blob[SLOT_MAX_BYTES];
    TEST_ASSERT_EQUAL_size_t(SLOT_MAX_BYTES, encodeSlot(m, value, 256, blob, sizeof(blob)));
    TEST_ASSERT_EQUAL_UINT8(24, blob[1]);
    for (size_t i = 32; i < 38; i++) TEST_ASSERT_EQUAL_UINT8(0, blob[i]);
    TEST_ASSERT_EQUAL_UINT8(0x00, blob[46]); // 256 = 0x0100 LE
    TEST_ASSERT_EQUAL_UINT8(0x01, blob[47]);
    TEST_ASSERT_EQUAL_UINT8(80, blob[30]);
    TEST_ASSERT_EQUAL_UINT8(0, blob[31]);
}

void test_slot_encode_refuses_bad_input(void) {
    uint8_t blob[SLOT_MAX_BYTES];
    SlotMeta m = meta("xweb_admin", IP_XWEB, 80, MAC_XWEB, NOW);
    TEST_ASSERT_EQUAL_size_t(0, encodeSlot(m, u8("s3cret"), 6, blob, 53)); // cap one short
    TEST_ASSERT_EQUAL_size_t(54, encodeSlot(m, u8("s3cret"), 6, blob, 54));
    TEST_ASSERT_EQUAL_size_t(0, encodeSlot(m, u8("s3\ncret"), 7, blob, sizeof(blob)));
    TEST_ASSERT_EQUAL_size_t(0, encodeSlot(m, u8(""), 0, blob, sizeof(blob)));
    SlotMeta bad = meta("XWEB", IP_XWEB, 80, MAC_XWEB, NOW);
    TEST_ASSERT_EQUAL_size_t(0, encodeSlot(bad, u8("s3cret"), 6, blob, sizeof(blob)));
}

void test_slot_round_trip(void) {
    SlotMeta in = meta("xweb_admin", IP_XWEB, 80, MAC_XWEB, NOW);
    uint8_t blob[SLOT_MAX_BYTES];
    size_t n = encodeSlot(in, u8("s3cret"), 6, blob, sizeof(blob));
    SlotMeta out{};
    const uint8_t *value = nullptr;
    size_t length = 0;
    TEST_ASSERT_TRUE(decodeSlot(blob, n, &out, &value, &length));
    TEST_ASSERT_EQUAL_STRING("xweb_admin", out.name);
    TEST_ASSERT_EQUAL_MEMORY(IP_XWEB, out.ip, 4);
    TEST_ASSERT_EQUAL_UINT16(80, out.port);
    TEST_ASSERT_TRUE(out.hasMac);
    TEST_ASSERT_EQUAL_MEMORY(MAC_XWEB, out.mac, 6);
    TEST_ASSERT_EQUAL_UINT64(NOW, out.setAt);
    TEST_ASSERT_EQUAL_size_t(6, length);
    TEST_ASSERT_TRUE(value == blob + 48);

    SlotMeta noMac = meta("site_api", IP_ROUTED, 65535, nullptr, 1);
    n = encodeSlot(noMac, u8("token"), 5, blob, sizeof(blob));
    TEST_ASSERT_TRUE(decodeSlot(blob, n, &out, &value, &length));
    TEST_ASSERT_FALSE(out.hasMac);
    TEST_ASSERT_EQUAL_UINT16(65535, out.port);
    TEST_ASSERT_EQUAL_STRING("site_api", out.name);
}

// Fresh valid blob "xweb_admin" / "s3cret" (size 54) for mutation tests.
static size_t validBlob(uint8_t *blob) {
    SlotMeta m = meta("xweb_admin", IP_XWEB, 80, MAC_XWEB, NOW);
    return encodeSlot(m, u8("s3cret"), 6, blob, SLOT_MAX_BYTES + 4);
}

static bool decodes(const uint8_t *blob, size_t size) {
    SlotMeta out{};
    const uint8_t *value = nullptr;
    size_t length = 0;
    return decodeSlot(blob, size, &out, &value, &length);
}

void test_slot_decode_version_and_size(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    TEST_ASSERT_TRUE(decodes(blob, n));
    blob[0] = 0;
    TEST_ASSERT_FALSE(decodes(blob, n));
    blob[0] = 2;
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    TEST_ASSERT_FALSE(decodes(blob, n - 1));
    TEST_ASSERT_FALSE(decodes(blob, n + 1));
    TEST_ASSERT_FALSE(decodes(blob, 47));
    TEST_ASSERT_FALSE(decodes(blob, 0));
    TEST_ASSERT_FALSE(decodes(nullptr, n));
}

void test_slot_decode_name_len(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[1] = 0;
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[1] = 25;
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[1] = 255;
    TEST_ASSERT_FALSE(decodes(blob, n));
}

void test_slot_decode_non_zero_name_padding(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[25] = 'x'; // last padding byte
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[12] = 'x'; // first padding byte after the 10-char name
    TEST_ASSERT_FALSE(decodes(blob, n));
}

void test_slot_decode_invalid_name_bytes(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[2] = 'X'; // uppercase
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[2] = '1'; // leading digit
    TEST_ASSERT_FALSE(decodes(blob, n));
    // name_len 10 but a NUL inside the name: "xweb\0admin" is not a valid name.
    n = validBlob(blob);
    blob[6] = 0;
    TEST_ASSERT_FALSE(decodes(blob, n));
}

void test_slot_decode_name_len_covers_padding(void) {
    // name_len claims 12 for a 10-char name (two zero bytes counted as name).
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[1] = 12;
    TEST_ASSERT_FALSE(decodes(blob, n));
}

void test_slot_decode_value_length(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    validBlob(blob);
    blob[46] = 0;
    blob[47] = 0;
    TEST_ASSERT_FALSE(decodes(blob, 48)); // len 0, size consistent
    uint8_t big[48 + 257];
    memset(big, 'v', sizeof(big));
    validBlob(blob);
    memcpy(big, blob, 48);
    big[46] = 0x01; // 257
    big[47] = 0x01;
    TEST_ASSERT_FALSE(decodes(big, sizeof(big)));
    big[46] = 0x00; // 256 with a matching size is fine
    TEST_ASSERT_TRUE(decodes(big, 48 + 256));
}

void test_slot_decode_mac(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[32] = 0x01; // multicast bit
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    memset(blob + 32, 0xFF, 6); // broadcast
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[32] = 0x01;
    blob[33] = 0x00;
    blob[34] = 0x5e; // IPv4 multicast MAC 01:00:5e:..
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    memset(blob + 32, 0, 6); // all zero = unbound
    SlotMeta out{};
    const uint8_t *value;
    size_t length;
    TEST_ASSERT_TRUE(decodeSlot(blob, n, &out, &value, &length));
    TEST_ASSERT_FALSE(out.hasMac);
}

void test_slot_decode_target(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    const uint8_t publicIp[4] = {8, 8, 8, 8};
    memcpy(blob + 26, publicIp, 4);
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[30] = 0;
    blob[31] = 0; // port 0
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    const uint8_t p172[4] = {172, 16, 0, 1};
    memcpy(blob + 26, p172, 4);
    TEST_ASSERT_TRUE(decodes(blob, n));
}

void test_slot_decode_invalid_value(void) {
    uint8_t blob[SLOT_MAX_BYTES + 4];
    size_t n = validBlob(blob);
    blob[50] = '\n';
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[50] = 0;
    TEST_ASSERT_FALSE(decodes(blob, n));
    n = validBlob(blob);
    blob[50] = 0xC0; // overlong lead
    blob[51] = 0x80;
    TEST_ASSERT_FALSE(decodes(blob, n));
}

// ============================================================================
// AAD, issued_at, plaintext padding
// ============================================================================

void test_build_aad_with_mac(void) {
    char out[AAD_MAX];
    size_t n = buildAad(out, sizeof(out), "907069875394", "xweb_admin", IP_XWEB, 80, MAC_XWEB, 1793450000ULL);
    const char *expected = "energyme-secret-v1|907069875394|xweb_admin|192.168.1.50:80|00:11:22:33:44:55|1793450000";
    TEST_ASSERT_EQUAL_STRING(expected, out);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), n);
}

void test_build_aad_without_mac(void) {
    char out[AAD_MAX];
    size_t n = buildAad(out, sizeof(out), "907069875394", "xweb_admin", IP_XWEB, 80, nullptr, 1793450000ULL);
    const char *expected = "energyme-secret-v1|907069875394|xweb_admin|192.168.1.50:80||1793450000";
    TEST_ASSERT_EQUAL_STRING(expected, out);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), n);
}

void test_build_aad_lowercase_mac_and_decimal_fields(void) {
    char out[AAD_MAX];
    const uint8_t mac[6] = {0x0a, 0xBC, 0xde, 0xF0, 0x01, 0x02};
    const uint8_t ip[4] = {10, 0, 0, 7};
    buildAad(out, sizeof(out), "dev", "a", ip, 65535, mac, 1);
    TEST_ASSERT_EQUAL_STRING("energyme-secret-v1|dev|a|10.0.0.7:65535|0a:bc:de:f0:01:02|1", out);
}

void test_build_aad_cap(void) {
    const char *expected = "energyme-secret-v1|907069875394|xweb_admin|192.168.1.50:80||1793450000";
    size_t len = strlen(expected);
    char out[AAD_MAX];
    TEST_ASSERT_EQUAL_size_t(len, buildAad(out, len + 1, "907069875394", "xweb_admin", IP_XWEB, 80, nullptr, 1793450000ULL));
    TEST_ASSERT_EQUAL_size_t(0, buildAad(out, len, "907069875394", "xweb_admin", IP_XWEB, 80, nullptr, 1793450000ULL));
}

void test_parse_issued_at(void) {
    uint64_t v = 0;
    TEST_ASSERT_TRUE(parseIssuedAt("1793450000", &v));
    TEST_ASSERT_EQUAL_UINT64(1793450000ULL, v);
    TEST_ASSERT_TRUE(parseIssuedAt("1", &v));
    TEST_ASSERT_EQUAL_UINT64(1, v);
    TEST_ASSERT_TRUE(parseIssuedAt("9999999999999999999", &v)); // 19 digits
    TEST_ASSERT_EQUAL_UINT64(9999999999999999999ULL, v);
    v = 42;
    TEST_ASSERT_FALSE(parseIssuedAt("10000000000000000000", &v)); // 20 digits
    TEST_ASSERT_FALSE(parseIssuedAt("01793450000", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("0", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("-5", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("+5", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("", &v));
    TEST_ASSERT_FALSE(parseIssuedAt(" 12", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("12 ", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("12a", &v));
    TEST_ASSERT_FALSE(parseIssuedAt("1.5", &v));
    TEST_ASSERT_FALSE(parseIssuedAt(nullptr, &v));
    TEST_ASSERT_EQUAL_UINT64(42, v); // untouched on failure
}

static void makePlaintext(uint8_t *pt, uint16_t len, const char *value) {
    memset(pt, 0, PLAINTEXT_BYTES);
    pt[0] = static_cast<uint8_t>(len & 0xFF);
    pt[1] = static_cast<uint8_t>(len >> 8);
    if (value != nullptr) memcpy(pt + 2, value, strlen(value));
}

void test_unpad_plaintext(void) {
    uint8_t pt[PLAINTEXT_BYTES + 2];
    const uint8_t *value = nullptr;
    size_t length = 0;
    makePlaintext(pt, 6, "s3cret");
    TEST_ASSERT_TRUE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));
    TEST_ASSERT_EQUAL_size_t(6, length);
    TEST_ASSERT_TRUE(value == pt + 2);
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES - 1, &value, &length));
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES + 1, &value, &length));

    makePlaintext(pt, 0, nullptr);
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));
    makePlaintext(pt, 300, "s3cret");
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));
    makePlaintext(pt, 257, "s3cret");
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));

    makePlaintext(pt, 6, "s3cret");
    pt[PLAINTEXT_BYTES - 1] = 1; // last padding byte
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));
    makePlaintext(pt, 6, "s3cret");
    pt[2 + 6] = 0x80; // first padding byte
    TEST_ASSERT_FALSE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));

    // 256-byte value fills the plaintext exactly: no padding.
    memset(pt, 'v', PLAINTEXT_BYTES);
    pt[0] = 0x00;
    pt[1] = 0x01;
    TEST_ASSERT_TRUE(unpadPlaintext(pt, PLAINTEXT_BYTES, &value, &length));
    TEST_ASSERT_EQUAL_size_t(256, length);
}

// ============================================================================
// Replay and rollback
// ============================================================================

static const uint64_t HWM_2031 = 1951000000ULL;

void test_ceiling(void) {
    bool lowered = true;
    TEST_ASSERT_EQUAL_UINT64(NOW + 300, applyCeiling(true, NOW, HWM_2031, &lowered));
    TEST_ASSERT_TRUE(lowered);
    TEST_ASSERT_EQUAL_UINT64(HWM_2031, applyCeiling(false, NOW, HWM_2031, &lowered));
    TEST_ASSERT_FALSE(lowered);
    TEST_ASSERT_EQUAL_UINT64(NOW + 300, applyCeiling(true, NOW, NOW + 300, &lowered));
    TEST_ASSERT_FALSE(lowered);
    TEST_ASSERT_EQUAL_UINT64(NOW + 300, applyCeiling(true, NOW, NOW + 301, &lowered));
    TEST_ASSERT_TRUE(lowered);
    TEST_ASSERT_EQUAL_UINT64(5, applyCeiling(true, NOW, 5, &lowered));
    TEST_ASSERT_FALSE(lowered);
}

void test_decide_set_order(void) {
    // CLOCK_NOT_SYNCED first, whatever else holds.
    TEST_ASSERT_TRUE(decideSet(false, NOW, 0, NOW, true) == SetVerdict::CLOCK_NOT_SYNCED);
    TEST_ASSERT_TRUE(decideSet(false, NOW, 0, NOW, false) == SetVerdict::CLOCK_NOT_SYNCED);
    // Window before the idempotent check.
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW - 901, true) == SetVerdict::STALE);
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW + 301, true) == SetVerdict::CLOCK_SKEW);
    // Window boundaries: exactly now - 900 and now + 300 are inside.
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW - 900, false) == SetVerdict::APPLY);
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW + 300, false) == SetVerdict::APPLY);
    // Idempotent repeat before the hwm test.
    TEST_ASSERT_TRUE(decideSet(true, NOW, NOW, NOW, true) == SetVerdict::ALREADY_APPLIED);
    TEST_ASSERT_TRUE(decideSet(true, NOW, NOW + 100, NOW - 10, true) == SetVerdict::ALREADY_APPLIED);
    // issued_at <= hwm.
    TEST_ASSERT_TRUE(decideSet(true, NOW, NOW, NOW, false) == SetVerdict::STALE);
    TEST_ASSERT_TRUE(decideSet(true, NOW, NOW - 1, NOW, false) == SetVerdict::APPLY);
}

void test_hwm_after_delete(void) {
    TEST_ASSERT_EQUAL_UINT64(NOW, hwmAfterDelete(true, NOW, NOW - 50));
    TEST_ASSERT_EQUAL_UINT64(NOW + 50, hwmAfterDelete(true, NOW, NOW + 50));
    TEST_ASSERT_EQUAL_UINT64(NOW - 50, hwmAfterDelete(false, NOW, NOW - 50));
    TEST_ASSERT_EQUAL_UINT64(0, hwmAfterDelete(false, NOW, 0));
}

void test_local_set_at(void) {
    TEST_ASSERT_EQUAL_UINT64(NOW, localSetAt(true, NOW, NOW - 50));
    TEST_ASSERT_EQUAL_UINT64(NOW + 51, localSetAt(true, NOW, NOW + 50));
    TEST_ASSERT_EQUAL_UINT64(NOW + 1, localSetAt(true, NOW, NOW));
    TEST_ASSERT_EQUAL_UINT64(NOW, localSetAt(true, NOW, NOW - 1));
    TEST_ASSERT_EQUAL_UINT64(1, localSetAt(false, NOW, 0));
    TEST_ASSERT_EQUAL_UINT64(NOW - 49, localSetAt(false, NOW, NOW - 50));
}

void test_replay_same_envelope_twice(void) {
    uint64_t hwm = 0;
    uint64_t issued = NOW - 10;
    TEST_ASSERT_TRUE(decideSet(true, NOW, hwm, issued, false) == SetVerdict::APPLY);
    hwm = issued;
    TEST_ASSERT_TRUE(decideSet(true, NOW + 5, hwm, issued, true) == SetVerdict::ALREADY_APPLIED);
}

void test_replay_older_after_newer(void) {
    uint64_t a = NOW - 200, b = NOW - 100, hwm = 0;
    TEST_ASSERT_TRUE(decideSet(true, NOW, hwm, a, false) == SetVerdict::APPLY);
    hwm = a;
    TEST_ASSERT_TRUE(decideSet(true, NOW, hwm, b, false) == SetVerdict::APPLY);
    hwm = b;
    // The slot now holds b's set_at, so a is not an idempotent repeat.
    TEST_ASSERT_TRUE(decideSet(true, NOW, hwm, a, false) == SetVerdict::STALE);
}

void test_replay_after_delete(void) {
    uint64_t issued = NOW - 10;
    uint64_t hwm = issued;
    bool lowered;
    hwm = applyCeiling(true, NOW, hwm, &lowered);
    hwm = hwmAfterDelete(true, NOW, hwm);
    TEST_ASSERT_EQUAL_UINT64(NOW, hwm);
    TEST_ASSERT_TRUE(decideSet(true, NOW + 1, hwm, issued, false) == SetVerdict::STALE);
    // Envelope issued slightly in the future, deleted before real time reached it.
    uint64_t future = NOW + 200;
    hwm = hwmAfterDelete(true, NOW, future);
    TEST_ASSERT_TRUE(decideSet(true, NOW + 250, hwm, future, false) == SetVerdict::STALE);
}

void test_replay_two_names_same_second(void) {
    uint64_t hwm = 0;
    TEST_ASSERT_TRUE(decideSet(true, 5000 + 10, hwm, 5000, false) == SetVerdict::APPLY);
    hwm = 5000;
    TEST_ASSERT_TRUE(decideSet(true, 5000 + 10, hwm, 5000, false) == SetVerdict::STALE);
    TEST_ASSERT_TRUE(decideSet(true, 5000 + 10, hwm, 5001, false) == SetVerdict::APPLY);
}

void test_replay_future_issued_at(void) {
    TEST_ASSERT_TRUE(decideSet(true, NOW, NOW - 100, NOW + 600, false) == SetVerdict::CLOCK_SKEW);
}

void test_replay_hwm_2031_after_clock_correction(void) {
    uint64_t hwm = HWM_2031;
    bool lowered = false;
    hwm = applyCeiling(true, NOW, hwm, &lowered);
    TEST_ASSERT_TRUE(lowered);
    TEST_ASSERT_EQUAL_UINT64(NOW + 300, hwm);
    // Tool sends issued_at = reported (2031) hwm + 1.
    TEST_ASSERT_TRUE(decideSet(true, NOW, hwm, HWM_2031 + 1, false) == SetVerdict::CLOCK_SKEW);
    // Tool waits until its clock reaches hwm + 1, sends a fresh envelope.
    uint64_t later = NOW + 301;
    hwm = applyCeiling(true, later, hwm, &lowered);
    TEST_ASSERT_FALSE(lowered);
    TEST_ASSERT_TRUE(decideSet(true, later, hwm, NOW + 301, false) == SetVerdict::APPLY);
}

void test_replay_ceiling_before_delete(void) {
    bool lowered = false;
    uint64_t hwm = applyCeiling(true, NOW, HWM_2031, &lowered);
    TEST_ASSERT_TRUE(lowered);
    hwm = hwmAfterDelete(true, NOW, hwm);
    TEST_ASSERT_EQUAL_UINT64(NOW + 300, hwm);
}

void test_replay_after_factory_reset(void) {
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW - 899, false) == SetVerdict::APPLY);
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, NOW - 901, false) == SetVerdict::STALE);
    TEST_ASSERT_TRUE(decideSet(true, NOW, 0, 1000, false) == SetVerdict::STALE);
}

// ============================================================================
// Resolver decision
// ============================================================================

static ResolveContext ctx(Module module, const uint8_t ip[4], uint16_t port, bool onLink) {
    ResolveContext c{};
    c.module = module;
    memcpy(c.ip, ip, 4);
    c.port = port;
    c.onLink = onLink;
    return c;
}

struct Cache {
    SlotMeta slots[SLOT_COUNT];
    bool used[SLOT_COUNT];
};

static Cache cacheWithXweb(bool withMac) {
    Cache c{};
    c.slots[3] = meta("xweb_admin", IP_XWEB, 80, withMac ? MAC_XWEB : nullptr, NOW);
    c.used[3] = true;
    // A stale entry with the same name in an unused slot must be ignored.
    c.slots[0] = meta("ghost", IP_OTHER, 80, nullptr, NOW);
    c.used[0] = false;
    return c;
}

void test_decide_ok_on_link(void) {
    Cache c = cacheWithXweb(true);
    Decision d = decide(ctx(Module::HTTP, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_NULL(d.reasonCode);
    TEST_ASSERT_EQUAL_INT(3, d.slot);
    d = decide(ctx(Module::HTTPS, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 7);
    TEST_ASSERT_NULL(d.reasonCode);
}

void test_decide_bad_arg(void) {
    Cache c = cacheWithXweb(true);
    const char *bad[] = {"", "XWEB", "xweb.admin", "1abc", "abcdefghijklmnopqrstuvwxy"};
    for (const char *arg : bad) {
        // Name rule first, even for a tcp module.
        Decision d = decide(ctx(Module::TCP, IP_XWEB, 80, true), arg, c.slots, c.used, SLOT_COUNT, 0);
        TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", d.reasonCode);
        TEST_ASSERT_EQUAL_INT(-1, d.slot);
    }
    Decision d = decide(ctx(Module::HTTP, IP_XWEB, 80, true), nullptr, c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", d.reasonCode);
}

void test_decide_ninth_placeholder(void) {
    Cache c = cacheWithXweb(true);
    Decision d = decide(ctx(Module::TCP, IP_XWEB, 80, true), "unknown", c.slots, c.used, SLOT_COUNT, 8);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", d.reasonCode);
    d = decide(ctx(Module::HTTP, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 8);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", d.reasonCode);
}

void test_decide_module_not_allowed(void) {
    Cache c = cacheWithXweb(true);
    Module others[] = {Module::TCP, Module::UDP, Module::MODBUS_TCP};
    for (Module m : others) {
        Decision d = decide(ctx(m, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
        TEST_ASSERT_EQUAL_STRING("SECRET_MODULE_NOT_ALLOWED", d.reasonCode);
    }
    // Module before lookup.
    Decision d = decide(ctx(Module::TCP, IP_XWEB, 80, true), "unknown", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_MODULE_NOT_ALLOWED", d.reasonCode);
}

void test_decide_not_found(void) {
    Cache c = cacheWithXweb(true);
    Decision d = decide(ctx(Module::HTTP, IP_XWEB, 8080, false), "unknown", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_NOT_FOUND", d.reasonCode);
    d = decide(ctx(Module::HTTP, IP_OTHER, 80, false), "ghost", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_NOT_FOUND", d.reasonCode); // unused slot
    d = decide(ctx(Module::HTTP, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, 0, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_NOT_FOUND", d.reasonCode);
}

static bool isMacDetail(const char *detail) {
    return detail != nullptr && (strcmp(detail, "MAC_REQUIRED") == 0 || strcmp(detail, "TARGET_MAC_CHANGED") == 0);
}

void test_decide_target_mismatch(void) {
    Cache c = cacheWithXweb(false);
    // Other port, on-link and without MAC: the IP/port rule comes first.
    Decision d = decide(ctx(Module::HTTP, IP_XWEB, 8080, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", d.reasonCode);
    TEST_ASSERT_FALSE(isMacDetail(d.reasonDetail));
    d = decide(ctx(Module::HTTP, IP_OTHER, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", d.reasonCode);
    TEST_ASSERT_FALSE(isMacDetail(d.reasonDetail));
    // https default port 443 vs a slot bound to 80.
    d = decide(ctx(Module::HTTPS, IP_XWEB, 443, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", d.reasonCode);
    TEST_ASSERT_EQUAL_INT(-1, d.slot);
}

// The target MAC travels on the request: the slot MAC no longer decides.
void test_decide_ignores_slot_mac(void) {
    Cache c = cacheWithXweb(false);
    Decision d = decide(ctx(Module::HTTP, IP_XWEB, 80, true), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_NULL(d.reasonCode);
    TEST_ASSERT_EQUAL_INT(3, d.slot);
    c = cacheWithXweb(true);
    d = decide(ctx(Module::HTTP, IP_XWEB, 80, false), "xweb_admin", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_NULL(d.reasonCode);
    TEST_ASSERT_EQUAL_INT(3, d.slot);
}

void test_decide_routed_without_mac(void) {
    Cache c{};
    const uint8_t routed[4] = {10, 20, 0, 5};
    c.slots[0] = meta("site_api", routed, 80, nullptr, NOW);
    c.used[0] = true;
    Decision d = decide(ctx(Module::HTTP, routed, 80, false), "site_api", c.slots, c.used, SLOT_COUNT, 0);
    TEST_ASSERT_NULL(d.reasonCode);
    TEST_ASSERT_EQUAL_INT(0, d.slot);
}

// ============================================================================
// Resolver on the real template engine
// ============================================================================

struct TestStore {
    Cache cache;
    const char *values[SLOT_COUNT];
    size_t placeholders;
    size_t valueReads;
    size_t calls;
};

static ResolveStatus testResolve(void *context, const ResolveContext &request, const char *name, const char *arg, ResolveOutput *out) {
    TestStore *store = static_cast<TestStore *>(context);
    if (strcmp(name, "secret") != 0) return ResolveStatus::UNKNOWN;
    store->calls++;
    Decision d = decide(request, arg, store->cache.slots, store->cache.used, SLOT_COUNT, store->placeholders);
    if (d.reasonCode != nullptr) {
        out->reasonCode = d.reasonCode;
        snprintf(out->reasonDetail, sizeof(out->reasonDetail), "%s", d.reasonDetail != nullptr ? d.reasonDetail : "");
        return ResolveStatus::FAILED;
    }
    store->valueReads++;
    const char *value = store->values[d.slot];
    size_t length = strlen(value);
    if (length > out->cap) return ResolveStatus::FAILED;
    memcpy(out->value, value, length);
    out->length = length;
    out->sensitive = true;
    store->placeholders++;
    return ResolveStatus::OK;
}

static void addSecret(TestStore *s, size_t slot, const char *name, const uint8_t ip[4], uint16_t port, const uint8_t *mac,
                      const char *value) {
    s->cache.slots[slot] = meta(name, ip, port, mac, NOW);
    s->cache.used[slot] = true;
    s->values[slot] = value;
}

static uint8_t g_out[6000];
static uint8_t g_scratch[2048];

static ExpandStatus expand(TestStore *store, const ResolveContext &request, const char *tmpl, ExpandResult *result,
                           size_t cap = sizeof(g_out)) {
    Resolver chain[1] = {{testResolve, store}};
    *result = ExpandResult{};
    memset(g_out, 0, sizeof(g_out));
    return GatewayLogic::expandTemplate(tmpl, strlen(tmpl), chain, 1, request, g_out, cap, g_scratch, sizeof(g_scratch), result);
}

static void assertExpanded(const char *expected, const ExpandResult &result) {
    TEST_ASSERT_EQUAL_size_t(strlen(expected), result.length);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_out, result.length);
}

void test_expand_form_login_url(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "a&b c");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), "user=admin&pass=${secret:xweb_admin|url}", &r) == ExpandStatus::OK);
    assertExpanded("user=admin&pass=a%26b%20c", r);
    TEST_ASSERT_TRUE(r.sensitive);
}

void test_expand_basic_auth_b64(void) {
    TestStore s{};
    const uint8_t ip[4] = {192, 168, 1, 60};
    addSecret(&s, 5, "meter_basic", ip, 80, MAC_XWEB, "admin:pw");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, ip, 80, true), "Basic ${secret:meter_basic|b64}", &r) == ExpandStatus::OK);
    assertExpanded("Basic YWRtaW46cHc=", r);
}

void test_expand_chained_url_b64(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "a b");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), "${secret:xweb_admin|url|b64}", &r) == ExpandStatus::OK);
    assertExpanded("YSUyMGI=", r);
}

void test_expand_escaped_placeholder_no_lookup(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "s3cret");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), "x=$${secret:xweb_admin}", &r) == ExpandStatus::OK);
    assertExpanded("x=${secret:xweb_admin}", r);
    TEST_ASSERT_EQUAL_size_t(0, s.calls);
    TEST_ASSERT_FALSE(r.sensitive);
}

void test_expand_malformed_placeholders(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "s3cret");
    ExpandResult r;
    ResolveContext c = ctx(Module::HTTP, IP_XWEB, 80, true);
    TEST_ASSERT_TRUE(expand(&s, c, "/p?${secret:xweb_admin|hex}", &r) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_TRUE(expand(&s, c, "/p?${secret:xweb_admin", &r) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_EQUAL_size_t(0, s.valueReads);
    // Upper case passes the engine arg grammar, fails the secret name rule.
    TEST_ASSERT_TRUE(expand(&s, c, "/p?${secret:XWEB}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", r.reasonCode);
    // Missing arg fails the name rule.
    TEST_ASSERT_TRUE(expand(&s, c, "${secret}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", r.reasonCode);
    TEST_ASSERT_EQUAL_size_t(0, s.valueReads);
}

void test_expand_placeholder_cap(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "v");
    ExpandResult r;
    ResolveContext c = ctx(Module::HTTP, IP_XWEB, 80, true);
    const char *eight = "${secret:xweb_admin}${secret:xweb_admin}${secret:xweb_admin}${secret:xweb_admin}"
                        "${secret:xweb_admin}${secret:xweb_admin}${secret:xweb_admin}${secret:xweb_admin}";
    TEST_ASSERT_TRUE(expand(&s, c, eight, &r) == ExpandStatus::OK);
    assertExpanded("vvvvvvvv", r);
    s.placeholders = 0;
    char nine[256];
    snprintf(nine, sizeof(nine), "%s${secret:xweb_admin}", eight);
    TEST_ASSERT_TRUE(expand(&s, c, nine, &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("BAD_TEMPLATE", r.reasonCode);
}

void test_expand_tcp_module_refused_without_read(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "s3cret");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::TCP, IP_XWEB, 80, true), "${secret:xweb_admin}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_MODULE_NOT_ALLOWED", r.reasonCode);
    TEST_ASSERT_EQUAL_size_t(0, s.valueReads);
}

void test_expand_target_rules(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "s3cret");
    const uint8_t ip60[4] = {192, 168, 1, 60};
    addSecret(&s, 1, "meter_basic", ip60, 80, nullptr, "admin:pw");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 8080, true), "${secret:xweb_admin}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", r.reasonCode);
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_OTHER, 80, true), "${secret:xweb_admin}", &r) ==
                     ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", r.reasonCode);
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, ip60, 8080, true), "${secret:meter_basic|b64}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_MISMATCH", r.reasonCode);
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), "${secret:nope}", &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_NOT_FOUND", r.reasonCode);
    TEST_ASSERT_EQUAL_size_t(0, s.valueReads);
}

void test_expand_https_default_port(void) {
    TestStore s{};
    const uint8_t ip70[4] = {192, 168, 1, 70};
    addSecret(&s, 0, "meter_token", ip70, 443, MAC_XWEB, "tok3n");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTPS, ip70, 443, true), "Bearer ${secret:meter_token}", &r) == ExpandStatus::OK);
    assertExpanded("Bearer tok3n", r);
}

void test_expand_routed_without_mac(void) {
    TestStore s{};
    const uint8_t routed[4] = {10, 20, 0, 5};
    addSecret(&s, 0, "site_api", routed, 80, nullptr, "k3y");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, routed, 80, false), "${secret:site_api}", &r) == ExpandStatus::OK);
    assertExpanded("k3y", r);
}

void test_expand_value_not_rescanned(void) {
    TestStore s{};
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, "${secret:other}");
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), "[${secret:xweb_admin}]", &r) == ExpandStatus::OK);
    assertExpanded("[${secret:other}]", r);
    TEST_ASSERT_EQUAL_size_t(1, s.calls);
}

void test_expand_exceeds_send_limit(void) {
    TestStore s{};
    static char value[257];
    memset(value, '&', 256);
    value[256] = '\0';
    addSecret(&s, 0, "xweb_admin", IP_XWEB, 80, MAC_XWEB, value);
    static char tmpl[5601];
    const char *placeholder = "${secret:xweb_admin|url}";
    size_t pl = strlen(placeholder);
    memset(tmpl, 'x', 5600);
    memcpy(tmpl + 5600 - pl, placeholder, pl);
    tmpl[5600] = '\0';
    ExpandResult r;
    TEST_ASSERT_TRUE(expand(&s, ctx(Module::HTTP, IP_XWEB, 80, true), tmpl, &r, 5760) == ExpandStatus::TOO_LONG);
}

// ============================================================================
// Echo scrub
// ============================================================================

static const uint8_t GUARD = 0xA5;
static const size_t GUARD_BYTES = 32;

struct ScrubBuffers {
    uint8_t buffer[4096 + GUARD_BYTES];
    uint8_t scratch[4096 + GUARD_BYTES];
};
static ScrubBuffers g_sb;

static void armGuards(size_t cap) {
    memset(g_sb.buffer, 0, sizeof(g_sb.buffer));
    memset(g_sb.scratch, 0, sizeof(g_sb.scratch));
    memset(g_sb.buffer + cap, GUARD, GUARD_BYTES);
    memset(g_sb.scratch + cap, GUARD, GUARD_BYTES);
}

static void assertGuards(size_t cap) {
    for (size_t i = 0; i < GUARD_BYTES; i++) {
        TEST_ASSERT_EQUAL_HEX8(GUARD, g_sb.buffer[cap + i]);
        TEST_ASSERT_EQUAL_HEX8(GUARD, g_sb.scratch[cap + i]);
    }
}

static size_t runScrub(const char *input, size_t cap, const ScrubForm *forms, size_t count, bool *truncated, bool *redacted) {
    armGuards(cap);
    size_t n = strlen(input);
    memcpy(g_sb.buffer, input, n);
    return scrub(g_sb.buffer, n, cap, g_sb.scratch, forms, count, truncated, redacted);
}

static void assertBuffer(const char *expected, size_t length) {
    TEST_ASSERT_EQUAL_size_t(strlen(expected), length);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_sb.buffer, length);
}

void test_scrub_raw_and_encoded_forms(void) {
    ScrubForm forms[] = {{u8("a&b c"), 5, "xweb_admin"}, {u8("a%26b%20c"), 9, "xweb_admin"}};
    bool truncated = false, redacted = false;
    size_t n = runScrub("Bad password a%26b%20c (a&b c)", 1024, forms, 2, &truncated, &redacted);
    assertBuffer("Bad password ${secret:xweb_admin} (${secret:xweb_admin})", n);
    TEST_ASSERT_TRUE(redacted);
    TEST_ASSERT_FALSE(truncated);
}

void test_scrub_nothing_to_replace(void) {
    ScrubForm forms[] = {{u8("s3cret"), 6, "xweb_admin"}};
    bool truncated = false, redacted = false;
    size_t n = runScrub("all good", 1024, forms, 1, &truncated, &redacted);
    assertBuffer("all good", n);
    TEST_ASSERT_FALSE(redacted);
    TEST_ASSERT_FALSE(truncated);
}

void test_scrub_four_byte_floor(void) {
    ScrubForm forms[] = {{u8("a b"), 3, "xweb_admin"}, {u8("YSUyMGI="), 8, "xweb_admin"}};
    bool truncated = false, redacted = false;
    size_t n = runScrub("a b and YSUyMGI=", 1024, forms, 2, &truncated, &redacted);
    assertBuffer("a b and ${secret:xweb_admin}", n);
    TEST_ASSERT_TRUE(redacted);

    ScrubForm shortOnly[] = {{u8("pin"), 3, "pin"}};
    truncated = redacted = false;
    n = runScrub("pin pin", 1024, shortOnly, 1, &truncated, &redacted);
    assertBuffer("pin pin", n);
    TEST_ASSERT_FALSE(redacted);

    ScrubForm four[] = {{u8("1234"), 4, "pin"}};
    truncated = redacted = false;
    n = runScrub("x1234y", 1024, four, 1, &truncated, &redacted);
    assertBuffer("x${secret:pin}y", n);
    TEST_ASSERT_TRUE(redacted);
}

void test_scrub_longest_match_first(void) {
    ScrubForm forms[] = {{u8("abcd"), 4, "short"}, {u8("abcdefgh"), 8, "long"}};
    bool truncated = false, redacted = false;
    size_t n = runScrub("[abcdefgh][abcdxx]", 1024, forms, 2, &truncated, &redacted);
    assertBuffer("[${secret:long}][${secret:short}xx]", n);
}

void test_scrub_trailing_prefix_kept_when_not_truncated(void) {
    ScrubForm forms[] = {{u8("Pa55word!xyz"), 12, "xweb_admin"}};
    bool truncated = false, redacted = false;
    size_t n = runScrub("end Pa55wo", 1024, forms, 1, &truncated, &redacted);
    assertBuffer("end Pa55wo", n);
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_FALSE(redacted);
}

void test_scrub_echo_split_by_response_cap(void) {
    // A 12-byte password echoed at byte 4090 of a reply cut at 4096 on receive.
    const char *pw = "Pa55word!xyz";
    ScrubForm forms[] = {{u8(pw), 12, "xweb_admin"}};
    armGuards(4096);
    memset(g_sb.buffer, 'x', 4090);
    memcpy(g_sb.buffer + 4090, pw, 6);
    bool truncated = true, redacted = false;
    size_t n = scrub(g_sb.buffer, 4096, 4096, g_sb.scratch, forms, 1, &truncated, &redacted);
    TEST_ASSERT_TRUE(n <= 4090);
    TEST_ASSERT_EQUAL_size_t(4090, n); // only the partial echo goes, the 'x' bytes stay
    for (size_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT8('x', g_sb.buffer[i]);
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_TRUE(redacted);
    assertGuards(4096);
}

void test_scrub_placeholder_straddling_cap_dropped_whole(void) {
    ScrubForm forms[] = {{u8("1234"), 4, "pin"}}; // placeholder 13 bytes
    bool truncated = false, redacted = false;
    size_t n = runScrub("aaaaaaaaaa1234bbbb", 20, forms, 1, &truncated, &redacted);
    assertBuffer("aaaaaaaaaa", n);
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_TRUE(redacted);
    assertGuards(20);
}

void test_scrub_growth_cuts_plain_tail(void) {
    // One early echo grows the body; the plain tail is cut at the cap.
    ScrubForm forms[] = {{u8("1234"), 4, "pin"}};
    armGuards(4096);
    memcpy(g_sb.buffer, "1234", 4);
    memset(g_sb.buffer + 4, 'x', 4092);
    bool truncated = false, redacted = false;
    size_t n = scrub(g_sb.buffer, 4096, 4096, g_sb.scratch, forms, 1, &truncated, &redacted);
    TEST_ASSERT_EQUAL_size_t(4096, n);
    TEST_ASSERT_EQUAL_MEMORY("${secret:pin}", g_sb.buffer, 13);
    for (size_t i = 13; i < n; i++) TEST_ASSERT_EQUAL_UINT8('x', g_sb.buffer[i]);
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_TRUE(redacted);
    assertGuards(4096);
}

static void repeatedEchoes(size_t cap) {
    ScrubForm forms[] = {{u8("1234"), 4, "pin"}};
    armGuards(cap);
    for (size_t i = 0; i < cap; i += 4) memcpy(g_sb.buffer + i, "1234", 4);
    bool truncated = false, redacted = false;
    size_t n = scrub(g_sb.buffer, cap, cap, g_sb.scratch, forms, 1, &truncated, &redacted);
    const char *ph = "${secret:pin}";
    size_t pl = strlen(ph);
    TEST_ASSERT_TRUE(n <= cap);
    TEST_ASSERT_TRUE(n >= pl);
    TEST_ASSERT_EQUAL_size_t(0, n % pl); // only whole placeholders, no partial value
    TEST_ASSERT_EQUAL_size_t(cap / pl * pl, n);
    for (size_t i = 0; i < n; i += pl) TEST_ASSERT_EQUAL_MEMORY(ph, g_sb.buffer + i, pl);
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_TRUE(redacted);
    assertGuards(cap);
}

void test_scrub_growth_4096_body(void) { repeatedEchoes(4096); }
void test_scrub_growth_2048_header_block(void) { repeatedEchoes(2048); }

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_name_rule);
    RUN_TEST(test_value_rule_lengths);
    RUN_TEST(test_value_rule_forbidden_bytes);
    RUN_TEST(test_value_rule_strict_utf8);

    RUN_TEST(test_slot_encode_exact_offsets);
    RUN_TEST(test_slot_encode_unbound_mac_and_256_value);
    RUN_TEST(test_slot_encode_refuses_bad_input);
    RUN_TEST(test_slot_round_trip);
    RUN_TEST(test_slot_decode_version_and_size);
    RUN_TEST(test_slot_decode_name_len);
    RUN_TEST(test_slot_decode_non_zero_name_padding);
    RUN_TEST(test_slot_decode_invalid_name_bytes);
    RUN_TEST(test_slot_decode_name_len_covers_padding);
    RUN_TEST(test_slot_decode_value_length);
    RUN_TEST(test_slot_decode_mac);
    RUN_TEST(test_slot_decode_target);
    RUN_TEST(test_slot_decode_invalid_value);

    RUN_TEST(test_build_aad_with_mac);
    RUN_TEST(test_build_aad_without_mac);
    RUN_TEST(test_build_aad_lowercase_mac_and_decimal_fields);
    RUN_TEST(test_build_aad_cap);
    RUN_TEST(test_parse_issued_at);
    RUN_TEST(test_unpad_plaintext);

    RUN_TEST(test_ceiling);
    RUN_TEST(test_decide_set_order);
    RUN_TEST(test_hwm_after_delete);
    RUN_TEST(test_local_set_at);
    RUN_TEST(test_replay_same_envelope_twice);
    RUN_TEST(test_replay_older_after_newer);
    RUN_TEST(test_replay_after_delete);
    RUN_TEST(test_replay_two_names_same_second);
    RUN_TEST(test_replay_future_issued_at);
    RUN_TEST(test_replay_hwm_2031_after_clock_correction);
    RUN_TEST(test_replay_ceiling_before_delete);
    RUN_TEST(test_replay_after_factory_reset);

    RUN_TEST(test_decide_ok_on_link);
    RUN_TEST(test_decide_bad_arg);
    RUN_TEST(test_decide_ninth_placeholder);
    RUN_TEST(test_decide_module_not_allowed);
    RUN_TEST(test_decide_not_found);
    RUN_TEST(test_decide_target_mismatch);
    RUN_TEST(test_decide_ignores_slot_mac);
    RUN_TEST(test_decide_routed_without_mac);

    RUN_TEST(test_expand_form_login_url);
    RUN_TEST(test_expand_basic_auth_b64);
    RUN_TEST(test_expand_chained_url_b64);
    RUN_TEST(test_expand_escaped_placeholder_no_lookup);
    RUN_TEST(test_expand_malformed_placeholders);
    RUN_TEST(test_expand_placeholder_cap);
    RUN_TEST(test_expand_tcp_module_refused_without_read);
    RUN_TEST(test_expand_target_rules);
    RUN_TEST(test_expand_https_default_port);
    RUN_TEST(test_expand_routed_without_mac);
    RUN_TEST(test_expand_value_not_rescanned);
    RUN_TEST(test_expand_exceeds_send_limit);

    RUN_TEST(test_scrub_raw_and_encoded_forms);
    RUN_TEST(test_scrub_nothing_to_replace);
    RUN_TEST(test_scrub_four_byte_floor);
    RUN_TEST(test_scrub_longest_match_first);
    RUN_TEST(test_scrub_trailing_prefix_kept_when_not_truncated);
    RUN_TEST(test_scrub_echo_split_by_response_cap);
    RUN_TEST(test_scrub_placeholder_straddling_cap_dropped_whole);
    RUN_TEST(test_scrub_growth_cuts_plain_tail);
    RUN_TEST(test_scrub_growth_4096_body);
    RUN_TEST(test_scrub_growth_2048_header_block);

    return UNITY_END();
}
