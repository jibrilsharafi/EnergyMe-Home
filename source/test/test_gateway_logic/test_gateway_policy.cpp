// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the gateway policy (openspec gateway-modules): canonical
// addresses, read/write classification, write grants and read declarations,
// MAC binding decisions and digests. Written from the spec scenarios.

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "gateway_logic.h"
#include "gateway_policy.h"

using namespace GatewayLogic;

// ============================================================================
// Helpers
// ============================================================================

static uint32_t hostIp(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | d;
}

// Arbitrary 32-byte "digest": the policy only compares bytes, it never hashes.
static void fakeDigest(uint8_t seed, uint8_t out[DIGEST_BYTES]) {
    for (size_t i = 0; i < DIGEST_BYTES; i++) out[i] = static_cast<uint8_t>(seed * 31 + i * 7 + 1);
}

// Independent hex formatter so tests do not rely on formatHexDigest.
static void toHex(const uint8_t digest[DIGEST_BYTES], char out[65]) {
    for (size_t i = 0; i < DIGEST_BYTES; i++) snprintf(out + i * 2, 3, "%02x", digest[i]);
}

static const char *validate(const char *ip, int64_t port, const char *module, const char *kind, const char *mac,
                            std::initializer_list<const char *> allow, GrantEntry *out) {
    GrantInput in{};
    in.ip = ip;
    in.port = port;
    in.module = module;
    in.kind = kind;
    in.mac = mac;
    in.allow = allow.begin();
    in.allowCount = allow.size();
    return validateGrant(in, out);
}

static GrantEntry entry(const char *ip, int64_t port, const char *module, const char *kind, const char *mac,
                        std::initializer_list<const char *> allow) {
    GrantEntry e;
    memset(&e, 0, sizeof(e));
    const char *reason = validate(ip, port, module, kind, mac, allow, &e);
    TEST_ASSERT_NULL_MESSAGE(reason, reason ? reason : "");
    return e;
}

static bool refused(const char *ip, int64_t port, const char *module, const char *kind, const char *mac,
                    std::initializer_list<const char *> allow) {
    GrantEntry e;
    memset(&e, 0, sizeof(e));
    return validate(ip, port, module, kind, mac, allow, &e) != nullptr;
}

static const uint8_t XWEB_IP[4] = {192, 168, 2, 40};

// The read declaration of the spec scenarios.
static GrantEntry xwebDeclaration() {
    return entry("192.168.2.40", 80, "http", "read", "00:11:22:33:44:55",
                 {"POST /cgi-bin/login.lua", "POST /cgi-bin/datachart.cgi"});
}

static PolicyRequest httpReq(const char *method, const char *path, Access access = Access::NONE, bool hasBody = false,
                             uint16_t port = 80, Module module = Module::HTTP) {
    PolicyRequest r;
    memset(&r, 0, sizeof(r));
    r.module = module;
    memcpy(r.ip, XWEB_IP, 4);
    r.port = port;
    r.access = access;
    r.method = method;
    r.path = path;
    r.hasBody = hasBody;
    return r;
}

static PolicyRequest modbusReq(uint8_t fc, const char *address, const uint16_t *values, size_t count,
                               Access access = Access::NONE, uint16_t port = 502) {
    PolicyRequest r;
    memset(&r, 0, sizeof(r));
    r.module = Module::MODBUS_TCP;
    r.ip[0] = 192; r.ip[1] = 168; r.ip[2] = 2; r.ip[3] = 50;
    r.port = port;
    r.access = access;
    r.function = fc;
    TEST_ASSERT_TRUE_MESSAGE(parseModbusAddress(address, &r.modbus), address);
    r.values = values;
    r.valueCount = count;
    return r;
}

static PolicyRequest rawReq(Module module, uint16_t port, size_t length, const uint8_t *digest,
                            Access access = Access::NONE) {
    PolicyRequest r;
    memset(&r, 0, sizeof(r));
    r.module = module;
    r.ip[0] = 192; r.ip[1] = 168; r.ip[2] = 2; r.ip[3] = 60;
    r.port = port;
    r.access = access;
    r.payloadLength = length;
    r.payloadDigest = digest;
    return r;
}

// ============================================================================
// New ops and reserved headers
// ============================================================================

void test_parse_new_ops(void) {
    TEST_ASSERT_TRUE(parseOp("tcp_request") == Op::TCP_REQUEST);
    TEST_ASSERT_TRUE(parseOp("https_request") == Op::HTTPS_REQUEST);
    TEST_ASSERT_TRUE(parseOp("tls_probe") == Op::TLS_PROBE);
    TEST_ASSERT_EQUAL_STRING("tcp_request", opName(Op::TCP_REQUEST));
    TEST_ASSERT_EQUAL_STRING("https_request", opName(Op::HTTPS_REQUEST));
    TEST_ASSERT_EQUAL_STRING("tls_probe", opName(Op::TLS_PROBE));
}

void test_method_override_headers_reserved(void) {
    TEST_ASSERT_TRUE(isReservedHeaderName("X-HTTP-Method-Override"));
    TEST_ASSERT_TRUE(isReservedHeaderName("x-http-method-override"));
    TEST_ASSERT_TRUE(isReservedHeaderName("X-HTTP-Method"));
    TEST_ASSERT_TRUE(isReservedHeaderName("x-http-METHOD"));
    TEST_ASSERT_TRUE(isReservedHeaderName("X-Method-Override"));
    TEST_ASSERT_TRUE(isReservedHeaderName("X-METHOD-OVERRIDE"));
    TEST_ASSERT_FALSE(isReservedHeaderName("X-HTTP-Method-Overrides"));
    TEST_ASSERT_FALSE(isReservedHeaderName("X-Method"));
}

void test_parse_module(void) {
    Module m;
    TEST_ASSERT_TRUE(parseModule("http", &m) && m == Module::HTTP);
    TEST_ASSERT_TRUE(parseModule("https", &m) && m == Module::HTTPS);
    TEST_ASSERT_TRUE(parseModule("modbus_tcp", &m) && m == Module::MODBUS_TCP);
    TEST_ASSERT_TRUE(parseModule("tcp", &m) && m == Module::TCP);
    TEST_ASSERT_TRUE(parseModule("udp", &m) && m == Module::UDP);
    TEST_ASSERT_FALSE(parseModule("ftp", &m));
    TEST_ASSERT_FALSE(parseModule("HTTP", &m));
    TEST_ASSERT_FALSE(parseModule(nullptr, &m));
    TEST_ASSERT_EQUAL_STRING("modbus_tcp", moduleName(Module::MODBUS_TCP));
}

