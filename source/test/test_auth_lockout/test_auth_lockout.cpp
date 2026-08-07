// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the per-source auth lockout. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "auth_lockout.h"

using namespace AuthLockout;

namespace {

constexpr uint32_t kAlice = 0xC0A80102u;  // 192.168.1.2
constexpr uint32_t kBob = 0xC0A80103u;    // 192.168.1.3
constexpr uint64_t kSecond = 1000ULL;

Table table;

uint32_t failUntilLocked(uint32_t address, uint64_t nowMs) {
    uint32_t retryAfter = 0;
    for (int i = 0; i < AUTH_LOCKOUT_MAX_FAILURES; i++) {
        recordFailure(table, address, nowMs);
    }
    isLocked(table, address, nowMs, retryAfter);
    return retryAfter;
}

}  // namespace

void setUp(void) { init(table); }
void tearDown(void) {}

// --- Threshold -----------------------------------------------------------------------

void test_a_fresh_source_is_not_locked(void) {
    uint32_t retryAfter = 99;
    TEST_ASSERT_FALSE(isLocked(table, kAlice, 0, retryAfter));
    TEST_ASSERT_EQUAL_UINT32(0, retryAfter);
}

void test_one_short_of_the_threshold_does_not_lock(void) {
    uint32_t retryAfter = 0;
    for (int i = 0; i < AUTH_LOCKOUT_MAX_FAILURES - 1; i++) {
        recordFailure(table, kAlice, 0);
        TEST_ASSERT_FALSE(isLocked(table, kAlice, 0, retryAfter));
    }
}

void test_the_threshold_locks(void) {
    uint32_t retryAfter = failUntilLocked(kAlice, 0);
    uint32_t dummy = 0;
    TEST_ASSERT_TRUE(isLocked(table, kAlice, 0, dummy));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS, retryAfter);
}

// --- Success clears ------------------------------------------------------------------

void test_success_clears_the_count_at_every_point_below_the_threshold(void) {
    uint32_t retryAfter = 0;
    for (int failures = 1; failures < AUTH_LOCKOUT_MAX_FAILURES; failures++) {
        init(table);
        for (int i = 0; i < failures; i++) recordFailure(table, kAlice, 0);
        recordSuccess(table, kAlice, 0);

        // Having cleared, a full fresh run of failures should be needed to lock.
        for (int i = 0; i < AUTH_LOCKOUT_MAX_FAILURES - 1; i++) {
            recordFailure(table, kAlice, 0);
            TEST_ASSERT_FALSE(isLocked(table, kAlice, 0, retryAfter));
        }
    }
}

// A user who mistypes and then gets it right must carry nothing forward. An attacker never
// reaches this path, so a generous reset costs nothing defensively.
void test_success_also_clears_the_escalation_history(void) {
    failUntilLocked(kAlice, 0);
    recordSuccess(table, kAlice, 60 * kSecond);

    uint32_t retryAfter = failUntilLocked(kAlice, 60 * kSecond);
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS, retryAfter);
}

// --- Expiry --------------------------------------------------------------------------

void test_the_lockout_expires_on_its_own(void) {
    failUntilLocked(kAlice, 0);
    uint32_t retryAfter = 0;

    uint64_t deadline = (uint64_t)AUTH_LOCKOUT_BASE_SECONDS * kSecond;
    TEST_ASSERT_TRUE(isLocked(table, kAlice, deadline - 1, retryAfter));
    TEST_ASSERT_FALSE(isLocked(table, kAlice, deadline, retryAfter));
    TEST_ASSERT_FALSE(isLocked(table, kAlice, deadline + 1, retryAfter));
}

// Nobody should ever be told to retry in zero seconds.
void test_retry_after_is_never_zero_while_locked(void) {
    failUntilLocked(kAlice, 0);
    uint64_t deadline = (uint64_t)AUTH_LOCKOUT_BASE_SECONDS * kSecond;

    for (uint64_t now = 0; now < deadline; now += 250) {
        uint32_t retryAfter = 0;
        TEST_ASSERT_TRUE(isLocked(table, kAlice, now, retryAfter));
        TEST_ASSERT_GREATER_THAN_UINT32(0, retryAfter);
    }
}

