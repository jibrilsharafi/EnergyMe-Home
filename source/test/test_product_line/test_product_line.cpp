// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for product line parsing, artifact-name matching and hardware
// profile selection (lib/product_line). Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include "product_line.h"

using ProfileSelection::Result;

void setUp(void) {}
void tearDown(void) {}

// Mirrors the PCB_PROFILES ordering contract: newest first within each product.
struct StubProfile {
    ProductLine product;
    uint8_t version;
};
static const StubProfile PROFILES[] = {
    {ProductLine::HOME, 61},
    {ProductLine::HOME, 60},
    {ProductLine::HOMEPRO, 10},
    {ProductLine::HOME, 50},
};
static const size_t PROFILE_COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);

// ============================================================================
// Artifact names
// ============================================================================

static void assertArtifact(const char* name, bool expectedOk, ProductLine expected) {
    ProductLine out = ProductLine::HOME;
    TEST_ASSERT_EQUAL_MESSAGE(expectedOk, productFromArtifactName(name, out), name);
    if (expectedOk) TEST_ASSERT_EQUAL_MESSAGE((int)expected, (int)out, name);
}

void test_artifact_home(void) {
    assertArtifact("energyme_home_2.4.0.bin", true, ProductLine::HOME);
    assertArtifact("energyme_home_dev_2.4.0.bin", true, ProductLine::HOME);
}

void test_artifact_pro_matched_before_home_substring(void) {
    assertArtifact("energyme_homepro_2.4.0.bin", true, ProductLine::HOMEPRO);
    assertArtifact("releases/energyme_homepro_dev_2.4.0.bin", true, ProductLine::HOMEPRO);
}

void test_artifact_unknown_is_not_a_mismatch(void) {
    assertArtifact("firmware.bin", false, ProductLine::HOME);
    assertArtifact("", false, ProductLine::HOME);
    assertArtifact(nullptr, false, ProductLine::HOME);
    assertArtifact("EnergyMe_Home_2.4.0.bin", false, ProductLine::HOME); // case-sensitive
}

void test_artifact_failure_leaves_output_untouched(void) {
    ProductLine out = ProductLine::HOMEPRO;
    TEST_ASSERT_FALSE(productFromArtifactName("firmware.bin", out));
    TEST_ASSERT_EQUAL((int)ProductLine::HOMEPRO, (int)out);
}

// ============================================================================
// Product strings
// ============================================================================

void test_product_string_round_trip(void) {
    ProductLine out = ProductLine::HOME;
    TEST_ASSERT_TRUE(parseProductLineString(productLineToString(ProductLine::HOMEPRO), out));
    TEST_ASSERT_EQUAL((int)ProductLine::HOMEPRO, (int)out);
    TEST_ASSERT_TRUE(parseProductLineString(productLineToString(ProductLine::HOME), out));
    TEST_ASSERT_EQUAL((int)ProductLine::HOME, (int)out);
}

void test_product_string_rejects_unknown(void) {
    ProductLine out = ProductLine::HOMEPRO;
    TEST_ASSERT_FALSE(parseProductLineString("Home", out));
    TEST_ASSERT_FALSE(parseProductLineString("home ", out));
    TEST_ASSERT_FALSE(parseProductLineString("industry", out));
    TEST_ASSERT_FALSE(parseProductLineString("", out));
    TEST_ASSERT_FALSE(parseProductLineString(nullptr, out));
    TEST_ASSERT_EQUAL((int)ProductLine::HOMEPRO, (int)out);
}

// ============================================================================
// PCB revision
// ============================================================================

void test_pcb_revision_valid(void) {
    uint8_t v = 0;
    TEST_ASSERT_TRUE(parsePcbRevision("v6.1", v));
    TEST_ASSERT_EQUAL_UINT8(61, v);
    TEST_ASSERT_TRUE(parsePcbRevision("v1.0", v));
    TEST_ASSERT_EQUAL_UINT8(10, v);
    TEST_ASSERT_TRUE(parsePcbRevision("v25.5", v)); // 255: the uint8_t ceiling
    TEST_ASSERT_EQUAL_UINT8(255, v);
}

