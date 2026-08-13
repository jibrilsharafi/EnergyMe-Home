// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for BackoffSchedule::delayForAttempt. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>

#include <cstdint>

#include "backoff_schedule.h"

using namespace BackoffSchedule;

void setUp(void) {}
void tearDown(void) {}

// The OTA download retry schedule: 2 min initial, x2, capped at 15 min.
static constexpr uint64_t OTA_INITIAL = 2ULL * 60 * 1000;
static constexpr uint64_t OTA_MAX = 15ULL * 60 * 1000;
static constexpr uint64_t OTA_MULTIPLIER = 2;

static uint64_t otaDelay(uint64_t attempt) {
    return delayForAttempt(attempt, OTA_INITIAL, OTA_MAX, OTA_MULTIPLIER);
}

// ============================================================================
// The OTA schedule: 2, 4, 8, 15, 15 minutes
// ============================================================================

void test_ota_schedule_attempt_1_is_two_minutes(void) {
    TEST_ASSERT_EQUAL_UINT64(2ULL * 60 * 1000, otaDelay(1));
}

void test_ota_schedule_attempt_2_is_four_minutes(void) {
    TEST_ASSERT_EQUAL_UINT64(4ULL * 60 * 1000, otaDelay(2));
}

void test_ota_schedule_attempt_3_is_eight_minutes(void) {
    TEST_ASSERT_EQUAL_UINT64(8ULL * 60 * 1000, otaDelay(3));
}

// 16 min would exceed the cap, so this is where the schedule flattens.
void test_ota_schedule_attempt_4_clamps_to_fifteen_minutes(void) {
    TEST_ASSERT_EQUAL_UINT64(OTA_MAX, otaDelay(4));
}

void test_ota_schedule_attempt_5_stays_at_the_cap(void) {
    TEST_ASSERT_EQUAL_UINT64(OTA_MAX, otaDelay(5));
}

// The whole point of the schedule: it has to fit inside the 60 min presigned
// URL lifetime with room for the attempts themselves.
void test_ota_schedule_cumulative_wait_is_29_minutes(void) {
    uint64_t cumulative = 0;
    for (uint64_t attempt = 1; attempt <= 4; ++attempt) {
        cumulative += otaDelay(attempt);
    }
    TEST_ASSERT_EQUAL_UINT64(29ULL * 60 * 1000, cumulative);
}

// ============================================================================
// Attempt 0
// ============================================================================

void test_attempt_zero_has_no_delay(void) {
    TEST_ASSERT_EQUAL_UINT64(0, otaDelay(0));
}

void test_attempt_zero_ignores_multiplier_below_two(void) {
    TEST_ASSERT_EQUAL_UINT64(0, delayForAttempt(0, 1000, 5000, 1));
    TEST_ASSERT_EQUAL_UINT64(0, delayForAttempt(0, 1000, 5000, 0));
}

// ============================================================================
// Clamping
// ============================================================================

void test_first_attempt_clamps_when_initial_exceeds_max(void) {
    TEST_ASSERT_EQUAL_UINT64(500, delayForAttempt(1, 1000, 500, 2));
}

void test_initial_equal_to_max_is_returned_unchanged(void) {
    TEST_ASSERT_EQUAL_UINT64(1000, delayForAttempt(1, 1000, 1000, 2));
}

void test_general_path_clamps_at_max(void) {
    // 100 * 3^3 = 2700, over the 2000 ceiling.
    TEST_ASSERT_EQUAL_UINT64(2000, delayForAttempt(4, 100, 2000, 3));
}

void test_general_path_below_max_is_exact(void) {
    // 100 * 3^2 = 900.
    TEST_ASSERT_EQUAL_UINT64(900, delayForAttempt(3, 100, 2000, 3));
}

// ============================================================================
// Multiplier edge cases
// ============================================================================

void test_multiplier_one_is_constant(void) {
    TEST_ASSERT_EQUAL_UINT64(1000, delayForAttempt(1, 1000, 5000, 1));
    TEST_ASSERT_EQUAL_UINT64(1000, delayForAttempt(9, 1000, 5000, 1));
}

// Would divide by zero on the general path if not defined away.
void test_multiplier_zero_is_constant_not_a_crash(void) {
    TEST_ASSERT_EQUAL_UINT64(1000, delayForAttempt(1, 1000, 5000, 0));
    TEST_ASSERT_EQUAL_UINT64(1000, delayForAttempt(9, 1000, 5000, 0));
}

