// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for OfflineEnergyRecord. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include <math.h>
#include <string.h>
#include "offline_energy_record.h"

using namespace OfflineEnergyRecord;

static const uint32_t BOUNDARY = 1793318400; // 2026-10-30 00:00:00 UTC
static const uint64_t BOUNDARY_MS = (uint64_t)BOUNDARY * 1000;

void setUp(void) {}
void tearDown(void) {}

static ChannelReading reading(uint8_t channel, int64_t offsetMs, double imported, double exported) {
    return ChannelReading{channel, (uint64_t)((int64_t)BOUNDARY_MS + offsetMs), imported, exported};
}

// ============================================================================
// Round trip
// ============================================================================

void test_single_channel_round_trip(void) {
    ChannelReading in[] = {reading(0, 1234, 12345.67, 0.04)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(recordBytes(1), written);

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(written, decode(buf, written, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT32(BOUNDARY, boundary);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_UINT8(0, out[0].channel);
    TEST_ASSERT_EQUAL_UINT64(BOUNDARY_MS + 1234, out[0].unixTimeMs);
    TEST_ASSERT_EQUAL_UINT32(123457, out[0].activeEnergyImportedDeciWh); // 12345.67 Wh rounds to 12345.7
    TEST_ASSERT_EQUAL_UINT32(0, out[0].activeEnergyExportedDeciWh);
}

void test_sixteen_channels_round_trip_with_negative_offsets(void) {
    ChannelReading in[16];
    for (uint8_t ch = 0; ch < 16; ch++) {
        in[ch] = reading(ch, (int64_t)ch * 1000 - 8000, 1000.0 * ch + 0.1, 10.0 * ch);
    }
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 16, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(174, written);

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(written, decode(buf, written, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT32(16, count);
    for (uint8_t ch = 0; ch < 16; ch++) {
        TEST_ASSERT_EQUAL_UINT8(ch, out[ch].channel);
        TEST_ASSERT_EQUAL_UINT64(BOUNDARY_MS + (int64_t)ch * 1000 - 8000, out[ch].unixTimeMs);
        TEST_ASSERT_EQUAL_UINT32(10000 * ch + 1, out[ch].activeEnergyImportedDeciWh);
        TEST_ASSERT_EQUAL_UINT32(100 * ch, out[ch].activeEnergyExportedDeciWh);
    }
}

void test_sparse_mask_keeps_channel_indices(void) {
    ChannelReading in[] = {reading(2, 0, 1.0, 0.0), reading(9, 0, 2.0, 0.0), reading(16, 0, 3.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 3, buf, sizeof(buf));

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(written, decode(buf, written, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT32(3, count);
    TEST_ASSERT_EQUAL_UINT8(2, out[0].channel);
    TEST_ASSERT_EQUAL_UINT8(9, out[1].channel);
    TEST_ASSERT_EQUAL_UINT8(16, out[2].channel);
    TEST_ASSERT_EQUAL_UINT32(30, out[2].activeEnergyImportedDeciWh);
}

// ============================================================================
// Channels that cannot be stored faithfully are skipped, never altered
// ============================================================================

void test_offset_out_of_range_skips_channel(void) {
    ChannelReading in[] = {reading(0, 0, 1.0, 0.0), reading(1, 40000, 2.0, 0.0), reading(2, -40000, 3.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(recordBytes(1), written);
}

void test_offset_at_i16_limits_is_kept(void) {
    ChannelReading in[] = {reading(0, -32768, 1.0, 0.0), reading(1, 32767, 2.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 2, buf, sizeof(buf));

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(written, decode(buf, written, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT64(BOUNDARY_MS - 32768, out[0].unixTimeMs);
    TEST_ASSERT_EQUAL_UINT64(BOUNDARY_MS + 32767, out[1].unixTimeMs);
}

void test_overflowing_or_invalid_counter_skips_channel(void) {
    ChannelReading in[] = {
        reading(0, 0, 429496729.6, 0.0), // 4294967296 deci-Wh: one past uint32
        reading(1, 0, -0.5, 0.0),
        reading(2, 0, NAN, 0.0),
        reading(3, 0, 429496729.5, 0.0), // exactly UINT32_MAX deci-Wh
    };
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(recordBytes(1), written);

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    decode(buf, written, boundary, out, MAX_CHANNELS, count);
    TEST_ASSERT_EQUAL_UINT8(3, out[0].channel);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, out[0].activeEnergyImportedDeciWh);
}

void test_duplicate_or_descending_channel_skipped(void) {
    ChannelReading in[] = {reading(3, 0, 1.0, 0.0), reading(3, 0, 2.0, 0.0), reading(1, 0, 3.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    TEST_ASSERT_EQUAL_UINT32(recordBytes(1), encode(BOUNDARY, in, 3, buf, sizeof(buf)));
}

void test_no_encodable_channel_writes_nothing(void) {
    ChannelReading in[] = {reading(0, 50000, 1.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    TEST_ASSERT_EQUAL_UINT32(0, encode(BOUNDARY, in, 1, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0, encode(BOUNDARY, in, 0, buf, sizeof(buf)));
}

void test_output_buffer_too_small_writes_nothing(void) {
    ChannelReading in[] = {reading(0, 0, 1.0, 0.0), reading(1, 0, 1.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    TEST_ASSERT_EQUAL_UINT32(0, encode(BOUNDARY, in, 2, buf, recordBytes(2) - 1));
}

// ============================================================================
// Corrupt and torn data
// ============================================================================

void test_torn_tail_is_rejected(void) {
    ChannelReading in[] = {reading(0, 0, 1.0, 0.0), reading(1, 0, 2.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 2, buf, sizeof(buf));

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(0, decode(buf, written - 1, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT32(0, count);
}

void test_crc_mismatch_is_rejected(void) {
    ChannelReading in[] = {reading(0, 0, 1.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 1, buf, sizeof(buf));
    buf[HEADER_BYTES + 3] ^= 0x01;

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(0, decode(buf, written, boundary, out, MAX_CHANNELS, count));
}

void test_capacity_smaller_than_count_is_rejected(void) {
    ChannelReading in[] = {reading(0, 0, 1.0, 0.0), reading(1, 0, 2.0, 0.0)};
    uint8_t buf[MAX_RECORD_BYTES];
    size_t written = encode(BOUNDARY, in, 2, buf, sizeof(buf));

    uint32_t boundary = 0;
    DecodedChannel out[1];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(0, decode(buf, written, boundary, out, 1, count));
}

void test_resync_skips_torn_record_and_keeps_following(void) {
    ChannelReading first[] = {reading(0, 0, 1.0, 0.0), reading(1, 0, 2.0, 0.0)};
    ChannelReading second[] = {reading(0, 60000, 5.0, 0.0)}; // read at the next boundary
    uint8_t stream[2 * MAX_RECORD_BYTES];

    size_t a = encode(BOUNDARY, first, 2, stream, sizeof(stream));
    size_t torn = a - 5; // power lost mid-append: only part of the first record hit flash
    size_t b = encode(BOUNDARY + 60, second, 1, stream + torn, sizeof(stream) - torn);
    TEST_ASSERT_NOT_EQUAL(0, b);
    size_t length = torn + b;

    uint32_t boundary = 0;
    DecodedChannel out[MAX_CHANNELS];
    size_t count = 0;
    TEST_ASSERT_EQUAL_UINT32(0, decode(stream, length, boundary, out, MAX_CHANNELS, count));

    size_t next = findNextMagic(stream, length, 1);
    TEST_ASSERT_EQUAL_UINT32(torn, next);
    TEST_ASSERT_EQUAL_UINT32(b, decode(stream + next, length - next, boundary, out, MAX_CHANNELS, count));
    TEST_ASSERT_EQUAL_UINT32(BOUNDARY + 60, boundary);
    TEST_ASSERT_EQUAL_UINT32(50, out[0].activeEnergyImportedDeciWh);
}

void test_find_next_magic_without_match_returns_length(void) {
    uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), findNextMagic(data, sizeof(data), 0));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_single_channel_round_trip);
    RUN_TEST(test_sixteen_channels_round_trip_with_negative_offsets);
    RUN_TEST(test_sparse_mask_keeps_channel_indices);
    RUN_TEST(test_offset_out_of_range_skips_channel);
    RUN_TEST(test_offset_at_i16_limits_is_kept);
    RUN_TEST(test_overflowing_or_invalid_counter_skips_channel);
    RUN_TEST(test_duplicate_or_descending_channel_skipped);
    RUN_TEST(test_no_encodable_channel_writes_nothing);
    RUN_TEST(test_output_buffer_too_small_writes_nothing);
    RUN_TEST(test_torn_tail_is_rejected);
    RUN_TEST(test_crc_mismatch_is_rejected);
    RUN_TEST(test_capacity_smaller_than_count_is_rejected);
    RUN_TEST(test_resync_skips_torn_record_and_keeps_following);
    RUN_TEST(test_find_next_magic_without_match_returns_length);
    return UNITY_END();
}
