// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free WiFi provisioning decision logic.
//
// Holds every rule that decides *what* should happen during provisioning: which
// state the device is in, when the SoftAP goes up and comes down, when the
// catch-all DNS responder may run, whether a request qualifies for the
// authentication carve-out, and which AP subnet is safe to use.
//
// It deliberately knows nothing about Arduino, FreeRTOS, lwIP or NVS. The caller
// feeds it events and a millisecond clock and acts on the answers. That is what
// makes it host-testable, and the reason the counter separation below can be
// regression-tested at all.

namespace WifiProvisioning {

// Timing. All milliseconds, all bounded.
#define WIFI_PROVISIONING_GRACE_MS (5UL * 60UL * 1000UL)         // AP stays up this long after a successful connect
#define WIFI_PROVISIONING_AP_MAX_LIFETIME_MS (30UL * 60UL * 1000UL)  // Hard bound on any single AP window
#define WIFI_PROVISIONING_AP_COOLDOWN_MS (5UL * 60UL * 1000UL)   // Gap before an unprovisioned device re-raises

// Consecutive association failures before the AP is raised to ask for help.
#define WIFI_PROVISIONING_AP_RAISE_THRESHOLD 5

// Only /24 to /28 are accepted by NetworkInterface::config(); anything else is a
// hard failure at runtime, so candidate subnets never stray outside that range.
#define WIFI_PROVISIONING_MIN_CIDR 24
#define WIFI_PROVISIONING_MAX_CIDR 28

enum class State : uint8_t {
    UNPROVISIONED,   // No stored credentials. AP up, DNS on, auth carve-out active.
    STA_CONNECTING,  // Association in progress or being retried.
    AP_ASSIST,       // In-service device that lost its network. AP up, full auth.
    GRACE,           // STA is up and the AP is deliberately held open a while longer.
    STA_ONLY         // Steady state. AP down.
};

enum class Event : uint8_t {
    STA_CONNECTED,
    STA_ATTEMPT_FAILED,     // One association attempt gave up
    STA_LOST,               // Was connected, dropped
    CREDENTIALS_SUBMITTED,
    AP_REQUESTED,           // Button, or a UI toggle while connected
    AP_DISMISSED,           // User closed it
    AP_LAST_CLIENT_LEFT,    // No stations remain associated to the SoftAP
    TICK                    // Time passed; re-evaluate the timers
};

struct Context {
    State state;
    bool hadCredentialsAtBoot;

    // These two must stay separate. `_forceReconnectInternal()` fires from the
    // periodic check every 30 s while STA is down and increments the retry
    // counter; if the AP-raise predicate read the same counter it would be driven
    // by the retry timer rather than by genuine association failures, and would
    // never settle while the AP is up.
    uint32_t staRetryAttempts;
    uint32_t apRaiseTriggers;

    bool apRaised;
    uint64_t apRaisedAtMs;
    uint64_t graceStartedAtMs;
    uint64_t apCooldownUntilMs;
    bool apClientEverConnected;
};

// Sets the initial state from what NVS holds. `nowMs` seeds the AP timers when the
// device comes up with nothing to connect to.
void init(Context &context, bool hasCredentials, uint64_t nowMs);

// Applies an event and returns the resulting state. Pure apart from `context`.
State onEvent(Context &context, Event event, uint64_t nowMs);

// Timer evaluation, safe to call as often as the caller likes. Returns true when
// the caller should tear the AP down.
bool shouldTearDownAp(const Context &context, uint64_t nowMs);

// True when an unprovisioned device with its AP down has waited out the cooldown
// and should broadcast again. Never true for AP_ASSIST: an in-service device gets
// one bounded window per boot, and the button is the way to ask for another.
bool shouldRaiseAp(const Context &context, uint64_t nowMs);

// Applies the teardown, including the cooldown for the unprovisioned case.
void tearDownAp(Context &context, uint64_t nowMs);

// Applies the raise.
void raiseAp(Context &context, uint64_t nowMs);

// The health check must not restart a device that is serving perfectly well on the
// AP netif with no upstream network. Serving on either interface counts.
bool isNetworkServiceable(bool staConnected, bool apServing);

// The authentication carve-out. `fromApNetif` must be a real arrival-interface
// check, not the SYN destination: the device's own loopback health probe and a LAN
// host with a static route to the AP address both have to come out false here.
bool isAuthBypassAllowed(State state, bool fromApNetif, bool isOtaRoute);

// The catch-all DNS responder answers every name with the AP address regardless of
// arrival interface, so it may only run while there is no LAN to leak onto.
bool isDnsAllowed(const Context &context, bool staConnected);

struct Subnet {
    uint32_t address;  // Host-order, the AP's own address (x.y.z.1)
    uint8_t cidr;
};

// True when the two networks share any address. lwIP resolves an ambiguous route
// to the *first* matching interface rather than the longest prefix, and netif_add
// prepends, so an AP raised after STA would win every ambiguous match and send
// LAN-destined traffic out of the AP.
bool subnetsOverlap(uint32_t addressA, uint8_t cidrA, uint32_t addressB, uint8_t cidrB);

// Picks the first candidate AP subnet that collides with neither the live STA
// subnet nor a configured static IP. The static IP matters on its own: a
// configuration backup restored from a different LAN carries a foreign address
// that is applied before the AP is raised.
//
// Returns false when every candidate collides, which the caller must treat as
// "do not raise the AP" rather than falling back to a colliding default.
bool selectApSubnet(
    bool staValid, uint32_t staAddress, uint8_t staCidr,
    bool staticValid, uint32_t staticAddress, uint8_t staticCidr,
    Subnet &out);

// Candidate list, exposed for tests and for logging.
size_t candidateSubnetCount();
Subnet candidateSubnet(size_t index);

// Prefix length of a host-order netmask, e.g. 0xFFFFFF00 -> 24.
//
// Returns 0 for a non-contiguous mask (0xFF00FF00 and friends). A netmask must be a
// run of ones followed by a run of zeros; anything else is malformed and callers must
// not treat the result as a usable prefix. Reading it from a live interface is not a
// guarantee of well-formedness, so this is checked rather than assumed.
uint8_t cidrFromNetmask(uint32_t netmask);

// Host-order netmask for a prefix length, e.g. 24 -> 0xFFFFFF00.
//
// Defined for the whole range 0..32 inclusive. A naive 0xFFFFFFFF << (32 - cidr) is
// undefined behaviour at cidr == 0, where the shift width equals the operand width;
// this handles that case explicitly.
uint32_t netmaskFromCidr(uint8_t cidr);

}  // namespace WifiProvisioning
