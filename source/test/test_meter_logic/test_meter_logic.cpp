// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the pure meter logic. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)
//
// These need no hardware: they exercise the CT-polarity detector, the negative
// clamp, the WDRR weighting and the scheduler core entirely in memory. The
// integration simulations at the bottom reproduce the dev-bench starvation
// regression and prove the conduction-gated boost fixes it without breaking
// reversed-CT detection on circuits that are idle at install time.

#include <unity.h>
#include "meter_logic.h"

using namespace MeterLogic;

void setUp(void) {}
void tearDown(void) {}

// Config values mirror the firmware defaults so the tests track real behaviour.
static const WeightConfig WCFG = {0.4f, 0.4f, 0.1f, 1.5f, 2.0f};
static const float MIN_A = 0.02f;              // validation threshold (firmware: CT rating * MINIMUM_CURRENT_RATIO_VALIDATION, e.g. 20 A * 0.001)
static const PolarityConfig PCFG = {5, 50, MIN_A};
static const float COND_A = 1.0f;              // a nominal "real load" current (>= MIN_A)

// ============================================================================
// updatePolarity
// ============================================================================

void test_polarity_not_armed_is_noop(void) {
    PolarityState s{false, 0, 0};
    PolarityResult r = updatePolarity(s, -100.0f, COND_A, PCFG);
    TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    TEST_ASSERT_FALSE(r.state.armed);
    TEST_ASSERT_EQUAL_INT8(0, r.state.voteCount);
    TEST_ASSERT_EQUAL_UINT16(0, r.state.conductingReads);
}

void test_polarity_steady_reverse_flips_at_threshold(void) {
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    int reads = 0;
    for (int i = 0; i < 10; i++) {
        PolarityResult r = updatePolarity(s, -230.0f, COND_A, PCFG);
        s = r.state;
        action = r.action;
        reads++;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
    TEST_ASSERT_EQUAL_INT(5, reads);   // flips on exactly the 5th reversed vote
    TEST_ASSERT_FALSE(s.armed);        // disarms after deciding
}

void test_polarity_steady_forward_confirms_without_flip(void) {
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 10; i++) {
        PolarityResult r = updatePolarity(s, 1500.0f, COND_A, PCFG);
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Disarm, action); // confirmed correct, no flip
    TEST_ASSERT_FALSE(s.armed);
}

void test_polarity_is_magnitude_independent(void) {
    // A tiny steady reversed load must flip exactly like a huge one.
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 10; i++) {
        PolarityResult r = updatePolarity(s, -0.3f, COND_A, PCFG);
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
}

void test_polarity_idle_is_noop_and_stays_armed(void) {
    // No load: a non-conducting reading must not vote, must not count toward the
    // bound, and must leave the channel armed (waiting for load).
    PolarityState s{true, 0, 0};
    for (int i = 0; i < 500; i++) {
        PolarityResult r = updatePolarity(s, 0.0f, 0.0f, PCFG);
        s = r.state;
        TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    }
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_INT8(0, s.voteCount);
    TEST_ASSERT_EQUAL_UINT16(0, s.conductingReads);
}

void test_polarity_nan_is_noop(void) {
    PolarityState s{true, 0, 0};
    PolarityResult r = updatePolarity(s, NAN, COND_A, PCFG);
    TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    TEST_ASSERT_TRUE(r.state.armed);
    TEST_ASSERT_EQUAL_INT8(0, r.state.voteCount);
    TEST_ASSERT_EQUAL_UINT16(0, r.state.conductingReads);
}

void test_polarity_delayed_load_still_detects(void) {
    // THE KEY CORRECTNESS CASE: a CT clipped on a circuit that is off at install
    // time. The channel stays armed through a long idle period, then a reversed
    // load finally appears - and it must still be detected and flipped.
    PolarityState s{true, 0, 0};
    for (int i = 0; i < 200; i++) {
        s = updatePolarity(s, 0.0f, 0.0f, PCFG).state; // idle: no load yet
    }
    TEST_ASSERT_TRUE(s.armed); // still armed after a long wait

    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 10; i++) {
        PolarityResult r = updatePolarity(s, -230.0f, COND_A, PCFG); // load finally flows, reversed
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
}

void test_polarity_oscillating_conducting_disarms_via_bound(void) {
    // A channel that keeps conducting but whose sign oscillates can never decide;
    // the conducting-read bound makes it give up (no flip) so it cannot stay
    // armed-and-boosted forever.
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 200; i++) {
        float p = (i % 2 == 0) ? -50.0f : 50.0f;
        PolarityResult r = updatePolarity(s, p, COND_A, PCFG);
        s = r.state;
        action = r.action;
        if (!s.armed) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Disarm, action); // gave up, did not flip
    TEST_ASSERT_FALSE(s.armed);
}

void test_polarity_unbounded_oscillation_never_disarms(void) {
    // Documents why the bound exists: with maxConductingReads == 0 an oscillating
    // conducting channel never decides and never disarms.
    PolarityState s{true, 0, 0};
    PolarityConfig unbounded{5, 0, MIN_A};
    for (int i = 0; i < 1000; i++) {
        float p = (i % 2 == 0) ? -50.0f : 50.0f;
        s = updatePolarity(s, p, COND_A, unbounded).state;
    }
    TEST_ASSERT_TRUE(s.armed);
}

// ============================================================================
// isConductingReading  (the current gate that stops offset-noise false flips)
// ============================================================================

void test_conducting_true_for_real_load(void) {
    TEST_ASSERT_TRUE(isConductingReading(100.0f, 5.0f, MIN_A)); // forward load
    TEST_ASSERT_TRUE(isConductingReading(1.0f, MIN_A, MIN_A));  // exactly at the threshold
}

