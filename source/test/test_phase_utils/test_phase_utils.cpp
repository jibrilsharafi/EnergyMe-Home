// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for PhaseUtils phase-rotation helpers. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)

#include <unity.h>
#include <math.h>
#include "phase_utils.h"

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Forward physical model of the ADE7953 ANGLE register
//
// This is the part the naming tests below cannot cover: it starts from physical
// reality (which line the CT is actually clipped on, what the load is doing, which
// way round the CT is) and synthesises the register the chip would report, so the
// correction can be checked end to end instead of only for self-consistency.
//
// Datasheet Rev. C, Figure 60: ANGLE is the delay from the voltage negative-to-
// positive zero crossing to the current one, so ANGLE = thetaV - thetaI, positive
// for an inductive (lagging) load. Register range is +-180 deg (a raw -104.2 deg
// was captured in the field, so it is genuinely signed, not a 0..360 delay).
// ============================================================================

static float simulateRawAngleDeg(Phase voltageLine, Phase trueLine, float loadAngleDeg, bool ctReversed) {
    float thetaV = PhaseUtils::phaseAngleDeg(voltageLine);
    float thetaI = PhaseUtils::phaseAngleDeg(trueLine) - loadAngleDeg + (ctReversed ? 180.0f : 0.0f);
    return PhaseUtils::wrapDeg180(thetaV - thetaI);
}

// Power factor exactly as _readMeterValues computes it from the folded angle:
// cos() is non-negative over [-90, 90], so the sign carries inductive (+) vs
// capacitive (-), matching the chip's own convention (datasheet Equation 37).
static float powerFactorFrom(const PhaseUtils::LoadAngle &a) {
    float rad = a.foldedAngleDeg * (float)M_PI / 180.0f;
    return cosf(rad) * (a.foldedAngleDeg >= 0.0f ? 1.0f : -1.0f);
}

static const Phase ROTATIONAL_PHASES[3] = {PHASE_1, PHASE_2, PHASE_3};

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
// loadAngleFromRawDeg - round trip against the physical model
// ============================================================================

void test_correct_assignment_recovers_load_angle(void) {
    // Every voltage tap x every CT line x a spread of realistic load angles: with the
    // channel labelled to match the line the CT is really on, the recovered angle must
    // be the load angle itself, whatever the reference phase happens to be called.
    const float loadAngles[] = {0.0f, 5.0f, 20.0f, 35.5f, 60.0f, 89.0f, -15.0f, -45.0f};

    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            for (unsigned i = 0; i < sizeof(loadAngles) / sizeof(loadAngles[0]); i++) {
                float raw = simulateRawAngleDeg(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], loadAngles[i], false);
                PhaseUtils::LoadAngle a =
                    PhaseUtils::loadAngleFromRawDeg(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], raw);

                TEST_ASSERT_FLOAT_WITHIN(0.01f, loadAngles[i], a.angleDeg);
                TEST_ASSERT_FLOAT_WITHIN(0.01f, loadAngles[i], a.foldedAngleDeg);
                TEST_ASSERT_FALSE(a.activePowerNegative);
            }
        }
    }
}

void test_reversed_ct_keeps_angle_and_flags_negative_power(void) {
    // A backwards CT is a 180 deg flip: the load angle magnitude survives, the power
    // direction does not. This is what the reverse flag / auto-detector then corrects.
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            float raw = simulateRawAngleDeg(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], 25.0f, true);
            PhaseUtils::LoadAngle a =
                PhaseUtils::loadAngleFromRawDeg(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], raw);

            TEST_ASSERT_TRUE(a.activePowerNegative);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, a.foldedAngleDeg);
            TEST_ASSERT_FLOAT_WITHIN(0.001f, cosf(25.0f * (float)M_PI / 180.0f), powerFactorFrom(a));
        }
    }
}

