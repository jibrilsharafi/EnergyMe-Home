// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for PhaseUtils phase-rotation helpers. Run with:  pio test -e native
// (On Windows the native toolchain is unreliable - run it from WSL.)

#include <unity.h>
#include <math.h>
#include <stdio.h>
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

// Power factor exactly as the ANGLE method (powersFromFoldedAngle) computes it:
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
// isOffPhase / toChannelFrame - identities
// ============================================================================

static const float SQRT3_2 = 0.8660254f;

void test_off_phase_only_for_another_rotational_line(void) {
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL(v != c, PhaseUtils::isOffPhase(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c]));
        }
        TEST_ASSERT_FALSE(PhaseUtils::isOffPhase(ROTATIONAL_PHASES[v], PHASE_SPLIT_240));
    }
}

void test_frame_same_phase_is_not_rotated(void) {
    // Channel 0 and every channel on its line take this path: exact, not merely close.
    for (int v = 0; v < 3; v++) {
        PhaseUtils::ActiveReactive r =
            PhaseUtils::toChannelFrame(800.0f, 300.0f, false, ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[v]);
        TEST_ASSERT_EQUAL_FLOAT(800.0f, r.active);
        TEST_ASSERT_EQUAL_FLOAT(300.0f, r.reactive);
    }
}

void test_frame_reverse_negates_both_on_same_phase(void) {
    PhaseUtils::ActiveReactive r = PhaseUtils::toChannelFrame(800.0f, 300.0f, true, PHASE_1, PHASE_1);
    TEST_ASSERT_EQUAL_FLOAT(-800.0f, r.active);
    TEST_ASSERT_EQUAL_FLOAT(-300.0f, r.reactive);
}

void test_frame_split240_is_never_rotated(void) {
    // Split-phase 240 V shares the L1 angle, so calculatePhaseShiftDeg would rotate it by
    // +-120 against an L2/L3 base: only the reverse flag may touch it.
    const Phase bases[] = {PHASE_1, PHASE_2, PHASE_3, PHASE_SPLIT_240};
    for (unsigned b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
        for (int rev = 0; rev < 2; rev++) {
            const float sign = rev ? -1.0f : 1.0f;
            PhaseUtils::ActiveReactive r =
                PhaseUtils::toChannelFrame(800.0f, 300.0f, rev != 0, bases[b], PHASE_SPLIT_240);
            TEST_ASSERT_EQUAL_FLOAT(sign * 800.0f, r.active);
            TEST_ASSERT_EQUAL_FLOAT(sign * 300.0f, r.reactive);
        }
    }
}

void test_frame_l2_identity(void) {
    // L2 against an L1 reference: shift -120 deg.
    const float p1 = 800.0f, q1 = 300.0f;
    PhaseUtils::ActiveReactive r = PhaseUtils::toChannelFrame(p1, q1, false, PHASE_1, PHASE_2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f * p1 + SQRT3_2 * q1, r.active);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f * q1 - SQRT3_2 * p1, r.reactive);
}

void test_frame_l3_identity(void) {
    // L3 against an L1 reference: shift +120 deg.
    const float p1 = 800.0f, q1 = 300.0f;
    PhaseUtils::ActiveReactive r = PhaseUtils::toChannelFrame(p1, q1, false, PHASE_1, PHASE_3);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f * p1 - SQRT3_2 * q1, r.active);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f * q1 + SQRT3_2 * p1, r.reactive);
}

void test_frame_reverse_negates_the_rotated_pair(void) {
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            if (c == v) continue;
            PhaseUtils::ActiveReactive forward =
                PhaseUtils::toChannelFrame(800.0f, 300.0f, false, ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c]);
            PhaseUtils::ActiveReactive reversed =
                PhaseUtils::toChannelFrame(800.0f, 300.0f, true, ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c]);
            TEST_ASSERT_EQUAL_FLOAT(-forward.active, reversed.active);
            TEST_ASSERT_EQUAL_FLOAT(-forward.reactive, reversed.reactive);
        }
    }
}

void test_frame_resistive_l2_load_recovers_full_power(void) {
    // A 2300 W kettle on L2 integrates against V1 as P1 = -S/2, Q1 = +S*sqrt3/2: the
    // chip alone would book it as a half-power export. The rotation puts it back.
    PhaseUtils::ActiveReactive r =
        PhaseUtils::toChannelFrame(-1150.0f, 2300.0f * SQRT3_2, false, PHASE_1, PHASE_2);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2300.0f, r.active);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, r.reactive);
}

