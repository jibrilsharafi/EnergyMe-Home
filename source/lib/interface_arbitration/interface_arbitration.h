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

// An engaged WiFi station is kept this much longer than the hold-down once the
// wire is back. The default route moves to Ethernet at the hold-down; the linger
// gives the sessions that were riding WiFi time to reconnect over the wire before
// the station (and its address) goes away, so the release never races the switch.
#define INTERFACE_ARBITRATION_STA_RELEASE_LINGER_MS (5UL * 1000UL)

enum class Interface : uint8_t {
    NONE,
    ETHERNET,
    WIFI_STATION,
};

// Wire/log name for an interface. Falls back to "none".
const char *interfaceName(Interface iface);

struct Context {
    Interface active;               // Interface currently holding the default route
    bool ethLinkUp;
    bool ethHasAddress;             // DHCP lease obtained or static config applied
    bool staConnected;
    uint64_t ethServiceableSinceMs; // When ETH last became serviceable; 0 while it is not
    bool staEngaged;                // The station was needed or in use: a returning wire must outlast the release window
};

struct Decision {
    Interface preferred;   // What the default route should be right now
    bool switchRequired;   // preferred != active: caller must apply the transition
};

void init(Context &context);

// Feed state changes. Link drop or address loss clears the serviceability clock,
// so every link bounce restarts the hold-down from zero. Losing a serviceable wire
// also engages the station, as does a station that connects: from then on a
// returning wire has to outlast the release window before the station is let go.
// A station attempt that never connected does not engage it.
void onEthState(Context &context, bool linkUp, bool hasAddress, uint64_t nowMs);
void onStaState(Context &context, bool connected);

// Serviceable = the interface could carry traffic now. Link without an address
// (cable in, no DHCP server) is NOT serviceable: the device is unreachable on it.
bool isEthServiceable(const Context &context);

// Evaluate the preferred default route and record it as active when it changed.
// Call on every event and tick; the returned decision tells the caller whether
// a transition must be applied (Network.setDefaultInterface, session drops).
// Rules:
//   - ETH serviceable and active: stay.
//   - ETH serviceable, STA active and working: move to ETH only after the
//     hold-down has elapsed (flap filter).
//   - ETH serviceable, nothing else working: take ETH immediately.
//   - ETH not serviceable: STA if connected, else NONE.
// Also disengages the station once the wire has held for the whole release window.
Decision evaluateAndApply(Context &context, uint64_t nowMs);

// Whether the WiFi station should be running at all (one station-side interface at
// a time). Independent of which interface holds the default route:
//   - ETH not serviceable: always wanted. This is every moment on Home, and the
//     time before the wire comes up on a wired product (the caller's boot hold
//     covers that window).
//   - ETH serviceable, station never engaged (wired boot): not wanted.
//   - ETH serviceable, station engaged: wanted until the wire has held for
//     hold-down + linger, so the route switch always precedes the release and a
//     flapping link never releases the station.
// A clock that went backwards reads as "no time has passed": the station is kept.
bool isStaWanted(const Context &context, uint64_t nowMs);

}  // namespace InterfaceArbitration
