// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

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

// Returns the degrees to shift the voltage waveform to align it with the current's
// phase reference. Formula: currentAngle - voltageAngle.
// Positive = current leads voltage reference; negative = current lags.
inline float calculatePhaseShiftDeg(Phase voltagePhase, Phase currentPhase) {
    auto toAngle = [](Phase p) -> float {
        switch (p) {
            case PHASE_1: return 0.0f;
            case PHASE_2: return -120.0f;
            case PHASE_3: return 120.0f;
            case PHASE_SPLIT_240: return 0.0f;
            default: return 0.0f;
        }
    };
    return toAngle(currentPhase) - toAngle(voltagePhase);
}

} // namespace PhaseUtils
