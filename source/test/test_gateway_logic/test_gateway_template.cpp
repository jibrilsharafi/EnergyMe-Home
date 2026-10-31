// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the gateway template engine (openspec gateway-modules,
// "Template expansion before send"). Written from the spec scenarios.

#include <unity.h>
#include <cstdio>
#include <cstring>
#include "gateway_template.h"

using namespace GatewayLogic;

// ============================================================================
// Test resolvers
// ============================================================================

// Resolves one name to a fixed value; everything else is UNKNOWN.
struct FixedResolver {
    const char *name;
    const char *value;
    bool sensitive;
    bool requireMac;
    uint8_t mac[6];
    int calls;
};

static ResolveStatus fixedResolve(void *context, const ResolveContext &, const char *name, const char *, ResolveOutput *out) {
    FixedResolver *r = static_cast<FixedResolver *>(context);
    if (strcmp(name, r->name) != 0) return ResolveStatus::UNKNOWN;
    r->calls++;
    size_t n = strlen(r->value);
    if (n > out->cap) return ResolveStatus::FAILED;
    memcpy(out->value, r->value, n);
    out->length = n;
    out->sensitive = r->sensitive;
    if (r->requireMac) {
        out->requireMac = true;
        memcpy(out->mac, r->mac, 6);
        out->macReasonCode = "SECRET_TARGET_CHANGED";
    }
    return ResolveStatus::OK;
}

// `${echo:arg}` resolves to its arg.
static ResolveStatus echoResolve(void *, const ResolveContext &, const char *name, const char *arg, ResolveOutput *out) {
    if (strcmp(name, "echo") != 0) return ResolveStatus::UNKNOWN;
    size_t n = arg ? strlen(arg) : 0;
    if (n > out->cap) return ResolveStatus::FAILED;
    if (n) memcpy(out->value, arg, n);
    out->length = n;
    return ResolveStatus::OK;
}

// `${port}` resolves to the effective port of the request context.
static ResolveStatus portResolve(void *, const ResolveContext &request, const char *name, const char *, ResolveOutput *out) {
    if (strcmp(name, "port") != 0) return ResolveStatus::UNKNOWN;
    char text[8];
    int n = snprintf(text, sizeof(text), "%u", static_cast<unsigned>(request.port));
    memcpy(out->value, text, static_cast<size_t>(n));
    out->length = static_cast<size_t>(n);
    return ResolveStatus::OK;
}

struct FailingResolver {
    const char *reasonCode;
};

static ResolveStatus failingResolve(void *context, const ResolveContext &, const char *name, const char *, ResolveOutput *out) {
    if (strcmp(name, "secret") != 0) return ResolveStatus::UNKNOWN;
    out->reasonCode = static_cast<FailingResolver *>(context)->reasonCode;
    snprintf(out->reasonDetail, sizeof(out->reasonDetail), "slot empty");
    return ResolveStatus::FAILED;
}

static ResolveContext ctx() {
    ResolveContext c;
    memset(&c, 0, sizeof(c));
    c.module = Module::HTTP;
    c.ip[0] = 192; c.ip[1] = 168; c.ip[2] = 2; c.ip[3] = 40;
    c.port = 8081;
    c.onLink = true;
    return c;
}

static char gOut[1024];
static size_t gLen;
static uint8_t gScratch[2048];

static ExpandStatus expand(const char *tmpl, const Resolver *chain, size_t chainLength, ExpandResult *result,
                           size_t cap = sizeof(gOut) - 1) {
    memset(gOut, 0, sizeof(gOut));
    ExpandStatus s = expandTemplate(tmpl, strlen(tmpl), chain, chainLength, ctx(), reinterpret_cast<uint8_t *>(gOut), cap,
                                    gScratch, sizeof(gScratch), result);
    gLen = result->length;
    if (s == ExpandStatus::OK) gOut[gLen] = '\0';
    return s;
}

static ExpandStatus expandFresh(const char *tmpl, const Resolver *chain, size_t chainLength) {
    ExpandResult r;
    memset(&r, 0, sizeof(r));
    return expand(tmpl, chain, chainLength, &r);
}

