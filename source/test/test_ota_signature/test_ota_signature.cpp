// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for OtaSignature::decodeAndValidate. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)
//
// Covers the "signature field absent or malformed" spec scenario (fail fast,
// before a firmware download starts). Does NOT cover cryptographic verification
// (mbedtls_pk_verify against the downloaded firmware's hash) - that's ESP32-only
// code in Mqtt::_verifyOtaSignature (mqtt.cpp), exercised by hardware/e2e tests
// (tasks.md 6.3-6.5), not by these host tests.

#include <unity.h>
#include "ota_signature.h"

using namespace OtaSignature;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Valid input
// ============================================================================

void test_valid_real_signature_decodes(void) {
    // A real ECDSA P-256 / SHA-256 signature (openssl dgst -sign) over an
    // arbitrary fixture message - 70 bytes of DER SEQUENCE{INTEGER, INTEGER}.
    const char *base64Sig = "MEQCIC3xkav1nWoKKUCKsosnsJcuqTeuL8KgBiIFPcnWtKaPAiAk+M1B9jiU6Sr3gIxNN/JgS2vNQex/xLqZer/LEFVOdA==";
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;

    TEST_ASSERT_TRUE(decodeAndValidate(base64Sig, out, sizeof(out), outLen));
    TEST_ASSERT_EQUAL(70, outLen);
    TEST_ASSERT_EQUAL_HEX8(0x30, out[0]);  // SEQUENCE tag
    TEST_ASSERT_EQUAL_HEX8(0x44, out[1]);  // content length (68) = outLen - 2
}

void test_valid_real_kms_signature_decodes(void) {
    // A real signature straight off the DEPLOYED dev pipeline: produced by
    // `aws kms sign --key-id alias/energyme-home-dev-ota-signing
    //  --signing-algorithm ECDSA_SHA_256 --message-type DIGEST` over the SHA-256
    // of an esp32s3-dev firmware.bin, exactly what the OTA job document carries.
    // KMS emits a 71-byte DER SEQUENCE (0x30 0x45) - a 32-byte r plus a 33-byte s
    // (leading 0x00 pad) - which this decoder must accept as-is and hand to
    // mbedtls_pk_verify. Ties the host suite to real pipeline output, not just
    // openssl-generated fixtures. The full cryptographic verify of this same
    // vector is proven offline (openssl dgst -verify against the dev public key,
    // tasks.md 13.6) and on-device (Mqtt::_verifyOtaSignature, tasks.md 13.7).
    const char *base64Sig =
        "MEUCIH4qC7fTmUrvKui6RtP3hla26q83G0rjLxA4j9O8ZnXAAiEA+9NxuTNVdy1RPjp991hMJWzuCX5FWXUJSzbVHC/VBlQ=";
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;

    TEST_ASSERT_TRUE(decodeAndValidate(base64Sig, out, sizeof(out), outLen));
    TEST_ASSERT_EQUAL(71, outLen);
    TEST_ASSERT_EQUAL_HEX8(0x30, out[0]);  // SEQUENCE tag
    TEST_ASSERT_EQUAL_HEX8(0x45, out[1]);  // content length (69) = outLen - 2
}

void test_valid_minimal_der_decodes(void) {
    // 30 06 02 01 05 02 01 07 - smallest well-formed SEQUENCE{INTEGER,INTEGER},
    // exactly at the 8-byte minimum this parser accepts.
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;

    TEST_ASSERT_TRUE(decodeAndValidate("MAYCAQUCAQc=", out, sizeof(out), outLen));
    TEST_ASSERT_EQUAL(8, outLen);
}

// ============================================================================
// Missing / empty input
// ============================================================================

void test_null_input_rejected(void) {
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 123;
    TEST_ASSERT_FALSE(decodeAndValidate(nullptr, out, sizeof(out), outLen));
    TEST_ASSERT_EQUAL(0, outLen);
}

void test_empty_input_rejected(void) {
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 123;
    TEST_ASSERT_FALSE(decodeAndValidate("", out, sizeof(out), outLen));
    TEST_ASSERT_EQUAL(0, outLen);
}

// ============================================================================
// Malformed base64
// ============================================================================

void test_non_multiple_of_4_base64_rejected(void) {
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("QQ", out, sizeof(out), outLen));
}