// ============================================================================
// toChannelFrame - time-domain model of one latched window
//
// v1 is the single voltage input, vk the line the CT really sits on, i the current.
// The chip integrates P1 = mean(v1 * i) and Q1 = mean(v1(t - T/4) * i): an ideal 90 deg
// reactive path, which gives +V*I*sin(phi) for a lagging current as the datasheet
// defines it (Rev. C, "Sign of Reactive Power Calculation"). The reference is what
// the circuit really exchanges on its own line, mean(vk * i). Integer cycles with
// uniform sampling make the 2w ripple average out exactly, as the latched window does.
// The firmware derives PF from these P and the apparent energy, so P and Q are what
// the helper has to get right.
// ============================================================================

static const double SIM_V_RMS = 230.0;
static const double SIM_I_RMS = 10.0;
static const int SIM_CYCLES = 10;
static const int SIM_SAMPLES_PER_CYCLE = 500;
static const float SIM_TOLERANCE = 0.0005f; // 0.05%

static const float PF_087_DEG = 29.541f; // acos(0.87)
static const float SINE_LOAD_ANGLES[] = {0.0f, PF_087_DEG, -PF_087_DEG, 60.0f, -60.0f};
static const unsigned SINE_LOAD_ANGLE_COUNT = sizeof(SINE_LOAD_ANGLES) / sizeof(SINE_LOAD_ANGLES[0]);

struct SimLoad {
    Phase trueLine;        // line the CT is physically clamped on
    float loadAngleDeg;    // current lags its own line voltage by this (negative: leads)
    bool exporting;        // the circuit delivers power: current phasor flipped
    bool ctBackwards;      // CT clamped the wrong way round: the chip sees -i
    double harmonic3;      // 3rd and 5th harmonic current, relative to the fundamental
    double harmonic5;
};

struct SimWindow {
    float p1;        // what the chip integrates against V1, before the reverse flag
    float q1;
    float apparent;  // VRMS * IRMS, the chip's apparent energy path
    double pTrue;    // what the circuit really exchanges on its own line
    double qTrue;
};

static SimLoad sineLoad(Phase trueLine, float loadAngleDeg) {
    return SimLoad{trueLine, loadAngleDeg, false, false, 0.0, 0.0};
}

static SimWindow simulateWindow(Phase voltageLine, const SimLoad &load) {
    const double degToRad = M_PI / 180.0;
    const double thetaV1 = PhaseUtils::phaseAngleDeg(voltageLine) * degToRad;
    const double thetaK = PhaseUtils::phaseAngleDeg(load.trueLine) * degToRad;
    const double phi = load.loadAngleDeg * degToRad;
    const double vPeak = sqrt(2.0) * SIM_V_RMS;
    const double iPeak = sqrt(2.0) * SIM_I_RMS * (load.exporting ? -1.0 : 1.0);
    const int samples = SIM_CYCLES * SIM_SAMPLES_PER_CYCLE;

    double p1 = 0.0, q1 = 0.0, pTrue = 0.0, qTrue = 0.0, iSquared = 0.0;
    for (int s = 0; s < samples; s++) {
        double wt = 2.0 * M_PI * s / SIM_SAMPLES_PER_CYCLE;
        double x = wt + thetaK - phi;
        double iPhysical = iPeak * (cos(x) + load.harmonic3 * cos(3.0 * x) + load.harmonic5 * cos(5.0 * x));
        double iChip = load.ctBackwards ? -iPhysical : iPhysical;

        p1 += vPeak * cos(wt + thetaV1) * iChip;
        q1 += vPeak * cos(wt + thetaV1 - M_PI / 2.0) * iChip;
        pTrue += vPeak * cos(wt + thetaK) * iPhysical;
        qTrue += vPeak * cos(wt + thetaK - M_PI / 2.0) * iPhysical;
        iSquared += iChip * iChip;
    }

    SimWindow w{};
    w.p1 = (float)(p1 / samples);
    w.q1 = (float)(q1 / samples);
    w.apparent = (float)(SIM_V_RMS * sqrt(iSquared / samples));
    w.pTrue = pTrue / samples;
    w.qTrue = qTrue / samples;
    return w;
}

