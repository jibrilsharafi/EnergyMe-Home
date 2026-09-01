// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the WiFi provisioning decision logic. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <cstring>
#include <unity.h>
#include "wifi_provisioning.h"

using namespace WifiProvisioning;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint64_t kMinute = 60ULL * 1000ULL;

Context provisionedContext(uint64_t nowMs = 0) {
    Context context;
    init(context, true, nowMs);
    return context;
}

Context unprovisionedContext(uint64_t nowMs = 0) {
    Context context;
    init(context, false, nowMs);
    return context;
}

// 172.31.42.1 etc, spelled out so the tests read as addresses rather than hex.
constexpr uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8) | static_cast<uint32_t>(d);
}

}  // namespace

// ============================================================================
// Initial state
// ============================================================================

void test_boot_with_credentials_starts_connecting_without_ap(void) {
    Context context = provisionedContext();
    TEST_ASSERT_EQUAL(State::STA_CONNECTING, context.state);
    TEST_ASSERT_FALSE(context.apRaised);
}

void test_boot_without_credentials_raises_ap(void) {
    Context context = unprovisionedContext();
    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

// ============================================================================
// Counter poisoning regression
//
// _forceReconnectInternal() fires from the periodic check every 30 s while STA is
// down. If the AP-raise predicate shared that counter it would be driven by the
// retry timer instead of by real association failures.
// ============================================================================

void test_retry_attempts_and_ap_triggers_are_separate_fields(void) {
    Context context = provisionedContext();

    onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
    TEST_ASSERT_EQUAL_UINT32(1, context.staRetryAttempts);
    TEST_ASSERT_EQUAL_UINT32(1, context.apRaiseTriggers);

    // A forced reconnect that does not produce an association failure must leave
    // the AP-raise counter alone. The caller expresses that by not sending the
    // event at all, so the invariant under test is that nothing else moves it.
    onEvent(context, Event::TICK, 2 * kMinute);
    TEST_ASSERT_EQUAL_UINT32(1, context.apRaiseTriggers);
}

void test_ap_raises_only_after_threshold_failures(void) {
    Context context = provisionedContext();

    for (int i = 1; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, static_cast<uint64_t>(i) * kMinute);
        TEST_ASSERT_EQUAL(State::STA_CONNECTING, context.state);
        TEST_ASSERT_FALSE(context.apRaised);
    }

    onEvent(context, Event::STA_ATTEMPT_FAILED,
            static_cast<uint64_t>(WIFI_PROVISIONING_AP_RAISE_THRESHOLD) * kMinute);
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

void test_successful_connection_clears_both_counters(void) {
    Context context = provisionedContext();
    onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
    onEvent(context, Event::STA_ATTEMPT_FAILED, 2 * kMinute);

    onEvent(context, Event::STA_CONNECTED, 3 * kMinute);
    TEST_ASSERT_EQUAL_UINT32(0, context.staRetryAttempts);
    TEST_ASSERT_EQUAL_UINT32(0, context.apRaiseTriggers);
}

void test_failed_credentials_keep_unprovisioned_device_open(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    TEST_ASSERT_EQUAL(State::STA_CONNECTING, context.state);

    // A wrong password must not demand the web password to try again.
    onEvent(context, Event::STA_ATTEMPT_FAILED, 2 * kMinute);
    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

// Regression: `hasCredentials` was a boot snapshot, so a device provisioned during this
// boot still looked unprovisioned to the branch above. Losing its network then dropped it
// back into UNPROVISIONED, which opens the auth carve-out on the AP - setup page, scan,
// diagnostics and the credentials POST, all unauthenticated to anyone who can join an AP
// whose PSK is the SSID suffix on community hardware - and re-raises that AP every
// cooldown for as long as the outage lasts. It belongs in AP_ASSIST: it holds the user's
// data now, and it gets one bounded window.
void test_device_provisioned_this_boot_falls_back_to_ap_assist(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);
    TEST_ASSERT_EQUAL(State::GRACE, context.state);

    // End the grace window first, so the AP is genuinely down when the outage starts.
    // Without this the AP is still up from provisioning and the raise below proves nothing.
    uint64_t graceEnd = 2 * kMinute + WIFI_PROVISIONING_GRACE_MS;
    onEvent(context, Event::AP_LAST_CLIENT_LEFT, graceEnd);
    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);
    TEST_ASSERT_FALSE(context.apRaised);

    onEvent(context, Event::STA_LOST, graceEnd + kMinute);
    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, graceEnd + 2 * kMinute);
        // Not even the first failure may reopen the device.
        TEST_ASSERT_TRUE(context.state != State::UNPROVISIONED);
    }

    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);

    // The property that was broken: full digest auth on that AP.
    TEST_ASSERT_FALSE(isAuthBypassAllowed(context.state, true, false));
}

