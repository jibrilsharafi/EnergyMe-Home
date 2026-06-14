// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for PhaseUtils phase-rotation helpers. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)

#include <unity.h>
#include "phase_utils.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// getLaggingPhase - IEC rotation L1->L2->L3->L1
// ============================================================================

void test_lagging_from_L1_is_L2(void) {
    TEST_ASSERT_EQUAL(PHASE_2, PhaseUtils::getLaggingPhase(PHASE_1));
}

void test_lagging_from_L2_is_L3(void) {
    TEST_ASSERT_EQUAL(PHASE_3, PhaseUtils::getLaggingPhase(PHASE_2));
}

void test_lagging_from_L3_is_L1(void) {
    TEST_ASSERT_EQUAL(PHASE_1, PhaseUtils::getLaggingPhase(PHASE_3));
}

void test_lagging_split240_is_identity(void) {
    TEST_ASSERT_EQUAL(PHASE_SPLIT_240, PhaseUtils::getLaggingPhase(PHASE_SPLIT_240));
}

// ============================================================================
// getLeadingPhase - IEC rotation L1->L3->L2->L1
// ============================================================================

void test_leading_from_L1_is_L3(void) {
    TEST_ASSERT_EQUAL(PHASE_3, PhaseUtils::getLeadingPhase(PHASE_1));
}

void test_leading_from_L2_is_L1(void) {
    TEST_ASSERT_EQUAL(PHASE_1, PhaseUtils::getLeadingPhase(PHASE_2));
}

void test_leading_from_L3_is_L2(void) {
    TEST_ASSERT_EQUAL(PHASE_2, PhaseUtils::getLeadingPhase(PHASE_3));
}

void test_leading_split240_is_identity(void) {
    TEST_ASSERT_EQUAL(PHASE_SPLIT_240, PhaseUtils::getLeadingPhase(PHASE_SPLIT_240));
}

void test_leading_and_lagging_are_inverses(void) {
    // Leading and lagging must be exact inverses: leading(lagging(x)) == x
    TEST_ASSERT_EQUAL(PHASE_1, PhaseUtils::getLeadingPhase(PhaseUtils::getLaggingPhase(PHASE_1)));
    TEST_ASSERT_EQUAL(PHASE_2, PhaseUtils::getLeadingPhase(PhaseUtils::getLaggingPhase(PHASE_2)));
    TEST_ASSERT_EQUAL(PHASE_3, PhaseUtils::getLeadingPhase(PhaseUtils::getLaggingPhase(PHASE_3)));
}

// ============================================================================
// calculatePhaseShiftDeg
// ============================================================================

void test_shift_same_phase_is_zero(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_2, PHASE_2));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_3, PHASE_3));
}

void test_shift_L1_voltage_L2_current_is_minus_120(void) {
    // L2 lags L1 by 120°: current zero-crossing comes 120° after voltage reference
    TEST_ASSERT_EQUAL_FLOAT(-120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_2));
}

void test_shift_L1_voltage_L3_current_is_plus_120(void) {
    // L3 leads L1 by 120°: current zero-crossing comes 120° before voltage reference
    TEST_ASSERT_EQUAL_FLOAT(120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_3));
}

void test_shift_is_antisymmetric(void) {
    // Swapping voltage and current must negate the shift
    TEST_ASSERT_EQUAL_FLOAT(120.0f,  PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_3));
    TEST_ASSERT_EQUAL_FLOAT(-120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_3, PHASE_1));
    TEST_ASSERT_EQUAL_FLOAT(-120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_2));
    TEST_ASSERT_EQUAL_FLOAT(120.0f,  PhaseUtils::calculatePhaseShiftDeg(PHASE_2, PHASE_1));
}

void test_shift_split240_reference_is_zero(void) {
    // Split-phase 240V shares the L1 reference angle
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_SPLIT_240, PHASE_1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_SPLIT_240));
}

// ============================================================================
// Regression guard: old (wrong) convention would have flipped signs
// These values must NOT change without a deliberate decision and release note.
// ============================================================================

void test_regression_L2_angle_is_negative(void) {
    // Before the IEC fix, PHASE_2 was +120° (L3). Confirm it is now -120° (L2).
    TEST_ASSERT_EQUAL_FLOAT(-120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_2));
}

void test_regression_L3_angle_is_positive(void) {
    // Before the IEC fix, PHASE_3 was -120° (L2). Confirm it is now +120° (L3).
    TEST_ASSERT_EQUAL_FLOAT(120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_1, PHASE_3));
}

void test_regression_lagging_from_L1_is_L2_not_L3(void) {
    // Before the fix: getLaggingPhase(PHASE_1) returned PHASE_3. Must now return PHASE_2.
    TEST_ASSERT_EQUAL(PHASE_2, PhaseUtils::getLaggingPhase(PHASE_1));
    TEST_ASSERT_NOT_EQUAL(PHASE_3, PhaseUtils::getLaggingPhase(PHASE_1));
}

void test_regression_leading_from_L1_is_L3_not_L2(void) {
    // Before the fix: getLeadingPhase(PHASE_1) returned PHASE_2. Must now return PHASE_3.
    TEST_ASSERT_EQUAL(PHASE_3, PhaseUtils::getLeadingPhase(PHASE_1));
    TEST_ASSERT_NOT_EQUAL(PHASE_2, PhaseUtils::getLeadingPhase(PHASE_1));
}

// ============================================================================
// runner
// ============================================================================

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_lagging_from_L1_is_L2);
    RUN_TEST(test_lagging_from_L2_is_L3);
    RUN_TEST(test_lagging_from_L3_is_L1);
    RUN_TEST(test_lagging_split240_is_identity);

    RUN_TEST(test_leading_from_L1_is_L3);
    RUN_TEST(test_leading_from_L2_is_L1);
    RUN_TEST(test_leading_from_L3_is_L2);
    RUN_TEST(test_leading_split240_is_identity);
    RUN_TEST(test_leading_and_lagging_are_inverses);

    RUN_TEST(test_shift_same_phase_is_zero);
    RUN_TEST(test_shift_L1_voltage_L2_current_is_minus_120);
    RUN_TEST(test_shift_L1_voltage_L3_current_is_plus_120);
    RUN_TEST(test_shift_is_antisymmetric);
    RUN_TEST(test_shift_split240_reference_is_zero);

    RUN_TEST(test_regression_L2_angle_is_negative);
    RUN_TEST(test_regression_L3_angle_is_positive);
    RUN_TEST(test_regression_lagging_from_L1_is_L2_not_L3);
    RUN_TEST(test_regression_leading_from_L1_is_L3_not_L2);

    return UNITY_END();
}
