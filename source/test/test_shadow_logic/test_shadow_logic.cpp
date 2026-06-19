// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the pure shadow logic. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)
//
// These need no hardware and no JSON library: they exercise the log-level name
// mapping, the transient/persistent classification, the channel-key bounds
// check, the client-token formatter and the version gate entirely in memory.

#include <unity.h>
#include <cstring>
#include "shadow_logic.h"

using namespace ShadowLogic;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// logLevelFromString
// ============================================================================

void test_loglevel_from_string_all_valid(void) {
    TEST_ASSERT_EQUAL_INT(0, logLevelFromString("VERBOSE"));
    TEST_ASSERT_EQUAL_INT(1, logLevelFromString("DEBUG"));
    TEST_ASSERT_EQUAL_INT(2, logLevelFromString("INFO"));
    TEST_ASSERT_EQUAL_INT(3, logLevelFromString("WARNING"));
    TEST_ASSERT_EQUAL_INT(4, logLevelFromString("ERROR"));
    TEST_ASSERT_EQUAL_INT(5, logLevelFromString("FATAL"));
}

void test_loglevel_from_string_rejects_unknown(void) {
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString("TRACE"));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString("WARN"));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString(""));
}

void test_loglevel_from_string_is_case_sensitive(void) {
    // The cloud contract and existing firmware parser both use strict uppercase.
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString("info"));
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString("Debug"));
}

void test_loglevel_from_string_null_is_invalid(void) {
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INVALID, logLevelFromString(nullptr));
}

// ============================================================================
// logLevelToString
// ============================================================================

void test_loglevel_to_string_all_valid(void) {
    TEST_ASSERT_EQUAL_STRING("VERBOSE", logLevelToString(0));
    TEST_ASSERT_EQUAL_STRING("DEBUG", logLevelToString(1));
    TEST_ASSERT_EQUAL_STRING("INFO", logLevelToString(2));
    TEST_ASSERT_EQUAL_STRING("WARNING", logLevelToString(3));
    TEST_ASSERT_EQUAL_STRING("ERROR", logLevelToString(4));
    TEST_ASSERT_EQUAL_STRING("FATAL", logLevelToString(5));
}

void test_loglevel_to_string_out_of_range_is_null(void) {
    TEST_ASSERT_NULL(logLevelToString(-1));
    TEST_ASSERT_NULL(logLevelToString(6));
    TEST_ASSERT_NULL(logLevelToString(100));
}

void test_loglevel_round_trip(void) {
    for (int i = 0; i <= 5; i++) {
        TEST_ASSERT_EQUAL_INT(i, logLevelFromString(logLevelToString(i)));
    }
}

// ============================================================================
// isTransientLogLevel
// ============================================================================

void test_transient_levels(void) {
    TEST_ASSERT_TRUE(isTransientLogLevel(0));  // VERBOSE
    TEST_ASSERT_TRUE(isTransientLogLevel(1));  // DEBUG
}

void test_persistent_levels(void) {
    TEST_ASSERT_FALSE(isTransientLogLevel(2)); // INFO
    TEST_ASSERT_FALSE(isTransientLogLevel(3)); // WARNING
    TEST_ASSERT_FALSE(isTransientLogLevel(4)); // ERROR
    TEST_ASSERT_FALSE(isTransientLogLevel(5)); // FATAL
}

void test_transient_out_of_range_is_false(void) {
    TEST_ASSERT_FALSE(isTransientLogLevel(-1));
    TEST_ASSERT_FALSE(isTransientLogLevel(6));
}

// ============================================================================
// parseChannelIndex
// ============================================================================

void test_channel_index_valid_bounds(void) {
    uint8_t idx = 255;
    TEST_ASSERT_TRUE(parseChannelIndex("0", 17, &idx));
    TEST_ASSERT_EQUAL_UINT8(0, idx);
    TEST_ASSERT_TRUE(parseChannelIndex("16", 17, &idx));
    TEST_ASSERT_EQUAL_UINT8(16, idx);
    TEST_ASSERT_TRUE(parseChannelIndex("5", 17, &idx));
    TEST_ASSERT_EQUAL_UINT8(5, idx);
}