// ============================================================================
// Canonical Modbus addresses
// ============================================================================

void test_modbus_address_round_trip(void) {
    const char *good[] = {"u3/hr3000", "u0/co0", "u255/ir65535", "u1/di0", "u12/hr100", "u1/co65535"};
    for (const char *s : good) {
        ModbusAddress a;
        TEST_ASSERT_TRUE_MESSAGE(parseModbusAddress(s, &a), s);
        char out[MODBUS_ADDRESS_BUFFER];
        TEST_ASSERT_TRUE_MESSAGE(formatModbusAddress(a, out, sizeof(out)), s);
        TEST_ASSERT_EQUAL_STRING(s, out);
    }
    ModbusAddress a;
    TEST_ASSERT_TRUE(parseModbusAddress("u3/hr3000", &a));
    TEST_ASSERT_EQUAL_UINT8(3, a.unit);
    TEST_ASSERT_TRUE(a.table == ModbusTable::HOLDING_REGISTERS);
    TEST_ASSERT_EQUAL_UINT16(3000, a.address);
    TEST_ASSERT_TRUE(parseModbusAddress("u1/di7", &a) && a.table == ModbusTable::DISCRETE_INPUTS);
    TEST_ASSERT_TRUE(parseModbusAddress("u1/ir7", &a) && a.table == ModbusTable::INPUT_REGISTERS);
    TEST_ASSERT_TRUE(parseModbusAddress("u1/co7", &a) && a.table == ModbusTable::COILS);
}

void test_modbus_address_malformed(void) {
    const char *bad[] = {"u256/hr1", "u1/hr65536", "u1/hr007", "u1/xx5", "U1/HR5", "u01/hr5", "u1/hr",
                         "u1hr5",   "",           "u1/hr5 ",  "u1/hr-1", "u/hr5", "u1/HR5", "u1/hr40001x"};
    for (const char *s : bad) {
        ModbusAddress a;
        TEST_ASSERT_FALSE_MESSAGE(parseModbusAddress(s, &a), s);
    }
    ModbusAddress a;
    TEST_ASSERT_FALSE(parseModbusAddress(nullptr, &a));
}

