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
// ceiling
// ============================================================================

static const uint64_t DAY = 86400ULL;

void test_ceiling_is_anchor_plus_uptime(void) {
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, ceiling(PERSISTED, 0));
    TEST_ASSERT_EQUAL_UINT32(PERSISTED + HOUR, ceiling(PERSISTED, HOUR));
}

void test_ceiling_without_anchor_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, ceiling(0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, ceiling(0, HOUR)); // not the bare uptime
}

void test_ceiling_saturates_instead_of_wrapping(void) {
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, ceiling(UINT32_MAX - 10U, 100));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, ceiling(UINT32_MAX, UINT32_MAX));
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
// onAnswer
// ============================================================================

void test_on_answer_rejects_below_the_floor(void) {
    AnswerOutcome outcome = onAnswer(BUILD, BUILD, BUILD, BASE_UPTIME, BASE_UPTIME + 10, BOGUS_2023, TOLERANCE);
    TEST_ASSERT_FALSE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.floor);
}

void test_on_answer_rejects_zero_transmit(void) {
    AnswerOutcome outcome = onAnswer(0, 0, 0, 0, 10, ZERO_TRANSMIT_SECONDS, TOLERANCE);
    TEST_ASSERT_FALSE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.floor);
}

void test_on_answer_without_corroboration_keeps_the_floor(void) {
    AnswerOutcome outcome = onAnswer(BUILD, BUILD, 0, 0, 10, BUILD + HOUR, TOLERANCE);
    TEST_ASSERT_TRUE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.floor);
}

void test_on_answer_with_corroboration_raises_the_floor(void) {
    AnswerOutcome outcome = onAnswer(PERSISTED, PERSISTED, PERSISTED + 10, 10, 10 + HOUR, PERSISTED + 10 + HOUR, TOLERANCE);
    TEST_ASSERT_TRUE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED + 10 + HOUR, outcome.floor);
}

void test_on_answer_raises_at_most_to_the_ceiling(void) {
    // Corroborated, but a day past anchor + uptime: the source is fast, not real time
    AnswerOutcome outcome = onAnswer(PERSISTED, PERSISTED, PERSISTED + DAY + 10, 10, 10 + HOUR,
                                     PERSISTED + DAY + 10 + HOUR, TOLERANCE);
    TEST_ASSERT_TRUE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED + 10 + HOUR, outcome.floor);
}

void test_on_answer_without_anchor_never_raises(void) {
    AnswerOutcome outcome = onAnswer(0, 0, PERSISTED + 10, 10, 10 + HOUR, PERSISTED + 10 + HOUR, TOLERANCE);
    TEST_ASSERT_TRUE(outcome.accept);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.floor);
}

// ============================================================================
// onManualSet
// ============================================================================

void test_manual_set_lowers_floor_and_anchor(void) {
    ManualOutcome outcome = onManualSet(PERSISTED, PERSISTED, 100, BUILD);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.floor);
    TEST_ASSERT_EQUAL_UINT32(BUILD - 100, outcome.anchor);
}

void test_manual_set_keeps_the_ceiling_at_the_value(void) {
    ManualOutcome outcome = onManualSet(PERSISTED, PERSISTED, HOUR, BUILD);
    TEST_ASSERT_EQUAL_UINT32(BUILD, ceiling(outcome.anchor, HOUR));
}

void test_manual_set_never_raises_floor_or_anchor(void) {
    ManualOutcome outcome = onManualSet(BUILD, BUILD, 100, PERSISTED);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.floor);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.anchor);

    outcome = onManualSet(PERSISTED, PERSISTED - 100, 100, PERSISTED);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, outcome.floor);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED - 100, outcome.anchor);
}

void test_manual_set_uptime_past_the_value_zeroes_the_anchor(void) {
    ManualOutcome outcome = onManualSet(PERSISTED, PERSISTED, UINT32_MAX, BUILD);
    TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.floor);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.anchor);

    outcome = onManualSet(PERSISTED, PERSISTED, BUILD, BUILD);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.anchor);
}

