// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>
#include <cmath>

// Phase enum and pure phase-rotation helpers, deliberately free of any Arduino /
// FreeRTOS dependency so the logic can be unit-tested on the host (see
// test/test_phase_utils). ade7953.h includes this header for the Phase type.
//
// Convention (IEC positive sequence, European 3-phase):
//   PHASE_1 = L1  0°      (reference)
//   PHASE_2 = L2  -120°   (lags L1 by 120°)
//   PHASE_3 = L3  +120°   (leads L1 by 120°)

enum Phase : uint32_t { // plain enum so it serializes directly to/from JSON as an integer
    PHASE_1       = 1,
    PHASE_2       = 2,
    PHASE_3       = 3,
    PHASE_SPLIT_240 = 4, // North America split-phase 240 V (L1-L2), auto-applies 2x multiplier
};

namespace PhaseUtils {

// Arduino's PI / DEG_TO_RAD macros do not exist on the host, and this header stays
// Arduino-free (see the note above), so carry the conversion locally.
constexpr float DEG_TO_RAD_F = 0.01745329251994329577f;

// Returns the phase 120° behind `phase` in IEC rotation (L1->L2->L3->L1).
inline Phase getLaggingPhase(Phase phase) {
    switch (phase) {
        case PHASE_1: return PHASE_2;
        case PHASE_2: return PHASE_3;
        case PHASE_3: return PHASE_1;
        case PHASE_SPLIT_240: return PHASE_SPLIT_240;
        default: return PHASE_1;
    }
}

// Returns the phase 120° ahead of `phase` in IEC rotation (L1->L3->L2->L1).
inline Phase getLeadingPhase(Phase phase) {
    switch (phase) {
        case PHASE_1: return PHASE_3;
        case PHASE_2: return PHASE_1;
        case PHASE_3: return PHASE_2;
        case PHASE_SPLIT_240: return PHASE_SPLIT_240;
        default: return PHASE_1;
    }
}

// Angle of a phase in the IEC positive-sequence frame. Split-phase 240 V shares the
// L1 reference angle (it is a 180 deg sign flip, not a rotation).
inline float phaseAngleDeg(Phase phase) {
    switch (phase) {
        case PHASE_1: return 0.0f;
        case PHASE_2: return -120.0f;
        case PHASE_3: return 120.0f;
        case PHASE_SPLIT_240: return 0.0f;
        default: return 0.0f;
    }
}

// Returns the degrees to shift the voltage waveform to align it with the current's
// phase reference. Formula: currentAngle - voltageAngle.
// Positive = current leads voltage reference; negative = current lags.
inline float calculatePhaseShiftDeg(Phase voltagePhase, Phase currentPhase) {
    return phaseAngleDeg(currentPhase) - phaseAngleDeg(voltagePhase);
}

// Wraps an angle into (-180, 180].
inline float wrapDeg180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// Load angle recovered from the ADE7953 ANGLE register on a channel whose CT sits on
// a different line than the single voltage input.
//
// Sign conventions (datasheet Rev. C, Figure 60 "Current-to-Voltage Time Delay"): the
// ANGLE register is the delay from the voltage negative-to-positive zero crossing to
// the current one, i.e. rawAngleDeg = thetaV - thetaI. Positive therefore means the
// current LAGS the voltage: an inductive load. Confirmed against field data (see
// test_phase_utils).
//
//   rawAngleDeg = (baseAngle - channelAngle) + phi
//   => phi      = rawAngleDeg + (channelAngle - baseAngle)
//
// which is exactly rawAngleDeg + calculatePhaseShiftDeg(basePhase, channelPhase), so
// the same expression covers the same-phase case (shift 0) and both rotations without
// a lagging/leading branch.
struct LoadAngle {
    float angleDeg;            // phi wrapped into (-180, 180]
    float foldedAngleDeg;      // phi folded into [-90, 90], the argument of the PF cosine
    bool activePowerNegative;  // true when the fold crossed +-90 (power flows the other way)
};

inline LoadAngle loadAngleFromRawDeg(Phase basePhase, Phase channelPhase, float rawAngleDeg) {
    LoadAngle result{};
    result.angleDeg = wrapDeg180(rawAngleDeg + calculatePhaseShiftDeg(basePhase, channelPhase));

    // A single fold always lands inside [-90, 90] because the input is already wrapped,
    // which keeps cos() non-negative so the power factor sign means inductive/capacitive
    // (never a folding artefact).
    result.foldedAngleDeg = result.angleDeg;
    result.activePowerNegative = false;
    if (result.foldedAngleDeg < -90.0f || result.foldedAngleDeg > 90.0f) {
        result.foldedAngleDeg += (result.foldedAngleDeg > 0.0f ? -180.0f : 180.0f);
        result.activePowerNegative = true;
    }
    return result;
}

// Signed powers reconstructed from the recovered load angle (the ANGLE method) for a
// channel whose CT sits on a different line than the single voltage input. The
// firmware now uses toChannelFrame; this remains for the field A/B log and as the
// reference toChannelFrame must match on a sine.
//
// A backwards CT and a genuine reverse flow are both a 180 deg flip of the current
// phasor, so they negate BOTH P and Q. Flipping only P publishes an impossible
// quadrant (real power leaving while reactive power arrives) and makes any consumer
// that reconstructs the angle from (P, Q) - the installer phase preview does exactly
// that - land 180 deg out.
struct SignedPowers {
    float activePower;
    float reactivePower;
    float powerFactor;
};

inline SignedPowers powersFromFoldedAngle(float apparentPower, float foldedAngleDeg,
                                          bool ctReversed, bool activePowerNegative) {
    const float rad = foldedAngleDeg * DEG_TO_RAD_F;
    const float flip = (ctReversed ? -1.0f : 1.0f) * (activePowerNegative ? -1.0f : 1.0f);

    SignedPowers powers{};
    // cos() is non-negative across the folded [-90, 90] range, so the power factor sign
    // is the load angle sign: positive inductive, negative capacitive (datasheet Eq. 37).
    powers.powerFactor = std::cos(rad) * (foldedAngleDeg >= 0.0f ? 1.0f : -1.0f);
    // fabs keeps the active power identical to what the magnitude-times-sign form
    // produced; it only differs from cos() outside the folded range, which is
    // unreachable for an angle that went through loadAngleFromRawDeg.
    powers.activePower = apparentPower * std::fabs(std::cos(rad)) * flip;
    powers.reactivePower = apparentPower * std::sin(rad) * flip;
    return powers;
}

// True for a channel whose CT sits on another line of a three-phase supply than the
// single voltage input. Split-phase 240 V is never off-phase: phaseAngleDeg gives it the
// L1 angle, which against an L2/L3 base would rotate it by +-120.
inline bool isOffPhase(Phase basePhase, Phase channelPhase) {
    return channelPhase != PHASE_SPLIT_240 && channelPhase != basePhase;
}

struct ActiveReactive {
    float active;
    float reactive;
};

// One latched window (active, reactive energy as read, LSB-scaled) in the channel's own
// line frame, as _readMeterValues publishes it. `reverse` negates both: a backwards CT is
// a 180 deg flip of the current phasor. An off-phase channel is integrated against V1 as
// P1 + jQ1 = V1 * I^*; assuming V_k = V1 * e^{j*shift} (same magnitude, exact 120 deg
// spacing), its own S_k = e^{j*shift} * (P1 + jQ1), with shift =
// calculatePhaseShiftDeg(basePhase, channelPhase), the same correction loadAngleFromRawDeg
// adds to the ANGLE register, so on a pure sine both methods agree exactly. The rotation
// is linear, so it applies to energies and powers alike.
inline ActiveReactive toChannelFrame(float active, float reactive, bool reverse,
                                     Phase basePhase, Phase channelPhase) {
    const float sign = reverse ? -1.0f : 1.0f;
    ActiveReactive out{active * sign, reactive * sign};
    if (!isOffPhase(basePhase, channelPhase)) return out;

    const float a = calculatePhaseShiftDeg(basePhase, channelPhase) * DEG_TO_RAD_F;
    const float c = std::cos(a);
    const float s = std::sin(a);
    return ActiveReactive{out.active * c - out.reactive * s, out.reactive * c + out.active * s};
}

} // namespace PhaseUtils
