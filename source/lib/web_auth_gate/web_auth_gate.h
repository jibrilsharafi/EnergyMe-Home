// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free decision logic for the default-password lockdown.
//
// The web password ships as a published constant. A device still holding it is
// functionally unauthenticated, so it refuses to serve anything except what the
// user needs in order to leave that state.
//
// This header holds the whole allowlist, and it holds it exactly once. The
// firmware middleware is an adapter that reads a cached flag, calls evaluate(),
// and acts on the answer - it must never restate the rule. That is not a style
// preference: wifi_provisioning::isAuthBypassAllowed() states the provisioning
// carve-out rule, is unit-tested, is called by nothing, and has already drifted
// from the copy customserver.cpp reimplemented inline. One rule, one home.
//
// Knows nothing about Arduino, FreeRTOS, lwIP or NVS, which is what makes the
// allowlist host-testable at all.

namespace WebAuthGate {

// What the server should do with a request while the default password is active.
enum class Action : uint8_t {
    ALLOW,             // Hand off to the route handler as normal.
    REDIRECT_TO_ROOT,  // 302 to "/", which serves the password-change page.
    DENY               // 403 with a machine-readable reason.
};

// Decides what happens to one request.
//
// usingDefaultPassword is the cached "stored password == shipped default" flag.
// When it is false this returns ALLOW for everything, unconditionally and as the
// first thing it does: a device whose password has been changed must behave
// exactly as it did before this code existed.
//
// isApOrigin is true when the request was addressed to the device's own SoftAP.
// It widens the allowlist to the WiFi provisioning surface, and it is not a
// nicety - without it the lockdown strands a device that loses its network.
//
// The provisioning carve-out in customserver.cpp only covers UNPROVISIONED, so in
// GRACE and AP_ASSIST the provisioning routes run the full middleware chain and
// this guard sees them. A meter on the default password whose router changed
// would raise its assist AP, refuse /api/v1/network/wifi/credentials with 403,
// and have no way back onto a network - the physical button resets the password
// TO the default, so it cannot help. An AP-origin request already means physical
// proximity plus the AP's own PSK, which is a higher bar than the web password
// this lockdown exists because of.
//
// OTA is deliberately not widened. It is refused on the AP in every state, which
// is what wifi-provisioning's "Firmware update always requires authentication"
// requires.
//
// url is the request path, without query string (the caller passes
// request->url(), which ESPAsyncWebServer has already split). A null or empty
// url is treated as an unknown path.
//
// Matching is exact, never by prefix, except for the two static-asset
// directories - so "/api/v1/auth/status-extra" is not allowlisted by looking
// like "/api/v1/auth/status", and "/cssx/a" is not a stylesheet.
Action evaluate(bool usingDefaultPassword, bool isApOrigin, const char *url);

}  // namespace WebAuthGate
