// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure plausibility check for a real device unix timestamp: no Arduino, no
// globals. src/customtime.cpp wraps this as CustomTime::isUnixTimeValid for
// the rest of the firmware; this is the host-testable source of truth (see
// test/test_unix_time) so the bounds math isn't silently reimplemented
// elsewhere whenever a "does this look like a real timestamp" question comes
// up (e.g. ShadowLogic::isPlausibleStartMeasuringUnixTimeMs).
namespace UnixTime {

constexpr uint64_t MIN_SECONDS = 1000000000ULL;         // 2001-09-09T01:46:40Z
constexpr uint64_t MIN_MILLISECONDS = 1000000000000ULL; // 2001-09-09T01:46:40Z
constexpr uint64_t MAX_SECONDS = 4102444800ULL;         // 2100-01-01T00:00:00Z
constexpr uint64_t MAX_MILLISECONDS = 4102444800000ULL; // 2100-01-01T00:00:00Z

// True if unixTime falls within [MIN, MAX] for the given unit - catches unit
// mistakes (seconds sent as ms, or vice versa) and garbage/overflow values
// that land far outside a plausible device-clock reading either way.
bool isValid(uint64_t unixTime, bool isMilliseconds = true);

// Hourly energy rows are stamped with a UTC hour, so the save is scheduled on UTC
// hour boundaries: on local ones, a :30/:45-offset timezone would save mid-UTC-hour
// and the rounding would shift readings by up to half an hour.
constexpr uint64_t MS_PER_HOUR = 3600000ULL;

uint64_t millisUntilNextUtcHour(uint64_t unixMs);
uint64_t millisFromNearestUtcHour(uint64_t unixMs);
// Rounded as a whole instant, so 23:59:30 becomes 00:00 of the next day (not "24:00")
uint64_t nearestUtcHourSeconds(uint64_t unixSeconds);

} // namespace UnixTime