// ============================================================================
// The AP is up while the device is unreachable, and only then
// ============================================================================

// The AP used to be bounded to 30 minutes per boot, which meant a meter whose router
// was replaced went dark until someone power-cycled it. It is now held for as long as
// the device cannot associate, because the assist AP keeps full authentication and
// being fixable in place matters more than the airtime.
void test_ap_assist_ap_is_not_time_limited(void) {
    Context context = provisionedContext();
    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
    }
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);

    TEST_ASSERT_FALSE(shouldTearDownAp(context, kMinute + 30ULL * kMinute));
    TEST_ASSERT_FALSE(shouldTearDownAp(context, 30ULL * 24ULL * 60ULL * kMinute));
}

void test_ap_assist_re_raises_immediately_if_lowered(void) {
    Context context = provisionedContext();
    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
    }

    uint64_t teardownAt = 10 * kMinute;
    tearDownAp(context, teardownAt);

    // Still cannot associate, so it still needs the AP. No cooldown to wait out.
    TEST_ASSERT_TRUE(shouldRaiseAp(context, teardownAt));
}

void test_unprovisioned_ap_re_raises_immediately_if_lowered(void) {
    Context context = unprovisionedContext();
    tearDownAp(context, 10 * kMinute);

    TEST_ASSERT_TRUE(shouldRaiseAp(context, 10 * kMinute));
}

// Associating is the one thing that ends the AP for good.
void test_ap_is_not_raised_once_connected(void) {
    Context context = provisionedContext();
    onEvent(context, Event::STA_CONNECTED, kMinute);
    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);

    TEST_ASSERT_FALSE(shouldRaiseAp(context, 24ULL * 60ULL * kMinute));
}

void test_no_teardown_while_ap_is_down(void) {
    Context context = provisionedContext();
    TEST_ASSERT_FALSE(shouldTearDownAp(context, 10ULL * 24ULL * 60ULL * kMinute));
}

// ============================================================================
// Grace window
// ============================================================================

void test_connect_with_ap_up_enters_grace(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);

    TEST_ASSERT_EQUAL(State::GRACE, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

void test_connect_with_ap_down_goes_straight_to_sta_only(void) {
    Context context = provisionedContext();
    onEvent(context, Event::STA_CONNECTED, kMinute);
    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);
}

void test_grace_expires_at_the_configured_window(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);

    TEST_ASSERT_FALSE(shouldTearDownAp(context, 2 * kMinute + WIFI_PROVISIONING_GRACE_MS - 1));
    TEST_ASSERT_TRUE(shouldTearDownAp(context, 2 * kMinute + WIFI_PROVISIONING_GRACE_MS));
}

// Regression: shouldTearDownAp() fires, the caller lowers the AP, and the state was left
// at GRACE forever. Nothing else clears it, so the device kept reporting a grace window
// for an AP that had been down for hours.
void test_expired_grace_teardown_settles_on_sta_only(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);

    uint64_t expiresAt = 2 * kMinute + WIFI_PROVISIONING_GRACE_MS;
    TEST_ASSERT_TRUE(shouldTearDownAp(context, expiresAt));
    tearDownAp(context, expiresAt);

    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);
    TEST_ASSERT_FALSE(context.apRaised);

    // And it stays down: the device is on the LAN now, which is the only condition
    // that keeps the AP from coming back.
    TEST_ASSERT_FALSE(shouldRaiseAp(context, expiresAt));
    TEST_ASSERT_FALSE(shouldRaiseAp(context, expiresAt + 24ULL * 60ULL * kMinute));
}

void test_last_client_leaving_ends_grace_immediately(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);

    onEvent(context, Event::AP_LAST_CLIENT_LEFT, 3 * kMinute);
    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);
    TEST_ASSERT_FALSE(context.apRaised);
}

