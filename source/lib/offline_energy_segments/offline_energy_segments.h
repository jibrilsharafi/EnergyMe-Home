// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure, dependency-free retention rules for the offline energy buffer: one
// segment file per wall-clock hour, trimmed oldest-first by age, by a byte
// budget and by a LittleFS free-space headroom (see openspec
// offline-energy-buffer, design D3). Kept apart from the LittleFS code so the
// trim arithmetic is host-testable.

namespace OfflineEnergySegments {

constexpr uint32_t SEGMENT_SECONDS = 3600;
constexpr size_t NAME_BUFFER_SIZE = 16; // "<10-digit hour>.bin" + null terminator

struct Segment {
    uint32_t hourUnixSeconds;
    size_t bytes;
};

struct TrimPlan {
    size_t dropCount; // Oldest segments to delete, from the front of the sorted list
    bool canAppend;   // False when even after dropping, the new record would break budget or headroom
};

uint32_t segmentHour(uint32_t boundaryUnixSeconds);

// True when every reading the segment can hold is older than `maxAgeSeconds`.
bool isExpired(uint32_t hourUnixSeconds, uint64_t nowUnixSeconds, uint32_t maxAgeSeconds);

// True when a reading taken at `unixTimeMs` is not older than `maxAgeSeconds`.
bool isReadingFresh(uint64_t unixTimeMs, uint64_t nowUnixSeconds, uint32_t maxAgeSeconds);

// `sorted` is oldest-first. Drops every expired segment, then further oldest
// segments until appending `incomingBytes` keeps the buffer within
// `budgetBytes` and leaves at least `headroomBytes` free on the filesystem.
// The segment the record will be appended to (`appendHour`) is never dropped
// for budget or headroom, only for age.
TrimPlan planTrim(const Segment* sorted, size_t count, uint32_t appendHour, uint64_t nowUnixSeconds,
                  uint32_t maxAgeSeconds, size_t incomingBytes, size_t budgetBytes,
                  size_t fsFreeBytes, size_t headroomBytes);

bool formatName(uint32_t hourUnixSeconds, char* out, size_t outSize);
bool parseName(const char* name, uint32_t& hourUnixSeconds);

}  // namespace OfflineEnergySegments
