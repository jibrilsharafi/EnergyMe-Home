// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for InterfaceArbitration (Home Pro ETH-primary / STA-fallback).
// Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "interface_arbitration.h"

using namespace InterfaceArbitration;

static Context ctx;

void setUp(void) { init(ctx); }
void tearDown(void) {}

static const uint64_t HOLDDOWN = INTERFACE_ARBITRATION_ETH_HOLDDOWN_MS;
static const uint64_t LINGER = INTERFACE_ARBITRATION_STA_RELEASE_LINGER_MS;
static const uint64_t RELEASE = HOLDDOWN + LINGER; // wire uptime after which an engaged STA is let go

// Wired boot, then a cable pull with the STA taking over: the common starting
// point of the failback tests. Leaves WiFi holding the route.
static void bootWiredThenFailOverToSta(void) {
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);
    onEthState(ctx, false, false, 60000);
    onStaState(ctx, true);
    evaluateAndApply(ctx, 61000);
}

// ============================================================================
// Boot / bring-up
// ============================================================================

void test_boot_nothing_up_prefers_none(void) {
    Decision d = evaluateAndApply(ctx, 1000);
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)d.preferred);
    TEST_ASSERT_FALSE(d.switchRequired);
}

void test_eth_taken_immediately_at_boot(void) {
    onEthState(ctx, true, true, 1000);
    Decision d = evaluateAndApply(ctx, 1001);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
    TEST_ASSERT_TRUE(d.switchRequired);
}

void test_sta_taken_when_no_eth(void) {
    onStaState(ctx, true);
    Decision d = evaluateAndApply(ctx, 1001);
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)d.preferred);
}

void test_eth_wins_when_both_come_up_at_boot(void) {
    onStaState(ctx, true);
    onEthState(ctx, true, true, 1000);
    Decision d = evaluateAndApply(ctx, 1001);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
}

// ============================================================================
// Serviceability: link without address is not a usable interface
// ============================================================================

void test_link_without_address_is_not_serviceable(void) {
    onEthState(ctx, true, false, 1000);
    TEST_ASSERT_FALSE(isEthServiceable(ctx));
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)evaluateAndApply(ctx, 2000).preferred);
}

void test_static_address_counts_like_a_lease(void) {
    // The caller reports hasAddress for an applied static config exactly as for
    // a DHCP lease; arbitration must not care which it was.
    onEthState(ctx, true, true, 1000);
    TEST_ASSERT_TRUE(isEthServiceable(ctx));
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)evaluateAndApply(ctx, 1001).preferred);
}

void test_address_lost_makes_eth_unserviceable(void) {
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);
    onEthState(ctx, true, false, 5000); // link stays, lease expires unrenewed
    TEST_ASSERT_FALSE(isEthServiceable(ctx));
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)evaluateAndApply(ctx, 5001).preferred);
}

// ============================================================================
// Failover: cable pull -> STA
// ============================================================================

void test_cable_pull_fails_over_to_sta(void) {
    onEthState(ctx, true, true, 1000);
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)ctx.active);

    onEthState(ctx, false, false, 60000);
    Decision d = evaluateAndApply(ctx, 60001);
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)d.preferred);
    TEST_ASSERT_TRUE(d.switchRequired);
}

void test_cable_pull_with_no_sta_goes_dark(void) {
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);
    onEthState(ctx, false, false, 60000);
    Decision d = evaluateAndApply(ctx, 60001);
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)d.preferred);
}

// ============================================================================
// Failback: cable return respects the hold-down while STA works
// ============================================================================

void test_cable_return_does_not_preempt_sta_before_holddown(void) {
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);

    onEthState(ctx, true, true, 10000);
    Decision d = evaluateAndApply(ctx, 10000 + HOLDDOWN - 1);
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)d.preferred);
    TEST_ASSERT_FALSE(d.switchRequired);
}