// ============================================================================
// Credentials cleared (WiFi reset)
// ============================================================================

// Regression, found on hardware. resetWifi() erased the stored credentials but nothing
// told the context, so it kept reporting hasCredentials and sat in STA_CONNECTING -
// which shouldRaiseAp() does not cover. The restart that normally hides this was refused
// by the minimum-uptime gate, and the device stayed off the LAN with no AP, unreachable
// until it was power-cycled.
void test_clearing_credentials_returns_to_unprovisioned_with_the_ap_up(void) {
    Context context = provisionedContext();
    onEvent(context, Event::STA_CONNECTED, kMinute);
    TEST_ASSERT_EQUAL(State::STA_ONLY, context.state);
    TEST_ASSERT_FALSE(context.apRaised);

    onEvent(context, Event::CREDENTIALS_CLEARED, 2 * kMinute);

    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_FALSE(context.hasCredentials);
    TEST_ASSERT_TRUE(context.apRaised);
}

// A failure after the reset must keep the device in setup rather than counting toward
// AP_ASSIST, which would demand the web password on the AP of a device the user has
// just deliberately wiped.
void test_failure_after_clearing_credentials_stays_in_setup(void) {
    Context context = provisionedContext();
    onEvent(context, Event::STA_CONNECTED, kMinute);
    onEvent(context, Event::CREDENTIALS_CLEARED, 2 * kMinute);

    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD + 2; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, 3 * kMinute + i);
    }

    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

// Clearing while the AP is already up must not restart the grace clock or double-raise.
void test_clearing_credentials_with_the_ap_already_up_is_idempotent(void) {
    Context context = unprovisionedContext();
    uint64_t raisedAt = context.apRaisedAtMs;

    onEvent(context, Event::CREDENTIALS_CLEARED, 5 * kMinute);

    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
    TEST_ASSERT_EQUAL(raisedAt, context.apRaisedAtMs);
}

// The grace window is measured from the connect, not from when the AP went up, so a
// long provisioning session does not eat into the time the user gets to read the
// "here is my new address" page.
void test_grace_is_measured_from_the_connect(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, 0);

    uint64_t connectAt = 45ULL * kMinute;  // AP had been up a long time already
    onEvent(context, Event::STA_CONNECTED, connectAt);
    TEST_ASSERT_EQUAL(State::GRACE, context.state);

    TEST_ASSERT_FALSE(shouldTearDownAp(context, connectAt + WIFI_PROVISIONING_GRACE_MS - 1));
    TEST_ASSERT_TRUE(shouldTearDownAp(context, connectAt + WIFI_PROVISIONING_GRACE_MS));
}

void test_losing_sta_during_grace_returns_to_connecting(void) {
    Context context = unprovisionedContext();
    onEvent(context, Event::CREDENTIALS_SUBMITTED, kMinute);
    onEvent(context, Event::STA_CONNECTED, 2 * kMinute);

    onEvent(context, Event::STA_LOST, 3 * kMinute);
    TEST_ASSERT_EQUAL(State::STA_CONNECTING, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

// Regression, found on hardware: a device sitting in AP_ASSIST reported
// AP_ASSIST -> STA_CONNECTING -> AP_ASSIST on every retry cycle, because each failed
// attempt raised STA_LOST and STA_LOST demoted unconditionally. The AP stayed up
// throughout, so it was the reported state that was wrong, not the radio.
void test_losing_sta_again_keeps_ap_assist(void) {
    Context context = provisionedContext();
    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
    }
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);

    onEvent(context, Event::STA_LOST, 2 * kMinute);
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);

    // And it survives a whole run of them, which is what the retry loop produces.
    for (int i = 0; i < 5; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, 3 * kMinute);
        onEvent(context, Event::STA_LOST, 3 * kMinute);
    }
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

void test_clock_going_backwards_does_not_expire_timers(void) {
    Context context = unprovisionedContext(10 * kMinute);
    onEvent(context, Event::CREDENTIALS_SUBMITTED, 11 * kMinute);
    onEvent(context, Event::STA_CONNECTED, 12 * kMinute);

    TEST_ASSERT_FALSE(shouldTearDownAp(context, kMinute));
}

// ============================================================================
// Health check predicate
// ============================================================================