void test_swapped_labels_collapse_power_factor(void) {
    // The failure mode that matters in the field: a resistive load whose two non-
    // reference channels are labelled the wrong way round (sequence reversed) reads
    // |PF| = 0.5 instead of 1.0, i.e. half the power, not a subtle error.
    float raw = simulateRawAngleDeg(PHASE_3, PHASE_1, 0.0f, false); // CT really on L1, voltage on L3

    PhaseUtils::LoadAngle right = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_1, raw);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, powerFactorFrom(right));

    PhaseUtils::LoadAngle wrong = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_2, raw); // mislabelled
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, fabsf(powerFactorFrom(wrong)));
}

void test_cyclic_relabelling_is_indistinguishable(void) {
    // Only relative rotation is physical. Rotating every label by one step (L1L2L3 ->
    // L2L3L1 -> L3L1L2) leaves every measurement identical, which is why an installer
    // does not need to know which wire the utility calls L1 - only the sequence order.
    for (int step = 0; step < 3; step++) {
        Phase base = ROTATIONAL_PHASES[step];
        Phase lag = PhaseUtils::getLaggingPhase(base);
        Phase lead = PhaseUtils::getLeadingPhase(base);

        float rawLag = simulateRawAngleDeg(base, lag, 30.0f, false);
        float rawLead = simulateRawAngleDeg(base, lead, 30.0f, false);

        TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, PhaseUtils::loadAngleFromRawDeg(base, lag, rawLag).angleDeg);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, PhaseUtils::loadAngleFromRawDeg(base, lead, rawLead).angleDeg);
    }
}

void test_fold_always_lands_in_quadrant_and_keeps_cosine_positive(void) {
    // Regression for the single-fold gap in _readMeterValues: raw +155.5 deg with a
    // +120 deg correction reaches 275.5 deg, which one -180 fold leaves at 95.5 deg -
    // a negative cosine multiplied by a positive sign, i.e. a power factor whose sign
    // no longer means inductive/capacitive. Wrapping before folding closes it.
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            for (float raw = -180.0f; raw <= 180.0f; raw += 1.0f) {
                PhaseUtils::LoadAngle a =
                    PhaseUtils::loadAngleFromRawDeg(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], raw);

                TEST_ASSERT_TRUE(a.angleDeg > -180.0f && a.angleDeg <= 180.0f);
                TEST_ASSERT_TRUE(a.foldedAngleDeg >= -90.0f && a.foldedAngleDeg <= 90.0f);
                // Epsilon because cosf(90 deg) rounds to -4.4e-8 in single precision;
                // what matters is that no fold artefact can drive it properly negative.
                TEST_ASSERT_TRUE(cosf(a.foldedAngleDeg * (float)M_PI / 180.0f) >= -1e-6f);
            }
        }
    }
}

void test_split240_base_does_not_break_rotation(void) {
    // A split-phase 240 V reference shares the L1 angle, so a rotational channel still
    // gets a defined +-120 deg correction. (In _readMeterValues this pair currently
    // matches neither the lagging nor the leading branch and fails every read.)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_SPLIT_240, PHASE_2));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, PhaseUtils::calculatePhaseShiftDeg(PHASE_SPLIT_240, PHASE_3));

    float raw = simulateRawAngleDeg(PHASE_1, PHASE_2, 20.0f, false);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, PhaseUtils::loadAngleFromRawDeg(PHASE_SPLIT_240, PHASE_2, raw).angleDeg);
}

// ============================================================================
// Field regression - three-phase cabinet captured 2026-07-25 (device_log.txt)
//
// Voltage tapped on L3, channel 0 = "Rete L3" = PHASE_3, so PHASE_1 is the lagging
// line and PHASE_2 the leading one. The raw ANGLE values below are from the log; the
// expected outputs are what the firmware printed for them. They pin the convention to
// a real installation: with the opposite ANGLE sign convention both mains legs would
// come out capacitive (-15.8 deg and -35.5 deg) while channel 0 - measured through the
// energy registers, which never touch the ANGLE register - independently reported
// +3730 VAR inductive on the same board. That contradiction is what makes this the
// correct convention rather than merely a plausible one.
// ============================================================================

void test_field_rete_l2_leading_leg(void) {
    // "Rete L2 (1) (phase 2): Angle difference: 15.8 deg (from -104.2 deg), PF 96.2%"
    PhaseUtils::LoadAngle a = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_2, -104.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 15.8f, a.foldedAngleDeg);
    TEST_ASSERT_FALSE(a.activePowerNegative);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.962f, powerFactorFrom(a));
}

