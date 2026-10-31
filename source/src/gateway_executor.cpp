// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_executor.h"

#include <cstdarg>

#include <AdvancedLogger.h>
#include <Preferences.h>

#include "lwip/etharp.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

#include "constants.h"
#include "gateway.h"
#include "gateway_modules.h"
#include "gateway_stream.h"
#include "tunnel.h"

using namespace GatewayLogic;

namespace GatewayExec
{
    // Grant list: written by the shadow (MQTT task) and REST, read by the
    // gateway task, which copies it into _snapshot at request start.
    static GrantEntry _grants[GRANT_MAX_ENTRIES];
    static size_t _grantCount = 0;
    static bool _grantsLoaded = false;
    static GrantEntry _snapshot[GRANT_MAX_ENTRIES]; // gateway task only
    static constexpr uint32_t GRANTS_MUTEX_WAIT_MS = 1000;

    static bool _lockGrants();
    static void _unlockGrants();

    static constexpr uint32_t ARP_POLL_MS = 20;
    static constexpr uint32_t ARP_WAIT_MAX_MS = 500;

    void Result::release() {
        free(headers);
        free(body);
        free(registers);
        free(readback);
        headers = nullptr;
        body = nullptr;
        registers = nullptr;
        readback = nullptr;
        headersLength = bodyLength = registerCount = readbackCount = 0;
    }

    void setOutcome(Result& result, Status status, const char* code, const char* detailFormat, ...) {
        result.status = status;
        result.reasonCode = code;
        result.reasonDetail[0] = '\0';
        if (detailFormat != nullptr) {
            va_list args;
            va_start(args, detailFormat);
            vsnprintf(result.reasonDetail, sizeof(result.reasonDetail), detailFormat, args);
            va_end(args);
        }
    }

    bool httpsAvailable() { return true; } // gate passed on Home and Home Pro (tasks.md 2.3)

    // Routing and ARP
    // ===============

    static ip4_addr_t _toIp4(const uint8_t octets[4]) {
        ip4_addr_t address;
        IP4_ADDR(&address, octets[0], octets[1], octets[2], octets[3]);
        return address;
    }