void test_modbus_format_small_cap(void) {
    ModbusAddress a{255, ModbusTable::HOLDING_REGISTERS, 65535};
    char out[MODBUS_ADDRESS_BUFFER];
    TEST_ASSERT_TRUE(formatModbusAddress(a, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("u255/hr65535", out);
    char tiny[5];
    TEST_ASSERT_FALSE(formatModbusAddress(a, tiny, sizeof(tiny)));
}

void test_modbus_function_codes(void) {
    TEST_ASSERT_EQUAL_UINT8(1, functionFor(ModbusTable::COILS, false, 1, false));
    TEST_ASSERT_EQUAL_UINT8(5, functionFor(ModbusTable::COILS, true, 1, false));
    TEST_ASSERT_EQUAL_UINT8(15, functionFor(ModbusTable::COILS, true, 2, false));
    TEST_ASSERT_EQUAL_UINT8(15, functionFor(ModbusTable::COILS, true, 1, true));
    TEST_ASSERT_EQUAL_UINT8(2, functionFor(ModbusTable::DISCRETE_INPUTS, false, 1, false));
    TEST_ASSERT_EQUAL_UINT8(4, functionFor(ModbusTable::INPUT_REGISTERS, false, 1, false));
    TEST_ASSERT_EQUAL_UINT8(3, functionFor(ModbusTable::HOLDING_REGISTERS, false, 2, false));
    TEST_ASSERT_EQUAL_UINT8(6, functionFor(ModbusTable::HOLDING_REGISTERS, true, 1, false));
    TEST_ASSERT_EQUAL_UINT8(16, functionFor(ModbusTable::HOLDING_REGISTERS, true, 2, false));
    TEST_ASSERT_EQUAL_UINT8(16, functionFor(ModbusTable::HOLDING_REGISTERS, true, 1, true));
    // Read-only tables cannot be written
    TEST_ASSERT_EQUAL_UINT8(0, functionFor(ModbusTable::INPUT_REGISTERS, true, 1, false));
    TEST_ASSERT_EQUAL_UINT8(0, functionFor(ModbusTable::DISCRETE_INPUTS, true, 1, false));
    TEST_ASSERT_TRUE(isWritableTable(ModbusTable::COILS));
    TEST_ASSERT_TRUE(isWritableTable(ModbusTable::HOLDING_REGISTERS));
    TEST_ASSERT_FALSE(isWritableTable(ModbusTable::INPUT_REGISTERS));
    TEST_ASSERT_FALSE(isWritableTable(ModbusTable::DISCRETE_INPUTS));
}

void test_modbus_table_for_function(void) {
    ModbusTable t;
    TEST_ASSERT_TRUE(tableForFunction(1, &t) && t == ModbusTable::COILS);
    TEST_ASSERT_TRUE(tableForFunction(2, &t) && t == ModbusTable::DISCRETE_INPUTS);
    TEST_ASSERT_TRUE(tableForFunction(3, &t) && t == ModbusTable::HOLDING_REGISTERS);
    TEST_ASSERT_TRUE(tableForFunction(4, &t) && t == ModbusTable::INPUT_REGISTERS);
    TEST_ASSERT_TRUE(tableForFunction(5, &t) && t == ModbusTable::COILS);
    TEST_ASSERT_TRUE(tableForFunction(6, &t) && t == ModbusTable::HOLDING_REGISTERS);
    TEST_ASSERT_TRUE(tableForFunction(15, &t) && t == ModbusTable::COILS);
    TEST_ASSERT_TRUE(tableForFunction(16, &t) && t == ModbusTable::HOLDING_REGISTERS);
    TEST_ASSERT_FALSE(tableForFunction(0, &t));
    TEST_ASSERT_FALSE(tableForFunction(7, &t));
    TEST_ASSERT_FALSE(tableForFunction(23, &t));
}

void test_modbus_range_fits(void) {
    TEST_ASSERT_FALSE(modbusRangeFits(65500, 100)); // spec: u1/hr65500 + count 100
    TEST_ASSERT_TRUE(modbusRangeFits(65436, 100));  // ends exactly at 65536
    TEST_ASSERT_FALSE(modbusRangeFits(65437, 100));
    TEST_ASSERT_TRUE(modbusRangeFits(65535, 1));
    TEST_ASSERT_FALSE(modbusRangeFits(65535, 2));
    TEST_ASSERT_FALSE(modbusRangeFits(0, 0)); // count 0
    TEST_ASSERT_TRUE(modbusRangeFits(0, 65536));
    TEST_ASSERT_FALSE(modbusRangeFits(0, 65537));
}

// ============================================================================
// HTTP addresses and safe paths
// ============================================================================

void test_http_address_drops_query(void) {
    char out[64];
    TEST_ASSERT_TRUE(formatHttpAddress("POST", "/x?k=secret", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("POST /x", out);
    TEST_ASSERT_TRUE(formatHttpAddress("GET", "/rpc/Switch.Set?id=0&on=false", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("GET /rpc/Switch.Set", out);
    TEST_ASSERT_TRUE(formatHttpAddress("DELETE", "/a/b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("DELETE /a/b", out);
    char tiny[7];
    TEST_ASSERT_FALSE(formatHttpAddress("POST", "/abcdef", tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_size_t(2, httpPathLength("/a?b=c"));
    TEST_ASSERT_EQUAL_size_t(4, httpPathLength("/a/b"));
    TEST_ASSERT_EQUAL_size_t(1, httpPathLength("/?"));
}

void test_safe_grant_path(void) {
    const char *good[] = {"/rpc/Switch.Set", "/", "/cgi-bin/datachart.cgi", "/a.b/c.d", "/file..txt", "/a/.hidden",
                          "/a?x=../y", "/a?x=%2f", "/a?x=//", "/a?x=\\", "/a%20b"};
    for (const char *s : good) TEST_ASSERT_TRUE_MESSAGE(isSafeGrantPath(s), s);
    const char *bad[] = {"/a/./b",
                         "/a/../b",
                         "/..",
                         "/.",
                         "/a/.",
                         "/a/..",
                         "/a/..?x=1",
                         "/rpc/Switch./../Sys.Reboot",
                         "/rpc/Switch.%2e%2e/Sys.Reboot",
                         "/rpc/Switch.%2E%2E/Sys.Reboot",
                         "/a%2fb",
                         "/a%2Fb",
                         "/a%5cb",
                         "/a%5Cb",
                         "/a%2eb",
                         "/rpc//Switch.Set",
                         "//",
                         "/a\\b",
                         "/cgi-bin/datachart.cgi/../setpoint.cgi"};
    for (const char *s : bad) TEST_ASSERT_FALSE_MESSAGE(isSafeGrantPath(s), s);
}

// ============================================================================
// Grant validation
// ============================================================================

void test_grant_valid_entries(void) {
    GrantEntry e = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.*"});
    TEST_ASSERT_TRUE(e.kind == GrantKind::WRITE); // kind defaults to write
    TEST_ASSERT_TRUE(e.module == Module::HTTP);
    TEST_ASSERT_EQUAL_UINT8(192, e.ip[0]);
    TEST_ASSERT_EQUAL_UINT8(40, e.ip[3]);
    TEST_ASSERT_EQUAL_UINT16(80, e.port);
    TEST_ASSERT_TRUE(e.hasMac);
    TEST_ASSERT_EQUAL_UINT8(0x55, e.mac[5]);
    TEST_ASSERT_FALSE(e.wildcard);
    TEST_ASSERT_EQUAL_UINT8(1, e.allowCount);
    char text[GRANT_ALLOW_TEXT_BUFFER];
    TEST_ASSERT_TRUE(grantAllowText(e, 0, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("/rpc/Switch.*", text);
    TEST_ASSERT_FALSE(grantAllowText(e, 1, text, sizeof(text)));

    GrantEntry w = entry("192.168.2.40", 80, "http", "write", nullptr, {"*"});
    TEST_ASSERT_TRUE(w.wildcard);
    TEST_ASSERT_FALSE(w.hasMac);
    TEST_ASSERT_TRUE(grantAllowText(w, 0, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("*", text);

    GrantEntry r = xwebDeclaration();
    TEST_ASSERT_TRUE(r.kind == GrantKind::READ);
    TEST_ASSERT_EQUAL_UINT8(2, r.allowCount);
    TEST_ASSERT_TRUE(grantAllowText(r, 1, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING("POST /cgi-bin/datachart.cgi", text);

    entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/hr100:2=0..400", "u1/co0:4", "u2/hr0:1"});
    entry("10.20.0.5", 502, "modbus_tcp", "write", nullptr, {"u1/hr65535:1"});
    entry("172.16.0.1", 443, "https", "read", nullptr, {"PUT /a", "PATCH /b", "DELETE /c", "POST /d"});

    uint8_t d[DIGEST_BYTES];
    fakeDigest(1, d);
    char hex[65];
    toHex(d, hex);
    GrantEntry u = entry("192.168.2.61", 4210, "udp", nullptr, "00:11:22:33:44:77", {hex});
    TEST_ASSERT_FALSE(u.wildcard);
    TEST_ASSERT_TRUE(grantAllowText(u, 0, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(hex, text);
    TEST_ASSERT_EQUAL_MEMORY(d, u.allow[0], DIGEST_BYTES); // stored as 32 bytes binary
    entry("192.168.2.61", 4210, "tcp", "read", nullptr, {hex});
    GrantEntry uw = entry("192.168.2.61", 4210, "udp", "write", nullptr, {"*"});
    TEST_ASSERT_TRUE(uw.wildcard);
}

void test_grant_invalid_entries(void) {
    uint8_t d[DIGEST_BYTES];
    fakeDigest(2, d);
    char hex[65];
    toHex(d, hex);
    char hex63[64];
    memcpy(hex63, hex, 63);
    hex63[63] = '\0';
    char hexUpper[65];
    memcpy(hexUpper, hex, sizeof(hexUpper));
    for (char &c : hexUpper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');

    const char *mac = "00:11:22:33:44:55";
    // Target
    TEST_ASSERT_TRUE(refused("8.8.8.8", 80, "http", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2", 80, "http", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused(nullptr, 80, "http", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 0, "http", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 65536, "http", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "ftp", nullptr, mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, nullptr, nullptr, mac, {"/a"}));
    // Kind
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "admin", mac, {"/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "READ", mac, {"POST /a"}));
    // Allow count
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"/a", "/b", "/c", "/d", "/e"}));
    // Modbus
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/di0:4"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/ir0:4"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", "read", mac, {"u1/hr100:2"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr65535:2"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr65536:1"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr100:2=400..0"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr100:2=0..65536"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr100:0"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u1/hr100"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"u256/hr100:1"}));
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, mac, {"*"}));
    // HTTP read declarations
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"POST /a*"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"*"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"/cgi-bin/datachart.cgi"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"GET /cgi-bin/datachart.cgi"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"post /cgi-bin/datachart.cgi"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"POST  /cgi-bin/datachart.cgi"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"POST/cgi-bin/datachart.cgi"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"HEAD /a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"POST /a/../b"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", mac, {"POST cgi-bin/x"}));
    // HTTP write paths
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"rpc/Switch.Set"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"/rpc/../Sys.Reboot"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"/rpc//x"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"/rpc%2fx"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"/a\\b"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"*", "/a"}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, mac, {"POST /a"}));
    // Raw payload hashes
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "tcp", nullptr, mac, {hex63}));
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "tcp", nullptr, mac, {hexUpper}));
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "tcp", nullptr, mac, {"*", hex}));
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "udp", nullptr, mac, {hex, "*"}));
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "udp", "read", mac, {"*"}));
    TEST_ASSERT_TRUE(refused("192.168.2.61", 4210, "udp", nullptr, mac, {"/a"}));
    // MAC
    const char *badMacs[] = {"00:11:22:33:44",    "00-11-22-33-44-55", "ff:ff:ff:ff:ff:ff", "01:00:5e:00:00:01",
                             "00:00:00:00:00:00", "00:11:22:33:44:AA", "0:11:22:33:44:55",  "00:11:22:33:44:55:66",
                             "",                  "00:11:22:33:44:5g"};
    for (const char *m : badMacs) {
        TEST_ASSERT_TRUE_MESSAGE(refused("192.168.2.40", 80, "http", nullptr, m, {"/a"}), m);
    }
}

void test_grant_48_byte_boundary(void) {
    char path48[49];
    path48[0] = '/';
    memset(path48 + 1, 'a', 47);
    path48[48] = '\0';
    char path49[50];
    path49[0] = '/';
    memset(path49 + 1, 'a', 48);
    path49[49] = '\0';
    TEST_ASSERT_FALSE(refused("192.168.2.40", 80, "http", nullptr, nullptr, {path48}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", nullptr, nullptr, {path49}));

    GrantEntry e = entry("192.168.2.40", 80, "http", nullptr, nullptr, {path48});
    char text[GRANT_ALLOW_TEXT_BUFFER];
    TEST_ASSERT_TRUE(grantAllowText(e, 0, text, sizeof(text)));
    TEST_ASSERT_EQUAL_STRING(path48, text); // full 48 bytes, no terminator in the slot

    // Read strings: method and space count toward the 48 bytes
    char read48[49];
    memcpy(read48, "POST /", 6);
    memset(read48 + 6, 'b', 42);
    read48[48] = '\0';
    char read49[50];
    memcpy(read49, "POST /", 6);
    memset(read49 + 6, 'b', 43);
    read49[49] = '\0';
    TEST_ASSERT_FALSE(refused("192.168.2.40", 80, "http", "read", nullptr, {read48}));
    TEST_ASSERT_TRUE(refused("192.168.2.40", 80, "http", "read", nullptr, {read49}));

    // Modbus string over 48 bytes
    char modbus49[50];
    memset(modbus49, '0', sizeof(modbus49));
    memcpy(modbus49, "u1/hr100:", 9);
    modbus49[48] = '1';
    modbus49[49] = '\0';
    TEST_ASSERT_TRUE(refused("192.168.2.50", 502, "modbus_tcp", nullptr, nullptr, {modbus49}));
}

void test_parse_and_format_mac(void) {
    uint8_t mac[6];
    TEST_ASSERT_TRUE(parseMac("00:11:22:33:44:5f", mac));
    TEST_ASSERT_EQUAL_UINT8(0x5f, mac[5]);
    TEST_ASSERT_TRUE(parseMac("02:00:00:00:00:01", mac)); // locally administered unicast
    TEST_ASSERT_FALSE(parseMac("ff:ff:ff:ff:ff:ff", mac));
    TEST_ASSERT_FALSE(parseMac("01:00:5e:7f:ff:fa", mac));
    TEST_ASSERT_FALSE(parseMac("33:33:00:00:00:01", mac)); // IPv6 multicast
    TEST_ASSERT_FALSE(parseMac("00:00:00:00:00:00", mac));
    TEST_ASSERT_FALSE(parseMac("00:11:22:33:44:5F", mac));
    TEST_ASSERT_FALSE(parseMac(nullptr, mac));
    char out[MAC_TEXT_BUFFER];
    const uint8_t m[6] = {0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    formatMac(m, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("00:aa:bb:cc:dd:ee", out);
}

// ============================================================================
// Grant blob
// ============================================================================

static void assertEntriesEqual(const GrantEntry &a, const GrantEntry &b) {
    TEST_ASSERT_EQUAL_MEMORY(a.ip, b.ip, 4);
    TEST_ASSERT_EQUAL_UINT16(a.port, b.port);
    TEST_ASSERT_TRUE(a.module == b.module);
    TEST_ASSERT_TRUE(a.kind == b.kind);
    TEST_ASSERT_EQUAL(a.hasMac, b.hasMac);
    TEST_ASSERT_EQUAL(a.wildcard, b.wildcard);
    if (a.hasMac) TEST_ASSERT_EQUAL_MEMORY(a.mac, b.mac, 6);
    TEST_ASSERT_EQUAL_UINT8(a.allowCount, b.allowCount);
    for (size_t i = 0; i < a.allowCount; i++) {
        char ta[GRANT_ALLOW_TEXT_BUFFER], tb[GRANT_ALLOW_TEXT_BUFFER];
        TEST_ASSERT_TRUE(grantAllowText(a, i, ta, sizeof(ta)));
        TEST_ASSERT_TRUE(grantAllowText(b, i, tb, sizeof(tb)));
        TEST_ASSERT_EQUAL_STRING(ta, tb);
    }
}

static size_t fillEight(GrantEntry *entries) {
    uint8_t d[DIGEST_BYTES];
    char h[4][65];
    for (uint8_t i = 0; i < 4; i++) {
        fakeDigest(static_cast<uint8_t>(10 + i), d);
        toHex(d, h[i]);
    }
    char path48[49];
    path48[0] = '/';
    memset(path48 + 1, 'z', 47);
    path48[48] = '\0';
    entries[0] = xwebDeclaration();
    entries[1] = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.*", path48});
    entries[2] = entry("192.168.2.41", 80, "https", nullptr, nullptr, {"*"});
    entries[3] = entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/hr100:2=0..400", "u1/co0:4"});
    entries[4] = entry("192.168.2.61", 4210, "udp", nullptr, "00:11:22:33:44:77", {h[0], h[1], h[2], h[3]});
    entries[5] = entry("192.168.2.60", 47808, "udp", "read", "00:11:22:33:44:88", {h[0]});
    entries[6] = entry("10.20.0.5", 9000, "tcp", nullptr, nullptr, {"*"});
    entries[7] = entry("172.16.5.5", 65535, "tcp", "read", nullptr, {h[3]});
    return 8;
}

void test_grant_blob_round_trip(void) {
    GrantEntry entries[GRANT_MAX_ENTRIES];
    memset(entries, 0, sizeof(entries));
    size_t n = fillEight(entries);
    TEST_ASSERT_EQUAL_size_t(1665, GRANT_BLOB_MAX_BYTES);

    static uint8_t blob[GRANT_BLOB_MAX_BYTES];
    size_t len = serializeGrants(entries, n, blob, sizeof(blob));
    TEST_ASSERT_EQUAL_size_t(1665, len);
    TEST_ASSERT_EQUAL_UINT8(GRANT_BLOB_VERSION, blob[0]);

    GrantEntry back[GRANT_MAX_ENTRIES];
    size_t count = 99;
    TEST_ASSERT_TRUE(deserializeGrants(blob, len, back, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL_size_t(8, count);
    for (size_t i = 0; i < n; i++) assertEntriesEqual(entries[i], back[i]);

    // One entry is 1 + 208 bytes
    TEST_ASSERT_EQUAL_size_t(1 + GRANT_ENTRY_BYTES, serializeGrants(entries, 1, blob, sizeof(blob)));
    TEST_ASSERT_EQUAL_size_t(209, 1 + GRANT_ENTRY_BYTES);
    // Too small a buffer
    TEST_ASSERT_EQUAL_size_t(0, serializeGrants(entries, 8, blob, 1664));
}

void test_grant_blob_corruption_refused(void) {
    GrantEntry entries[GRANT_MAX_ENTRIES];
    memset(entries, 0, sizeof(entries));
    size_t n = fillEight(entries);
    static uint8_t blob[GRANT_BLOB_MAX_BYTES];
    size_t len = serializeGrants(entries, n, blob, sizeof(blob));
    TEST_ASSERT_EQUAL_size_t(1665, len);
    GrantEntry back[GRANT_MAX_ENTRIES];
    size_t count;

    static uint8_t bad[GRANT_BLOB_MAX_BYTES];
    memcpy(bad, blob, len);
    bad[0] = static_cast<uint8_t>(GRANT_BLOB_VERSION + 1);
    count = 99;
    TEST_ASSERT_FALSE(deserializeGrants(bad, len, back, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);

    // Not a whole number of entries
    count = 99;
    TEST_ASSERT_FALSE(deserializeGrants(blob, len - 1, back, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);

    // Public IP in the third entry (IPv4 is the first field of an entry)
    memcpy(bad, blob, len);
    bad[1 + 2 * GRANT_ENTRY_BYTES] = 8;
    count = 99;
    TEST_ASSERT_FALSE(deserializeGrants(bad, len, back, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);

    // Unknown module id (after ip 4 B and port 2 B)
    memcpy(bad, blob, len);
    bad[1 + 4 + 2] = 99;
    count = 99;
    TEST_ASSERT_FALSE(deserializeGrants(bad, len, back, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);

    // Fewer output slots than entries
    count = 99;
    TEST_ASSERT_FALSE(deserializeGrants(blob, len, back, 7, &count));
    TEST_ASSERT_EQUAL_size_t(0, count);
}

// ============================================================================
// Classification
// ============================================================================

void test_classify_http_get(void) {
    GrantEntry decl = xwebDeclaration();
    int idx = 7;
    PolicyRequest r = httpReq("GET", "/status");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::READ);
    // access: "read" never lowers, and is harmless on a read
    r = httpReq("GET", "/status", Access::READ);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::READ);
    // GET with a body is write
    r = httpReq("GET", "/rpc", Access::NONE, true);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::WRITE);
    r = httpReq("GET", "/rpc", Access::READ, true);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::WRITE);
    // GET raised to write
    r = httpReq("GET", "/rpc/Switch.Set?id=0&on=false", Access::WRITE);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::WRITE);
    // A GET can reach https too
    r = httpReq("GET", "/", Access::NONE, false, 443, Module::HTTPS);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::READ);
}

void test_classify_http_declared_post(void) {
    GrantEntry decl = xwebDeclaration();
    int idx = -5;
    PolicyRequest r = httpReq("POST", "/cgi-bin/datachart.cgi");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    TEST_ASSERT_EQUAL_INT(0, idx);
    r = httpReq("POST", "/cgi-bin/login.lua");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    // Body is not part of the match
    r = httpReq("POST", "/cgi-bin/datachart.cgi", Access::NONE, true);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    // Query outside the match
    r = httpReq("POST", "/cgi-bin/datachart.cgi?lang=en");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    // access: "read" accepted
    r = httpReq("POST", "/cgi-bin/datachart.cgi", Access::READ);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::READ);
    // access: "write" raises it
    r = httpReq("POST", "/cgi-bin/datachart.cgi", Access::WRITE);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    // Declaration found at a later index
    GrantEntry list[2] = {entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"*"}), decl};
    r = httpReq("POST", "/cgi-bin/datachart.cgi");
    TEST_ASSERT_TRUE(classify(r, list, 2, &idx) == Verdict::READ);
    TEST_ASSERT_EQUAL_INT(1, idx);
}

void test_classify_http_undeclared_is_write(void) {
    GrantEntry decl = xwebDeclaration();
    int idx = 7;
    // Undeclared path, even claiming read
    PolicyRequest r = httpReq("POST", "/cgi-bin/setpoint.cgi", Access::READ);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    TEST_ASSERT_EQUAL_INT(-1, idx);
    // No declaration at all
    r = httpReq("POST", "/cgi-bin/datachart.cgi");
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::WRITE);
    // Another method to a declared path
    const char *methods[] = {"DELETE", "PUT", "PATCH"};
    for (const char *m : methods) {
        r = httpReq(m, "/cgi-bin/datachart.cgi");
        TEST_ASSERT_TRUE_MESSAGE(classify(r, &decl, 1, &idx) == Verdict::WRITE, m);
    }
    // Another port, module or ip
    r = httpReq("POST", "/cgi-bin/datachart.cgi", Access::NONE, false, 8080);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    r = httpReq("POST", "/cgi-bin/datachart.cgi", Access::NONE, false, 80, Module::HTTPS);
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    r = httpReq("POST", "/cgi-bin/datachart.cgi");
    r.ip[3] = 41;
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    // A prefix of the declared path, or a longer path, is not the declared address
    r = httpReq("POST", "/cgi-bin/datachart");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    r = httpReq("POST", "/cgi-bin/datachart.cgi/x");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) == Verdict::WRITE);
    // A write entry never classes a request read
    GrantEntry grant = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/cgi-bin/datachart.cgi"});
    r = httpReq("POST", "/cgi-bin/datachart.cgi");
    TEST_ASSERT_TRUE(classify(r, &grant, 1, &idx) == Verdict::WRITE);
}