void test_cable_return_takes_over_after_holddown(void) {
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);

    onEthState(ctx, true, true, 10000);
    Decision d = evaluateAndApply(ctx, 10000 + HOLDDOWN);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
    TEST_ASSERT_TRUE(d.switchRequired);
}

void test_cable_return_immediate_when_sta_is_down(void) {
    // Hold-down only protects a WORKING fallback. With STA gone, the wire is
    // taken the moment it is serviceable.
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);
    onStaState(ctx, false);

    onEthState(ctx, true, true, 20050);
    Decision d = evaluateAndApply(ctx, 20051);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
}

// ============================================================================
// Flap filter
// ============================================================================

void test_flapping_link_never_steals_the_route(void) {
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);

    // Link bounces every 2 s for a minute: each drop restarts the hold-down.
    uint64_t t = 10000;
    for (int i = 0; i < 30; i++) {
        onEthState(ctx, true, true, t);
        Decision d = evaluateAndApply(ctx, t + 1000);
        TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)d.preferred);
        onEthState(ctx, false, false, t + 2000);
        t += 2000;
    }
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)ctx.active);
}

void test_flap_then_stable_link_takes_over(void) {
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);

    onEthState(ctx, true, true, 10000);
    onEthState(ctx, false, false, 12000);
    onEthState(ctx, true, true, 14000); // stable from here

    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)evaluateAndApply(ctx, 14000 + HOLDDOWN - 1).preferred);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)evaluateAndApply(ctx, 14000 + HOLDDOWN).preferred);
}

// ============================================================================
// Home behavior: no Ethernet inputs -> identical to today's STA-only logic
// ============================================================================

void test_home_sta_connect_then_drop(void) {
    // Ethernet callbacks never fire on Home; the outputs must reduce to the
    // existing behavior: STA when connected, nothing otherwise.
    onStaState(ctx, true);
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)evaluateAndApply(ctx, 1001).preferred);

    onStaState(ctx, false);
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)evaluateAndApply(ctx, 2001).preferred);
}

// ============================================================================
// AP raise input
// ============================================================================




// ============================================================================
// STA wanted: one station-side interface at a time
// ============================================================================

void test_sta_wanted_at_boot_before_any_wire(void) {
    // No wire yet: arbitration alone always asks for the station. Holding it back
    // while the wire comes up is the caller's boot hold, not this module's job.
    TEST_ASSERT_TRUE(isStaWanted(ctx, 0));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 1000));

    onEthState(ctx, true, false, 2000); // cable in, no lease: still no usable wire
    TEST_ASSERT_TRUE(isStaWanted(ctx, 2001));
    TEST_ASSERT_FALSE(ctx.staEngaged);
}

void test_wired_boot_never_wants_sta(void) {
    onEthState(ctx, true, false, 500); // link first, lease a moment later
    onEthState(ctx, true, true, 1000);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 1000));

    const uint64_t checkpoints[] = {
        1001, 1000 + HOLDDOWN - 1, 1000 + HOLDDOWN, 1000 + RELEASE - 1, 1000 + RELEASE,
        1000 + 24ULL * 60ULL * 60ULL * 1000ULL,
    };
    for (size_t i = 0; i < sizeof(checkpoints) / sizeof(checkpoints[0]); i++) {
        evaluateAndApply(ctx, checkpoints[i]);
        TEST_ASSERT_FALSE(isStaWanted(ctx, checkpoints[i]));
    }
    TEST_ASSERT_FALSE(ctx.staEngaged);
}

void test_cable_pull_wants_sta_immediately(void) {
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 59999));

    onEthState(ctx, false, false, 60000);
    // Wanted in the same millisecond, before any evaluate call.
    TEST_ASSERT_TRUE(isStaWanted(ctx, 60000));
    TEST_ASSERT_TRUE(ctx.staEngaged);
}

