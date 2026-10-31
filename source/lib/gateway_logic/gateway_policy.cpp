// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_policy.h"

#include <cstdio>
#include <cstring>

#include "gateway_logic.h"

namespace GatewayLogic {

namespace {

struct ModuleName {
    const char *name;
    Module module;
};

constexpr ModuleName MODULES[] = {
    {"http", Module::HTTP}, {"https", Module::HTTPS}, {"modbus_tcp", Module::MODBUS_TCP}, {"tcp", Module::TCP}, {"udp", Module::UDP},
};

constexpr const char *TABLE_NAMES[] = {"co", "di", "ir", "hr"};

bool isDigit(char c) { return c >= '0' && c <= '9'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Decimal without sign or leading zeros, at most `maxDigits`. Advances `*p`.
bool parseDecimal(const char **p, uint32_t maxValue, uint32_t *out) {
    const char *s = *p;
    if (!isDigit(*s)) return false;
    if (*s == '0' && isDigit(s[1])) return false;
    uint32_t value = 0;
    int digits = 0;
    while (isDigit(*s)) {
        if (++digits > 5) return false;
        value = value * 10 + static_cast<uint32_t>(*s - '0');
        s++;
    }
    if (value > maxValue) return false;
    *p = s;
    *out = value;
    return true;
}

bool parseTable(const char **p, ModbusTable *out) {
    for (size_t i = 0; i < 4; i++) {
        if (strncmp(*p, TABLE_NAMES[i], 2) == 0) {
            *out = static_cast<ModbusTable>(i);
            *p += 2;
            return true;
        }
    }
    return false;
}

// `u<unit>/<table><addr>` prefix; advances `*p`.
bool parseAddressPrefix(const char **p, ModbusAddress *out) {
    const char *s = *p;
    uint32_t unit = 0, address = 0;
    if (*s++ != 'u' || !parseDecimal(&s, 255, &unit) || *s++ != '/' || !parseTable(&s, &out->table) ||
        !parseDecimal(&s, 65535, &address)) {
        return false;
    }
    out->unit = static_cast<uint8_t>(unit);
    out->address = static_cast<uint16_t>(address);
    *p = s;
    return true;
}

struct ModbusRange {
    ModbusAddress start;
    uint32_t count;
    uint16_t min;
    uint16_t max;
};

// `u<unit>/<co|hr><start>:<count>[=<min>..<max>]`
bool parseModbusRange(const char *text, ModbusRange *out) {
    const char *s = text;
    uint32_t count = 0, min = 0, max = 65535;
    if (!parseAddressPrefix(&s, &out->start) || !isWritableTable(out->start.table) || *s++ != ':' ||
        !parseDecimal(&s, 65536, &count) || !modbusRangeFits(out->start.address, count)) {
        return false;
    }
    if (out->start.table == ModbusTable::COILS) max = 1;
    if (*s == '=') {
        s++;
        if (!parseDecimal(&s, 65535, &min) || s[0] != '.' || s[1] != '.') return false;
        s += 2;
        if (!parseDecimal(&s, 65535, &max) || min > max) return false;
    }
    if (*s != '\0') return false;
    if (out->start.table == ModbusTable::COILS && max > 1) return false; // coils are 0-1
    out->count = count;
    out->min = static_cast<uint16_t>(min);
    out->max = static_cast<uint16_t>(max);
    return true;
}

bool isHexLower(char c) { return isDigit(c) || (c >= 'a' && c <= 'f'); }

uint8_t hexValue(char c) { return isDigit(c) ? static_cast<uint8_t>(c - '0') : static_cast<uint8_t>(c - 'a' + 10); }

bool isRawModule(Module m) { return m == Module::TCP || m == Module::UDP; }
bool isHttpModule(Module m) { return m == Module::HTTP || m == Module::HTTPS; }

const char *const READ_METHODS[] = {"POST", "PUT", "PATCH", "DELETE"};

bool isReadMethod(const char *method, size_t length) {
    for (const char *m : READ_METHODS) {
        if (strlen(m) == length && strncmp(m, method, length) == 0) return true;
    }
    return false;
}

size_t slotLength(const uint8_t *slot) {
    size_t n = 0;
    while (n < GRANT_ALLOW_SLOT_BYTES && slot[n] != 0) n++;
    return n;
}

bool sameTarget(const GrantEntry &e, const PolicyRequest &r) {
    return memcmp(e.ip, r.ip, 4) == 0 && e.port == r.port && e.module == r.module;
}

// Path rules for allow strings and request paths: valid, safe, no query.
bool isValidAllowPath(const char *path, size_t length, bool allowStar) {
    char buffer[GRANT_ALLOW_SLOT_BYTES + 1];
    if (length == 0 || length > GRANT_ALLOW_SLOT_BYTES) return false;
    memcpy(buffer, path, length);
    buffer[length] = '\0';
    if (!isValidHttpPath(buffer) || !isSafeGrantPath(buffer) || strchr(buffer, '?') != nullptr) return false;
    const char *star = strchr(buffer, '*');
    if (star == nullptr) return true;
    return allowStar && star == buffer + length - 1;
}

bool slotMatchesPath(const uint8_t *slot, const char *path, size_t pathLength) {
    size_t length = slotLength(slot);
    if (length > 0 && slot[length - 1] == '*') {
        return pathLength >= length - 1 && memcmp(slot, path, length - 1) == 0;
    }
    return length == pathLength && memcmp(slot, path, length) == 0;
}

bool slotMatchesRead(const uint8_t *slot, const char *method, const char *path, size_t pathLength) {
    size_t length = slotLength(slot);
    size_t methodLength = strlen(method);
    return length == methodLength + 1 + pathLength && memcmp(slot, method, methodLength) == 0 && slot[methodLength] == ' ' &&
           memcmp(slot + methodLength + 1, path, pathLength) == 0;
}

} // namespace

bool parseModule(const char *text, Module *out) {
    if (text == nullptr) return false;
    for (const auto &m : MODULES) {
        if (strcmp(text, m.name) == 0) {
            *out = m.module;
            return true;
        }
    }
    return false;
}

const char *moduleName(Module module) {
    for (const auto &m : MODULES) {
        if (m.module == module) return m.name;
    }
    return "unknown";
}

bool parseModbusAddress(const char *text, ModbusAddress *out) {
    if (text == nullptr || out == nullptr) return false;
    const char *s = text;
    ModbusAddress parsed{};
    if (!parseAddressPrefix(&s, &parsed) || *s != '\0') return false;
    *out = parsed;
    return true;
}

bool formatModbusAddress(const ModbusAddress &address, char *out, size_t cap) {
    int n = snprintf(out, cap, "u%u/%s%u", address.unit, TABLE_NAMES[static_cast<size_t>(address.table) & 3], address.address);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool isWritableTable(ModbusTable table) { return table == ModbusTable::COILS || table == ModbusTable::HOLDING_REGISTERS; }

bool tableForFunction(uint8_t fc, ModbusTable *out) {
    switch (fc) {
        case 1: case 5: case 15: *out = ModbusTable::COILS; return true;
        case 2: *out = ModbusTable::DISCRETE_INPUTS; return true;
        case 3: case 6: case 16: *out = ModbusTable::HOLDING_REGISTERS; return true;
        case 4: *out = ModbusTable::INPUT_REGISTERS; return true;
        default: return false;
    }
}

uint8_t functionFor(ModbusTable table, bool write, size_t count, bool writeMultiple) {
    if (!write) {
        static constexpr uint8_t READ_FC[] = {1, 2, 4, 3};
        return READ_FC[static_cast<size_t>(table) & 3];
    }
    bool multiple = writeMultiple || count != 1;
    if (table == ModbusTable::COILS) return multiple ? 15 : 5;
    if (table == ModbusTable::HOLDING_REGISTERS) return multiple ? 16 : 6;
    return 0;
}

bool modbusRangeFits(uint16_t address, uint32_t count) { return count >= 1 && static_cast<uint32_t>(address) + count <= 65536; }

size_t httpPathLength(const char *path) {
    if (path == nullptr) return 0;
    const char *query = strchr(path, '?');
    return query != nullptr ? static_cast<size_t>(query - path) : strlen(path);
}

bool formatHttpAddress(const char *method, const char *path, char *out, size_t cap) {
    if (method == nullptr || path == nullptr) return false;
    int n = snprintf(out, cap, "%s %.*s", method, static_cast<int>(httpPathLength(path)), path);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool isSafeGrantPath(const char *path) {
    if (path == nullptr) return false;
    size_t length = httpPathLength(path);
    for (size_t i = 0; i < length; i++) {
        char c = path[i];
        if (c == '\\') return false;
        if (c == '/' && i + 1 < length && path[i + 1] == '/') return false;
        if (c == '%' && i + 2 < length) {
            char a = path[i + 1], b = lower(path[i + 2]);
            if (a == '2' && (b == 'e' || b == 'f')) return false;
            if (a == '5' && b == 'c') return false;
        }
        if (c == '/') {
            // segment starting after this slash
            size_t start = i + 1, end = start;
            while (end < length && path[end] != '/' && path[end] != ';') end++;
            size_t segment = end - start;
            if ((segment == 1 && path[start] == '.') || (segment == 2 && path[start] == '.' && path[start + 1] == '.')) return false;
        }
    }
    return true;
}

bool parseMac(const char *text, uint8_t out[6]) {
    if (text == nullptr || strlen(text) != 17) return false;
    uint8_t mac[6];
    for (size_t i = 0; i < 6; i++) {
        const char *p = text + i * 3;
        if (!isHexLower(p[0]) || !isHexLower(p[1]) || (i < 5 && p[2] != ':')) return false;
        mac[i] = static_cast<uint8_t>((hexValue(p[0]) << 4) | hexValue(p[1]));
    }
    bool zero = true;
    for (uint8_t b : mac) zero = zero && b == 0;
    if (zero || (mac[0] & 0x01) != 0) return false; // all-zero, multicast and broadcast
    memcpy(out, mac, 6);
    return true;
}

void formatMac(const uint8_t mac[6], char *out, size_t cap) {
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool parseHexDigest(const char *text, uint8_t out[DIGEST_BYTES]) {
    if (text == nullptr || strlen(text) != DIGEST_BYTES * 2) return false;
    for (size_t i = 0; i < DIGEST_BYTES * 2; i++) {
        if (!isHexLower(text[i])) return false;
    }
    for (size_t i = 0; i < DIGEST_BYTES; i++) out[i] = static_cast<uint8_t>((hexValue(text[2 * i]) << 4) | hexValue(text[2 * i + 1]));
    return true;
}

void formatHexDigest(const uint8_t digest[DIGEST_BYTES], char *out, size_t cap) {
    if (cap < DIGEST_BYTES * 2 + 1) {
        if (cap > 0) out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < DIGEST_BYTES; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

bool digestEquals(const uint8_t *a, const uint8_t *b) {
    if (a == nullptr || b == nullptr) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < DIGEST_BYTES; i++) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

const char *validateGrant(const GrantInput &input, GrantEntry *out) {
    GrantEntry entry{};
    if (!parseIpv4(input.ip, entry.ip) || !isPrivateIpv4(entry.ip)) return "ip must be a private IPv4 literal";
    if (!isValidPort(input.port)) return "port must be 1-65535";
    entry.port = static_cast<uint16_t>(input.port);
    if (!parseModule(input.module, &entry.module)) return "module must be http, https, modbus_tcp, tcp or udp";
    if (input.kind == nullptr || strcmp(input.kind, "write") == 0) {
        entry.kind = GrantKind::WRITE;
    } else if (strcmp(input.kind, "read") == 0) {
        entry.kind = GrantKind::READ;
    } else {
        return "kind must be write or read";
    }
    if (entry.kind == GrantKind::READ && entry.module == Module::MODBUS_TCP) return "modbus_tcp entries cannot be kind read";
    if (input.mac != nullptr) {
        if (!parseMac(input.mac, entry.mac)) return "mac must be six lowercase hex pairs, unicast and non-zero";
        entry.hasMac = true;
    }
    if (input.allow == nullptr || input.allowCount < 1 || input.allowCount > GRANT_ALLOW_MAX) return "allow must hold 1-4 strings";

    bool star = false;
    for (size_t i = 0; i < input.allowCount; i++) star = star || (input.allow[i] != nullptr && strcmp(input.allow[i], "*") == 0);
    if (star) {
        if (input.allowCount != 1) return "\"*\" must be the only allow string";
        if (entry.kind == GrantKind::READ) return "read entries cannot hold \"*\"";
        if (entry.module == Module::MODBUS_TCP) return "modbus_tcp grants take ranges, not \"*\"";
        entry.wildcard = true;
        entry.allowCount = 1;
        entry.allow[0][0] = '*';
        *out = entry;
        return nullptr;
    }

    for (size_t i = 0; i < input.allowCount; i++) {
        const char *text = input.allow[i];
        if (text == nullptr) return "allow entries must be strings";
        size_t length = strlen(text);
        uint8_t *slot = entry.allow[i];
        if (isRawModule(entry.module)) {
            if (!parseHexDigest(text, slot)) return "tcp/udp allow strings must be 64 lowercase hex (SHA-256)";
            continue;
        }
        if (length > GRANT_ALLOW_SLOT_BYTES) return "allow strings are at most 48 bytes";
        if (entry.module == Module::MODBUS_TCP) {
            ModbusRange range{};
            if (!parseModbusRange(text, &range)) return "modbus_tcp allow must be u<unit>/<co|hr><start>:<count>[=<min>..<max>]";
        } else if (entry.kind == GrantKind::READ) {
            const char *space = strchr(text, ' ');
            if (space == nullptr || !isReadMethod(text, static_cast<size_t>(space - text)) || strchr(space + 1, ' ') != nullptr ||
                !isValidAllowPath(space + 1, length - static_cast<size_t>(space + 1 - text), false)) {
                return "http read declarations must be \"<POST|PUT|PATCH|DELETE> <path>\" without * or ?";
            }
        } else if (!isValidAllowPath(text, length, true)) {
            return "http allow paths must start with /, hold no dot segment, //, backslash, encoded separator or ?, and * only at the end";
        }
        memcpy(slot, text, length);
    }
    entry.allowCount = static_cast<uint8_t>(input.allowCount);
    *out = entry;
    return nullptr;
}

bool grantAllowText(const GrantEntry &entry, size_t i, char *out, size_t cap) {
    if (i >= entry.allowCount || cap == 0) return false;
    if (entry.wildcard) {
        snprintf(out, cap, "*");
        return true;
    }
    if (isRawModule(entry.module)) {
        formatHexDigest(entry.allow[i], out, cap);
        return out[0] != '\0';
    }
    size_t length = slotLength(entry.allow[i]);
    int n = snprintf(out, cap, "%.*s", static_cast<int>(length), reinterpret_cast<const char *>(entry.allow[i]));
    return n >= 0 && static_cast<size_t>(n) < cap;
}

// Entry layout (208 B): ip 4, port 2 (big endian), module 1, kind 1, flags 1
// (bit 0 mac, bit 1 "*"), mac 6, allow count 1, 4 x 48 B allow slots.
size_t serializeGrants(const GrantEntry *entries, size_t count, uint8_t *out, size_t cap) {
    size_t length = 1 + count * GRANT_ENTRY_BYTES;
    if (count > GRANT_MAX_ENTRIES || out == nullptr || cap < length || (count > 0 && entries == nullptr)) return 0;
    memset(out, 0, length);
    out[0] = GRANT_BLOB_VERSION;
    for (size_t i = 0; i < count; i++) {
        const GrantEntry &e = entries[i];
        uint8_t *p = out + 1 + i * GRANT_ENTRY_BYTES;
        memcpy(p, e.ip, 4);
        p[4] = static_cast<uint8_t>(e.port >> 8);
        p[5] = static_cast<uint8_t>(e.port & 0xFF);
        p[6] = static_cast<uint8_t>(e.module);
        p[7] = static_cast<uint8_t>(e.kind);
        p[8] = static_cast<uint8_t>((e.hasMac ? 1 : 0) | (e.wildcard ? 2 : 0));
        memcpy(p + 9, e.mac, 6);
        p[15] = e.allowCount;
        memcpy(p + 16, e.allow, sizeof(e.allow));
    }
    return length;
}

bool deserializeGrants(const uint8_t *blob, size_t length, GrantEntry *out, size_t cap, size_t *count) {
    *count = 0;
    if (blob == nullptr || length < 1 || blob[0] != GRANT_BLOB_VERSION || (length - 1) % GRANT_ENTRY_BYTES != 0) return false;
    size_t n = (length - 1) / GRANT_ENTRY_BYTES;
    if (n > GRANT_MAX_ENTRIES || n > cap) return false;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *p = blob + 1 + i * GRANT_ENTRY_BYTES;
        GrantEntry e{};
        memcpy(e.ip, p, 4);
        e.port = static_cast<uint16_t>((p[4] << 8) | p[5]);
        e.module = static_cast<Module>(p[6]);
        e.kind = static_cast<GrantKind>(p[7]);
        e.hasMac = (p[8] & 1) != 0;
        e.wildcard = (p[8] & 2) != 0;
        memcpy(e.mac, p + 9, 6);
        e.allowCount = p[15];
        memcpy(e.allow, p + 16, sizeof(e.allow));

        // Re-validate through the text form so a corrupted blob never widens a grant.
        char ipText[16], macText[MAC_TEXT_BUFFER];
        char allowText[GRANT_ALLOW_MAX][GRANT_ALLOW_TEXT_BUFFER];
        const char *allow[GRANT_ALLOW_MAX];
        if (e.allowCount < 1 || e.allowCount > GRANT_ALLOW_MAX || (e.wildcard && e.allowCount != 1)) return false;
        if (e.wildcard) {
            // The flag alone must not turn a stored path or digest into a whole-target grant.
            uint8_t rest = 0;
            for (size_t b = 1; b < sizeof(e.allow); b++) rest |= (&e.allow[0][0])[b];
            if (e.allow[0][0] != '*' || rest != 0) return false;
        }
        for (size_t a = 0; a < e.allowCount; a++) {
            if (!grantAllowText(e, a, allowText[a], sizeof(allowText[a]))) return false;
            allow[a] = allowText[a];
        }
        snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", e.ip[0], e.ip[1], e.ip[2], e.ip[3]);
        formatMac(e.mac, macText, sizeof(macText));
        GrantInput input{ipText, e.port, moduleName(e.module), e.kind == GrantKind::READ ? "read" : (e.kind == GrantKind::WRITE ? "write" : "?"),
                         e.hasMac ? macText : nullptr, allow, e.allowCount};
        if (validateGrant(input, &out[i]) != nullptr) return false;
    }
    *count = n;
    return true;
}

Verdict classify(const PolicyRequest &request, const GrantEntry *entries, size_t count, int *readEntry) {
    *readEntry = -1;
    if (request.module == Module::MODBUS_TCP) {
        bool write = isModbusWriteFunction(request.function);
        if (!write && !isModbusReadFunction(request.function)) return Verdict::BAD_ARGS;
        if ((write && request.access == Access::READ) || (!write && request.access == Access::WRITE)) return Verdict::BAD_ARGS;
        return write ? Verdict::WRITE : Verdict::READ;
    }
    if (request.access == Access::WRITE) return Verdict::WRITE;

    if (isHttpModule(request.module)) {
        if (request.method == nullptr || request.path == nullptr) return Verdict::BAD_ARGS;
        if (strcmp(request.method, "GET") == 0) return request.hasBody ? Verdict::WRITE : Verdict::READ;
        size_t pathLength = httpPathLength(request.path);
        for (size_t i = 0; i < count; i++) {
            const GrantEntry &e = entries[i];
            if (e.kind != GrantKind::READ || !sameTarget(e, request)) continue;
            for (size_t a = 0; a < e.allowCount; a++) {
                if (slotMatchesRead(e.allow[a], request.method, request.path, pathLength)) {
                    *readEntry = static_cast<int>(i);
                    return Verdict::READ;
                }
            }
        }
        return Verdict::WRITE;
    }

    if (request.module == Module::TCP && request.payloadLength == 0) return Verdict::READ;
    if (request.payloadDigest == nullptr) return Verdict::WRITE;
    for (size_t i = 0; i < count; i++) {
        const GrantEntry &e = entries[i];
        if (e.kind != GrantKind::READ || !sameTarget(e, request)) continue;
        for (size_t a = 0; a < e.allowCount; a++) {
            if (digestEquals(e.allow[a], request.payloadDigest)) {
                *readEntry = static_cast<int>(i);
                return Verdict::READ;
            }
        }
    }
    return Verdict::WRITE;
}

GrantMatch matchWriteGrant(const PolicyRequest &request, const GrantEntry *entries, size_t count, int *entryIndex) {
    *entryIndex = -1;
    bool outOfRange = false;
    size_t pathLength = isHttpModule(request.module) ? httpPathLength(request.path) : 0;
    for (size_t i = 0; i < count; i++) {
        const GrantEntry &e = entries[i];
        if (e.kind != GrantKind::WRITE || !sameTarget(e, request)) continue;
        if (e.wildcard) {
            *entryIndex = static_cast<int>(i);
            return GrantMatch::ALLOWED;
        }
        for (size_t a = 0; a < e.allowCount; a++) {
            bool admitted = false;
            if (isHttpModule(request.module)) {
                // A path the server may normalize never matches, whatever classed the request write.
                admitted = request.path != nullptr && isSafeGrantPath(request.path) && slotMatchesPath(e.allow[a], request.path, pathLength);
            } else if (isRawModule(request.module)) {
                admitted = request.payloadDigest != nullptr && digestEquals(e.allow[a], request.payloadDigest);
            } else {
                char text[GRANT_ALLOW_SLOT_BYTES + 1];
                size_t length = slotLength(e.allow[a]);
                memcpy(text, e.allow[a], length);
                text[length] = '\0';
                ModbusRange range{};
                if (!parseModbusRange(text, &range) || range.start.unit != request.modbus.unit || range.start.table != request.modbus.table) continue;
                uint32_t first = request.modbus.address, last = first + request.valueCount; // exclusive
                if (request.valueCount == 0 || first < range.start.address || last > range.start.address + range.count) continue;
                bool inBounds = request.values != nullptr;
                for (size_t v = 0; v < request.valueCount && inBounds; v++) {
                    inBounds = request.values[v] >= range.min && request.values[v] <= range.max;
                }
                if (!inBounds) {
                    outOfRange = true;
                    continue;
                }
                admitted = true;
            }
            if (admitted) {
                *entryIndex = static_cast<int>(i);
                return GrantMatch::ALLOWED;
            }
        }
    }
    return outOfRange ? GrantMatch::OUT_OF_RANGE : GrantMatch::NOT_ALLOWED;
}

bool isOnLink(uint32_t target, uint32_t interfaceAddress, uint32_t netmask) {
    return interfaceAddress != 0 && (target & netmask) == (interfaceAddress & netmask);
}

MacCheck macCheckFor(bool entryHasMac, bool onLink) {
    if (entryHasMac) return onLink ? MacCheck::COMPARE : MacCheck::TARGET_MAC_CHANGED;
    return onLink ? MacCheck::MAC_REQUIRED : MacCheck::NONE;
}

} // namespace GatewayLogic
