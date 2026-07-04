// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the pure grid frequency EMA. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)
//
// These need no hardware: they verify the datasheet Eq.36 conversion, seeding,
// range rejection, convergence onto a dithering input's sub-LSB mean (the whole
// point of the Q24.8 filter) and the monotonic update counter that the 500 ms
// sampler uses as its freshness gate.

#include <unity.h>
#include <cmath>
#include "grid_frequency.h"

using namespace GridFrequency;

void setUp(void) {}
void tearDown(void) {}

// Firmware values: 223.75 kHz PERIOD clock, 45-65 Hz validation window.
static const Config CFG = {223750.0f, 45.0f, 65.0f};

// Eq.36 reference for a (possibly fractional) period value.
static float eq36(float period) { return 223750.0f / (period + 1.0f); }

// PERIOD is ~4474 raw at exactly 50.000 Hz: 223750 / (4474 + 1) = 50.000
static const int32_t PERIOD_50HZ = 4474;

// ---------------------------------------------------------------------------
// Eq.36 conversion
// ---------------------------------------------------------------------------

void test_eq36_nominal_50hz(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.0f, rawFrequencyHz(CFG, PERIOD_50HZ));
}

void test_eq36_plus_one_matters(void) {
    // Without the +1 the same register value reads ~11 mHz high.
    float withPlusOne = rawFrequencyHz(CFG, PERIOD_50HZ);
    float withoutPlusOne = 223750.0f / float(PERIOD_50HZ);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.0112f, withoutPlusOne - withPlusOne);
}

void test_eq36_non_positive_period(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rawFrequencyHz(CFG, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rawFrequencyHz(CFG, -1));
}

// ---------------------------------------------------------------------------
// Seeding
// ---------------------------------------------------------------------------

void test_unseeded_readout_is_zero(void) {
    State s;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, frequencyHz(s, CFG));
}

void test_first_valid_read_seeds_exactly(void) {
    State s;
    TEST_ASSERT_TRUE(update(s, CFG, PERIOD_50HZ));
    TEST_ASSERT_TRUE(s.seeded);
    TEST_ASSERT_EQUAL_INT32(PERIOD_50HZ << 8, s.emaPeriodQ8);
    TEST_ASSERT_EQUAL_UINT32(1, s.updateCount);
    // Readout equals the single-read conversion: no ramp from zero.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, eq36(float(PERIOD_50HZ)), frequencyHz(s, CFG));
}

void test_invalid_reads_do_not_seed(void) {
    State s;
    TEST_ASSERT_FALSE(update(s, CFG, 0));     // dead read
    TEST_ASSERT_FALSE(update(s, CFG, 3000));  // ~74.5 Hz, above max
    TEST_ASSERT_FALSE(update(s, CFG, 5000));  // ~44.7 Hz, below min
    TEST_ASSERT_FALSE(s.seeded);
    TEST_ASSERT_EQUAL_UINT32(0, s.updateCount);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, frequencyHz(s, CFG));
}

// ---------------------------------------------------------------------------
// Range rejection after seeding
// ---------------------------------------------------------------------------

void test_out_of_range_leaves_state_untouched(void) {
    State s;
    update(s, CFG, PERIOD_50HZ);
    int32_t emaBefore = s.emaPeriodQ8;
    uint32_t countBefore = s.updateCount;

    TEST_ASSERT_FALSE(update(s, CFG, 3000));   // glitch high
    TEST_ASSERT_FALSE(update(s, CFG, 65535));  // ~3.4 Hz, absurd
    TEST_ASSERT_FALSE(update(s, CFG, -5));

    TEST_ASSERT_EQUAL_INT32(emaBefore, s.emaPeriodQ8);
    TEST_ASSERT_EQUAL_UINT32(countBefore, s.updateCount);
}

// ---------------------------------------------------------------------------
// Convergence and sub-LSB resolution (the reason the filter exists)
// ---------------------------------------------------------------------------

void test_converges_to_dithering_mean(void) {
    // The raw register dithers between adjacent codes almost every cycle;
    // 50/50 between 4474 and 4475 means a true period of 4474.5. The EMA must
    // settle onto that half-code value, which no single read can express.
    State s;
    for (int i = 0; i < 400; i++) update(s, CFG, (i % 2 == 0) ? 4474 : 4475);

    // EMA ripple for an alternating square wave is ~alpha/(2-alpha) of the step
    // (~0.07 LSB, ~0.75 mHz here) - assert within 1.5 mHz of the true mean.
    TEST_ASSERT_FLOAT_WITHIN(0.0015f, eq36(4474.5f), frequencyHz(s, CFG));
}

void test_sub_lsb_duty_cycle_resolved(void) {
    // 25% duty (one 4475 in four reads) = true period 4474.25. A plain-integer
    // EMA freezes on 4474 and cannot see this at all; the Q8 fraction must.
    State s;
    for (int i = 0; i < 800; i++) update(s, CFG, (i % 4 == 0) ? 4475 : 4474);

    float f = frequencyHz(s, CFG);
    float expected = eq36(4474.25f);
    float frozen = eq36(4474.0f); // what an integer EMA would report
    TEST_ASSERT_FLOAT_WITHIN(0.0015f, expected, f);
    // Must be clearly distinguishable from the integer-frozen answer (~2.8 mHz off).
    TEST_ASSERT_TRUE(fabsf(f - frozen) > fabsf(f - expected));
}

void test_step_response_converges_both_directions(void) {
    State s;
    for (int i = 0; i < 300; i++) update(s, CFG, 4474);
    // Step up: >>3 on a positive gap truncates toward zero, parking within
    // 7 Q8 units (~0.027 LSB, ~0.3 mHz) below the target - accept 0.5 mHz.
    for (int i = 0; i < 300; i++) update(s, CFG, 4475);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, eq36(4475.0f), frequencyHz(s, CFG));
    // Step down: arithmetic shift of the negative gap rounds away from zero,
    // so it converges exactly.
    for (int i = 0; i < 300; i++) update(s, CFG, 4474);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, eq36(4474.0f), frequencyHz(s, CFG));
}

// ---------------------------------------------------------------------------
// Update counter (freshness gate for the 500 ms sampler)
// ---------------------------------------------------------------------------

void test_counter_advances_only_on_accept(void) {
    State s;
    update(s, CFG, PERIOD_50HZ);
    update(s, CFG, PERIOD_50HZ + 1);
    TEST_ASSERT_EQUAL_UINT32(2, s.updateCount);

    update(s, CFG, 0);
    update(s, CFG, 3000);
    TEST_ASSERT_EQUAL_UINT32(2, s.updateCount);

    update(s, CFG, PERIOD_50HZ);
    TEST_ASSERT_EQUAL_UINT32(3, s.updateCount);
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_eq36_nominal_50hz);
    RUN_TEST(test_eq36_plus_one_matters);
    RUN_TEST(test_eq36_non_positive_period);

    RUN_TEST(test_unseeded_readout_is_zero);
    RUN_TEST(test_first_valid_read_seeds_exactly);
    RUN_TEST(test_invalid_reads_do_not_seed);

    RUN_TEST(test_out_of_range_leaves_state_untouched);

    RUN_TEST(test_converges_to_dithering_mean);
    RUN_TEST(test_sub_lsb_duty_cycle_resolved);
    RUN_TEST(test_step_response_converges_both_directions);

    RUN_TEST(test_counter_advances_only_on_accept);

    return UNITY_END();
}
