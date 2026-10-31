// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

// Protocol modules of the gateway executor (openspec capability:
// gateway-modules): http/https, modbus_tcp, tcp, udp. Each runs after every
// pre-socket check passed; reason details keep the wording of the previous
// per-op functions so command replies stay byte-identical.

#include "gateway_modules.h"

#include <WiFi.h>

#include "gateway_logic.h"
#include "gateway_stream.h"

using namespace GatewayLogic;

namespace GatewayExec
{
    static constexpr uint32_t READ_WAIT_MS = 50;
    static constexpr size_t READ_CHUNK_BYTES = 1460;

    // HTTP / HTTPS
    // ============

    struct HttpBody {
        int64_t contentLength = -1;
        bool chunked = false;
        ChunkDecoder decoder;
        ChunkDecoder::Status chunkStatus = ChunkDecoder::Status::MORE;
        size_t received = 0; // body bytes on the wire (Content-Length framing)
        bool overflow = false;
        bool complete = false;
    };

    // Feeds received body bytes; true while more are wanted.
    static bool _feedBody(HttpBody& state, const uint8_t* data, size_t length, uint8_t* body, size_t cap, size_t* bodyLength) {
        if (state.chunked) {
            state.chunkStatus = state.decoder.feed(data, length, body, cap, bodyLength);
            if (state.chunkStatus == ChunkDecoder::Status::OVERFLOW) state.overflow = true;
            state.complete = state.chunkStatus == ChunkDecoder::Status::DONE;
            return state.chunkStatus == ChunkDecoder::Status::MORE;
        }
        size_t take = length;
        if (state.contentLength >= 0) {
            size_t remaining = (size_t)state.contentLength - state.received;
            if (take > remaining) take = remaining;
        }
        state.received += take;
        size_t room = cap - *bodyLength;
        if (take > room) {
            state.overflow = true;
            take = room;
        }
        memcpy(body + *bodyLength, data, take);
        *bodyLength += take;
        if (state.contentLength >= 0 && state.received >= (size_t)state.contentLength) state.complete = true;
        if (state.contentLength >= 0 && *bodyLength >= cap && !state.complete) state.overflow = true; // Content-Length beyond the cap
        return !state.complete && !state.overflow;
    }

