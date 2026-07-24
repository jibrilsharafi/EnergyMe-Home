// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for VersionCompare::compare. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "version_compare.h"

using namespace VersionCompare;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Well-formed, no prefix
// ============================================================================

void test_equal_versions(void) {
    TEST_ASSERT_EQUAL(0, compare("1.5.0", "1.5.0"));
}

void test_greater_by_patch(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("1.5.1", "1.5.0"));
}

void test_less_by_patch(void) {
    TEST_ASSERT_LESS_THAN(0, compare("1.5.0", "1.5.1"));
}

void test_greater_by_minor(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("1.6.0", "1.5.9"));
}

void test_less_by_minor(void) {
    TEST_ASSERT_LESS_THAN(0, compare("1.5.9", "1.6.0"));
}

void test_greater_by_major(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("2.0.0", "1.9.9"));
}

void test_less_by_major(void) {
    TEST_ASSERT_LESS_THAN(0, compare("1.9.9", "2.0.0"));
}

// ============================================================================
// Multi-digit correctness (must not compare lexicographically)
// ============================================================================

void test_double_digit_major_beats_single_digit(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("10.0.0", "9.99.99"));
}

void test_double_digit_minor_beats_single_digit(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("1.10.0", "1.9.0"));
}

void test_double_digit_patch_beats_single_digit(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("1.0.10", "1.0.9"));
}

// ============================================================================
// 'v' prefix, mixed combinations
// ============================================================================

void test_v_prefix_both_sides(void) {
    TEST_ASSERT_EQUAL(0, compare("v1.5.0", "v1.5.0"));
}

void test_v_prefix_current_only(void) {
    TEST_ASSERT_EQUAL(0, compare("v1.5.0", "1.5.0"));
}

void test_v_prefix_available_only(void) {
    TEST_ASSERT_EQUAL(0, compare("1.5.0", "v1.5.0"));
}

void test_capital_v_prefix(void) {
    TEST_ASSERT_EQUAL(0, compare("V1.5.0", "1.5.0"));
}

void test_capital_v_prefix_not_treated_as_downgrade(void) {
    // Regression: capital 'V' must strip the same as lowercase 'v' - if it
    // didn't, "V2.2.0" would parse as 0.0.0 and read as an incorrect
    // downgrade against a real running version.
    TEST_ASSERT_GREATER_THAN(0, compare("V2.2.0", "2.1.0"));
}

// ============================================================================
// Leading zeros (real device job payloads use "00.12.31" style)
// ============================================================================

void test_leading_zeros_equal(void) {
    TEST_ASSERT_EQUAL(0, compare("00.12.31", "00.12.31"));
}

void test_leading_zeros_greater(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("00.12.31", "00.12.30"));
}

// ============================================================================
// Trailing garbage / suffixes (must parse the leading numeric part only)
// ============================================================================

void test_suffix_rc_tag(void) {
    TEST_ASSERT_EQUAL(0, compare("2.1.0-rc1", "2.1.0"));
}

void test_suffix_dev_marker(void) {
    TEST_ASSERT_EQUAL(0, compare("2.1.0 (dev)", "2.1.0"));
}

// ============================================================================
// Partial version strings (missing component(s) default to 0)
// ============================================================================

void test_partial_major_minor_only(void) {
    // "2.1" -> 2.1.0
    TEST_ASSERT_EQUAL(0, compare("2.1", "2.1.0"));
}

void test_partial_major_only(void) {
    // "2" -> 2.0.0
    TEST_ASSERT_EQUAL(0, compare("2", "2.0.0"));
}

void test_extra_trailing_component_ignored(void) {
    // "1.2.3.4" -> parses 1.2.3, trailing ".4" ignored
    TEST_ASSERT_EQUAL(0, compare("1.2.3.4", "1.2.3"));
}

void test_leading_whitespace_ignored(void) {
    // strtol skips leading whitespace on each numeric component, same as
    // scanf's "%d" - " 2.1.0" parses identically to "2.1.0"
    TEST_ASSERT_EQUAL(0, compare(" 2.1.0", "2.1.0"));
}

void test_space_before_dot_stops_parse(void) {
    // "1 . 2 . 3": after parsing major=1, the next char is a space, not
    // '.', so parsing stops there -> 1.0.0, not 1.2.3
    TEST_ASSERT_EQUAL(0, compare("1 . 2 . 3", "1.0.0"));
}

void test_dots_only_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("...", "0.0.0"));
}

void test_plus_sign_prefix_parses(void) {
    // strtol accepts a leading '+' like scanf's "%d" did
    TEST_ASSERT_EQUAL(0, compare("+2.1.0", "2.1.0"));
}

// ============================================================================
// Negative numbers and integer overflow - must clamp, never invoke UB or
// wrap around via subtraction (both were real bugs found in review)
// ============================================================================

void test_negative_major_clamped_to_zero(void) {
    // "-1.2.3" -> major clamps to 0 (versions are non-negative) -> 0.2.3
    TEST_ASSERT_EQUAL(0, compare("-1.2.3", "0.2.3"));
}

void test_negative_vs_positive_is_older(void) {
    TEST_ASSERT_LESS_THAN(0, compare("-1.0.0", "1.0.0"));
}

void test_overflow_major_clamps_not_wraps(void) {
    // A cloud typo like "99999999999.0.0" must not overflow strtol/int
    // (sscanf's "%d" would be undefined behavior here) - it clamps to
    // INT_MAX and compares as a huge-but-defined value.
    TEST_ASSERT_GREATER_THAN(0, compare("99999999999.0.0", "-1.0.0"));
}

