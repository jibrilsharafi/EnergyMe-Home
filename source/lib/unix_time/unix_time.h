// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure plausibility check for a real device unix timestamp: no Arduino, no
// globals. src/customtime.cpp wraps this as CustomTime::isUnixTimeValid for
// the rest of the firmware; this is the host-testable source of truth (see
// test/test_unix_time) so the bounds math isn't silently reimplemented
// elsewhere a "does this look like a real timestamp" question comes up (e.g.
// ShadowLogic::isPlausibleStartMeasuringUnixTimeMs).
namespace UnixTime {

constexpr uint64_t MIN_SECONDS = 1000000000ULL;         // 2001-01-01T00:00:00Z
constexpr uint64_t MIN_MILLISECONDS = 1000000000000ULL; // 2001-01-01T00:00:00Z
constexpr uint64_t MAX_SECONDS = 4102444800ULL;         // 2100-01-01T00:00:00Z
constexpr uint64_t MAX_MILLISECONDS = 4102444800000ULL; // 2100-01-01T00:00:00Z

// True if unixTime falls within [MIN, MAX] for the given unit - catches unit
// mistakes (seconds sent as ms, or vice versa) and garbage/overflow values
// that land far outside a plausible device-clock reading either way.
bool isValid(uint64_t unixTime, bool isMilliseconds = true);

} // namespace UnixTime
