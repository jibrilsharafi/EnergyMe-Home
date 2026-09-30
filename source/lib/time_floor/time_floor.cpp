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

bool corroborates(uint32_t baseWallSeconds, uint32_t baseUptimeSeconds,
                  uint64_t wallSeconds, uint32_t uptimeSeconds, uint32_t toleranceSeconds) {
    if (baseWallSeconds == 0 || toFloor(wallSeconds) == 0) return false;
    if (uptimeSeconds < baseUptimeSeconds) return false;

    int64_t wallElapsed = static_cast<int64_t>(wallSeconds) - static_cast<int64_t>(baseWallSeconds);
    int64_t uptimeElapsed = static_cast<int64_t>(uptimeSeconds - baseUptimeSeconds);
    int64_t skew = wallElapsed - uptimeElapsed;
    if (skew < 0) skew = -skew;
    return skew <= static_cast<int64_t>(toleranceSeconds);
}

uint32_t raisedBy(uint32_t floor, uint64_t acceptedSeconds) {
    uint32_t candidate = toFloor(acceptedSeconds);
    return candidate > floor ? candidate : floor;
}

uint32_t manualFloor(uint32_t floor, uint64_t manualSeconds) {
    uint32_t manual = toFloor(manualSeconds);
    if (manual == 0) return floor;
    return manual < floor ? manual : floor;
}

bool isZeroTransmitArtifact(uint64_t candidateSeconds) {
    return candidateSeconds == ZERO_TRANSMIT_SECONDS;
}

AnswerOutcome onAnswer(uint32_t floor, uint32_t baseWallSeconds, uint32_t baseUptimeSeconds,
                       uint32_t uptimeSeconds, uint64_t candidateSeconds, uint32_t toleranceSeconds) {
    if (isZeroTransmitArtifact(candidateSeconds) || !accepts(floor, candidateSeconds, toleranceSeconds)) {
        return {false, floor};
    }
    if (!corroborates(baseWallSeconds, baseUptimeSeconds, candidateSeconds, uptimeSeconds, toleranceSeconds)) {
        return {true, floor};
    }
    return {true, raisedBy(floor, candidateSeconds)};
}

} // namespace TimeFloor