static PhaseUtils::ActiveReactive rotateWindow(Phase basePhase, Phase configuredPhase, const SimWindow &w,
                                               bool reverse) {
    return PhaseUtils::toChannelFrame(w.p1, w.q1, reverse, basePhase, configuredPhase);
}

static void assertMatchesTruth(const SimWindow &w, const PhaseUtils::ActiveReactive &r, const char *what) {
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SIM_TOLERANCE * (float)fabs(w.pTrue), (float)w.pTrue, r.active, what);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(SIM_TOLERANCE * w.apparent, (float)w.qTrue, r.reactive, what);
}

void test_rotate_matches_true_power_on_sine_loads(void) {
    // Every voltage tap x every CT line x PF 1 / 0.87 / 0.5 leading and lagging, with
    // the CT forward and clamped backwards (reverse flag set to match).
    char what[96];
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            for (unsigned a = 0; a < SINE_LOAD_ANGLE_COUNT; a++) {
                for (int backwards = 0; backwards < 2; backwards++) {
                    SimLoad load = sineLoad(ROTATIONAL_PHASES[c], SINE_LOAD_ANGLES[a]);
                    load.ctBackwards = backwards != 0;
                    SimWindow w = simulateWindow(ROTATIONAL_PHASES[v], load);

                    snprintf(what, sizeof(what), "base L%d, load L%d, phi %.1f, ct backwards %d",
                             v + 1, c + 1, (double)SINE_LOAD_ANGLES[a], backwards);
                    assertMatchesTruth(
                        w, rotateWindow(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], w, load.ctBackwards), what);
                }
            }
        }
    }
}

void test_rotate_books_genuine_export_as_negative(void) {
    // Regression: L2/L3 export was once booked as import (seen in prod on a 3-phase
    // site). The accumulators take the direction from the signed energy of the window,
    // so a circuit really delivering power must come out negative on every line.
    char what[96];
    for (int v = 0; v < 3; v++) {
        for (int c = 0; c < 3; c++) {
            SimLoad load = sineLoad(ROTATIONAL_PHASES[c], 10.0f);
            load.exporting = true;
            SimWindow w = simulateWindow(ROTATIONAL_PHASES[v], load);
            PhaseUtils::ActiveReactive r = rotateWindow(ROTATIONAL_PHASES[v], ROTATIONAL_PHASES[c], w, false);

            snprintf(what, sizeof(what), "base L%d, exporting L%d", v + 1, c + 1);
            TEST_ASSERT_TRUE_MESSAGE(r.active < 0.0f, what);
            assertMatchesTruth(w, r, what);
        }
    }
}

void test_rotate_ignores_harmonic_current(void) {
    // Capacitor-input rectifier-like current (30% 3rd, 20% 5th) on a clean sine
    // voltage: harmonic current carries no active power, and both P1 and Q1 are
    // integrated against the sine V1, so the rotation stays exact. Irms times the
    // displacement cosine - what the ANGLE method amounts to - overstates it.
    for (int c = 1; c < 3; c++) {
        SimLoad load = sineLoad(ROTATIONAL_PHASES[c], 20.0f);
        load.harmonic3 = 0.3;
        load.harmonic5 = 0.2;
        SimWindow w = simulateWindow(PHASE_1, load);
        PhaseUtils::ActiveReactive r = rotateWindow(PHASE_1, ROTATIONAL_PHASES[c], w, false);

        assertMatchesTruth(w, r, "harmonic current");
        float displacementEstimate = w.apparent * cosf(20.0f * (float)M_PI / 180.0f);
        TEST_ASSERT_TRUE(displacementEstimate > 1.05f * (float)w.pTrue);
    }
}

// ============================================================================
// toChannelFrame vs the ANGLE method - identical on a pure sine
//
// Both apply the same calculatePhaseShiftDeg correction, so on a sine they must agree
// for every configuration, including wrong ones: a misconfiguration stays exactly as
// visible (PF, installer preview) as it was. S is the same for both, so equal P and Q
// give the firmware's P/S power factor equal too.
// ============================================================================