void test_conducting_true_for_reversed_load_with_current(void) {
    // CRITICAL for the load/PV priority work: a reversed load reads NEGATIVE power
    // but with REAL current, so it must still count as conducting - that is what lets
    // a reversed CT both vote (to flip) and earn the armed boost (to resolve fast).
    TEST_ASSERT_TRUE(isConductingReading(-100.0f, 5.0f, MIN_A));
    TEST_ASSERT_TRUE(isConductingReading(-0.3f, 5.0f, MIN_A)); // tiny reversed power, real current
}

void test_conducting_false_for_offset_noise(void) {
    // THE BUG: ADE7953 offset noise is a small, steady, signed power at ~0 current.
    // On an unclamped role it must NOT conduct, or it votes a false reversal and hogs.
    TEST_ASSERT_FALSE(isConductingReading(1.2f, 0.004f, MIN_A));  // the observed bench value
    TEST_ASSERT_FALSE(isConductingReading(-3.5f, 0.0f, MIN_A));
    TEST_ASSERT_FALSE(isConductingReading(1.0f, 0.0199f, MIN_A)); // just below the threshold
}

void test_conducting_false_for_zero_power_or_nan(void) {
    TEST_ASSERT_FALSE(isConductingReading(0.0f, 5.0f, MIN_A));  // no power, current irrelevant
    TEST_ASSERT_FALSE(isConductingReading(NAN, 5.0f, MIN_A));   // bad power read
    TEST_ASSERT_FALSE(isConductingReading(100.0f, NAN, MIN_A)); // bad current read
}

void test_conducting_zero_threshold_degrades_to_power_only(void) {
    // minCurrent == 0 disables the gate: any non-zero power conducts (old behaviour).
    TEST_ASSERT_TRUE(isConductingReading(1.0f, 0.0f, 0.0f));
    TEST_ASSERT_FALSE(isConductingReading(0.0f, 0.0f, 0.0f));
}

// ============================================================================
// shouldClampNegative
// ============================================================================

void test_clamp_load_and_pv_negatives(void) {
    TEST_ASSERT_TRUE(shouldClampNegative(-5.0f, CHANNEL_ROLE_LOAD));
    TEST_ASSERT_TRUE(shouldClampNegative(-5.0f, CHANNEL_ROLE_PV));
}

void test_clamp_allows_grid_battery_inverter_negatives(void) {
    TEST_ASSERT_FALSE(shouldClampNegative(-5.0f, CHANNEL_ROLE_GRID));
    TEST_ASSERT_FALSE(shouldClampNegative(-5.0f, CHANNEL_ROLE_BATTERY));
    TEST_ASSERT_FALSE(shouldClampNegative(-5.0f, CHANNEL_ROLE_INVERTER));
}

void test_clamp_never_touches_positive(void) {
    TEST_ASSERT_FALSE(shouldClampNegative(5.0f, CHANNEL_ROLE_LOAD));
    TEST_ASSERT_FALSE(shouldClampNegative(0.0f, CHANNEL_ROLE_PV));
    TEST_ASSERT_FALSE(shouldClampNegative(0.0f, CHANNEL_ROLE_LOAD)); // exact zero is not negative
    TEST_ASSERT_FALSE(shouldClampNegative(NAN, CHANNEL_ROLE_LOAD));  // NaN < 0 is false in IEEE-754
}

// ============================================================================
// weighting
// ============================================================================

void test_role_priority_grid_battery_only(void) {
    TEST_ASSERT_TRUE(roleHasSchedulingPriority(CHANNEL_ROLE_GRID));
    TEST_ASSERT_TRUE(roleHasSchedulingPriority(CHANNEL_ROLE_BATTERY));
    TEST_ASSERT_FALSE(roleHasSchedulingPriority(CHANNEL_ROLE_LOAD));
    TEST_ASSERT_FALSE(roleHasSchedulingPriority(CHANNEL_ROLE_PV));
    TEST_ASSERT_FALSE(roleHasSchedulingPriority(CHANNEL_ROLE_INVERTER));
}

void test_weight_inactive_is_zero(void) {
    ChannelWeightInput in{false, 100.0f, 1.0f, false, CHANNEL_ROLE_LOAD};
    TEST_ASSERT_EQUAL_FLOAT(0.0f, computeChannelWeight(in, 0.5f, 0.5f, WCFG));
}