void test_int_max_boundary_no_overflow(void) {
    // Old subtraction-based compare (currentMajor - availableMajor) could
    // overflow signed int here; three-way compare cannot.
    TEST_ASSERT_GREATER_THAN(0, compare("2147483647.0.0", "-1.0.0"));
}

void test_int_max_vs_int_max_equal(void) {
    TEST_ASSERT_EQUAL(0, compare("2147483647.0.0", "2147483647.0.0"));
}

// ============================================================================
// Malformed / non-numeric input -> degrades to "0.0.0", never crashes
// ============================================================================

void test_garbage_string_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("abc", "0.0.0"));
}

void test_empty_string_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("", "0.0.0"));
}

void test_garbage_vs_real_version_is_older(void) {
    // A malformed target ("available") degrades to 0.0.0, so it must never
    // be treated as an upgrade over a real, positive "current" version -
    // compare() must report current as newer (>0), matching how the OTA
    // guard uses it: a garbage target version gets rejected as a downgrade,
    // never silently accepted.
    TEST_ASSERT_GREATER_THAN(0, compare("2.1.0", "not-a-version"));
}

void test_whitespace_only_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("   ", "0.0.0"));
}

void test_lone_v_is_zero(void) {
    // "v" with nothing after the prefix
    TEST_ASSERT_EQUAL(0, compare("v", "0.0.0"));
}

void test_words_only_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("banana", "0.0.0"));
}

void test_middle_component_non_numeric_stops_at_major(void) {
    // "1.x.3" -> major=1 parses, then '.', then minor component "x.3" has
    // no leading digit -> minor=0, str left at "x.3" (not '.') -> patch=0
    TEST_ASSERT_EQUAL(0, compare("1.x.3", "1.0.0"));
}

void test_semver_suffix_alpha_plus_build(void) {
    // "1.2.3-alpha+build.5" -> parses 1.2.3, full semver suffix ignored
    TEST_ASSERT_EQUAL(0, compare("1.2.3-alpha+build.5", "1.2.3"));
}

void test_json_looking_garbage_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("{\"version\":\"1.2.3\"}", "0.0.0"));
}

void test_very_long_garbage_string_is_zero(void) {
    // Must not misbehave (crash, hang, huge alloc) on a long non-numeric
    // string - this is a fixed C string, no dynamic allocation involved.
    TEST_ASSERT_EQUAL(0, compare(
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        "0.0.0"));
}

void test_unicode_bytes_is_zero(void) {
    TEST_ASSERT_EQUAL(0, compare("\xC3\xA9\xC3\xA8.1.0", "0.0.0"));
}

// ============================================================================
// Null safety - must not crash, treated as "0.0.0"
// ============================================================================

void test_null_available_is_safe(void) {
    TEST_ASSERT_GREATER_THAN(0, compare("1.0.0", nullptr));
}

void test_null_current_is_safe(void) {
    TEST_ASSERT_LESS_THAN(0, compare(nullptr, "1.0.0"));
}

void test_both_null_is_safe(void) {
    TEST_ASSERT_EQUAL(0, compare(nullptr, nullptr));
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_equal_versions);
    RUN_TEST(test_greater_by_patch);
    RUN_TEST(test_less_by_patch);
    RUN_TEST(test_greater_by_minor);
    RUN_TEST(test_less_by_minor);
    RUN_TEST(test_greater_by_major);
    RUN_TEST(test_less_by_major);

    RUN_TEST(test_double_digit_major_beats_single_digit);
    RUN_TEST(test_double_digit_minor_beats_single_digit);
    RUN_TEST(test_double_digit_patch_beats_single_digit);

    RUN_TEST(test_v_prefix_both_sides);
    RUN_TEST(test_v_prefix_current_only);
    RUN_TEST(test_v_prefix_available_only);
    RUN_TEST(test_capital_v_prefix);
    RUN_TEST(test_capital_v_prefix_not_treated_as_downgrade);

    RUN_TEST(test_leading_zeros_equal);
    RUN_TEST(test_leading_zeros_greater);

    RUN_TEST(test_suffix_rc_tag);
    RUN_TEST(test_suffix_dev_marker);

    RUN_TEST(test_partial_major_minor_only);
    RUN_TEST(test_partial_major_only);
    RUN_TEST(test_extra_trailing_component_ignored);
    RUN_TEST(test_leading_whitespace_ignored);
    RUN_TEST(test_space_before_dot_stops_parse);
    RUN_TEST(test_dots_only_is_zero);
    RUN_TEST(test_plus_sign_prefix_parses);

    RUN_TEST(test_negative_major_clamped_to_zero);
    RUN_TEST(test_negative_vs_positive_is_older);
    RUN_TEST(test_overflow_major_clamps_not_wraps);
    RUN_TEST(test_int_max_boundary_no_overflow);
    RUN_TEST(test_int_max_vs_int_max_equal);

    RUN_TEST(test_garbage_string_is_zero);
    RUN_TEST(test_empty_string_is_zero);
    RUN_TEST(test_garbage_vs_real_version_is_older);
    RUN_TEST(test_whitespace_only_is_zero);
    RUN_TEST(test_lone_v_is_zero);
    RUN_TEST(test_words_only_is_zero);
    RUN_TEST(test_middle_component_non_numeric_stops_at_major);
    RUN_TEST(test_semver_suffix_alpha_plus_build);
    RUN_TEST(test_json_looking_garbage_is_zero);
    RUN_TEST(test_very_long_garbage_string_is_zero);
    RUN_TEST(test_unicode_bytes_is_zero);

    RUN_TEST(test_null_available_is_safe);
    RUN_TEST(test_null_current_is_safe);
    RUN_TEST(test_both_null_is_safe);

    return UNITY_END();
}
