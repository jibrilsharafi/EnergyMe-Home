// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "backoff_schedule.h"

namespace BackoffSchedule {

namespace {

constexpr uint64_t UINT64_BITS = 64;

uint64_t clampToMax(uint64_t value, uint64_t maxInterval) {
    return value < maxInterval ? value : maxInterval;
}

}  // namespace

uint64_t delayForAttempt(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval,
                         uint64_t multiplier) {
    if (attempt == 0) return 0;
    if (multiplier <= 1) return clampToMax(initialInterval, maxInterval);

    const uint64_t exponent = attempt - 1;

    // Power-of-two fast path: shifting beats the loop, and comparing against
    // `maxInterval >> exponent` rules out both an over-cap result and an
    // overflowing shift in one test.
    if (multiplier == 2) {
        if (exponent >= UINT64_BITS) return maxInterval;
        if (initialInterval > (maxInterval >> exponent)) return maxInterval;
        return initialInterval << exponent;
    }

    uint64_t delay = initialInterval;
    for (uint64_t i = 0; i < exponent; ++i) {
        if (delay > maxInterval / multiplier) return maxInterval;
        delay *= multiplier;
    }

    return clampToMax(delay, maxInterval);
}

}  // namespace BackoffSchedule
