// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "interface_arbitration.h"

namespace InterfaceArbitration {

// How long the wire must hold before an engaged station is let go.
static const uint64_t STA_RELEASE_WINDOW_MS =
    (uint64_t)INTERFACE_ARBITRATION_ETH_HOLDDOWN_MS + (uint64_t)INTERFACE_ARBITRATION_STA_RELEASE_LINGER_MS;

// Saturating, so a clock that went backwards reads as "no time has passed" rather
// than as a huge elapsed value that would release the station at once. Keeping the
// station is the safe direction: the device stays reachable on it.
static uint64_t elapsedSince(uint64_t startMs, uint64_t nowMs) {
    if (nowMs <= startMs) return 0;
    return nowMs - startMs;
}

const char *interfaceName(Interface iface) {
    switch (iface) {
        case Interface::NONE:     return "none";
        case Interface::ETHERNET: return "ethernet";
        case Interface::WIFI_STATION: return "wifi";
    }
    return "none";
}

void init(Context &context) {
    context.active = Interface::NONE;
    context.ethLinkUp = false;
    context.ethHasAddress = false;
    context.staConnected = false;
    context.ethServiceableSinceMs = 0;
    context.staEngaged = false;
}

void onEthState(Context &context, bool linkUp, bool hasAddress, uint64_t nowMs) {
    bool wasServiceable = isEthServiceable(context);
    context.ethLinkUp = linkUp;
    context.ethHasAddress = hasAddress;
    bool nowServiceable = isEthServiceable(context);

    if (nowServiceable && !wasServiceable) {
        // nowMs can legitimately be 0 right at boot; 0 means "not serviceable",
        // so clamp the timestamp to 1 to keep the two states distinguishable.
        context.ethServiceableSinceMs = (nowMs == 0) ? 1 : nowMs;
    } else if (!nowServiceable) {
        // Every drop restarts the hold-down: a flapping link never accumulates
        // enough continuous uptime to steal the route back from a working STA.
        context.ethServiceableSinceMs = 0;
        // Losing a wire that was serving is what calls the station in. Engaging
        // here, not when it connects, is what keeps it wanted through a flap even
        // if it never manages to associate between two bounces. A wire that never
        // served (boot, link without address) engages nothing.
        if (wasServiceable) context.staEngaged = true;
    }
}

void onStaState(Context &context, bool connected) {
    context.staConnected = connected;
    // A connected station may be carrying sessions, whatever brought it up (it
    // beat the wire at boot, or a credential check on a wired product). Only a
    // real connection counts: an attempt still in flight has nothing to protect,
    // so it must not delay the release when the wire arrives first.
    // Never cleared on a disconnect: the release window is timed off the wire.
    if (connected) context.staEngaged = true;
}

bool isEthServiceable(const Context &context) {
    return context.ethLinkUp && context.ethHasAddress;
}

static Decision evaluate(const Context &context, uint64_t nowMs) {
    Interface preferred;

    if (isEthServiceable(context)) {
        if (context.active == Interface::ETHERNET) {
            preferred = Interface::ETHERNET;
        } else if (context.active == Interface::WIFI_STATION && context.staConnected) {
            // A working fallback is only abandoned for an ETH that has proven
            // itself stable for the whole hold-down window.
            uint64_t since = context.ethServiceableSinceMs;
            bool heldLongEnough = since != 0 && (nowMs - since) >= INTERFACE_ARBITRATION_ETH_HOLDDOWN_MS;
            preferred = heldLongEnough ? Interface::ETHERNET : Interface::WIFI_STATION;
        } else {
            // Nothing else is carrying traffic: take the wire immediately.
            preferred = Interface::ETHERNET;
        }
    } else {
        preferred = context.staConnected ? Interface::WIFI_STATION : Interface::NONE;
    }

    Decision decision;
    decision.preferred = preferred;
    decision.switchRequired = (preferred != context.active);
    return decision;
}

Decision evaluateAndApply(Context &context, uint64_t nowMs) {
    Decision decision = evaluate(context, nowMs);
    if (decision.switchRequired) context.active = decision.preferred;

    // The wire has outlasted the whole release window: the station is no longer
    // owed a linger. isStaWanted() already reads false past the window, so this
    // changes nothing on a sane clock; it stops a stale flag from re-wanting the
    // station on a long-stable wire if the clock ever steps backwards.
    uint64_t since = context.ethServiceableSinceMs;
    if (context.staEngaged && since != 0 && elapsedSince(since, nowMs) >= STA_RELEASE_WINDOW_MS) {
        context.staEngaged = false;
    }
    return decision;
}

bool isStaWanted(const Context &context, uint64_t nowMs) {
    if (!isEthServiceable(context)) return true;
    if (!context.staEngaged) return false;

    // Serviceable implies a non-zero timestamp (clamped to 1 in onEthState).
    return elapsedSince(context.ethServiceableSinceMs, nowMs) < STA_RELEASE_WINDOW_MS;
}

}  // namespace InterfaceArbitration
