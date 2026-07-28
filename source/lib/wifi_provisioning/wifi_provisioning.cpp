// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "wifi_provisioning.h"

namespace WifiProvisioning {

namespace {

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

uint32_t maskFromCidr(uint8_t cidr) {
    if (cidr == 0) return 0u;
    if (cidr >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - cidr);
}

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

void init(Context &context, bool hasCredentials, uint64_t nowMs) {
    context.hadCredentialsAtBoot = hasCredentials;
    context.staRetryAttempts = 0;
    context.apRaiseTriggers = 0;
    context.apRaised = false;
    context.apRaisedAtMs = 0;
    context.graceStartedAtMs = 0;
    context.apCooldownUntilMs = 0;
    context.apClientEverConnected = false;

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
    context.apCooldownUntilMs = 0;
    context.apClientEverConnected = false;
}

void tearDownAp(Context &context, uint64_t nowMs) {
    context.apRaised = false;
    context.apRaisedAtMs = 0;
    context.graceStartedAtMs = 0;

    // An unprovisioned device has no other way to be reached, so its AP comes back
    // after a cooldown. An in-service device in AP_ASSIST does not: it gets one
    // bounded window per boot and the button raises another. That asymmetry is the
    // whole point of the lifetime bound - a meter whose router died must not sit
    // there broadcasting for weeks.
    if (context.state == State::UNPROVISIONED) {
        context.apCooldownUntilMs = nowMs + WIFI_PROVISIONING_AP_COOLDOWN_MS;
    } else {
        context.apCooldownUntilMs = 0;
    }
}

State onEvent(Context &context, Event event, uint64_t nowMs) {
    switch (event) {
        case Event::STA_CONNECTED:
            context.staRetryAttempts = 0;
            context.apRaiseTriggers = 0;
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

            if (!context.hadCredentialsAtBoot) {
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
            context.state = State::STA_CONNECTING;
            context.graceStartedAtMs = 0;
            break;

        case Event::CREDENTIALS_SUBMITTED:
            context.staRetryAttempts = 0;
            context.apRaiseTriggers = 0;
            context.state = State::STA_CONNECTING;
            break;

        case Event::AP_REQUESTED:
            if (!context.apRaised) raiseAp(context, nowMs);
            // An explicit request from a device that has credentials is assistance,
            // not provisioning, so it keeps full authentication.
            if (context.state != State::UNPROVISIONED) context.state = State::AP_ASSIST;
            break;

        case Event::AP_DISMISSED:
            tearDownAp(context, nowMs);
            if (context.state == State::GRACE) context.state = State::STA_ONLY;
            break;

        case Event::AP_LAST_CLIENT_LEFT:
            // Nothing is watching, so the grace window has already delivered
            // whatever it was going to deliver.
            if (context.state == State::GRACE) {
                tearDownAp(context, nowMs);
                context.state = State::STA_ONLY;
            }
            break;

        case Event::TICK:
            break;
    }

    return context.state;
}

bool shouldTearDownAp(const Context &context, uint64_t nowMs) {
    if (!context.apRaised) return false;

    if (context.state == State::GRACE &&
        elapsedSince(context.graceStartedAtMs, nowMs) >= WIFI_PROVISIONING_GRACE_MS) {
        return true;
    }

    // The hard bound. Applies in every state, including GRACE, so no single AP
    // window can outlive it regardless of which timer the caller is watching.
    return elapsedSince(context.apRaisedAtMs, nowMs) >= WIFI_PROVISIONING_AP_MAX_LIFETIME_MS;
}

bool shouldRaiseAp(const Context &context, uint64_t nowMs) {
    if (context.apRaised) return false;
    if (context.state != State::UNPROVISIONED) return false;
    return nowMs >= context.apCooldownUntilMs;
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
    uint32_t mask = maskFromCidr(shorter);
    return (addressA & mask) == (addressB & mask);
}

bool selectApSubnet(
    bool staValid, uint32_t staAddress, uint8_t staCidr,
    bool staticValid, uint32_t staticAddress, uint8_t staticCidr,
    Subnet &out) {
    for (size_t i = 0; i < kCandidateCount; i++) {
        const Subnet &candidate = kCandidates[i];

        if (!candidateCidrIsUsable(candidate.cidr)) continue;

        if (staValid && comparisonCidrIsUsable(staCidr) &&
            subnetsOverlap(candidate.address, candidate.cidr, staAddress, staCidr)) {
            continue;
        }

        if (staticValid && comparisonCidrIsUsable(staticCidr) &&
            subnetsOverlap(candidate.address, candidate.cidr, staticAddress, staticCidr)) {
            continue;
        }

        out = candidate;
        return true;
    }

    return false;
}

size_t candidateSubnetCount() {
    return kCandidateCount;
}

Subnet candidateSubnet(size_t index) {
    if (index >= kCandidateCount) return Subnet{0u, 0u};
    return kCandidates[index];
}

}  // namespace WifiProvisioning
