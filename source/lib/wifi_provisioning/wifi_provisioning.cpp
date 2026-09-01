// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "wifi_provisioning.h"

namespace WifiProvisioning {

namespace {

// Indexed by the enum value, matching the table idiom in led_state/issue_logic/shadow_logic.
const char *const STATE_NAMES[] = {"unprovisioned", "sta_connecting", "ap_assist", "grace", "sta_only"};
static_assert(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]) == STATE_COUNT,
              "Every state needs a wire name");

// 172.31.42.0/24 first: consumer routers essentially never use it, unlike the ESP
// default 192.168.4.0/24 which does collide with some ISP CPE. The rest are
// fallbacks for the rare LAN that occupies the first choice.
constexpr Subnet kCandidates[] = {
    {0xAC1F2A01u, 24},  // 172.31.42.1
    {0xAC1F2B01u, 24},  // 172.31.43.1
    {0x0A2A2A01u, 24},  // 10.42.42.1
    {0xC0A8F201u, 24},  // 192.168.242.1
};

constexpr size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

// The /24 to /28 limit is what NetworkInterface::config() will accept for the AP's
// own address. It says nothing about the networks we compare against: a home LAN on
// a /16 is perfectly ordinary and must still be honoured, so comparison networks are
// only required to carry a sane prefix.
bool candidateCidrIsUsable(uint8_t cidr) {
    return cidr >= WIFI_PROVISIONING_MIN_CIDR && cidr <= WIFI_PROVISIONING_MAX_CIDR;
}

bool comparisonCidrIsUsable(uint8_t cidr) {
    return cidr >= 1 && cidr <= 32;
}

// Saturating, so a clock that went backwards reads as "no time has passed" rather
// than as a huge elapsed value that would expire every timer at once. Zero is a
// legitimate timestamp and is deliberately not treated as a sentinel; validity is
// carried by `apRaised` and by the state, which the callers check first.
uint64_t elapsedSince(uint64_t startMs, uint64_t nowMs) {
    if (nowMs <= startMs) return 0;
    return nowMs - startMs;
}

}  // namespace

const char *stateName(State state) {
    if ((uint8_t)state >= STATE_COUNT) { return STATE_NAMES[(uint8_t)State::UNPROVISIONED]; }
    return STATE_NAMES[(uint8_t)state];
}

void init(Context &context, bool hasCredentials, uint64_t nowMs) {
    context.hasCredentials = hasCredentials;
    context.staRetryAttempts = 0;
    context.apRaiseTriggers = 0;
    context.apRaised = false;
    context.apRaisedAtMs = 0;
    context.graceStartedAtMs = 0;

    if (hasCredentials) {
        context.state = State::STA_CONNECTING;
    } else {
        context.state = State::UNPROVISIONED;
        raiseAp(context, nowMs);
    }
}

void raiseAp(Context &context, uint64_t nowMs) {
    context.apRaised = true;
    context.apRaisedAtMs = nowMs;
}

void tearDownAp(Context &context, uint64_t nowMs) {
    (void)nowMs;

    context.apRaised = false;
    context.apRaisedAtMs = 0;
    context.graceStartedAtMs = 0;

    // GRACE is the one state that describes an AP deliberately held up while STA is
    // already connected, so lowering it settles the device into its steady state. Leaving
    // the state alone would strand it there: shouldTearDownAp() goes false with apRaised,
    // and nothing else clears GRACE, so a device that simply ran out its grace window
    // would report GRACE until the next time it lost its network.
    if (context.state == State::GRACE) context.state = State::STA_ONLY;

    // No cooldown to arm. In UNPROVISIONED and AP_ASSIST the AP is raised again
    // immediately by shouldRaiseAp(), because in both the device has no other way to be
    // reached; only associating takes it down for good.
}