// The executor refuses unsafe non-GET paths before classify() runs; here the
// matchers must still never let an escape attempt hit a declaration or prefix.
void test_classify_http_escape_never_matches(void) {
    GrantEntry decl = xwebDeclaration();
    int idx;
    PolicyRequest r = httpReq("POST", "/cgi-bin/datachart.cgi/../setpoint.cgi");
    TEST_ASSERT_TRUE(classify(r, &decl, 1, &idx) != Verdict::READ);
    GrantEntry prefix = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.*"});
    // Spec scenario paths are refused by the path rule the executor applies
    TEST_ASSERT_FALSE(isSafeGrantPath("/rpc/Switch./../Sys.Reboot"));
    TEST_ASSERT_FALSE(isSafeGrantPath("/rpc/Switch.%2e%2e/Sys.Reboot"));
    r = httpReq("POST", "/rpc/Sys.Reboot");
    TEST_ASSERT_TRUE(matchWriteGrant(r, &prefix, 1, &idx) == GrantMatch::NOT_ALLOWED);
    // A GET with a body is classed write; the matcher refuses its unsafe path too
    r = httpReq("GET", "/rpc/Switch./../Sys.Reboot", Access::NONE, true);
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(matchWriteGrant(r, &prefix, 1, &idx) == GrantMatch::NOT_ALLOWED);
    // A plain GET is read whatever its path
    r = httpReq("GET", "/a/../b");
    TEST_ASSERT_TRUE(classify(r, nullptr, 0, &idx) == Verdict::READ);
}

