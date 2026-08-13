// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for RollbackLogic. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "rollback_logic.h"

using namespace RollbackLogic;

// 64-hex fixtures (distinct builds)
static const char *SHA_A = "e88e6af83cb101413dde8cdf267057ae60b23b2c019c7981a3dc8deaae3e60b3";
static const char *SHA_A_UPPER = "E88E6AF83CB101413DDE8CDF267057AE60B23B2C019C7981A3DC8DEAAE3E60B3";
static const char *SHA_B = "06b7d333419df0172f8ffb4117b40bc7c428ee552e142d387b12e46fdedc7e1a";
static const char *SHA_C = "0000000000000000000000000000000000000000000000000000000000000001";

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// isValidSha256Hex
// ============================================================================

void test_valid_hex_lower(void) {
    TEST_ASSERT_TRUE(isValidSha256Hex(SHA_A));
}

void test_valid_hex_upper(void) {
    TEST_ASSERT_TRUE(isValidSha256Hex(SHA_A_UPPER));
}

void test_null_is_invalid(void) {
    TEST_ASSERT_FALSE(isValidSha256Hex(nullptr));
}

void test_empty_is_invalid(void) {
    TEST_ASSERT_FALSE(isValidSha256Hex(""));
}

void test_too_short_is_invalid(void) {
    TEST_ASSERT_FALSE(isValidSha256Hex("e88e6af8"));
}

void test_too_long_is_invalid(void) {
    char buf[66];
    for (int i = 0; i < 65; i++) buf[i] = 'a';
    buf[65] = '\0';
    TEST_ASSERT_FALSE(isValidSha256Hex(buf));
}

void test_non_hex_char_is_invalid(void) {
    char buf[65];
    for (int i = 0; i < 64; i++) buf[i] = 'a';
    buf[64] = '\0';
    buf[31] = 'g';
    TEST_ASSERT_FALSE(isValidSha256Hex(buf));
}

// ============================================================================
// sha256HexEquals
// ============================================================================

void test_equals_same(void) {
    TEST_ASSERT_TRUE(sha256HexEquals(SHA_A, SHA_A));
}

void test_equals_case_insensitive(void) {
    TEST_ASSERT_TRUE(sha256HexEquals(SHA_A, SHA_A_UPPER));
}

void test_equals_different(void) {
    TEST_ASSERT_FALSE(sha256HexEquals(SHA_A, SHA_B));
}

void test_equals_invalid_side(void) {
    TEST_ASSERT_FALSE(sha256HexEquals(SHA_A, "short"));
    TEST_ASSERT_FALSE(sha256HexEquals(nullptr, SHA_A));
}

// ============================================================================
// decide - all five outcomes
// ============================================================================

void test_decide_proceed(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::PROCEED),
                      static_cast<int>(decide(SHA_B, true, SHA_B, SHA_A)));
}

void test_decide_proceed_case_insensitive(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::PROCEED),
                      static_cast<int>(decide(SHA_A_UPPER, true, SHA_A, SHA_B)));
}

void test_decide_noop_redelivery_after_success(void) {
    // After a completed rollback: expected == running, passive holds the
    // image we left. Must be a success no-op, never a second switch.
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::NOOP_ALREADY_DONE),
                      static_cast<int>(decide(SHA_A, true, SHA_B, SHA_A)));
}

void test_decide_noop_wins_over_no_target(void) {
    // Redelivery with an unreadable passive slot is still a success no-op.
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::NOOP_ALREADY_DONE),
                      static_cast<int>(decide(SHA_A, false, nullptr, SHA_A)));
}

void test_decide_mismatch(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::MISMATCH),
                      static_cast<int>(decide(SHA_C, true, SHA_B, SHA_A)));
}

void test_decide_missing_null(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::MISSING_SHA),
                      static_cast<int>(decide(nullptr, true, SHA_B, SHA_A)));
}

void test_decide_missing_malformed(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::MISSING_SHA),
                      static_cast<int>(decide("deadbeef", true, SHA_B, SHA_A)));
}

void test_decide_no_target(void) {
    TEST_ASSERT_EQUAL(static_cast<int>(Decision::NO_TARGET),
                      static_cast<int>(decide(SHA_B, false, nullptr, SHA_A)));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_valid_hex_lower);
    RUN_TEST(test_valid_hex_upper);
    RUN_TEST(test_null_is_invalid);
    RUN_TEST(test_empty_is_invalid);
    RUN_TEST(test_too_short_is_invalid);
    RUN_TEST(test_too_long_is_invalid);
    RUN_TEST(test_non_hex_char_is_invalid);

    RUN_TEST(test_equals_same);
    RUN_TEST(test_equals_case_insensitive);
    RUN_TEST(test_equals_different);
    RUN_TEST(test_equals_invalid_side);

    RUN_TEST(test_decide_proceed);
    RUN_TEST(test_decide_proceed_case_insensitive);
    RUN_TEST(test_decide_noop_redelivery_after_success);
    RUN_TEST(test_decide_noop_wins_over_no_target);
    RUN_TEST(test_decide_mismatch);
    RUN_TEST(test_decide_missing_null);
    RUN_TEST(test_decide_missing_malformed);
    RUN_TEST(test_decide_no_target);

    return UNITY_END();
}
