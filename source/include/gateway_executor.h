// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "gateway_net.h"
#include "gateway_policy.h"
#include "gateway_template.h"

// Gateway executor (openspec capability: gateway-modules): one request/result
// contract for every LAN protocol module (http, https, modbus_tcp, tcp, udp),
// the template expansion stage, read/write classification and the write grants.
// Runs synchronously in the gateway task; callers are the IoT Command adapter
// (gateway.cpp) and, later, the collector scheduler. Modules know protocols,
// never vendors.

#define GATEWAY_WGRANTS_KEY "wgrants"
#define GATEWAY_HTTPS_MIN_INTERNAL_HEAP (32 * 1024) // bench gate, add-gateway-modules tasks.md 2.3

namespace GatewayExec
{
    using GatewayLogic::Access;
    using GatewayLogic::Module;

    enum class Status { OK, FAILED, REJECTED };
    enum class Writes { ALLOW, DENY };

    struct Header {
        const char* name;  // literal
        const char* value; // templated
    };

    inline constexpr size_t HEADER_BLOCK_MAX = 2048;
    inline constexpr uint32_t UDP_REPLY_MAX = 1460;

    struct Request {
        Module module = Module::HTTP;
        const char* ip = nullptr;
        uint16_t port = 0;
        Access access = Access::NONE;
        Writes writes = Writes::ALLOW;
        uint32_t timeoutMs = 5000;
        size_t maxBytes = 4096;
        const char* callerId = "";

        // http, https
        const char* method = "GET";
        const char* path = "/";   // templated
        const Header* headers = nullptr;
        size_t headerCount = 0;
        const char* body = nullptr; // templated text
        const uint8_t* bodyBytes = nullptr; // decoded body_b64, literal
        size_t bodyBytesLength = 0;
        const char* certSha256 = nullptr; // https: 64 lowercase hex
        bool probeOnly = false;           // tls_probe: handshake, send nothing

        // modbus_tcp
        GatewayLogic::ModbusAddress address = {};
        bool modbusWrite = false;
        uint16_t count = 0;               // reads
        const uint16_t* values = nullptr; // writes
        size_t valueCount = 0;
        bool writeMultiple = false;
        bool verify = false;

        // tcp, udp
        const char* payload = nullptr; // tcp text, templated
        const uint8_t* payloadBytes = nullptr; // decoded payload_b64, literal
        size_t payloadBytesLength = 0;
        uint32_t idleMs = 0;
        uint32_t waitMs = 0;

        // Resolver chain for this request only (no global registry).
        const GatewayLogic::Resolver* resolvers = nullptr;
        size_t resolverCount = 0;
    };

    struct Result {
        Status status = Status::FAILED;
        const char* reasonCode = "";
        char reasonDetail[160] = "";
        int protoStatus = 0;

        char* headers = nullptr; // PSRAM, raw status line + header block
        size_t headersLength = 0;
        uint8_t* body = nullptr; // PSRAM
        size_t bodyLength = 0;
        uint16_t* registers = nullptr; // PSRAM, Modbus read
        size_t registerCount = 0;
        size_t written = 0;
        uint16_t* readback = nullptr; // PSRAM, Modbus verify
        size_t readbackCount = 0;

        bool truncated = false;
        size_t bytesTx = 0;
        size_t bytesRx = 0;
        bool connected = false;
        char address[64] = "";
        uint32_t connectMs = 0;
        uint32_t firstByteMs = 0;
        uint32_t totalMs = 0;

        bool redacted = false; // a sensitive resolver's scrub replaced echoed bytes
        bool hasCert = false;  // tls_probe
        GatewayNet::CertInfo cert = {};

        Result() = default;
        ~Result() { release(); }
        Result(const Result&) = delete;
        Result& operator=(const Result&) = delete;
        void release();
    };

    void run(const Request& request, Result& result);

    // The one registration the executor allows: a sensitive resolver's echo
    // scrub, run on every OK result of every caller before run() returns.
    using ResultScrub = void (*)(const Request& request, Result& result);
    void setResultScrub(ResultScrub scrub);

    bool httpsAvailable();

    // Write grants and read declarations (one list, replaced as a whole).
    void begin(); // loads the list from NVS
    // Validates and persists; `error` gets the reason when it returns false.
    bool setGrants(JsonArrayConst list, char* error, size_t errorSize);
    void grantsToJson(JsonArray out);
    // Copies the list into `out` (GRANT_MAX_ENTRIES entries); empty on a lock failure.
    size_t copyGrants(GatewayLogic::GrantEntry* out);
}
