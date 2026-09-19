// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Host unit tests for ImageDescriptor. Run with:
//   pio test -e native          (from WSL - Windows native toolchain is unreliable)

#include <unity.h>
#include <cstdio>
#include <cstring>

#include "image_descriptor.h"

using namespace ImageDescriptor;

// ============================================================================
// Fixtures
// ============================================================================

static constexpr size_t IMAGE_LEN = IMAGE_OFFSET + sizeof(Descriptor) + 64;
static uint8_t gImage[IMAGE_LEN];

static Descriptor makeDescriptor(const char* product = "home", uint32_t psramMb = 2,
                                 const char* env = "prod") {
    Descriptor d = {};
    d.magic = MAGIC;
    d.layout = LAYOUT_VERSION;
    snprintf(d.product, sizeof(d.product), "%s", product);
    d.psramMb = psramMb;
    snprintf(d.fwVersion, sizeof(d.fwVersion), "2.4.0");
    snprintf(d.buildEnv, sizeof(d.buildEnv), "%s", env);
    snprintf(d.gitRev, sizeof(d.gitRev), "abc1234");
    d.partitionLayoutId = 1;
    return d;
}

static void writeImage(const Descriptor& d) {
    memset(gImage, 0xA5, sizeof(gImage));
    memcpy(gImage + IMAGE_OFFSET, &d, sizeof(Descriptor));
}

static DeviceIdentity makeDevice(const char* product = "home", uint32_t psramMb = 2,
                                 bool prodEnv = true) {
    DeviceIdentity dev = {};
    snprintf(dev.product, sizeof(dev.product), "%s", product);
    dev.psramMb = psramMb;
    dev.pcbVersion = 61; // v6.1
    dev.partitionLayoutId = 1;
    dev.runningProdEnv = prodEnv;
    dev.allowMissingDescriptor = (strcmp(product, "home") == 0);
    return dev;
}

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// parseFromImageStart
// ============================================================================

void test_parse_valid_image(void) {
    writeImage(makeDescriptor());
    Descriptor out;
    TEST_ASSERT_TRUE(parseFromImageStart(gImage, IMAGE_LEN, out));
    TEST_ASSERT_EQUAL_STRING("home", out.product);
    TEST_ASSERT_EQUAL_UINT32(2, out.psramMb);
    TEST_ASSERT_EQUAL_STRING("2.4.0", out.fwVersion);
    TEST_ASSERT_EQUAL_STRING("prod", out.buildEnv);
    TEST_ASSERT_EQUAL_STRING("abc1234", out.gitRev);
}

void test_parse_null_buffer(void) {
    Descriptor out;
    TEST_ASSERT_FALSE(parseFromImageStart(nullptr, IMAGE_LEN, out));
}

void test_parse_short_buffer(void) {
    writeImage(makeDescriptor());
    Descriptor out;
    TEST_ASSERT_FALSE(parseFromImageStart(gImage, IMAGE_OFFSET + sizeof(Descriptor) - 1, out));
}

void test_parse_exact_length_buffer(void) {
    writeImage(makeDescriptor());
    Descriptor out;
    TEST_ASSERT_TRUE(parseFromImageStart(gImage, IMAGE_OFFSET + sizeof(Descriptor), out));
}

void test_parse_bad_magic(void) {
    Descriptor d = makeDescriptor();
    d.magic = 0xDEADBEEF;
    writeImage(d);
    Descriptor out;
    TEST_ASSERT_FALSE(parseFromImageStart(gImage, IMAGE_LEN, out));
}

void test_parse_zero_layout(void) {
    Descriptor d = makeDescriptor();
    d.layout = 0;
    writeImage(d);
    Descriptor out;
    TEST_ASSERT_FALSE(parseFromImageStart(gImage, IMAGE_LEN, out));
}

void test_parse_legacy_image_all_code_bytes(void) {
    // A pre-descriptor image has arbitrary code/rodata at the descriptor offset.
    memset(gImage, 0x3C, sizeof(gImage));
    Descriptor out;
    TEST_ASSERT_FALSE(parseFromImageStart(gImage, IMAGE_LEN, out));
}

void test_parse_newer_layout_accepted(void) {
    Descriptor d = makeDescriptor();
    d.layout = LAYOUT_VERSION + 1; // future firmware appended fields
    writeImage(d);
    Descriptor out;
    TEST_ASSERT_TRUE(parseFromImageStart(gImage, IMAGE_LEN, out));
}

