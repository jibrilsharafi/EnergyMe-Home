// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_logic.h"

#include <cstring>

namespace GatewayLogic {

namespace {

struct OpEntry {
    const char *name;
    Op op;
};

constexpr OpEntry OPS[] = {
    {"ping", Op::PING},
    {"net_scan", Op::NET_SCAN},
    {"http_request", Op::HTTP_REQUEST},
    {"modbus_read", Op::MODBUS_READ},
    {"modbus_write", Op::MODBUS_WRITE},
    {"udp_send", Op::UDP_SEND},
    {"tunnel_open", Op::TUNNEL_OPEN},
    {"tunnel_close", Op::TUNNEL_CLOSE},
    {"tcp_request", Op::TCP_REQUEST},
    {"https_request", Op::HTTPS_REQUEST},
    {"tls_probe", Op::TLS_PROBE},
    {"secret_set", Op::SECRET_SET},
    {"secret_delete", Op::SECRET_DELETE},
};

void putU16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

uint16_t getU16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

bool isBitFunction(uint8_t fc) { return fc == 1 || fc == 2 || fc == 5 || fc == 15; }

} // namespace

Op parseOp(const char *op) {
    if (op == nullptr) return Op::UNKNOWN;
    for (const auto &e : OPS) {
        if (strcmp(op, e.name) == 0) return e.op;
    }
    return Op::UNKNOWN;
}

const char *opName(Op op) {
    for (const auto &e : OPS) {
        if (e.op == op) return e.name;
    }
    return "unknown";
}

bool parseIpv4(const char *text, uint8_t out[4]) {
    if (text == nullptr) return false;
    const char *p = text;
    for (int octet = 0; octet < 4; octet++) {
        if (*p < '0' || *p > '9') return false;
        if (*p == '0' && p[1] >= '0' && p[1] <= '9') return false; // leading zero
        uint32_t value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + static_cast<uint32_t>(*p - '0');
            p++;
        }
        if (value > 255) return false;
        out[octet] = static_cast<uint8_t>(value);
        if (octet < 3) {
            if (*p != '.') return false;
            p++;
        }
    }
    return *p == '\0';
}

bool isPrivateIpv4(const char *text) {
    uint8_t ip[4];
    return parseIpv4(text, ip) && isPrivateIpv4(ip);
}

bool isPrivateIpv4(const uint8_t ip[4]) {
    if (ip[0] == 10) return true;
    if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
    if (ip[0] == 192 && ip[1] == 168) return true;
    return false;
}

uint32_t clampTimeoutMs(int64_t requested) {
    if (requested <= 0) return TIMEOUT_DEFAULT_MS;
    if (requested < TIMEOUT_MIN_MS) return TIMEOUT_MIN_MS;
    if (requested > TIMEOUT_MAX_MS) return TIMEOUT_MAX_MS;
    return static_cast<uint32_t>(requested);
}

uint32_t clampUdpWaitMs(int64_t requested) {
    if (requested <= 0) return 0;
    if (requested > UDP_WAIT_MAX_MS) return UDP_WAIT_MAX_MS;
    return static_cast<uint32_t>(requested);
}

uint32_t clampPingCount(int64_t requested) {
    if (requested <= 0) return PING_COUNT_DEFAULT;
    if (requested > PING_COUNT_MAX) return PING_COUNT_MAX;
    return static_cast<uint32_t>(requested);
}

bool isValidPort(int64_t port) { return port >= 1 && port <= 65535; }

bool isAllowedHttpMethod(const char *method) {
    if (method == nullptr) return false;
    static const char *const METHODS[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};
    for (const char *m : METHODS) {
        if (strcmp(method, m) == 0) return true;
    }
    return false;
}

bool httpMethodHasBody(const char *method) {
    if (method == nullptr) return false;
    return strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0;
}

bool isValidHttpPath(const char *path) {
    if (path == nullptr || path[0] != '/') return false;
    size_t len = strnlen(path, HTTP_PATH_MAX_LEN + 1);
    if (len > HTTP_PATH_MAX_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (c <= 0x20 || c == 0x7F) return false;
    }
    return true;
}

bool isValidHeaderName(const char *name) {
    if (name == nullptr || name[0] == '\0') return false;
    static const char *const SEPARATORS = "()<>@,;:\\\"/[]?={} \t";
    for (const char *p = name; *p; p++) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c <= 0x20 || c >= 0x7F || strchr(SEPARATORS, c) != nullptr) return false;
    }
    return true;
}

bool isValidHeaderValue(const char *value) {
    if (value == nullptr) return false;
    for (const char *p = value; *p; p++) {
        if (*p == '\r' || *p == '\n') return false;
    }
    return true;
}

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        if (lower(*a) != lower(*b)) return false;
    }
    return *a == *b;
}

