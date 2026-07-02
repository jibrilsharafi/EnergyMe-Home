// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure per-cycle grid frequency EMA (issue #157).
//
// Free-standing like the other lib/ modules: no Arduino, no FreeRTOS, no SPI,
// no logging, no global state. The caller (src/ade7953.cpp) feeds raw PERIOD
// register reads from the ZXV interrupt path and reads the filtered frequency
// off the hot path; this module owns only the arithmetic, which is what lets
// it be unit-tested on the host (see test/test_grid_frequency).
//
// Filter: first-order EMA on the raw integer PERIOD, Q24.8 fixed point,
// alpha = 1/8 so the update is one arithmetic shift (no multiply, divide or
// float on the per-cycle path). At 50 Hz this is a ~15-cycle (~300 ms) window,
// ~1.06 Hz cutoff, ~0.8 mHz noise floor - the Allan-optimal averaging length
// for the ADE7953's ~11 mHz single-cycle quantization dither. Q8 keeps the
// sub-LSB fraction alive through the shift; a plain-integer EMA would freeze
// on a dithering input and lose exactly the resolution the dither provides.
//
// Conversion (datasheet Eq.36): f = factor / (PERIOD + 1). Applied at readout
// only - filtering happens in the period domain, where the reciprocal's
// nonlinearity bias is sub-uHz for the <1% period swings of a real grid.
namespace GridFrequency {

struct Config {
    float conversionFactorHz; // PERIOD measurement clock, 223.75 kHz on the ADE7953
    float minFrequencyHz;     // validation range; reads outside it never touch the EMA
    float maxFrequencyHz;
};

struct State {
    int32_t emaPeriodQ8 = 0;  // filtered PERIOD * 256 (Q24.8)
    bool seeded = false;      // first in-range read seeds the EMA (never ramps from 0)
    uint32_t updateCount = 0; // monotonic; advances only on accepted updates (freshness signal)
};

// Single-read conversion, Eq.36. Shared by the raw path so both agree on the +1.
// Returns 0.0f for a non-positive period.
float rawFrequencyHz(const Config& cfg, int32_t periodRaw);

// Feed one raw PERIOD read. Returns true if accepted (in range) and the EMA advanced.
bool update(State& state, const Config& cfg, int32_t periodRaw);

// Filtered frequency in Hz. Returns 0.0f until the first accepted update.
float frequencyHz(const State& state, const Config& cfg);

} // namespace GridFrequency