void test_weight_idle_load_is_min_base(void) {
    ChannelWeightInput in{true, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    TEST_ASSERT_EQUAL_FLOAT(WCFG.minBase, computeChannelWeight(in, 0.0f, 0.0f, WCFG));
}

void test_weight_grid_gets_multiplier(void) {
    ChannelWeightInput load{true, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    ChannelWeightInput grid{true, 0.0f, 0.0f, false, CHANNEL_ROLE_GRID};
    float wLoad = computeChannelWeight(load, 0.0f, 0.0f, WCFG);
    float wGrid = computeChannelWeight(grid, 0.0f, 0.0f, WCFG);
    TEST_ASSERT_EQUAL_FLOAT(wLoad * WCFG.rolePriorityMult, wGrid);
}

void test_weight_armed_boost_only_when_conducting(void) {
    // Armed but NOT conducting (no current): NO boost - this is what stops a no-load
    // armed channel from hogging the scheduler.
    ChannelWeightInput idleArmed{true, 0.0f, 0.0f, true, CHANNEL_ROLE_LOAD, false};
    TEST_ASSERT_EQUAL_FLOAT(WCFG.minBase, computeChannelWeight(idleArmed, 0.0f, 0.0f, WCFG));

    // Armed AND conducting: boost added on top of the normal weight.
    ChannelWeightInput condArmed{true, 500.0f, 0.0f, true, CHANNEL_ROLE_LOAD, true};
    ChannelWeightInput condUnarmed{true, 500.0f, 0.0f, false, CHANNEL_ROLE_LOAD, true};
    float wArmed = computeChannelWeight(condArmed, 1.0f, 0.0f, WCFG);
    float wUnarmed = computeChannelWeight(condUnarmed, 1.0f, 0.0f, WCFG);
    TEST_ASSERT_EQUAL_FLOAT(wUnarmed + WCFG.armedBoost, wArmed);
}

void test_weight_clamped_reversed_load_still_boosted(void) {
    // The load/PV priority fix: a reversed LOAD is stored clamped to 0, but it IS
    // conducting - so it must still earn the armed boost (driven by `conducting`,
    // not the clamped stored power). Without this, reversed loads resolved slowly.
    ChannelWeightInput in{true, 0.0f /*clamped*/, 0.0f, true, CHANNEL_ROLE_LOAD, true /*conducting*/};
    TEST_ASSERT_FLOAT_WITHIN(1e-5, WCFG.minBase + WCFG.armedBoost,
                             computeChannelWeight(in, 0.0f, 0.0f, WCFG));
    // And a clamped reversed PV likewise.
    ChannelWeightInput pv{true, 0.0f, 0.0f, true, CHANNEL_ROLE_PV, true};
    TEST_ASSERT_FLOAT_WITHIN(1e-5, WCFG.minBase + WCFG.armedBoost,
                             computeChannelWeight(pv, 0.0f, 0.0f, WCFG));
}

void test_weight_shares_contribute(void) {
    ChannelWeightInput in{true, 500.0f, 2.0f, false, CHANNEL_ROLE_LOAD};
    // full power share + full variability share + base
    float expected = WCFG.powerShare * 1.0f + WCFG.variability * 1.0f + WCFG.minBase;
    TEST_ASSERT_EQUAL_FLOAT(expected, computeChannelWeight(in, 1.0f, 1.0f, WCFG));
}

void test_computeWeights_zeroes_channel_zero_and_handles_nan(void) {
    const uint8_t N = 4;
    ChannelWeightInput in[N];
    in[0] = {true, 1000.0f, 0.0f, false, CHANNEL_ROLE_GRID}; // ch0 - excluded
    in[1] = {true, NAN, NAN, false, CHANNEL_ROLE_LOAD};      // bad reading
    in[2] = {true, 100.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    in[3] = {false, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};   // inactive
    float w[N] = {9, 9, 9, 9};

    computeWeights(in, N, 1, WCFG, w);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, w[0]); // channel 0 not scheduled
    TEST_ASSERT_EQUAL_FLOAT(0.0f, w[3]); // inactive
    // NaN channel does not crash and gets a finite (base) weight; ch2 carries the share.
    TEST_ASSERT_TRUE(w[1] >= WCFG.minBase);
    TEST_ASSERT_TRUE(w[2] > w[1]);
}

// ============================================================================
// scheduler core
// ============================================================================

void test_find_starved_channel(void) {
    const uint8_t N = 4;
    bool active[N] = {true, true, true, false};
    uint64_t last[N] = {0, 19000, 1000, 0};
    uint64_t now = 20000;
    uint8_t s = findStarvedChannel(last, active, N, 1, now, 15000);
    TEST_ASSERT_EQUAL_UINT8(2, s); // ch2 gap 19000 > 15000 (ch1 gap 1000 is fine)
}

void test_find_starved_skips_unbaselined_and_inactive(void) {
    const uint8_t N = 4;
    bool active[N] = {true, false, true, true};
    uint64_t last[N] = {0, 1, 0, 1}; // ch1 inactive, ch2 never baselined (0), ch3 baselined
    uint64_t now = 100000;
    uint8_t s = findStarvedChannel(last, active, N, 1, now, 15000);
    TEST_ASSERT_EQUAL_UINT8(3, s); // only ch3 qualifies
}

void test_find_starved_none(void) {
    const uint8_t N = 3;
    bool active[N] = {true, true, true};
    uint64_t last[N] = {0, 99000, 99500};
    uint64_t now = 100000;
    TEST_ASSERT_EQUAL_UINT8(NO_CHANNEL, findStarvedChannel(last, active, N, 1, now, 15000));
}

void test_wdrr_accumulate_gains_zeroes_and_clamps(void) {
    const uint8_t N = 4;
    float weights[N] = {0, 1.0f, 0.1f, 99.0f};
    bool active[N] = {true, true, true, false};
    float deficits[N] = {0, 4.9f, 0.0f, 7.0f};
    wdrrAccumulate(weights, deficits, active, N, 1, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 5.0f, deficits[1]); // 4.9 + 1.0 -> clamped to 5.0
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.1f, deficits[2]); // 0 + 0.1
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deficits[3]);        // inactive -> zeroed
}

void test_wdrr_pick_highest_deficit(void) {
    const uint8_t N = 4;
    float deficits[N] = {0, 0.2f, 0.9f, 0.5f};
    bool active[N] = {true, true, true, true};
    uint8_t cursor = 0;
    uint8_t pick = wdrrPick(deficits, active, N, 1, &cursor);
    TEST_ASSERT_EQUAL_UINT8(2, pick);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, -0.1f, deficits[2]); // decremented by 1.0
    TEST_ASSERT_EQUAL_UINT8(2, cursor);
}

void test_wdrr_pick_all_inactive_returns_invalid(void) {
    const uint8_t N = 3;
    float deficits[N] = {0, 1.0f, 2.0f};
    bool active[N] = {true, false, false};
    uint8_t cursor = 0;
    TEST_ASSERT_EQUAL_UINT8(NO_CHANNEL, wdrrPick(deficits, active, N, 1, &cursor));
}

