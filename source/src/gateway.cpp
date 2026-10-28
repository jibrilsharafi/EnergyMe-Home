// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway.h"

#include <atomic>

#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
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
        char* args; // PSRAM, owned by the gateway task once queued
    };

    static bool _enabled = GATEWAY_ENABLED_DEFAULT; // aligned bool: atomic read on the S3
    static TaskHandle_t _taskHandle = nullptr;
    static QueueHandle_t _queue = nullptr;
    static SemaphoreHandle_t _resultMutex = nullptr;

    // Set by submit(), cleared by takeResult(): covers both the queued and the
    // running request and the undrained result, so the single result slot is
    // never overwritten.
    static std::atomic<bool> _busy{false};
    static char* _resultExecutionId = nullptr;
    static char* _resultJson = nullptr;

    static void _gatewayTask(void* parameter);
    static void _run(const Request& request);

    // Response building
    // =================

    static void _reject(JsonDocument& doc, const char* code, const char* description) {
        doc["status"] = "REJECTED";
        doc["statusReason"]["reasonCode"] = code;
        if (description) doc["statusReason"]["reasonDescription"] = description;
    }

    static void _fail(JsonDocument& doc, const char* code, const char* description) {
        doc["status"] = "FAILED";
        doc["statusReason"]["reasonCode"] = code;
        if (description) doc["statusReason"]["reasonDescription"] = description;
    }

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
        if (mbedtls_base64_encode((unsigned char*)encoded, encodedLength + 1, &written, data, length) != 0) {
            free(encoded);
            return false;
        }
        encoded[written] = '\0';
        result[key]["bin"] = (const char*)encoded; // const char* -> ArduinoJson copies
        free(encoded);
        return true;
    }

    // Decodes a base64 string into a new PSRAM buffer (caller frees). Returns false on bad input.
    static bool _decodeBase64(const char* text, uint8_t** out, size_t* outLength, size_t maxLength) {
        size_t textLength = strlen(text);
        size_t decodedLength = 0;
        int rc = mbedtls_base64_decode(nullptr, 0, &decodedLength, (const unsigned char*)text, textLength);
        if (rc != 0 && rc != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) return false;
        if (decodedLength == 0 || decodedLength > maxLength) return false;
        uint8_t* buffer = (uint8_t*)ps_malloc(decodedLength);
        if (buffer == nullptr) return false;
        if (mbedtls_base64_decode(buffer, decodedLength, &decodedLength, (const unsigned char*)text, textLength) != 0) {
            free(buffer);
            return false;
        }
        *out = buffer;
        *outLength = decodedLength;
        return true;
    }

    // Argument helpers. Absent -> default; present with the wrong type -> false (BAD_ARGS).
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

    // Validates `ip`, and sets reject codes on `doc` when invalid.
    static bool _readTarget(JsonObjectConst args, JsonDocument& doc, const char** ip, uint8_t octets[4]) {
        JsonVariantConst value = args["ip"];
        if (!value.is<const char*>()) {
            _reject(doc, "BAD_ARGS", "ip must be an IPv4 string");
            return false;
        }
        *ip = value.as<const char*>();
        if (!GatewayLogic::isPrivateIpv4(*ip) || !GatewayLogic::parseIpv4(*ip, octets)) {
            _reject(doc, "TARGET_NOT_PRIVATE", "Target must be a private IPv4 literal");
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
        if (!_readInt(args, "timeout_ms", 0, &value)) {
            _reject(doc, "BAD_ARGS", "timeout_ms must be an integer");
            return false;
        }
        *timeoutMs = GatewayLogic::clampTimeoutMs(value);
        return true;
    }

    // Collects up to `cap` bytes; the first write that would overflow marks the
    // buffer full and returns short, which makes HTTPClient::writeToStream stop.
    class CappedBuffer : public Stream {
    public:
        CappedBuffer(uint8_t* buffer, size_t cap) : _buffer(buffer), _cap(cap) {}
        size_t write(uint8_t byte) override { return write(&byte, 1); }
        size_t write(const uint8_t* data, size_t size) override {
            size_t room = _cap - _length;
            size_t copy = size < room ? size : room;
            memcpy(_buffer + _length, data, copy);
            _length += copy;
            if (copy < size) _full = true;
            return copy;
        }
        int available() override { return 0; }
        int read() override { return -1; }
        int peek() override { return -1; }
        void flush() override {}
        size_t length() const { return _length; }
        bool full() const { return _full; }
    private:
        uint8_t* _buffer;
        size_t _cap;
        size_t _length = 0;
        bool _full = false;
    };

    // Primitives
    // ==========

    static void _runHttp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 80, doc, &port) || !_readTimeout(args, doc, &timeoutMs)) return;

        const char* method = args["method"].is<const char*>() ? args["method"].as<const char*>() : (args["method"].isNull() ? "GET" : nullptr);
        if (!GatewayLogic::isAllowedHttpMethod(method)) {
            _reject(doc, "BAD_ARGS", "method must be GET, POST, PUT, PATCH or DELETE");
            return;
        }
        const char* path = args["path"].is<const char*>() ? args["path"].as<const char*>() : (args["path"].isNull() ? "/" : nullptr);
        if (!GatewayLogic::isValidHttpPath(path)) {
            _reject(doc, "BAD_ARGS", "path must start with / and contain no spaces or control characters");
            return;
        }

        JsonVariantConst headers = args["headers"];
        if (!headers.isNull()) {
            if (!headers.is<JsonObjectConst>()) {
                _reject(doc, "BAD_ARGS", "headers must be an object of strings");
                return;
            }
            for (JsonPairConst header : headers.as<JsonObjectConst>()) {
                if (!header.value().is<const char*>() || !GatewayLogic::isValidHeaderName(header.key().c_str()) ||
                    !GatewayLogic::isValidHeaderValue(header.value().as<const char*>())) {
                    _reject(doc, "BAD_ARGS", "Invalid header name or value");
                    return;
                }
            }
        }

        uint8_t* body = nullptr;
        size_t bodyLength = 0;
        bool bodyOwned = false;
        if (args["body_b64"].is<const char*>()) {
            if (!_decodeBase64(args["body_b64"].as<const char*>(), &body, &bodyLength, GatewayLogic::RESPONSE_CAP_BYTES * 2)) {
                _reject(doc, "BAD_ARGS", "body_b64 is not valid base64 or is too large");
                return;
            }
            bodyOwned = true;
        } else if (args["body"].is<const char*>()) {
            body = (uint8_t*)args["body"].as<const char*>();
            bodyLength = strlen((const char*)body);
        } else if (!args["body"].isNull() || !args["body_b64"].isNull()) {
            _reject(doc, "BAD_ARGS", "body must be a string");
            return;
        }

        LOG_INFO("Gateway %s: http_request %s %s:%u%s", executionId, method, ip, port, path);

        uint8_t* responseBuffer = (uint8_t*)ps_malloc(GatewayLogic::RESPONSE_CAP_BYTES);
        if (responseBuffer == nullptr) {
            if (bodyOwned) free(body);
            _fail(doc, "NO_MEMORY", "Could not allocate the response buffer");
            return;
        }

        WiFiClient client;
        HTTPClient http;
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.setConnectTimeout((int32_t)timeoutMs);
        http.setTimeout((uint16_t)timeoutMs);

        if (!http.begin(client, ip, port, path, false)) {
            if (bodyOwned) free(body);
            free(responseBuffer);
            _fail(doc, "CONNECT_FAILED", "Could not prepare the HTTP request");
            return;
        }
        if (!headers.isNull()) {
            for (JsonPairConst header : headers.as<JsonObjectConst>()) {
                http.addHeader(header.key().c_str(), header.value().as<const char*>());
            }
        }

        int code = http.sendRequest(method, body, bodyLength);
        if (bodyOwned) free(body);

        if (code < 0) {
            _fail(doc, code == HTTPC_ERROR_READ_TIMEOUT ? "TIMEOUT" : "CONNECT_FAILED", HTTPClient::errorToString(code).c_str());
            http.end();
            free(responseBuffer);
            return;
        }

        // writeToStream errors out on an empty body once the server has closed the
        // connection (we send Connection: close), so an announced empty body is not read.
        CappedBuffer capped(responseBuffer, GatewayLogic::RESPONSE_CAP_BYTES);
        int announced = http.getSize();
        bool truncated = false;
        if (announced != 0) {
            int streamed = http.writeToStream(&capped);
            bool complete = announced > 0 && capped.length() == (size_t)announced;
            // A read timeout or a short write after the cap still leaves a usable prefix.
            truncated = capped.full() || (streamed < 0 && !complete);
        }
        http.end();

        JsonObject result = _succeed(doc);
        _setNumber(result, "status_code", code);
        if (!_setBinary(result, "body", responseBuffer, capped.length())) {
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not encode the response body");
        } else {
            _setBool(result, "truncated", truncated);
        }
        free(responseBuffer);
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
        if (!_readTarget(args, doc, &ip, octets) || !_readTimeout(args, doc, &timeoutMs)) return;
        if (!_readInt(args, "count", 0, &count)) {
            _reject(doc, "BAD_ARGS", "count must be an integer");
            return;
        }
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

    static void _runUdp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        int64_t wait = 0;
        if (!_readTarget(args, doc, &ip, octets)) return;
        if (args["port"].isNull()) {
            _reject(doc, "BAD_ARGS", "port is required");
            return;
        }
        if (!_readPort(args, 0, doc, &port)) return;
        if (!_readInt(args, "wait_ms", 0, &wait)) {
            _reject(doc, "BAD_ARGS", "wait_ms must be an integer");
            return;
        }
        uint32_t waitMs = GatewayLogic::clampUdpWaitMs(wait);

        uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        if (!args["payload_b64"].is<const char*>() ||
            !_decodeBase64(args["payload_b64"].as<const char*>(), &payload, &payloadLength, 1472)) {
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

        uint8_t* reply = (uint8_t*)ps_malloc(GatewayLogic::RESPONSE_CAP_BYTES);
        if (reply == nullptr) {
            udp.stop();
            _fail(doc, "NO_MEMORY", "Could not allocate the reply buffer");
            return;
        }
        size_t replyLength = 0;
        bool gotReply = false;
        uint32_t start = millis();
        uint32_t loops = 0;
        while (millis() - start < waitMs && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            int size = udp.parsePacket();
            if (size > 0) {
                if (udp.remoteIP() == target) {
                    replyLength = udp.read(reply, GatewayLogic::RESPONSE_CAP_BYTES);
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
        bool encoded = _setBinary(result, "reply", reply, replyLength);
        free(reply);
        if (!encoded) {
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not encode the reply");
        }
    }

    static bool _readExact(WiFiClient& client, uint8_t* buffer, size_t length, uint32_t deadline) {
        size_t got = 0;
        uint32_t loops = 0;
        while (got < length && (int32_t)(deadline - millis()) > 0 && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            int available = client.available();
            if (available > 0) {
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

    static void _runModbus(JsonObjectConst args, JsonDocument& doc, const char* executionId, bool write) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        int64_t unit = 0, function = 0, address = 0, count = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 502, doc, &port) || !_readTimeout(args, doc, &timeoutMs)) return;
        if (!_readInt(args, "unit", 1, &unit) || unit < 0 || unit > 255 ||
            !_readInt(args, "function", -1, &function) || !_readInt(args, "address", -1, &address) ||
            address < 0 || address > 0xFFFF) {
            _reject(doc, "BAD_ARGS", "unit 0-255, function and address 0-65535 are required");
            return;
        }
        uint8_t fc = (function >= 0 && function <= 255) ? (uint8_t)function : 0;

        uint16_t* values = nullptr;
        size_t valueCount = 0;
        if (write) {
            JsonArrayConst list = args["values"].as<JsonArrayConst>();
            valueCount = list.size();
            if (!GatewayLogic::isModbusWriteFunction(fc) || list.isNull() || !GatewayLogic::isValidModbusWriteCount(fc, valueCount)) {
                _reject(doc, "BAD_ARGS", "function must be 5, 6, 15 or 16 with a values array of a valid length");
                return;
            }
            values = (uint16_t*)ps_malloc(valueCount * sizeof(uint16_t));
            if (values == nullptr) {
                _fail(doc, "NO_MEMORY", "Could not allocate the values buffer");
                return;
            }
            size_t i = 0;
            for (JsonVariantConst v : list) {
                if (!v.is<int64_t>() || !GatewayLogic::isValidModbusWriteValue(fc, v.as<int64_t>())) {
                    free(values);
                    _reject(doc, "BAD_ARGS", "values must be 0/1 for coils or 0-65535 for registers");
                    return;
                }
                values[i++] = (uint16_t)v.as<int64_t>();
            }
        } else {
            if (!_readInt(args, "count", -1, &count) || !GatewayLogic::isModbusReadFunction(fc) ||
                count < 0 || !GatewayLogic::isValidModbusReadCount(fc, (uint32_t)count)) {
                _reject(doc, "BAD_ARGS", "function must be 1-4 with count 1-2000 (bits) or 1-125 (registers)");
                return;
            }
            valueCount = (size_t)count;
        }

        uint16_t transactionId = (uint16_t)esp_random();
        uint8_t frame[GatewayLogic::MODBUS_MAX_FRAME];
        size_t frameLength = write
            ? GatewayLogic::buildModbusWriteRequest(frame, sizeof(frame), transactionId, (uint8_t)unit, fc, (uint16_t)address, values, valueCount)
            : GatewayLogic::buildModbusReadRequest(frame, sizeof(frame), transactionId, (uint8_t)unit, fc, (uint16_t)address, (uint16_t)valueCount);
        if (values) free(values);
        if (frameLength == 0) {
            _reject(doc, "BAD_ARGS", "Could not build the Modbus request");
            return;
        }

        LOG_INFO("Gateway %s: modbus_%s %s:%u unit %d fc %u addr %d n %u", executionId, write ? "write" : "read",
                 ip, port, (int)unit, fc, (int)address, (unsigned)valueCount);

        WiFiClient client;
        if (!client.connect(IPAddress(octets[0], octets[1], octets[2], octets[3]), port, (int32_t)timeoutMs)) {
            _fail(doc, "CONNECT_FAILED", "TCP connect failed");
            return;
        }
        uint32_t deadline = millis() + timeoutMs;
        if (client.write(frame, frameLength) != frameLength) {
            client.stop();
            _fail(doc, "CONNECT_FAILED", "Could not send the Modbus request");
            return;
        }

        uint8_t response[GatewayLogic::MODBUS_MAX_FRAME];
        size_t responseLength = 0;
        if (_readExact(client, response, 6, deadline)) {
            responseLength = GatewayLogic::modbusFrameLength(response, 6);
            if (responseLength == 0 || !_readExact(client, response + 6, responseLength - 6, deadline)) responseLength = 0;
        } else {
            client.stop();
            _fail(doc, "TIMEOUT", "No Modbus response within timeout_ms");
            return;
        }
        client.stop();
        if (responseLength == 0) {
            _fail(doc, "BAD_RESPONSE", "Truncated or invalid Modbus frame");
            return;
        }

        uint16_t* decoded = write ? nullptr : (uint16_t*)ps_malloc(valueCount * sizeof(uint16_t));
        if (!write && decoded == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the decode buffer");
            return;
        }
        size_t decodedCount = 0;
        uint8_t exception = 0;
        GatewayLogic::ModbusParse parsed = GatewayLogic::parseModbusResponse(
            response, responseLength, transactionId, (uint8_t)unit, fc, (uint16_t)valueCount,
            decoded, decoded ? valueCount : 0, &decodedCount, &exception);

        if (parsed == GatewayLogic::ModbusParse::EXCEPTION) {
            if (decoded) free(decoded);
            char description[32];
            snprintf(description, sizeof(description), "Modbus exception code %u", exception);
            _fail(doc, "MODBUS_EXCEPTION", description);
            return;
        }
        if (parsed != GatewayLogic::ModbusParse::OK) {
            if (decoded) free(decoded);
            _fail(doc, "BAD_RESPONSE", "Modbus response does not match the request");
            return;
        }

        JsonObject result = _succeed(doc);
        if (write) {
            _setNumber(result, "written", (int64_t)decodedCount);
            return;
        }

        // At most 2000 values of up to 5 digits plus separators: well under 16 KB.
        size_t cap = decodedCount * 6 + 3;
        char* list = (char*)ps_malloc(cap);
        if (list == nullptr) {
            free(decoded);
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not allocate the values string");
            return;
        }
        size_t used = (size_t)snprintf(list, cap, "[");
        for (size_t i = 0; i < decodedCount && used < cap; i++) {
            used += (size_t)snprintf(list + used, cap - used, i == 0 ? "%u" : ",%u", decoded[i]);
        }
        if (used < cap) snprintf(list + used, cap - used, "]");
        _setString(result, "values", list);
        free(list);
        free(decoded);
    }

    struct ScanHost {
        uint32_t ip; // network byte order, as lwIP stores it
        uint8_t mac[6];
    };

    static void _collectArpEntries(struct netif* scanNetif, ScanHost* hosts, size_t* hostCount, size_t cap) {
        for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
            ip4_addr_t* entryIp = nullptr;
            struct netif* entryNetif = nullptr;
            struct eth_addr* entryMac = nullptr;
            if (!etharp_get_entry(i, &entryIp, &entryNetif, &entryMac) || entryNetif != scanNetif) continue;
            uint32_t address = ip4_addr_get_u32(entryIp);
            bool known = false;
            for (size_t j = 0; j < *hostCount; j++) {
                if (hosts[j].ip == address) {
                    known = true;
                    break;
                }
            }
            if (known || *hostCount >= cap) continue;
            hosts[*hostCount].ip = address;
            memcpy(hosts[*hostCount].mac, entryMac->addr, 6);
            (*hostCount)++;
        }
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

    // Raw size budget for each JSON list so the whole response stays well inside the 9 KB MQTT buffer.
    static constexpr size_t SCAN_LIST_BUDGET = 3500;

    static void _runScan(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        char service[32] = "";
        char proto[8] = "";
        JsonVariantConst mdnsArg = args["mdns_service"];
        if (!mdnsArg.isNull() && !_parseMdnsService(mdnsArg.as<const char*>(), service, sizeof(service), proto, sizeof(proto))) {
            _reject(doc, "BAD_ARGS", "mdns_service must look like _http._tcp");
            return;
        }

        LOCK_TCPIP_CORE();
        struct netif* scanNetif = netif_default;
        bool up = scanNetif != nullptr && netif_is_up(scanNetif) && netif_is_link_up(scanNetif) &&
                  !ip4_addr_isany_val(*netif_ip4_addr(scanNetif));
        uint32_t self = up ? lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(scanNetif))) : 0;
        uint32_t mask = up ? lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(scanNetif))) : 0;
        UNLOCK_TCPIP_CORE();
        if (!up) {
            _fail(doc, "NETWORK_DOWN", "No active network interface");
            return;
        }

        uint32_t* targets = (uint32_t*)ps_malloc(254 * sizeof(uint32_t));
        ScanHost* hosts = (ScanHost*)ps_malloc(254 * sizeof(ScanHost));
        if (targets == nullptr || hosts == nullptr) {
            if (targets) free(targets);
            if (hosts) free(hosts);
            _fail(doc, "NO_MEMORY", "Could not allocate the scan buffers");
            return;
        }
        size_t targetCount = GatewayLogic::buildScanTargets(self, mask, targets, 254);
        size_t hostCount = 0;

        LOG_INFO("Gateway %s: net_scan %u hosts%s%s", executionId, (unsigned)targetCount, service[0] ? " + mDNS " : "", mdnsArg.isNull() ? "" : mdnsArg.as<const char*>());

        uint32_t start = millis();
        bool interfaceChanged = false;
        for (size_t offset = 0; offset < targetCount; offset += GATEWAY_SCAN_BATCH_SIZE) {
            if (millis() - start > GATEWAY_SCAN_MAX_DURATION_MS) break;
            LOCK_TCPIP_CORE();
            if (netif_default != scanNetif) {
                interfaceChanged = true;
                UNLOCK_TCPIP_CORE();
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
            if (netif_default == scanNetif) _collectArpEntries(scanNetif, hosts, &hostCount, 254);
            UNLOCK_TCPIP_CORE();
        }
        free(targets);

        if (interfaceChanged) {
            free(hosts);
            _fail(doc, "NETWORK_DOWN", "Active interface changed during the scan");
            return;
        }

        char* list = (char*)ps_malloc(SCAN_LIST_BUDGET + 64);
        if (list == nullptr) {
            free(hosts);
            _fail(doc, "NO_MEMORY", "Could not allocate the host list");
            return;
        }
        bool truncated = false;
        size_t used = (size_t)snprintf(list, SCAN_LIST_BUDGET, "[");
        for (size_t i = 0; i < hostCount; i++) {
            ip4_addr_t address;
            ip4_addr_set_u32(&address, hosts[i].ip);
            const uint8_t* m = hosts[i].mac;
            char entry[64];
            int n = snprintf(entry, sizeof(entry), "%s{\"ip\":\"%s\",\"mac\":\"%02x%02x%02x%02x%02x%02x\"}",
                             i == 0 ? "" : ",", ip4addr_ntoa(&address), m[0], m[1], m[2], m[3], m[4], m[5]);
            if (used + (size_t)n + 2 > SCAN_LIST_BUDGET) {
                truncated = true;
                break;
            }
            memcpy(list + used, entry, (size_t)n);
            used += (size_t)n;
        }
        snprintf(list + used, SCAN_LIST_BUDGET + 64 - used, "]");
        free(hosts);

        JsonObject result = _succeed(doc);
        _setString(result, "hosts", list);
        _setNumber(result, "host_count", (int64_t)hostCount);

        if (service[0] != '\0') {
            int found = MDNS.queryService(service, proto);
            used = (size_t)snprintf(list, SCAN_LIST_BUDGET, "[");
            for (int i = 0; i < found; i++) {
                char entry[160];
                int n = snprintf(entry, sizeof(entry), "%s{\"name\":\"%s\",\"ip\":\"%s\",\"port\":%u}", i == 0 ? "" : ",",
                                 MDNS.hostname(i).c_str(), MDNS.address(i).toString().c_str(), MDNS.port(i));
                if (n < 0 || (size_t)n >= sizeof(entry) || used + (size_t)n + 2 > SCAN_LIST_BUDGET) {
                    truncated = true;
                    break;
                }
                memcpy(list + used, entry, (size_t)n);
                used += (size_t)n;
            }
            snprintf(list + used, SCAN_LIST_BUDGET + 64 - used, "]");
            _setString(result, "mdns", list);
        }
        _setBool(result, "truncated", truncated);
        free(list);
    }

    // Task plumbing
    // =============

    static void _run(const Request& request) {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        JsonDocument argsDoc(&allocator);
        DeserializationError error = deserializeJson(argsDoc, request.args);
        if (error || !argsDoc.is<JsonObject>()) {
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
        const char* reason = doc["statusReason"]["reasonCode"] | "";
        if (strcmp(status, "SUCCEEDED") == 0) {
            LOG_INFO("Gateway %s: %s succeeded", request.executionId, GatewayLogic::opName(request.op));
        } else {
            LOG_WARNING("Gateway %s: %s %s %s", request.executionId, GatewayLogic::opName(request.op), status, reason);
        }

        size_t length = measureJson(doc) + 1;
        char* json = (char*)ps_malloc(length);
        char* executionId = (char*)ps_malloc(strlen(request.executionId) + 1);
        if (json == nullptr || executionId == nullptr) {
            // Nothing to publish: drop the busy flag so the device does not stay stuck.
            if (json) free(json);
            if (executionId) free(executionId);
            LOG_ERROR("Gateway %s: could not allocate the response", request.executionId);
            _busy = false;
            return;
        }
        serializeJson(doc, json, length);
        snprintf(executionId, strlen(request.executionId) + 1, "%s", request.executionId);

        if (acquireMutex(&_resultMutex)) {
            _resultExecutionId = executionId;
            _resultJson = json;
            releaseMutex(&_resultMutex);
        } else {
            free(json);
            free(executionId);
            LOG_ERROR("Gateway %s: could not store the result", request.executionId);
            _busy = false;
        }
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

        if (!createMutexIfNeeded(&_resultMutex)) {
            LOG_ERROR("Failed to create the gateway result mutex");
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

    void stop() {
        stopTaskGracefully(&_taskHandle, "gateway task");
    }

    bool isEnabled() { return _enabled; }

    void setEnabled(bool enabled) {
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false)) {
            preferences.putBool(GATEWAY_ENABLED_KEY, enabled);
            preferences.end();
        } else {
            LOG_ERROR("Failed to persist the gateway switch");
        }
        if (_enabled != enabled) LOG_INFO("Gateway %s", enabled ? "enabled" : "disabled");
        _enabled = enabled;
    }

    SubmitResult submit(const char* executionId, Op op, const char* argsJson) {
        if (!_enabled) return SubmitResult::NOT_ENABLED;
        if (_queue == nullptr || _taskHandle == nullptr) return SubmitResult::NOT_ENABLED;

        bool expected = false;
        if (!_busy.compare_exchange_strong(expected, true)) return SubmitResult::BUSY;

        Request request = {};
        snprintf(request.executionId, sizeof(request.executionId), "%s", executionId);
        request.op = op;
        size_t length = strlen(argsJson) + 1;
        request.args = (char*)ps_malloc(length);
        if (request.args == nullptr) {
            _busy = false;
            return SubmitResult::NO_MEMORY;
        }
        memcpy(request.args, argsJson, length);

        if (xQueueSend(_queue, &request, 0) != pdTRUE) {
            free(request.args);
            _busy = false;
            return SubmitResult::BUSY;
        }
        return SubmitResult::ACCEPTED;
    }

    bool takeResult(char** executionId, char** responseJson) {
        if (!_busy) return false;
        bool taken = false;
        if (acquireMutex(&_resultMutex)) {
            if (_resultJson != nullptr) {
                *executionId = _resultExecutionId;
                *responseJson = _resultJson;
                _resultExecutionId = nullptr;
                _resultJson = nullptr;
                taken = true;
            }
            releaseMutex(&_resultMutex);
        }
        if (taken) _busy = false;
        return taken;
    }
}
