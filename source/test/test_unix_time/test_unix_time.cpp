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

    return UNITY_END();
}