// Finds `name:` at the start of a header line and returns the value (leading
// spaces skipped) and its length up to CR/LF.
bool findHeader(const uint8_t *buffer, size_t headerLen, const char *name, const char **value, size_t *valueLen) {
    if (buffer == nullptr) return false;
    const char *text = reinterpret_cast<const char *>(buffer);
    size_t nameLen = strlen(name);
    for (size_t i = 0; i + nameLen + 1 <= headerLen; i++) {
        if (i != 0 && text[i - 1] != '\n') continue;
        bool match = true;
        for (size_t k = 0; k < nameLen && match; k++) match = lower(text[i + k]) == lower(name[k]);
        if (!match || text[i + nameLen] != ':') continue;
        size_t v = i + nameLen + 1;
        while (v < headerLen && (text[v] == ' ' || text[v] == '\t')) v++;
        size_t end = v;
        while (end < headerLen && text[end] != '\r' && text[end] != '\n') end++;
        *value = text + v;
        *valueLen = end - v;
        return true;
    }
    return false;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

bool isReservedHeaderName(const char *name) {
    if (name == nullptr) return true;
    static const char *const RESERVED[] = {"host", "content-length", "transfer-encoding", "connection", "expect",
                                           "te", "upgrade", "keep-alive", "proxy-connection", "trailer",
                                           "x-http-method-override", "x-http-method", "x-method-override"};
    // CGI servers map '_' and '-' to the same HTTP_* variable: compare with '_' as '-'.
    char normalized[32];
    size_t length = strlen(name);
    if (length >= sizeof(normalized)) return false;
    for (size_t i = 0; i <= length; i++) normalized[i] = name[i] == '_' ? '-' : name[i];
    for (const char *r : RESERVED) {
        if (equalsIgnoreCase(normalized, r)) return true;
    }
    return false;
}

size_t httpHeaderEnd(const uint8_t *buffer, size_t len) {
    if (buffer == nullptr) return 0;
    for (size_t i = 3; i < len; i++) {
        if (buffer[i - 3] == '\r' && buffer[i - 2] == '\n' && buffer[i - 1] == '\r' && buffer[i] == '\n') return i + 1;
    }
    return 0;
}

bool httpStatusCode(const uint8_t *buffer, size_t headerLen, int *code) {
    if (buffer == nullptr || code == nullptr || headerLen < 12) return false;
    const char *t = reinterpret_cast<const char *>(buffer);
    if (strncmp(t, "HTTP/1.", 7) != 0 || t[8] != ' ') return false;
    int value = 0;
    for (int i = 9; i < 12; i++) {
        if (t[i] < '0' || t[i] > '9') return false;
        value = value * 10 + (t[i] - '0');
    }
    if (value < 100 || (headerLen > 12 && t[12] != ' ' && t[12] != '\r')) return false;
    *code = value;
    return true;
}

int64_t httpContentLength(const uint8_t *buffer, size_t headerLen) {
    const char *value = nullptr;
    size_t valueLen = 0;
    if (!findHeader(buffer, headerLen, "content-length", &value, &valueLen) || valueLen == 0 || valueLen > 9) return -1;
    int64_t result = 0;
    for (size_t i = 0; i < valueLen; i++) {
        if (value[i] < '0' || value[i] > '9') return -1;
        result = result * 10 + (value[i] - '0');
    }
    return result;
}

bool httpIsChunked(const uint8_t *buffer, size_t headerLen) {
    const char *value = nullptr;
    size_t valueLen = 0;
    if (!findHeader(buffer, headerLen, "transfer-encoding", &value, &valueLen)) return false;
    static const char CHUNKED[] = "chunked";
    const size_t chunkedLen = sizeof(CHUNKED) - 1;
    for (size_t i = 0; i + chunkedLen <= valueLen; i++) {
        bool match = true;
        for (size_t k = 0; k < chunkedLen && match; k++) match = lower(value[i + k]) == CHUNKED[k];
        if (match) return true;
    }
    return false;
}

bool httpChunkedEnded(const uint8_t *body, size_t len) {
    static const char LAST[] = "0\r\n\r\n";
    const size_t lastLen = sizeof(LAST) - 1;
    if (body == nullptr || len < lastLen || memcmp(body + len - lastLen, LAST, lastLen) != 0) return false;
    return len == lastLen || body[len - lastLen - 1] == '\n';
}

Chunked decodeChunked(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *outLen) {
    if (in == nullptr || out == nullptr || outLen == nullptr) return Chunked::MALFORMED;
    *outLen = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t size = 0;
        int digits = 0;
        while (pos < len && hexValue(static_cast<char>(in[pos])) >= 0) {
            if (++digits > 7) return Chunked::MALFORMED; // > 256 MB chunk: not a LAN device reply
            size = size * 16 + static_cast<size_t>(hexValue(static_cast<char>(in[pos])));
            pos++;
        }
        if (digits == 0) return pos >= len ? Chunked::INCOMPLETE : Chunked::MALFORMED;
        while (pos < len && in[pos] != '\n') pos++; // skip chunk extensions and CR
        if (pos >= len) return Chunked::INCOMPLETE;
        pos++;
        if (size == 0) return Chunked::COMPLETE;
        size_t available = len - pos < size ? len - pos : size;
        size_t room = cap - *outLen;
        size_t copy = available < room ? available : room;
        memcpy(out + *outLen, in + pos, copy);
        *outLen += copy;
        if (copy < size) return Chunked::INCOMPLETE; // input ended or output full mid-chunk
        pos += size;
        if (pos + 2 > len) return Chunked::INCOMPLETE;
        if (in[pos] != '\r' || in[pos + 1] != '\n') return Chunked::MALFORMED;
        pos += 2;
    }
    return Chunked::INCOMPLETE;
}

