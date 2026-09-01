// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free network interface arbitration for products with both
// Ethernet and WiFi (Home Pro). Decides which interface should hold the default
// route: Ethernet when serviceable, WiFi STA as automatic fallback.
//
// Knows nothing about Arduino, lwIP or the drivers. The caller feeds it link/
// address/association state and a millisecond clock, and acts on the decisions
// (Network.setDefaultInterface, dropping MQTT sessions, reapplying DNS/NTP).
// On Home the Ethernet inputs simply never become serviceable and the outputs
// reduce to today's STA-only behavior.

namespace InterfaceArbitration {

// Ethernet must hold link + address this long before the default route moves
// back to it from a WORKING WiFi fallback. Filters a flapping link (marginal
// cable, negotiating switch port) from repeatedly dropping cloud sessions.
// Not applied when nothing else works: with no serviceable alternative, any
// interface that comes up is taken immediately.
#define INTERFACE_ARBITRATION_ETH_HOLDDOWN_MS (10UL * 1000UL)

enum class Interface : uint8_t {
    NONE,
    ETHERNET,
    WIFI_STA,
};

// Wire/log name for an interface. Falls back to "none".
const char *interfaceName(Interface iface);

struct Context {
    Interface active;               // Interface currently holding the default route
    bool ethLinkUp;
    bool ethHasAddress;             // DHCP lease obtained or static config applied
    bool staConnected;
    uint64_t ethServiceableSinceMs; // When ETH last became serviceable; 0 while it is not
};

struct Decision {
    Interface preferred;   // What the default route should be right now
    bool switchRequired;   // preferred != active: caller must apply the transition
};

void init(Context &context, uint64_t nowMs);

// Feed state changes. Link drop or address loss clears the serviceability clock,
// so every link bounce restarts the hold-down from zero.
void onEthState(Context &context, bool linkUp, bool hasAddress, uint64_t nowMs);
void onStaState(Context &context, bool connected, uint64_t nowMs);

// Serviceable = the interface could carry traffic now. Link without an address
// (cable in, no DHCP server) is NOT serviceable: the device is unreachable on it.
bool isEthServiceable(const Context &context);

// Evaluate the preferred default route. Pure; call on every event and tick.
// Rules:
//   - ETH serviceable and active: stay.
//   - ETH serviceable, STA active and working: move to ETH only after the
//     hold-down has elapsed (flap filter).
//   - ETH serviceable, nothing else working: take ETH immediately.
//   - ETH not serviceable: STA if connected, else NONE.
Decision evaluate(const Context &context, uint64_t nowMs);

// Record that the caller has applied a switch (default route now on newActive).
void applySwitch(Context &context, Interface newActive);

// True when at least one station-side interface is serviceable. Feeds the AP
// raise conditions (a device reachable over the wire must not raise the AP)
// and the health check. The SoftAP is accounted separately by the caller, as
// today (WifiProvisioning::isNetworkServiceable).
bool anyInterfaceServiceable(const Context &context);

}  // namespace InterfaceArbitration
