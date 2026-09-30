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
static const uint32_t PERSISTED = 1791000000UL;  // ~3 days later, a last corroborated sync
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
// corroborates
// ============================================================================

static const uint32_t BASE_UPTIME = 100;
static const uint64_t HOUR = 3600ULL;

void test_corroborates_answer_at_the_same_pace(void) {
    TEST_ASSERT_TRUE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + HOUR, BASE_UPTIME + HOUR, TOLERANCE));
}

void test_corroborates_with_no_elapsed_time(void) {
    TEST_ASSERT_TRUE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED, BASE_UPTIME, TOLERANCE));
}

void test_no_base_never_corroborates(void) {
    TEST_ASSERT_FALSE(corroborates(0, 0, PERSISTED, BASE_UPTIME, TOLERANCE));
    TEST_ASSERT_FALSE(corroborates(0, BASE_UPTIME, PERSISTED + HOUR, BASE_UPTIME + HOUR, TOLERANCE));
}

void test_corroborates_exactly_tolerance_ahead_or_behind(void) {
    TEST_ASSERT_TRUE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + HOUR + TOLERANCE, BASE_UPTIME + HOUR, TOLERANCE));
    TEST_ASSERT_TRUE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + HOUR - TOLERANCE, BASE_UPTIME + HOUR, TOLERANCE));
}

void test_rejects_one_second_past_tolerance_ahead_or_behind(void) {
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + HOUR + TOLERANCE + 1ULL, BASE_UPTIME + HOUR, TOLERANCE));
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + HOUR - TOLERANCE - 1ULL, BASE_UPTIME + HOUR, TOLERANCE));
}

void test_wall_behind_base_corroborates_when_uptime_agrees(void) {
    // Within tolerance of the base, slightly earlier: a server a few seconds behind
    TEST_ASSERT_TRUE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED - 5ULL, BASE_UPTIME, TOLERANCE));
}

void test_bogus_future_answer_does_not_corroborate(void) {
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED + 365ULL * 86400ULL, BASE_UPTIME + HOUR, TOLERANCE));
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, ZERO_TRANSMIT_SECONDS, BASE_UPTIME + HOUR, TOLERANCE));
}

void test_base_uptime_zero_is_a_valid_base(void) {
    TEST_ASSERT_TRUE(corroborates(PERSISTED, 0, PERSISTED + 5ULL, 5, TOLERANCE));
    TEST_ASSERT_TRUE(corroborates(PERSISTED, 0, PERSISTED, 0, TOLERANCE));
}

void test_uptime_going_backwards_never_corroborates(void) {
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED, BASE_UPTIME - 1, TOLERANCE));
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, PERSISTED, 0, TOLERANCE));
}

void test_implausible_wall_never_corroborates(void) {
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, 0, BASE_UPTIME, TOLERANCE));
    TEST_ASSERT_FALSE(corroborates(PERSISTED, BASE_UPTIME, UnixTime::MAX_SECONDS + 1, BASE_UPTIME, TOLERANCE));
}