    void runHttp(const Prepared& prepared, Result& result, bool tls) {
        const Request& rq = *prepared.request;
        GatewayNet::Connection connection;
        if (rq.probeOnly) {
            connectAndSend(connection, prepared, result, tls, nullptr, 0); // tls_probe: the certificate is the result
            return;
        }

        // Allocated before connecting, so a write is never sent without room for its reply.
        result.headers = (char*)ps_malloc(HEADER_BLOCK_MAX + 1);
        result.body = (uint8_t*)ps_malloc(rq.maxBytes);
        uint8_t* chunk = (uint8_t*)ps_malloc(READ_CHUNK_BYTES);
        if (result.headers == nullptr || result.body == nullptr || chunk == nullptr) {
            free(chunk);
            result.release();
            setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the HTTP buffers");
            return;
        }
        if (!connectAndSend(connection, prepared, result, tls, prepared.wire, prepared.wireLength)) {
            free(chunk);
            result.release();
            return;
        }

        HttpBody state;
        size_t headerLength = 0, headerEnd = 0;
        bool closed = false, wantMore = true;
        for (uint32_t loops = 0; wantMore && loops < 1000000; loops++) {
            size_t got = 0;
            GatewayNet::Status status = headerEnd == 0
                ? connection.read((uint8_t*)result.headers + headerLength, HEADER_BLOCK_MAX - headerLength, &got, prepared.deadline, READ_WAIT_MS)
                : connection.read(chunk, READ_CHUNK_BYTES, &got, prepared.deadline, READ_WAIT_MS);
            if (status == GatewayNet::Status::CLOSED) {
                closed = true;
                break;
            }
            if (status != GatewayNet::Status::OK) break;
            if (got == 0) continue;
            if (result.firstByteMs == 0) result.firstByteMs = elapsedSince(prepared.start);
            result.bytesRx += got;

            if (headerEnd != 0) {
                wantMore = _feedBody(state, chunk, got, result.body, rq.maxBytes, &result.bodyLength);
                continue;
            }
            headerLength += got;
            headerEnd = httpHeaderEnd((const uint8_t*)result.headers, headerLength);
            if (headerEnd == 0) {
                if (headerLength >= HEADER_BLOCK_MAX) break;
                continue;
            }
            state.contentLength = httpContentLength((const uint8_t*)result.headers, headerEnd);
            state.chunked = httpIsChunked((const uint8_t*)result.headers, headerEnd);
            if (state.chunked) state.contentLength = -1;
            wantMore = state.contentLength != 0 || state.chunked;
            if (state.contentLength == 0) state.complete = true;
            if (headerLength > headerEnd) {
                wantMore = _feedBody(state, (const uint8_t*)result.headers + headerEnd, headerLength - headerEnd, result.body, rq.maxBytes,
                                     &result.bodyLength);
            }
        }
        free(chunk);
        connection.close();

        int statusCode = 0;
        if (headerEnd == 0 || !httpStatusCode((const uint8_t*)result.headers, headerEnd, &statusCode)) {
            bool timedOut = headerEnd == 0 && !closed && headerLength < HEADER_BLOCK_MAX;
            result.release();
            if (timedOut) setOutcome(result, Status::FAILED, "TIMEOUT", "No complete HTTP response header within timeout_ms");
            else setOutcome(result, Status::FAILED, "BAD_RESPONSE", "Missing, oversized or malformed HTTP header");
            return;
        }
        if (state.chunked && state.chunkStatus == ChunkDecoder::Status::MALFORMED) {
            result.release();
            setOutcome(result, Status::FAILED, "BAD_RESPONSE", "Malformed chunked body");
            return;
        }

        result.headersLength = headerEnd;
        result.headers[headerEnd] = '\0';
        result.protoStatus = statusCode;
        if (state.chunked || state.contentLength >= 0) {
            result.truncated = state.overflow || !state.complete;
        } else {
            result.truncated = state.overflow || !closed;
        }
        setOutcome(result, Status::OK, "");
    }

    // Modbus TCP
    // ==========

    static bool _readExact(GatewayNet::Connection& connection, uint8_t* buffer, size_t length, const Prepared& prepared, Result& result) {
        size_t got = 0;
        for (uint32_t loops = 0; got < length && loops < 100000; loops++) {
            size_t n = 0;
            if (connection.read(buffer + got, length - got, &n, prepared.deadline, READ_WAIT_MS) != GatewayNet::Status::OK) break;
            if (n > 0 && result.firstByteMs == 0) result.firstByteMs = elapsedSince(prepared.start);
            got += n;
            result.bytesRx += n;
        }
        return got == length;
    }

    enum class Exchange { OK, NO_REPLY, BAD_FRAME, EXCEPTION, MISMATCH, SEND_FAILED };

    // One request/response on an open connection.
    static Exchange _exchange(GatewayNet::Connection& connection, const Prepared& prepared, Result& result, uint8_t fc, uint16_t address,
                              const uint16_t* values, size_t count, uint16_t* decoded, size_t* decodedCount, uint8_t* exception) {
        const Request& rq = *prepared.request;
        uint16_t transactionId = (uint16_t)esp_random();
        uint8_t frame[MODBUS_MAX_FRAME];
        size_t frameLength = values != nullptr
            ? buildModbusWriteRequest(frame, sizeof(frame), transactionId, rq.address.unit, fc, address, values, count)
            : buildModbusReadRequest(frame, sizeof(frame), transactionId, rq.address.unit, fc, address, (uint16_t)count);
        if (frameLength == 0) return Exchange::BAD_FRAME;
        if (connection.writeAll(frame, frameLength, prepared.deadline) != GatewayNet::Status::OK) return Exchange::SEND_FAILED;
        result.bytesTx += frameLength;

        uint8_t response[MODBUS_MAX_FRAME];
        if (!_readExact(connection, response, MODBUS_LENGTH_PREFIX, prepared, result)) return Exchange::NO_REPLY;
        size_t responseLength = modbusFrameLength(response, MODBUS_LENGTH_PREFIX);
        if (responseLength == 0 || !_readExact(connection, response + MODBUS_LENGTH_PREFIX, responseLength - MODBUS_LENGTH_PREFIX, prepared, result)) {
            return Exchange::BAD_FRAME;
        }
        ModbusParse parsed = parseModbusResponse(response, responseLength, transactionId, rq.address.unit, fc, (uint16_t)count, decoded,
                                                 decoded != nullptr ? count : 0, decodedCount, exception);
        if (parsed == ModbusParse::EXCEPTION) return Exchange::EXCEPTION;
        return parsed == ModbusParse::OK ? Exchange::OK : Exchange::MISMATCH;
    }

