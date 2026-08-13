// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free exponential backoff schedule.
//
// The delay for attempt N is `initialInterval * multiplier^(N-1)`, clamped to
// `maxInterval`. Attempt 0 has no delay, so the first attempt of any retry loop
// runs immediately and `delayForAttempt(N)` is the wait *before* attempt N+1.
//
// The result is clamped, never wrapped: the scaled value is checked against the
// ceiling before each multiplication (and before the shift on the power-of-two
// fast path), so an attempt number or interval large enough to overflow a
// uint64_t returns `maxInterval` rather than a wrapped-around small delay. A
// wrapped value would silently turn a long backoff into a tight retry loop.
//
// A `multiplier` below 2 yields a constant `initialInterval` on every attempt.
// This also defines away the division by zero that a literal 0 multiplier would
// otherwise reach on the general path.

namespace BackoffSchedule {

// Returns the delay before the attempt following `attempt`, in whatever time
// unit the caller uses for `initialInterval` and `maxInterval`.
uint64_t delayForAttempt(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval,
                         uint64_t multiplier);

}  // namespace BackoffSchedule