// --- Escalation ----------------------------------------------------------------------

void test_each_lockout_is_longer_than_the_last(void) {
    uint32_t previous = 0;
    uint64_t now = 0;

    for (int round = 0; round < 5; round++) {
        uint32_t retryAfter = failUntilLocked(kAlice, now);
        if (round > 0) TEST_ASSERT_GREATER_THAN_UINT32(previous, retryAfter);
        previous = retryAfter;
        now += (uint64_t)retryAfter * kSecond + kSecond;  // wait it out
    }
}

void test_escalation_stops_at_the_ceiling(void) {
    // No source may ever be barred permanently - someone locked out of their own meter has to
    // be able to wait it out.
    for (uint16_t lockCount = 1; lockCount < 1000; lockCount++) {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(AUTH_LOCKOUT_MAX_SECONDS, lockoutSecondsFor(lockCount));
    }
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_MAX_SECONDS, lockoutSecondsFor(999));
}

void test_the_escalation_curve_starts_at_the_base_and_doubles(void) {
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS, lockoutSecondsFor(1));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS * 2, lockoutSecondsFor(2));
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS * 4, lockoutSecondsFor(3));
}

// Hammering while already locked must not inflate the next lockout - otherwise an attacker
// could drive their own lockout to the ceiling and, more importantly, the arithmetic could
// overflow.
void test_failures_during_a_lockout_do_not_escalate_it(void) {
    uint32_t first = failUntilLocked(kAlice, 0);
    for (int i = 0; i < 500; i++) recordFailure(table, kAlice, 1000);

    uint32_t retryAfter = 0;
    TEST_ASSERT_TRUE(isLocked(table, kAlice, 1000, retryAfter));

    uint64_t afterExpiry = (uint64_t)first * kSecond + kSecond;
    uint32_t second = failUntilLocked(kAlice, afterExpiry);
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS * 2, second);
}

// --- Isolation between sources --------------------------------------------------------

void test_locking_one_source_does_not_lock_another(void) {
    failUntilLocked(kAlice, 0);
    uint32_t retryAfter = 0;
    TEST_ASSERT_TRUE(isLocked(table, kAlice, 0, retryAfter));
    TEST_ASSERT_FALSE(isLocked(table, kBob, 0, retryAfter));
}

// --- Loopback -------------------------------------------------------------------------
//
// The device probes its own health endpoint from 127.0.0.1. That route skips the middleware
// chain so this is unreachable in practice, but five failed health checks request a restart
// and the loop ends in a firmware rollback and an NVS wipe - one structural guarantee is not
// enough for that.
void test_loopback_is_never_locked_out(void) {
    uint32_t retryAfter = 0;
    for (int i = 0; i < AUTH_LOCKOUT_MAX_FAILURES * 20; i++) {
        recordFailure(table, AUTH_LOCKOUT_LOOPBACK_IP, (uint64_t)i);
        TEST_ASSERT_FALSE(isLocked(table, AUTH_LOCKOUT_LOOPBACK_IP, (uint64_t)i, retryAfter));
    }
}

void test_loopback_never_consumes_a_slot(void) {
    for (int i = 0; i < 100; i++) recordFailure(table, AUTH_LOCKOUT_LOOPBACK_IP, (uint64_t)i);

    // Every slot must still be available to real sources.
    for (uint32_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        failUntilLocked(0x0A000001u + i, 0);
    }
    uint32_t retryAfter = 0;
    for (uint32_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        TEST_ASSERT_TRUE(isLocked(table, 0x0A000001u + i, 0, retryAfter));
    }
}

// --- Table pressure --------------------------------------------------------------------

