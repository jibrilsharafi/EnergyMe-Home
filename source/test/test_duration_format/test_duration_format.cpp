// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for DurationFormat::humanizeDuration. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include <cstring>
#include "duration_format.h"

using namespace DurationFormat;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Milliseconds range  (< 1000)
// ============================================================================

void test_zero_ms(void) {
    char buf[32];
    humanizeDuration(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0 ms", buf);
}

void test_1_ms(void) {
    char buf[32];
    humanizeDuration(1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 ms", buf);
}

void test_850_ms(void) {
    char buf[32];
    humanizeDuration(850, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("850 ms", buf);
}

void test_999_ms(void) {
    char buf[32];
    humanizeDuration(999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("999 ms", buf);
}

// ============================================================================
// Seconds range  (1000 ms .. 59999 ms)
// ============================================================================

void test_1000_ms_is_1_s(void) {
    char buf[32];
    humanizeDuration(1000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 s", buf);
}

void test_1500_ms_is_1_s(void) {
    char buf[32];
    humanizeDuration(1500, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 s", buf);
}

void test_2400_ms_is_2_s(void) {
    char buf[32];
    humanizeDuration(2400, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2 s", buf);
}

void test_10000_ms_is_10_s(void) {
    char buf[32];
    humanizeDuration(10000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("10 s", buf);
}

void test_59999_ms_is_59_s(void) {
    char buf[32];
    humanizeDuration(59999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("59 s", buf);
}

// ============================================================================
// Minutes range  (60000 ms .. 3599999 ms)
// ============================================================================

void test_60000_ms_is_1_min(void) {
    char buf[32];
    humanizeDuration(60000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 min", buf);
}

void test_3_min(void) {
    char buf[32];
    humanizeDuration(3 * 60000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("3 min", buf);
}

void test_59_min(void) {
    char buf[32];
    humanizeDuration(59 * 60000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("59 min", buf);
}

void test_3599999_ms_is_59_min(void) {
    char buf[32];
    humanizeDuration(3599999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("59 min", buf);
}

// ============================================================================
// Hours range  (3600000 ms .. 86399999 ms)
// ============================================================================

void test_3600000_ms_is_1_h(void) {
    char buf[32];
    humanizeDuration(3600000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 h", buf);
}

void test_5_h(void) {
    char buf[32];
    humanizeDuration(5 * 3600000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("5 h", buf);
}

void test_23_h(void) {
    char buf[32];
    humanizeDuration(23 * 3600000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("23 h", buf);
}

void test_86399999_ms_is_23_h(void) {
    char buf[32];
    humanizeDuration(86399999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("23 h", buf);
}

// ============================================================================
// Days range  (>= 86400000 ms)
// ============================================================================

void test_86400000_ms_is_1_d(void) {
    char buf[32];
    humanizeDuration(86400000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1 d", buf);
}

void test_2_d(void) {
    char buf[32];
    humanizeDuration(2 * 86400000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2 d", buf);
}

void test_large_duration(void) {
    char buf[32];
    humanizeDuration(30 * 86400000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("30 d", buf);
}

// ============================================================================
// Safety: null/zero-size buffer
// ============================================================================

void test_null_buffer_is_safe(void) {
    humanizeDuration(1000, nullptr, 32);  // must not crash
}

void test_zero_size_is_safe(void) {
    char buf[32] = "sentinel";
    humanizeDuration(1000, buf, 0);  // must not write anything
    TEST_ASSERT_EQUAL_STRING("sentinel", buf);
}

// ============================================================================
// DURATION_FORMAT_BUFFER_SIZE adequacy
// "999 ms" is the widest realistic output; verify the constant fits it.
// ============================================================================

void test_buffer_size_fits_999_ms(void) {
    char buf[DURATION_FORMAT_BUFFER_SIZE];
    humanizeDuration(999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("999 ms", buf);
}

void test_buffer_size_fits_59_min(void) {
    char buf[DURATION_FORMAT_BUFFER_SIZE];
    humanizeDuration(59 * 60000ULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("59 min", buf);
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_zero_ms);
    RUN_TEST(test_1_ms);
    RUN_TEST(test_850_ms);
    RUN_TEST(test_999_ms);

    RUN_TEST(test_1000_ms_is_1_s);
    RUN_TEST(test_1500_ms_is_1_s);
    RUN_TEST(test_2400_ms_is_2_s);
    RUN_TEST(test_10000_ms_is_10_s);
    RUN_TEST(test_59999_ms_is_59_s);

    RUN_TEST(test_60000_ms_is_1_min);
    RUN_TEST(test_3_min);
    RUN_TEST(test_59_min);
    RUN_TEST(test_3599999_ms_is_59_min);

    RUN_TEST(test_3600000_ms_is_1_h);
    RUN_TEST(test_5_h);
    RUN_TEST(test_23_h);
    RUN_TEST(test_86399999_ms_is_23_h);

    RUN_TEST(test_86400000_ms_is_1_d);
    RUN_TEST(test_2_d);
    RUN_TEST(test_large_duration);

    RUN_TEST(test_null_buffer_is_safe);
    RUN_TEST(test_zero_size_is_safe);

    RUN_TEST(test_buffer_size_fits_999_ms);
    RUN_TEST(test_buffer_size_fits_59_min);

    return UNITY_END();
}