void test_manual_set_keeps_no_floor_and_no_anchor(void) {
    ManualOutcome outcome = onManualSet(0, 0, 100, BUILD);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.floor);
    TEST_ASSERT_EQUAL_UINT32(0, outcome.anchor);
}

void test_manual_set_ignores_implausible_value(void) {
    const uint64_t values[] = {0, UnixTime::MAX_SECONDS + 1, 1790756411000ULL}; // last: milliseconds
    for (uint64_t value : values) {
        ManualOutcome outcome = onManualSet(PERSISTED, BUILD, 100, value);
        TEST_ASSERT_EQUAL_UINT32(PERSISTED, outcome.floor);
        TEST_ASSERT_EQUAL_UINT32(BUILD, outcome.anchor);
    }
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

// src/customtime.cpp: the atomics around onAnswer (sntp_sync_time) and onManualSet (setUnixTime)
struct Device {
    uint32_t floor;
    uint32_t anchor;
    uint32_t baseWall;
    uint32_t baseUptime;
    uint64_t clock;
};

static Device bootedDevice(uint64_t persistedFloor, uint64_t buildFloor = BUILD) {
    uint32_t floor = effective(buildFloor, persistedFloor);
    return Device{floor, floor, 0, 0, 0};
}

static bool ntpAnswer(Device &device, uint64_t answer, uint32_t uptime) {
    AnswerOutcome outcome =
        onAnswer(device.floor, device.anchor, device.baseWall, device.baseUptime, uptime, answer, TOLERANCE);
    if (!outcome.accept) return false;
    device.clock = answer;
    device.baseWall = toFloor(answer);
    device.baseUptime = uptime;
    device.floor = outcome.floor;
    return true;
}

static void manualSet(Device &device, uint64_t value, uint32_t uptime) {
    ManualOutcome outcome = onManualSet(device.floor, device.anchor, uptime, value);
    device.floor = outcome.floor;
    device.anchor = outcome.anchor;
    device.baseWall = toFloor(value);
    device.baseUptime = uptime;
    device.clock = value;
}

// Real time at an uptime, for a device booted when real time was PERSISTED, its persisted
// floor: the tightest anchor, so the ceiling is real time itself
static uint64_t realAt(uint32_t uptime) {
    return PERSISTED + uptime;
}

void test_2023_is_rejected_with_a_2026_build_floor(void) {
    Device device = bootedDevice(0);
    TEST_ASSERT_FALSE(ntpAnswer(device, BOGUS_2023, 10));
    TEST_ASSERT_EQUAL_UINT64(0, device.clock);
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor);
}

void test_first_answer_after_boot_does_not_raise_the_floor(void) {
    Device device = bootedDevice(0);
    TEST_ASSERT_TRUE(ntpAnswer(device, BUILD + DAY, 10));
    TEST_ASSERT_EQUAL_UINT32(BUILD, device.floor);
}

void test_agreeing_answers_raise_the_floor_to_real_time(void) {
    Device device = bootedDevice(PERSISTED);
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10), 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + HOUR), 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(realAt(10 + HOUR), device.floor);
}