void test_pcb_revision_invalid(void) {
    uint8_t v = 77;
    const char* bad[] = {"6.1", "V6.1", "v6", "v", "", "v26.0", "v25.6", "v25.9", "v6.10", "vx.1", " v6.1"};
    for (const char* s : bad) TEST_ASSERT_FALSE_MESSAGE(parsePcbRevision(s, v), s);
    TEST_ASSERT_FALSE(parsePcbRevision(nullptr, v));
    TEST_ASSERT_EQUAL_UINT8(77, v);
}

// ============================================================================
// Profile selection
// ============================================================================

struct Selection {
    Result result;
    const StubProfile* profile;
    ProductLine product;
    uint8_t version;
};

static Selection run(const char* revision, const char* product) {
    Selection s = {Result::SELECTED, nullptr, ProductLine::HOME, 0};
    s.result = ProfileSelection::select(revision, product, PROFILES, PROFILE_COUNT, s.profile, s.product, s.version);
    return s;
}

void test_select_home_with_absent_product_key(void) {
    Selection s = run("v6.1", "");
    TEST_ASSERT_EQUAL((int)Result::SELECTED, (int)s.result);
    TEST_ASSERT_EQUAL_PTR(&PROFILES[0], s.profile);
    s = run("v6.0", nullptr);
    TEST_ASSERT_EQUAL((int)Result::SELECTED, (int)s.result);
    TEST_ASSERT_EQUAL_PTR(&PROFILES[1], s.profile);
}

void test_select_is_keyed_by_product_and_version(void) {
    Selection s = run("v1.0", "homepro");
    TEST_ASSERT_EQUAL((int)Result::SELECTED, (int)s.result);
    TEST_ASSERT_EQUAL_PTR(&PROFILES[2], s.profile);
    // Home has no v1.0 and Pro has no v6.1: versions are per product
    TEST_ASSERT_EQUAL((int)Result::UNKNOWN_PROFILE, (int)run("v1.0", "home").result);
    s = run("v6.1", "homepro");
    TEST_ASSERT_EQUAL((int)Result::UNKNOWN_PROFILE, (int)s.result);
    TEST_ASSERT_NULL(s.profile);
    TEST_ASSERT_EQUAL((int)ProductLine::HOMEPRO, (int)s.product);
    TEST_ASSERT_EQUAL_UINT8(61, s.version);
}

void test_select_community_reasons(void) {
    TEST_ASSERT_EQUAL((int)Result::NO_REVISION, (int)run("", "homepro").result);
    TEST_ASSERT_EQUAL((int)Result::NO_REVISION, (int)run(nullptr, nullptr).result);
    TEST_ASSERT_EQUAL((int)Result::UNKNOWN_PRODUCT, (int)run("v6.1", "industry").result);
    TEST_ASSERT_EQUAL((int)Result::MALFORMED_REVISION, (int)run("6.1", "home").result);
    // An unknown product is reported before a malformed revision
    TEST_ASSERT_EQUAL((int)Result::UNKNOWN_PRODUCT, (int)run("junk", "industry").result);
}

void test_latest_for_product(void) {
    TEST_ASSERT_EQUAL_PTR(&PROFILES[0], ProfileSelection::latestForProduct(PROFILES, PROFILE_COUNT, ProductLine::HOME));
    TEST_ASSERT_EQUAL_PTR(&PROFILES[2], ProfileSelection::latestForProduct(PROFILES, PROFILE_COUNT, ProductLine::HOMEPRO));
    // A product with no entry falls back to the first (latest overall) entry
    TEST_ASSERT_EQUAL_PTR(&PROFILES[0], ProfileSelection::latestForProduct(PROFILES, 2, ProductLine::HOMEPRO));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_artifact_home);
    RUN_TEST(test_artifact_pro_matched_before_home_substring);
    RUN_TEST(test_artifact_unknown_is_not_a_mismatch);
    RUN_TEST(test_artifact_failure_leaves_output_untouched);
    RUN_TEST(test_product_string_round_trip);
    RUN_TEST(test_product_string_rejects_unknown);
    RUN_TEST(test_pcb_revision_valid);
    RUN_TEST(test_pcb_revision_invalid);
    RUN_TEST(test_select_home_with_absent_product_key);
    RUN_TEST(test_select_is_keyed_by_product_and_version);
    RUN_TEST(test_select_community_reasons);
    RUN_TEST(test_latest_for_product);
    return UNITY_END();
}