void test_classify_modbus(void) {
    int idx;
    const uint16_t one[] = {215};
    TEST_ASSERT_TRUE(classify(modbusReq(1, "u1/co0", nullptr, 0), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(modbusReq(2, "u1/di0", nullptr, 0), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(modbusReq(3, "u3/hr3000", nullptr, 0), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(modbusReq(4, "u1/ir10", nullptr, 0), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(modbusReq(3, "u3/hr3000", nullptr, 0, Access::READ), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(modbusReq(5, "u1/co0", one, 1), nullptr, 0, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(modbusReq(6, "u1/hr100", one, 1), nullptr, 0, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(modbusReq(15, "u1/co0", one, 1), nullptr, 0, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(modbusReq(16, "u1/hr100", one, 1, Access::WRITE), nullptr, 0, &idx) == Verdict::WRITE);
    // Contradictions both ways
    TEST_ASSERT_TRUE(classify(modbusReq(6, "u1/hr100", one, 1, Access::READ), nullptr, 0, &idx) == Verdict::BAD_ARGS);
    TEST_ASSERT_TRUE(classify(modbusReq(3, "u1/hr100", nullptr, 0, Access::WRITE), nullptr, 0, &idx) == Verdict::BAD_ARGS);
}

void test_classify_raw_payloads(void) {
    uint8_t declared[DIGEST_BYTES], other[DIGEST_BYTES];
    fakeDigest(3, declared);
    memcpy(other, declared, DIGEST_BYTES);
    other[16] ^= 0x01; // the same frame with one byte changed hashes differently
    char hex[65];
    toHex(declared, hex);
    GrantEntry decls[2] = {entry("192.168.2.60", 47808, "udp", "read", "00:11:22:33:44:88", {hex}),
                           entry("192.168.2.60", 4000, "tcp", "read", "00:11:22:33:44:88", {hex})};
    int idx = 7;

    // Only an empty tcp payload is a banner read; every udp send needs a declared hash
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 0, nullptr), decls, 2, &idx) == Verdict::WRITE);
    // Empty tcp payload: banner read, no declaration needed
    TEST_ASSERT_TRUE(classify(rawReq(Module::TCP, 4000, 0, nullptr), nullptr, 0, &idx) == Verdict::READ);
    TEST_ASSERT_TRUE(classify(rawReq(Module::TCP, 4000, 0, nullptr, Access::WRITE), nullptr, 0, &idx) == Verdict::WRITE);

    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 17, declared), decls, 2, &idx) == Verdict::READ);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 17, other), decls, 2, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 17, declared, Access::WRITE), decls, 2, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47809, 17, declared), decls, 2, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 4000, 17, declared), decls, 2, &idx) == Verdict::WRITE); // tcp decl
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 17, declared), nullptr, 0, &idx) == Verdict::WRITE);

    TEST_ASSERT_TRUE(classify(rawReq(Module::TCP, 4000, 12, declared), decls, 2, &idx) == Verdict::READ);
    TEST_ASSERT_EQUAL_INT(1, idx);
    TEST_ASSERT_TRUE(classify(rawReq(Module::TCP, 4000, 12, other), decls, 2, &idx) == Verdict::WRITE);
    TEST_ASSERT_TRUE(classify(rawReq(Module::TCP, 47808, 12, declared), decls, 2, &idx) == Verdict::WRITE); // udp decl

    // A write entry holding the same hash does not class it read
    GrantEntry grant = entry("192.168.2.60", 47808, "udp", nullptr, "00:11:22:33:44:88", {hex});
    TEST_ASSERT_TRUE(classify(rawReq(Module::UDP, 47808, 17, declared), &grant, 1, &idx) == Verdict::WRITE);
}