    // The interface lwIP routes the target through, when the target is in its
    // subnet; nullptr otherwise. Caller holds the TCPIP core lock (not recursive).
    static struct netif* _onLinkNetif(const ip4_addr_t* target) {
        struct netif* nif = ip4_route(target);
        if (nif == nullptr) return nullptr;
        bool onLink = GatewayLogic::isOnLink(lwip_ntohl(ip4_addr_get_u32(target)), lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif))),
                                             lwip_ntohl(ip4_addr_get_u32(netif_ip4_netmask(nif))));
        return onLink ? nif : nullptr;
    }

    static bool _isOnLink(const uint8_t octets[4]) {
        ip4_addr_t target = _toIp4(octets);
        LOCK_TCPIP_CORE();
        bool onLink = _onLinkNetif(&target) != nullptr;
        UNLOCK_TCPIP_CORE();
        return onLink;
    }

    // ARP entry of an on-link target; false when none or off-link.
    static bool _arpLookup(const uint8_t octets[4], uint8_t mac[6], bool request) {
        ip4_addr_t target = _toIp4(octets);
        bool found = false;
        LOCK_TCPIP_CORE();
        struct netif* nif = _onLinkNetif(&target);
        if (nif != nullptr) {
            struct eth_addr* entryMac = nullptr;
            const ip4_addr_t* entryIp = nullptr;
            if (etharp_find_addr(nif, &target, &entryMac, &entryIp) >= 0 && entryMac != nullptr) {
                memcpy(mac, entryMac->addr, 6);
                found = true;
            } else if (request) {
                etharp_request(nif, &target);
            }
        }
        UNLOCK_TCPIP_CORE();
        return found;
    }

    static bool _resolveMac(const uint8_t octets[4], uint8_t mac[6], bool udp, uint32_t deadline) {
        if (_arpLookup(octets, mac, udp)) return true;
        if (!udp) return false; // a completed TCP connect always leaves an entry
        int32_t remaining = (int32_t)(deadline - millis());
        uint32_t waitMs = remaining <= 0 ? 0 : ((uint32_t)remaining < ARP_WAIT_MAX_MS ? (uint32_t)remaining : ARP_WAIT_MAX_MS);
        uint32_t until = millis() + waitMs;
        for (uint32_t loops = 0; beforeDeadline(until) && loops < ARP_WAIT_MAX_MS / ARP_POLL_MS + 2; loops++) {
            delay(ARP_POLL_MS);
            if (_arpLookup(octets, mac, false)) return true;
        }
        return false;
    }

    bool checkMacBeforeFirstByte(const Prepared& prepared, Result& result, bool udp) {
        if (!prepared.entryMac && !prepared.resolverMac) return true;
        uint8_t mac[6];
        bool found = _resolveMac(prepared.octets, mac, udp, prepared.deadline);
        if (prepared.entryMac && (!found || memcmp(mac, prepared.entryMacAddress, 6) != 0)) {
            setOutcome(result, Status::REJECTED, "WRITE_NOT_ALLOWED", "TARGET_MAC_CHANGED");
            return false;
        }
        if (prepared.resolverMac && (!found || memcmp(mac, prepared.resolverMacAddress, 6) != 0)) {
            setOutcome(result, Status::REJECTED, prepared.resolverMacReason ? prepared.resolverMacReason : "WRITE_NOT_ALLOWED", "TARGET_MAC_CHANGED");
            return false;
        }
        return true;
    }

    bool connectAndSend(GatewayNet::Connection& connection, const Prepared& prepared, Result& result, bool tls, const uint8_t* data,
                        size_t length) {
        const Request& rq = *prepared.request;
        GatewayNet::Status status = connection.connect(prepared.octets, rq.port, prepared.deadline);
        result.connectMs = elapsedSince(prepared.start);
        if (status != GatewayNet::Status::OK) {
            setOutcome(result, Status::FAILED, beforeDeadline(prepared.deadline) ? "CONNECT_FAILED" : "TIMEOUT", "TCP connect failed");
            return false;
        }
        result.connected = true;
        if (!checkMacBeforeFirstByte(prepared, result, false)) return false;
        if (tls) {
            status = connection.startTls(prepared.deadline);
            if (status != GatewayNet::Status::OK) {
                if (status == GatewayNet::Status::TIMEOUT) setOutcome(result, Status::FAILED, "TIMEOUT", "TLS handshake did not finish within timeout_ms");
                else if (status == GatewayNet::Status::NO_MEMORY) setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the TLS context");
                else setOutcome(result, Status::FAILED, "TLS_FAILED", "TLS handshake failed");
                return false;
            }
            result.hasCert = connection.peerCert(&result.cert);
            if (!result.hasCert) {
                setOutcome(result, Status::FAILED, "TLS_FAILED", "No peer certificate");
                return false;
            }
            if (rq.probeOnly) return true;
            if (!digestEquals(result.cert.sha256, prepared.pin)) {
                setOutcome(result, Status::FAILED, "TLS_PIN_MISMATCH", "Leaf certificate does not match cert_sha256");
                return false;
            }
        }
        if (length == 0) return true;
        status = connection.writeAll(data, length, prepared.deadline);
        if (status != GatewayNet::Status::OK) {
            setOutcome(result, Status::FAILED, status == GatewayNet::Status::TIMEOUT ? "TIMEOUT" : "CONNECT_FAILED", "Could not send the request");
            return false;
        }
        result.bytesTx = length;
        return true;
    }

    // Validation
    // ==========

    static bool _validate(const Request& rq, Result& result) {
        if (rq.timeoutMs < TIMEOUT_MIN_MS || rq.timeoutMs > TIMEOUT_MAX_MS) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "timeout_ms must be 100-10000");
            return false;
        }
        if (rq.maxBytes < 1 || rq.maxBytes > GATEWAY_BODY_MAX) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "max_bytes must be 1-131072");
            return false;
        }
        switch (rq.module) {
            case Module::HTTP:
            case Module::HTTPS: {
                if (!isAllowedHttpMethod(rq.method) || rq.path == nullptr) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "method must be GET, POST, PUT, PATCH or DELETE");
                    return false;
                }
                if (rq.headerCount > 0 && rq.headers == nullptr) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "Invalid header name or value");
                    return false;
                }
                for (size_t i = 0; i < rq.headerCount; i++) {
                    if (!isValidHeaderName(rq.headers[i].name) || rq.headers[i].value == nullptr) {
                        setOutcome(result, Status::REJECTED, "BAD_ARGS", "Invalid header name or value");
                        return false;
                    }
                    if (isReservedHeaderName(rq.headers[i].name)) {
                        setOutcome(result, Status::REJECTED, "BAD_ARGS", "Framing and method-override headers are set by the device");
                        return false;
                    }
                }
                if (rq.body != nullptr && rq.bodyBytes != nullptr) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "body and body_b64 are exclusive");
                    return false;
                }
                uint8_t pin[DIGEST_BYTES];
                if (rq.module == Module::HTTPS && !rq.probeOnly && !parseHexDigest(rq.certSha256, pin)) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "cert_sha256 must be 64 lowercase hex");
                    return false;
                }
                return true;
            }
            case Module::MODBUS_TCP: {
                size_t count = rq.modbusWrite ? rq.valueCount : rq.count;
                uint8_t fc = functionFor(rq.address.table, rq.modbusWrite, count, rq.writeMultiple);
                bool ok = fc != 0 && modbusRangeFits(rq.address.address, (uint32_t)count) &&
                          (rq.modbusWrite ? (rq.values != nullptr && isValidModbusWriteCount(fc, count)) : isValidModbusReadCount(fc, count));
                for (size_t i = 0; ok && rq.modbusWrite && i < count; i++) ok = isValidModbusWriteValue(fc, rq.values[i]);
                if (!ok || (rq.verify && !rq.modbusWrite)) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "Invalid Modbus address, count or values");
                    return false;
                }
                return true;
            }
            case Module::TCP:
                if ((rq.payload != nullptr && rq.payloadBytes != nullptr) || rq.payloadBytesLength > GATEWAY_REQUEST_MAX) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "payload or payload_b64 of 0-5760 bytes");
                    return false;
                }
                return true;
            case Module::UDP:
                if (rq.payloadBytes == nullptr || rq.payloadBytesLength < 1 || rq.payloadBytesLength > UDP_REPLY_MAX) {
                    setOutcome(result, Status::REJECTED, "BAD_ARGS", "payload_b64 must be base64 of 1-1460 bytes");
                    return false;
                }
                return true;
        }
        setOutcome(result, Status::REJECTED, "BAD_ARGS", "Unknown module");
        return false;
    }

    // Expansion
    // =========

    // PSRAM buffers of one request, zeroized and freed together.
    struct Buffers {
        uint8_t* scratch = nullptr; // resolver values and encodings
        uint8_t* path = nullptr;    // expanded path, NUL-terminated
        uint8_t* field = nullptr;   // one expanded header value or the body/payload
        uint8_t* wire = nullptr;    // the bytes to send
        size_t wireLength = 0;
        size_t bodyLength = 0;      // http: expanded or decoded body

        static constexpr size_t SCRATCH = 2 * GATEWAY_REQUEST_MAX;
        static constexpr size_t PATH = HTTP_PATH_MAX_LEN + 1;
        static constexpr size_t FIELD = GATEWAY_REQUEST_MAX + 1;
        static constexpr size_t WIRE = GATEWAY_REQUEST_MAX + HTTP_PATH_MAX_LEN + 256;

        bool allocate() {
            scratch = (uint8_t*)ps_calloc(1, SCRATCH);
            path = (uint8_t*)ps_calloc(1, PATH);
            field = (uint8_t*)ps_calloc(1, FIELD);
            wire = (uint8_t*)ps_calloc(1, WIRE);
            return scratch && path && field && wire;
        }
        ~Buffers() {
            uint8_t* all[] = {scratch, path, field, wire};
            const size_t sizes[] = {SCRATCH, PATH, FIELD, WIRE};
            for (size_t i = 0; i < 4; i++) {
                if (all[i] == nullptr) continue;
                mbedtls_platform_zeroize(all[i], sizes[i]);
                free(all[i]);
            }
        }
    };

    // Expands `text` into `out` (cap bytes). False with `result` set on error.
    static bool _expand(const Request& rq, const ResolveContext& context, const char* text, uint8_t* out, size_t cap, uint8_t* scratch,
                        ExpandResult* expansion, size_t* length, Result& result) {
        ExpandStatus status = expandTemplate(text, strlen(text), rq.resolvers, rq.resolverCount, context, out, cap, scratch, Buffers::SCRATCH, expansion);
        *length = expansion->length;
        if (status == ExpandStatus::OK) return true;
        if (status == ExpandStatus::TOO_LONG) {
            bool http = rq.module == Module::HTTP || rq.module == Module::HTTPS;
            setOutcome(result, Status::REJECTED, "BAD_ARGS", http ? "HTTP request larger than one TCP send buffer" : "Expanded request exceeds its size limit");
        } else if (status == ExpandStatus::RESOLVER_FAILED && expansion->reasonCode != nullptr) {
            setOutcome(result, Status::REJECTED, expansion->reasonCode, "%s", expansion->reasonDetail);
        } else {
            setOutcome(result, Status::REJECTED, "BAD_TEMPLATE", "Unknown or malformed placeholder");
        }
        return false;
    }

    // Builds the HTTP request in `b.wire`: the whole request in one buffer, since
    // naive embedded servers (Sonoff eWeLink LAN) parse each TCP segment as a request.
    static bool _prepareHttp(const Request& rq, const ResolveContext& context, Buffers& b, ExpandResult* expansion, Result& result) {
        size_t length = 0;
        if (!_expand(rq, context, rq.path, b.path, Buffers::PATH - 1, b.scratch, expansion, &length, result)) return false;
        b.path[length] = '\0';
        if (!isValidHttpPath((const char*)b.path)) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "path must start with / and contain no spaces or control characters");
            return false;
        }

        size_t used = (size_t)snprintf((char*)b.wire, Buffers::WIRE, "%s %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n", rq.method,
                                       (const char*)b.path, rq.ip, rq.port);
        for (size_t i = 0; i < rq.headerCount; i++) {
            if (!_expand(rq, context, rq.headers[i].value, b.field, Buffers::FIELD - 1, b.scratch, expansion, &length, result)) return false;
            b.field[length] = '\0';
            if (!isValidHeaderValue((const char*)b.field) || strlen((const char*)b.field) != length) {
                setOutcome(result, Status::REJECTED, "BAD_ARGS", "Invalid header name or value");
                return false;
            }
            int n = snprintf((char*)b.wire + used, Buffers::WIRE - used, "%s: %s\r\n", rq.headers[i].name, (const char*)b.field);
            if (n < 0 || used + (size_t)n > GATEWAY_REQUEST_MAX) { // keeps every later write inside WIRE
                setOutcome(result, Status::REJECTED, "BAD_ARGS", "HTTP request larger than one TCP send buffer");
                return false;
            }
            used += (size_t)n;
        }

        const uint8_t* body = rq.bodyBytes;
        size_t bodyLength = rq.bodyBytesLength;
        if (rq.body != nullptr) {
            if (!_expand(rq, context, rq.body, b.field, Buffers::FIELD - 1, b.scratch, expansion, &bodyLength, result)) return false;
            body = b.field;
        }
        int n = bodyLength > 0 || httpMethodHasBody(rq.method)
                    ? snprintf((char*)b.wire + used, Buffers::WIRE - used, "Content-Length: %u\r\n\r\n", (unsigned)bodyLength)
                    : snprintf((char*)b.wire + used, Buffers::WIRE - used, "\r\n");
        if (n < 0 || used + (size_t)n + bodyLength > GATEWAY_REQUEST_MAX) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "HTTP request larger than one TCP send buffer");
            return false;
        }
        used += (size_t)n;
        if (bodyLength > 0) memcpy(b.wire + used, body, bodyLength);
        b.bodyLength = bodyLength;
        b.wireLength = used + bodyLength;
        return true;
    }

    static bool _preparePayload(const Request& rq, const ResolveContext& context, Buffers& b, ExpandResult* expansion, Result& result) {
        if (rq.payload != nullptr) {
            size_t length = 0;
            if (!_expand(rq, context, rq.payload, b.wire, GATEWAY_REQUEST_MAX, b.scratch, expansion, &length, result)) return false;
            b.wireLength = length;
        } else if (rq.payloadBytes != nullptr) {
            memcpy(b.wire, rq.payloadBytes, rq.payloadBytesLength);
            b.wireLength = rq.payloadBytesLength;
        }
        return true;
    }

    // Executor
    // ========

    // Copies the list into `out` (GRANT_MAX_ENTRIES). A failed lock copies
    // nothing, so a request sees an empty list: every write is refused.
    static size_t _copyGrants(GrantEntry* out) {
        size_t count = 0;
        if (_lockGrants()) {
            memcpy(out, _grants, sizeof(GrantEntry) * _grantCount);
            count = _grantCount;
            _unlockGrants();
        }
        return count;
    }

    static const char* _accessName(Verdict verdict) { return verdict == Verdict::WRITE ? "write" : "read"; }

    static void _runChecked(const Request& rq, Result& result, uint32_t start) {
        if (!_validate(rq, result)) return;

        bool http = rq.module == Module::HTTP || rq.module == Module::HTTPS;
        if (http) {
            formatHttpAddress(rq.method, rq.path, result.address, sizeof(result.address));
        } else if (rq.module == Module::MODBUS_TCP) {
            formatModbusAddress(rq.address, result.address, sizeof(result.address));
        }

        uint8_t octets[4];
        Gateway::TargetCheck target = Gateway::validateTarget(rq.ip, octets);
        if (target != Gateway::TargetCheck::OK) {
            setOutcome(result, Status::REJECTED, Gateway::targetCheckReasonCode(target), "%s", Gateway::targetCheckDescription(target));
            return;
        }

        if (rq.module == Module::HTTPS) {
            if (!httpsAvailable()) {
                setOutcome(result, Status::REJECTED, "MODULE_UNAVAILABLE", "HTTPS is not available on this product");
                return;
            }
            if (Tunnel::isActive()) {
                setOutcome(result, Status::FAILED, "NO_MEMORY", "TUNNEL_ACTIVE");
                return;
            }
            if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < GATEWAY_HTTPS_MIN_INTERNAL_HEAP) {
                setOutcome(result, Status::FAILED, "NO_MEMORY", "Internal heap below the TLS threshold");
                return;
            }
        }

        Buffers buffers;
        if (!buffers.allocate()) {
            setOutcome(result, Status::FAILED, "NO_MEMORY", "Could not allocate the request buffers");
            return;
        }
        bool onLink = _isOnLink(octets);
        ResolveContext context = {rq.module, {octets[0], octets[1], octets[2], octets[3]}, rq.port, onLink};
        ExpandResult expansion = {};
        if (http) {
            if (!_prepareHttp(rq, context, buffers, &expansion, result)) return;
        } else if (rq.module == Module::TCP || rq.module == Module::UDP) {
            if (!_preparePayload(rq, context, buffers, &expansion, result)) return;
        }
        if (expansion.macConflict) {
            setOutcome(result, Status::REJECTED, expansion.macReasonCode ? expansion.macReasonCode : "BAD_TEMPLATE", "TARGET_MAC_CHANGED");
            return;
        }

        // Also for a GET with a body, which is classed write below: a server may
        // normalize dot segments or encoded separators past a prefix grant.
        const char* expandedPath = (const char*)buffers.path;
        if (http && (strcmp(rq.method, "GET") != 0 || rq.access == Access::WRITE || buffers.bodyLength > 0) && !isSafeGrantPath(expandedPath)) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "path holds a dot segment, //, a backslash or an encoded separator");
            return;
        }

        uint8_t digest[DIGEST_BYTES];
        bool hasDigest = false;
        if ((rq.module == Module::TCP || rq.module == Module::UDP) && buffers.wireLength > 0) {
            hasDigest = mbedtls_sha256(buffers.wire, buffers.wireLength, digest, 0) == 0;
        }
        PolicyRequest policy = {};
        policy.module = rq.module;
        memcpy(policy.ip, octets, 4);
        policy.port = rq.port;
        policy.access = rq.access;
        policy.method = rq.method;
        policy.path = expandedPath;
        policy.hasBody = http && buffers.bodyLength > 0;
        policy.function = rq.module == Module::MODBUS_TCP
                              ? functionFor(rq.address.table, rq.modbusWrite, rq.modbusWrite ? rq.valueCount : rq.count, rq.writeMultiple)
                              : 0;
        policy.modbus = rq.address;
        policy.values = rq.values;
        policy.valueCount = rq.valueCount;
        policy.payloadLength = buffers.wireLength;
        policy.payloadDigest = hasDigest ? digest : nullptr;

        size_t grantCount = _copyGrants(_snapshot);
        int readEntry = -1;
        Verdict verdict = classify(policy, _snapshot, grantCount, &readEntry);
        if (verdict == Verdict::BAD_ARGS) {
            setOutcome(result, Status::REJECTED, "BAD_ARGS", "access contradicts the Modbus function");
            return;
        }
        if (verdict == Verdict::WRITE && rq.writes == Writes::DENY) {
            setOutcome(result, Status::REJECTED, "WRITE_NOT_ALLOWED", "WRITES_DENIED");
            return;
        }
        int entryIndex = readEntry;
        if (verdict == Verdict::WRITE) {
            GrantMatch match = matchWriteGrant(policy, _snapshot, grantCount, &entryIndex);
            if (match != GrantMatch::ALLOWED) {
                setOutcome(result, Status::REJECTED, match == GrantMatch::OUT_OF_RANGE ? "WRITE_OUT_OF_RANGE" : "WRITE_NOT_ALLOWED",
                           match == GrantMatch::OUT_OF_RANGE ? "Value outside the grant bounds for %s:%u %s" : "No write grant for %s:%u %s", rq.ip,
                           rq.port, result.address);
                return;
            }
        }

        Prepared prepared = {};
        prepared.request = &rq;
        memcpy(prepared.octets, octets, 4);
        prepared.start = start;
        prepared.deadline = start + rq.timeoutMs;
        prepared.wire = buffers.wire;
        prepared.wireLength = buffers.wireLength;
        if (rq.module == Module::HTTPS && !rq.probeOnly) parseHexDigest(rq.certSha256, prepared.pin);
        if (entryIndex >= 0) {
            const GrantEntry& entry = _snapshot[entryIndex];
            switch (macCheckFor(entry.hasMac, onLink)) {
                case MacCheck::MAC_REQUIRED:
                    setOutcome(result, Status::REJECTED, "WRITE_NOT_ALLOWED", "MAC_REQUIRED");
                    return;
                case MacCheck::TARGET_MAC_CHANGED:
                    setOutcome(result, Status::REJECTED, "WRITE_NOT_ALLOWED", "TARGET_MAC_CHANGED");
                    return;
                case MacCheck::COMPARE:
                    prepared.entryMac = true;
                    memcpy(prepared.entryMacAddress, entry.mac, 6);
                    break;
                case MacCheck::NONE:
                    break;
            }
        }
        if (expansion.requireMac) {
            prepared.resolverMac = true;
            memcpy(prepared.resolverMacAddress, expansion.mac, 6);
            prepared.resolverMacReason = expansion.macReasonCode;
        }

        if (entryIndex >= 0) {
            LOG_INFO("Gateway %s: %s %s by entry %d %s:%u %s", rq.callerId, moduleName(rq.module), _accessName(verdict), entryIndex, rq.ip, rq.port,
                     result.address);
        } else {
            LOG_INFO("Gateway %s: %s %s %s:%u %s", rq.callerId, moduleName(rq.module), _accessName(verdict), rq.ip, rq.port, result.address);
        }

        result.status = Status::OK;
        switch (rq.module) {
            case Module::HTTP: runHttp(prepared, result, false); break;
            case Module::HTTPS: runHttp(prepared, result, true); break;
            case Module::MODBUS_TCP: runModbus(prepared, result); break;
            case Module::TCP: runTcp(prepared, result); break;
            case Module::UDP: runUdp(prepared, result); break;
        }
    }

    void run(const Request& rq, Result& result) {
        result.release();
        result.status = Status::FAILED;
        result.reasonCode = "";
        result.reasonDetail[0] = '\0';
        result.protoStatus = 0;
        result.written = 0;
        result.truncated = result.connected = result.hasCert = false;
        result.bytesTx = result.bytesRx = 0;
        result.address[0] = '\0';
        result.connectMs = result.firstByteMs = 0;

        uint32_t start = millis();
        _runChecked(rq, result, start);
        result.totalMs = elapsedSince(start);
        if (result.status != Status::OK) {
            LOG_WARNING("Gateway %s: %s %s %s %s", rq.callerId, moduleName(rq.module), result.status == Status::REJECTED ? "REJECTED" : "FAILED",
                        result.reasonCode, result.reasonDetail);
        }
    }

    // Grant storage
    // =============

    // Created on first use (thread-safe static init): REST can set grants before
    // Gateway::begin(), and community mode never calls it.
    static SemaphoreHandle_t _grantsMutex() {
        static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
        return mutex;
    }

    // Takes the lock and loads the stored list on first use, so a REST write can
    // never be overwritten by a later boot-time load.
    static bool _lockGrants() {
        SemaphoreHandle_t mutex = _grantsMutex();
        if (mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(GRANTS_MUTEX_WAIT_MS)) != pdTRUE) return false;
        if (_grantsLoaded) return true;
        _grantsLoaded = true;
        uint8_t* blob = (uint8_t*)ps_malloc(GRANT_BLOB_MAX_BYTES);
        size_t length = 0;
        Preferences preferences;
        if (blob != nullptr && preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, true)) {
            if (preferences.isKey(GATEWAY_WGRANTS_KEY)) length = preferences.getBytes(GATEWAY_WGRANTS_KEY, blob, GRANT_BLOB_MAX_BYTES);
            preferences.end();
        }
        if (length > 0 && !deserializeGrants(blob, length, _grants, GRANT_MAX_ENTRIES, &_grantCount)) {
            LOG_ERROR("Stored gateway write grants are invalid, ignoring them");
            _grantCount = 0;
        }
        free(blob);
        if (_grantCount > 0) LOG_INFO("Gateway write grants loaded: %u entries", (unsigned)_grantCount);
        return true;
    }

    static void _unlockGrants() { xSemaphoreGive(_grantsMutex()); }

    void begin() {
        if (_lockGrants()) _unlockGrants();
    }

    bool setGrants(JsonArrayConst list, char* error, size_t errorSize) {
        if (list.isNull() || list.size() > GRANT_MAX_ENTRIES) {
            snprintf(error, errorSize, "write_grants must be an array of at most 8 entries");
            return false;
        }
        if (measureJson(list) > GRANT_JSON_MAX_BYTES) {
            snprintf(error, errorSize, "write_grants must be at most 3072 bytes of compact JSON");
            return false;
        }
        GrantEntry* entries = (GrantEntry*)ps_calloc(GRANT_MAX_ENTRIES, sizeof(GrantEntry));
        uint8_t* blob = (uint8_t*)ps_malloc(GRANT_BLOB_MAX_BYTES);
        if (entries == nullptr || blob == nullptr) {
            free(entries);
            free(blob);
            snprintf(error, errorSize, "Out of memory");
            return false;
        }
        size_t count = 0;
        bool valid = true;
        for (JsonVariantConst item : list) {
            JsonObjectConst entry = item.as<JsonObjectConst>();
            JsonArrayConst allow = entry["allow"].as<JsonArrayConst>();
            const char* allowText[GRANT_ALLOW_MAX] = {};
            size_t allowCount = 0;
            bool shapeOk = item.is<JsonObjectConst>() && entry["ip"].is<const char*>() && entry["port"].is<int64_t>() &&
                           entry["module"].is<const char*>() && (entry["kind"].isNull() || entry["kind"].is<const char*>()) &&
                           (entry["mac"].isNull() || entry["mac"].is<const char*>()) && !allow.isNull() && allow.size() >= 1 &&
                           allow.size() <= GRANT_ALLOW_MAX;
            for (JsonVariantConst a : allow) {
                if (!shapeOk) break;
                if (!a.is<const char*>()) shapeOk = false;
                else allowText[allowCount++] = a.as<const char*>();
            }
            if (!shapeOk) {
                snprintf(error, errorSize, "Entry %u: needs ip, port, module, allow (1-4 strings), optional kind and mac", (unsigned)count);
                valid = false;
                break;
            }
            GrantInput input = {entry["ip"].as<const char*>(), entry["port"].as<int64_t>(), entry["module"].as<const char*>(),
                                entry["kind"].as<const char*>(), entry["mac"].as<const char*>(), allowText, allowCount};
            const char* reason = validateGrant(input, &entries[count]);
            if (reason != nullptr) {
                snprintf(error, errorSize, "Entry %u: %s", (unsigned)count, reason);
                valid = false;
                break;
            }
            count++;
        }

        size_t length = valid ? serializeGrants(entries, count, blob, GRANT_BLOB_MAX_BYTES) : 0;
        bool persisted = false;
        if (valid && length > 0 && _lockGrants()) {
            Preferences preferences;
            persisted = preferences.begin(PREFERENCES_NAMESPACE_GATEWAY, false) && preferences.putBytes(GATEWAY_WGRANTS_KEY, blob, length) == length;
            preferences.end();
            if (persisted) {
                memcpy(_grants, entries, sizeof(GrantEntry) * count);
                _grantCount = count;
            }
            _unlockGrants();
            if (!persisted) snprintf(error, errorSize, "Could not persist the write grants");
        } else if (valid) {
            snprintf(error, errorSize, "Could not lock the write grants");
        }
        free(entries);
        free(blob);
        if (persisted) LOG_WARNING("Gateway write grants replaced: %u entries", (unsigned)count);
        return persisted;
    }

    void grantsToJson(JsonArray out) {
        GrantEntry* entries = (GrantEntry*)ps_malloc(sizeof(GrantEntry) * GRANT_MAX_ENTRIES);
        if (entries == nullptr) return;
        size_t count = _copyGrants(entries);
        for (size_t i = 0; i < count; i++) {
            const GrantEntry& e = entries[i];
            JsonObject entry = out.add<JsonObject>();
            char text[GRANT_ALLOW_TEXT_BUFFER];
            snprintf(text, sizeof(text), "%u.%u.%u.%u", e.ip[0], e.ip[1], e.ip[2], e.ip[3]);
            entry["ip"] = text;
            entry["port"] = e.port;
            entry["module"] = moduleName(e.module);
            entry["kind"] = e.kind == GrantKind::READ ? "read" : "write";
            if (e.hasMac) {
                formatMac(e.mac, text, sizeof(text));
                entry["mac"] = text;
            }
            JsonArray allow = entry["allow"].to<JsonArray>();
            for (size_t a = 0; a < e.allowCount; a++) {
                if (grantAllowText(e, a, text, sizeof(text))) allow.add(text);
            }
        }
        free(entries);
    }
}