void test_invalid_base64_char_rejected(void) {
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("AB!D", out, sizeof(out), outLen));
}

void test_too_long_base64_input_rejected(void) {
    // 108 'A' characters (multiple of 4) - past MAX_BASE64_SIGNATURE_INPUT_LEN,
    // rejected on length alone before any decode attempt.
    char longInput[MAX_BASE64_SIGNATURE_INPUT_LEN + 8 + 1];
    size_t len = MAX_BASE64_SIGNATURE_INPUT_LEN + 4;  // still a multiple of 4, still too long
    for (size_t i = 0; i < len; i++) longInput[i] = 'A';
    longInput[len] = '\0';

    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate(longInput, out, sizeof(out), outLen));
}

// ============================================================================
// Well-formed base64, malformed DER shape
// ============================================================================

void test_wrong_outer_tag_rejected(void) {
    // 31 06 02 01 05 02 01 07 - outer tag 0x31 instead of SEQUENCE (0x30)
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("MQYCAQUCAQc=", out, sizeof(out), outLen));
}

void test_trailing_garbage_rejected(void) {
    // 30 06 02 01 05 02 01 07 FF - a valid 8-byte SEQUENCE plus one extra byte
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("MAYCAQUCAQf/", out, sizeof(out), outLen));
}

void test_zero_length_integer_rejected(void) {
    // 30 04 02 00 02 01 05 - first INTEGER (r) has zero-length content
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("MAQCAAIBBQ==", out, sizeof(out), outLen));
}

void test_sequence_length_overclaims_rejected(void) {
    // 30 08 02 01 05 02 01 07 - outer SEQUENCE declares 8 content bytes but only
    // 6 are actually present in the 8-byte buffer
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("MAgCAQUCAQc=", out, sizeof(out), outLen));
}

void test_missing_second_integer_rejected(void) {
    // 30 03 02 01 05 - only one INTEGER present, no second (s) value
    uint8_t out[MAX_DER_SIGNATURE_LEN];
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate("MAMCAQU=", out, sizeof(out), outLen));
}

void test_oversized_but_structurally_valid_der_rejected(void) {
    // A structurally well-formed SEQUENCE{INTEGER(37), INTEGER(37)} = 80 bytes
    // total, past MAX_DER_SIGNATURE_LEN (74) - a well-formed shape does not
    // exempt it from the size cap.
    const char *base64Sig =
        "ME4CJRERERERERERERERERERERERERERERERERERERERERERERERAiUiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIiIi";
    uint8_t out[MAX_DER_SIGNATURE_LEN + 16];  // large enough capacity - the length check itself must reject it
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate(base64Sig, out, sizeof(out), outLen));
}

// ============================================================================
// Output capacity
// ============================================================================

void test_output_capacity_too_small_rejected(void) {
    const char *base64Sig = "MEQCIC3xkav1nWoKKUCKsosnsJcuqTeuL8KgBiIFPcnWtKaPAiAk+M1B9jiU6Sr3gIxNN/JgS2vNQex/xLqZer/LEFVOdA==";
    uint8_t out[8];  // real signature decodes to 70 bytes, capacity is far too small
    size_t outLen = 0;
    TEST_ASSERT_FALSE(decodeAndValidate(base64Sig, out, sizeof(out), outLen));
}

// ============================================================================
// Runner
// ============================================================================

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_valid_real_signature_decodes);
    RUN_TEST(test_valid_real_kms_signature_decodes);
    RUN_TEST(test_valid_minimal_der_decodes);

    RUN_TEST(test_null_input_rejected);
    RUN_TEST(test_empty_input_rejected);

    RUN_TEST(test_non_multiple_of_4_base64_rejected);
    RUN_TEST(test_invalid_base64_char_rejected);
    RUN_TEST(test_too_long_base64_input_rejected);

    RUN_TEST(test_wrong_outer_tag_rejected);
    RUN_TEST(test_trailing_garbage_rejected);
    RUN_TEST(test_zero_length_integer_rejected);
    RUN_TEST(test_sequence_length_overclaims_rejected);
    RUN_TEST(test_missing_second_integer_rejected);
    RUN_TEST(test_oversized_but_structurally_valid_der_rejected);

    RUN_TEST(test_output_capacity_too_small_rejected);

    return UNITY_END();
}