void test_channel_index_out_of_range(void) {
    uint8_t idx = 255;
    TEST_ASSERT_FALSE(parseChannelIndex("17", 17, &idx));   // == count
    TEST_ASSERT_FALSE(parseChannelIndex("99", 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex("100000", 17, &idx)); // would overflow if unguarded
    TEST_ASSERT_EQUAL_UINT8(255, idx);                       // untouched on failure
}

void test_channel_index_non_numeric(void) {
    uint8_t idx = 255;
    TEST_ASSERT_FALSE(parseChannelIndex("abc", 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex("3x", 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex("-1", 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex(" 5", 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex("", 17, &idx));
}

void test_channel_index_null_args(void) {
    uint8_t idx = 255;
    TEST_ASSERT_FALSE(parseChannelIndex(nullptr, 17, &idx));
    TEST_ASSERT_FALSE(parseChannelIndex("5", 17, nullptr));
}

// ============================================================================
// formatClientToken
// ============================================================================

void test_client_token_format(void) {
    char buf[24];
    size_t n = formatClientToken(buf, sizeof(buf), 0x12345678u, 0x9abcdef0u);
    TEST_ASSERT_EQUAL_size_t(16, n);
    TEST_ASSERT_EQUAL_STRING("123456789abcdef0", buf);
}

void test_client_token_zero_padding(void) {
    char buf[24];
    formatClientToken(buf, sizeof(buf), 0x1u, 0x0u);
    TEST_ASSERT_EQUAL_STRING("0000000100000000", buf);
    TEST_ASSERT_EQUAL_size_t(16, strlen(buf));
}

void test_client_token_buffer_too_small(void) {
    char buf[16];  // one short of the 17 needed
    TEST_ASSERT_EQUAL_size_t(0, formatClientToken(buf, sizeof(buf), 1u, 2u));
    TEST_ASSERT_EQUAL_size_t(0, formatClientToken(nullptr, 24, 1u, 2u));
}

// ============================================================================
// shouldSendVersion
// ============================================================================

void test_should_send_version(void) {
    TEST_ASSERT_FALSE(shouldSendVersion(0));
    TEST_ASSERT_TRUE(shouldSendVersion(1));
    TEST_ASSERT_TRUE(shouldSendVersion(4294967295u));
}

// ============================================================================
// extractExecutionId
// ============================================================================

void test_exec_id_basic(void) {
    char id[64];
    TEST_ASSERT_TRUE(extractExecutionId(
        "$aws/commands/things/907069875394/executions/abc123/request/json", id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("abc123", id);
}

void test_exec_id_uuid(void) {
    char id[64];
    TEST_ASSERT_TRUE(extractExecutionId(
        "$aws/commands/things/dev/executions/7f3e-1a2b-99cd/request/cbor", id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("7f3e-1a2b-99cd", id);
}

void test_exec_id_malformed(void) {
    char id[64];
    TEST_ASSERT_FALSE(extractExecutionId("$aws/commands/things/dev/no-exec-segment", id, sizeof(id)));
    TEST_ASSERT_FALSE(extractExecutionId("$aws/commands/things/dev/executions/", id, sizeof(id))); // empty id, no trailing /
    TEST_ASSERT_FALSE(extractExecutionId("$aws/commands/things/dev/executions//request/json", id, sizeof(id))); // empty id
    TEST_ASSERT_FALSE(extractExecutionId(nullptr, id, sizeof(id)));
}

void test_exec_id_truncation_rejected(void) {
    char id[5]; // too small for "abc123" (6 chars + null)
    TEST_ASSERT_FALSE(extractExecutionId(
        "$aws/commands/things/dev/executions/abc123/request/json", id, sizeof(id)));
}

// ============================================================================
// isCommandStale
// ============================================================================

void test_command_stale(void) {
    TEST_ASSERT_TRUE(isCommandStale(1000, 1000 + 301, 300));  // 301s old, 5-min limit
    TEST_ASSERT_FALSE(isCommandStale(1000, 1000 + 299, 300)); // 299s old
    TEST_ASSERT_FALSE(isCommandStale(1000, 1000 + 300, 300)); // exactly at limit (not over)
}

void test_command_stale_no_timestamp(void) {
    TEST_ASSERT_FALSE(isCommandStale(0, 1781650000, 300)); // no timestamp -> cannot judge
}

void test_command_stale_clock_skew(void) {
    TEST_ASSERT_FALSE(isCommandStale(2000, 1000, 300)); // created "in the future" -> not stale
    TEST_ASSERT_FALSE(isCommandStale(1000, 1000, 300)); // same instant
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_loglevel_from_string_all_valid);
    RUN_TEST(test_loglevel_from_string_rejects_unknown);
    RUN_TEST(test_loglevel_from_string_is_case_sensitive);
    RUN_TEST(test_loglevel_from_string_null_is_invalid);

    RUN_TEST(test_loglevel_to_string_all_valid);
    RUN_TEST(test_loglevel_to_string_out_of_range_is_null);
    RUN_TEST(test_loglevel_round_trip);

    RUN_TEST(test_transient_levels);
    RUN_TEST(test_persistent_levels);
    RUN_TEST(test_transient_out_of_range_is_false);

    RUN_TEST(test_channel_index_valid_bounds);
    RUN_TEST(test_channel_index_out_of_range);
    RUN_TEST(test_channel_index_non_numeric);
    RUN_TEST(test_channel_index_null_args);

    RUN_TEST(test_client_token_format);
    RUN_TEST(test_client_token_zero_padding);
    RUN_TEST(test_client_token_buffer_too_small);

    RUN_TEST(test_should_send_version);

    RUN_TEST(test_exec_id_basic);
    RUN_TEST(test_exec_id_uuid);
    RUN_TEST(test_exec_id_malformed);
    RUN_TEST(test_exec_id_truncation_rejected);

    RUN_TEST(test_command_stale);
    RUN_TEST(test_command_stale_no_timestamp);
    RUN_TEST(test_command_stale_clock_skew);

    return UNITY_END();
}