bool utf8TextLength(const uint8_t *data, size_t len, bool truncated, size_t *textLen) {
    if (data == nullptr || textLen == nullptr) return false;
    size_t pos = 0;
    while (pos < len) {
        uint8_t lead = data[pos];
        size_t continuation;
        uint8_t low = 0x80, high = 0xBF; // allowed range of the first continuation byte
        if (lead < 0x80) {
            if (lead < 0x20 && lead != '\t' && lead != '\n' && lead != '\r') return false;
            pos++;
            continue;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            continuation = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuation = 2;
            if (lead == 0xE0) low = 0xA0;  // overlong
            if (lead == 0xED) high = 0x9F; // UTF-16 surrogates
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuation = 3;
            if (lead == 0xF0) low = 0x90;  // overlong
            if (lead == 0xF4) high = 0x8F; // above U+10FFFF
        } else {
            return false; // stray continuation byte, C0/C1 overlong or F5-FF
        }
        for (size_t i = 1; i <= continuation; i++) {
            if (pos + i >= len) {
                if (!truncated) return false;
                *textLen = pos; // a valid prefix cut by the cap: drop it
                return pos > 0;
            }
            uint8_t byte = data[pos + i];
            if (byte < (i == 1 ? low : 0x80) || byte > (i == 1 ? high : 0xBF)) return false;
        }
        pos += continuation + 1;
    }
    *textLen = len;
    return len > 0;
}

bool isModbusReadFunction(uint8_t fc) { return fc >= 1 && fc <= 4; }

bool isModbusWriteFunction(uint8_t fc) { return fc == 5 || fc == 6 || fc == 15 || fc == 16; }

bool isValidModbusReadCount(uint8_t fc, uint32_t count) {
    if (fc == 1 || fc == 2) return count >= 1 && count <= 2000;
    if (fc == 3 || fc == 4) return count >= 1 && count <= 125;
    return false;
}

bool isValidModbusWriteCount(uint8_t fc, size_t count) {
    if (fc == 5 || fc == 6) return count == 1;
    if (fc == 15) return count >= 1 && count <= 1968;
    if (fc == 16) return count >= 1 && count <= 123;
    return false;
}

bool isValidModbusWriteValue(uint8_t fc, int64_t value) {
    if (fc == 5 || fc == 15) return value == 0 || value == 1;
    if (fc == 6 || fc == 16) return value >= 0 && value <= 0xFFFF;
    return false;
}

static size_t writeMbap(uint8_t *out, uint16_t transactionId, uint8_t unit, size_t pduLen) {
    putU16(out, transactionId);
    putU16(out + 2, 0); // protocol id
    putU16(out + 4, static_cast<uint16_t>(pduLen + 1)); // unit id + PDU
    out[6] = unit;
    return 7;
}

size_t buildModbusReadRequest(uint8_t *out, size_t cap, uint16_t transactionId, uint8_t unit,
                              uint8_t fc, uint16_t address, uint16_t count) {
    if (out == nullptr || !isModbusReadFunction(fc) || !isValidModbusReadCount(fc, count)) return 0;
    const size_t pduLen = 5;
    if (cap < 7 + pduLen) return 0;
    size_t n = writeMbap(out, transactionId, unit, pduLen);
    out[n++] = fc;
    putU16(out + n, address);
    n += 2;
    putU16(out + n, count);
    n += 2;
    return n;
}

