// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway.h"

#include <atomic>

#include <AdvancedLogger.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "mbedtls/base64.h"
#include "ping/ping_sock.h"

#include "constants.h"
#include "utils.h"

using GatewayLogic::Op;

namespace Gateway
{
    struct Request {
        char executionId[NAME_BUFFER_SIZE];
        Op op;
        char* args; // PSRAM JSON, owned by the gateway task once queued
    };

    static bool _enabled = GATEWAY_ENABLED_DEFAULT; // aligned bool: atomic read on the S3
    static SemaphoreHandle_t _stateMutex = nullptr;  // serializes setEnabled (REST vs shadow)
    static TaskHandle_t _taskHandle = nullptr;
    static QueueHandle_t _queue = nullptr;
    static SemaphoreHandle_t _resultMutex = nullptr;

    // Set by submit(), cleared by takeResult(): covers the queued request, the
    // running one and its undrained result, so the single result slot is never
    // overwritten.
    static std::atomic<bool> _busy{false};
    static char _resultExecutionId[NAME_BUFFER_SIZE] = "";
    static char* _resultJson = nullptr;

    // Response building
    // =================

    static void _setStatus(JsonDocument& doc, const char* status, const char* code, const char* description) {
        doc["status"] = status;
        doc["statusReason"]["reasonCode"] = code;
        if (description) doc["statusReason"]["reasonDescription"] = description;
    }

    static void _reject(JsonDocument& doc, const char* code, const char* description) { _setStatus(doc, "REJECTED", code, description); }
    static void _fail(JsonDocument& doc, const char* code, const char* description) { _setStatus(doc, "FAILED", code, description); }

    static JsonObject _succeed(JsonDocument& doc) {
        doc["status"] = "SUCCEEDED";
        return doc["result"].to<JsonObject>();
    }

    static void _setString(JsonObject result, const char* key, const char* value) { result[key]["s"] = value; }

    static void _setNumber(JsonObject result, const char* key, int64_t value) {
        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
        result[key]["s"] = buffer;
    }

    static void _setBool(JsonObject result, const char* key, bool value) { result[key]["b"] = value; }

    // AWS requires at least one byte in a bin value, so an empty payload is reported as an empty string instead.
    static bool _setBinary(JsonObject result, const char* key, const uint8_t* data, size_t length) {
        if (length == 0) {
            result[key]["s"] = "";
            return true;
        }
        size_t encodedLength = 0;
        mbedtls_base64_encode(nullptr, 0, &encodedLength, data, length);
        char* encoded = (char*)ps_malloc(encodedLength + 1);
        if (encoded == nullptr) return false;
        size_t written = 0;
        bool ok = mbedtls_base64_encode((unsigned char*)encoded, encodedLength + 1, &written, data, length) == 0;
        if (ok) {
            encoded[written] = '\0';
            result[key]["bin"] = (const char*)encoded; // const char* -> ArduinoJson copies
        }
        free(encoded);
        return ok;
    }

    // Decodes base64 into a new NUL-terminated PSRAM buffer (caller frees).
    static bool _decodeBase64(const char* text, uint8_t** out, size_t* outLength, size_t maxLength) {
        size_t textLength = strlen(text);
        size_t decodedLength = 0;
        int rc = mbedtls_base64_decode(nullptr, 0, &decodedLength, (const unsigned char*)text, textLength);
        if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return false;
        if (decodedLength == 0 || decodedLength > maxLength) return false;
        uint8_t* buffer = (uint8_t*)ps_malloc(decodedLength + 1);
        if (buffer == nullptr) return false;
        if (mbedtls_base64_decode(buffer, decodedLength, &decodedLength, (const unsigned char*)text, textLength) != 0) {
            free(buffer);
            return false;
        }
        buffer[decodedLength] = '\0';
        *out = buffer;
        *outLength = decodedLength;
        return true;
    }

    // Serializes a result list, dropping trailing entries until it fits the budget.
    static void _setList(JsonObject result, const char* key, JsonArray list, bool* truncated) {
        size_t loops = 0;
        while (list.size() > 0 && measureJson(list) > GATEWAY_SCAN_LIST_BUDGET && loops++ < GATEWAY_SCAN_MAX_HOSTS) {
            list.remove(list.size() - 1);
            *truncated = true;
        }
        size_t length = measureJson(list) + 1;
        char* text = (char*)ps_malloc(length);
        if (text == nullptr) {
            _setString(result, key, "[]");
            *truncated = true;
            return;
        }
        serializeJson(list, text, length);
        _setString(result, key, text);
        free(text);
    }

    // Argument helpers
    // ================

    // Absent -> default; present with the wrong type -> false.
    static bool _readInt(JsonObjectConst args, const char* key, int64_t defaultValue, int64_t* out) {
        JsonVariantConst value = args[key];
        if (value.isNull()) {
            *out = defaultValue;
            return true;
        }
        if (!value.is<int64_t>()) return false;
        *out = value.as<int64_t>();
        return true;
    }

    static bool _requireInt(JsonObjectConst args, const char* key, int64_t defaultValue, JsonDocument& doc, int64_t* out) {
        if (_readInt(args, key, defaultValue, out)) return true;
        char description[64];
        snprintf(description, sizeof(description), "%s must be an integer", key);
        _reject(doc, "BAD_ARGS", description);
        return false;
    }