void test_address_loss_wants_sta(void) {
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);

    onEthState(ctx, true, false, 5000); // link stays, lease expires unrenewed
    TEST_ASSERT_TRUE(isStaWanted(ctx, 5000));
    TEST_ASSERT_TRUE(ctx.staEngaged);
}

void test_cable_return_keeps_sta_until_holddown_plus_linger(void) {
    bootWiredThenFailOverToSta();

    onEthState(ctx, true, true, 100000);
    evaluateAndApply(ctx, 100000);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000 + HOLDDOWN));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000 + RELEASE - 1));
    TEST_ASSERT_FALSE(isStaWanted(ctx, 100000 + RELEASE));

    evaluateAndApply(ctx, 100000 + RELEASE);
    TEST_ASSERT_FALSE(ctx.staEngaged);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 100000 + RELEASE + 1));
}

void test_route_switch_precedes_sta_release(void) {
    // The station must still be up when the route moves, so the sessions riding
    // it reconnect over the wire instead of being cut with nowhere to go.
    bootWiredThenFailOverToSta();
    onEthState(ctx, true, true, 100000);

    Decision before = evaluateAndApply(ctx, 100000 + HOLDDOWN - 1);
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)before.preferred);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000 + HOLDDOWN - 1));

    Decision atSwitch = evaluateAndApply(ctx, 100000 + HOLDDOWN);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)atSwitch.preferred);
    TEST_ASSERT_TRUE(atSwitch.switchRequired);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000 + HOLDDOWN));

    Decision atRelease = evaluateAndApply(ctx, 100000 + RELEASE);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)atRelease.preferred);
    TEST_ASSERT_FALSE(atRelease.switchRequired);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 100000 + RELEASE));
}

void test_flap_never_releases_sta(void) {
    bootWiredThenFailOverToSta();

    // Fast flap: up 1 s, down 1 s. The route never moves either.
    uint64_t t = 100000;
    for (int i = 0; i < 30; i++) {
        onEthState(ctx, true, true, t);
        evaluateAndApply(ctx, t + 999);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + 999));
        onEthState(ctx, false, false, t + 1000);
        evaluateAndApply(ctx, t + 1000);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + 1000));
        t += 2000;
    }
    TEST_ASSERT_EQUAL(Interface::WIFI_STATION, (Interface)ctx.active);

    // Slow flap: each uptime outlasts the hold-down (the route does move to the
    // wire) but never the release window, so the station is still never let go.
    const uint64_t up = HOLDDOWN + 1000;
    TEST_ASSERT_TRUE(up < RELEASE);
    for (int i = 0; i < 10; i++) {
        onEthState(ctx, true, true, t);
        Decision d = evaluateAndApply(ctx, t + up - 1);
        TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + up - 1));
        onEthState(ctx, false, false, t + up);
        evaluateAndApply(ctx, t + up);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + up));
        t += up + 1000;
    }
    TEST_ASSERT_TRUE(ctx.staEngaged);
}

void test_flap_with_sta_down_takes_eth_but_keeps_sta_wanted(void) {
    // The station never associated after the pull. Each return takes the wire at
    // once (nothing else works), yet the station stays wanted: the engagement
    // comes from losing the wire, not from the station having connected.
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);
    onEthState(ctx, false, false, 60000);
    TEST_ASSERT_EQUAL(Interface::NONE, (Interface)evaluateAndApply(ctx, 60001).preferred);

    uint64_t t = 62000;
    for (int i = 0; i < 10; i++) {
        onEthState(ctx, true, true, t);
        Decision d = evaluateAndApply(ctx, t + 1);
        TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
        TEST_ASSERT_TRUE(d.switchRequired);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + 1));

        onEthState(ctx, false, false, t + 2000);
        evaluateAndApply(ctx, t + 2001);
        TEST_ASSERT_TRUE(isStaWanted(ctx, t + 2001));
        t += 4000;
    }

    // Stable from here: a single release, one full window after the last return.
    onEthState(ctx, true, true, t);
    evaluateAndApply(ctx, t + 1);
    TEST_ASSERT_TRUE(isStaWanted(ctx, t + RELEASE - 1));
    TEST_ASSERT_FALSE(isStaWanted(ctx, t + RELEASE));
}