void test_floor_lags_real_time_by_the_install_delay(void) {
    // Built a week before it was powered on: the anchor is the build floor, so the ceiling
    // stays a week behind the genuine answers
    Device device = bootedDevice(0);
    const uint64_t bootReal = BUILD + 7 * DAY;
    TEST_ASSERT_TRUE(ntpAnswer(device, bootReal + 10, 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, bootReal + 10 + HOUR, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(BUILD + 10 + HOUR, device.floor);
}

void test_consistently_fast_source_never_lifts_the_floor_past_real_time(void) {
    // A gateway a day fast on every answer corroborates itself. Uncapped, it became the floor,
    // was persisted, and after a reboot every genuine answer was rejected.
    Device device = bootedDevice(PERSISTED);
    uint32_t uptime = 10;
    for (int i = 0; i < 48; i++, uptime += HOUR) {
        TEST_ASSERT_TRUE(ntpAnswer(device, realAt(uptime) + DAY, uptime));
        TEST_ASSERT_TRUE(device.floor <= realAt(uptime));
    }
    const uint32_t lastUptime = uptime - HOUR;
    TEST_ASSERT_EQUAL_UINT32(realAt(lastUptime), device.floor);

    // Rebooted after an hour without power: the persisted floor is the new anchor
    const uint64_t rebootReal = realAt(lastUptime) + HOUR;
    Device rebooted = bootedDevice(device.floor);
    TEST_ASSERT_TRUE(rebooted.anchor <= rebootReal);
    TEST_ASSERT_TRUE(ntpAnswer(rebooted, rebootReal + 30, 30)); // the genuine source is back
    TEST_ASSERT_EQUAL_UINT64(rebootReal + 30, rebooted.clock);
}

void test_single_future_answer_heals_at_the_next_sync(void) {
    // A gateway answering a year ahead once: the clock steps (as it always could) but the
    // floor stays, so the next genuine answer is accepted and steps it back
    Device device = bootedDevice(PERSISTED);
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10), 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + HOUR) + 365 * DAY, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor);

    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + 2 * HOUR), 10 + 2 * HOUR));
    TEST_ASSERT_EQUAL_UINT64(realAt(10 + 2 * HOUR), device.clock);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor); // disagrees with the bogus base

    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + 3 * HOUR), 10 + 3 * HOUR));
    TEST_ASSERT_EQUAL_UINT32(realAt(10 + 3 * HOUR), device.floor);
}

void test_zero_transmit_answer_is_rejected_and_keeps_the_base(void) {
    Device device = bootedDevice(PERSISTED);
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10), 10));
    TEST_ASSERT_FALSE(ntpAnswer(device, ZERO_TRANSMIT_SECONDS, 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT64(realAt(10), device.clock);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor);

    // The base is still the genuine answer, so the next genuine one corroborates it
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + 2 * HOUR), 10 + 2 * HOUR));
    TEST_ASSERT_EQUAL_UINT32(realAt(10 + 2 * HOUR), device.floor);
}

void test_rejected_answer_does_not_move_the_base(void) {
    Device device = bootedDevice(PERSISTED);
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10), 10));
    TEST_ASSERT_FALSE(ntpAnswer(device, BOGUS_2023, 20));
    TEST_ASSERT_EQUAL_UINT32(realAt(10), device.baseWall);
    TEST_ASSERT_EQUAL_UINT32(10, device.baseUptime);
}

void test_manual_set_lowers_a_future_floor(void) {
    // A future-dated floor rejects every genuine answer; the manual set is the escape hatch
    Device device = bootedDevice(2000000000ULL); // 2033, bad persisted value
    const uint64_t genuine = BUILD + DAY;
    TEST_ASSERT_FALSE(ntpAnswer(device, genuine, 10));

    manualSet(device, genuine, 20);
    TEST_ASSERT_EQUAL_UINT32(genuine, device.floor);
    TEST_ASSERT_TRUE(ntpAnswer(device, genuine + HOUR, 20 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(genuine + HOUR, device.floor);
}

void test_manual_set_minutes_fast_is_corrected_by_the_next_answer(void) {
    // A browser clock 10 minutes fast: floor and anchor stay, so NTP steps the clock back
    Device device = bootedDevice(PERSISTED);
    manualSet(device, realAt(10) + 600, 10);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.anchor);

    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(30), 30));
    TEST_ASSERT_EQUAL_UINT64(realAt(30), device.clock);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor); // disagrees with the manual base

    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(30 + HOUR), 30 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(realAt(30 + HOUR), device.floor);
}

