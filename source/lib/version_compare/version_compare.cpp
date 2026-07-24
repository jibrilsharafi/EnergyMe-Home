// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "version_compare.h"
#include <cstdio>

namespace VersionCompare {

// Parses "vX.Y.Z" / "X.Y.Z" (trailing text like "-rc1" or " (dev)" is
// ignored). A null pointer or unparseable string leaves major/minor/patch
// at their initialized 0.
static void parse(const char *version, int &major, int &minor, int &patch) {
    major = minor = patch = 0;
    if (!version) return;

    const char *str = (version[0] == 'v') ? version + 1 : version;
    sscanf(str, "%d.%d.%d", &major, &minor, &patch);
}

int compare(const char *current, const char *available) {
    int currentMajor, currentMinor, currentPatch;
    int availableMajor, availableMinor, availablePatch;

    parse(current, currentMajor, currentMinor, currentPatch);
    parse(available, availableMajor, availableMinor, availablePatch);

    if (currentMajor != availableMajor) return currentMajor - availableMajor;
    if (currentMinor != availableMinor) return currentMinor - availableMinor;
    return currentPatch - availablePatch;
}

}  // namespace VersionCompare
