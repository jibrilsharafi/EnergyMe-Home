// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "grid_frequency.h"

namespace GridFrequency {

float rawFrequencyHz(const Config& cfg, int32_t periodRaw) {
    if (periodRaw <= 0) return 0.0f;
    return cfg.conversionFactorHz / (float(periodRaw) + 1.0f);
}

bool update(State& state, const Config& cfg, int32_t periodRaw) {
    float frequency = rawFrequencyHz(cfg, periodRaw);
    if (frequency < cfg.minFrequencyHz || frequency > cfg.maxFrequencyHz) return false;

    int32_t xQ8 = periodRaw << 8;
    if (!state.seeded) {
        state.emaPeriodQ8 = xQ8;
        state.seeded = true;
    } else {
        // ema += alpha * (x - ema), alpha = 1/8; the shift keeps the Q8 fraction
        state.emaPeriodQ8 += (xQ8 - state.emaPeriodQ8) >> 3;
    }
    state.updateCount++;
    return true;
}

float frequencyHz(const State& state, const Config& cfg) {
    if (!state.seeded) return 0.0f;
    return cfg.conversionFactorHz / (float(state.emaPeriodQ8) / 256.0f + 1.0f);
}

} // namespace GridFrequency