void test_serving_on_the_ap_alone_counts_as_serviceable(void) {
    TEST_ASSERT_TRUE(isNetworkServiceable(false, true));
}

void test_serving_on_sta_alone_counts_as_serviceable(void) {
    TEST_ASSERT_TRUE(isNetworkServiceable(true, false));
}

void test_no_interface_is_not_serviceable(void) {
    TEST_ASSERT_FALSE(isNetworkServiceable(false, false));
}

// ============================================================================
// Authentication carve-out
// ============================================================================

void test_unprovisioned_ap_request_is_carved_out(void) {
    TEST_ASSERT_TRUE(isAuthBypassAllowed(State::UNPROVISIONED, true, false));
}

void test_ap_assist_is_never_carved_out(void) {
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::AP_ASSIST, true, false));
}

void test_sta_netif_is_never_carved_out(void) {
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::UNPROVISIONED, false, false));
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::STA_ONLY, false, false));
}

void test_ota_is_never_carved_out_in_any_state(void) {
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::UNPROVISIONED, true, true));
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::AP_ASSIST, true, true));
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::STA_ONLY, true, true));
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::GRACE, true, true));
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::STA_CONNECTING, true, true));
}

void test_grace_is_not_carved_out(void) {
    // By GRACE the device is on the LAN and the AP is a convenience, so the
    // justification for opening up (user cannot reach it any other way) is gone.
    TEST_ASSERT_FALSE(isAuthBypassAllowed(State::GRACE, true, false));
}

// ============================================================================
// DNS lifecycle
// ============================================================================

void test_dns_runs_while_ap_is_up_and_sta_is_down(void) {
    Context context = unprovisionedContext();
    TEST_ASSERT_TRUE(isDnsAllowed(context, false));
}

void test_dns_stops_once_sta_connects(void) {
    Context context = unprovisionedContext();
    // Running a catch-all resolver during APSTA would answer every name on the
    // customer's LAN with the AP address.
    TEST_ASSERT_FALSE(isDnsAllowed(context, true));
}

void test_dns_never_runs_without_an_ap(void) {
    Context context = provisionedContext();
    TEST_ASSERT_FALSE(isDnsAllowed(context, false));
}

// ============================================================================
// Subnet overlap and selection
// ============================================================================

void test_identical_subnets_overlap(void) {
    TEST_ASSERT_TRUE(subnetsOverlap(ip(172, 31, 42, 1), 24, ip(172, 31, 42, 50), 24));
}

void test_adjacent_subnets_do_not_overlap(void) {
    TEST_ASSERT_FALSE(subnetsOverlap(ip(172, 31, 42, 1), 24, ip(172, 31, 43, 1), 24));
}

void test_wider_network_containing_narrower_overlaps(void) {
    // A /16 LAN swallows the /24 candidate sitting inside it.
    TEST_ASSERT_TRUE(subnetsOverlap(ip(172, 31, 42, 1), 24, ip(172, 31, 0, 1), 16));
}

void test_default_candidate_is_chosen_on_a_typical_lan(void) {
    Subnet chosen{};
    Subnet lan[1] = {{ip(192, 168, 1, 50), 24}};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(lan, 1, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(172, 31, 42, 1), chosen.address);
    TEST_ASSERT_EQUAL_UINT8(24, chosen.cidr);
}

void test_colliding_lan_advances_to_the_next_candidate(void) {
    Subnet chosen{};
    Subnet lan[1] = {{ip(172, 31, 42, 10), 24}};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(lan, 1, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(172, 31, 43, 1), chosen.address);
}

void test_foreign_restored_static_ip_is_avoided(void) {
    // wifi_ns is backed up and restored, and restore runs before WiFi comes up, so
    // a backup from another LAN pushes an address the live STA lease never shows.
    Subnet chosen{};
    Subnet foreignStatic[1] = {{ip(172, 31, 42, 7), 24}};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(foreignStatic, 1, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(172, 31, 43, 1), chosen.address);
}

void test_static_ip_and_live_lease_are_both_avoided(void) {
    Subnet chosen{};
    Subnet both[2] = {{ip(172, 31, 42, 10), 24}, {ip(172, 31, 43, 9), 24}};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(both, 2, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(10, 42, 42, 1), chosen.address);
}

