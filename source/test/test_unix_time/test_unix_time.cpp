// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the pure unix-timestamp plausibility check. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "unix_time.h"

using namespace UnixTime;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// isValid - milliseconds (default)
// ============================================================================

void test_ms_exactly_at_floor_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(MIN_MILLISECONDS));
}

void test_ms_one_below_floor_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(MIN_MILLISECONDS - 1));
}

void test_ms_exactly_at_ceiling_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(MAX_MILLISECONDS));
}

void test_ms_one_above_ceiling_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(MAX_MILLISECONDS + 1));
}

void test_ms_zero_is_invalid(void) {
    // Unlike ShadowLogic::isPlausibleStartMeasuringUnixTimeMs, this function has no
    // "0 means unset" special case - 0 is just an implausible timestamp here.
    TEST_ASSERT_FALSE(isValid(0));
}

void test_ms_plausible_value_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(1700000000000ULL));
}

void test_ms_seconds_value_sent_by_mistake_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(1700000000ULL)); // ~1000x below the ms floor
}

void test_ms_microseconds_value_sent_by_mistake_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(1700000000000000ULL)); // ~1000x above the ms ceiling
}

// ============================================================================
// isValid - seconds
// ============================================================================

void test_seconds_exactly_at_floor_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(MIN_SECONDS, false));
}

void test_seconds_one_below_floor_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(MIN_SECONDS - 1, false));
}

void test_seconds_exactly_at_ceiling_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(MAX_SECONDS, false));
}

void test_seconds_one_above_ceiling_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(MAX_SECONDS + 1, false));
}

void test_seconds_plausible_value_is_valid(void) {
    TEST_ASSERT_TRUE(isValid(1700000000ULL, false));
}

void test_seconds_ms_value_sent_by_mistake_is_invalid(void) {
    TEST_ASSERT_FALSE(isValid(1700000000000ULL, false)); // ~1000x above the seconds ceiling
}

// ============================================================================
// UTC hour scheduling
// ============================================================================

static const uint64_t H_2026_09_22_05 = 1790053200ULL; // 2026-09-22T05:00:00Z

void test_until_next_hour_from_exact_hour_is_full_hour(void) {
    TEST_ASSERT_EQUAL_UINT64(MS_PER_HOUR, millisUntilNextUtcHour(H_2026_09_22_05 * 1000ULL));
}

void test_until_next_hour_ignores_half_hour_local_offset(void) {
    // 11:00 IST is 05:30Z: the next save is at 06:00Z, not at local 12:00
    TEST_ASSERT_EQUAL_UINT64(1800000ULL, millisUntilNextUtcHour((H_2026_09_22_05 + 1800ULL) * 1000ULL));
}

void test_distance_from_nearest_hour_on_both_sides(void) {
    TEST_ASSERT_EQUAL_UINT64(0ULL, millisFromNearestUtcHour(H_2026_09_22_05 * 1000ULL));
    TEST_ASSERT_EQUAL_UINT64(5000ULL, millisFromNearestUtcHour((H_2026_09_22_05 + 5ULL) * 1000ULL));
    TEST_ASSERT_EQUAL_UINT64(5000ULL, millisFromNearestUtcHour((H_2026_09_22_05 - 5ULL) * 1000ULL));
    TEST_ASSERT_EQUAL_UINT64(1800000ULL, millisFromNearestUtcHour((H_2026_09_22_05 + 1800ULL) * 1000ULL));
}

void test_nearest_hour_rounds_to_closest(void) {
    TEST_ASSERT_EQUAL_UINT64(H_2026_09_22_05, nearestUtcHourSeconds(H_2026_09_22_05 + 1799ULL));
    TEST_ASSERT_EQUAL_UINT64(H_2026_09_22_05 + 3600ULL, nearestUtcHourSeconds(H_2026_09_22_05 + 1800ULL));
    TEST_ASSERT_EQUAL_UINT64(H_2026_09_22_05, nearestUtcHourSeconds(H_2026_09_22_05 - 30ULL));
}

void test_nearest_hour_before_midnight_rolls_into_next_day(void) {
    const uint64_t midnight = 1790121600ULL; // 2026-09-23T00:00:00Z
    TEST_ASSERT_EQUAL_UINT64(midnight, nearestUtcHourSeconds(midnight - 2ULL));
    TEST_ASSERT_EQUAL_UINT64(0ULL, nearestUtcHourSeconds(midnight - 2ULL) % 86400ULL);
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_ms_exactly_at_floor_is_valid);
    RUN_TEST(test_ms_one_below_floor_is_invalid);
    RUN_TEST(test_ms_exactly_at_ceiling_is_valid);
    RUN_TEST(test_ms_one_above_ceiling_is_invalid);
    RUN_TEST(test_ms_zero_is_invalid);
    RUN_TEST(test_ms_plausible_value_is_valid);
    RUN_TEST(test_ms_seconds_value_sent_by_mistake_is_invalid);
    RUN_TEST(test_ms_microseconds_value_sent_by_mistake_is_invalid);

    RUN_TEST(test_seconds_exactly_at_floor_is_valid);
    RUN_TEST(test_seconds_one_below_floor_is_invalid);
    RUN_TEST(test_seconds_exactly_at_ceiling_is_valid);
    RUN_TEST(test_seconds_one_above_ceiling_is_invalid);
    RUN_TEST(test_seconds_plausible_value_is_valid);
    RUN_TEST(test_seconds_ms_value_sent_by_mistake_is_invalid);

    RUN_TEST(test_until_next_hour_from_exact_hour_is_full_hour);
    RUN_TEST(test_until_next_hour_ignores_half_hour_local_offset);
    RUN_TEST(test_distance_from_nearest_hour_on_both_sides);
    RUN_TEST(test_nearest_hour_rounds_to_closest);
    RUN_TEST(test_nearest_hour_before_midnight_rolls_into_next_day);

    return UNITY_END();
}