// ============================================================================
// Write grant matching
// ============================================================================

void test_match_modbus_range(void) {
    GrantEntry g = entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/hr100:2=0..400"});
    int idx = 7;
    const uint16_t inside[] = {215, 300};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr100", inside, 2), &g, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_EQUAL_INT(0, idx);
    const uint16_t bounds[] = {0, 400};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr100", bounds, 2), &g, 1, &idx) == GrantMatch::ALLOWED);
    const uint16_t one[] = {400};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr101", one, 1), &g, 1, &idx) == GrantMatch::ALLOWED);
    // Crosses the end of the range
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr101", inside, 2), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr99", one, 1), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr102", one, 1), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    // Value outside bounds
    const uint16_t high[] = {500};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr100", high, 1), &g, 1, &idx) == GrantMatch::OUT_OF_RANGE);
    const uint16_t mixed[] = {215, 401};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr100", mixed, 2), &g, 1, &idx) == GrantMatch::OUT_OF_RANGE);
    // Another unit, port or table
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u2/hr100", one, 1), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr100", one, 1, Access::NONE, 503), &g, 1, &idx) ==
                     GrantMatch::NOT_ALLOWED);
    const uint16_t coil[] = {1};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(5, "u1/co100", coil, 1), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    // No grants
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr100", one, 1), nullptr, 0, &idx) == GrantMatch::NOT_ALLOWED);
}

