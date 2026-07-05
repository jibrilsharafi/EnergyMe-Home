// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for MqttGridSchedule::nextAlignedBoundarySeconds. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "mqtt_grid_schedule.h"

using namespace MqttGridSchedule;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Boundary just crossed
// ============================================================================

void test_one_second_past_boundary_rolls_to_next_minute(void) {
    // 121 s is 1 s past the 120 s boundary; next boundary is 180 s
    TEST_ASSERT_EQUAL_UINT64(180, nextAlignedBoundarySeconds(121, 60));
}

void test_one_second_before_boundary_rolls_to_that_boundary(void) {
    // 119 s is 1 s before the 120 s boundary; next boundary is 120 s
    TEST_ASSERT_EQUAL_UINT64(120, nextAlignedBoundarySeconds(119, 60));
}

// ============================================================================
// Exactly on a multiple: next boundary is strictly after "now"
// ============================================================================

void test_exact_multiple_rolls_to_the_following_boundary(void) {
    // 120 s is itself a boundary; the *next* one strictly after it is 180 s
    TEST_ASSERT_EQUAL_UINT64(180, nextAlignedBoundarySeconds(120, 60));
}

void test_zero_rolls_to_first_boundary(void) {
    TEST_ASSERT_EQUAL_UINT64(60, nextAlignedBoundarySeconds(0, 60));
}

// ============================================================================
// Large gaps (long outage): still lands on a real wall-clock boundary, not a
// value derived from a stale prior deadline.
// ============================================================================

void test_large_gap_lands_on_next_boundary_not_stale_increment(void) {
    // A device reconnecting after a long outage at an arbitrary unix time
    // (not a round number) must resync to the next real minute boundary.
    uint64_t now = 1700000015ULL; // arbitrary, not a multiple of 60
    uint64_t next = nextAlignedBoundarySeconds(now, 60);
    TEST_ASSERT_EQUAL_UINT64(0, next % 60);
    TEST_ASSERT_TRUE(next > now);
    TEST_ASSERT_TRUE(next - now <= 60);
}

// ============================================================================
// Alignment period matches MQTT_GRID_PUBLISH_ALIGN_SECONDS (60 s)
// ============================================================================

void test_sixty_second_alignment_matches_grid_publish_period(void) {
    // Two consecutive boundaries are exactly 60 s apart under 60 s alignment
    uint64_t first = nextAlignedBoundarySeconds(0, 60);
    uint64_t second = nextAlignedBoundarySeconds(first, 60);
    TEST_ASSERT_EQUAL_UINT64(60, second - first);
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_one_second_past_boundary_rolls_to_next_minute);
    RUN_TEST(test_one_second_before_boundary_rolls_to_that_boundary);

    RUN_TEST(test_exact_multiple_rolls_to_the_following_boundary);
    RUN_TEST(test_zero_rolls_to_first_boundary);

    RUN_TEST(test_large_gap_lands_on_next_boundary_not_stale_increment);

    RUN_TEST(test_sixty_second_alignment_matches_grid_publish_period);

    return UNITY_END();
}