void test_field_rete_l1_lagging_leg(void) {
    // "Rete L1 (2) (phase 1): Angle difference: 35.5 deg (from 155.5 deg), PF 81.4%"
    PhaseUtils::LoadAngle a = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_1, 155.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 35.5f, a.foldedAngleDeg);
    TEST_ASSERT_FALSE(a.activePowerNegative);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.814f, powerFactorFrom(a));
}

void test_field_reversed_ct_on_correct_phase(void) {
    // "Cella nuova L1 (4) (phase 1): Angle difference: 32.1 deg (from -27.9 deg),
    //  PF 84.7% (negative power)" - configured on the lagging line (L1), CT clipped
    // backwards: a 32.1 deg inductive load read the wrong way round.
    PhaseUtils::LoadAngle a = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_1, -27.9f);
    TEST_ASSERT_TRUE(a.activePowerNegative);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 32.1f, a.foldedAngleDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.847f, powerFactorFrom(a));

    // The third line is ruled out outright: read as the leading line (L2) the same raw
    // value gives 92.1 deg, which is not a load in either orientation (forward is past
    // 90 deg, reversed leaves PF 0.04 on a 3 A circuit). That leaves L1-reversed and
    // L3-forward-capacitive, and a cold room compressor is not capacitive.
    PhaseUtils::LoadAngle asLeading = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_2, -27.9f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 92.1f, asLeading.angleDeg);
    TEST_ASSERT_TRUE(fabsf(powerFactorFrom(asLeading)) < 0.05f);
}

void test_field_old_convention_hid_a_reversed_ct(void) {
    // Same cabinet, raw +87.4 deg on "Cella nuova L2" (channel 6). Under the pre-#188
    // convention this channel took the -120 deg correction and printed -32.6 deg /
    // PF -84.3% / +571 W: a plausible-looking positive load with a nonsense capacitive
    // power factor. Under the current convention it resolves to a 27.4 deg inductive
    // load read backwards, which the reversal detector can then correct.
    PhaseUtils::LoadAngle now = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_2, 87.4f);
    TEST_ASSERT_TRUE(now.activePowerNegative);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 27.4f, now.foldedAngleDeg);

    float oldConvention = 87.4f - 120.0f; // what the swapped lagging/leading map produced
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -32.6f, oldConvention);
}

// ============================================================================
// powersFromFoldedAngle - P and Q must stay a consistent phasor
// ============================================================================

void test_forward_inductive_load_imports_both(void) {
    PhaseUtils::SignedPowers p = PhaseUtils::powersFromFoldedAngle(1000.0f, 30.0f, false, false);
    TEST_ASSERT_TRUE(p.activePower > 0.0f);
    TEST_ASSERT_TRUE(p.reactivePower > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 866.0f, p.activePower);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 500.0f, p.reactivePower);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.866f, p.powerFactor);
}

void test_forward_capacitive_load_has_negative_pf_and_q(void) {
    PhaseUtils::SignedPowers p = PhaseUtils::powersFromFoldedAngle(1000.0f, -30.0f, false, false);
    TEST_ASSERT_TRUE(p.activePower > 0.0f);
    TEST_ASSERT_TRUE(p.reactivePower < 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, -0.866f, p.powerFactor);
}

void test_flipped_current_negates_both_powers(void) {
    // The fix: a backwards CT (or genuine export) is a 180 deg flip of the current
    // phasor, so P and Q must land in the opposite quadrant together. Publishing
    // P < 0 with Q > 0 claims real power leaving while reactive power arrives.
    PhaseUtils::SignedPowers forward = PhaseUtils::powersFromFoldedAngle(1000.0f, 30.0f, false, false);

    PhaseUtils::SignedPowers reversedFlag = PhaseUtils::powersFromFoldedAngle(1000.0f, 30.0f, true, false);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -forward.activePower, reversedFlag.activePower);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -forward.reactivePower, reversedFlag.reactivePower);

    PhaseUtils::SignedPowers negativeBranch = PhaseUtils::powersFromFoldedAngle(1000.0f, 30.0f, false, true);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -forward.activePower, negativeBranch.activePower);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -forward.reactivePower, negativeBranch.reactivePower);

    // Both flips together cancel: a reversed CT on a genuinely exporting circuit reads
    // forward again, which is exactly what the reverse flag is for.
    PhaseUtils::SignedPowers both = PhaseUtils::powersFromFoldedAngle(1000.0f, 30.0f, true, true);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, forward.activePower, both.activePower);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, forward.reactivePower, both.reactivePower);
}

