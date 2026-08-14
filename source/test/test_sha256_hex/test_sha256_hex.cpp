// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include <unity.h>
#include "sha256_hex.h"

void setUp(void) {}
void tearDown(void) {}

// Canary-padded output buffer so overwrites past BUFFER_SIZE are caught
static char out[Sha256Hex::BUFFER_SIZE + 4];

static void fillCanary(void) {
    for (size_t i = 0; i < sizeof(out); i++) out[i] = '\x7f';
}

void test_all_zero_digest(void) {
    const uint8_t digest[Sha256Hex::DIGEST_LEN] = {0};
    fillCanary();
    TEST_ASSERT_TRUE(Sha256Hex::bytesToHex(digest, out, Sha256Hex::BUFFER_SIZE));
    TEST_ASSERT_EQUAL_STRING(
        "0000000000000000000000000000000000000000000000000000000000000000", out);
}

void test_all_ff_digest(void) {
    uint8_t digest[Sha256Hex::DIGEST_LEN];
    for (size_t i = 0; i < sizeof(digest); i++) digest[i] = 0xFF;
    fillCanary();
    TEST_ASSERT_TRUE(Sha256Hex::bytesToHex(digest, out, Sha256Hex::BUFFER_SIZE));
    TEST_ASSERT_EQUAL_STRING(
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", out);
}

void test_known_pattern_lowercase_and_order(void) {
    // 0x01, 0x23, 0x45, ... nibble pattern repeated: catches nibble swaps,
    // uppercase output, and byte-order mistakes in one vector
    uint8_t digest[Sha256Hex::DIGEST_LEN];
    const uint8_t pattern[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    for (size_t i = 0; i < sizeof(digest); i++) digest[i] = pattern[i % 8];
    fillCanary();
    TEST_ASSERT_TRUE(Sha256Hex::bytesToHex(digest, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", out);
}

void test_no_write_past_terminator(void) {
    const uint8_t digest[Sha256Hex::DIGEST_LEN] = {0};
    fillCanary();
    TEST_ASSERT_TRUE(Sha256Hex::bytesToHex(digest, out, sizeof(out)));
    for (size_t i = Sha256Hex::BUFFER_SIZE; i < sizeof(out); i++) {
        TEST_ASSERT_EQUAL_CHAR('\x7f', out[i]);
    }
}

void test_buffer_too_small_rejected_untouched(void) {
    const uint8_t digest[Sha256Hex::DIGEST_LEN] = {0};
    fillCanary();
    TEST_ASSERT_FALSE(Sha256Hex::bytesToHex(digest, out, Sha256Hex::BUFFER_SIZE - 1));
    TEST_ASSERT_EQUAL_CHAR('\x7f', out[0]); // nothing written
}

void test_null_output_rejected(void) {
    const uint8_t digest[Sha256Hex::DIGEST_LEN] = {0};
    TEST_ASSERT_FALSE(Sha256Hex::bytesToHex(digest, nullptr, Sha256Hex::BUFFER_SIZE));
}

void test_null_digest_rejected(void) {
    fillCanary();
    TEST_ASSERT_FALSE(Sha256Hex::bytesToHex(nullptr, out, Sha256Hex::BUFFER_SIZE));
    TEST_ASSERT_EQUAL_CHAR('\x7f', out[0]);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_all_zero_digest);
    RUN_TEST(test_all_ff_digest);
    RUN_TEST(test_known_pattern_lowercase_and_order);
    RUN_TEST(test_no_write_past_terminator);
    RUN_TEST(test_buffer_too_small_rejected_untouched);
    RUN_TEST(test_null_output_rejected);
    RUN_TEST(test_null_digest_rejected);
    return UNITY_END();
}
