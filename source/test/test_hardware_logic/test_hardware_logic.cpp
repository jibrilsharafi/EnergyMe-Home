// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for the PGA helpers and the mux channel maps of the hardware
// profiles (lib/hardware_logic). Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "hardware_logic.h"
#include "product_line.h"

using namespace HardwareLogic;

void setUp(void) {}
void tearDown(void) {}

// Copies of the muxChannelMap entries in hardware_profile.cpp (first muxChannelCount used).
static const uint8_t MAP_HOME_V61[15] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const uint8_t MAP_HOME_V62[15] = {0, 2, 3, 4, 5, 6, 7, 9, 8, 10, 11, 12, 13, 14, 15};
static const uint8_t MAP_PRO_V10[11] = {15, 14, 13, 12, 11, 2, 8, 1, 9, 0, 10};
static const uint8_t MAP_PRO_V11[11] = {15, 14, 13, 12, 11, 0, 10, 1, 9, 2, 8};

// Physical mux line carrying each CT on the PCB (index = CT number, 0 = not on the mux).
// Taken from the netlists: Home v6.2 (no CT2), Pro v1.1.
static int physicalLineOfCt(const uint8_t* map, size_t count, const int* ctPerLogical, int ct) {
    for (size_t i = 0; i < count; i++) {
        if (ctPerLogical[i] == ct) return map[i];
    }
    return -1;
}

static const int HOME_CT_ORDER[15] = {1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const int PRO_CT_ORDER[11] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

void test_pga_register_validity(void) {
    for (uint8_t r = 0; r <= 4; r++) TEST_ASSERT_TRUE(pgaRegisterIsValid(r));
    for (uint8_t r = 5; r < 8; r++) TEST_ASSERT_FALSE(pgaRegisterIsValid(r));
    TEST_ASSERT_FALSE(pgaRegisterIsValid(255));
}

void test_pga_gain_from_register(void) {
    TEST_ASSERT_EQUAL_UINT8(1, pgaGainFromRegister(0));
    TEST_ASSERT_EQUAL_UINT8(2, pgaGainFromRegister(1)); // 0b001 = x2 (v6.2 / Pro v1.1)
    TEST_ASSERT_EQUAL_UINT8(4, pgaGainFromRegister(2));
    TEST_ASSERT_EQUAL_UINT8(8, pgaGainFromRegister(3));
    TEST_ASSERT_EQUAL_UINT8(16, pgaGainFromRegister(4));
    TEST_ASSERT_EQUAL_UINT8(0, pgaGainFromRegister(5));
}

void test_net_gain_is_unity_with_divider_and_pga2(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, netInputGain(0.5f, 1)); // v6.2 / Pro v1.1 mux inputs
    TEST_ASSERT_EQUAL_FLOAT(1.0f, netInputGain(1.0f, 0)); // v6.1 mux inputs and channel A
}

void test_net_gain_full_scale_matches_jack(void) {
    // ADC full scale at gain 1 is 0.5 V peak; with net gain 1 the jack reaches it at 0.5 V,
    // about 106% of a 333 mV rms CT (0.333 * sqrt(2) = 0.471 V peak).
    const float jackFullScalePeak = 0.5f / netInputGain(0.5f, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, jackFullScalePeak);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.06f, jackFullScalePeak / (0.333f * 1.41421356f));
}

void test_mux_maps_are_valid(void) {
    TEST_ASSERT_TRUE(muxMapIsValid(MAP_HOME_V61, 15, 16));
    TEST_ASSERT_TRUE(muxMapIsValid(MAP_HOME_V62, 15, 16));
    TEST_ASSERT_TRUE(muxMapIsValid(MAP_PRO_V10, 11, 16));
    TEST_ASSERT_TRUE(muxMapIsValid(MAP_PRO_V11, 11, 16));
}

void test_mux_map_rejects_bad_maps(void) {
    const uint8_t dup[3] = {0, 2, 2};
    const uint8_t range[3] = {0, 2, 16};
    TEST_ASSERT_FALSE(muxMapIsValid(dup, 3, 16));
    TEST_ASSERT_FALSE(muxMapIsValid(range, 3, 16));
    TEST_ASSERT_FALSE(muxMapIsValid(dup, 17, 16));
    TEST_ASSERT_FALSE(muxMapIsValid(nullptr, 3, 16));
}

void test_home_v62_differs_from_v61_only_on_y8_y9(void) {
    for (size_t i = 0; i < 15; i++) {
        if (MAP_HOME_V61[i] == 8 || MAP_HOME_V61[i] == 9) continue;
        TEST_ASSERT_EQUAL_UINT8(MAP_HOME_V61[i], MAP_HOME_V62[i]);
    }
    // CT9 and CT10 are logical 7 and 8 (v6.1 convention, no CT2).
    TEST_ASSERT_EQUAL_UINT8(8, MAP_HOME_V61[7]);
    TEST_ASSERT_EQUAL_UINT8(9, MAP_HOME_V61[8]);
    TEST_ASSERT_EQUAL_UINT8(9, MAP_HOME_V62[7]);
    TEST_ASSERT_EQUAL_UINT8(8, MAP_HOME_V62[8]);
}

