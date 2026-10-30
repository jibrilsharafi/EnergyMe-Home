// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "offline_energy_segments.h"

#include <stdio.h>
#include <string.h>

namespace OfflineEnergySegments {

uint32_t segmentHour(uint32_t boundaryUnixSeconds) {
    return boundaryUnixSeconds - (boundaryUnixSeconds % SEGMENT_SECONDS);
}

bool isExpired(uint32_t hourUnixSeconds, uint64_t nowUnixSeconds, uint32_t maxAgeSeconds) {
    // The newest reading a segment can hold is just before hour + SEGMENT_SECONDS
    return (uint64_t)hourUnixSeconds + SEGMENT_SECONDS + maxAgeSeconds <= nowUnixSeconds;
}

bool isReadingFresh(uint64_t unixTimeMs, uint64_t nowUnixSeconds, uint32_t maxAgeSeconds) {
    return unixTimeMs + (uint64_t)maxAgeSeconds * 1000 >= nowUnixSeconds * 1000;
}

TrimPlan planTrim(const Segment* sorted, size_t count, uint32_t appendHour, uint64_t nowUnixSeconds,
                  uint32_t maxAgeSeconds, size_t incomingBytes, size_t budgetBytes,
                  size_t fsFreeBytes, size_t headroomBytes) {
    TrimPlan plan = {0, false};
    if (sorted == nullptr) count = 0;

    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += sorted[i].bytes;

    size_t freed = 0;
    while (plan.dropCount < count && isExpired(sorted[plan.dropCount].hourUnixSeconds, nowUnixSeconds, maxAgeSeconds)) {
        freed += sorted[plan.dropCount].bytes;
        plan.dropCount++;
    }

    auto fits = [&]() {
        bool withinBudget = (total - freed) + incomingBytes <= budgetBytes;
        bool keepsHeadroom = fsFreeBytes + freed >= headroomBytes + incomingBytes;
        return withinBudget && keepsHeadroom;
    };

    while (!fits() && plan.dropCount < count && sorted[plan.dropCount].hourUnixSeconds != appendHour) {
        freed += sorted[plan.dropCount].bytes;
        plan.dropCount++;
    }

    plan.canAppend = fits();
    return plan;
}

bool formatName(uint32_t hourUnixSeconds, char* out, size_t outSize) {
    if (out == nullptr) return false;
    int written = snprintf(out, outSize, "%010lu.bin", (unsigned long)hourUnixSeconds);
    return written > 0 && (size_t)written < outSize;
}

bool parseName(const char* name, uint32_t& hourUnixSeconds) {
    if (name == nullptr || strlen(name) != 14 || strcmp(name + 10, ".bin") != 0) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < 10; i++) {
        if (name[i] < '0' || name[i] > '9') return false;
        value = value * 10 + (uint64_t)(name[i] - '0');
    }
    if (value > UINT32_MAX || value % SEGMENT_SECONDS != 0) return false;
    hourUnixSeconds = (uint32_t)value;
    return true;
}

}  // namespace OfflineEnergySegments