void test_wdrr_tie_break_rotates_no_monopoly(void) {
    // Equal-deficit idle channels must rotate, not let the lowest index win every
    // time. Run many rounds and assert service counts are balanced.
    const uint8_t N = 6; // ch0 + 5 schedulable
    bool active[N] = {true, true, true, true, true, true};
    float weights[N] = {0, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    float deficits[N] = {0, 0, 0, 0, 0, 0};
    uint8_t cursor = 0;
    int picks[N] = {0};
    for (int r = 0; r < 500; r++) {
        wdrrAccumulate(weights, deficits, active, N, 1, 5.0f);
        uint8_t p = wdrrPick(deficits, active, N, 1, &cursor);
        TEST_ASSERT_NOT_EQUAL(NO_CHANNEL, p);
        picks[p]++;
    }
    for (uint8_t i = 1; i < N; i++) {
        TEST_ASSERT_TRUE(picks[i] > 50); // none starved
    }
}

// ============================================================================
// Integration simulations
// ============================================================================
//
// A small driver that runs the full per-round pipeline (weights -> accumulate ->
// pick -> detector) so the pieces are exercised together exactly as the firmware
// composes them.

void test_armed_no_load_channel_does_not_starve_peers(void) {
    // ch0 + 6 active LOAD channels, none carrying any load. ch1 is armed (the
    // installer just toggled it). The conduction-gated boost means ch1 is NEVER
    // boosted while idle, so peers are serviced fairly from the very first round,
    // and ch1 simply stays armed waiting for load.
    const uint8_t N = 7;
    const uint8_t START = 1;
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    PolarityState pol[N];
    for (uint8_t i = 0; i < N; i++) {
        in[i] = {i >= START, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
        active[i] = (i >= START);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
        pol[i] = {false, 0, 0};
    }
    pol[1].armed = true;
    in[1].armed = true;

    uint8_t cursor = 0;
    int gap[N] = {0};
    int maxGap[N] = {0};
    const int ROUNDS = 600;

    for (int r = 0; r < ROUNDS; r++) {
        for (uint8_t i = START; i < N; i++) {
            in[i].armed = pol[i].armed;
            in[i].conducting = (in[i].activePower != 0.0f); // pre-clamp conduction signal
        }
        computeWeights(in, N, START, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, START, 5.0f);
        uint8_t pick = wdrrPick(deficits, active, N, START, &cursor);
        TEST_ASSERT_NOT_EQUAL(NO_CHANNEL, pick);

        PolarityResult pr = updatePolarity(pol[pick], in[pick].activePower, COND_A, PCFG);
        pol[pick] = pr.state;

        for (uint8_t i = START; i < N; i++) {
            if (i == pick) gap[i] = 0;
            else {
                gap[i]++;
                if (gap[i] > maxGap[i]) maxGap[i] = gap[i];
            }
        }
    }

    // ch1 stays armed (no load ever arrived) - it must keep waiting, not give up.
    TEST_ASSERT_TRUE(pol[1].armed);
    // No starvation at any point: with 6 equal channels, fair service is ~6 rounds.
    for (uint8_t i = START; i < N; i++) {
        TEST_ASSERT_TRUE(maxGap[i] > 0);
        TEST_ASSERT_TRUE(maxGap[i] < 12);
    }
}

void test_armed_conducting_channel_resolves_quickly(void) {
    // ch1 armed AND carrying a steady reversed load: the conduction-gated boost
    // makes it win most slots and resolve (flip) within a handful of rounds.
    const uint8_t N = 7;
    const uint8_t START = 1;
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    PolarityState pol[N];
    for (uint8_t i = 0; i < N; i++) {
        in[i] = {i >= START, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
        active[i] = (i >= START);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
        pol[i] = {false, 0, 0};
    }
    pol[1].armed = true;
    in[1].activePower = -500.0f; // reversed load conducting

    uint8_t cursor = 0;
    int flipRound = -1;
    for (int r = 0; r < 200; r++) {
        for (uint8_t i = START; i < N; i++) {
            in[i].armed = pol[i].armed;
            in[i].conducting = (in[i].activePower != 0.0f); // pre-clamp conduction signal
        }
        computeWeights(in, N, START, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, START, 5.0f);
        uint8_t pick = wdrrPick(deficits, active, N, START, &cursor);

        PolarityResult pr = updatePolarity(pol[pick], in[pick].activePower, COND_A, PCFG);
        if (pr.action == PolarityAction::Flip && flipRound < 0) flipRound = r;
        pol[pick] = pr.state;
    }

    TEST_ASSERT_TRUE(flipRound >= 0);
    TEST_ASSERT_TRUE(flipRound < 20); // boosted -> resolves fast
    TEST_ASSERT_FALSE(pol[1].armed);  // disarmed after the flip
}

void test_grid_sampled_more_often_than_load(void) {
    // A grid channel and load channels, all idle: grid should be picked clearly
    // more often thanks to the role multiplier (which is NOT gated on conduction).
    const uint8_t N = 5; // ch0 + ch1 grid + ch2..4 load
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    for (uint8_t i = 0; i < N; i++) {
        ChannelRole role = (i == 1) ? CHANNEL_ROLE_GRID : CHANNEL_ROLE_LOAD;
        in[i] = {i >= 1, 0.0f, 0.0f, false, role};
        active[i] = (i >= 1);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
    }
    uint8_t cursor = 0;
    int picks[N] = {0};
    for (int r = 0; r < 1000; r++) {
        computeWeights(in, N, 1, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, 1, 5.0f);
        uint8_t p = wdrrPick(deficits, active, N, 1, &cursor);
        picks[p]++;
    }
    TEST_ASSERT_TRUE(picks[1] > picks[2]);
    TEST_ASSERT_TRUE(picks[1] > picks[3]);
    TEST_ASSERT_TRUE(picks[1] > picks[4]);
}

// ============================================================================
// Additional coverage (boundaries, precedence, multi-channel, full-size)
// ============================================================================

void test_polarity_nonpositive_threshold_is_noop(void) {
    // Misconfigured threshold must be a safe no-op, not decide on the first read.
    PolarityState s{true, 0, 0};
    PolarityConfig zero{0, 50, MIN_A};
    PolarityResult r = updatePolarity(s, -230.0f, COND_A, zero);
    TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    TEST_ASSERT_TRUE(r.state.armed);
    TEST_ASSERT_EQUAL_INT8(0, r.state.voteCount);
}

void test_polarity_threshold_one_decides_immediately(void) {
    PolarityState s{true, 0, 0};
    PolarityConfig one{1, 50, MIN_A};
    TEST_ASSERT_EQUAL(PolarityAction::Flip, updatePolarity(s, -100.0f, COND_A, one).action);
    TEST_ASSERT_EQUAL(PolarityAction::Disarm, updatePolarity(s, 100.0f, COND_A, one).action);
}

void test_polarity_one_below_threshold_stays_armed(void) {
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 4; i++) { // threshold is 5
        PolarityResult r = updatePolarity(s, -230.0f, COND_A, PCFG);
        s = r.state;
        action = r.action;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Continue, action);
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_INT8(-4, s.voteCount);
    TEST_ASSERT_EQUAL_UINT16(4, s.conductingReads);
}

void test_polarity_flip_takes_precedence_over_bound(void) {
    // bound == threshold: a genuinely reversed CT must Flip, not Disarm, on the
    // read where both conditions are first satisfiable.
    PolarityState s{true, 0, 0};
    PolarityConfig cfg{5, 5, MIN_A};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 5; i++) {
        PolarityResult r = updatePolarity(s, -230.0f, COND_A, cfg);
        s = r.state;
        action = r.action;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
    TEST_ASSERT_FALSE(s.armed);
}

void test_polarity_bound_disarms_without_flip(void) {
    PolarityState s{true, 0, 0};
    PolarityConfig cfg{5, 4, MIN_A};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 4; i++) {
        float p = (i % 2 == 0) ? -50.0f : 50.0f; // net stays near 0, never reaches 5
        PolarityResult r = updatePolarity(s, p, COND_A, cfg);
        s = r.state;
        action = r.action;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Disarm, action);
    TEST_ASSERT_FALSE(s.armed);
}

void test_polarity_vote_recovers_and_confirms(void) {
    // Goes to -4, then a run of forward votes recovers it to +5 -> confirm (no flip).
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 4; i++) s = updatePolarity(s, -100.0f, COND_A, PCFG).state; // vote -4
    TEST_ASSERT_EQUAL_INT8(-4, s.voteCount);
    for (int i = 0; i < 9; i++) {
        PolarityResult r = updatePolarity(s, 100.0f, COND_A, PCFG); // -3,-2,-1,0,1,2,3,4,5
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Disarm, action); // confirmed forward, no flip
}