void test_home_v62_ct_routing(void) {
    const int expectedY[17] = {-1, 0, -1, 2, 3, 4, 5, 6, 7, 9, 8, 10, 11, 12, 13, 14, 15}; // by CT number
    for (int ct = 1; ct <= 16; ct++) {
        if (ct == 2) continue;
        TEST_ASSERT_EQUAL_INT_MESSAGE(expectedY[ct], physicalLineOfCt(MAP_HOME_V62, 15, HOME_CT_ORDER, ct), "Home v6.2 CT line");
    }
    TEST_ASSERT_EQUAL_INT(-1, physicalLineOfCt(MAP_HOME_V62, 15, HOME_CT_ORDER, 2)); // no CT2 jack
}

void test_home_v62_software_channel_convention(void) {
    TEST_ASSERT_EQUAL_INT(3, HOME_CT_ORDER[1]);  // CT3 is logical mux index 1
    TEST_ASSERT_EQUAL_UINT8(2, MAP_HOME_V62[1]); // ... on Y2; software channel = logical + 1 = 2
}

void test_pro_v11_ct_routing(void) {
    const int expectedY[12] = {-1, 15, 14, 13, 12, 11, 0, 10, 1, 9, 2, 8}; // by CT number
    for (int ct = 1; ct <= 11; ct++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(expectedY[ct], physicalLineOfCt(MAP_PRO_V11, 11, PRO_CT_ORDER, ct), "Pro v1.1 CT line");
    }
    // Y3..Y7 are tied to GND and must never be selected.
    for (size_t i = 0; i < 11; i++) {
        TEST_ASSERT_FALSE(MAP_PRO_V11[i] >= 3 && MAP_PRO_V11[i] <= 7);
    }
}

void test_pro_v11_differs_from_v10(void) {
    TEST_ASSERT_FALSE(MAP_PRO_V10[5] == MAP_PRO_V11[5]);
    for (size_t i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT8(MAP_PRO_V10[i], MAP_PRO_V11[i]); // CT1..CT5 unchanged
}

// Same stub ordering contract as test_product_line: newest first within each product.
struct StubProfile {
    ProductLine product;
    uint8_t version;
};
static const StubProfile PROFILES[] = {
    {ProductLine::HOMEPRO, 11},
    {ProductLine::HOMEPRO, 10},
    {ProductLine::HOME, 62},
    {ProductLine::HOME, 61},
    {ProductLine::HOME, 60},
    {ProductLine::HOME, 50},
};
static const size_t COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);

void test_revision_strings_select_new_profiles(void) {
    const StubProfile* p = nullptr;
    ProductLine product = ProductLine::HOME;
    uint8_t version = 0;
    TEST_ASSERT_EQUAL((int)ProfileSelection::Result::SELECTED,
                      (int)ProfileSelection::select("v6.2", "home", PROFILES, COUNT, p, product, version));
    TEST_ASSERT_EQUAL_UINT8(62, p->version);
    TEST_ASSERT_EQUAL((int)ProfileSelection::Result::SELECTED,
                      (int)ProfileSelection::select("v1.1", "homepro", PROFILES, COUNT, p, product, version));
    TEST_ASSERT_EQUAL_UINT8(11, p->version);
    TEST_ASSERT_EQUAL((int)ProductLine::HOMEPRO, (int)p->product);
    // Product keys stay separate: Home has no v1.1, Pro has no v6.2.
    TEST_ASSERT_EQUAL((int)ProfileSelection::Result::UNKNOWN_PROFILE,
                      (int)ProfileSelection::select("v1.1", "home", PROFILES, COUNT, p, product, version));
    TEST_ASSERT_EQUAL((int)ProfileSelection::Result::UNKNOWN_PROFILE,
                      (int)ProfileSelection::select("v6.2", "homepro", PROFILES, COUNT, p, product, version));
}

void test_latest_profile_per_product(void) {
    TEST_ASSERT_EQUAL_UINT8(62, ProfileSelection::latestForProduct(PROFILES, COUNT, ProductLine::HOME)->version);
    TEST_ASSERT_EQUAL_UINT8(11, ProfileSelection::latestForProduct(PROFILES, COUNT, ProductLine::HOMEPRO)->version);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pga_register_validity);
    RUN_TEST(test_pga_gain_from_register);
    RUN_TEST(test_net_gain_is_unity_with_divider_and_pga2);
    RUN_TEST(test_net_gain_full_scale_matches_jack);
    RUN_TEST(test_mux_maps_are_valid);
    RUN_TEST(test_mux_map_rejects_bad_maps);
    RUN_TEST(test_home_v62_differs_from_v61_only_on_y8_y9);
    RUN_TEST(test_home_v62_ct_routing);
    RUN_TEST(test_home_v62_software_channel_convention);
    RUN_TEST(test_pro_v11_ct_routing);
    RUN_TEST(test_pro_v11_differs_from_v10);
    RUN_TEST(test_revision_strings_select_new_profiles);
    RUN_TEST(test_latest_profile_per_product);
    return UNITY_END();
}