State onEvent(Context &context, Event event, uint64_t nowMs) {
    switch (event) {
        case Event::STA_CONNECTED:
            context.staRetryAttempts = 0;
            context.apRaiseTriggers = 0;

            // An association is the only proof the stored credentials work, and it is
            // what makes a device provisioned regardless of how it booted. Not recording
            // it here leaves a device provisioned during this boot taking the branch
            // below meant for a device still in setup, so the next outage would reopen
            // the auth carve-out on its AP and re-raise that AP every cooldown.
            context.hasCredentials = true;

            if (context.apRaised) {
                context.state = State::GRACE;
                context.graceStartedAtMs = nowMs;
            } else {
                context.state = State::STA_ONLY;
            }
            break;

        case Event::STA_ATTEMPT_FAILED:
            context.staRetryAttempts++;

            // Only a genuine association failure counts toward raising the AP.
            // Retries driven by the periodic timer bump staRetryAttempts alone.
            context.apRaiseTriggers++;

            if (!context.hasCredentials) {
                // Still being provisioned: keep the AP up and let the user try
                // again rather than demanding the web password mid-setup.
                context.state = State::UNPROVISIONED;
                if (!context.apRaised) raiseAp(context, nowMs);
            } else if (context.apRaiseTriggers >= WIFI_PROVISIONING_AP_RAISE_THRESHOLD) {
                context.state = State::AP_ASSIST;
                if (!context.apRaised) raiseAp(context, nowMs);
            } else {
                context.state = State::STA_CONNECTING;
            }
            break;

        case Event::STA_LOST:
            // AP_ASSIST already means "cannot associate", so a further drop is the
            // condition it describes, not a change of situation. Demoting here made the
            // reported state flap AP_ASSIST -> STA_CONNECTING on every retry cycle, seen
            // on hardware as a 1 <-> 2 oscillation while the AP stayed up throughout.
            if (context.state != State::AP_ASSIST) context.state = State::STA_CONNECTING;
            context.graceStartedAtMs = 0;
            break;

        case Event::CREDENTIALS_SUBMITTED:
            context.staRetryAttempts = 0;
            context.apRaiseTriggers = 0;
            context.state = State::STA_CONNECTING;

            // Deliberately NOT setting hasCredentials here. It means "these credentials
            // have been proven to work at least once" (see the comment on STA_CONNECTED),
            // and a submission is only a claim, not proof. The first attempt after a
            // submission is always driven directly by the caller regardless of this flag
            // (customwifi.cpp calls _startStaAttempt() right after feeding this event), so
            // nothing is lost there. What this flag controls is what happens on failure: a
            // device that has never connected falls straight back to UNPROVISIONED with the
            // carve-out open (test_failed_credentials_keep_unprovisioned_device_open), so a
            // typo'd password is one unauthenticated retry away. Setting this here would
            // instead route a first-attempt failure into the apRaiseTriggers/STA_CONNECTING
            // path meant for a device with previously-working credentials, silently closing
            // the carve-out on a device that has never proven anything.
            break;

        case Event::CREDENTIALS_CLEARED:
            // A WiFi reset erases the credentials the driver stores, so the context must
            // stop claiming the device has any. Leaving hasCredentials set left the device
            // retrying an association it could no longer make: STA_CONNECTING is not a
            // state shouldRaiseAp() covers, so the SoftAP never came back and the meter was
            // unreachable until someone power-cycled it. Seen on hardware when the restart
            // that normally follows the reset was refused by the minimum-uptime gate.
            context.hasCredentials = false;
            context.staRetryAttempts = 0;
            context.apRaiseTriggers = 0;
            context.graceStartedAtMs = 0;
            context.state = State::UNPROVISIONED;
            if (!context.apRaised) raiseAp(context, nowMs);
            break;

        case Event::AP_LAST_CLIENT_LEFT:
            // Nothing is watching, so the grace window has already delivered whatever it
            // was going to deliver. Only meaningful in GRACE: in every other state the AP
            // is up because the device needs it, client or no client.
            if (context.state == State::GRACE) tearDownAp(context, nowMs);
            break;

        case Event::TICK:
            break;
    }

    return context.state;
}

bool shouldTearDownAp(const Context &context, uint64_t nowMs) {
    if (!context.apRaised) return false;

    // The only timed teardown. Everywhere else the AP is up because the device cannot
    // be reached without it, and the cure is associating, not waiting.
    return context.state == State::GRACE &&
           elapsedSince(context.graceStartedAtMs, nowMs) >= WIFI_PROVISIONING_GRACE_MS;
}