void test_sta_connected_before_wire_engages(void) {
    // Station won the boot race (switch powered late): it is carrying sessions,
    // so the wire has to prove itself before the station goes.
    onStaState(ctx, true);
    evaluateAndApply(ctx, 5000);
    TEST_ASSERT_TRUE(ctx.staEngaged);

    onEthState(ctx, true, true, 30000);
    evaluateAndApply(ctx, 30000);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 30000));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 30000 + RELEASE - 1));
    TEST_ASSERT_FALSE(isStaWanted(ctx, 30000 + RELEASE));
}

void test_sta_attempt_in_flight_is_not_engaged(void) {
    // An attempt that has not connected protects nothing: when the wire arrives
    // first the station is dropped at once, never getting a second address.
    onStaState(ctx, false);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 5000));
    TEST_ASSERT_FALSE(ctx.staEngaged);

    onEthState(ctx, true, true, 8000);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 8000));
    evaluateAndApply(ctx, 8001);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 8001));
}

void test_bounce_restarts_release_clock(void) {
    bootWiredThenFailOverToSta();

    onEthState(ctx, true, true, 100000);
    evaluateAndApply(ctx, 100001);
    onEthState(ctx, false, false, 108000); // one bounce inside the window
    onEthState(ctx, true, true, 109000);
    evaluateAndApply(ctx, 109001);

    // The first return's window would have ended here; the bounce restarted it.
    TEST_ASSERT_TRUE(isStaWanted(ctx, 100000 + RELEASE));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 109000 + RELEASE - 1));
    TEST_ASSERT_FALSE(isStaWanted(ctx, 109000 + RELEASE));
}

void test_sta_connect_on_long_stable_wire_is_not_wanted(void) {
    // A credential check brings the station up on a wire that has served for
    // minutes. It must not earn a fresh linger, and the route must not move.
    onEthState(ctx, true, true, 1000);
    evaluateAndApply(ctx, 1001);

    const uint64_t later = 1000 + 10ULL * 60ULL * 1000ULL;
    evaluateAndApply(ctx, later);
    onStaState(ctx, true);
    TEST_ASSERT_FALSE(isStaWanted(ctx, later));

    Decision d = evaluateAndApply(ctx, later + 1);
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)d.preferred);
    TEST_ASSERT_FALSE(d.switchRequired);
    TEST_ASSERT_FALSE(ctx.staEngaged);
    TEST_ASSERT_FALSE(isStaWanted(ctx, later + 1));

    // The exit still works afterwards: a pull wants the station at once.
    onStaState(ctx, false);
    onEthState(ctx, false, false, later + 5000);
    TEST_ASSERT_TRUE(isStaWanted(ctx, later + 5000));
}

void test_home_context_always_wants_sta(void) {
    // Ethernet callbacks never fire on Home: the station is wanted at every
    // moment, connected or not, so today's STA-only behavior is unchanged.
    const uint64_t day = 24ULL * 60ULL * 60ULL * 1000ULL;

    TEST_ASSERT_TRUE(isStaWanted(ctx, 0));
    onStaState(ctx, true);
    evaluateAndApply(ctx, 1001);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 1001));
    TEST_ASSERT_TRUE(isStaWanted(ctx, 1001 + RELEASE));
    evaluateAndApply(ctx, day);
    TEST_ASSERT_TRUE(isStaWanted(ctx, day));

    onStaState(ctx, false);
    evaluateAndApply(ctx, day + 1);
    TEST_ASSERT_TRUE(isStaWanted(ctx, day + 1));
}