void test_lan_wider_than_a_slash_24_is_not_ignored(void) {
    // The /24 to /28 limit constrains the AP's own address, not the networks we
    // compare against. A home LAN on a /16 is ordinary, and skipping the comparison
    // because its prefix is "out of range" would hand back a colliding subnet.
    Subnet chosen{};
    Subnet wideLan[1] = {{ip(172, 31, 0, 1), 16}};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(wideLan, 1, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(10, 42, 42, 1), chosen.address);
}

void test_every_candidate_blocked_fails_closed(void) {
    // Contrived by construction - two halves of the address space - but it is the
    // branch that matters: falling back to a colliding default would route LAN
    // traffic out of the AP, because lwIP takes the first matching netif and
    // netif_add prepends the interface raised last.
    Subnet chosen{};
    Subnet halves[2] = {{ip(10, 0, 0, 0), 8}, {ip(128, 0, 0, 0), 1}};
    TEST_ASSERT_FALSE(selectApSubnetAvoiding(halves, 2, chosen));
}

void test_avoiding_four_networks_finds_the_free_candidate(void) {
    // The Pro shape: live STA, WiFi static, live ETH lease, ETH static - four
    // occupied networks blocking the first three candidates.
    Subnet occupied[4] = {
        {ip(172, 31, 42, 10), 24},  // live STA
        {ip(172, 31, 43, 9), 24},   // WiFi static
        {ip(10, 42, 42, 7), 24},    // live ETH lease
        {ip(192, 168, 1, 5), 24},   // ETH static (does not collide with any candidate)
    };
    Subnet chosen{};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(occupied, 4, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(192, 168, 242, 1), chosen.address);
}

void test_avoiding_with_no_networks_picks_the_default(void) {
    Subnet chosen{};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(nullptr, 0, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(172, 31, 42, 1), chosen.address);
}

void test_avoiding_all_candidates_fails_closed(void) {
    Subnet occupied[2] = {
        {ip(10, 0, 0, 0), 8},
        {ip(128, 0, 0, 0), 1},
    };
    Subnet chosen{};
    TEST_ASSERT_FALSE(selectApSubnetAvoiding(occupied, 2, chosen));
}

void test_avoiding_skips_malformed_comparison_prefixes(void) {
    // A cidr of 0 is not a usable comparison network and must be ignored, not
    // treated as "overlaps everything".
    Subnet occupied[1] = {{ip(172, 31, 42, 10), 0}};
    Subnet chosen{};
    TEST_ASSERT_TRUE(selectApSubnetAvoiding(occupied, 1, chosen));
    TEST_ASSERT_EQUAL_UINT32(ip(172, 31, 42, 1), chosen.address);
}


// ============================================================================
// Ethernet commissioning and wired-reachability inputs (Home Pro)
// ============================================================================

void test_commissioned_device_without_credentials_is_not_unprovisioned(void) {
    Context context;
    init(context, false, 0, true); // no WiFi credentials, but wire-commissioned
    TEST_ASSERT_TRUE(context.state != State::UNPROVISIONED);
    TEST_ASSERT_FALSE(context.apRaised);
}

void test_commissioned_device_wire_loss_lands_in_ap_assist_with_auth(void) {
    Context context;
    init(context, false, 0, true);

    // No credentials to associate with: every attempt fails. The device must
    // settle into AP_ASSIST (recovery AP, full auth) - never UNPROVISIONED,
    // which would hand the credentials carve-out to anyone in radio range.
    for (int i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        onEvent(context, Event::STA_ATTEMPT_FAILED, kMinute);
        TEST_ASSERT_TRUE(context.state != State::UNPROVISIONED);
    }
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
    TEST_ASSERT_FALSE(isAuthBypassAllowed(context.state, true, false));
}

void test_commissioned_device_credentials_cleared_stays_provisioned(void) {
    Context context;
    init(context, true, 0, true);
    onEvent(context, Event::STA_CONNECTED, kMinute);

    // A WiFi reset on an in-service wired device must not reopen the carve-out:
    // only a factory reset decommissions.
    onEvent(context, Event::CREDENTIALS_CLEARED, 2 * kMinute);
    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
    TEST_ASSERT_FALSE(isAuthBypassAllowed(context.state, true, false));
}

