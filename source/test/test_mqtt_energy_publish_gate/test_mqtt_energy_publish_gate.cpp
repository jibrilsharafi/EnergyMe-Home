// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for MqttEnergyPublishGate::shouldPublishNow. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "mqtt_energy_publish_gate.h"

using namespace MqttEnergyPublishGate;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Before the boundary: never publish, regardless of freshness
// ============================================================================

void test_before_boundary_does_not_publish_even_if_fresh(void) {
    TEST_ASSERT_FALSE(shouldPublishNow(119, 120, 10, true));
}

void test_before_boundary_does_not_publish_when_not_fresh(void) {
    TEST_ASSERT_FALSE(shouldPublishNow(100, 120, 10, false));
}

// ============================================================================
// At/after the boundary, all channels fresh: publish immediately
// ============================================================================

void test_at_boundary_with_all_fresh_publishes_immediately(void) {
    TEST_ASSERT_TRUE(shouldPublishNow(120, 120, 10, true));
}

void test_shortly_after_boundary_with_all_fresh_publishes(void) {
    TEST_ASSERT_TRUE(shouldPublishNow(122, 120, 10, true));
}

// ============================================================================
// At/after the boundary, not all fresh yet, before the deadline: keep waiting
// ============================================================================

void test_after_boundary_not_fresh_before_deadline_waits(void) {
    // 5 s past a 120 s boundary, 10 s deadline, still not fresh -> wait
    TEST_ASSERT_FALSE(shouldPublishNow(125, 120, 10, false));
}

void test_one_second_before_deadline_still_waits(void) {
    TEST_ASSERT_FALSE(shouldPublishNow(129, 120, 10, false));
}

// ============================================================================
// Deadline reached, still not fresh: publish anyway (starved-channel case)
// ============================================================================

void test_at_deadline_not_fresh_publishes_anyway(void) {
    TEST_ASSERT_TRUE(shouldPublishNow(130, 120, 10, false));
}

void test_past_deadline_not_fresh_publishes_anyway(void) {
    TEST_ASSERT_TRUE(shouldPublishNow(200, 120, 10, false));
}

// ============================================================================
// Zero deadline: publish immediately at the boundary regardless of freshness
// ============================================================================

void test_zero_deadline_publishes_immediately_at_boundary(void) {
    TEST_ASSERT_TRUE(shouldPublishNow(120, 120, 0, false));
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_before_boundary_does_not_publish_even_if_fresh);
    RUN_TEST(test_before_boundary_does_not_publish_when_not_fresh);

    RUN_TEST(test_at_boundary_with_all_fresh_publishes_immediately);
    RUN_TEST(test_shortly_after_boundary_with_all_fresh_publishes);

    RUN_TEST(test_after_boundary_not_fresh_before_deadline_waits);
    RUN_TEST(test_one_second_before_deadline_still_waits);

    RUN_TEST(test_at_deadline_not_fresh_publishes_anyway);
    RUN_TEST(test_past_deadline_not_fresh_publishes_anyway);

    RUN_TEST(test_zero_deadline_publishes_immediately_at_boundary);

    return UNITY_END();
}