void test_ntp_agreeing_with_manual_set_raises_the_floor(void) {
    // A browser 2 s slow: it corroborates, and the anchor it lowered caps the floor 2 s behind
    Device device = bootedDevice(PERSISTED);
    manualSet(device, realAt(10) - 2, 10);
    TEST_ASSERT_EQUAL_UINT32(PERSISTED, device.floor);

    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + HOUR), 10 + HOUR));
    TEST_ASSERT_EQUAL_UINT32(realAt(10 + HOUR) - 2, device.floor);
}

void test_no_anchor_never_engages_the_floor(void) {
    // No build floor and nothing persisted: every answer but the zero transmit one sets the
    // clock and nothing ever becomes a floor, as before the floor existed
    Device device = bootedDevice(0, 0);
    TEST_ASSERT_EQUAL_UINT32(0, device.anchor);
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10), 10));
    TEST_ASSERT_TRUE(ntpAnswer(device, realAt(10 + HOUR), 10 + HOUR));
    TEST_ASSERT_TRUE(ntpAnswer(device, BOGUS_2023, 10 + 2 * HOUR));
    TEST_ASSERT_EQUAL_UINT64(BOGUS_2023, device.clock);
    TEST_ASSERT_FALSE(ntpAnswer(device, ZERO_TRANSMIT_SECONDS, 10 + 3 * HOUR));
    TEST_ASSERT_EQUAL_UINT32(0, device.floor);

    manualSet(device, realAt(20 + 3 * HOUR), 20 + 3 * HOUR);
    TEST_ASSERT_EQUAL_UINT32(0, device.floor);
    TEST_ASSERT_EQUAL_UINT32(0, device.anchor);
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

    RUN_TEST(test_ceiling_is_anchor_plus_uptime);
    RUN_TEST(test_ceiling_without_anchor_is_zero);
    RUN_TEST(test_ceiling_saturates_instead_of_wrapping);

    RUN_TEST(test_zero_transmit_artifact_is_detected);
    RUN_TEST(test_zero_transmit_neighbours_are_not_the_artifact);
    RUN_TEST(test_zero_transmit_artifact_passes_the_floor_check);

    RUN_TEST(test_on_answer_rejects_below_the_floor);
    RUN_TEST(test_on_answer_rejects_zero_transmit);
    RUN_TEST(test_on_answer_without_corroboration_keeps_the_floor);
    RUN_TEST(test_on_answer_with_corroboration_raises_the_floor);
    RUN_TEST(test_on_answer_raises_at_most_to_the_ceiling);
    RUN_TEST(test_on_answer_without_anchor_never_raises);

    RUN_TEST(test_manual_set_lowers_floor_and_anchor);
    RUN_TEST(test_manual_set_keeps_the_ceiling_at_the_value);
    RUN_TEST(test_manual_set_never_raises_floor_or_anchor);
    RUN_TEST(test_manual_set_uptime_past_the_value_zeroes_the_anchor);
    RUN_TEST(test_manual_set_keeps_no_floor_and_no_anchor);
    RUN_TEST(test_manual_set_ignores_implausible_value);

    RUN_TEST(test_floor_is_monotonic_across_syncs);
    RUN_TEST(test_2023_is_rejected_with_a_2026_build_floor);
    RUN_TEST(test_first_answer_after_boot_does_not_raise_the_floor);
    RUN_TEST(test_agreeing_answers_raise_the_floor_to_real_time);
    RUN_TEST(test_floor_lags_real_time_by_the_install_delay);
    RUN_TEST(test_consistently_fast_source_never_lifts_the_floor_past_real_time);
    RUN_TEST(test_single_future_answer_heals_at_the_next_sync);
    RUN_TEST(test_zero_transmit_answer_is_rejected_and_keeps_the_base);
    RUN_TEST(test_rejected_answer_does_not_move_the_base);
    RUN_TEST(test_manual_set_lowers_a_future_floor);
    RUN_TEST(test_manual_set_minutes_fast_is_corrected_by_the_next_answer);
    RUN_TEST(test_ntp_agreeing_with_manual_set_raises_the_floor);
    RUN_TEST(test_no_anchor_never_engages_the_floor);

    return UNITY_END();
}