void test_power_factor_sign_is_independent_of_flow_direction(void) {
    // PF sign carries inductive/capacitive, not direction (direction is the sign of P),
    // so flipping the current phasor must not touch it.
    for (int rev = 0; rev < 2; rev++) {
        for (int neg = 0; neg < 2; neg++) {
            PhaseUtils::SignedPowers p =
                PhaseUtils::powersFromFoldedAngle(500.0f, 45.0f, rev != 0, neg != 0);
            TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.707f, p.powerFactor);
        }
    }
}

void test_powers_conserve_apparent_power(void) {
    // P^2 + Q^2 == S^2 across the folded range and every flip combination.
    const float angles[] = {-89.0f, -45.0f, -5.0f, 0.0f, 5.0f, 45.0f, 89.0f};
    for (unsigned i = 0; i < sizeof(angles) / sizeof(angles[0]); i++) {
        for (int rev = 0; rev < 2; rev++) {
            for (int neg = 0; neg < 2; neg++) {
                PhaseUtils::SignedPowers p =
                    PhaseUtils::powersFromFoldedAngle(2400.0f, angles[i], rev != 0, neg != 0);
                float s = sqrtf(p.activePower * p.activePower + p.reactivePower * p.reactivePower);
                TEST_ASSERT_FLOAT_WITHIN(0.5f, 2400.0f, s);
            }
        }
    }
}

void test_field_reversed_ct_publishes_a_real_quadrant(void) {
    // "Cella nuova L1 (4)": raw -27.9 deg on the lagging line resolves to a 32.1 deg
    // inductive load read backwards. Before the fix this published P < 0 with Q > 0.
    PhaseUtils::LoadAngle a = PhaseUtils::loadAngleFromRawDeg(PHASE_3, PHASE_1, -27.9f);
    PhaseUtils::SignedPowers p =
        PhaseUtils::powersFromFoldedAngle(700.0f, a.foldedAngleDeg, false, a.activePowerNegative);
    TEST_ASSERT_TRUE(p.activePower < 0.0f);
    TEST_ASSERT_TRUE(p.reactivePower < 0.0f);
    TEST_ASSERT_TRUE(p.powerFactor > 0.0f); // still inductive
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

    RUN_TEST(test_correct_assignment_recovers_load_angle);
    RUN_TEST(test_reversed_ct_keeps_angle_and_flags_negative_power);
    RUN_TEST(test_swapped_labels_collapse_power_factor);
    RUN_TEST(test_cyclic_relabelling_is_indistinguishable);
    RUN_TEST(test_fold_always_lands_in_quadrant_and_keeps_cosine_positive);
    RUN_TEST(test_split240_base_does_not_break_rotation);

    RUN_TEST(test_field_rete_l2_leading_leg);
    RUN_TEST(test_field_rete_l1_lagging_leg);
    RUN_TEST(test_field_reversed_ct_on_correct_phase);
    RUN_TEST(test_field_old_convention_hid_a_reversed_ct);

    RUN_TEST(test_forward_inductive_load_imports_both);
    RUN_TEST(test_forward_capacitive_load_has_negative_pf_and_q);
    RUN_TEST(test_flipped_current_negates_both_powers);
    RUN_TEST(test_power_factor_sign_is_independent_of_flow_direction);
    RUN_TEST(test_powers_conserve_apparent_power);
    RUN_TEST(test_field_reversed_ct_publishes_a_real_quadrant);

    return UNITY_END();
}
