// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the NTP time floor rules. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "time_floor.h"
#include "unix_time.h"

using namespace TimeFloor;

void setUp(void) {}
void tearDown(void) {}

static const uint32_t TOLERANCE = 60;
static const uint32_t BUILD = 1790756411UL;      // 2026-09-30, a commit time
static const uint32_t PERSISTED = 1791000000UL;  // ~3 days later, a last accepted sync
static const uint64_t BOGUS_2023 = 1690000000ULL; // the regression seen in the field

// ============================================================================
// toFloor
// ============================================================================

void test_to_floor_keeps_plausible_seconds(void) {
    TEST_ASSERT_EQUAL_UINT32(BUILD, toFloor(BUILD));
}

void test_to_floor_zero_is_no_floor(void) {
    TEST_ASSERT_EQUAL_UINT32(0, toFloor(0));
}

void test_to_floor_rejects_implausible_values(void) {
    TEST_ASSERT_EQUAL_UINT32(0, toFloor(UnixTime::MIN_SECONDS - 1));
    TEST_ASSERT_EQUAL_UINT32(0, toFloor(UnixTime::MAX_SECONDS + 1));
    TEST_ASSERT_EQUAL_UINT32(0, toFloor(1790756411000ULL)); // milliseconds by mistake
}

// ============================================================================
// effective
// ============================================================================

void test_effective_is_the_later_of_build_and_persisted(void) {
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, effective(BUILD, PERSISTED));
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, effective(PERSISTED, BUILD));
}

void test_effective_with_one_side_missing(void) {
    TEST_ASSERT_EQUAL_UINT32(BUILD, effective(BUILD, 0));      // factory-fresh device
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, effective(0, PERSISTED)); // build without git
}

void test_effective_with_both_missing_is_no_floor(void) {
    TEST_ASSERT_EQUAL_UINT32(0, effective(0, 0));
}

void test_effective_ignores_garbage_persisted_value(void) {
    // A persisted value past 2100 would otherwise reject every genuine answer forever
    TEST_ASSERT_EQUAL_UINT32(BUILD, effective(BUILD, 0xFFFFFFFFULL));
}

// ============================================================================
// accepts
// ============================================================================

void test_accepts_candidate_after_floor(void) {
    TEST_ASSERT_TRUE(accepts(PERSISTED, PERSISTED + 3600ULL, TOLERANCE));
}

void test_accepts_candidate_equal_to_floor(void) {
    TEST_ASSERT_TRUE(accepts(PERSISTED, PERSISTED, TOLERANCE));
}

void test_accepts_candidate_exactly_tolerance_below_floor(void) {
    TEST_ASSERT_TRUE(accepts(PERSISTED, PERSISTED - TOLERANCE, TOLERANCE));
}

void test_rejects_candidate_one_second_past_tolerance(void) {
    TEST_ASSERT_FALSE(accepts(PERSISTED, PERSISTED - TOLERANCE - 1ULL, TOLERANCE));
}

void test_rejects_bogus_past_answer(void) {
    TEST_ASSERT_FALSE(accepts(BUILD, BOGUS_2023, TOLERANCE));
}

void test_rejects_epoch_zero_candidate_without_underflow(void) {
    TEST_ASSERT_FALSE(accepts(BUILD, 0, TOLERANCE));
}

void test_zero_floor_accepts_anything(void) {
    TEST_ASSERT_TRUE(accepts(0, BOGUS_2023, TOLERANCE));
    TEST_ASSERT_TRUE(accepts(0, 0, TOLERANCE));
}

void test_zero_tolerance_is_strict(void) {
    TEST_ASSERT_TRUE(accepts(PERSISTED, PERSISTED, 0));
    TEST_ASSERT_FALSE(accepts(PERSISTED, PERSISTED - 1ULL, 0));
}

// ============================================================================
// raisedBy
// ============================================================================

void test_raise_moves_floor_to_later_answer(void) {
    TEST_ASSERT_EQUAL_UINT32(PERSISTED + 3600UL, raisedBy(PERSISTED, PERSISTED + 3600ULL));
}

void test_raise_never_lowers_for_answer_within_tolerance(void) {
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, raisedBy(PERSISTED, PERSISTED - 30ULL));
}

void test_raise_from_no_floor(void) {
    TEST_ASSERT_EQUAL_UINT32(BUILD, raisedBy(0, BUILD));
}

void test_raise_ignores_implausible_answer(void) {
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, raisedBy(PERSISTED, UnixTime::MAX_SECONDS + 1));
    TEST_ASSERT_EQUAL_UINT32(0, raisedBy(0, 0));
}

// ============================================================================
// Scenarios
// ============================================================================

void test_floor_is_monotonic_across_syncs(void) {
    uint32_t floor = effective(BUILD, 0);
    const uint64_t answers[] = {BUILD + 100ULL, BUILD + 3700ULL, BUILD + 3680ULL, BUILD + 7300ULL};
    uint32_t previous = floor;
    for (uint64_t answer : answers) {
        TEST_ASSERT_TRUE(accepts(floor, answer, TOLERANCE));
        floor = raisedBy(floor, answer);
        TEST_ASSERT_TRUE(floor >= previous);
        previous = floor;
    }
    TEST_ASSERT_EQUAL_UINT32(BUILD + 7300UL, floor);
}

void test_manual_set_may_lower_the_floor(void) {
    // A future-dated floor rejects every genuine answer; the manual set is the escape
    // hatch and replaces the floor outright instead of going through raisedBy.
    uint32_t floor = effective(BUILD, 2000000000ULL); // 2033, bad persisted value
    const uint64_t genuine = BUILD + 86400ULL;
    TEST_ASSERT_FALSE(accepts(floor, genuine, TOLERANCE));

    floor = toFloor(genuine);
    TEST_ASSERT_EQUAL_UINT32(genuine, floor);
    TEST_ASSERT_TRUE(accepts(floor, genuine + 3600ULL, TOLERANCE));
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_to_floor_keeps_plausible_seconds);
    RUN_TEST(test_to_floor_zero_is_no_floor);
    RUN_TEST(test_to_floor_rejects_implausible_values);

    RUN_TEST(test_effective_is_the_later_of_build_and_persisted);
    RUN_TEST(test_effective_with_one_side_missing);
    RUN_TEST(test_effective_with_both_missing_is_no_floor);
    RUN_TEST(test_effective_ignores_garbage_persisted_value);

    RUN_TEST(test_accepts_candidate_after_floor);
    RUN_TEST(test_accepts_candidate_equal_to_floor);
    RUN_TEST(test_accepts_candidate_exactly_tolerance_below_floor);
    RUN_TEST(test_rejects_candidate_one_second_past_tolerance);
    RUN_TEST(test_rejects_bogus_past_answer);
    RUN_TEST(test_rejects_epoch_zero_candidate_without_underflow);
    RUN_TEST(test_zero_floor_accepts_anything);
    RUN_TEST(test_zero_tolerance_is_strict);

    RUN_TEST(test_raise_moves_floor_to_later_answer);
    RUN_TEST(test_raise_never_lowers_for_answer_within_tolerance);
    RUN_TEST(test_raise_from_no_floor);
    RUN_TEST(test_raise_ignores_implausible_answer);

    RUN_TEST(test_floor_is_monotonic_across_syncs);
    RUN_TEST(test_manual_set_may_lower_the_floor);

    return UNITY_END();
}