void test_corroborates_over_a_long_uptime(void) {
    // uint32 uptime seconds: ~136 years, a base from days ago still compares correctly
    const uint32_t baseUptime = 30U * 86400U;
    TEST_ASSERT_TRUE(corroborates(PERSISTED, baseUptime, PERSISTED + 7ULL * 86400ULL, baseUptime + 7U * 86400U, TOLERANCE));
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
// isZeroTransmitArtifact
// ============================================================================

void test_zero_transmit_artifact_is_detected(void) {
    TEST_ASSERT_EQUAL_UINT64(2085978496ULL, ZERO_TRANSMIT_SECONDS); // 2036-02-07T06:28:16Z
    TEST_ASSERT_TRUE(isZeroTransmitArtifact(ZERO_TRANSMIT_SECONDS));
}

void test_zero_transmit_neighbours_are_not_the_artifact(void) {
    TEST_ASSERT_FALSE(isZeroTransmitArtifact(ZERO_TRANSMIT_SECONDS - 1ULL));
    TEST_ASSERT_FALSE(isZeroTransmitArtifact(ZERO_TRANSMIT_SECONDS + 1ULL));
    TEST_ASSERT_FALSE(isZeroTransmitArtifact(BUILD));
    TEST_ASSERT_FALSE(isZeroTransmitArtifact(0));
}

void test_zero_transmit_artifact_passes_the_floor_check(void) {
    // Why it needs its own check: it is in the future, so the floor lets it through
    TEST_ASSERT_TRUE(accepts(PERSISTED, ZERO_TRANSMIT_SECONDS, TOLERANCE));
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

// The sntp_sync_time override (src/customtime.cpp), composed from the rules above
struct Device {
    uint32_t floor;
    uint32_t baseWall;
    uint32_t baseUptime;
    uint64_t clock;
};

static Device bootedDevice(uint64_t persistedFloor) {
    return Device{effective(BUILD, persistedFloor), 0, 0, 0};
}

static bool ntpAnswer(Device &device, uint64_t answer, uint32_t uptime) {
    if (isZeroTransmitArtifact(answer) || !accepts(device.floor, answer, TOLERANCE)) return false;
    device.clock = answer;
    bool agreed = corroborates(device.baseWall, device.baseUptime, answer, uptime, TOLERANCE);
    device.baseWall = toFloor(answer);
    device.baseUptime = uptime;
    if (agreed) device.floor = raisedBy(device.floor, answer);
    return true;
}

void test_first_answer_after_boot_does_not_raise_the_floor(void) {
    Device device = bootedDevice(0);
    TEST_ASSERT_TRUE(ntpAnswer(device, BUILD + 86400ULL, 10));
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor);
}

void test_agreeing_answers_raise_the_floor(void) {
    Device device = bootedDevice(0);
    const uint64_t genuine = BUILD + 86400ULL;
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine, 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + HOUR, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(genuine + HOUR, device.floor);
}

void test_single_future_answer_heals_at_the_next_sync(void) {
    // A gateway answering a year ahead once: the clock steps (as it always could) but the
    // floor stays, so the next genuine answer is accepted and steps it back
    Device device = bootedDevice(0);
    const uint64_t genuine = BUILD + 86400ULL;
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine, 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + HOUR + 365ULL * 86400ULL, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor);

    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + 2 * HOUR, 10 + 2 * HOUR));
    TEST_ASSERT_EQUAL_UINT64(genuine + 2 * HOUR, device.clock);
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor); // disagrees with the bogus base

    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + 3 * HOUR, 10 + 3 * HOUR));
    TEST_ASSERT_EQUAL_UINT32(genuine + 3 * HOUR, device.floor);
}

void test_zero_transmit_answer_is_rejected_and_keeps_the_base(void) {
    Device device = bootedDevice(0);
    const uint64_t genuine = BUILD + 86400ULL;
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine, 10));
    TEST_ASSERT_FALSE(ntpAnswer(device, ZERO_TRANSMIT_SECONDS, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT64(genuine, device.clock);
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor);

    // The base is still the genuine answer, so the next genuine one corroborates it
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + 2 * HOUR, 10 + 2 * HOUR));
    TEST_ASSERT_EQUAL_UINT32(genuine + 2 * HOUR, device.floor);
}

void test_rejected_answer_does_not_move_the_base(void) {
    Device device = bootedDevice(PERSISTED);
    TEST_ASSERT_TRUE(ntpAnswer(device, PERSISTED + HOUR, 10));
    TEST_ASSERT_FALSE(ntpAnswer(device, BOGUS_2023, 20));
    TEST_ASSERT_EQUAL_UINT32(PERSISTED + HOUR, device.baseWall);
    TEST_ASSERT_EQUAL_UINT32(10, device.baseUptime);
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

    RUN_TEST(test_corroborates_answer_at_the_same_pace);
    RUN_TEST(test_corroborates_with_no_elapsed_time);
    RUN_TEST(test_no_base_never_corroborates);
    RUN_TEST(test_corroborates_exactly_tolerance_ahead_or_behind);
    RUN_TEST(test_rejects_one_second_past_tolerance_ahead_or_behind);
    RUN_TEST(test_wall_behind_base_corroborates_when_uptime_agrees);
    RUN_TEST(test_bogus_future_answer_does_not_corroborate);
    RUN_TEST(test_base_uptime_zero_is_a_valid_base);
    RUN_TEST(test_uptime_going_backwards_never_corroborates);
    RUN_TEST(test_implausible_wall_never_corroborates);
    RUN_TEST(test_corroborates_over_a_long_uptime);

    RUN_TEST(test_raise_moves_floor_to_later_answer);
    RUN_TEST(test_raise_never_lowers_for_answer_within_tolerance);
    RUN_TEST(test_raise_from_no_floor);
    RUN_TEST(test_raise_ignores_implausible_answer);

    RUN_TEST(test_zero_transmit_artifact_is_detected);
    RUN_TEST(test_zero_transmit_neighbours_are_not_the_artifact);
    RUN_TEST(test_zero_transmit_artifact_passes_the_floor_check);

    RUN_TEST(test_floor_is_monotonic_across_syncs);
    RUN_TEST(test_first_answer_after_boot_does_not_raise_the_floor);
    RUN_TEST(test_agreeing_answers_raise_the_floor);
    RUN_TEST(test_single_future_answer_heals_at_the_next_sync);
    RUN_TEST(test_zero_transmit_answer_is_rejected_and_keeps_the_base);
    RUN_TEST(test_rejected_answer_does_not_move_the_base);
    RUN_TEST(test_manual_set_may_lower_the_floor);

    return UNITY_END();
}