void test_more_sources_than_slots_stays_within_the_table(void) {
    // Fill beyond capacity; the point is that it does not misbehave, not that every source
    // is remembered. Eviction is a documented limitation (design D5).
    for (uint32_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE * 4; i++) {
        failUntilLocked(0x0A000001u + i, (uint64_t)i * kSecond);
    }

    uint32_t retryAfter = 0;
    uint32_t lockedCount = 0;
    for (uint32_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE * 4; i++) {
        if (isLocked(table, 0x0A000001u + i, (uint64_t)AUTH_LOCKOUT_TABLE_SIZE * 4 * kSecond, retryAfter)) {
            lockedCount++;
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(AUTH_LOCKOUT_TABLE_SIZE, lockedCount);
}

// The most recent offenders are the ones worth remembering.
void test_eviction_takes_the_least_recently_active(void) {
    for (uint32_t i = 0; i < AUTH_LOCKOUT_TABLE_SIZE; i++) {
        failUntilLocked(0x0A000001u + i, (uint64_t)i * kSecond);
    }

    uint64_t later = (uint64_t)(AUTH_LOCKOUT_TABLE_SIZE + 10) * kSecond;
    failUntilLocked(0xC0A80199u, later);

    uint32_t retryAfter = 0;
    TEST_ASSERT_TRUE(isLocked(table, 0xC0A80199u, later, retryAfter));
    // The oldest (index 0, seen at t=0) is the one that should have gone.
    TEST_ASSERT_FALSE(isLocked(table, 0x0A000001u, later, retryAfter));
}

void test_an_unknown_source_success_is_harmless(void) {
    recordSuccess(table, kBob, 0);
    uint32_t retryAfter = 0;
    TEST_ASSERT_FALSE(isLocked(table, kBob, 0, retryAfter));
}

// isLocked is a pure query with no side effect - the upload handlers call it before doing
// their own auth, and calling it must not itself advance or reset any counter. A defense
// that changed state just by being *checked* would be a footgun.
void test_isLocked_is_a_pure_query(void) {
    for (int i = 0; i < AUTH_LOCKOUT_MAX_FAILURES - 1; i++) recordFailure(table, kAlice, 0);

    uint32_t retryAfter = 0;
    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_FALSE(isLocked(table, kAlice, 0, retryAfter));  // still one short
    }
    // The pending failures are intact: one more locks, checking did not consume them.
    recordFailure(table, kAlice, 0);
    TEST_ASSERT_TRUE(isLocked(table, kAlice, 0, retryAfter));
}

// --- Large clock values -----------------------------------------------------------------
//
// millis64() does not wrap in any practical uptime, but the arithmetic should not assume a
// small clock either.
void test_large_clock_values_behave(void) {
    uint64_t huge = 1000ULL * 60 * 60 * 24 * 365 * 10;  // ~10 years of uptime
    uint32_t retryAfter = failUntilLocked(kAlice, huge);
    TEST_ASSERT_EQUAL_UINT32(AUTH_LOCKOUT_BASE_SECONDS, retryAfter);

    uint32_t dummy = 0;
    TEST_ASSERT_TRUE(isLocked(table, kAlice, huge, dummy));
    TEST_ASSERT_FALSE(isLocked(table, kAlice, huge + (uint64_t)AUTH_LOCKOUT_BASE_SECONDS * kSecond, dummy));
}

int main(int, char **) {
    UNITY_BEGIN();

    RUN_TEST(test_a_fresh_source_is_not_locked);
    RUN_TEST(test_one_short_of_the_threshold_does_not_lock);
    RUN_TEST(test_the_threshold_locks);

    RUN_TEST(test_success_clears_the_count_at_every_point_below_the_threshold);
    RUN_TEST(test_success_also_clears_the_escalation_history);

    RUN_TEST(test_the_lockout_expires_on_its_own);
    RUN_TEST(test_retry_after_is_never_zero_while_locked);

    RUN_TEST(test_each_lockout_is_longer_than_the_last);
    RUN_TEST(test_escalation_stops_at_the_ceiling);
    RUN_TEST(test_the_escalation_curve_starts_at_the_base_and_doubles);
    RUN_TEST(test_failures_during_a_lockout_do_not_escalate_it);

    RUN_TEST(test_locking_one_source_does_not_lock_another);

    RUN_TEST(test_loopback_is_never_locked_out);
    RUN_TEST(test_loopback_never_consumes_a_slot);

    RUN_TEST(test_more_sources_than_slots_stays_within_the_table);
    RUN_TEST(test_eviction_takes_the_least_recently_active);
    RUN_TEST(test_an_unknown_source_success_is_harmless);
    RUN_TEST(test_isLocked_is_a_pure_query);

    RUN_TEST(test_large_clock_values_behave);

    return UNITY_END();
}