void test_uncommissioned_credentials_cleared_still_reopens_provisioning(void) {
    Context context = provisionedContext();
    onEvent(context, Event::CREDENTIALS_CLEARED, kMinute);
    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state); // Home behavior unchanged
}

void test_wired_reachable_suppresses_ap_raise(void) {
    Context context = unprovisionedContext();
    tearDownAp(context, kMinute);
    TEST_ASSERT_TRUE(shouldRaiseAp(context, 2 * kMinute));
    TEST_ASSERT_FALSE(shouldRaiseAp(context, 2 * kMinute, true /* wiredReachable */));
}

void test_wired_reachable_tears_ap_down(void) {
    Context context = unprovisionedContext();
    TEST_ASSERT_TRUE(context.apRaised);
    TEST_ASSERT_FALSE(shouldTearDownAp(context, kMinute));
    TEST_ASSERT_TRUE(shouldTearDownAp(context, kMinute, true /* wiredReachable */));
}

void test_wired_link_without_address_holds_raise_only_during_boot_grace(void) {
    Context context = unprovisionedContext();
    tearDownAp(context, 0);

    // Cable in, DHCP negotiating: no AP blip inside the boot grace window...
    TEST_ASSERT_FALSE(shouldRaiseAp(context, WIFI_PROVISIONING_WIRED_DHCP_GRACE_MS - 1, false, true));
    // ...but a link that never leases counts as unreachable after it.
    TEST_ASSERT_TRUE(shouldRaiseAp(context, WIFI_PROVISIONING_WIRED_DHCP_GRACE_MS, false, true));
}

void test_wired_inputs_default_to_home_behavior(void) {
    // The default arguments are the Home code path: identical to pre-Ethernet.
    Context context = unprovisionedContext();
    tearDownAp(context, kMinute);
    TEST_ASSERT_EQUAL(shouldRaiseAp(context, 2 * kMinute), shouldRaiseAp(context, 2 * kMinute, false, false));
    raiseAp(context, 3 * kMinute);
    TEST_ASSERT_EQUAL(shouldTearDownAp(context, 4 * kMinute), shouldTearDownAp(context, 4 * kMinute, false));
}

void test_candidates_all_sit_in_the_supported_cidr_range(void) {
    for (size_t i = 0; i < candidateSubnetCount(); i++) {
        Subnet candidate = candidateSubnet(i);
        // NetworkInterface::config() hard-fails outside /24 to /28.
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(WIFI_PROVISIONING_MIN_CIDR, candidate.cidr);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(WIFI_PROVISIONING_MAX_CIDR, candidate.cidr);
    }
}

void test_out_of_range_index_returns_empty_subnet(void) {
    Subnet candidate = candidateSubnet(candidateSubnetCount());
    TEST_ASSERT_EQUAL_UINT32(0, candidate.address);
    TEST_ASSERT_EQUAL_UINT8(0, candidate.cidr);
}

// Regression: shouldRaiseAp() is UNPROVISIONED-only and goes false the moment the AP is
// flagged, so a caller that polls ONLY that predicate never raises the radio for a device
// that lost its network - it goes silent and unreachable. The contract callers must honour
// is "reconcile against context.apRaised", not "poll shouldRaiseAp()". These pin that.

void test_ap_assist_flags_the_ap_without_shouldraiseap_ever_being_true(void) {
    Context context;
    init(context, true, 0); // Had credentials: this is an in-service device losing its network

    for (uint32_t i = 0; i < WIFI_PROVISIONING_AP_RAISE_THRESHOLD; i++) {
        // A caller polling this predicate would raise nothing, at any point.
        TEST_ASSERT_FALSE(shouldRaiseAp(context, 1000));
        onEvent(context, Event::STA_ATTEMPT_FAILED, 1000);
    }

    TEST_ASSERT_EQUAL(State::AP_ASSIST, context.state);
    TEST_ASSERT_TRUE(context.apRaised);          // The decision IS recorded here
    TEST_ASSERT_FALSE(shouldRaiseAp(context, 1000)); // ...and never here
}

void test_unprovisioned_also_flags_the_ap_on_the_context(void) {
    Context context;
    init(context, false, 0);

    onEvent(context, Event::STA_ATTEMPT_FAILED, 500);

    TEST_ASSERT_EQUAL(State::UNPROVISIONED, context.state);
    TEST_ASSERT_TRUE(context.apRaised);
}

