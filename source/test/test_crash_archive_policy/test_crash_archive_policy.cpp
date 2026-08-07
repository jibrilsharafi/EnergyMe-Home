// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for CrashArchivePolicy. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "crash_archive_policy.h"

using namespace CrashArchivePolicy;

// "energyme/home/v1/<device_id>/crash" with a 12-character device id.
static const size_t CRASH_TOPIC_LENGTH = 34;

// Room the crash message's own JSON needs around the base64 dump: the frozen
// crashInfo object, firmwareVersion, crashId and the field names.
static const size_t TYPICAL_METADATA_BYTES = 1024;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// base64EncodedSize
// ============================================================================

void test_base64_size_of_empty_input(void) {
    TEST_ASSERT_EQUAL_size_t(0, base64EncodedSize(0));
}

void test_base64_size_pads_partial_groups(void) {
    TEST_ASSERT_EQUAL_size_t(4, base64EncodedSize(1));
    TEST_ASSERT_EQUAL_size_t(4, base64EncodedSize(2));
    TEST_ASSERT_EQUAL_size_t(4, base64EncodedSize(3));
    TEST_ASSERT_EQUAL_size_t(8, base64EncodedSize(4));
}

void test_base64_size_of_exact_groups(void) {
    TEST_ASSERT_EQUAL_size_t(4000, base64EncodedSize(3000));
}

void test_base64_size_of_full_coredump_partition(void) {
    // 64 KB partition, the largest raw dump the hardware can ever produce.
    TEST_ASSERT_EQUAL_size_t(87384, base64EncodedSize(65536));
}

void test_base64_size_saturates_instead_of_wrapping(void) {
    // A wrapped result would be small enough to read as "fits", which is the
    // one failure mode the guard must never have.
    TEST_ASSERT_EQUAL_size_t((size_t)-1, base64EncodedSize((size_t)-1));
}

// ============================================================================
// maxPublishPayloadBytes
// ============================================================================

void test_max_payload_leaves_room_for_topic_and_prefix(void) {
    TEST_ASSERT_EQUAL_size_t(65535 - 34 - 2, maxPublishPayloadBytes(CRASH_TOPIC_LENGTH));
}

void test_max_payload_shrinks_as_topic_grows(void) {
    TEST_ASSERT_TRUE(maxPublishPayloadBytes(100) < maxPublishPayloadBytes(10));
}

void test_max_payload_is_zero_when_topic_fills_the_packet(void) {
    TEST_ASSERT_EQUAL_size_t(0, maxPublishPayloadBytes(65535));
    TEST_ASSERT_EQUAL_size_t(0, maxPublishPayloadBytes(70000));
}

// ============================================================================
// fitsPublishLimit - the guard against a truncated remaining-length field
// ============================================================================

