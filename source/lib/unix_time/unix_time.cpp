// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "unix_time.h"

namespace UnixTime {

bool isValid(uint64_t unixTime, bool isMilliseconds) {
    if (isMilliseconds) {
        return unixTime >= MIN_MILLISECONDS && unixTime <= MAX_MILLISECONDS;
    }
    return unixTime >= MIN_SECONDS && unixTime <= MAX_SECONDS;
}

uint64_t millisUntilNextUtcHour(uint64_t unixMs) {
    return MS_PER_HOUR - (unixMs % MS_PER_HOUR);
}

uint64_t millisFromNearestUtcHour(uint64_t unixMs) {
    uint64_t sinceHour = unixMs % MS_PER_HOUR;
    uint64_t untilHour = MS_PER_HOUR - sinceHour;
    return sinceHour < untilHour ? sinceHour : untilHour;
}

uint64_t nearestUtcHourSeconds(uint64_t unixSeconds) {
    return (unixSeconds + 1800ULL) / 3600ULL * 3600ULL;
}

} // namespace UnixTime