void test_polarity_vote_recovers_and_flips(void) {
    // Mirror: +4 then a run of reversed votes -> reaches -5 -> flip.
    PolarityState s{true, 0, 0};
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 4; i++) s = updatePolarity(s, 100.0f, COND_A, PCFG).state; // vote +4
    for (int i = 0; i < 9; i++) {
        PolarityResult r = updatePolarity(s, -100.0f, COND_A, PCFG);
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
}

void test_polarity_disarmed_mid_sequence_is_noop(void) {
    PolarityState s{false, -3, 3}; // disarmed but with residual counters
    PolarityResult r = updatePolarity(s, -230.0f, COND_A, PCFG);
    TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    TEST_ASSERT_FALSE(r.state.armed);
    TEST_ASSERT_EQUAL_INT8(-3, r.state.voteCount);     // unchanged
    TEST_ASSERT_EQUAL_UINT16(3, r.state.conductingReads);
}

void test_weight_grid_armed_conducting_all_bumps_stack(void) {
    // grid (x2) + armed + conducting + full power share: pins the arithmetic order
    // (role multiply on the base, THEN add the flat armed boost).
    ChannelWeightInput in{true, -500.0f, 0.0f, true, CHANNEL_ROLE_GRID, true};
    // base = 0.4*1 + 0.4*0 + 0.1 = 0.5; *2.0 = 1.0; + 1.5 = 2.5
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.5f, computeChannelWeight(in, 1.0f, 0.0f, WCFG));
    // same but not conducting -> no boost: 0.5*2.0 = 1.0
    ChannelWeightInput idle{true, 0.0f, 0.0f, true, CHANNEL_ROLE_GRID, false};
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.0f, computeChannelWeight(idle, 1.0f, 0.0f, WCFG));
}

