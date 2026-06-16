// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "shadow_logic.h"

#include <cstdio>
#include <cstring>

namespace ShadowLogic {

// Indexed by level int (0..5); the single source of truth for both directions.
static const char* const LEVEL_NAMES[] = {
    "VERBOSE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
static constexpr int LEVEL_COUNT = 6;

int logLevelFromString(const char* name) {
    if (name == nullptr) return LOG_LEVEL_INVALID;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (strcmp(name, LEVEL_NAMES[i]) == 0) return i;
    }
    return LOG_LEVEL_INVALID;
}

const char* logLevelToString(int level) {
    if (level < 0 || level >= LEVEL_COUNT) return nullptr;
    return LEVEL_NAMES[level];
}

bool isTransientLogLevel(int level) {
    // VERBOSE (0) and DEBUG (1) are runtime-only and auto-reverted.
    return level == 0 || level == 1;
}

bool parseChannelIndex(const char* key, uint8_t channelCount, uint8_t* out) {
    if (key == nullptr || key[0] == '\0' || out == nullptr) return false;

    uint32_t value = 0;
    for (const char* p = key; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') return false;       // non-numeric key
        value = value * 10u + (uint32_t)(*p - '0');
        if (value >= (uint32_t)channelCount) return false; // out of range (also caps overflow)
    }

    *out = (uint8_t)value;
    return true;
}

size_t formatClientToken(char* out, size_t outSize, uint32_t r1, uint32_t r2) {
    if (out == nullptr || outSize < 17) return 0; // 16 hex chars + null terminator
    snprintf(out, outSize, "%08x%08x", r1, r2);
    return 16;
}

} // namespace ShadowLogic
