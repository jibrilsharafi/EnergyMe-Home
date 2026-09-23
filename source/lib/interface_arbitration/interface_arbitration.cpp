// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "interface_arbitration.h"

namespace InterfaceArbitration {

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
    }
}

void onStaState(Context &context, bool connected) {
    context.staConnected = connected;
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
    return decision;
}

}  // namespace InterfaceArbitration
