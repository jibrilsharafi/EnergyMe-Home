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

bool extractExecutionId(const char* topic, char* out, size_t outSize) {
    if (topic == nullptr || out == nullptr || outSize == 0) return false;

    static const char marker[] = "/executions/";
    const char* start = strstr(topic, marker);
    if (start == nullptr) return false;
    start += sizeof(marker) - 1; // advance past "/executions/"

    const char* end = strchr(start, '/');
    if (end == nullptr || end == start) return false; // missing trailing "/request/..." or empty id

    size_t len = (size_t)(end - start);
    if (len + 1 > outSize) return false; // would truncate -> wrong response topic

    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool isCommandStale(uint64_t createdAtUnix, uint64_t nowUnix, uint64_t maxAgeSeconds) {
    if (createdAtUnix == 0) return false;       // no timestamp: cannot judge
    if (nowUnix <= createdAtUnix) return false; // future/equal (clock skew): not stale
    return (nowUnix - createdAtUnix) > maxAgeSeconds;
}

size_t parseChannelList(const char* spec, uint8_t channelCount,
                         uint8_t* validIndices, size_t maxOut,
                         bool* invalidTokenSeen) {
    if (invalidTokenSeen != nullptr) *invalidTokenSeen = false;
    if (spec == nullptr || validIndices == nullptr || maxOut == 0) return 0;

    size_t written = 0;
    const char* p = spec;
    while (*p != '\0') {
        while (*p == ' ') p++; // trim leading spaces
        const char* tokenStart = p;
        while (*p != '\0' && *p != ',') p++;
        const char* tokenEnd = p; // exclusive
        while (tokenEnd > tokenStart && *(tokenEnd - 1) == ' ') tokenEnd--; // trim trailing spaces

        size_t tokenLen = (size_t)(tokenEnd - tokenStart);
        if (tokenLen > 0) {
            char tok[8]; // room for any uint8_t index ("255") with margin
            if (tokenLen >= sizeof(tok)) {
                if (invalidTokenSeen != nullptr) *invalidTokenSeen = true; // can't possibly be a valid index
            } else {
                memcpy(tok, tokenStart, tokenLen);
                tok[tokenLen] = '\0';
                uint8_t idx;
                if (parseChannelIndex(tok, channelCount, &idx)) {
                    if (written < maxOut) validIndices[written++] = idx; // beyond maxOut: silently dropped, not invalid
                } else {
                    if (invalidTokenSeen != nullptr) *invalidTokenSeen = true;
                }
            }
        } // empty token (stray "," or trailing comma): skipped silently

        if (*p == ',') p++;
    }
    return written;
}

bool isPlausibleStartMeasuringUnixTimeMs(uint64_t valueMs, uint64_t minPlausibleMs) {
    return valueMs == 0 || valueMs >= minPlausibleMs;
}

} // namespace ShadowLogic
