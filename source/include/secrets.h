// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "gateway_template.h"
#include "secrets_logic.h"

// Write-only store of LAN equipment credentials (openspec capability:
// gateway-secrets): 16 NVS slots in `secrets_ns`, filled by the `secret_set`
// envelope (RSA-OAEP-SHA256 + AES-256-GCM, only this device can open it) or the
// local REST API, and expanded into gateway requests by the `secret` resolver,
// only toward the target each secret is bound to. No interface ever returns a
// value, its length or a hash of it.
//
// Threading: envelope ops and the resolver run in the gateway task, REST in the
// web server task, the shadow report in the MQTT task; the metadata cache and
// every NVS access are guarded by one mutex.

namespace Secrets
{
    struct Outcome {
        const char* status = "SUCCEEDED"; // SUCCEEDED, REJECTED, FAILED
        const char* reasonCode = nullptr;
        char reasonDetail[96] = "";
        char name[SecretsLogic::SECRET_NAME_MAX + 1] = "";
        uint64_t setAt = 0;
        bool applied = false; // secret_set
        bool deleted = false; // secret_delete
    };

    void begin();

    // Gateway task only (the RSA private operation never runs on the MQTT task).
    void setFromEnvelope(JsonObjectConst args, Outcome& outcome);
    void remove(JsonObjectConst args, Outcome& outcome);

    // Local REST: plaintext value from the authenticated LAN API.
    bool setLocal(const char* name, const char* ip, int64_t port, const uint8_t* value, size_t length, Outcome& outcome);
    bool removeLocal(const char* name, Outcome& outcome);

    // False when the store lock timed out (nothing written to `out`).
    bool metadataToJson(JsonObject out); // REST: kid, max, slots sorted by name with name, ip, port, set_at
    bool shadowToJson(JsonObject out);   // system shadow: kid, positional slots sorted by name
    const char* kid();

    // Per-request state of the `secret` resolver: the tokens it expanded, kept
    // for the echo scrub and zeroized when the request ends.
    class RequestState {
    public:
        RequestState() = default;
        ~RequestState() { clear(); }
        RequestState(const RequestState&) = delete;
        RequestState& operator=(const RequestState&) = delete;
        void clear();

        static constexpr size_t TOKENS_MAX = SecretsLogic::PLACEHOLDERS_MAX;
        struct Token {
            char name[SecretsLogic::SECRET_NAME_MAX + 1];
            uint8_t* raw;
            size_t rawLength;
            uint8_t* encoded; // the form sent, after the token's encodings
            size_t encodedLength;
        };
        Token tokens[TOKENS_MAX] = {};
        size_t count = 0;
        size_t placeholders = 0;
    };

    // Resolver for one request's chain (never registered globally).
    GatewayLogic::Resolver resolver(RequestState* state);

    // In-place echo scrub of every token in `state` over `buffer` (never grows it).
    size_t scrub(const RequestState& state, uint8_t* buffer, size_t length, bool truncated, bool* redacted);
}
