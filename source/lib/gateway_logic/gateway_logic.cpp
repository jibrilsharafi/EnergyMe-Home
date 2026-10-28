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
    if (!parseIpv4(text, ip)) return false;
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