void test_match_modbus_unbounded_and_coils(void) {
    GrantEntry list[2] = {entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/co0:4"}),
                          entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/hr0:10"})};
    int idx;
    const uint16_t on[] = {1, 0, 1};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(15, "u1/co1", on, 3), list, 2, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(15, "u1/co2", on, 3), list, 2, &idx) == GrantMatch::NOT_ALLOWED);
    // Unbounded register range admits any raw value
    const uint16_t big[] = {65535};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(6, "u1/hr9", big, 1), list, 2, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_EQUAL_INT(1, idx);
    // Coils are always 0-1, whatever bounds the entry states
    const uint16_t two[] = {2};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(5, "u1/co0", two, 1), list, 2, &idx) == GrantMatch::OUT_OF_RANGE);
    // Every written address must fall inside ONE range: two adjacent ranges do not join
    GrantEntry split = entry("192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", {"u1/hr100:2", "u1/hr102:2"});
    const uint16_t pair[] = {1, 2};
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr101", pair, 2), &split, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(modbusReq(16, "u1/hr102", pair, 2), &split, 1, &idx) == GrantMatch::ALLOWED);
    // Coil bounds above 1 are refused, not clamped, so the stored text is what is enforced
    const char *coilAllow[] = {"u1/co0:4=0..2"};
    GrantInput coilInput = {"192.168.2.50", 502, "modbus_tcp", nullptr, "00:11:22:33:44:66", coilAllow, 1};
    GrantEntry refused;
    TEST_ASSERT_NOT_NULL(validateGrant(coilInput, &refused));
}

void test_match_http_paths(void) {
    GrantEntry prefix = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.*"});
    int idx;
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Set?id=0&on=true"), &prefix, 1, &idx) ==
                     GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch."), &prefix, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Sys.Reboot"), &prefix, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch"), &prefix, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("GET", "/rpc/Switch.Set", Access::WRITE), &prefix, 1, &idx) ==
                     GrantMatch::ALLOWED);
    // Another port or module
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Set", Access::NONE, false, 8080), &prefix, 1, &idx) ==
                     GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Set", Access::NONE, false, 80, Module::HTTPS), &prefix,
                                     1, &idx) == GrantMatch::NOT_ALLOWED);

    GrantEntry exact = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.Set"});
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Set"), &exact, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Set?x=1"), &exact, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.SetX"), &exact, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/rpc/Switch.Se"), &exact, 1, &idx) == GrantMatch::NOT_ALLOWED);

    GrantEntry all = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"*"});
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("DELETE", "/anything/at/all"), &all, 1, &idx) == GrantMatch::ALLOWED);

    // A read declaration never admits a write
    GrantEntry decl = xwebDeclaration();
    TEST_ASSERT_TRUE(matchWriteGrant(httpReq("POST", "/cgi-bin/datachart.cgi", Access::WRITE), &decl, 1, &idx) ==
                     GrantMatch::NOT_ALLOWED);
}

void test_match_raw_payloads(void) {
    uint8_t granted[DIGEST_BYTES], other[DIGEST_BYTES];
    fakeDigest(4, granted);
    memcpy(other, granted, DIGEST_BYTES);
    other[0] ^= 0x80;
    char hex[65];
    toHex(granted, hex);
    GrantEntry g = entry("192.168.2.60", 4210, "udp", nullptr, "00:11:22:33:44:99", {hex});
    int idx;
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::UDP, 4210, 12, granted), &g, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::UDP, 4210, 12, other), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::TCP, 4210, 12, granted), &g, 1, &idx) == GrantMatch::NOT_ALLOWED);

    GrantEntry all = entry("192.168.2.60", 4210, "udp", nullptr, "00:11:22:33:44:99", {"*"});
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::UDP, 4210, 12, other), &all, 1, &idx) == GrantMatch::ALLOWED);
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::UDP, 4211, 12, other), &all, 1, &idx) == GrantMatch::NOT_ALLOWED);

    // A read declaration with the same hash does not admit a write
    GrantEntry decl = entry("192.168.2.60", 4210, "udp", "read", "00:11:22:33:44:99", {hex});
    TEST_ASSERT_TRUE(matchWriteGrant(rawReq(Module::UDP, 4210, 12, granted, Access::WRITE), &decl, 1, &idx) ==
                     GrantMatch::NOT_ALLOWED);
}

// ============================================================================
// MAC binding and digests
// ============================================================================