void test_rotate_agrees_with_angle_method_on_sine(void) {
    char what[128];
    for (int v = 0; v < 3; v++) {
        for (int line = 0; line < 3; line++) {
            for (int cfg = 0; cfg < 3; cfg++) {
                for (unsigned a = 0; a < SINE_LOAD_ANGLE_COUNT; a++) {
                    for (int backwards = 0; backwards < 2; backwards++) {
                        for (int rev = 0; rev < 2; rev++) {
                            Phase base = ROTATIONAL_PHASES[v];
                            Phase configured = ROTATIONAL_PHASES[cfg];
                            SimLoad load = sineLoad(ROTATIONAL_PHASES[line], SINE_LOAD_ANGLES[a]);
                            load.ctBackwards = backwards != 0;
                            SimWindow w = simulateWindow(base, load);

                            PhaseUtils::ActiveReactive rotated = rotateWindow(base, configured, w, rev != 0);

                            float raw = simulateRawAngleDeg(base, load.trueLine, load.loadAngleDeg, load.ctBackwards);
                            PhaseUtils::LoadAngle angle = PhaseUtils::loadAngleFromRawDeg(base, configured, raw);
                            PhaseUtils::SignedPowers legacy = PhaseUtils::powersFromFoldedAngle(
                                w.apparent, angle.foldedAngleDeg, rev != 0, angle.activePowerNegative);

                            snprintf(what, sizeof(what), "base L%d, load L%d as L%d, phi %.1f, backwards %d, reverse %d",
                                     v + 1, line + 1, cfg + 1, (double)SINE_LOAD_ANGLES[a], backwards, rev);
                            float tolerance = SIM_TOLERANCE * w.apparent;
                            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance, legacy.activePower, rotated.active, what);
                            TEST_ASSERT_FLOAT_WITHIN_MESSAGE(tolerance, legacy.reactivePower, rotated.reactive, what);
                        }
                    }
                }
            }
        }
    }
}

void test_rotate_misconfiguration_stays_detectable(void) {
    // Load really on L1 (the voltage tap) but configured as L3, with reverse set. A PF
    // 0.72 load then reads S*cos(phi - 60) = +34% over the truth under both methods:
    // the rotation neither hides nor worsens a wrong phase setting.
    const float phi = 44.0f;
    SimLoad load = sineLoad(PHASE_1, phi);
    SimWindow w = simulateWindow(PHASE_1, load);

    PhaseUtils::ActiveReactive rotated = rotateWindow(PHASE_1, PHASE_3, w, true);
    float raw = simulateRawAngleDeg(PHASE_1, PHASE_1, phi, false);
    PhaseUtils::LoadAngle angle = PhaseUtils::loadAngleFromRawDeg(PHASE_1, PHASE_3, raw);
    PhaseUtils::SignedPowers legacy =
        PhaseUtils::powersFromFoldedAngle(w.apparent, angle.foldedAngleDeg, true, angle.activePowerNegative);

    float expected = w.apparent * cosf((phi - 60.0f) * (float)M_PI / 180.0f);
    TEST_ASSERT_FLOAT_WITHIN(SIM_TOLERANCE * w.apparent, expected, rotated.active);
    TEST_ASSERT_FLOAT_WITHIN(SIM_TOLERANCE * w.apparent, legacy.activePower, rotated.active);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.336f, rotated.active / (float)w.pTrue);
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

    RUN_TEST(test_off_phase_only_for_another_rotational_line);
    RUN_TEST(test_frame_same_phase_is_not_rotated);
    RUN_TEST(test_frame_reverse_negates_both_on_same_phase);
    RUN_TEST(test_frame_split240_is_never_rotated);
    RUN_TEST(test_frame_l2_identity);
    RUN_TEST(test_frame_l3_identity);
    RUN_TEST(test_frame_reverse_negates_the_rotated_pair);
    RUN_TEST(test_frame_resistive_l2_load_recovers_full_power);
    RUN_TEST(test_rotate_matches_true_power_on_sine_loads);
    RUN_TEST(test_rotate_books_genuine_export_as_negative);
    RUN_TEST(test_rotate_ignores_harmonic_current);
    RUN_TEST(test_rotate_agrees_with_angle_method_on_sine);
    RUN_TEST(test_rotate_misconfiguration_stays_detectable);

    return UNITY_END();
}
