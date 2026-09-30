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

uint32_t ceiling(uint32_t anchorSeconds, uint32_t uptimeSeconds) {
    if (anchorSeconds == 0) return 0;
    uint64_t sum = static_cast<uint64_t>(anchorSeconds) + uptimeSeconds;
    return sum > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(sum);
}

bool isZeroTransmitArtifact(uint64_t candidateSeconds) {
    return candidateSeconds == ZERO_TRANSMIT_SECONDS;
}

AnswerOutcome onAnswer(uint32_t floor, uint32_t anchorSeconds, uint32_t uptimeSeconds,
                       uint64_t candidateSeconds, uint32_t toleranceSeconds) {
    // Past UnixTime::MAX_SECONDS too: lwIP decodes era-1 NTP seconds up to 2104
    if (isZeroTransmitArtifact(candidateSeconds) || !UnixTime::isValid(candidateSeconds, false) ||
        !accepts(floor, candidateSeconds, toleranceSeconds)) {
        return {false, floor};
    }
    uint32_t cap = ceiling(anchorSeconds, uptimeSeconds);
    return {true, raisedBy(floor, candidateSeconds < cap ? candidateSeconds : cap)};
}

ManualOutcome onManualSet(uint32_t floor, uint32_t anchor, uint32_t uptimeSeconds, uint64_t manualSeconds) {
    uint32_t manual = toFloor(manualSeconds);
    if (manual == 0) return {floor, anchor};
    uint32_t manualAnchor = manual > uptimeSeconds ? manual - uptimeSeconds : 0;
    return {manual < floor ? manual : floor, manualAnchor < anchor ? manualAnchor : anchor};
}

} // namespace TimeFloor