static const Resolver ECHO[] = {{echoResolve, nullptr}};

// ============================================================================
// Tokenizer
// ============================================================================

void test_template_plain_text_and_literals(void) {
    TEST_ASSERT_TRUE(expandFresh("{\"a\":1}", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", gOut);
    // Escaped literal
    TEST_ASSERT_TRUE(expandFresh("$${x}", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("${x}", gOut);
    TEST_ASSERT_TRUE(expandFresh("a$${echo:z}b", ECHO, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("a${echo:z}b", gOut);
    // Lone dollar signs are literal
    TEST_ASSERT_TRUE(expandFresh("cost $5", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("cost $5", gOut);
    TEST_ASSERT_TRUE(expandFresh("$", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("$", gOut);
    TEST_ASSERT_TRUE(expandFresh("end$", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("end$", gOut);
    TEST_ASSERT_TRUE(expandFresh("{x}$", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("{x}$", gOut);
    // Empty template
    TEST_ASSERT_TRUE(expandFresh("", nullptr, 0) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_size_t(0, gLen);
}

void test_template_has_placeholder(void) {
    TEST_ASSERT_TRUE(hasPlaceholder("a${x}", 5));
    TEST_ASSERT_TRUE(hasPlaceholder("$${x}", 5));
    TEST_ASSERT_FALSE(hasPlaceholder("a$b{c}", 6));
    TEST_ASSERT_FALSE(hasPlaceholder("", 0));
    TEST_ASSERT_FALSE(hasPlaceholder("a${x}", 2)); // length bounds the scan
}

void test_template_unknown_name(void) {
    // No chain at all: every placeholder is BAD_TEMPLATE
    TEST_ASSERT_TRUE(expandFresh("${foo}", nullptr, 0) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_TRUE(expandFresh("x=${now}", nullptr, 0) == ExpandStatus::BAD_TEMPLATE);
    // A chain without that name
    TEST_ASSERT_TRUE(expandFresh("${foo}", ECHO, 1) == ExpandStatus::BAD_TEMPLATE);
}

void test_template_chain_order_and_context(void) {
    FixedResolver a{"cap", "AAA", false, false, {0}, 0};
    Resolver chain[] = {{echoResolve, nullptr}, {fixedResolve, &a}, {portResolve, nullptr}};
    TEST_ASSERT_TRUE(expandFresh("${echo:x}-${cap:tok}-${port}", chain, 3) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("x-AAA-8081", gOut);
    TEST_ASSERT_EQUAL_INT(1, a.calls);
}

void test_template_resolver_not_visible_to_other_caller(void) {
    FixedResolver cap{"cap", "CAPTURED-SESSION", false, false, {0}, 0};
    Resolver first[] = {{fixedResolve, &cap}};
    TEST_ASSERT_TRUE(expandFresh("sid=${cap:tok}", first, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("sid=CAPTURED-SESSION", gOut);

    // Another caller with another chain, then with none
    TEST_ASSERT_TRUE(expandFresh("sid=${cap:tok}", ECHO, 1) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_NULL(strstr(gOut, "CAPTURED"));
    TEST_ASSERT_TRUE(expandFresh("sid=${cap:tok}", nullptr, 0) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_NULL(strstr(gOut, "CAPTURED"));
    TEST_ASSERT_EQUAL_INT(1, cap.calls);
}

void test_template_malformed(void) {
    const char *bad[] = {"${}",
                         "${abc",
                         "${Foo}",
                         "${a1}",
                         "${a-b}",
                         "${echo:}",
                         "${echo:a/b}",
                         "${echo:a b}",
                         "${echo:a:b}",
                         "${echo:x|}",
                         "${echo:x|URL}",
                         "${echo:x|hex}",
                         "${echo:x|url|}",
                         "${echo:x|b64|b64|b64|b64}",
                         "${ echo:x}",
                         "${echo:x }"};
    for (const char *t : bad) {
        TEST_ASSERT_TRUE_MESSAGE(expandFresh(t, ECHO, 1) == ExpandStatus::BAD_TEMPLATE, t);
    }
}

void test_template_name_and_arg_limits(void) {
    FixedResolver n16{"abcdefghijklmnop", "ok", false, false, {0}, 0};
    FixedResolver n17{"abcdefghijklmnopq", "ok", false, false, {0}, 0};
    Resolver c16[] = {{fixedResolve, &n16}};
    Resolver c17[] = {{fixedResolve, &n17}};
    TEST_ASSERT_TRUE(expandFresh("${abcdefghijklmnop}", c16, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("ok", gOut);
    TEST_ASSERT_TRUE(expandFresh("${abcdefghijklmnopq}", c17, 1) == ExpandStatus::BAD_TEMPLATE);
    FixedResolver under{"a_b", "u", false, false, {0}, 0};
    Resolver cu[] = {{fixedResolve, &under}};
    TEST_ASSERT_TRUE(expandFresh("${a_b}", cu, 1) == ExpandStatus::OK);

    TEST_ASSERT_TRUE(expandFresh("${echo:Ab0_.-z}", ECHO, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("Ab0_.-z", gOut);
    // 32-byte arg accepted, 33 refused
    TEST_ASSERT_TRUE(expandFresh("${echo:abcdefghijklmnopqrstuvwxyz012345}", ECHO, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz012345", gOut);
    TEST_ASSERT_TRUE(expandFresh("${echo:abcdefghijklmnopqrstuvwxyz0123456}", ECHO, 1) == ExpandStatus::BAD_TEMPLATE);
}

// ============================================================================
// Encodings
// ============================================================================

void test_template_encodings(void) {
    FixedResolver v{"v", "a b/\"c\"~", false, false, {0}, 0};
    Resolver chain[] = {{fixedResolve, &v}};
    TEST_ASSERT_TRUE(expandFresh("${v|url}", chain, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("a%20b%2F%22c%22~", gOut);
    TEST_ASSERT_TRUE(expandFresh("${v|json}", chain, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("a b/\\\"c\\\"~", gOut);
    // Left to right: json then url
    TEST_ASSERT_TRUE(expandFresh("${v|json|url}", chain, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("a%20b%2F%5C%22c%5C%22~", gOut);

    FixedResolver f{"f", "f", false, false, {0}, 0};
    Resolver cf[] = {{fixedResolve, &f}};
    TEST_ASSERT_TRUE(expandFresh("${f|b64}", cf, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("Zg==", gOut);
    // Three encodings accepted
    TEST_ASSERT_TRUE(expandFresh("${f|b64|b64|b64}", cf, 1) == ExpandStatus::OK);
    // b64("f") = "Zg==", b64("Zg==") = "Wmc9PQ==", b64("Wmc9PQ==") = "V21jOVBRPT0="
    TEST_ASSERT_EQUAL_STRING("V21jOVBRPT0=", gOut);
    TEST_ASSERT_TRUE(expandFresh("${f|b64|b64|b64|b64}", cf, 1) == ExpandStatus::BAD_TEMPLATE);
    TEST_ASSERT_TRUE(expandFresh("${echo:ab|url}", ECHO, 1) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("ab", gOut);
}

static const char *enc(size_t (*fn)(const uint8_t *, size_t, uint8_t *, size_t, bool *), const char *in, size_t len) {
    static char out[256];
    bool ok = false;
    size_t n = fn(reinterpret_cast<const uint8_t *>(in), len, reinterpret_cast<uint8_t *>(out), sizeof(out) - 1, &ok);
    TEST_ASSERT_TRUE(ok);
    out[n] = '\0';
    return out;
}

void test_encode_base64_rfc4648(void) {
    // RFC 4648 section 10
    TEST_ASSERT_EQUAL_STRING("", enc(encodeBase64, "", 0));
    TEST_ASSERT_EQUAL_STRING("Zg==", enc(encodeBase64, "f", 1));
    TEST_ASSERT_EQUAL_STRING("Zm8=", enc(encodeBase64, "fo", 2));
    TEST_ASSERT_EQUAL_STRING("Zm9v", enc(encodeBase64, "foo", 3));
    TEST_ASSERT_EQUAL_STRING("Zm9vYg==", enc(encodeBase64, "foob", 4));
    TEST_ASSERT_EQUAL_STRING("Zm9vYmE=", enc(encodeBase64, "fooba", 5));
    TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", enc(encodeBase64, "foobar", 6));
    // Standard alphabet (+ and /), not URL-safe
    const char bin[] = {static_cast<char>(0xfb), static_cast<char>(0xff), static_cast<char>(0xbf)};
    TEST_ASSERT_EQUAL_STRING("+/+/", enc(encodeBase64, bin, 3));
    bool ok = true;
    uint8_t small[3];
    TEST_ASSERT_EQUAL_size_t(0, encodeBase64(reinterpret_cast<const uint8_t *>("f"), 1, small, sizeof(small), &ok));
    TEST_ASSERT_FALSE(ok);
}

void test_encode_url_rfc3986(void) {
    TEST_ASSERT_EQUAL_STRING("", enc(encodeUrl, "", 0));
    // Unreserved characters stay as they are
    TEST_ASSERT_EQUAL_STRING("AZaz09-._~", enc(encodeUrl, "AZaz09-._~", 10));
    // Reserved and other bytes are percent-encoded with uppercase hex
    TEST_ASSERT_EQUAL_STRING("%3A%2F%3F%23%5B%5D%40%21%24%26%27%28%29%2A%2B%2C%3B%3D%25%20",
                             enc(encodeUrl, ":/?#[]@!$&'()*+,;=% ", 20));
    TEST_ASSERT_EQUAL_STRING("%E2%82%AC", enc(encodeUrl, "\xE2\x82\xAC", 3));
    const char nul[] = {'a', 0, 'b'};
    TEST_ASSERT_EQUAL_STRING("a%00b", enc(encodeUrl, nul, 3));
    bool ok = true;
    uint8_t small[2];
    TEST_ASSERT_EQUAL_size_t(0, encodeUrl(reinterpret_cast<const uint8_t *>(" "), 1, small, sizeof(small), &ok));
    TEST_ASSERT_FALSE(ok);
}

void test_encode_json(void) {
    TEST_ASSERT_EQUAL_STRING("", enc(encodeJson, "", 0));
    TEST_ASSERT_EQUAL_STRING("plain", enc(encodeJson, "plain", 5));
    TEST_ASSERT_EQUAL_STRING("\\\"q\\\" \\\\", enc(encodeJson, "\"q\" \\", 5));
    TEST_ASSERT_EQUAL_STRING("a\\nb\\rc\\td", enc(encodeJson, "a\nb\rc\td", 7));
    TEST_ASSERT_EQUAL_STRING("\\u0001", enc(encodeJson, "\x01", 1));
    TEST_ASSERT_EQUAL_STRING("\\u0000", enc(encodeJson, "\0", 1));
    // Non-ASCII UTF-8 passes through
    TEST_ASSERT_EQUAL_STRING("\xE2\x82\xAC", enc(encodeJson, "\xE2\x82\xAC", 3));
    bool ok = true;
    uint8_t small[1];
    TEST_ASSERT_EQUAL_size_t(0, encodeJson(reinterpret_cast<const uint8_t *>("\""), 1, small, sizeof(small), &ok));
    TEST_ASSERT_FALSE(ok);
}

// ============================================================================
// Result flags, limits and failures
// ============================================================================

void test_template_sensitive_propagates(void) {
    FixedResolver secret{"secret", "hunter2", true, false, {0}, 0};
    Resolver chain[] = {{fixedResolve, &secret}, {echoResolve, nullptr}};
    ExpandResult r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("${echo:a}", chain, 2, &r) == ExpandStatus::OK);
    TEST_ASSERT_FALSE(r.sensitive);
    TEST_ASSERT_TRUE(expand("k=${secret:t|url}", chain, 2, &r) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("k=hunter2", gOut);
    TEST_ASSERT_TRUE(r.sensitive);
    // Accumulates across the fields of one request
    TEST_ASSERT_TRUE(expand("plain", chain, 2, &r) == ExpandStatus::OK);
    TEST_ASSERT_TRUE(r.sensitive);
}

void test_template_cap_overflow(void) {
    ExpandResult r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("12345", nullptr, 0, &r, 5) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_size_t(5, r.length);
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("123456", nullptr, 0, &r, 5) == ExpandStatus::TOO_LONG);
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("12${echo:abc}", ECHO, 1, &r, 5) == ExpandStatus::OK);
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("12${echo:abcd}", ECHO, 1, &r, 5) == ExpandStatus::TOO_LONG);
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("${echo:abcd|b64}", ECHO, 1, &r, 7) == ExpandStatus::TOO_LONG); // "YWJjZA==" is 8
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("${echo:abcd|b64}", ECHO, 1, &r, 8) == ExpandStatus::OK);
    TEST_ASSERT_EQUAL_STRING("YWJjZA==", gOut);
}

void test_template_resolver_failed(void) {
    FailingResolver f{"SECRET_UNAVAILABLE"};
    Resolver chain[] = {{failingResolve, &f}};
    ExpandResult r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("${secret:t}", chain, 1, &r) == ExpandStatus::RESOLVER_FAILED);
    TEST_ASSERT_EQUAL_STRING("SECRET_UNAVAILABLE", r.reasonCode);
    TEST_ASSERT_EQUAL_STRING("slot empty", r.reasonDetail);

    // Without its own reason the failure still refuses the request
    FailingResolver plain{nullptr};
    Resolver chain2[] = {{failingResolve, &plain}};
    memset(&r, 0, sizeof(r));
    ExpandStatus s = expand("${secret:t}", chain2, 1, &r);
    TEST_ASSERT_TRUE(s == ExpandStatus::RESOLVER_FAILED || s == ExpandStatus::BAD_TEMPLATE);
}

void test_template_mac_requirement(void) {
    FixedResolver a{"a", "1", true, true, {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, 0};
    FixedResolver b{"b", "2", true, true, {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, 0};
    FixedResolver c{"c", "3", true, true, {0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee}, 0};
    Resolver chain[] = {{fixedResolve, &a}, {fixedResolve, &b}, {fixedResolve, &c}};
    ExpandResult r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("plain", chain, 3, &r) == ExpandStatus::OK);
    TEST_ASSERT_FALSE(r.requireMac);

    TEST_ASSERT_TRUE(expand("${a}${b}", chain, 3, &r) == ExpandStatus::OK);
    TEST_ASSERT_TRUE(r.requireMac);
    TEST_ASSERT_FALSE(r.macConflict);
    TEST_ASSERT_EQUAL_MEMORY(a.mac, r.mac, 6);
    TEST_ASSERT_EQUAL_STRING("SECRET_TARGET_CHANGED", r.macReasonCode);

    // A different MAC in a later field of the same request conflicts
    TEST_ASSERT_TRUE(expand("${c}", chain, 3, &r) == ExpandStatus::OK);
    TEST_ASSERT_TRUE(r.macConflict);

    memset(&r, 0, sizeof(r));
    TEST_ASSERT_TRUE(expand("${a}-${c}", chain, 3, &r) == ExpandStatus::OK);
    TEST_ASSERT_TRUE(r.macConflict);
}

void run_gateway_template_tests(void) {
    RUN_TEST(test_template_plain_text_and_literals);
    RUN_TEST(test_template_has_placeholder);
    RUN_TEST(test_template_unknown_name);
    RUN_TEST(test_template_chain_order_and_context);
    RUN_TEST(test_template_resolver_not_visible_to_other_caller);
    RUN_TEST(test_template_malformed);
    RUN_TEST(test_template_name_and_arg_limits);
    RUN_TEST(test_template_encodings);
    RUN_TEST(test_encode_base64_rfc4648);
    RUN_TEST(test_encode_url_rfc3986);
    RUN_TEST(test_encode_json);
    RUN_TEST(test_template_sensitive_propagates);
    RUN_TEST(test_template_cap_overflow);
    RUN_TEST(test_template_resolver_failed);
    RUN_TEST(test_template_mac_requirement);
}
