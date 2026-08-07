// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "web_auth_gate.h"

#include <cstring>

namespace WebAuthGate {

namespace {

// Exactly what the user needs to leave the default-password state, and nothing else.
//
// /api/v1/health is here for defence in depth only: it is registered with
// skipServerMiddlewares() and no filter, so the guard never actually sees it. That
// exemption is load-bearing - the device self-probes that route from 127.0.0.1 and
// five consecutive failures walk it into a restart loop that ends in a firmware
// rollback and an NVS wipe - so it is stated in both places on purpose.
const char *const ALLOWED_PATHS[] = {
    "/",                              // Serves the password-change page while locked down
    "/api/v1/health",                 // The device's own liveness probe
    "/api/v1/auth/status",            // How the gate page knows when to let go
    "/api/v1/auth/change-password",   // The only way out
    "/favicon.svg"                    // Part of rendering the gate
};

constexpr size_t ALLOWED_PATH_COUNT = sizeof(ALLOWED_PATHS) / sizeof(ALLOWED_PATHS[0]);

// Directory prefixes served in full. Both are compile-time-embedded static text,
// identical on every device, and already served without any password at all during
// provisioning - there is nothing behind them worth protecting, and a gate that
// cannot fetch its stylesheet is a gate that cannot be passed.
const char *const ALLOWED_PREFIXES[] = {
    "/css/",
    "/js/"
};

constexpr size_t ALLOWED_PREFIX_COUNT = sizeof(ALLOWED_PREFIXES) / sizeof(ALLOWED_PREFIXES[0]);

// Additionally reachable when the request arrived on the device's own SoftAP.
//
// Without these, a meter on the default password that loses its network is stranded:
// the assist AP comes up, the setup page loads, and submitting credentials returns
// 403. The physical button cannot rescue it either - that resets the password TO the
// default. Reaching the AP already requires physical proximity and its PSK.
//
// OTA is not here on purpose, and must not be added: firmware update requires
// authentication on the AP in every provisioning state.
const char *const AP_ORIGIN_PATHS[] = {
    "/wifi-setup.html",
    "/api/v1/network/wifi/status",
    "/api/v1/network/wifi/scan",
    "/api/v1/network/wifi/diagnostics",
    "/api/v1/network/wifi/credentials"
};

constexpr size_t AP_ORIGIN_PATH_COUNT = sizeof(AP_ORIGIN_PATHS) / sizeof(AP_ORIGIN_PATHS[0]);

constexpr char API_PREFIX[] = "/api/";

bool startsWith(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

}  // namespace

Action evaluate(bool usingDefaultPassword, bool isApOrigin, const char *url) {
    // A device whose password has been changed behaves exactly as it did before this
    // code existed. First line, no exceptions, no lookups.
    if (!usingDefaultPassword) return Action::ALLOW;

    // No path to reason about. Send it to the gate rather than serving it.
    if (url == nullptr || url[0] == '\0') return Action::REDIRECT_TO_ROOT;

    for (size_t i = 0; i < ALLOWED_PATH_COUNT; i++) {
        if (strcmp(url, ALLOWED_PATHS[i]) == 0) return Action::ALLOW;
    }

    for (size_t i = 0; i < ALLOWED_PREFIX_COUNT; i++) {
        if (startsWith(url, ALLOWED_PREFIXES[i])) return Action::ALLOW;
    }

    if (isApOrigin) {
        for (size_t i = 0; i < AP_ORIGIN_PATH_COUNT; i++) {
            if (strcmp(url, AP_ORIGIN_PATHS[i]) == 0) return Action::ALLOW;
        }
    }

    // Default-deny for the API, so a route added later is refused without anyone
    // having to remember to refuse it.
    if (startsWith(url, API_PREFIX)) return Action::DENY;

    // Page routes, and anything else that is not an API call. The user has one thing
    // to do; send them to it.
    return Action::REDIRECT_TO_ROOT;
}

}  // namespace WebAuthGate