void test_typical_gzipped_dump_fits(void) {
    // 48 KB raw compresses to roughly 12 KB, ~16 KB once base64-encoded.
    TEST_ASSERT_TRUE(fitsPublishLimit(12 * 1024, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_worst_case_partition_dump_fits_when_compressed(void) {
    // Even a poorly compressing 64 KB dump landing at 40 KB gzipped stays under.
    TEST_ASSERT_TRUE(fitsPublishLimit(40 * 1024, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_synthetic_large_dump_trips_the_guard(void) {
    // An incompressible 64 KB dump would base64 to 87384 bytes and truncate the
    // 16-bit remaining-length field. This is the case the guard exists for.
    TEST_ASSERT_FALSE(fitsPublishLimit(65536, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_uncompressed_average_dump_trips_the_guard(void) {
    // Guards against a regression that stops compressing: the 48 KB production
    // average is already over the limit raw (65536 base64 bytes).
    TEST_ASSERT_FALSE(fitsPublishLimit(48 * 1024, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_dump_exactly_at_the_budget_fits(void) {
    const size_t budget = maxPublishPayloadBytes(CRASH_TOPIC_LENGTH);
    // 3 raw bytes per 4 encoded, so this encodes to exactly budget - metadata.
    const size_t compressed = ((budget - TYPICAL_METADATA_BYTES) / 4) * 3;
    TEST_ASSERT_TRUE(fitsPublishLimit(compressed, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_one_byte_over_the_budget_trips_the_guard(void) {
    const size_t budget = maxPublishPayloadBytes(CRASH_TOPIC_LENGTH);
    const size_t compressed = ((budget - TYPICAL_METADATA_BYTES) / 4) * 3;
    TEST_ASSERT_FALSE(fitsPublishLimit(compressed + 1, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_metadata_alone_can_exhaust_the_budget(void) {
    TEST_ASSERT_FALSE(fitsPublishLimit(1024, 65535, CRASH_TOPIC_LENGTH));
}

void test_empty_dump_with_metadata_fits(void) {
    TEST_ASSERT_TRUE(fitsPublishLimit(0, TYPICAL_METADATA_BYTES, CRASH_TOPIC_LENGTH));
}

void test_oversized_topic_rejects_everything(void) {
    TEST_ASSERT_FALSE(fitsPublishLimit(0, 0, 65535));
}

void test_saturated_base64_size_does_not_read_as_fitting(void) {
    TEST_ASSERT_FALSE(fitsPublishLimit((size_t)-1, 0, CRASH_TOPIC_LENGTH));
}

// ============================================================================
// canStore
// ============================================================================

void test_record_within_budget_can_be_stored(void) {
    TEST_ASSERT_TRUE(canStore(20 * 1024, 500 * 1024));
}

void test_record_exactly_at_budget_can_be_stored(void) {
    TEST_ASSERT_TRUE(canStore(500 * 1024, 500 * 1024));
}

void test_record_larger_than_budget_cannot_be_stored(void) {
    TEST_ASSERT_FALSE(canStore(500 * 1024 + 1, 500 * 1024));
}

// ============================================================================
// evictionCount
// ============================================================================

void test_no_eviction_when_archive_is_empty(void) {
    TEST_ASSERT_EQUAL_size_t(0, evictionCount(nullptr, 0, 20 * 1024, 10, 500 * 1024));
}

void test_no_eviction_when_both_caps_have_room(void) {
    const size_t records[] = {20 * 1024, 20 * 1024};
    TEST_ASSERT_EQUAL_size_t(0, evictionCount(records, 2, 20 * 1024, 10, 500 * 1024));
}

void test_evicts_oldest_when_record_cap_is_reached(void) {
    const size_t records[] = {1024, 1024, 1024};
    TEST_ASSERT_EQUAL_size_t(1, evictionCount(records, 3, 1024, 3, 500 * 1024));
}

void test_evicts_only_as_many_as_the_byte_cap_needs(void) {
    // 100 KB used, 60 KB incoming, 128 KB budget: dropping the oldest 40 KB
    // leaves 120 KB, which fits, so the second record is kept.
    const size_t records[] = {40 * 1024, 30 * 1024, 30 * 1024};
    TEST_ASSERT_EQUAL_size_t(1, evictionCount(records, 3, 60 * 1024, 10, 128 * 1024));
}

void test_evicts_repeatedly_until_the_byte_cap_is_met(void) {
    // Same archive, 100 KB budget: 120 KB still overflows after one eviction,
    // so a second is needed to reach 90 KB.
    const size_t records[] = {40 * 1024, 30 * 1024, 30 * 1024};
    TEST_ASSERT_EQUAL_size_t(2, evictionCount(records, 3, 60 * 1024, 10, 100 * 1024));
}

void test_evicts_everything_when_incoming_needs_the_whole_budget(void) {
    const size_t records[] = {10 * 1024, 10 * 1024};
    TEST_ASSERT_EQUAL_size_t(2, evictionCount(records, 2, 64 * 1024, 10, 64 * 1024));
}

void test_evicts_everything_when_incoming_can_never_fit(void) {
    // Caller is expected to check canStore() and refuse; evicting all is the
    // safe answer either way.
    const size_t records[] = {10 * 1024, 10 * 1024};
    TEST_ASSERT_EQUAL_size_t(2, evictionCount(records, 2, 1024 * 1024, 10, 64 * 1024));
}

void test_zero_record_cap_evicts_everything(void) {
    const size_t records[] = {1024};
    TEST_ASSERT_EQUAL_size_t(1, evictionCount(records, 1, 1024, 0, 500 * 1024));
}

void test_single_record_cap_always_evicts_the_previous(void) {
    const size_t records[] = {1024};
    TEST_ASSERT_EQUAL_size_t(1, evictionCount(records, 1, 1024, 1, 500 * 1024));
}

void test_null_records_with_nonzero_count_is_safe(void) {
    TEST_ASSERT_EQUAL_size_t(0, evictionCount(nullptr, 5, 20 * 1024, 10, 500 * 1024));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_base64_size_of_empty_input);
    RUN_TEST(test_base64_size_pads_partial_groups);
    RUN_TEST(test_base64_size_of_exact_groups);
    RUN_TEST(test_base64_size_of_full_coredump_partition);
    RUN_TEST(test_base64_size_saturates_instead_of_wrapping);

    RUN_TEST(test_max_payload_leaves_room_for_topic_and_prefix);
    RUN_TEST(test_max_payload_shrinks_as_topic_grows);
    RUN_TEST(test_max_payload_is_zero_when_topic_fills_the_packet);

    RUN_TEST(test_typical_gzipped_dump_fits);
    RUN_TEST(test_worst_case_partition_dump_fits_when_compressed);
    RUN_TEST(test_synthetic_large_dump_trips_the_guard);
    RUN_TEST(test_uncompressed_average_dump_trips_the_guard);
    RUN_TEST(test_dump_exactly_at_the_budget_fits);
    RUN_TEST(test_one_byte_over_the_budget_trips_the_guard);
    RUN_TEST(test_metadata_alone_can_exhaust_the_budget);
    RUN_TEST(test_empty_dump_with_metadata_fits);
    RUN_TEST(test_oversized_topic_rejects_everything);
    RUN_TEST(test_saturated_base64_size_does_not_read_as_fitting);

    RUN_TEST(test_record_within_budget_can_be_stored);
    RUN_TEST(test_record_exactly_at_budget_can_be_stored);
    RUN_TEST(test_record_larger_than_budget_cannot_be_stored);

    RUN_TEST(test_no_eviction_when_archive_is_empty);
    RUN_TEST(test_no_eviction_when_both_caps_have_room);
    RUN_TEST(test_evicts_oldest_when_record_cap_is_reached);
    RUN_TEST(test_evicts_only_as_many_as_the_byte_cap_needs);
    RUN_TEST(test_evicts_repeatedly_until_the_byte_cap_is_met);
    RUN_TEST(test_evicts_everything_when_incoming_needs_the_whole_budget);
    RUN_TEST(test_evicts_everything_when_incoming_can_never_fit);
    RUN_TEST(test_zero_record_cap_evicts_everything);
    RUN_TEST(test_single_record_cap_always_evicts_the_previous);
    RUN_TEST(test_null_records_with_nonzero_count_is_safe);

    return UNITY_END();
}