// Netmask <-> CIDR. These sit under the AP-raise path, which reads a live netmask off the
// STA interface and must turn it into a prefix the overlap check can use. Getting this
// wrong picks an AP subnet that collides with the LAN, which silently blackholes traffic.

void test_common_netmasks_convert_to_prefix_lengths(void) {
    TEST_ASSERT_EQUAL_UINT8(24, cidrFromNetmask(0xFFFFFF00u));  // 255.255.255.0
    TEST_ASSERT_EQUAL_UINT8(16, cidrFromNetmask(0xFFFF0000u));  // 255.255.0.0
    TEST_ASSERT_EQUAL_UINT8(8, cidrFromNetmask(0xFF000000u));   // 255.0.0.0
    TEST_ASSERT_EQUAL_UINT8(28, cidrFromNetmask(0xFFFFFFF0u));  // 255.255.255.240
    TEST_ASSERT_EQUAL_UINT8(23, cidrFromNetmask(0xFFFFFE00u));  // 255.255.254.0
}

void test_non_contiguous_netmask_is_rejected(void) {
    // A popcount-based implementation returns 16 for this and hands back a prefix that
    // describes a different network than the mask does.
    TEST_ASSERT_EQUAL_UINT8(0, cidrFromNetmask(0xFF00FF00u));
    TEST_ASSERT_EQUAL_UINT8(0, cidrFromNetmask(0x0FFFFFFFu));
    TEST_ASSERT_EQUAL_UINT8(0, cidrFromNetmask(0xFFFFFF01u));
}

void test_all_ones_and_all_zeros_netmasks(void) {
    TEST_ASSERT_EQUAL_UINT8(32, cidrFromNetmask(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT8(0, cidrFromNetmask(0x00000000u));
}

void test_prefix_lengths_convert_to_netmasks(void) {
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u, netmaskFromCidr(24));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF0u, netmaskFromCidr(28));
    TEST_ASSERT_EQUAL_UINT32(0xFFFF0000u, netmaskFromCidr(16));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, netmaskFromCidr(32));
}

void test_cidr_zero_does_not_shift_by_the_operand_width(void) {
    // 0xFFFFFFFF << 32 is undefined behaviour, and on x86 the shift count is taken mod 32,
    // so a naive implementation returns 0xFFFFFFFF here instead of 0.
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, netmaskFromCidr(0));
}

void test_netmask_and_cidr_round_trip(void) {
    for (uint8_t cidr = 0; cidr <= 32; cidr++) {
        TEST_ASSERT_EQUAL_UINT8(cidr, cidrFromNetmask(netmaskFromCidr(cidr)));
    }
}

void test_every_state_has_a_distinct_wire_name(void) {
    for (uint8_t i = 0; i < STATE_COUNT; i++) {
        const char *name = stateName((State)i);
        TEST_ASSERT_NOT_NULL(name);
        for (uint8_t j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(name, stateName((State)j)));
        }
    }
}