bool shouldRaiseAp(const Context &context, uint64_t nowMs) {
    (void)nowMs;

    if (context.apRaised) return false;

    // Both states mean "unreachable over the network": UNPROVISIONED has nothing to
    // connect to, AP_ASSIST has credentials that do not work. Neither is time-limited,
    // so a device that loses its network stays fixable in place.
    return context.state == State::UNPROVISIONED || context.state == State::AP_ASSIST;
}

bool isNetworkServiceable(bool staConnected, bool apServing) {
    return staConnected || apServing;
}

bool isAuthBypassAllowed(State state, bool fromApNetif, bool isOtaRoute) {
    // Firmware update is never carved out. It is a deliberate recovery path over
    // the AP, but WPA2 plus digest is the floor, not WPA2 alone.
    if (isOtaRoute) return false;

    if (!fromApNetif) return false;

    // AP_ASSIST is an in-service meter that merely lost its network, so it still
    // holds the user's data and keeps full authentication. Only a device with
    // nothing to protect and a user who may not know the default password opens up.
    return state == State::UNPROVISIONED;
}

bool isDnsAllowed(const Context &context, bool staConnected) {
    if (!context.apRaised) return false;
    return !staConnected;
}

bool subnetsOverlap(uint32_t addressA, uint8_t cidrA, uint32_t addressB, uint8_t cidrB) {
    // Compare on the shorter prefix: the wider network is the one that can contain
    // the narrower, and containment either way is a collision.
    uint8_t shorter = (cidrA < cidrB) ? cidrA : cidrB;
    uint32_t mask = netmaskFromCidr(shorter);
    return (addressA & mask) == (addressB & mask);
}

bool selectApSubnetAvoiding(const Subnet *occupied, size_t occupiedCount, Subnet &out) {
    for (size_t i = 0; i < kCandidateCount; i++) {
        const Subnet &candidate = kCandidates[i];

        if (!candidateCidrIsUsable(candidate.cidr)) continue;

        bool collides = false;
        for (size_t j = 0; j < occupiedCount; j++) {
            if (!comparisonCidrIsUsable(occupied[j].cidr)) continue;
            if (subnetsOverlap(candidate.address, candidate.cidr, occupied[j].address, occupied[j].cidr)) {
                collides = true;
                break;
            }
        }
        if (collides) continue;

        out = candidate;
        return true;
    }

    return false;
}

bool selectApSubnet(
    bool staValid, uint32_t staAddress, uint8_t staCidr,
    bool staticValid, uint32_t staticAddress, uint8_t staticCidr,
    Subnet &out) {
    Subnet occupied[2];
    size_t count = 0;
    if (staValid)    occupied[count++] = {staAddress, staCidr};
    if (staticValid) occupied[count++] = {staticAddress, staticCidr};
    return selectApSubnetAvoiding(occupied, count, out);
}

size_t candidateSubnetCount() {
    return kCandidateCount;
}

Subnet candidateSubnet(size_t index) {
    if (index >= kCandidateCount) return Subnet{0u, 0u};
    return kCandidates[index];
}

uint8_t cidrFromNetmask(uint32_t netmask) {
    // Count leading ones, then require every remaining bit to be zero. Counting set bits
    // instead would accept 0xFF00FF00 as /16 and hand back a prefix describing a
    // different network than the mask actually does.
    uint8_t prefix = 0;
    uint32_t remaining = netmask;

    while (prefix < 32u && (remaining & 0x80000000u) != 0u) {
        prefix = static_cast<uint8_t>(prefix + 1u);
        remaining <<= 1;
    }

    if (remaining != 0u) return 0;  // A zero bit appeared before a one: non-contiguous

    return prefix;
}

uint32_t netmaskFromCidr(uint8_t cidr) {
    if (cidr == 0u) return 0u;           // Shifting a uint32_t by 32 is undefined, not zero
    if (cidr >= 32u) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(0xFFFFFFFFu << (32u - cidr));
}

}  // namespace WifiProvisioning