void test_sta_wanted_time_zero_clamp(void) {
    // A wire that comes up at millis()==0 is stamped 1, not 0 ("not serviceable").
    // A wired boot at time zero must still read as serviceable: no station.
    onEthState(ctx, true, true, 0);
    TEST_ASSERT_FALSE(isStaWanted(ctx, 0));

    // With an engaged station the window is timed off the clamped stamp, and a
    // clock reading before it counts as "no time has passed": the station stays.
    init(ctx);
    onStaState(ctx, true);
    onEthState(ctx, true, true, 0);
    TEST_ASSERT_EQUAL_UINT64(1, ctx.ethServiceableSinceMs);
    TEST_ASSERT_TRUE(isStaWanted(ctx, 0));
    evaluateAndApply(ctx, 0);
    TEST_ASSERT_TRUE(ctx.staEngaged);
    TEST_ASSERT_TRUE(isStaWanted(ctx, RELEASE));
    TEST_ASSERT_FALSE(isStaWanted(ctx, RELEASE + 1));
}

// ============================================================================
// Misc
// ============================================================================

void test_boot_time_zero_still_counts_as_serviceable(void) {
    onEthState(ctx, true, true, 0); // event at millis()==0
    TEST_ASSERT_TRUE(isEthServiceable(ctx));
    TEST_ASSERT_EQUAL(Interface::ETHERNET, (Interface)evaluateAndApply(ctx, 1).preferred);
}

void test_interface_names(void) {
    TEST_ASSERT_EQUAL_STRING("none", interfaceName(Interface::NONE));
    TEST_ASSERT_EQUAL_STRING("ethernet", interfaceName(Interface::ETHERNET));
    TEST_ASSERT_EQUAL_STRING("wifi", interfaceName(Interface::WIFI_STATION));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_boot_nothing_up_prefers_none);
    RUN_TEST(test_eth_taken_immediately_at_boot);
    RUN_TEST(test_sta_taken_when_no_eth);
    RUN_TEST(test_eth_wins_when_both_come_up_at_boot);

    RUN_TEST(test_link_without_address_is_not_serviceable);
    RUN_TEST(test_static_address_counts_like_a_lease);
    RUN_TEST(test_address_lost_makes_eth_unserviceable);

    RUN_TEST(test_cable_pull_fails_over_to_sta);
    RUN_TEST(test_cable_pull_with_no_sta_goes_dark);

    RUN_TEST(test_cable_return_does_not_preempt_sta_before_holddown);
    RUN_TEST(test_cable_return_takes_over_after_holddown);
    RUN_TEST(test_cable_return_immediate_when_sta_is_down);

    RUN_TEST(test_flapping_link_never_steals_the_route);
    RUN_TEST(test_flap_then_stable_link_takes_over);

    RUN_TEST(test_home_sta_connect_then_drop);

    RUN_TEST(test_sta_wanted_at_boot_before_any_wire);
    RUN_TEST(test_wired_boot_never_wants_sta);
    RUN_TEST(test_cable_pull_wants_sta_immediately);
    RUN_TEST(test_address_loss_wants_sta);
    RUN_TEST(test_cable_return_keeps_sta_until_holddown_plus_linger);
    RUN_TEST(test_route_switch_precedes_sta_release);
    RUN_TEST(test_flap_never_releases_sta);
    RUN_TEST(test_flap_with_sta_down_takes_eth_but_keeps_sta_wanted);
    RUN_TEST(test_sta_connected_before_wire_engages);
    RUN_TEST(test_sta_attempt_in_flight_is_not_engaged);
    RUN_TEST(test_bounce_restarts_release_clock);
    RUN_TEST(test_sta_connect_on_long_stable_wire_is_not_wanted);
    RUN_TEST(test_home_context_always_wants_sta);
    RUN_TEST(test_sta_wanted_time_zero_clamp);

    RUN_TEST(test_boot_time_zero_still_counts_as_serviceable);
    RUN_TEST(test_interface_names);

    return UNITY_END();
}
