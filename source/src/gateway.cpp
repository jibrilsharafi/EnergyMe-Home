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
#include "collector.h"
#include "gateway_executor.h"
#include "gateway_stream.h"
#include "secrets.h"
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

    static bool _setBinary(JsonObject result, const char* key, const uint8_t* data, size_t length) {
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

    // UTF-8 text goes out as a plain string so the cloud can read it without decoding; anything else stays base64.
    // AWS rejects the whole response when a value is empty (no "s": "" or zero-byte "bin"), so an empty payload omits the key.
    static bool _setPayload(JsonObject result, const char* key, const uint8_t* data, size_t length, bool truncated) {
        if (length == 0) return true;
        size_t textLength = 0;
        if (GatewayLogic::utf8TextLength(data, length, truncated, &textLength)) {
            return result[key]["s"].set(JsonString((const char*)data, textLength)); // not static -> ArduinoJson copies
        }
        return _setBinary(result, key, data, length);
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

    TargetCheck validateTarget(const char* ip, uint8_t octets[4]) {
        if (!GatewayLogic::parseIpv4(ip, octets) || !GatewayLogic::isPrivateIpv4(octets)) return TargetCheck::NOT_PRIVATE;
        if (_isLocalOrBroadcast(octets)) return TargetCheck::NOT_ALLOWED;
        return TargetCheck::OK;
    }

    const char* targetCheckDescription(TargetCheck check) {
        switch (check) {
            case TargetCheck::NOT_PRIVATE: return "Target must be a private IPv4 literal";
            case TargetCheck::NOT_ALLOWED: return "Target is this device or a network/broadcast address";
            default: return nullptr;
        }
    }

    const char* targetCheckReasonCode(TargetCheck check) {
        switch (check) {
            case TargetCheck::NOT_PRIVATE: return "TARGET_NOT_PRIVATE";
            case TargetCheck::NOT_ALLOWED: return "TARGET_NOT_ALLOWED";
            default: return nullptr;
        }
    }

    static bool _readTarget(JsonObjectConst args, JsonDocument& doc, const char** ip, uint8_t octets[4]) {
        *ip = _readString(args, "ip", nullptr);
        if (*ip == nullptr) {
            _reject(doc, "BAD_ARGS", "ip must be an IPv4 string");
            return false;
        }
        TargetCheck check = validateTarget(*ip, octets);
        if (check == TargetCheck::OK) return true;
        _reject(doc, targetCheckReasonCode(check), targetCheckDescription(check));
        return false;
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

    // Command adapter
    // ===============
    //
    // Each legacy op is parsed with its previous argument rules and messages, run
    // through the executor, and mapped back to the same result keys, so a command
    // without the new optional arguments replies byte for byte as before.

    static constexpr size_t COMMAND_MAX_BYTES = GatewayLogic::RESPONSE_CAP_BYTES;
    static constexpr size_t HTTP_REQUEST_MAX_BYTES = GatewayLogic::GATEWAY_REQUEST_MAX;

    // Optional `access`: absent, "read" or "write".
    static bool _readAccess(JsonObjectConst args, JsonDocument& doc, GatewayLogic::Access* access) {
        const char* text = _readString(args, "access", nullptr);
        if (args["access"].isNull()) {
            *access = GatewayLogic::Access::NONE;
        } else if (text != nullptr && strcmp(text, "read") == 0) {
            *access = GatewayLogic::Access::READ;
        } else if (text != nullptr && strcmp(text, "write") == 0) {
            *access = GatewayLogic::Access::WRITE;
        } else {
            _reject(doc, "BAD_ARGS", "access must be read or write");
            return false;
        }
        return true;
    }

    // Optional `mac` of the target, lowercase aa:bb:cc:dd:ee:ff.
    static bool _readMac(JsonObjectConst args, JsonDocument& doc, GatewayExec::Request& rq) {
        if (args["mac"].isNull()) return true;
        const char* text = _readString(args, "mac", nullptr);
        if (text != nullptr && GatewayLogic::parseMac(text, rq.mac)) {
            rq.hasMac = true;
            return true;
        }
        _reject(doc, "BAD_ARGS", "mac must be six lowercase hex pairs, unicast and non-zero");
        return false;
    }

    static bool _readFlag(JsonObjectConst args, const char* key, JsonDocument& doc, bool* out) {
        JsonVariantConst value = args[key];
        if (value.isNull()) {
            *out = false;
            return true;
        }
        if (!value.is<bool>()) {
            char description[48];
            snprintf(description, sizeof(description), "%s must be a boolean", key);
            _reject(doc, "BAD_ARGS", description);
            return false;
        }
        *out = value.as<bool>();
        return true;
    }

    static void _setValues(JsonObject result, const char* key, const uint16_t* values, size_t count, JsonDocument& doc) {
        // At most 2000 values of up to 5 digits plus separators.
        size_t cap = count * 6 + 3;
        char* list = (char*)ps_malloc(cap);
        if (list == nullptr) {
            doc.clear();
            _fail(doc, "NO_MEMORY", "Could not allocate the values string");
            return;
        }
        size_t used = (size_t)snprintf(list, cap, "[");
        for (size_t i = 0; i < count && used < cap; i++) {
            used += (size_t)snprintf(list + used, cap - used, i == 0 ? "%u" : ",%u", values[i]);
        }
        if (used < cap) snprintf(list + used, cap - used, "]");
        _setString(result, key, list);
        free(list);
    }

    // Non-OK results keep their status and reason; true when OK.
    static bool _mapStatus(const GatewayExec::Result& result, JsonDocument& doc) {
        if (result.status == GatewayExec::Status::OK) return true;
        const char* detail = result.reasonDetail[0] != '\0' ? result.reasonDetail : nullptr;
        if (result.status == GatewayExec::Status::REJECTED) _reject(doc, result.reasonCode, detail);
        else _fail(doc, result.reasonCode, detail);
        return false;
    }

    static void _setPayloadOrFail(JsonObject result, const char* key, const uint8_t* data, size_t length, bool truncated, JsonDocument& doc,
                                  const char* failure) {
        if (!_setPayload(result, key, data, length, truncated)) {
            doc.clear();
            _fail(doc, "NO_MEMORY", failure);
        }
    }

    static void _runHttp(JsonObjectConst args, JsonDocument& doc, const char* executionId, bool https) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, https ? 443 : 80, doc, &port) || !_readTimeout(args, doc, &timeoutMs)) return;

        GatewayExec::Request rq;
        rq.module = https ? GatewayLogic::Module::HTTPS : GatewayLogic::Module::HTTP;
        rq.ip = ip;
        rq.port = port;
        rq.timeoutMs = timeoutMs;
        rq.maxBytes = COMMAND_MAX_BYTES;
        rq.callerId = executionId;

        rq.method = _readString(args, "method", "GET");
        if (!GatewayLogic::isAllowedHttpMethod(rq.method)) {
            _reject(doc, "BAD_ARGS", "method must be GET, POST, PUT, PATCH or DELETE");
            return;
        }
        rq.path = _readString(args, "path", "/");
        if (!GatewayLogic::isValidHttpPath(rq.path)) {
            _reject(doc, "BAD_ARGS", "path must start with / and contain no spaces or control characters");
            return;
        }
        if (!_readMac(args, doc, rq)) return;

        JsonVariantConst headerArg = args["headers"];
        if (!headerArg.isNull() && !headerArg.is<JsonObjectConst>()) {
            _reject(doc, "BAD_ARGS", "headers must be an object of strings");
            return;
        }
        JsonObjectConst headerObject = headerArg.as<JsonObjectConst>();
        GatewayExec::Header* headers = headerObject.size() > 0 ? (GatewayExec::Header*)ps_malloc(headerObject.size() * sizeof(GatewayExec::Header)) : nullptr;
        if (headerObject.size() > 0 && headers == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the header list");
            return;
        }
        size_t headerCount = 0;
        for (JsonPairConst header : headerObject) {
            const char* name = header.key().c_str();
            const char* value = header.value().as<const char*>();
            if (!header.value().is<const char*>() || !GatewayLogic::isValidHeaderName(name) || !GatewayLogic::isValidHeaderValue(value)) {
                free(headers);
                _reject(doc, "BAD_ARGS", "Invalid header name or value");
                return;
            }
            if (GatewayLogic::isReservedHeaderName(name)) {
                free(headers);
                _reject(doc, "BAD_ARGS", "Host, Content-Length, Transfer-Encoding and Connection are set by the device");
                return;
            }
            headers[headerCount++] = {name, value};
        }
        rq.headers = headers;
        rq.headerCount = headerCount;

        const char* bodyB64 = _readString(args, "body_b64", nullptr);
        const char* bodyText = _readString(args, "body", nullptr);
        if ((!args["body_b64"].isNull() && bodyB64 == nullptr) || (!args["body"].isNull() && bodyText == nullptr)) {
            free(headers);
            _reject(doc, "BAD_ARGS", "body and body_b64 must be strings");
            return;
        }
        uint8_t* decodedBody = nullptr;
        size_t decodedLength = 0;
        if (bodyB64 != nullptr) {
            if (!_decodeBase64(bodyB64, &decodedBody, &decodedLength, HTTP_REQUEST_MAX_BYTES)) {
                free(headers);
                _reject(doc, "BAD_ARGS", "body_b64 is not valid base64 or is too large");
                return;
            }
            rq.bodyBytes = decodedBody;
            rq.bodyBytesLength = decodedLength;
        } else {
            rq.body = bodyText;
        }

        bool returnHeaders = false;
        if (!_readAccess(args, doc, &rq.access) || !_readFlag(args, "return_headers", doc, &returnHeaders)) {
            free(headers);
            free(decodedBody);
            return;
        }
        if (https) {
            rq.certSha256 = _readString(args, "cert_sha256", nullptr);
            uint8_t pin[GatewayLogic::DIGEST_BYTES];
            if (!GatewayLogic::parseHexDigest(rq.certSha256, pin)) {
                free(headers);
                free(decodedBody);
                _reject(doc, "BAD_ARGS", "cert_sha256 must be 64 lowercase hex");
                return;
            }
        }

        Secrets::RequestState secretState;
        GatewayLogic::Resolver chain[] = {Secrets::resolver(&secretState)};
        rq.resolvers = chain;
        rq.resolverCount = 1;
        GatewayExec::Result result;
        GatewayExec::run(rq, result);
        free(headers);
        free(decodedBody);
        if (!_mapStatus(result, doc)) return;

        JsonObject out = _succeed(doc);
        _setNumber(out, "status_code", result.protoStatus);
        _setBool(out, "truncated", result.truncated);
        if (result.redacted) _setString(out, "redacted", "true");
        // Same s/bin rule as the body: a non-UTF-8 byte in a header would make AWS refuse the whole response.
        if (returnHeaders) _setPayloadOrFail(out, "headers", (const uint8_t*)result.headers, result.headersLength, false, doc, "Could not encode the headers");
        if (doc["status"] == "SUCCEEDED") {
            _setPayloadOrFail(out, "body", result.body, result.bodyLength, result.truncated, doc, "Could not encode the response body");
        }
    }

    static void _runTlsProbe(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 443, doc, &port) || !_readTimeout(args, doc, &timeoutMs)) return;

        GatewayExec::Request rq;
        rq.module = GatewayLogic::Module::HTTPS;
        rq.ip = ip;
        rq.port = port;
        rq.timeoutMs = timeoutMs;
        rq.callerId = executionId;
        rq.probeOnly = true;
        if (!_readMac(args, doc, rq)) return;
        GatewayExec::Result result;
        GatewayExec::run(rq, result);
        if (!_mapStatus(result, doc)) return;
        if (!result.hasCert) {
            _fail(doc, "TLS_FAILED", "No peer certificate");
            return;
        }
        char sha[GatewayLogic::DIGEST_BYTES * 2 + 1];
        GatewayLogic::formatHexDigest(result.cert.sha256, sha, sizeof(sha));
        JsonObject out = _succeed(doc);
        _setString(out, "cert_sha256", sha);
        if (result.cert.subjectCn[0] != '\0') _setString(out, "subject_cn", result.cert.subjectCn);
        _setNumber(out, "not_after", (int64_t)result.cert.notAfter);
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

    static constexpr uint32_t PING_REPLY_TIMEOUT_MAX_MS = 1000;
    static constexpr uint32_t PING_INTERVAL_MS = 200;
    static constexpr uint32_t PING_END_GRACE_MS = 1000;

    static void _runPing(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint32_t timeoutMs = 0;
        int64_t count = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readTimeout(args, doc, &timeoutMs) || !_requireInt(args, "count", 0, doc, &count)) return;
        uint32_t pings = GatewayLogic::clampPingCount(count);
        uint32_t perPingTimeoutMs = timeoutMs < PING_REPLY_TIMEOUT_MAX_MS ? timeoutMs : PING_REPLY_TIMEOUT_MAX_MS;

        LOG_DEBUG("Gateway %s: ping %s x%u", executionId, ip, pings);

        PingContext context = {xSemaphoreCreateBinary(), 0, 0};
        if (context.done == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not create the ping semaphore");
            return;
        }

        esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
        IP_ADDR4(&config.target_addr, octets[0], octets[1], octets[2], octets[3]);
        config.count = pings;
        config.interval_ms = PING_INTERVAL_MS;
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
        uint32_t waitMs = pings * (perPingTimeoutMs + PING_INTERVAL_MS) + PING_END_GRACE_MS;
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

    static void _mapSecretOutcome(const Secrets::Outcome& outcome, JsonDocument& doc) {
        const char* detail = outcome.reasonDetail[0] != '\0' ? outcome.reasonDetail : nullptr;
        if (strcmp(outcome.status, "REJECTED") == 0) _reject(doc, outcome.reasonCode, detail);
        else _fail(doc, outcome.reasonCode, detail);
    }

    static void _runSecretSet(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        Secrets::Outcome outcome;
        Secrets::setFromEnvelope(args, outcome);
        LOG_INFO("Gateway %s: secret_set %s", executionId, outcome.name);
        if (strcmp(outcome.status, "SUCCEEDED") != 0) {
            _mapSecretOutcome(outcome, doc);
            return;
        }
        JsonObject out = _succeed(doc);
        _setString(out, "name", outcome.name);
        _setNumber(out, "set_at", (int64_t)outcome.setAt);
        _setString(out, "applied", outcome.applied ? "true" : "false");
    }

    static void _runSecretDelete(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        Secrets::Outcome outcome;
        Secrets::remove(args, outcome);
        LOG_INFO("Gateway %s: secret_delete %s", executionId, outcome.name);
        if (strcmp(outcome.status, "SUCCEEDED") != 0) {
            _mapSecretOutcome(outcome, doc);
            return;
        }
        _setString(_succeed(doc), "deleted", outcome.deleted ? "true" : "false");
    }

    // NetworkUDP buffers one MTU of payload: it reads at most 1460 bytes, and a
    // longer write is split into two datagrams.
    static constexpr size_t UDP_MAX_DATAGRAM_BYTES = GatewayExec::UDP_REPLY_MAX;

    static void _runUdp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        int64_t wait = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 0, doc, &port) || !_requireInt(args, "wait_ms", 0, doc, &wait)) return;

        GatewayExec::Request rq;
        rq.module = GatewayLogic::Module::UDP;
        rq.ip = ip;
        rq.port = port;
        rq.waitMs = GatewayLogic::clampUdpWaitMs(wait);
        rq.callerId = executionId;
        if (!_readMac(args, doc, rq)) return;

        uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        const char* payloadB64 = _readString(args, "payload_b64", nullptr);
        if (payloadB64 == nullptr || !_decodeBase64(payloadB64, &payload, &payloadLength, UDP_MAX_DATAGRAM_BYTES)) {
            _reject(doc, "BAD_ARGS", "payload_b64 must be base64 of 1-1460 bytes");
            return;
        }
        if (!_readAccess(args, doc, &rq.access)) {
            free(payload);
            return;
        }
        rq.payloadBytes = payload;
        rq.payloadBytesLength = payloadLength;

        GatewayExec::Result result;
        GatewayExec::run(rq, result);
        free(payload);
        if (!_mapStatus(result, doc)) return;
        JsonObject out = _succeed(doc);
        _setBool(out, "sent", true);
        if (rq.waitMs == 0) return;
        _setBool(out, "truncated", result.truncated);
        _setPayloadOrFail(out, "reply", result.body, result.bodyLength, result.truncated, doc, "Could not encode the reply");
    }

    static void _runTcp(JsonObjectConst args, JsonDocument& doc, const char* executionId) {
        const char* ip = nullptr;
        uint8_t octets[4];
        uint16_t port = 0;
        uint32_t timeoutMs = 0;
        int64_t idle = 0;
        if (!_readTarget(args, doc, &ip, octets) || !_readPort(args, 0, doc, &port) || !_readTimeout(args, doc, &timeoutMs) ||
            !_requireInt(args, "idle_ms", 0, doc, &idle)) {
            return;
        }

        GatewayExec::Request rq;
        rq.module = GatewayLogic::Module::TCP;
        rq.ip = ip;
        rq.port = port;
        rq.timeoutMs = timeoutMs;
        rq.maxBytes = COMMAND_MAX_BYTES;
        rq.idleMs = GatewayLogic::clampIdleMs(idle);
        rq.callerId = executionId;
        if (!_readMac(args, doc, rq)) return;

        const char* payloadB64 = _readString(args, "payload_b64", nullptr);
        const char* payloadText = _readString(args, "payload", nullptr);
        if ((!args["payload_b64"].isNull() && payloadB64 == nullptr) || (!args["payload"].isNull() && payloadText == nullptr) ||
            (payloadB64 != nullptr && payloadText != nullptr)) {
            _reject(doc, "BAD_ARGS", "payload or payload_b64 must be a string, not both");
            return;
        }
        uint8_t* payload = nullptr;
        size_t payloadLength = 0;
        if (payloadB64 != nullptr && payloadB64[0] != '\0' && !_decodeBase64(payloadB64, &payload, &payloadLength, HTTP_REQUEST_MAX_BYTES)) {
            _reject(doc, "BAD_ARGS", "payload_b64 must be base64 of 0-5760 bytes");
            return;
        }
        if (!_readAccess(args, doc, &rq.access)) {
            free(payload);
            return;
        }
        rq.payload = payloadText;
        rq.payloadBytes = payload;
        rq.payloadBytesLength = payloadLength;
        Secrets::RequestState secretState;
        GatewayLogic::Resolver chain[] = {Secrets::resolver(&secretState)};
        rq.resolvers = chain;
        rq.resolverCount = 1;

        GatewayExec::Result result;
        GatewayExec::run(rq, result);
        free(payload);
        if (!_mapStatus(result, doc)) return;
        JsonObject out = _succeed(doc);
        _setBool(out, "truncated", result.truncated);
        _setPayloadOrFail(out, "reply", result.body, result.bodyLength, result.truncated, doc, "Could not encode the reply");
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
            if (!_readInt(args, "count", -1, &requested) || requested < 0 || requested > UINT16_MAX ||
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

        GatewayExec::Request rq;
        rq.module = GatewayLogic::Module::MODBUS_TCP;
        rq.ip = ip;
        rq.port = port;
        rq.timeoutMs = timeoutMs;
        rq.callerId = executionId;
        GatewayLogic::tableForFunction(fc, &rq.address.table);
        rq.address.unit = (uint8_t)unit;
        rq.address.address = (uint16_t)address;
        rq.modbusWrite = write;
        rq.count = write ? 0 : (uint16_t)count;
        rq.values = values;
        rq.valueCount = write ? count : 0;
        rq.writeMultiple = fc == 15 || fc == 16;
        if (!_readAccess(args, doc, &rq.access) || !_readMac(args, doc, rq) || (write && !_readFlag(args, "verify", doc, &rq.verify))) {
            free(values);
            return;
        }
        if (!GatewayLogic::modbusRangeFits(rq.address.address, (uint32_t)count)) {
            free(values);
            _reject(doc, "BAD_ARGS", "address + count must not exceed 65536");
            return;
        }

        GatewayExec::Result result;
        GatewayExec::run(rq, result);
        free(values);
        if (!_mapStatus(result, doc)) return;
        JsonObject out = _succeed(doc);
        if (!write) {
            _setValues(out, "values", result.registers, result.registerCount, doc);
            return;
        }
        _setNumber(out, "written", (int64_t)result.written);
        if (rq.verify) _setValues(out, "readback", result.readback, result.readbackCount, doc);
    }

    struct ScanHost {
        uint32_t ip; // network byte order, as lwIP stores it
        uint8_t mac[ETH_HWADDR_LEN];
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
            memcpy(hosts[*hostCount].mac, entryMac->addr, ETH_HWADDR_LEN);
            (*hostCount)++;
        }
    }

    enum class SweepResult { DONE, TIME_CAP, NETWORK_DOWN, INTERFACE_CHANGED, NO_MEMORY };

    // ARP-sweeps the /24 around the active interface's address.
    static SweepResult _sweepArp(ScanHost* hosts, size_t* hostCount, uint32_t maxDurationMs) {
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
            if (millis() - start > maxDurationMs) {
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

        LOG_DEBUG("Gateway %s: net_scan%s%s", executionId, service[0] ? " + mDNS " : "", service[0] ? mdnsArg.as<const char*>() : "");

        ScanHost* hosts = (ScanHost*)ps_malloc(GATEWAY_SCAN_MAX_HOSTS * sizeof(ScanHost));
        if (hosts == nullptr) {
            _fail(doc, "NO_MEMORY", "Could not allocate the scan buffer");
            return;
        }
        size_t hostCount = 0;
        // ESPmDNS queryService() blocks for its own 3 s timeout; keep the whole scan within the cap.
        uint32_t sweepBudgetMs = GATEWAY_SCAN_MAX_DURATION_MS - (service[0] != '\0' ? GATEWAY_MDNS_QUERY_MS : 0);
        SweepResult sweep = _sweepArp(hosts, &hostCount, sweepBudgetMs);
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
            char macText[ETH_HWADDR_LEN * 2 + 1];
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
                case Op::HTTP_REQUEST: _runHttp(args, doc, request.executionId, false); break;
                case Op::HTTPS_REQUEST: _runHttp(args, doc, request.executionId, true); break;
                case Op::TLS_PROBE: _runTlsProbe(args, doc, request.executionId); break;
                case Op::TCP_REQUEST: _runTcp(args, doc, request.executionId); break;
                case Op::SECRET_SET: _runSecretSet(args, doc, request.executionId); break;
                case Op::SECRET_DELETE: _runSecretDelete(args, doc, request.executionId); break;
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
            LOG_DEBUG("Gateway %s: %s succeeded", request.executionId, GatewayLogic::opName(request.op));
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
            // Commands first, then at most one collector step, so a command waits at most one step.
            Request request;
            if (xQueueReceive(_queue, &request, pdMS_TO_TICKS(Collector::nextWaitMs(GATEWAY_QUEUE_WAIT_MS))) == pdTRUE) {
                uint64_t startMs = millis64();
                _run(request);
                free(request.args);
                Collector::noteCommandMs((uint32_t)(millis64() - startMs));
            }
            if (ulTaskNotifyTake(pdTRUE, 0) > 0) shouldRun = false;
            else Collector::step();
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
        GatewayExec::begin();
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

    bool setEnabled(bool enabled) {
        // Created here too: the REST switch can run before begin().
        if (!createMutexIfNeeded(&_stateMutex) || !acquireMutex(&_stateMutex)) {
            LOG_ERROR("Failed to acquire the gateway state mutex");
            return false;
        }
        Preferences preferences;
        bool persisted = preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false) && preferences.putBool(GATEWAY_ENABLED_KEY, enabled) > 0;
        preferences.end();
        if (persisted) {
            if (_enabled != enabled) LOG_INFO("Gateway %s", enabled ? "enabled" : "disabled");
            _enabled = enabled;
        } else {
            LOG_ERROR("Failed to persist the gateway switch");
        }
        releaseMutex(&_stateMutex);
        return persisted;
    }

    bool parseArgs(JsonVariantConst args, JsonDocument& out) {
        if (args.is<JsonObjectConst>()) {
            if (measureJson(args) > GATEWAY_ARGS_MAX_BYTES) return false;
            return out.set(args);
        }
        const char* encoded = args.is<const char*>() ? args.as<const char*>() : nullptr;
        uint8_t* decoded = nullptr;
        size_t length = 0;
        if (encoded == nullptr || !_decodeBase64(encoded, &decoded, &length, GATEWAY_ARGS_MAX_BYTES)) return false;
        bool ok = !deserializeJson(out, (const char*)decoded, length);
        free(decoded);
        return ok;
    }

    SubmitResult submit(const char* executionId, Op op, JsonVariantConst args) {
        if (!_enabled || _queue == nullptr || _taskHandle == nullptr) return SubmitResult::NOT_ENABLED;

        char* argsJson = nullptr;
        if (args.is<JsonObjectConst>()) {
            size_t length = measureJson(args) + 1;
            if (length > GATEWAY_ARGS_MAX_BYTES + 1) return SubmitResult::BAD_ARGS;
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