void test_parse_terminates_unterminated_strings(void) {
    Descriptor d = makeDescriptor();
    memset(d.product, 'x', sizeof(d.product)); // no NUL anywhere
    writeImage(d);
    Descriptor out;
    TEST_ASSERT_TRUE(parseFromImageStart(gImage, IMAGE_LEN, out));
    TEST_ASSERT_EQUAL('\0', out.product[PRODUCT_LEN - 1]);
    TEST_ASSERT_EQUAL(PRODUCT_LEN - 1, strlen(out.product));
}

void test_covers_descriptor(void) {
    TEST_ASSERT_FALSE(coversDescriptor(0));
    TEST_ASSERT_FALSE(coversDescriptor(IMAGE_OFFSET + sizeof(Descriptor) - 1));
    TEST_ASSERT_TRUE(coversDescriptor(IMAGE_OFFSET + sizeof(Descriptor)));
}

// ============================================================================
// validate - matching image
// ============================================================================

void test_matching_image_accepted(void) {
    Descriptor d = makeDescriptor();
    DeviceIdentity dev = makeDevice();
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, false));
}

void test_matching_pro_image_accepted(void) {
    Descriptor d = makeDescriptor("home_pro", 8);
    DeviceIdentity dev = makeDevice("home_pro", 8);
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

// ============================================================================
// validate - missing descriptor (legacy policy)
// ============================================================================

void test_missing_descriptor_accepted_on_home(void) {
    DeviceIdentity dev = makeDevice("home");
    TEST_ASSERT_EQUAL(Verdict::ACCEPT_LEGACY_NO_DESCRIPTOR, validate(nullptr, dev, true));
}

void test_missing_descriptor_rejected_on_pro(void) {
    DeviceIdentity dev = makeDevice("home_pro", 8);
    TEST_ASSERT_EQUAL(Verdict::REJECT_NO_DESCRIPTOR, validate(nullptr, dev, true));
}

// ============================================================================
// validate - hardware mismatches
// ============================================================================

void test_psram_mismatch_rejected(void) {
    // The bricking case: octal image on a quad device.
    Descriptor d = makeDescriptor("home_pro", 8);
    DeviceIdentity dev = makeDevice("home", 2);
    TEST_ASSERT_EQUAL(Verdict::REJECT_PSRAM_MISMATCH, validate(&d, dev, false));
}

void test_psram_mismatch_beats_product_mismatch(void) {
    // Same PSRAM class but wrong product still names the product, not PSRAM.
    Descriptor d = makeDescriptor("home_pro", 2);
    DeviceIdentity dev = makeDevice("home", 2);
    TEST_ASSERT_EQUAL(Verdict::REJECT_PRODUCT_MISMATCH, validate(&d, dev, false));
}

void test_product_mismatch_rejected(void) {
    Descriptor d = makeDescriptor("home", 2);
    DeviceIdentity dev = makeDevice("home_pro", 8);
    TEST_ASSERT_EQUAL(Verdict::REJECT_PSRAM_MISMATCH, validate(&d, dev, false));
    // With equal PSRAM the product check fires:
    dev.psramMb = 2;
    TEST_ASSERT_EQUAL(Verdict::REJECT_PRODUCT_MISMATCH, validate(&d, dev, false));
}

// ============================================================================
// validate - PCB version range
// ============================================================================

void test_pcb_unbounded_accepts_everything(void) {
    Descriptor d = makeDescriptor(); // min=max=0
    DeviceIdentity dev = makeDevice();
    dev.pcbVersion = 1;
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
    dev.pcbVersion = 65535;
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

void test_pcb_below_min_rejected(void) {
    Descriptor d = makeDescriptor();
    d.minPcbVersion = 60; // dropped v5.x support
    DeviceIdentity dev = makeDevice();
    dev.pcbVersion = 50;
    TEST_ASSERT_EQUAL(Verdict::REJECT_PCB_UNSUPPORTED, validate(&d, dev, true));
    dev.pcbVersion = 60;
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

void test_pcb_above_max_rejected(void) {
    Descriptor d = makeDescriptor();
    d.maxPcbVersion = 61;
    DeviceIdentity dev = makeDevice();
    dev.pcbVersion = 70;
    TEST_ASSERT_EQUAL(Verdict::REJECT_PCB_UNSUPPORTED, validate(&d, dev, true));
    dev.pcbVersion = 61;
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

// ============================================================================
// validate - partition layout
// ============================================================================

void test_partition_layout_mismatch_rejected(void) {
    Descriptor d = makeDescriptor();
    d.partitionLayoutId = 2;
    DeviceIdentity dev = makeDevice(); // id 1
    TEST_ASSERT_EQUAL(Verdict::REJECT_PARTITION_LAYOUT_MISMATCH, validate(&d, dev, true));
}

void test_partition_layout_zero_is_unspecified(void) {
    Descriptor d = makeDescriptor();
    d.partitionLayoutId = 0;
    DeviceIdentity dev = makeDevice();
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

// ============================================================================
// validate - env policy
// ============================================================================

void test_dev_on_prod_rejected_on_cloud_path(void) {
    Descriptor d = makeDescriptor("home", 2, "dev");
    DeviceIdentity dev = makeDevice("home", 2, true);
    TEST_ASSERT_EQUAL(Verdict::REJECT_DEV_IMAGE_ON_PROD, validate(&d, dev, true));
}

void test_dev_on_prod_warned_on_manual_path(void) {
    Descriptor d = makeDescriptor("home", 2, "dev");
    DeviceIdentity dev = makeDevice("home", 2, true);
    TEST_ASSERT_EQUAL(Verdict::ACCEPT_DEV_ON_PROD, validate(&d, dev, false));
    TEST_ASSERT_TRUE(accepts(Verdict::ACCEPT_DEV_ON_PROD));
}

void test_prod_on_dev_always_accepted(void) {
    Descriptor d = makeDescriptor("home", 2, "prod");
    DeviceIdentity dev = makeDevice("home", 2, false);
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

void test_dev_on_dev_accepted(void) {
    Descriptor d = makeDescriptor("home", 2, "dev");
    DeviceIdentity dev = makeDevice("home", 2, false);
    TEST_ASSERT_EQUAL(Verdict::ACCEPT, validate(&d, dev, true));
}

// ============================================================================
// accepts / verdictToString
// ============================================================================

void test_accepts_classification(void) {
    TEST_ASSERT_TRUE(accepts(Verdict::ACCEPT));
    TEST_ASSERT_TRUE(accepts(Verdict::ACCEPT_LEGACY_NO_DESCRIPTOR));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_NO_DESCRIPTOR));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_PSRAM_MISMATCH));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_PRODUCT_MISMATCH));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_PCB_UNSUPPORTED));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_PARTITION_LAYOUT_MISMATCH));
    TEST_ASSERT_FALSE(accepts(Verdict::REJECT_DEV_IMAGE_ON_PROD));
}

