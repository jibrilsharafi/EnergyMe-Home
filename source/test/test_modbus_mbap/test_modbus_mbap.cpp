// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi
//
// Host unit tests for the Modbus TCP MBAP header check. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "modbus_mbap.h"

using namespace ModbusMbap;

void setUp(void) {}
void tearDown(void) {}

// Read holding registers, unit 1, address 0, count 8: length field 6
static const uint8_t VALID_READ[MIN_FRAME_SIZE] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03};

static void header(uint8_t out[MIN_FRAME_SIZE], uint8_t lenHi, uint8_t lenLo) {
    for (size_t i = 0; i < MIN_FRAME_SIZE; i++) out[i] = VALID_READ[i];
    out[4] = lenHi;
    out[5] = lenLo;
}

void test_valid_read_request(void) {
    size_t length = 0;
    TEST_ASSERT_EQUAL(HeaderCheck::OK, checkHeader(VALID_READ, &length));
    TEST_ASSERT_EQUAL_UINT32(12, length);
}

// The frames that hung async_tcp in eModbus: shorter than the 8 bytes already buffered
void test_length_zero_rejected(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x00, 0x00);
    size_t length = 0;
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_LENGTH, checkHeader(h, &length));
    TEST_ASSERT_EQUAL_UINT32(6, length);
}

void test_length_one_rejected(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x00, 0x01);
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_LENGTH, checkHeader(h, nullptr));
}

void test_minimum_length_accepted(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x00, 0x02); // unit id + function code only
    size_t length = 0;
    TEST_ASSERT_EQUAL(HeaderCheck::OK, checkHeader(h, &length));
    TEST_ASSERT_EQUAL_UINT32(MIN_FRAME_SIZE, length);
}

void test_maximum_length_accepted(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x01, 0x00); // 256
    size_t length = 0;
    TEST_ASSERT_EQUAL(HeaderCheck::OK, checkHeader(h, &length));
    TEST_ASSERT_EQUAL_UINT32(MAX_FRAME_SIZE, length);
}

void test_over_maximum_length_rejected(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x01, 0x01);
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_LENGTH, checkHeader(h, nullptr));
    header(h, 0xFF, 0xFF);
    size_t length = 0;
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_LENGTH, checkHeader(h, &length));
    TEST_ASSERT_EQUAL_UINT32(0xFFFF + MBAP_SIZE, length);
}

void test_non_modbus_protocol_rejected(void) {
    uint8_t h[MIN_FRAME_SIZE];
    header(h, 0x00, 0x06);
    h[3] = 0x01;
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_PROTOCOL, checkHeader(h, nullptr));
    header(h, 0x00, 0x06);
    h[2] = 0x47; // "GET " from an HTTP client on the wrong port
    TEST_ASSERT_EQUAL(HeaderCheck::BAD_PROTOCOL, checkHeader(h, nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_read_request);
    RUN_TEST(test_length_zero_rejected);
    RUN_TEST(test_length_one_rejected);
    RUN_TEST(test_minimum_length_accepted);
    RUN_TEST(test_maximum_length_accepted);
    RUN_TEST(test_over_maximum_length_rejected);
    RUN_TEST(test_non_modbus_protocol_rejected);
    return UNITY_END();
}
