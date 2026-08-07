// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "auth_lockout.h"

namespace AuthLockout {

namespace {

Entry *findEntry(Table &table, uint32_t address) {
    for (size_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        if (table.entries[i].address == address) return &table.entries[i];
    }
    return nullptr;
}

const Entry *findEntry(const Table &table, uint32_t address) {
    for (size_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        if (table.entries[i].address == address) return &table.entries[i];
    }
    return nullptr;
}

// A free slot if there is one, otherwise the least recently active entry. Evicting under a
// many-source flood is a real weakness - an attacker can push a genuine lockout out of the
// table - but the alternative is unbounded memory on a path an attacker controls. The
// generic rate limiter is what covers flooding; this table covers guessing.
Entry &claimSlot(Table &table, uint32_t address, uint64_t nowMs) {
    Entry *chosen = nullptr;

    for (size_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        if (table.entries[i].address == 0) {
            chosen = &table.entries[i];
            break;
        }
        if (chosen == nullptr || table.entries[i].lastSeenMs < chosen->lastSeenMs) {
            chosen = &table.entries[i];
        }
    }

    chosen->address = address;
    chosen->lockedUntilMs = 0;
    chosen->lastSeenMs = nowMs;
    chosen->lockCount = 0;
    chosen->consecutiveFailures = 0;
    return *chosen;
}

}  // namespace

void init(Table &table) {
    for (size_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        table.entries[i].address = 0;
        table.entries[i].lockedUntilMs = 0;
        table.entries[i].lastSeenMs = 0;
        table.entries[i].lockCount = 0;
        table.entries[i].consecutiveFailures = 0;
    }
}

uint32_t lockoutSecondsFor(uint16_t lockCount) {
    // lockCount 1 -> base, 2 -> 2x, 3 -> 4x ... capped. Escalation is what makes sustained
    // guessing unproductive without making a single typo expensive.
    uint32_t seconds = AUTH_LOCKOUT_BASE_SECONDS;

    for (uint16_t i = 1; i < lockCount; i++) {
        if (seconds >= AUTH_LOCKOUT_MAX_SECONDS / 2) return AUTH_LOCKOUT_MAX_SECONDS;
        seconds *= 2;
    }

    return seconds > AUTH_LOCKOUT_MAX_SECONDS ? AUTH_LOCKOUT_MAX_SECONDS : seconds;
}

bool isLocked(const Table &table, uint32_t address, uint64_t nowMs, uint32_t &retryAfterSeconds) {
    retryAfterSeconds = 0;

    if (address == AUTH_LOCKOUT_LOOPBACK_IP) return false;

    const Entry *entry = findEntry(table, address);
    if (entry == nullptr || entry->lockedUntilMs == 0) return false;
    if (nowMs >= entry->lockedUntilMs) return false;  // expired; recordFailure/Success tidies up

    uint64_t remainingMs = entry->lockedUntilMs - nowMs;
    // Round up, and never report 0 - a Retry-After of zero invites an immediate retry.
    retryAfterSeconds = (uint32_t)((remainingMs + 999) / 1000);
    if (retryAfterSeconds == 0) retryAfterSeconds = 1;

    return true;
}

void recordFailure(Table &table, uint32_t address, uint64_t nowMs) {
    if (address == AUTH_LOCKOUT_LOOPBACK_IP) return;

    Entry *entry = findEntry(table, address);
    if (entry == nullptr) entry = &claimSlot(table, address, nowMs);

    entry->lastSeenMs = nowMs;

    // A lockout that has run its course is cleared here rather than by a sweep, so the
    // failure that follows it starts a fresh count toward the next (longer) lockout.
    if (entry->lockedUntilMs != 0 && nowMs >= entry->lockedUntilMs) {
        entry->lockedUntilMs = 0;
        entry->consecutiveFailures = 0;
    }

    // Already locked: nothing to escalate until this one expires, and not counting keeps a
    // flood of requests during a lockout from inflating the next one.
    if (entry->lockedUntilMs != 0) return;

    if (entry->consecutiveFailures < 0xFF) entry->consecutiveFailures++;

    if (entry->consecutiveFailures >= AUTH_LOCKOUT_MAX_FAILURES) {
        if (entry->lockCount < 0xFFFF) entry->lockCount++;
        entry->lockedUntilMs = nowMs + (uint64_t)lockoutSecondsFor(entry->lockCount) * 1000ULL;
        entry->consecutiveFailures = 0;
    }
}

void recordSuccess(Table &table, uint32_t address, uint64_t nowMs) {
    if (address == AUTH_LOCKOUT_LOOPBACK_IP) return;

    Entry *entry = findEntry(table, address);
    if (entry == nullptr) return;  // nothing held against this source

    // Free the slot outright. Keeping lockCount would mean a user who once mistyped their way
    // into a lockout carries a longer next one forever, which punishes the owner rather than
    // an attacker - an attacker never reaches this path.
    entry->address = 0;
    entry->lockedUntilMs = 0;
    entry->lastSeenMs = nowMs;
    entry->lockCount = 0;
    entry->consecutiveFailures = 0;
}

}  // namespace AuthLockout