size_t buildModbusWriteRequest(uint8_t *out, size_t cap, uint16_t transactionId, uint8_t unit,
                               uint8_t fc, uint16_t address, const uint16_t *values, size_t count) {
    if (out == nullptr || values == nullptr || !isModbusWriteFunction(fc) || !isValidModbusWriteCount(fc, count)) return 0;
    for (size_t i = 0; i < count; i++) {
        if (!isValidModbusWriteValue(fc, values[i])) return 0;
    }

    size_t pduLen;
    if (fc == 5 || fc == 6) pduLen = 5;
    else if (fc == 15) pduLen = 6 + (count + 7) / 8;
    else pduLen = 6 + count * 2;
    if (cap < 7 + pduLen) return 0;

    size_t n = writeMbap(out, transactionId, unit, pduLen);
    out[n++] = fc;
    putU16(out + n, address);
    n += 2;

    if (fc == 5) {
        putU16(out + n, values[0] ? 0xFF00 : 0x0000);
        n += 2;
    } else if (fc == 6) {
        putU16(out + n, values[0]);
        n += 2;
    } else if (fc == 15) {
        putU16(out + n, static_cast<uint16_t>(count));
        n += 2;
        size_t byteCount = (count + 7) / 8;
        out[n++] = static_cast<uint8_t>(byteCount);
        memset(out + n, 0, byteCount);
        for (size_t i = 0; i < count; i++) {
            if (values[i]) out[n + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
        n += byteCount;
    } else {
        putU16(out + n, static_cast<uint16_t>(count));
        n += 2;
        out[n++] = static_cast<uint8_t>(count * 2);
        for (size_t i = 0; i < count; i++) {
            putU16(out + n, values[i]);
            n += 2;
        }
    }
    return n;
}

size_t modbusFrameLength(const uint8_t *header, size_t len) {
    if (header == nullptr || len < 6) return 0;
    if (getU16(header + 2) != 0) return 0; // protocol id must be Modbus
    uint16_t remaining = getU16(header + 4);
    if (remaining < 2 || remaining > MODBUS_MAX_FRAME - 6) return 0;
    return 6 + remaining;
}

ModbusParse parseModbusResponse(const uint8_t *frame, size_t len, uint16_t transactionId, uint8_t unit,
                                uint8_t fc, uint16_t expectedCount, uint16_t *outValues, size_t outCap,
                                size_t *outCount, uint8_t *exceptionCode) {
    if (frame == nullptr || outCount == nullptr) return ModbusParse::MALFORMED;
    *outCount = 0;
    size_t frameLen = modbusFrameLength(frame, len);
    if (frameLen == 0 || frameLen != len) return ModbusParse::MALFORMED;
    if (getU16(frame) != transactionId || frame[6] != unit) return ModbusParse::MALFORMED;

    const uint8_t *pdu = frame + 7;
    size_t pduLen = len - 7;
    if (pduLen < 2) return ModbusParse::MALFORMED;

    if (pdu[0] == (fc | 0x80)) {
        if (exceptionCode != nullptr) *exceptionCode = pdu[1];
        return ModbusParse::EXCEPTION;
    }
    if (pdu[0] != fc) return ModbusParse::MALFORMED;

    if (isModbusReadFunction(fc)) {
        if (outValues == nullptr || outCap < expectedCount) return ModbusParse::MALFORMED;
        size_t byteCount = pdu[1];
        if (byteCount != pduLen - 2) return ModbusParse::MALFORMED;
        const uint8_t *data = pdu + 2;
        if (isBitFunction(fc)) {
            if (byteCount != (static_cast<size_t>(expectedCount) + 7) / 8) return ModbusParse::MALFORMED;
            for (size_t i = 0; i < expectedCount; i++) {
                outValues[i] = (data[i / 8] >> (i % 8)) & 1u;
            }
        } else {
            if (byteCount != static_cast<size_t>(expectedCount) * 2) return ModbusParse::MALFORMED;
            for (size_t i = 0; i < expectedCount; i++) {
                outValues[i] = getU16(data + i * 2);
            }
        }
        *outCount = expectedCount;
        return ModbusParse::OK;
    }

    if (isModbusWriteFunction(fc)) {
        if (pduLen != 5) return ModbusParse::MALFORMED; // fc + address + value/quantity
        if (fc == 15 || fc == 16) {
            uint16_t quantity = getU16(pdu + 3);
            if (quantity != expectedCount) return ModbusParse::MALFORMED;
            *outCount = quantity;
        } else {
            *outCount = 1;
        }
        return ModbusParse::OK;
    }

    return ModbusParse::MALFORMED;
}

size_t buildScanTargets(uint32_t self, uint32_t netmask, uint32_t *outHosts, size_t cap) {
    if (outHosts == nullptr) return 0;
    // Never sweep more than the /24 around the device: a wider subnet (e.g. a
    // /16 office LAN) would take minutes and flood the network.
    uint32_t mask = netmask | 0xFFFFFF00u;
    uint32_t network = self & mask;
    uint32_t broadcast = network | ~mask;
    size_t n = 0;
    for (uint32_t host = network + 1; host < broadcast && n < cap; host++) {
        if (host == self) continue;
        outHosts[n++] = host;
    }
    return n;
}

} // namespace GatewayLogic
