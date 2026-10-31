// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

#include "gateway_policy.h"

// Template expansion for templated gateway fields (openspec capability:
// gateway-modules, design D7). `${name}` / `${name:arg}` plus up to three
// encodings (`|url`, `|json`, `|b64`), `$${` for a literal `${`. The engine
// holds no resolver registry: every caller passes its own chain on each call,
// so a resolver of one caller is never visible to another.

namespace GatewayLogic {

inline constexpr size_t TEMPLATE_NAME_MAX = 16;
inline constexpr size_t TEMPLATE_ARG_MAX = 32;
inline constexpr size_t TEMPLATE_MAX_ENCODINGS = 3;
inline constexpr size_t RESOLVER_DETAIL_MAX = 96;

// What the request will do, passed to every resolver call.
struct ResolveContext {
    Module module;
    uint8_t ip[4];
    uint16_t port; // effective port, after op defaults
    bool onLink;
};

enum class ResolveStatus { UNKNOWN, OK, FAILED };

struct ResolveOutput {
    uint8_t *value; // write the raw value here
    size_t cap;
    size_t length;
    bool sensitive;
    // FAILED: optional reason in place of BAD_TEMPLATE (static strings).
    const char *reasonCode;
    char reasonDetail[RESOLVER_DETAIL_MAX];
    // OK: an ARP compare the executor must run after connect, before the first byte.
    bool requireMac;
    uint8_t mac[6];
    const char *macReasonCode; // reasonCode on a mismatch
};

struct Resolver {
    ResolveStatus (*resolve)(void *context, const ResolveContext &request, const char *name, const char *arg, ResolveOutput *out);
    void *context;
    // Optional: called after a successful resolve with the raw value (`final`
    // false), then with the form written to the output after the encodings
    // (`final` true), so a sensitive resolver can scrub echoes of both.
    void (*expanded)(void *context, const char *name, const char *arg, const uint8_t *bytes, size_t length, bool final);
};

enum class ExpandStatus { OK, BAD_TEMPLATE, TOO_LONG, RESOLVER_FAILED };

struct ExpandResult {
    size_t length;
    bool sensitive;
    // RESOLVER_FAILED: reason supplied by the resolver (BAD_TEMPLATE when null).
    const char *reasonCode;
    char reasonDetail[RESOLVER_DETAIL_MAX];
    // Every MAC a resolver required; a second, different MAC sets `macConflict`.
    bool requireMac;
    bool macConflict;
    uint8_t mac[6];
    const char *macReasonCode;
};

// Expands `length` bytes of `tmpl` into `out` (at most `cap` bytes, not
// NUL-terminated). `scratch` (at least 2 x the largest resolved value) holds
// resolved values and intermediate encodings; the caller zeroizes it and `out`.
// `result` accumulates across calls (sensitive, MAC requirement): zero it once
// per request.
ExpandStatus expandTemplate(const char *tmpl, size_t length, const Resolver *chain, size_t chainLength, const ResolveContext &request,
                            uint8_t *out, size_t cap, uint8_t *scratch, size_t scratchCap, ExpandResult *result);

// True when the text holds a placeholder or escape (`${` or `$${`).
bool hasPlaceholder(const char *text, size_t length);

// Encoders, shared by host and device. Return the output length, or 0 with
// `*ok` false when `cap` is too small (an empty input gives 0 and ok).
size_t encodeUrl(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok);
size_t encodeJson(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok);
size_t encodeBase64(const uint8_t *in, size_t length, uint8_t *out, size_t cap, bool *ok);

} // namespace GatewayLogic
