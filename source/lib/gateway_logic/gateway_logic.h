// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free logic for the LAN gateway (openspec capability:
// lan-gateway): target validation, argument clamps, HTTP request validation and
// response parsing, Modbus TCP framing and the scan range. Sockets, tasks and JSON live in gateway.cpp; everything here is
// host-testable.

namespace GatewayLogic {

inline constexpr uint32_t TIMEOUT_DEFAULT_MS = 5000;
inline constexpr uint32_t TIMEOUT_MIN_MS = 100;
inline constexpr uint32_t TIMEOUT_MAX_MS = 10000;
inline constexpr uint32_t UDP_WAIT_MAX_MS = 5000;
inline constexpr uint32_t PING_COUNT_DEFAULT = 3;
inline constexpr uint32_t PING_COUNT_MAX = 5;
inline constexpr size_t RESPONSE_CAP_BYTES = 4096;
inline constexpr size_t HTTP_HEADER_CAP_BYTES = 2048; // Status line + headers of a LAN reply
inline constexpr size_t HTTP_PATH_MAX_LEN = 512;

enum class Op { UNKNOWN, PING, NET_SCAN, HTTP_REQUEST, MODBUS_READ, MODBUS_WRITE, UDP_SEND, TUNNEL_OPEN, TUNNEL_CLOSE, TCP_REQUEST, HTTPS_REQUEST, TLS_PROBE, SECRET_SET, SECRET_DELETE, COLLECTOR_STATUS };

Op parseOp(const char *op);
const char *opName(Op op);

// Strict dotted-quad: exactly four decimal octets 0-255, no leading zeros
// (rejects octal-looking "010"), no sign, no whitespace, nothing trailing.
bool parseIpv4(const char *text, uint8_t out[4]);

// True iff the address is in 10/8, 172.16/12 or 192.168/16.
bool isPrivateIpv4(const char *text); // strict literal, see parseIpv4
bool isPrivateIpv4(const uint8_t ip[4]);

uint32_t clampTimeoutMs(int64_t requested); // <=0 -> default
uint32_t clampUdpWaitMs(int64_t requested); // <0 -> 0
uint32_t clampPingCount(int64_t requested); // <=0 -> default

bool isValidPort(int64_t port);
bool isAllowedHttpMethod(const char *method);
bool httpMethodHasBody(const char *method); // POST, PUT, PATCH always send Content-Length

// Must start with '/', at most HTTP_PATH_MAX_LEN, and contain no control
// characters or spaces (the firmware puts it on the request line as-is).
bool isValidHttpPath(const char *path);

// Header names: RFC 7230 token chars. Header values: no CR/LF/NUL.
bool isValidHeaderName(const char *name);
bool isValidHeaderValue(const char *value);

// Headers the firmware owns because they frame the request (Host, Content-Length,
// Transfer-Encoding, Connection, ...). Case-insensitive. Letting the cloud set them would allow request smuggling against lenient LAN servers.
bool isReservedHeaderName(const char *name);

// ---------------------------------------------------------------------------
// HTTP response parsing (the firmware sends HTTP/1.1 + Connection: close)
// ---------------------------------------------------------------------------

// Offset of the first body byte (just past the blank line), or 0 if the
// header block is not complete within `len`.
size_t httpHeaderEnd(const uint8_t *buffer, size_t len);

// "HTTP/1.x NNN ..." -> NNN. False if the status line is malformed.
bool httpStatusCode(const uint8_t *buffer, size_t headerLen, int *code);

// Content-Length value, or -1 when absent, invalid or above 9 digits (fits a 32-bit size_t).
int64_t httpContentLength(const uint8_t *buffer, size_t headerLen);

bool httpIsChunked(const uint8_t *buffer, size_t headerLen);

// True once a chunked body ends with its zero-size last chunk (no trailers).
bool httpChunkedEnded(const uint8_t *body, size_t len);

enum class Chunked { COMPLETE, INCOMPLETE, MALFORMED };

// Decodes a chunked body into `out` (up to `cap`). INCOMPLETE when the input
// ends before the terminating zero chunk or `out` fills up first.
Chunked decodeChunked(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *outLen);

// True when a LAN reply can be published as a plain string instead of base64:
// non-empty, strict UTF-8 (no overlongs, surrogates or code points above
// U+10FFFF) and no C0 controls other than tab, CR and LF, so the escaped JSON
// stays at most twice the byte count. When `truncated`, an incomplete trailing
// sequence cut by the cap is dropped and `textLen` excludes it.
bool utf8TextLength(const uint8_t *data, size_t len, bool truncated, size_t *textLen);

// ---------------------------------------------------------------------------
// Modbus TCP
// ---------------------------------------------------------------------------

inline constexpr size_t MODBUS_MAX_FRAME = 260; // MBAP (7) + PDU (253)
inline constexpr size_t MODBUS_LENGTH_PREFIX = 6; // MBAP bytes up to and including the length field

bool isModbusReadFunction(uint8_t fc);  // 1, 2, 3, 4
bool isModbusWriteFunction(uint8_t fc); // 5, 6, 15, 16

// Count limits from the Modbus spec: bits 1-2000 / registers 1-125 on reads;
// FC5/FC6 exactly 1, FC15 1-1968 bits, FC16 1-123 registers on writes.
bool isValidModbusReadCount(uint8_t fc, uint32_t count);
bool isValidModbusWriteCount(uint8_t fc, size_t count);

// Coil values must be 0/1, register values 0-65535.
bool isValidModbusWriteValue(uint8_t fc, int64_t value);

// Return the frame length, or 0 if the arguments are invalid or `cap` is too small.
size_t buildModbusReadRequest(uint8_t *out, size_t cap, uint16_t transactionId, uint8_t unit,
                              uint8_t fc, uint16_t address, uint16_t count);
size_t buildModbusWriteRequest(uint8_t *out, size_t cap, uint16_t transactionId, uint8_t unit,
                               uint8_t fc, uint16_t address, const uint16_t *values, size_t count);

// Total frame length announced by an MBAP header (needs >= 6 bytes), 0 if invalid.
size_t modbusFrameLength(const uint8_t *header, size_t len);

enum class ModbusParse { OK, EXCEPTION, MALFORMED };

// Validates the response against the request (transaction id, unit, function)
// and decodes it. Reads: `outValues` gets `expectedCount` values (bits unpacked
// to 0/1). Writes: `outCount` is the echoed quantity. On EXCEPTION,
// `exceptionCode` is set.
ModbusParse parseModbusResponse(const uint8_t *frame, size_t len, uint16_t transactionId, uint8_t unit,
                                uint8_t fc, uint16_t expectedCount, uint16_t *outValues, size_t outCap,
                                size_t *outCount, uint8_t *exceptionCode);

// ---------------------------------------------------------------------------
// Scan range
// ---------------------------------------------------------------------------

// Fills `outHosts` (cap >= 254) with the host addresses (as uint32 host order)
// of the device's subnet, limited to the /24 around `self`. Excludes network,
// broadcast and self. Returns the count.
size_t buildScanTargets(uint32_t self, uint32_t netmask, uint32_t *outHosts, size_t cap);

} // namespace GatewayLogic
