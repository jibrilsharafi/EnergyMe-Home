// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free per-source lockout for repeated failed logins.
//
// The web password is the only thing between a LAN host and the whole device, and
// before this existed nothing slowed guessing down: digestAuth short-circuits on
// failure, so the rate limiter registered after it never ran on a 401. Measured on
// hardware: 400 consecutive failed logins, zero 429.
//
// The rule that decides whether a source is locked out lives here and only here.
// customserver.cpp supplies the clock and the address and acts on the answer - it
// must not restate any threshold or duration. Same discipline as web_auth_gate, for
// the same reason.
//
// Knows nothing about Arduino, lwIP or sockets: addresses are plain uint32_t in host
// byte order, time is milliseconds from a monotonic clock.

namespace AuthLockout {

// How many consecutive credentialed failures from one source before it is locked out.
// Low enough to bite an attacker quickly, high enough to survive a fat-fingered password.
#define AUTH_LOCKOUT_MAX_FAILURES 5

// First lockout. Short on purpose - the common case for hitting it is a human mistyping,
// and the cost of that should be an annoyance rather than a support call.
#define AUTH_LOCKOUT_BASE_SECONDS 30

// Ceiling for the doubling. A source is never barred permanently: someone locked out of
// their own meter must always be able to wait it out, because the alternatives are a
// reboot or the physical button and neither is convenient.
#define AUTH_LOCKOUT_MAX_SECONDS 900  // 15 minutes

// Distinct sources tracked at once. Fixed, in .bss - a flood from many addresses must not
// grow memory. When full, the least recently active entry is evicted.
#define AUTH_LOCKOUT_TABLE_SIZE 8

// 127.0.0.1 in host byte order. The device probes its own health endpoint from here; that
// route skips the whole middleware chain so this can never be reached in practice, but the
// consequence of being wrong is the restart-loop-into-firmware-rollback failure documented
// in the default-password work, and one structural guarantee is not enough for that.
#define AUTH_LOCKOUT_LOOPBACK_IP 0x7F000001u

struct Entry {
    uint32_t address;              // 0 means the slot is free
    uint64_t lockedUntilMs;        // 0 when not currently locked
    uint64_t lastSeenMs;           // for eviction
    uint16_t lockCount;            // how many times this source has been locked; drives escalation
    uint8_t consecutiveFailures;
};

struct Table {
    Entry entries[AUTH_LOCKOUT_TABLE_SIZE];
};

// Clears every slot. Must be called before the table is used.
void init(Table &table);

// True when this source is currently locked out. When it returns true,
// retryAfterSeconds is set to the whole seconds remaining, never zero - a
// Retry-After of 0 would invite an immediate retry.
//
// Loopback is never locked out (AUTH_LOCKOUT_LOOPBACK_IP).
bool isLocked(const Table &table, uint32_t address, uint64_t nowMs, uint32_t &retryAfterSeconds);

// Records one genuine credential failure. Returns true only on the transition into a locked
// state - the single failure that crosses the threshold - so the caller can log and count a
// lockout exactly once, not on every failure and not on every subsequent request while locked.
//
// "Genuine" is the caller's job and it is the crux of the whole feature: a 401 answering a
// request that carried no credentials is the ordinary opening leg of a digest handshake, and
// counting those would lock out every normal user within a few page loads. Only pass a
// rejection of credentials that were actually supplied.
bool recordFailure(Table &table, uint32_t address, uint64_t nowMs);

// Records a successful authentication, clearing the source outright - a user who mistypes and
// then succeeds carries no penalty forward.
void recordSuccess(Table &table, uint32_t address, uint64_t nowMs);

// How long the Nth lockout lasts, in seconds. Exposed for testing the escalation curve
// directly rather than inferring it from the table.
uint32_t lockoutSecondsFor(uint16_t lockCount);

}  // namespace AuthLockout
