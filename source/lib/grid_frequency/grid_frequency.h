// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure per-cycle grid frequency EMA (issue #157). Free-standing like the other
// lib/ modules (no Arduino/FreeRTOS/SPI/logging) so it is host-testable.
//
// First-order EMA on the raw integer PERIOD register, Q24.8 fixed point,
// alpha = 1/8 (one arithmetic shift per update, no float on the hot path).
// A ~15-line-cycle window: the Allan-optimal averaging length for the chip's
// single-cycle quantization dither. The Q8 fraction is what keeps sub-LSB
// resolution alive - a plain-integer EMA would freeze on a dithering input.
//
// Conversion (datasheet Eq.36): f = factor / (PERIOD + 1), applied at readout
// only; filtering stays in the period domain where the reciprocal bias is
// negligible.
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