    // Absent -> default; present but not a string -> nullptr.
    static const char* _readString(JsonObjectConst args, const char* key, const char* defaultValue) {
        JsonVariantConst value = args[key];
        if (value.isNull()) return defaultValue;
        return value.is<const char*>() ? value.as<const char*>() : nullptr;
    }

    // The device itself (any interface, which would hand the cloud the local admin
    // API through loopback) and the network/broadcast addresses of its subnets.
    static bool _isLocalOrBroadcast(const uint8_t octets[4]) {
        uint32_t target = ((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) | ((uint32_t)octets[2] << 8) | octets[3];
        bool local = false;
        LOCK_TCPIP_CORE();
        struct netif* nif = nullptr;
        NETIF_FOREACH(nif) {
            if (!netif_is_up(nif)) continue;
            uint32_t address = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif)));
            if (address == 0) continue;
            uint32_t mask = lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(nif)));
            if (target == address || target == (address & mask) || target == (address | ~mask)) {
                local = true;
                break;
            }
        }
        UNLOCK_TCPIP_CORE();
        return local;
    }

    static bool _readTarget(JsonObjectConst args, JsonDocument& doc, const char** ip, uint8_t octets[4]) {
        *ip = _readString(args, "ip", nullptr);
        if (*ip == nullptr) {
            _reject(doc, "BAD_ARGS", "ip must be an IPv4 string");
            return false;
        }
        if (!GatewayLogic::parseIpv4(*ip, octets) || !GatewayLogic::isPrivateIpv4(octets)) {
            _reject(doc, "TARGET_NOT_PRIVATE", "Target must be a private IPv4 literal");
            return false;
        }
        if (_isLocalOrBroadcast(octets)) {
            _reject(doc, "TARGET_NOT_ALLOWED", "Target is this device or a network/broadcast address");
            return false;
        }
        return true;
    }

    static bool _readPort(JsonObjectConst args, int64_t defaultPort, JsonDocument& doc, uint16_t* port) {
        int64_t value = 0;
        if (!_readInt(args, "port", defaultPort, &value) || !GatewayLogic::isValidPort(value)) {
            _reject(doc, "BAD_ARGS", "port must be 1-65535");
            return false;
        }
        *port = (uint16_t)value;
        return true;
    }

    static bool _readTimeout(JsonObjectConst args, JsonDocument& doc, uint32_t* timeoutMs) {
        int64_t value = 0;
        if (!_requireInt(args, "timeout_ms", 0, doc, &value)) return false;
        *timeoutMs = GatewayLogic::clampTimeoutMs(value);
        return true;
    }

    // Loop bound for a polling loop that sleeps `pollMs` until `timeoutMs` runs out.
    static uint32_t _pollBound(uint32_t timeoutMs, uint32_t pollMs) { return timeoutMs / pollMs + 100; }

    static bool _beforeDeadline(uint32_t deadline) { return (int32_t)(deadline - millis()) > 0; }

    // Primitives
    // ==========

    // HTTP/1.0 + Connection: close so the server ends the body by closing and
    // never chunks; every read is bounded by one absolute deadline. HTTPClient
    // was dropped because its header and body loops have no overall deadline: a
    // LAN host trickling bytes kept the gateway busy forever.
    static void _runHttp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 80, doc, &port) || !_readTimeout(args, doc, &timeoutMs)) return;

        const char* method = _readString(args, "method", "GET");
        if (!GatewayLogic::isAllowedHttpMethod(method)) {
            _reject(doc, "BAD_ARGS", "method must be GET, POST, PUT, PATCH or DELETE");
            return;
        }
        const char* path = _readString(args, "path", "/");
        if (!GatewayLogic::isValidHttpPath(path)) {
            _reject(doc, "BAD_ARGS", "path must start with / and contain no spaces or control characters");
            return;
        }

        JsonVariantConst headers = args["headers"];
        size_t headersLength = 0;
        if (!headers.isNull()) {
            if (!headers.is<JsonObjectConst>()) {
                _reject(doc, "BAD_ARGS", "headers must be an object of strings");
                return;
            }
            for (JsonPairConst header : headers.as<JsonObjectConst>()) {
                const char* name = header.key().c_str();
                if (!header.value().is<const char*>() || !GatewayLogic::isValidHeaderName(name) ||
                    !GatewayLogic::isValidHeaderValue(header.value().as<const char*>())) {
                    _reject(doc, "BAD_ARGS", "Invalid header name or value");
                    return;
                }
                if (GatewayLogic::isReservedHeaderName(name)) {
                    _reject(doc, "BAD_ARGS", "Host, Content-Length, Transfer-Encoding and Connection are set by the device");
                    return;
                }
                headersLength += strlen(name) + strlen(header.value().as<const char*>()) + 4;
            }
        }

        uint8_t* decodedBody = nullptr;
        const uint8_t* body = nullptr;
        size_t bodyLength = 0;
        const char* bodyB64 = _readString(args, "body_b64", nullptr);
        const char* bodyText = _readString(args, "body", nullptr);
        if ((!args["body_b64"].isNull() && bodyB64 == nullptr) || (!args["body"].isNull() && bodyText == nullptr)) {
            _reject(doc, "BAD_ARGS", "body and body_b64 must be strings");
            return;
        }
        if (bodyB64 != nullptr) {
            if (!_decodeBase64(bodyB64, &decodedBody, &bodyLength, GATEWAY_ARGS_MAX_BYTES)) {
                _reject(doc, "BAD_ARGS", "body_b64 is not valid base64 or is too large");
                return;
            }
            body = decodedBody;
        } else if (bodyText != nullptr) {
            body = (const uint8_t*)bodyText;
            bodyLength = strlen(bodyText);
        }

        LOG_INFO("Gateway %s: http_request %s %s:%u%s", executionId, method, ip, port, path);

        size_t headCap = strlen(method) + strlen(path) + headersLength + 160;
        size_t rawCap = GatewayLogic::HTTP_HEADER_CAP_BYTES + GatewayLogic::RESPONSE_CAP_BYTES + 512;
        char* head = (char*)ps_malloc(headCap);
        uint8_t* raw = (uint8_t*)ps_malloc(rawCap);
        uint8_t* chunkedBody = nullptr;
        if (head == nullptr || raw == nullptr) {
            free(decodedBody);
            free(head);
            free(raw);
            _fail(doc, "NO_MEMORY", "Could not allocate the HTTP buffers");
            return;
        }

        size_t used = (size_t)snprintf(head, headCap, "%s %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n", method, path, ip, port);
        if (!headers.isNull()) {
            for (JsonPairConst header : headers.as<JsonObjectConst>()) {
                used += (size_t)snprintf(head + used, headCap - used, "%s: %s\r\n", header.key().c_str(), header.value().as<const char*>());
            }
        }
        bool sendsBody = bodyLength > 0 || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0;
        if (sendsBody) used += (size_t)snprintf(head + used, headCap - used, "Content-Length: %u\r\n", (unsigned)bodyLength);
        snprintf(head + used, headCap - used, "\r\n");

        uint32_t deadline = millis() + timeoutMs;
        WiFiClient client;
        if (!client.connect(IPAddress(octets[0], octets[1], octets[2], octets[3]), port, (int32_t)timeoutMs)) {
            free(decodedBody);
            free(head);
            free(raw);
            _fail(doc, _beforeDeadline(deadline) ? "CONNECT_FAILED" : "TIMEOUT", "TCP connect failed");
            return;
        }
        size_t headLength = strlen(head);
        bool sent = client.write((const uint8_t*)head, headLength) == headLength &&
                    (bodyLength == 0 || client.write(body, bodyLength) == bodyLength);
        free(decodedBody);
        free(head);
        if (!sent) {
            client.stop();
            free(raw);
            _fail(doc, "CONNECT_FAILED", "Could not send the HTTP request");
            return;
        }

        size_t length = 0;
        size_t headerEnd = 0;
        int64_t contentLength = -1;
        bool closed = false;
        bool rawFull = false;
        uint32_t loops = 0;
        const uint32_t maxLoops = _pollBound(timeoutMs, 2);
        while (_beforeDeadline(deadline) && loops++ < maxLoops) {
            int available = client.available();
            if (available > 0) {
                size_t room = rawCap - length;
                int n = client.read(raw + length, (size_t)available < room ? (size_t)available : room);
                if (n > 0) length += (size_t)n;
                if (length == rawCap) {
                    rawFull = true;
                    break;
                }
                if (headerEnd == 0) {
                    headerEnd = GatewayLogic::httpHeaderEnd(raw, length);
                    if (headerEnd == 0 && length >= GatewayLogic::HTTP_HEADER_CAP_BYTES) break;
                    if (headerEnd != 0) contentLength = GatewayLogic::httpContentLength(raw, headerEnd);
                }
                if (headerEnd != 0 && contentLength >= 0 && length - headerEnd >= (size_t)contentLength) break;
            } else if (!client.connected()) {
                closed = true;
                break;
            } else {
                delay(2);
            }
        }
        client.stop();

        int statusCode = 0;
        if (headerEnd == 0 || !GatewayLogic::httpStatusCode(raw, headerEnd, &statusCode)) {
            free(raw);
            if (!closed && !rawFull && length < GatewayLogic::HTTP_HEADER_CAP_BYTES) _fail(doc, "TIMEOUT", "No complete HTTP response header within timeout_ms");
            else _fail(doc, "BAD_RESPONSE", "Missing, oversized or malformed HTTP header");
            return;
        }

        const uint8_t* responseBody = raw + headerEnd;
        size_t received = length - headerEnd;
        size_t bodyOut = 0;
        bool truncated = false;
        if (GatewayLogic::httpIsChunked(raw, headerEnd)) {
            // HTTP/1.0 forbids it, but some embedded servers chunk anyway.
            chunkedBody = (uint8_t*)ps_malloc(GatewayLogic::RESPONSE_CAP_BYTES);
            if (chunkedBody == nullptr) {
                free(raw);
                _fail(doc, "NO_MEMORY", "Could not allocate the chunked body buffer");
                return;
            }
            truncated = GatewayLogic::decodeChunked(responseBody, received, chunkedBody, GatewayLogic::RESPONSE_CAP_BYTES, &bodyOut) !=
                        GatewayLogic::Chunked::COMPLETE;
            responseBody = chunkedBody;
        } else if (contentLength >= 0) {
            size_t expected = (size_t)contentLength;
            bodyOut = received < expected ? received : expected;
            truncated = received < expected;
        } else {
            bodyOut = received;
            truncated = !closed || rawFull;
        }
        if (bodyOut > GatewayLogic::RESPONSE_CAP_BYTES) {
            bodyOut = GatewayLogic::RESPONSE_CAP_BYTES;
            truncated = true;
        }

        JsonObject result = _succeed(doc);
        _setNumber(result, "status_code", statusCode);
        _setBool(result, "truncated", truncated);
        if (!_setBinary(result, "body", responseBody, bodyOut)) {
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not encode the response body");
        }
        free(chunkedBody);
        free(raw);
    }

    struct PingContext {
        SemaphoreHandle_t done;
        uint32_t received;
        uint32_t rttSumMs;
    };

    static void _onPingSuccess(esp_ping_handle_t handle, void* arg) {
        PingContext* context = (PingContext*)arg;
        uint32_t elapsed = 0;
        esp_ping_get_profile(handle, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
        context->received++;
        context->rttSumMs += elapsed;
    }

    static void _onPingEnd(esp_ping_handle_t handle, void* arg) {
        (void)handle;
        xSemaphoreGive(((PingContext*)arg)->done);
    }

    static void _runPing(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint32_t timeoutMs = 0;
        int64_t count = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readTimeout(args, doc, &timeoutMs) || !_requireInt(args, "count", 0, doc, &count)) return;
        uint32_t pings = GatewayLogic::clampPingCount(count);
        uint32_t perPingTimeoutMs = timeoutMs < 1000 ? timeoutMs : 1000;

        LOG_INFO("Gateway %s: ping %s x%u", executionId, ip, pings);

        PingContext context = {xSemaphoreCreateBinary(), 0, 0};
        if (context.done == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not create the ping semaphore");
            return;
        }

        esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
        IP_ADDR4(&config.target_addr, octets[0], octets[1], octets[2], octets[3]);
        config.count = pings;
        config.interval_ms = 200;
        config.timeout_ms = perPingTimeoutMs;

        esp_ping_callbacks_t callbacks = {};
        callbacks.cb_args = &context;
        callbacks.on_ping_success = _onPingSuccess;
        callbacks.on_ping_end = _onPingEnd;

        esp_ping_handle_t handle = nullptr;
        if (esp_ping_new_session(&config, &callbacks, &handle) != ESP_OK) {
            vSemaphoreDelete(context.done);
            _fail(doc, "NETWORK_DOWN", "Could not start the ping session");
            return;
        }
        esp_ping_start(handle);
        uint32_t waitMs = pings * (perPingTimeoutMs + config.interval_ms) + 1000;
        bool finished = xSemaphoreTake(context.done, pdMS_TO_TICKS(waitMs)) == pdTRUE;
        esp_ping_stop(handle);
        esp_ping_delete_session(handle);
        vSemaphoreDelete(context.done);

        if (!finished) LOG_WARNING("Gateway %s: ping session did not report its end in %u ms", executionId, waitMs);

        JsonObject result = _succeed(doc);
        _setNumber(result, "sent", pings);
        _setNumber(result, "received", context.received);
        if (context.received > 0) _setNumber(result, "rtt_avg_ms", context.rttSumMs / context.received);
    }

    // NetworkUDP::parsePacket() never reads more than one MTU of payload.
    static constexpr size_t UDP_MAX_REPLY_BYTES = 1460;

    static void _runUdp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        int64_t wait = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 0, doc, &port) || !_requireInt(args, "wait_ms", 0, doc, &wait)) return;
        uint32_t waitMs = GatewayLogic::clampUdpWaitMs(wait);

        uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        const char* payloadB64 = _readString(args, "payload_b64", nullptr);
        if (payloadB64 == nullptr || !_decodeBase64(payloadB64, &payload, &payloadLength, 1472)) {
            _reject(doc, "BAD_ARGS", "payload_b64 must be base64 of 1-1472 bytes");
            return;
        }

        LOG_INFO("Gateway %s: udp_send %s:%u (%u bytes, wait %u ms)", executionId, ip, port, (unsigned)payloadLength, waitMs);

        IPAddress target(octets[0], octets[1], octets[2], octets[3]);
        WiFiUDP udp;
        if (!udp.begin(0)) {
            free(payload);
            _fail(doc, "NETWORK_DOWN", "Could not open a UDP socket");
            return;
        }
        bool sent = udp.beginPacket(target, port) && udp.write(payload, payloadLength) == payloadLength && udp.endPacket();
        free(payload);
        if (!sent) {
            udp.stop();
            _fail(doc, "CONNECT_FAILED", "Could not send the datagram");
            return;
        }
        if (waitMs == 0) {
            udp.stop();
            _setBool(_succeed(doc), "sent", true);
            return;
        }

        uint8_t* reply = (uint8_t*)ps_malloc(UDP_MAX_REPLY_BYTES);
        if (reply == nullptr) {
            udp.stop();
            _fail(doc, "NO_MEMORY", "Could not allocate the reply buffer");
            return;
        }
        size_t replyLength = 0;
        bool gotReply = false;
        uint32_t deadline = millis() + waitMs;
        uint32_t loops = 0;
        const uint32_t maxLoops = _pollBound(waitMs, 10);
        while (_beforeDeadline(deadline) && loops++ < maxLoops) {
            if (udp.parsePacket() > 0) {
                if (udp.remoteIP() == target) {
                    replyLength = udp.read(reply, UDP_MAX_REPLY_BYTES);
                    gotReply = true;
                    break;
                }
                udp.flush(); // ignore datagrams from anyone else
            }
            delay(10);
        }
        udp.stop();

        if (!gotReply) {
            free(reply);
            _fail(doc, "NO_REPLY", "No datagram from the target within wait_ms");
            return;
        }
        JsonObject result = _succeed(doc);
        _setBool(result, "sent", true);
        _setBool(result, "truncated", replyLength >= UDP_MAX_REPLY_BYTES);
        bool encoded = _setBinary(result, "reply", reply, replyLength);
        free(reply);
        if (!encoded) {
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not encode the reply");
        }
    }

    static bool _readExact(WiFiClient& client, uint8_t* buffer, size_t length, uint32_t deadline, uint32_t maxLoops) {
        size_t got = 0;
        uint32_t loops = 0;
        while (got < length && _beforeDeadline(deadline) && loops++ < maxLoops) {
            if (client.available() > 0) {
                int n = client.read(buffer + got, length - got);
                if (n > 0) got += (size_t)n;
            } else if (!client.connected()) {
                break;
            } else {
                delay(5);
            }
        }
        return got == length;
    }

    // Parses and validates the Modbus arguments into `values` (writes) or `count`
    // (reads). Rejects on `doc` and returns false when invalid.
    static bool _readModbusArgs(JsonObjectConst args, JsonDocument& doc, bool write, int64_t* unit, uint8_t* fc,
                                int64_t* address, uint16_t** values, size_t* count) {
        int64_t function = 0;
        if (!_readInt(args, "unit", 1, unit) || *unit < 0 || *unit > 255 ||
            !_readInt(args, "function", -1, &function) || function < 0 || function > 255 ||
            !_readInt(args, "address", -1, address) || *address < 0 || *address > 0xFFFF) {
            _reject(doc, "BAD_ARGS", "unit 0-255, function and address 0-65535 are required");
            return false;
        }
        *fc = (uint8_t)function;

        if (!write) {
            int64_t requested = 0;
            if (!_readInt(args, "count", -1, &requested) || requested < 1 || requested > 2000 ||
                !GatewayLogic::isModbusReadFunction(*fc) || !GatewayLogic::isValidModbusReadCount(*fc, (uint32_t)requested)) {
                _reject(doc, "BAD_ARGS", "function must be 1-4 with count 1-2000 (bits) or 1-125 (registers)");
                return false;
            }
            *count = (size_t)requested;
            return true;
        }

        JsonArrayConst list = args["values"].as<JsonArrayConst>();
        if (!GatewayLogic::isModbusWriteFunction(*fc) || list.isNull() || !GatewayLogic::isValidModbusWriteCount(*fc, list.size())) {
            _reject(doc, "BAD_ARGS", "function must be 5, 6, 15 or 16 with a values array of a valid length");
            return false;
        }
        *values = (uint16_t*)ps_malloc(list.size() * sizeof(uint16_t));
        if (*values == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the values buffer");
            return false;
        }
        size_t i = 0;
        for (JsonVariantConst v : list) {
            if (!v.is<int64_t>() || !GatewayLogic::isValidModbusWriteValue(*fc, v.as<int64_t>())) {
                free(*values);
                *values = nullptr;
                _reject(doc, "BAD_ARGS", "values must be 0/1 for coils or 0-65535 for registers");
                return false;
            }
            (*values)[i++] = (uint16_t)v.as<int64_t>();
        }
        *count = list.size();
        return true;
    }

    static void _runModbus(JsonObjectConst args, JsonDocument& doc, const char* executionId, bool write) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        int64_t unit = 0, address = 0;
        uint8_t fc = 0;
        uint16_t* values = nullptr;
        size_t count = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 502, doc, &port) || !_readTimeout(args, doc, &timeoutMs) ||
            !_readModbusArgs(args, doc, write, &unit, &fc, &address, &values, &count)) return;

        uint16_t transactionId = (uint16_t)esp_random();
        uint8_t frame[GatewayLogic::MODBUS_MAX_FRAME];
        size_t frameLength = write
            ? GatewayLogic::buildModbusWriteRequest(frame, sizeof(frame), transactionId, (uint8_t)unit, fc, (uint16_t)address, values, count)
            : GatewayLogic::buildModbusReadRequest(frame, sizeof(frame), transactionId, (uint8_t)unit, fc, (uint16_t)address, (uint16_t)count);
        free(values);
        if (frameLength == 0) {
            _reject(doc, "BAD_ARGS", "Could not build the Modbus request");
            return;
        }

        LOG_INFO("Gateway %s: modbus_%s %s:%u unit %d fc %u addr %d n %u", executionId, write ? "write" : "read",
                 ip, port, (int)unit, fc, (int)address, (unsigned)count);

        uint32_t deadline = millis() + timeoutMs;
        WiFiClient client;
        if (!client.connect(IPAddress(octets[0], octets[1], octets[2], octets[3]), port, (int32_t)timeoutMs)) {
            _fail(doc, "CONNECT_FAILED", "TCP connect failed");
            return;
        }
        if (client.write(frame, frameLength) != frameLength) {
            client.stop();
            _fail(doc, "CONNECT_FAILED", "Could not send the Modbus request");
            return;
        }

        uint8_t response[GatewayLogic::MODBUS_MAX_FRAME];
        const uint32_t maxLoops = _pollBound(timeoutMs, 5);
        bool gotHeader = _readExact(client, response, GatewayLogic::MODBUS_LENGTH_PREFIX, deadline, maxLoops);
        size_t responseLength = gotHeader ? GatewayLogic::modbusFrameLength(response, GatewayLogic::MODBUS_LENGTH_PREFIX) : 0;
        bool gotFrame = responseLength != 0 &&
                        _readExact(client, response + GatewayLogic::MODBUS_LENGTH_PREFIX, responseLength - GatewayLogic::MODBUS_LENGTH_PREFIX, deadline, maxLoops);
        client.stop();
        if (!gotHeader) {
            _fail(doc, "TIMEOUT", "No Modbus response within timeout_ms");
            return;
        }
        if (!gotFrame) {
            _fail(doc, "BAD_RESPONSE", "Truncated or invalid Modbus frame");
            return;
        }

        uint16_t* decoded = write ? nullptr : (uint16_t*)ps_malloc(count * sizeof(uint16_t));
        if (!write && decoded == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the decode buffer");
            return;
        }
        size_t decodedCount = 0;
        uint8_t exception = 0;
        GatewayLogic::ModbusParse parsed = GatewayLogic::parseModbusResponse(
            response, responseLength, transactionId, (uint8_t)unit, fc, (uint16_t)count,
            decoded, decoded ? count : 0, &decodedCount, &exception);

        if (parsed == GatewayLogic::ModbusParse::EXCEPTION) {
            char description[32];
            snprintf(description, sizeof(description), "Modbus exception code %u", exception);
            _fail(doc, "MODBUS_EXCEPTION", description);
        } else if (parsed != GatewayLogic::ModbusParse::OK) {
            _fail(doc, "BAD_RESPONSE", "Modbus response does not match the request");
        } else if (write) {
            _setNumber(_succeed(doc), "written", (int64_t)decodedCount);
        } else {
            // At most 2000 values of up to 5 digits plus separators.
            size_t cap = decodedCount * 6 + 3;
            char* list = (char*)ps_malloc(cap);
            if (list == nullptr) {
                _fail(doc, "NO_MEMORY", "Could not allocate the values string");
            } else {
                size_t used = (size_t)snprintf(list, cap, "[");
                for (size_t i = 0; i < decodedCount && used < cap; i++) {
                    used += (size_t)snprintf(list + used, cap - used, i == 0 ? "%u" : ",%u", decoded[i]);
                }
                if (used < cap) snprintf(list + used, cap - used, "]");
                _setString(_succeed(doc), "values", list);
                free(list);
            }
        }
        free(decoded);
    }

    struct ScanHost {
        uint32_t ip; // network byte order, as lwIP stores it
        uint8_t mac[6];
    };

    static void _collectArpEntries(struct netif* scanNetif, ScanHost* hosts, size_t* hostCount) {
        for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
            ip4_addr_t* entryIp = nullptr;
            struct netif* entryNetif = nullptr;
            struct eth_addr* entryMac = nullptr;
            if (!etharp_get_entry(i, &entryIp, &entryNetif, &entryMac) || entryNetif != scanNetif) continue;
            uint32_t address = ip4_addr_get_u32(entryIp);
            bool known = false;
            for (size_t j = 0; j < *hostCount && !known; j++) known = hosts[j].ip == address;
            if (known || *hostCount >= GATEWAY_SCAN_MAX_HOSTS) continue;
            hosts[*hostCount].ip = address;
            memcpy(hosts[*hostCount].mac, entryMac->addr, 6);
            (*hostCount)++;
        }
    }

    enum class SweepResult { DONE, TIME_CAP, NETWORK_DOWN, INTERFACE_CHANGED, NO_MEMORY };

    // ARP-sweeps the /24 around the active interface's address.
    static SweepResult _sweepArp(ScanHost* hosts, size_t* hostCount) {
        LOCK_TCPIP_CORE();
        struct netif* scanNetif = netif_default;
        bool up = scanNetif != nullptr && netif_is_up(scanNetif) && netif_is_link_up(scanNetif) &&
                  !ip4_addr_isany_val(*netif_ip4_addr(scanNetif));
        uint32_t self = up ? lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(scanNetif))) : 0;
        uint32_t mask = up ? lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(scanNetif))) : 0;
        UNLOCK_TCPIP_CORE();
        if (!up) return SweepResult::NETWORK_DOWN;

        uint32_t* targets = (uint32_t*)ps_malloc(GATEWAY_SCAN_MAX_HOSTS * sizeof(uint32_t));
        if (targets == nullptr) return SweepResult::NO_MEMORY;
        size_t targetCount = GatewayLogic::buildScanTargets(self, mask, targets, GATEWAY_SCAN_MAX_HOSTS);

        SweepResult outcome = SweepResult::DONE;
        uint32_t start = millis();
        for (size_t offset = 0; offset < targetCount; offset += GATEWAY_SCAN_BATCH_SIZE) {
            if (millis() - start > GATEWAY_SCAN_MAX_DURATION_MS) {
                outcome = SweepResult::TIME_CAP;
                break;
            }
            LOCK_TCPIP_CORE();
            if (netif_default != scanNetif) {
                UNLOCK_TCPIP_CORE();
                outcome = SweepResult::INTERFACE_CHANGED;
                break;
            }
            for (size_t i = offset; i < offset + GATEWAY_SCAN_BATCH_SIZE && i < targetCount; i++) {
                ip4_addr_t address;
                ip4_addr_set_u32(&address, lwip_htonl(targets[i]));
                etharp_request(scanNetif, &address);
            }
            UNLOCK_TCPIP_CORE();
            delay(GATEWAY_SCAN_BATCH_WAIT_MS);
            LOCK_TCPIP_CORE();
            if (netif_default == scanNetif) _collectArpEntries(scanNetif, hosts, hostCount);
            UNLOCK_TCPIP_CORE();
        }
        free(targets);
        return outcome;
    }

    static bool _parseMdnsService(const char* text, char* service, size_t serviceSize, char* proto, size_t protoSize) {
        // "_http._tcp" -> "http", "tcp"
        if (text == nullptr || text[0] != '_') return false;
        const char* dot = strchr(text, '.');
        if (dot == nullptr || dot == text + 1 || (size_t)(dot - text - 1) >= serviceSize) return false;
        if (strcmp(dot, "._tcp") != 0 && strcmp(dot, "._udp") != 0) return false;
        snprintf(service, serviceSize, "%.*s", (int)(dot - text - 1), text + 1);
        snprintf(proto, protoSize, "%s", dot + 2);
        for (const char* p = service; *p; p++) {
            if (!isalnum((unsigned char)*p) && *p != '-') return false;
        }
        return true;
    }

    static void _runScan(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        char service[32] = "";
        char proto[8] = "";
        JsonVariantConst mdnsArg = args["mdns_service"];
        if (!mdnsArg.isNull() && !_parseMdnsService(mdnsArg.as<const char*>(), service, sizeof(service), proto, sizeof(proto))) {
            _reject(doc, "BAD_ARGS", "mdns_service must look like _http._tcp");
            return;
        }

        LOG_INFO("Gateway %s: net_scan%s%s", executionId, service[0] ? " + mDNS " : "", service[0] ? mdnsArg.as<const char*>() : "");

        ScanHost* hosts = (ScanHost*)ps_malloc(GATEWAY_SCAN_MAX_HOSTS * sizeof(ScanHost));
        if (hosts == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the scan buffer");
            return;
        }
        size_t hostCount = 0;
        SweepResult sweep = _sweepArp(hosts, &hostCount);
        if (sweep == SweepResult::NETWORK_DOWN || sweep == SweepResult::INTERFACE_CHANGED || sweep == SweepResult::NO_MEMORY) {
            free(hosts);
            if (sweep == SweepResult::NO_MEMORY) _fail(doc, "NO_MEMORY", "Could not allocate the scan targets");
            else _fail(doc, "NETWORK_DOWN", sweep == SweepResult::NETWORK_DOWN ? "No active network interface" : "Active interface changed during the scan");
            return;
        }

        SpiRamAllocator allocator;
        JsonDocument lists(&allocator);
        JsonArray hostList = lists["hosts"].to<JsonArray>();
        for (size_t i = 0; i < hostCount; i++) {
            ip4_addr_t address;
            ip4_addr_set_u32(&address, hosts[i].ip);
            char ipText[IP_ADDRESS_BUFFER_SIZE];
            char macText[13];
            const uint8_t* m = hosts[i].mac;
            snprintf(ipText, sizeof(ipText), "%s", ip4addr_ntoa(&address));
            snprintf(macText, sizeof(macText), "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            JsonObject entry = hostList.add<JsonObject>();
            entry["ip"] = ipText;
            entry["mac"] = macText;
        }
        free(hosts);

        bool truncated = sweep == SweepResult::TIME_CAP;
        JsonObject result = _succeed(doc);
        _setNumber(result, "host_count", (int64_t)hostCount);
        _setList(result, "hosts", hostList, &truncated);

        if (service[0] != '\0') {
            // Hostnames come from the network: ArduinoJson escapes them.
            JsonArray mdnsList = lists["mdns"].to<JsonArray>();
            int found = MDNS.queryService(service, proto);
            for (int i = 0; i < found; i++) {
                JsonObject entry = mdnsList.add<JsonObject>();
                entry["name"] = MDNS.hostname(i);
                entry["ip"] = MDNS.address(i).toString();
                entry["port"] = MDNS.port(i);
            }
            _setList(result, "mdns", mdnsList, &truncated);
        }
        _setBool(result, "truncated", truncated);
    }

    // Task plumbing
    // =============

    static void _run(const Request& request) {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        JsonDocument argsDoc(&allocator);
        if (deserializeJson(argsDoc, request.args) || !argsDoc.is<JsonObject>()) {
            _reject(doc, "BAD_ARGS", "args must be a JSON object");
        } else {
            JsonObjectConst args = argsDoc.as<JsonObjectConst>();
            switch (request.op) {
                case Op::HTTP_REQUEST: _runHttp(args, doc, request.executionId); break;
                case Op::PING: _runPing(args, doc, request.executionId); break;
                case Op::UDP_SEND: _runUdp(args, doc, request.executionId); break;
                case Op::MODBUS_READ: _runModbus(args, doc, request.executionId, false); break;
                case Op::MODBUS_WRITE: _runModbus(args, doc, request.executionId, true); break;
                case Op::NET_SCAN: _runScan(args, doc, request.executionId); break;
                default: _reject(doc, "UNKNOWN_OP", GatewayLogic::opName(request.op)); break;
            }
        }

        const char* status = doc["status"] | "FAILED";
        if (strcmp(status, "SUCCEEDED") == 0) {
            LOG_INFO("Gateway %s: %s succeeded", request.executionId, GatewayLogic::opName(request.op));
        } else {
            LOG_WARNING("Gateway %s: %s %s %s", request.executionId, GatewayLogic::opName(request.op), status,
                        doc["statusReason"]["reasonCode"] | "");
        }

        size_t length = measureJson(doc) + 1;
        char* json = (char*)ps_malloc(length);
        if (json != nullptr) serializeJson(doc, json, length);
        if (json != nullptr && acquireMutex(&_resultMutex)) {
            snprintf(_resultExecutionId, sizeof(_resultExecutionId), "%s", request.executionId);
            _resultJson = json;
            releaseMutex(&_resultMutex);
            return;
        }
        // Nothing to publish: drop the busy flag so the gateway does not stay stuck.
        free(json);
        LOG_ERROR("Gateway %s: could not store the result", request.executionId);
        _busy = false;
    }

    static void _gatewayTask(void* parameter) {
        (void)parameter;
        LOG_DEBUG("Gateway task started");
        bool shouldRun = true;
        while (shouldRun) {
            Request request;
            if (xQueueReceive(_queue, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
                _run(request);
                free(request.args);
            }
            if (ulTaskNotifyTake(pdTRUE, 0) > 0) shouldRun = false;
        }
        LOG_DEBUG("Gateway task stopping");
        _taskHandle = nullptr;
        vTaskDelete(NULL);
    }

    // Public API
    // ==========

    void begin() {
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, true)) {
            _enabled = preferences.getBool(GATEWAY_ENABLED_KEY, GATEWAY_ENABLED_DEFAULT);
            preferences.end();
        }

        if (!createMutexIfNeeded(&_stateMutex) || !createMutexIfNeeded(&_resultMutex)) {
            LOG_ERROR("Failed to create the gateway mutexes");
            return;
        }
        if (_queue == nullptr) _queue = xQueueCreate(1, sizeof(Request));
        if (_queue == nullptr) {
            LOG_ERROR("Failed to create the gateway queue");
            return;
        }
        if (_taskHandle == nullptr &&
            xTaskCreate(_gatewayTask, GATEWAY_TASK_NAME, GATEWAY_TASK_STACK_SIZE, nullptr, GATEWAY_TASK_PRIORITY, &_taskHandle) != pdPASS) {
            LOG_ERROR("Failed to create the gateway task");
            _taskHandle = nullptr;
            return;
        }
        LOG_INFO("Gateway ready (%s)", _enabled ? "enabled" : "disabled");
    }

    // Only called on the restart path. A request in flight can outlive the stop
    // timeout and be force-deleted; the restart's failsafe timer covers a socket
    // or lwIP lock left behind.
    void stop() {
        stopTaskGracefully(&_taskHandle, "gateway task");
    }

    bool isEnabled() { return _enabled; }

    void setEnabled(bool enabled) {
        if (!acquireMutex(&_stateMutex)) {
            LOG_ERROR("Failed to acquire the gateway state mutex");
            return;
        }
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false)) {
            preferences.putBool(GATEWAY_ENABLED_KEY, enabled);
            preferences.end();
        } else {
            LOG_ERROR("Failed to persist the gateway switch");
        }
        if (_enabled != enabled) LOG_INFO("Gateway %s", enabled ? "enabled" : "disabled");
        _enabled = enabled;
        releaseMutex(&_stateMutex);
    }

    SubmitResult submit(const char* executionId, Op op, JsonVariantConst args) {
        if (!_enabled || _queue == nullptr || _taskHandle == nullptr) return SubmitResult::NOT_ENABLED;

        char* argsJson = nullptr;
        if (args.is<JsonObjectConst>()) {
            size_t length = measureJson(args) + 1;
            argsJson = (char*)ps_malloc(length);
            if (argsJson == nullptr) return SubmitResult::NO_MEMORY;
            serializeJson(args, argsJson, length);
        } else {
            size_t length = 0;
            const char* encoded = args.is<const char*>() ? args.as<const char*>() : nullptr;
            if (encoded == nullptr || !_decodeBase64(encoded, (uint8_t**)&argsJson, &length, GATEWAY_ARGS_MAX_BYTES)) {
                return SubmitResult::BAD_ARGS;
            }
        }

        bool expected = false;
        if (!_busy.compare_exchange_strong(expected, true)) {
            free(argsJson);
            return SubmitResult::BUSY;
        }

        Request request = {};
        snprintf(request.executionId, sizeof(request.executionId), "%s", executionId);
        request.op = op;
        request.args = argsJson;
        if (xQueueSend(_queue, &request, 0) != pdTRUE) {
            free(argsJson);
            _busy = false;
            return SubmitResult::BUSY;
        }
        return SubmitResult::ACCEPTED;
    }

    bool takeResult(char* executionId, size_t executionIdSize, char** responseJson) {
        if (!_busy) return false;
        bool taken = false;
        if (acquireMutex(&_resultMutex)) {
            if (_resultJson != nullptr) {
                snprintf(executionId, executionIdSize, "%s", _resultExecutionId);
                *responseJson = _resultJson;
                _resultJson = nullptr;
                taken = true;
            }
            releaseMutex(&_resultMutex);
        }
        if (taken) _busy = false;
        return taken;
    }
}
