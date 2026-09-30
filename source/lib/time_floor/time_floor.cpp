// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "time_floor.h"

#include "unix_time.h"

namespace TimeFloor {

uint32_t toFloor(uint64_t unixSeconds) {
    return UnixTime::isValid(unixSeconds, false) ? static_cast<uint32_t>(unixSeconds) : 0;
}

uint32_t effective(uint64_t buildFloor, uint64_t persistedFloor) {
    uint32_t build = toFloor(buildFloor);
    uint32_t persisted = toFloor(persistedFloor);
    return build > persisted ? build : persisted;
}

bool accepts(uint32_t floor, uint64_t candidateSeconds, uint32_t toleranceSeconds) {
    if (floor == 0) return true;
    return candidateSeconds + toleranceSeconds >= floor;
}

uint32_t raisedBy(uint32_t floor, uint64_t acceptedSeconds) {
    uint32_t candidate = toFloor(acceptedSeconds);
    return candidate > floor ? candidate : floor;
}

} // namespace TimeFloor