void test_out_of_range_state_falls_back_to_unprovisioned(void) {
    TEST_ASSERT_EQUAL_STRING(stateName(State::UNPROVISIONED), stateName((State)STATE_COUNT));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_boot_with_credentials_starts_connecting_without_ap);
    RUN_TEST(test_boot_without_credentials_raises_ap);

    RUN_TEST(test_retry_attempts_and_ap_triggers_are_separate_fields);
    RUN_TEST(test_ap_raises_only_after_threshold_failures);
    RUN_TEST(test_successful_connection_clears_both_counters);
    RUN_TEST(test_failed_credentials_keep_unprovisioned_device_open);
    RUN_TEST(test_device_provisioned_this_boot_falls_back_to_ap_assist);

    RUN_TEST(test_ap_assist_ap_is_not_time_limited);
    RUN_TEST(test_ap_assist_re_raises_immediately_if_lowered);
    RUN_TEST(test_unprovisioned_ap_re_raises_immediately_if_lowered);
    RUN_TEST(test_ap_is_not_raised_once_connected);
    RUN_TEST(test_no_teardown_while_ap_is_down);

    RUN_TEST(test_connect_with_ap_up_enters_grace);
    RUN_TEST(test_connect_with_ap_down_goes_straight_to_sta_only);
    RUN_TEST(test_grace_expires_at_the_configured_window);
    RUN_TEST(test_expired_grace_teardown_settles_on_sta_only);
    RUN_TEST(test_last_client_leaving_ends_grace_immediately);
    RUN_TEST(test_clearing_credentials_returns_to_unprovisioned_with_the_ap_up);
    RUN_TEST(test_failure_after_clearing_credentials_stays_in_setup);
    RUN_TEST(test_clearing_credentials_with_the_ap_already_up_is_idempotent);
    RUN_TEST(test_grace_is_measured_from_the_connect);
    RUN_TEST(test_losing_sta_during_grace_returns_to_connecting);
    RUN_TEST(test_losing_sta_again_keeps_ap_assist);
    RUN_TEST(test_clock_going_backwards_does_not_expire_timers);

    RUN_TEST(test_serving_on_the_ap_alone_counts_as_serviceable);
    RUN_TEST(test_serving_on_sta_alone_counts_as_serviceable);
    RUN_TEST(test_no_interface_is_not_serviceable);

    RUN_TEST(test_unprovisioned_ap_request_is_carved_out);
    RUN_TEST(test_ap_assist_is_never_carved_out);
    RUN_TEST(test_sta_netif_is_never_carved_out);
    RUN_TEST(test_ota_is_never_carved_out_in_any_state);
    RUN_TEST(test_grace_is_not_carved_out);

    RUN_TEST(test_dns_runs_while_ap_is_up_and_sta_is_down);
    RUN_TEST(test_dns_stops_once_sta_connects);
    RUN_TEST(test_dns_never_runs_without_an_ap);

    RUN_TEST(test_identical_subnets_overlap);
    RUN_TEST(test_adjacent_subnets_do_not_overlap);
    RUN_TEST(test_wider_network_containing_narrower_overlaps);
    RUN_TEST(test_default_candidate_is_chosen_on_a_typical_lan);
    RUN_TEST(test_colliding_lan_advances_to_the_next_candidate);
    RUN_TEST(test_foreign_restored_static_ip_is_avoided);
    RUN_TEST(test_static_ip_and_live_lease_are_both_avoided);
    RUN_TEST(test_lan_wider_than_a_slash_24_is_not_ignored);
    RUN_TEST(test_every_candidate_blocked_fails_closed);
    RUN_TEST(test_avoiding_four_networks_finds_the_free_candidate);
    RUN_TEST(test_avoiding_with_no_networks_picks_the_default);
    RUN_TEST(test_avoiding_all_candidates_fails_closed);
    RUN_TEST(test_avoiding_skips_malformed_comparison_prefixes);
    RUN_TEST(test_commissioned_device_without_credentials_is_not_unprovisioned);
    RUN_TEST(test_commissioned_device_wire_loss_lands_in_ap_assist_with_auth);
    RUN_TEST(test_commissioned_device_credentials_cleared_stays_provisioned);
    RUN_TEST(test_uncommissioned_credentials_cleared_still_reopens_provisioning);
    RUN_TEST(test_wired_reachable_suppresses_ap_raise);
    RUN_TEST(test_wired_reachable_tears_ap_down);
    RUN_TEST(test_wired_link_without_address_holds_raise_only_during_boot_grace);
    RUN_TEST(test_wired_inputs_default_to_home_behavior);
    RUN_TEST(test_candidates_all_sit_in_the_supported_cidr_range);
    RUN_TEST(test_out_of_range_index_returns_empty_subnet);

    RUN_TEST(test_ap_assist_flags_the_ap_without_shouldraiseap_ever_being_true);
    RUN_TEST(test_unprovisioned_also_flags_the_ap_on_the_context);

    RUN_TEST(test_common_netmasks_convert_to_prefix_lengths);
    RUN_TEST(test_non_contiguous_netmask_is_rejected);
    RUN_TEST(test_all_ones_and_all_zeros_netmasks);
    RUN_TEST(test_prefix_lengths_convert_to_netmasks);
    RUN_TEST(test_cidr_zero_does_not_shift_by_the_operand_width);
    RUN_TEST(test_netmask_and_cidr_round_trip);

    RUN_TEST(test_every_state_has_a_distinct_wire_name);
    RUN_TEST(test_out_of_range_state_falls_back_to_unprovisioned);

    return UNITY_END();
}
