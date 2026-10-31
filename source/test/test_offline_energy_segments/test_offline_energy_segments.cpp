// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for OfflineEnergySegments. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "offline_energy_segments.h"

using namespace OfflineEnergySegments;

static const uint32_t HOUR = 1793318400; // 2026-10-30 00:00:00 UTC, hour-aligned
static const uint32_t DAY = 24 * 3600;
static const uint32_t MAX_AGE = 14 * DAY;
static const size_t LARGE = 100 * 1024 * 1024;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Hour and age arithmetic
// ============================================================================

void test_segment_hour_floors_to_hour(void) {
    TEST_ASSERT_EQUAL_UINT32(HOUR, segmentHour(HOUR));
    TEST_ASSERT_EQUAL_UINT32(HOUR, segmentHour(HOUR + 3599));
    TEST_ASSERT_EQUAL_UINT32(HOUR + 3600, segmentHour(HOUR + 3600));
}

void test_segment_expires_only_when_its_newest_reading_is_too_old(void) {
    uint64_t lastSecondKept = (uint64_t)HOUR + 3600 + MAX_AGE - 1;
    TEST_ASSERT_FALSE(isExpired(HOUR, lastSecondKept, MAX_AGE));
    TEST_ASSERT_TRUE(isExpired(HOUR, lastSecondKept + 1, MAX_AGE));
}

void test_reading_freshness_boundary(void) {
    uint64_t now = (uint64_t)HOUR + MAX_AGE;
    TEST_ASSERT_TRUE(isReadingFresh((uint64_t)HOUR * 1000, now, MAX_AGE));
    TEST_ASSERT_FALSE(isReadingFresh((uint64_t)HOUR * 1000 - 1, now, MAX_AGE));
}

// ============================================================================
// Trim plan
// ============================================================================

void test_nothing_to_drop_when_within_limits(void) {
    Segment segs[] = {{HOUR, 1000}, {HOUR + 3600, 1000}};
    TrimPlan p = planTrim(segs, 2, HOUR + 3600, HOUR + 3700, MAX_AGE, 174, 10000, LARGE, 512);
    TEST_ASSERT_EQUAL_UINT32(0, p.dropCount);
    TEST_ASSERT_TRUE(p.canAppend);
}

void test_expired_segments_dropped_first(void) {
    Segment segs[] = {{HOUR, 1000}, {HOUR + 3600, 1000}, {HOUR + 15 * DAY, 100}};
    uint64_t now = (uint64_t)HOUR + 15 * DAY + 10;
    TrimPlan p = planTrim(segs, 3, HOUR + 15 * DAY, now, MAX_AGE, 174, LARGE, LARGE, 512);
    TEST_ASSERT_EQUAL_UINT32(2, p.dropCount);
    TEST_ASSERT_TRUE(p.canAppend);
}

void test_budget_drops_oldest_until_record_fits(void) {
    Segment segs[] = {{HOUR, 1000}, {HOUR + 3600, 1000}, {HOUR + 7200, 1000}};
    TrimPlan p = planTrim(segs, 3, HOUR + 7200, HOUR + 7300, MAX_AGE, 200, 2200, LARGE, 512);
    TEST_ASSERT_EQUAL_UINT32(1, p.dropCount);
    TEST_ASSERT_TRUE(p.canAppend);
}

void test_headroom_drops_oldest_until_free_space_suffices(void) {
    Segment segs[] = {{HOUR, 1000}, {HOUR + 3600, 1000}, {HOUR + 7200, 1000}};
    // 600 free, need 512 + 200: one 1000-byte drop restores it
    TrimPlan p = planTrim(segs, 3, HOUR + 7200, HOUR + 7300, MAX_AGE, 200, LARGE, 600, 512);
    TEST_ASSERT_EQUAL_UINT32(1, p.dropCount);
    TEST_ASSERT_TRUE(p.canAppend);
}

void test_append_segment_never_dropped_for_space(void) {
    Segment segs[] = {{HOUR, 1000}, {HOUR + 3600, 5000}};
    TrimPlan p = planTrim(segs, 2, HOUR + 3600, HOUR + 3700, MAX_AGE, 200, 4000, LARGE, 512);
    TEST_ASSERT_EQUAL_UINT32(1, p.dropCount);
    TEST_ASSERT_FALSE(p.canAppend);
}

void test_no_segments_and_no_space_cannot_append(void) {
    TrimPlan p = planTrim(nullptr, 0, HOUR, HOUR, MAX_AGE, 200, LARGE, 100, 512);
    TEST_ASSERT_EQUAL_UINT32(0, p.dropCount);
    TEST_ASSERT_FALSE(p.canAppend);
}

// ============================================================================
// File names
// ============================================================================

void test_name_round_trip(void) {
    char name[SEGMENT_NAME_BUFFER_SIZE];
    TEST_ASSERT_TRUE(formatName(HOUR, name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("1793318400.bin", name);
    uint32_t hour = 0;
    TEST_ASSERT_TRUE(parseName(name, hour));
    TEST_ASSERT_EQUAL_UINT32(HOUR, hour);
}

void test_parse_rejects_foreign_names(void) {
    uint32_t hour = 0;
    TEST_ASSERT_FALSE(parseName("1793318400.csv", hour));
    TEST_ASSERT_FALSE(parseName("179331840a.bin", hour));
    TEST_ASSERT_FALSE(parseName("1793318401.bin", hour)); // not hour-aligned
    TEST_ASSERT_FALSE(parseName("9999999999.bin", hour)); // beyond uint32
    TEST_ASSERT_FALSE(parseName("tmp.bin", hour));
    TEST_ASSERT_FALSE(parseName(nullptr, hour));
}

void test_format_rejects_small_buffer(void) {
    char name[8];
    TEST_ASSERT_FALSE(formatName(HOUR, name, sizeof(name)));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_segment_hour_floors_to_hour);
    RUN_TEST(test_segment_expires_only_when_its_newest_reading_is_too_old);
    RUN_TEST(test_reading_freshness_boundary);
    RUN_TEST(test_nothing_to_drop_when_within_limits);
    RUN_TEST(test_expired_segments_dropped_first);
    RUN_TEST(test_budget_drops_oldest_until_record_fits);
    RUN_TEST(test_headroom_drops_oldest_until_free_space_suffices);
    RUN_TEST(test_append_segment_never_dropped_for_space);
    RUN_TEST(test_no_segments_and_no_space_cannot_append);
    RUN_TEST(test_name_round_trip);
    RUN_TEST(test_parse_rejects_foreign_names);
    RUN_TEST(test_format_rejects_small_buffer);
    return UNITY_END();
}