void test_computeWeights_battery_gets_multiplier(void) {
    const uint8_t N = 3;
    ChannelWeightInput in[N];
    in[0] = {false, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    in[1] = {true, 100.0f, 0.0f, false, CHANNEL_ROLE_BATTERY};
    in[2] = {true, 100.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    float w[N] = {};
    computeWeights(in, N, 1, WCFG, w);
    TEST_ASSERT_TRUE(w[1] > w[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, w[2] * WCFG.rolePriorityMult, w[1]);
}

void test_computeWeights_tiny_channel_gets_min_base(void) {
    const uint8_t N = 3;
    ChannelWeightInput in[N];
    in[0] = {false, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    in[1] = {true, 10000.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    in[2] = {true, 1.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    float w[N] = {};
    computeWeights(in, N, 1, WCFG, w);
    TEST_ASSERT_TRUE(w[2] >= WCFG.minBase); // anti-starvation floor holds under skew
    TEST_ASSERT_TRUE(w[1] > w[2]);
}

void test_computeWeights_all_inactive_all_zeros(void) {
    const uint8_t N = 4;
    ChannelWeightInput in[N];
    for (uint8_t i = 0; i < N; i++) in[i] = {false, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    float w[N] = {9, 9, 9, 9};
    computeWeights(in, N, 1, WCFG, w);
    for (uint8_t i = 0; i < N; i++) TEST_ASSERT_EQUAL_FLOAT(0.0f, w[i]);
}

void test_computeWeights_variability_differentiates_equal_power(void) {
    const uint8_t N = 3;
    ChannelWeightInput in[N];
    in[0] = {false, 0.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
    in[1] = {true, 100.0f, 0.1f, false, CHANNEL_ROLE_LOAD};
    in[2] = {true, 100.0f, 0.9f, false, CHANNEL_ROLE_LOAD};
    float w[N] = {};
    computeWeights(in, N, 1, WCFG, w);
    TEST_ASSERT_TRUE(w[2] > w[1]); // higher variability -> more weight
}

void test_find_starved_returns_lowest_index_when_multiple(void) {
    const uint8_t N = 5;
    bool active[N] = {true, true, true, true, true};
    uint64_t last[N] = {0, 1000, 2000, 1000, 2000}; // all past gap at now=30000
    TEST_ASSERT_EQUAL_UINT8(1, findStarvedChannel(last, active, N, 1, 30000, 5000));
}

void test_find_starved_exactly_at_gap_is_not_starved(void) {
    const uint8_t N = 3;
    bool active[N] = {true, true, true};
    uint64_t last[N] = {0, 5000, 4999}; // ch1 gap=15000 (==), ch2 gap=15001 (>)
    TEST_ASSERT_EQUAL_UINT8(2, findStarvedChannel(last, active, N, 1, 20000, 15000));
    uint64_t last2[N] = {0, 4999, 5000}; // ch1 gap=15001 (>), ch2 gap=15000 (==)
    TEST_ASSERT_EQUAL_UINT8(1, findStarvedChannel(last2, active, N, 1, 20000, 15000));
}

void test_find_starved_now_before_lastMillis_returns_channel(void) {
    // Defensive: if lastMillis > now (clock skew / torn read), the unsigned gap
    // underflows to a huge value and the channel is force-picked. This matches the
    // original firmware and is the intended (force-read rather than defer) behavior.
    const uint8_t N = 3;
    bool active[N] = {true, true, true};
    uint64_t last[N] = {0, 5000, 5000};
    TEST_ASSERT_EQUAL_UINT8(1, findStarvedChannel(last, active, N, 1, 1000, 5000));
}

void test_wdrr_accumulate_nan_weight_is_zeroed(void) {
    const uint8_t N = 3;
    float weights[N] = {0, NAN, 0.5f};
    bool active[N] = {false, true, true};
    float deficits[N] = {0, 2.0f, 2.0f};
    wdrrAccumulate(weights, deficits, active, N, 1, 5.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, deficits[1]); // NaN poisoned then reset
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.5f, deficits[2]);
}

void test_wdrr_accumulate_clamps_negative(void) {
    const uint8_t N = 2;
    float weights[N] = {0, 0.0f};
    bool active[N] = {false, true};
    float deficits[N] = {0, -5.5f};
    wdrrAccumulate(weights, deficits, active, N, 1, 5.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, -5.0f, deficits[1]); // clamped to -bound
}

void test_wdrr_pick_single_channel_always_wins(void) {
    const uint8_t N = 4;
    float deficits[N] = {0, 0.0f, 0.0f, 0.5f};
    bool active[N] = {true, false, false, true}; // only ch3 schedulable
    uint8_t cursor = 3;
    TEST_ASSERT_EQUAL_UINT8(3, wdrrPick(deficits, active, N, 1, &cursor));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, -0.5f, deficits[3]);
    cursor = 1; // even starting elsewhere, the wrap finds ch3
    deficits[3] = 0.5f;
    TEST_ASSERT_EQUAL_UINT8(3, wdrrPick(deficits, active, N, 1, &cursor));
}

void test_wdrr_pick_cursor_wraps_at_end(void) {
    // cursor at the last index must wrap to startIndex on the next pick.
    const uint8_t N = 4;
    float deficits[N] = {0, 0.1f, 0.1f, 0.1f};
    bool active[N] = {true, true, true, true};
    uint8_t cursor = 3; // last index -> next starts at ch1
    TEST_ASSERT_EQUAL_UINT8(1, wdrrPick(deficits, active, N, 1, &cursor));
    TEST_ASSERT_EQUAL_UINT8(1, cursor);
    // next pick starts one past ch1 -> ch2 (deficits ch2,ch3 still 0.1, ch1 now -0.9)
    TEST_ASSERT_EQUAL_UINT8(2, wdrrPick(deficits, active, N, 1, &cursor));
}

void test_wdrr_pick_tie_goes_to_channel_after_cursor(void) {
    const uint8_t N = 4;
    float deficits[N] = {0, 1.0f, 1.0f, 0.0f};
    bool active[N] = {true, true, true, false};
    uint8_t cursor = 1; // iteration starts at ch2
    TEST_ASSERT_EQUAL_UINT8(2, wdrrPick(deficits, active, N, 1, &cursor));
}

void test_wdrr_pick_all_negative_deficits_picks_max(void) {
    // Validates the no-sentinel picker: all deficits negative, highest still wins.
    const uint8_t N = 4;
    float deficits[N] = {0, -3.0f, -1.0f, -2.0f};
    bool active[N] = {true, true, true, true};
    uint8_t cursor = 0;
    TEST_ASSERT_EQUAL_UINT8(2, wdrrPick(deficits, active, N, 1, &cursor));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, -2.0f, deficits[2]);
}

void test_multiple_armed_channels_both_resolve(void) {
    // Two reversed CTs armed at once (panel with several reversed clips): both must
    // flip, neither starving the other.
    const uint8_t N = 5;
    const uint8_t START = 1;
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    PolarityState pol[N];
    float power[N] = {0, -500.0f, -300.0f, 200.0f, 100.0f};
    for (uint8_t i = 0; i < N; i++) {
        in[i] = {i >= START, power[i], 0.0f, false, CHANNEL_ROLE_LOAD};
        active[i] = (i >= START);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
        pol[i] = {false, 0, 0};
    }
    pol[1].armed = true;
    pol[2].armed = true;

    uint8_t cursor = 0;
    bool flipped1 = false, flipped2 = false;
    for (int r = 0; r < 200; r++) {
        for (uint8_t i = START; i < N; i++) {
            in[i].armed = pol[i].armed;
            in[i].conducting = (in[i].activePower != 0.0f); // pre-clamp conduction signal
        }
        computeWeights(in, N, START, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, START, 5.0f);
        uint8_t pick = wdrrPick(deficits, active, N, START, &cursor);
        PolarityResult pr = updatePolarity(pol[pick], in[pick].activePower, COND_A, PCFG);
        if (pr.action == PolarityAction::Flip && pick == 1) flipped1 = true;
        if (pr.action == PolarityAction::Flip && pick == 2) flipped2 = true;
        pol[pick] = pr.state;
    }
    TEST_ASSERT_TRUE(flipped1);
    TEST_ASSERT_TRUE(flipped2);
    TEST_ASSERT_FALSE(pol[1].armed);
    TEST_ASSERT_FALSE(pol[2].armed);
}

void test_full_17_channel_no_starvation(void) {
    // Production array size: 17 channels (ch0 + 16 mux), all active LOAD, equal load.
    // Every schedulable channel must be serviced and none may monopolise.
    const uint8_t N = 17;
    const uint8_t START = 1;
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    for (uint8_t i = 0; i < N; i++) {
        in[i] = {i >= START, 100.0f, 0.0f, false, CHANNEL_ROLE_LOAD};
        active[i] = (i >= START);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
    }
    uint8_t cursor = 0;
    int picks[N] = {0};
    for (int r = 0; r < 1600; r++) {
        computeWeights(in, N, START, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, START, 5.0f);
        uint8_t p = wdrrPick(deficits, active, N, START, &cursor);
        TEST_ASSERT_NOT_EQUAL(NO_CHANNEL, p);
        picks[p]++;
    }
    // 16 equal channels over 1600 rounds -> ~100 each. Assert balanced, none starved.
    for (uint8_t i = START; i < N; i++) {
        TEST_ASSERT_TRUE(picks[i] > 50);
        TEST_ASSERT_TRUE(picks[i] < 200);
    }
}

void test_mixed_fleet_grid_battery_sampled_more(void) {
    // grid + battery (priority roles) vs load + PV: priority roles sampled clearly
    // more often, and nothing is starved.
    const uint8_t N = 6;
    const uint8_t START = 1;
    ChannelWeightInput in[N];
    bool active[N];
    float deficits[N];
    float weights[N];
    ChannelRole roles[N] = {CHANNEL_ROLE_LOAD, CHANNEL_ROLE_GRID, CHANNEL_ROLE_BATTERY,
                            CHANNEL_ROLE_LOAD, CHANNEL_ROLE_LOAD, CHANNEL_ROLE_PV};
    float power[N] = {0, 1000.0f, 500.0f, 300.0f, 200.0f, 400.0f};
    for (uint8_t i = 0; i < N; i++) {
        in[i] = {i >= START, power[i], 0.0f, false, roles[i]};
        active[i] = (i >= START);
        deficits[i] = 0.0f;
        weights[i] = 0.0f;
    }
    uint8_t cursor = 0;
    int picks[N] = {0};
    for (int r = 0; r < 2000; r++) {
        computeWeights(in, N, START, WCFG, weights);
        wdrrAccumulate(weights, deficits, active, N, START, 5.0f);
        uint8_t p = wdrrPick(deficits, active, N, START, &cursor);
        picks[p]++;
    }
    TEST_ASSERT_TRUE(picks[1] > picks[3]); // grid > load
    TEST_ASSERT_TRUE(picks[2] > picks[3]); // battery > load
    TEST_ASSERT_TRUE(picks[1] > picks[5]); // grid > PV
    for (uint8_t i = START; i < N; i++) TEST_ASSERT_TRUE(picks[i] > 50); // none starved
}

// ============================================================================
// Current-gate integration (reproduces the bench false-flip and proves the fix)
// ============================================================================

void test_offset_noise_never_false_flips_unclamped_role(void) {
    // THE HARDWARE REGRESSION, end to end: a grid channel (unclamped role) armed with
    // no real load. The ADE7953 reports a small steady offset (+1.2 W) at ~0.004 A.
    // Current-gated, every such read is non-conducting: updatePolarity no-ops, the
    // channel never votes and stays armed - no false "CT reversal detected".
    PolarityState s{true, 0, 0};
    for (int i = 0; i < 1000; i++) {
        PolarityResult r = updatePolarity(s, 1.2f, 0.004f, PCFG);
        s = r.state;
        TEST_ASSERT_EQUAL(PolarityAction::Continue, r.action);
    }
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_INT8(0, s.voteCount);
    TEST_ASSERT_EQUAL_UINT16(0, s.conductingReads);

    // A genuine reversed load finally flows (-2000 W at 8.7 A): now it must flip.
    PolarityAction action = PolarityAction::Continue;
    for (int i = 0; i < 10; i++) {
        PolarityResult r = updatePolarity(s, -2000.0f, 8.7f, PCFG);
        s = r.state;
        action = r.action;
        if (action != PolarityAction::Continue) break;
    }
    TEST_ASSERT_EQUAL(PolarityAction::Flip, action);
}

void test_offset_noise_earns_no_boost(void) {
    // The other half of the regression: offset noise must not earn the armed boost,
    // or the noisy channel hogs the scheduler. The caller derives `conducting` from
    // isConductingReading, so an armed grid channel on offset noise gets only its
    // normal (role-multiplied) weight - no boost.
    bool conducting = isConductingReading(1.2f, 0.004f, MIN_A);
    TEST_ASSERT_FALSE(conducting);
    ChannelWeightInput in{true, 1.2f, 0.0f, true /*armed*/, CHANNEL_ROLE_GRID, conducting};
    // base 0.1 * rolePriorityMult 2.0 = 0.2; NO armed boost.
    TEST_ASSERT_FLOAT_WITHIN(1e-5, WCFG.minBase * WCFG.rolePriorityMult,
                             computeChannelWeight(in, 0.0f, 0.0f, WCFG));
}

void test_low_current_noise_does_not_burn_the_bound(void) {
    // A noisy unclamped channel that oscillates sign at ~0 current must NOT burn its
    // conducting-read bound: it stays armed indefinitely (waiting for real load),
    // exactly like an idle channel, rather than disarming on noise.
    PolarityState s{true, 0, 0};
    for (int i = 0; i < 500; i++) {
        float p = (i % 2 == 0) ? -2.0f : 2.0f; // offset noise, both signs
        s = updatePolarity(s, p, 0.004f, PCFG).state;
    }
    TEST_ASSERT_TRUE(s.armed);
    TEST_ASSERT_EQUAL_UINT16(0, s.conductingReads);
}

// ============================================================================
// runner
// ============================================================================

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_polarity_not_armed_is_noop);
    RUN_TEST(test_polarity_steady_reverse_flips_at_threshold);
    RUN_TEST(test_polarity_steady_forward_confirms_without_flip);
    RUN_TEST(test_polarity_is_magnitude_independent);
    RUN_TEST(test_polarity_idle_is_noop_and_stays_armed);
    RUN_TEST(test_polarity_nan_is_noop);
    RUN_TEST(test_polarity_delayed_load_still_detects);
    RUN_TEST(test_polarity_oscillating_conducting_disarms_via_bound);
    RUN_TEST(test_polarity_unbounded_oscillation_never_disarms);

    RUN_TEST(test_conducting_true_for_real_load);
    RUN_TEST(test_conducting_true_for_reversed_load_with_current);
    RUN_TEST(test_conducting_false_for_offset_noise);
    RUN_TEST(test_conducting_false_for_zero_power_or_nan);
    RUN_TEST(test_conducting_zero_threshold_degrades_to_power_only);

    RUN_TEST(test_clamp_load_and_pv_negatives);
    RUN_TEST(test_clamp_allows_grid_battery_inverter_negatives);
    RUN_TEST(test_clamp_never_touches_positive);

    RUN_TEST(test_role_priority_grid_battery_only);
    RUN_TEST(test_weight_inactive_is_zero);
    RUN_TEST(test_weight_idle_load_is_min_base);
    RUN_TEST(test_weight_grid_gets_multiplier);
    RUN_TEST(test_weight_armed_boost_only_when_conducting);
    RUN_TEST(test_weight_shares_contribute);
    RUN_TEST(test_computeWeights_zeroes_channel_zero_and_handles_nan);

    RUN_TEST(test_find_starved_channel);
    RUN_TEST(test_find_starved_skips_unbaselined_and_inactive);
    RUN_TEST(test_find_starved_none);
    RUN_TEST(test_wdrr_accumulate_gains_zeroes_and_clamps);
    RUN_TEST(test_wdrr_pick_highest_deficit);
    RUN_TEST(test_wdrr_pick_all_inactive_returns_invalid);
    RUN_TEST(test_wdrr_tie_break_rotates_no_monopoly);

    RUN_TEST(test_armed_no_load_channel_does_not_starve_peers);
    RUN_TEST(test_armed_conducting_channel_resolves_quickly);
    RUN_TEST(test_grid_sampled_more_often_than_load);

    // Additional coverage
    RUN_TEST(test_polarity_nonpositive_threshold_is_noop);
    RUN_TEST(test_polarity_threshold_one_decides_immediately);
    RUN_TEST(test_polarity_one_below_threshold_stays_armed);
    RUN_TEST(test_polarity_flip_takes_precedence_over_bound);
    RUN_TEST(test_polarity_bound_disarms_without_flip);
    RUN_TEST(test_polarity_vote_recovers_and_confirms);
    RUN_TEST(test_polarity_vote_recovers_and_flips);
    RUN_TEST(test_polarity_disarmed_mid_sequence_is_noop);

    RUN_TEST(test_weight_grid_armed_conducting_all_bumps_stack);
    RUN_TEST(test_weight_clamped_reversed_load_still_boosted);
    RUN_TEST(test_computeWeights_battery_gets_multiplier);
    RUN_TEST(test_computeWeights_tiny_channel_gets_min_base);
    RUN_TEST(test_computeWeights_all_inactive_all_zeros);
    RUN_TEST(test_computeWeights_variability_differentiates_equal_power);

    RUN_TEST(test_find_starved_returns_lowest_index_when_multiple);
    RUN_TEST(test_find_starved_exactly_at_gap_is_not_starved);
    RUN_TEST(test_find_starved_now_before_lastMillis_returns_channel);
    RUN_TEST(test_wdrr_accumulate_nan_weight_is_zeroed);
    RUN_TEST(test_wdrr_accumulate_clamps_negative);
    RUN_TEST(test_wdrr_pick_single_channel_always_wins);
    RUN_TEST(test_wdrr_pick_cursor_wraps_at_end);
    RUN_TEST(test_wdrr_pick_tie_goes_to_channel_after_cursor);
    RUN_TEST(test_wdrr_pick_all_negative_deficits_picks_max);

    RUN_TEST(test_multiple_armed_channels_both_resolve);
    RUN_TEST(test_full_17_channel_no_starvation);
    RUN_TEST(test_mixed_fleet_grid_battery_sampled_more);

    RUN_TEST(test_offset_noise_never_false_flips_unclamped_role);
    RUN_TEST(test_offset_noise_earns_no_boost);
    RUN_TEST(test_low_current_noise_does_not_burn_the_bound);

    return UNITY_END();
}
