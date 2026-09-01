// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for InterfaceArbitration (Home Pro ETH-primary / STA-fallback).
// Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>

#include <cstdint>

#include "interface_arbitration.h"

using namespace InterfaceArbitration;

static Context ctx;

void setUp(void) { init(ctx); }
void tearDown(void) {}

static const uint64_t HOLDDOWN = INTERFACE_ARBITRATION_ETH_HOLDDOWN_MS;

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


    RUN_TEST(test_boot_time_zero_still_counts_as_serviceable);
    RUN_TEST(test_interface_names);

    return UNITY_END();
}
