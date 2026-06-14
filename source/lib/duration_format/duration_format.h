// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free duration formatter.
//
// Formats a millisecond count into the most readable unit:
//   <1 s    ->  "850 ms"
//   <60 s   ->  "2 s"
//   <60 min ->  "3 min"
//   <24 h   ->  "5 h"
//   >=24 h  ->  "2 d"
//
// Output is ASCII-only and null-terminated. If `outSize` is 0 the call is a no-op.
// Use DURATION_FORMAT_BUFFER_SIZE for the local char buffer at every call site.

// "999 ms" is the widest sub-day output (6 chars + null = 7 bytes). The days branch
// is unbounded for huge uint64_t values but no real firmware duration exceeds a few
// hundred days ("999 d" = 6 bytes). 12 bytes gives comfortable headroom over the
// 7-byte worst case without inflating every call-site buffer to 24.
#define DURATION_FORMAT_BUFFER_SIZE 12

namespace DurationFormat {

void humanizeDuration(uint64_t ms, char *out, size_t outSize);

}  // namespace DurationFormat