    static bool _exchangeFailed(Exchange exchange, uint8_t exception, Result& result, bool readback) {
        switch (exchange) {
            case Exchange::OK: return false;
            case Exchange::NO_REPLY:
                if (readback) setOutcome(result, Status::FAILED, "TIMEOUT", "Write acknowledged, no read-back reply within timeout_ms");
                else setOutcome(result, Status::FAILED, "TIMEOUT", "No Modbus response within timeout_ms");
                return true;
            case Exchange::BAD_FRAME: setOutcome(result, Status::FAILED, "BAD_RESPONSE", "Truncated or invalid Modbus frame"); return true;
            case Exchange::EXCEPTION:
                result.protoStatus = exception;
                setOutcome(result, Status::FAILED, "MODBUS_EXCEPTION", "Modbus exception code %u", exception);
                return true;
            case Exchange::MISMATCH: setOutcome(result, Status::FAILED, "BAD_RESPONSE", "Modbus response does not match the request"); return true;
            case Exchange::SEND_FAILED: setOutcome(result, Status::FAILED, "CONNECT_FAILED", "Could not send the request"); return true;
        }
        return true;
    }

    void runModbus(const Prepared& prepared, Result& result) {
        const Request& rq = *prepared.request;
        size_t count = rq.modbusWrite ? rq.valueCount : rq.count;
        uint8_t fc = functionFor(rq.address.table, rq.modbusWrite, count, rq.writeMultiple);

        if (!rq.modbusWrite) {
            result.registers = (uint16_t*)ps_malloc(count * sizeof(uint16_t));
        } else if (rq.verify) {
            result.readback = (uint16_t*)ps_malloc(count * sizeof(uint16_t));
        }
        if ((!rq.modbusWrite && result.registers == nullptr) || (rq.verify && result.readback == nullptr)) {
            setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the decode buffer");
            return;
        }
        GatewayNet::Connection connection;
        if (!connectAndSend(connection, prepared, result, false, nullptr, 0)) {
            result.release();
            return;
        }
        size_t decodedCount = 0;
        uint8_t exception = 0;
        Exchange exchange = _exchange(connection, prepared, result, fc, rq.address.address, rq.modbusWrite ? rq.values : nullptr, count,
                                      result.registers, &decodedCount, &exception);
        if (_exchangeFailed(exchange, exception, result, false)) {
            result.release();
            return;
        }
        if (!rq.modbusWrite) {
            result.registerCount = decodedCount;
            setOutcome(result, Status::OK, "");
            return;
        }
        result.written = decodedCount;
        if (!rq.verify) {
            setOutcome(result, Status::OK, "");
            return;
        }

        uint8_t readFc = rq.address.table == ModbusTable::COILS ? 1 : 3;
        exchange = _exchange(connection, prepared, result, readFc, rq.address.address, nullptr, count, result.readback, &result.readbackCount, &exception);
        if (_exchangeFailed(exchange, exception, result, true)) return;
        bool same = result.readbackCount == count;
        for (size_t i = 0; same && i < count; i++) same = result.readback[i] == rq.values[i];
        if (!same) {
            char list[96];
            size_t used = 0;
            for (size_t i = 0; i < result.readbackCount && used + 8 < sizeof(list); i++) {
                used += (size_t)snprintf(list + used, sizeof(list) - used, i == 0 ? "%u" : ",%u", result.readback[i]);
            }
            setOutcome(result, Status::FAILED, "READBACK_MISMATCH", "Wrote %u values, read back [%s]", (unsigned)count, list);
            return;
        }
        setOutcome(result, Status::OK, "");
    }