void test_on_link(void) {
    const uint32_t self = hostIp(192, 168, 2, 10);
    const uint32_t mask24 = hostIp(255, 255, 255, 0);
    TEST_ASSERT_TRUE(isOnLink(hostIp(192, 168, 2, 50), self, mask24));
    TEST_ASSERT_FALSE(isOnLink(hostIp(192, 168, 3, 50), self, mask24));
    TEST_ASSERT_FALSE(isOnLink(hostIp(10, 20, 0, 5), self, mask24));
    TEST_ASSERT_TRUE(isOnLink(hostIp(192, 168, 3, 50), self, hostIp(255, 255, 252, 0)));
    TEST_ASSERT_FALSE(isOnLink(hostIp(192, 168, 2, 200), self, hostIp(255, 255, 255, 128)));
}

void test_mac_check_decisions(void) {
    TEST_ASSERT_TRUE(macCheckFor(true, true) == MacCheck::COMPARE);
    TEST_ASSERT_TRUE(macCheckFor(false, true) == MacCheck::MAC_REQUIRED);
    TEST_ASSERT_TRUE(macCheckFor(false, false) == MacCheck::NONE);
    TEST_ASSERT_TRUE(macCheckFor(true, false) == MacCheck::TARGET_MAC_CHANGED);
}

void test_hex_digest(void) {
    uint8_t d[DIGEST_BYTES], back[DIGEST_BYTES];
    fakeDigest(5, d);
    char hex[65];
    toHex(d, hex);
    TEST_ASSERT_TRUE(parseHexDigest(hex, back));
    TEST_ASSERT_EQUAL_MEMORY(d, back, DIGEST_BYTES);
    char out[65];
    formatHexDigest(d, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(hex, out);

    char bad[70];
    memcpy(bad, hex, 65);
    bad[63] = '\0'; // 63 hex
    TEST_ASSERT_FALSE(parseHexDigest(bad, back));
    memcpy(bad, hex, 64);
    bad[64] = 'a';
    bad[65] = '\0'; // 65 hex
    TEST_ASSERT_FALSE(parseHexDigest(bad, back));
    memcpy(bad, hex, 65);
    bad[10] = 'A';
    TEST_ASSERT_FALSE(parseHexDigest(bad, back));
    bad[10] = 'g';
    TEST_ASSERT_FALSE(parseHexDigest(bad, back));
    bad[10] = ' ';
    TEST_ASSERT_FALSE(parseHexDigest(bad, back));
    TEST_ASSERT_FALSE(parseHexDigest("", back));
    TEST_ASSERT_FALSE(parseHexDigest(nullptr, back));
}

void test_digest_equals(void) {
    uint8_t a[DIGEST_BYTES], b[DIGEST_BYTES];
    fakeDigest(6, a);
    memcpy(b, a, DIGEST_BYTES);
    TEST_ASSERT_TRUE(digestEquals(a, b));
    b[0] ^= 1;
    TEST_ASSERT_FALSE(digestEquals(a, b));
    b[0] ^= 1;
    b[31] ^= 0x40;
    TEST_ASSERT_FALSE(digestEquals(a, b));
}

// Review findings: underscore override headers, matrix-parameter dot segments,
// a wildcard flag that would widen a stored path entry.
void test_review_hardening(void) {
    TEST_ASSERT_TRUE(isReservedHeaderName("X_HTTP_Method_Override"));
    TEST_ASSERT_TRUE(isReservedHeaderName("x_method_override"));
    TEST_ASSERT_FALSE(isReservedHeaderName("X_Custom_Header"));
    TEST_ASSERT_FALSE(isSafeGrantPath("/rpc/Switch.x/..;/Sys.Reboot"));
    TEST_ASSERT_FALSE(isSafeGrantPath("/a/.;x/b"));
    TEST_ASSERT_TRUE(isSafeGrantPath("/a/b;v=1"));

    GrantEntry path = entry("192.168.2.40", 80, "http", nullptr, "00:11:22:33:44:55", {"/rpc/Switch.Set"});
    uint8_t blob[GRANT_BLOB_MAX_BYTES];
    size_t length = serializeGrants(&path, 1, blob, sizeof(blob));
    TEST_ASSERT_EQUAL(1 + GRANT_ENTRY_BYTES, length);
    blob[1 + 8] |= 2; // set the wildcard flag on a path entry
    GrantEntry out[GRANT_MAX_ENTRIES];
    size_t count = 9;
    TEST_ASSERT_FALSE(deserializeGrants(blob, length, out, GRANT_MAX_ENTRIES, &count));
    TEST_ASSERT_EQUAL(0, count);
}

void run_gateway_policy_tests(void) {
    RUN_TEST(test_review_hardening);
    RUN_TEST(test_parse_new_ops);
    RUN_TEST(test_method_override_headers_reserved);
    RUN_TEST(test_parse_module);
    RUN_TEST(test_modbus_address_round_trip);
    RUN_TEST(test_modbus_address_malformed);
    RUN_TEST(test_modbus_format_small_cap);
    RUN_TEST(test_modbus_function_codes);
    RUN_TEST(test_modbus_table_for_function);
    RUN_TEST(test_modbus_range_fits);
    RUN_TEST(test_http_address_drops_query);
    RUN_TEST(test_safe_grant_path);
    RUN_TEST(test_grant_valid_entries);
    RUN_TEST(test_grant_invalid_entries);
    RUN_TEST(test_grant_48_byte_boundary);
    RUN_TEST(test_parse_and_format_mac);
    RUN_TEST(test_grant_blob_round_trip);
    RUN_TEST(test_grant_blob_corruption_refused);
    RUN_TEST(test_classify_http_get);
    RUN_TEST(test_classify_http_declared_post);
    RUN_TEST(test_classify_http_undeclared_is_write);
    RUN_TEST(test_classify_http_escape_never_matches);
    RUN_TEST(test_classify_modbus);
    RUN_TEST(test_classify_raw_payloads);
    RUN_TEST(test_match_modbus_range);
    RUN_TEST(test_match_modbus_unbounded_and_coils);
    RUN_TEST(test_match_http_paths);
    RUN_TEST(test_match_raw_payloads);
    RUN_TEST(test_on_link);
    RUN_TEST(test_mac_check_decisions);
    RUN_TEST(test_hex_digest);
    RUN_TEST(test_digest_equals);
}
