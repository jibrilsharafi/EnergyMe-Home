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
// Output is ASCII-only and null-terminated. If `outSize` is 0 the call
// is a no-op. A minimum buffer of 16 bytes covers all representable values.
namespace DurationFormat {

void humanizeDuration(uint64_t ms, char *out, size_t outSize);

}  // namespace DurationFormat