    // Raw TCP
    // =======

    void runTcp(const Prepared& prepared, Result& result) {
        const Request& rq = *prepared.request;
        result.body = (uint8_t*)ps_malloc(rq.maxBytes);
        if (result.body == nullptr) {
            setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the reply buffer");
            return;
        }
        GatewayNet::Connection connection;
        if (!connectAndSend(connection, prepared, result, false, prepared.wire, prepared.wireLength)) {
            result.release();
            return;
        }
        uint32_t idleMs = clampIdleMs(rq.idleMs);
        uint32_t lastByte = millis();
        TcpReadStop stop = TcpReadStop::CONTINUE;
        bool closed = false;
        for (uint32_t loops = 0; loops < 1000000; loops++) {
            stop = tcpReadStop(closed, result.bodyLength, rq.maxBytes, millis() - lastByte, idleMs, !beforeDeadline(prepared.deadline));
            if (stop != TcpReadStop::CONTINUE) break;
            size_t got = 0;
            GatewayNet::Status status = connection.read(result.body + result.bodyLength, rq.maxBytes - result.bodyLength, &got, prepared.deadline,
                                                        idleMs < READ_WAIT_MS ? idleMs : READ_WAIT_MS);
            if (status == GatewayNet::Status::CLOSED) closed = true;
            if (got > 0) {
                if (result.firstByteMs == 0) result.firstByteMs = elapsedSince(prepared.start);
                result.bodyLength += got;
                result.bytesRx += got;
                lastByte = millis();
            }
        }
        connection.close();
        if (result.bodyLength == 0) {
            result.release();
            setOutcome(result, Status::FAILED, "NO_REPLY", "No bytes from the target within timeout_ms");
            return;
        }
        result.truncated = tcpReplyTruncated(stop, result.bodyLength);
        setOutcome(result, Status::OK, "");
    }

    // UDP
    // ===

    void runUdp(const Prepared& prepared, Result& result) {
        const Request& rq = *prepared.request;
        if (!checkMacBeforeFirstByte(prepared, result, true)) return;

        IPAddress target(prepared.octets[0], prepared.octets[1], prepared.octets[2], prepared.octets[3]);
        WiFiUDP udp;
        if (!udp.begin(0)) {
            setOutcome(result, Status::FAILED, "NETWORK_DOWN", "Could not open a UDP socket");
            return;
        }
        bool sent = udp.beginPacket(target, rq.port) && udp.write(prepared.wire, prepared.wireLength) == prepared.wireLength && udp.endPacket();
        if (!sent) {
            udp.stop();
            setOutcome(result, Status::FAILED, "CONNECT_FAILED", "Could not send the datagram");
            return;
        }
        result.connected = true;
        result.bytesTx = prepared.wireLength;
        if (rq.waitMs == 0) {
            udp.stop();
            setOutcome(result, Status::OK, "");
            return;
        }

        result.body = (uint8_t*)ps_malloc(UDP_REPLY_MAX);
        if (result.body == nullptr) {
            udp.stop();
            setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the reply buffer");
            return;
        }
        bool gotReply = false;
        uint32_t replyDeadline = millis() + rq.waitMs;
        const uint32_t maxLoops = rq.waitMs / 10 + 100;
        for (uint32_t loops = 0; beforeDeadline(replyDeadline) && loops < maxLoops; loops++) {
            if (udp.parsePacket() > 0) {
                if (udp.remoteIP() == target) {
                    result.bodyLength = udp.read(result.body, UDP_REPLY_MAX);
                    gotReply = true;
                    break;
                }
                udp.flush(); // ignore datagrams from anyone else
            }
            delay(10);
        }
        udp.stop();
        if (!gotReply) {
            result.release();
            setOutcome(result, Status::FAILED, "NO_REPLY", "No datagram from the target within wait_ms");
            return;
        }
        result.firstByteMs = elapsedSince(prepared.start);
        result.bytesRx = result.bodyLength;
        result.truncated = result.bodyLength >= UDP_REPLY_MAX;
        setOutcome(result, Status::OK, "");
    }
}