void test_multiplier_below_two_still_clamps_to_max(void) {
    TEST_ASSERT_EQUAL_UINT64(500, delayForAttempt(3, 1000, 500, 1));
}

// ============================================================================
// Overflow: must clamp, never wrap into a tight retry loop
// ============================================================================

void test_large_attempt_clamps_on_power_of_two_path(void) {
    TEST_ASSERT_EQUAL_UINT64(OTA_MAX, otaDelay(1000));
}

void test_attempt_at_word_size_boundary_clamps(void) {
    TEST_ASSERT_EQUAL_UINT64(OTA_MAX, otaDelay(64));
    TEST_ASSERT_EQUAL_UINT64(OTA_MAX, otaDelay(65));
}

// The old shift-without-checking would wrap this to a small value.
void test_shift_that_would_overflow_clamps(void) {
    const uint64_t huge = 1ULL << 62;
    TEST_ASSERT_EQUAL_UINT64(huge, delayForAttempt(10, huge, huge, 2));
}

void test_max_uint64_interval_does_not_wrap(void) {
    const uint64_t maxU64 = UINT64_MAX;
    TEST_ASSERT_EQUAL_UINT64(maxU64, delayForAttempt(40, maxU64, maxU64, 2));
}

void test_large_attempt_clamps_on_general_path(void) {
    TEST_ASSERT_EQUAL_UINT64(2000, delayForAttempt(1000, 100, 2000, 3));
}

void test_no_attempt_ever_returns_below_initial_before_cap(void) {
    // A wrapped result would show up as a delay shorter than the previous one.
    uint64_t previous = 0;
    for (uint64_t attempt = 1; attempt <= 128; ++attempt) {
        const uint64_t delay = otaDelay(attempt);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT64(previous, delay);
        TEST_ASSERT_LESS_OR_EQUAL_UINT64(OTA_MAX, delay);
        previous = delay;
    }
}

// ============================================================================
// Schedules of the existing callers, to pin the shared behaviour
// ============================================================================

void test_thirty_second_initial_one_hour_cap_schedule(void) {
    const uint64_t initial = 30ULL * 1000;
    const uint64_t cap = 60ULL * 60 * 1000;
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1000, delayForAttempt(1, initial, cap, 2));
    TEST_ASSERT_EQUAL_UINT64(60ULL * 1000, delayForAttempt(2, initial, cap, 2));
    TEST_ASSERT_EQUAL_UINT64(120ULL * 1000, delayForAttempt(3, initial, cap, 2));
    TEST_ASSERT_EQUAL_UINT64(cap, delayForAttempt(20, initial, cap, 2));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_ota_schedule_attempt_1_is_two_minutes);
    RUN_TEST(test_ota_schedule_attempt_2_is_four_minutes);
    RUN_TEST(test_ota_schedule_attempt_3_is_eight_minutes);
    RUN_TEST(test_ota_schedule_attempt_4_clamps_to_fifteen_minutes);
    RUN_TEST(test_ota_schedule_attempt_5_stays_at_the_cap);
    RUN_TEST(test_ota_schedule_cumulative_wait_is_29_minutes);

    RUN_TEST(test_attempt_zero_has_no_delay);
    RUN_TEST(test_attempt_zero_ignores_multiplier_below_two);

    RUN_TEST(test_first_attempt_clamps_when_initial_exceeds_max);
    RUN_TEST(test_initial_equal_to_max_is_returned_unchanged);
    RUN_TEST(test_general_path_clamps_at_max);
    RUN_TEST(test_general_path_below_max_is_exact);

    RUN_TEST(test_multiplier_one_is_constant);
    RUN_TEST(test_multiplier_zero_is_constant_not_a_crash);
    RUN_TEST(test_multiplier_below_two_still_clamps_to_max);

    RUN_TEST(test_large_attempt_clamps_on_power_of_two_path);
    RUN_TEST(test_attempt_at_word_size_boundary_clamps);
    RUN_TEST(test_shift_that_would_overflow_clamps);
    RUN_TEST(test_max_uint64_interval_does_not_wrap);
    RUN_TEST(test_large_attempt_clamps_on_general_path);
    RUN_TEST(test_no_attempt_ever_returns_below_initial_before_cap);

    RUN_TEST(test_thirty_second_initial_one_hour_cap_schedule);

    return UNITY_END();
}
