// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "version_compare.h"
#include <cstdlib>
#include <climits>

namespace VersionCompare {

// Parses one numeric component starting at `str`, advances `str` past it.
// No digits found -> 0, `str` left unchanged. Negative -> clamped to 0.
// Out of [0, INT_MAX] range (whether via strtol's own overflow clamping or
// ours) -> clamped to INT_MAX. Never invokes undefined behavior, unlike
// sscanf's "%d" on an out-of-range literal.
static long parseComponent(const char *&str) {
    if (!str) return 0;
    char *end = nullptr;
    long value = strtol(str, &end, 10);
    if (end == str) return 0;
    str = end;
    if (value < 0) return 0;
    if (value > INT_MAX) return INT_MAX;
    return value;
}

// Parses "vX.Y.Z" / "VX.Y.Z" / "X.Y.Z" (trailing text like "-rc1" or
// " (dev)" is ignored - parsing stops at the first non-numeric,
// non-'.'-separator character). A null pointer or unparseable string
// leaves major/minor/patch at their initialized 0.
static void parse(const char *version, long &major, long &minor, long &patch) {
    major = minor = patch = 0;
    if (!version) return;

    const char *str = (version[0] == 'v' || version[0] == 'V') ? version + 1 : version;

    major = parseComponent(str);
    if (*str != '.') return;
    str++;

    minor = parseComponent(str);
    if (*str != '.') return;
    str++;

    patch = parseComponent(str);
}

static int compareLong(long a, long b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

int compare(const char *current, const char *available) {
    long currentMajor, currentMinor, currentPatch;
    long availableMajor, availableMinor, availablePatch;

    parse(current, currentMajor, currentMinor, currentPatch);
    parse(available, availableMajor, availableMinor, availablePatch);

    int result = compareLong(currentMajor, availableMajor);
    if (result != 0) return result;

    result = compareLong(currentMinor, availableMinor);
    if (result != 0) return result;

    return compareLong(currentPatch, availablePatch);
}

}  // namespace VersionCompare
