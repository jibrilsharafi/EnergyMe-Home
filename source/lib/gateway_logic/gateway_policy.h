// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure logic for the gateway executor (openspec capability: gateway-modules):
// canonical addresses, read/write classification, write grants and read
// declarations, target MAC binding decisions. Sockets, ARP lookups, SHA-256 and
// JSON live in the firmware; everything here is host-testable.

namespace GatewayLogic {

// Ids are persisted in the grant blob: never renumber.
enum class Module : uint8_t { HTTP = 1, HTTPS = 2, MODBUS_TCP = 3, TCP = 4, UDP = 5 };
bool parseModule(const char *text, Module *out);
const char *moduleName(Module module);

enum class Access : uint8_t { NONE, READ, WRITE };

inline constexpr size_t DIGEST_BYTES = 32;

// ---------------------------------------------------------------------------
// Canonical addresses
// ---------------------------------------------------------------------------

enum class ModbusTable : uint8_t { COILS, DISCRETE_INPUTS, INPUT_REGISTERS, HOLDING_REGISTERS };

struct ModbusAddress {
    uint8_t unit;
    ModbusTable table;
    uint16_t address;
};

inline constexpr size_t MODBUS_ADDRESS_BUFFER = 16; // "u255/hr65535" + NUL

// `u<unit>/<table><addr>`: unit 0-255, table co|di|ir|hr, zero-based decimal
// address 0-65535, no leading zeros, lowercase only.
bool parseModbusAddress(const char *text, ModbusAddress *out);
bool formatModbusAddress(const ModbusAddress &address, char *out, size_t cap);

bool isWritableTable(ModbusTable table); // co, hr
bool tableForFunction(uint8_t fc, ModbusTable *out);
// Read: 1-4 by table. Write: 5/6 for one value, 15/16 for several or when
// `writeMultiple`. 0 when the table cannot be written.
uint8_t functionFor(ModbusTable table, bool write, size_t count, bool writeMultiple);
bool modbusRangeFits(uint16_t address, uint32_t count); // count >= 1 and address + count <= 65536

// `<METHOD> <path before ?>`; false when it does not fit.
bool formatHttpAddress(const char *method, const char *path, char *out, size_t cap);

// Length of the path before its query.
size_t httpPathLength(const char *path);

// ---------------------------------------------------------------------------
// Write grants and read declarations
// ---------------------------------------------------------------------------

enum class GrantKind : uint8_t { WRITE = 0, READ = 1 };

inline constexpr size_t GRANT_MAX_ENTRIES = 8;
inline constexpr size_t GRANT_ALLOW_MAX = 4;
inline constexpr size_t GRANT_ALLOW_SLOT_BYTES = 48;
inline constexpr size_t GRANT_ENTRY_BYTES = 208;
inline constexpr uint8_t GRANT_BLOB_VERSION = 1;
inline constexpr size_t GRANT_BLOB_MAX_BYTES = 1 + GRANT_MAX_ENTRIES * GRANT_ENTRY_BYTES; // 1665
inline constexpr size_t GRANT_JSON_MAX_BYTES = 3072;
inline constexpr size_t GRANT_ALLOW_TEXT_BUFFER = 65; // 64-hex digest + NUL
inline constexpr size_t MAC_TEXT_BUFFER = 18;

struct GrantEntry {
    uint8_t ip[4];
    uint16_t port;
    Module module;
    GrantKind kind;
    bool hasMac;
    bool wildcard; // allow: ["*"]
    uint8_t mac[6];
    uint8_t allowCount;
    // Path or Modbus range text (NUL-padded, not terminated at 48 bytes), or a
    // 32-byte SHA-256 for tcp/udp.
    uint8_t allow[GRANT_ALLOW_MAX][GRANT_ALLOW_SLOT_BYTES];
};

// One entry as it arrives from JSON. Strings may be null when absent.
struct GrantInput {
    const char *ip;
    int64_t port;
    const char *module;
    const char *kind; // null -> "write"
    const char *mac;  // null -> none
    const char *const *allow;
    size_t allowCount;
};

// nullptr when valid (and `out` filled), else a short reason for logs and the
// REST 400 body.
const char *validateGrant(const GrantInput &input, GrantEntry *out);

bool parseMac(const char *text, uint8_t out[6]); // lowercase aa:bb:..; refuses zero, broadcast, multicast
void formatMac(const uint8_t mac[6], char *out, size_t cap);

// Text form of allow slot `i` ("*", a path, a range or 64-hex); false if out of range.
bool grantAllowText(const GrantEntry &entry, size_t i, char *out, size_t cap);

// Version byte + count fixed-size entries. Returns the blob length, 0 on error.
size_t serializeGrants(const GrantEntry *entries, size_t count, uint8_t *out, size_t cap);
// False on a wrong version, a length that is not a whole number of entries, or
// any entry that fails re-validation; `count` is then 0.
bool deserializeGrants(const uint8_t *blob, size_t length, GrantEntry *out, size_t cap, size_t *count);

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

struct PolicyRequest {
    Module module;
    uint8_t ip[4];
    uint16_t port;
    Access access;
    // http/https: method and the expanded path (query ignored by the matcher)
    const char *method;
    const char *path;
    bool hasBody; // non-empty body or body_b64
    // modbus_tcp
    uint8_t function;
    ModbusAddress modbus;
    const uint16_t *values;
    size_t valueCount;
    // tcp/udp: payload length and its SHA-256 (null when empty)
    size_t payloadLength;
    const uint8_t *payloadDigest;
};

enum class Verdict { READ, WRITE, BAD_ARGS };

// Classes the request from the module rules and the stored read declarations.
// `readEntry` gets the index of the declaration that classed it read, -1 if none.
Verdict classify(const PolicyRequest &request, const GrantEntry *entries, size_t count, int *readEntry);

enum class GrantMatch { ALLOWED, NOT_ALLOWED, OUT_OF_RANGE };

// For a request classed write: the first `write` entry that admits it.
GrantMatch matchWriteGrant(const PolicyRequest &request, const GrantEntry *entries, size_t count, int *entryIndex);

// ---------------------------------------------------------------------------
// Target MAC binding
// ---------------------------------------------------------------------------

// Host-order addresses.
bool isOnLink(uint32_t target, uint32_t interfaceAddress, uint32_t netmask);

enum class MacCheck {
    NONE,              // routed target, entry without mac
    COMPARE,           // compare the ARP entry after connect, before the first byte
    MAC_REQUIRED,      // on-link target, entry without mac
    TARGET_MAC_CHANGED // entry with mac, target off-link now
};

MacCheck macCheckFor(bool entryHasMac, bool onLink);

// ---------------------------------------------------------------------------
// Digests
// ---------------------------------------------------------------------------

bool parseHexDigest(const char *text, uint8_t out[DIGEST_BYTES]); // exactly 64 lowercase hex
void formatHexDigest(const uint8_t digest[DIGEST_BYTES], char *out, size_t cap);
bool digestEquals(const uint8_t *a, const uint8_t *b); // constant time over 32 bytes

} // namespace GatewayLogic
