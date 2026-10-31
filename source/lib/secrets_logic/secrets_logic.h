// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

#include "gateway_logic.h"
#include "gateway_policy.h"
#include "gateway_template.h"

// Pure logic of the gateway secret store (openspec capability: gateway-secrets):
// slot blob layout, name and value rules, envelope AAD and plaintext padding,
// the `secret` resolver decision and the echo scrub. NVS,
// mbedTLS and JSON live in secrets.cpp; everything here is host-testable.

namespace SecretsLogic {

inline constexpr size_t SLOT_COUNT = 16;
inline constexpr size_t SECRET_NAME_MAX = 24;
inline constexpr size_t VALUE_MAX = 256;
inline constexpr size_t SLOT_HEADER_BYTES = 48;
inline constexpr size_t SLOT_MAX_BYTES = SLOT_HEADER_BYTES + VALUE_MAX;
inline constexpr uint8_t SLOT_VERSION = 1;
inline constexpr size_t PLAINTEXT_BYTES = 258; // u16 length + value + zero padding
inline constexpr size_t AAD_MAX = 160;
inline constexpr size_t PLACEHOLDERS_MAX = 8;
inline constexpr size_t SCRUB_MIN_BYTES = 4;
inline constexpr size_t PLACEHOLDER_MAX = 34; // "${secret:" + 24 + "}"

struct SlotMeta {
    char name[SECRET_NAME_MAX + 1];
    uint8_t ip[4];
    uint16_t port;
    bool hasMac;
    uint8_t mac[6];
    uint64_t setAt;
};

bool isValidName(const char *name);
// 1-256 bytes of strict UTF-8 without NUL, CR or LF.
bool isValidValue(const uint8_t *value, size_t length);

// Slot blob: ver u8, name_len u8, name 24 B, ip 4 B, port u16 LE, mac 6 B (all
// zero when unbound), set_at u64 LE, len u16 LE, value. Size 48 + len.
size_t encodeSlot(const SlotMeta &meta, const uint8_t *value, size_t length, uint8_t *out, size_t cap);
// False on any malformation (version, size, name, mac, length, value).
bool decodeSlot(const uint8_t *blob, size_t size, SlotMeta *meta, const uint8_t **value, size_t *length);

// `energyme-secret-v1|<device>|<name>|<ip>:<port>|<mac>|<issued_at>`, no
// terminator in the AAD (out is NUL-terminated for convenience). 0 when it does not fit.
size_t buildAad(char *out, size_t cap, const char *deviceId, const char *name, const uint8_t ip[4], uint16_t port, const uint8_t *mac,
                uint64_t issuedAt);

// Unix seconds as a JSON integer's decimal text or a digit string: digits only,
// no leading zeros, at most 19 digits.
bool parseIssuedAt(const char *text, uint64_t *out);

// Decrypted plaintext: u16 LE length 1-256, value, zero padding to 258 bytes.
bool unpadPlaintext(const uint8_t *plaintext, size_t size, const uint8_t **value, size_t *length);

// Resolver decision
// =================

struct Decision {
    int slot;               // index into the cache when OK
    const char *reasonCode; // nullptr when OK
    const char *reasonDetail;
};

// `${secret:<arg>}` for one request: `placeholders` counts the secret
// placeholders already resolved in this request.
Decision decide(const GatewayLogic::ResolveContext &context, const char *arg, const SlotMeta *slots, const bool *used, size_t count,
                size_t placeholders);

// Echo scrub
// ==========

struct ScrubForm {
    const uint8_t *bytes; // raw value or the token's final encoded form
    size_t length;
    const char *name;     // secret name for the placeholder
};

// Replaces every form (at least 4 bytes, longest match first) in `buffer`
// with `${secret:<name>}`, writing through `scratch` (at least `cap` bytes) and
// never past `cap`. A placeholder that would cross `cap` is dropped whole and
// the output cut there. When the result is truncated (on input or by the cut),
// a trailing prefix of any form is removed too. Returns the new length.
size_t scrub(uint8_t *buffer, size_t length, size_t cap, uint8_t *scratch, const ScrubForm *forms, size_t formCount, bool *truncated,
             bool *redacted);

inline constexpr size_t SCRUB_PLACEHOLDER_MAX = 48; // "${" + kind + ":" + name + "}"

// The same scrub with `${<kind>:<name>}` placeholders (the collector uses "cap").
size_t scrubAs(const char *kind, uint8_t *buffer, size_t length, size_t cap, uint8_t *scratch, const ScrubForm *forms, size_t formCount,
               bool *truncated, bool *redacted);

} // namespace SecretsLogic
