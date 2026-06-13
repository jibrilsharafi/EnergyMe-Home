// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "duration_format.h"
#include <cstdio>

namespace DurationFormat {

static constexpr uint64_t MS_PER_S   =     1000ULL;
static constexpr uint64_t MS_PER_MIN =    60000ULL;
static constexpr uint64_t MS_PER_H   =  3600000ULL;
static constexpr uint64_t MS_PER_D   = 86400000ULL;

void humanizeDuration(uint64_t ms, char *out, size_t outSize) {
    if (!out || outSize == 0) return;

    if (ms < MS_PER_S) {
        snprintf(out, outSize, "%llu ms", (unsigned long long)ms);
    } else if (ms < MS_PER_MIN) {
        snprintf(out, outSize, "%llu s", (unsigned long long)(ms / MS_PER_S));
    } else if (ms < MS_PER_H) {
        snprintf(out, outSize, "%llu min", (unsigned long long)(ms / MS_PER_MIN));
    } else if (ms < MS_PER_D) {
        snprintf(out, outSize, "%llu h", (unsigned long long)(ms / MS_PER_H));
    } else {
        snprintf(out, outSize, "%llu d", (unsigned long long)(ms / MS_PER_D));
    }
}

}  // namespace DurationFormat