void test_verdict_strings_are_stable_tokens(void) {
    TEST_ASSERT_EQUAL_STRING("psram_mismatch", verdictToString(Verdict::REJECT_PSRAM_MISMATCH));
    TEST_ASSERT_EQUAL_STRING("product_mismatch", verdictToString(Verdict::REJECT_PRODUCT_MISMATCH));
    TEST_ASSERT_EQUAL_STRING("no_descriptor", verdictToString(Verdict::REJECT_NO_DESCRIPTOR));
    TEST_ASSERT_EQUAL_STRING("dev_image_on_prod", verdictToString(Verdict::REJECT_DEV_IMAGE_ON_PROD));
}

// ============================================================================

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_valid_image);
    RUN_TEST(test_parse_null_buffer);
    RUN_TEST(test_parse_short_buffer);
    RUN_TEST(test_parse_exact_length_buffer);
    RUN_TEST(test_parse_bad_magic);
    RUN_TEST(test_parse_zero_layout);
    RUN_TEST(test_parse_legacy_image_all_code_bytes);
    RUN_TEST(test_parse_newer_layout_accepted);
    RUN_TEST(test_parse_terminates_unterminated_strings);
    RUN_TEST(test_covers_descriptor);

    RUN_TEST(test_matching_image_accepted);
    RUN_TEST(test_matching_pro_image_accepted);
    RUN_TEST(test_missing_descriptor_accepted_on_home);
    RUN_TEST(test_missing_descriptor_rejected_on_pro);
    RUN_TEST(test_psram_mismatch_rejected);
    RUN_TEST(test_psram_mismatch_beats_product_mismatch);
    RUN_TEST(test_product_mismatch_rejected);
    RUN_TEST(test_pcb_unbounded_accepts_everything);
    RUN_TEST(test_pcb_below_min_rejected);
    RUN_TEST(test_pcb_above_max_rejected);
    RUN_TEST(test_partition_layout_mismatch_rejected);
    RUN_TEST(test_partition_layout_zero_is_unspecified);
    RUN_TEST(test_dev_on_prod_rejected_on_cloud_path);
    RUN_TEST(test_dev_on_prod_warned_on_manual_path);
    RUN_TEST(test_prod_on_dev_always_accepted);
    RUN_TEST(test_dev_on_dev_accepted);
    RUN_TEST(test_accepts_classification);
    RUN_TEST(test_verdict_strings_are_stable_tokens);

    return UNITY_END();
}
