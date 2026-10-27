// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "app_image_descriptor.h"

#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_ota_ops.h>

#include "constants.h"
#include "hardware_profile.h"
#include "sdkconfig.h"

#if __has_include("git_rev.h")
#include "git_rev.h"
#endif
#ifndef GIT_REV
#define GIT_REV "unknown"
#endif

#if defined(PRODUCT_FALLBACK) && PRODUCT_FALLBACK == 1
#define ENERGYME_IMAGE_PRODUCT PRODUCT_LINE_HOMEPRO_STR
#else
#define ENERGYME_IMAGE_PRODUCT PRODUCT_LINE_HOME_STR
#endif

// Derived from the toolchain's own SPIRAM mode so it can never disagree with
// how the binary was actually built (quad = qio_qspi envs, octal = qio_opi).
#ifdef CONFIG_SPIRAM_MODE_OCT
#define ENERGYME_IMAGE_PSRAM_MB 8
#else
#define ENERGYME_IMAGE_PSRAM_MB 2
#endif

// Only ENV_DEV exists as a build flag (platformio.ini); everything else is prod.
#ifdef ENV_DEV
#define ENERGYME_IMAGE_ENV "dev"
#else
#define ENERGYME_IMAGE_ENV "prod"
#endif

__attribute__((section(".rodata_custom_desc"), used))
const ImageDescriptor::Descriptor ENERGYME_APP_DESC = {
    ImageDescriptor::MAGIC,
    ImageDescriptor::LAYOUT_VERSION,
    ENERGYME_IMAGE_PRODUCT,
    ENERGYME_IMAGE_PSRAM_MB,
    FIRMWARE_BUILD_VERSION,
    ENERGYME_IMAGE_ENV,
    GIT_REV,
    0, // minPcbVersion: unbounded until support for a PCB revision is dropped
    0, // maxPcbVersion: unbounded
    AppImageDescriptor::PARTITION_LAYOUT_ID,
    {0},
};

namespace AppImageDescriptor {

// The silicon, not the running image (see image_descriptor.h).
static uint32_t _hardwarePsramMb() {
    uint8_t cap = 0;
    if (esp_efuse_read_field_blob(ESP_EFUSE_PSRAM_CAP, &cap, 2) == ESP_OK) {
        uint32_t psramMb = ImageDescriptor::psramMbFromEfuseCap(cap);
        if (psramMb != 0) return psramMb;
    }
    return ENERGYME_APP_DESC.psramMb;
}

ImageDescriptor::DeviceIdentity deviceIdentity() {
    ImageDescriptor::DeviceIdentity dev = {};
    snprintf(dev.product, sizeof(dev.product), "%s", productLineToString(globalHwProfile->product));
    dev.psramMb = _hardwarePsramMb();
    dev.pcbVersion = globalHwProfile->version;
    dev.partitionLayoutId = ENERGYME_APP_DESC.partitionLayoutId;
    dev.runningProdEnv = strncmp(ENERGYME_APP_DESC.buildEnv, "prod", sizeof(ENERGYME_APP_DESC.buildEnv)) == 0;
    // Every pre-descriptor release is a quad-PSRAM Home image. The product alone is not
    // enough: it comes from factory NVS, and octal hardware provisioned as "home" would
    // otherwise accept an unbootable image.
    dev.allowMissingDescriptor = (globalHwProfile->product == ProductLine::HOME) && dev.psramMb == 2;
    return dev;
}

// Serial, not LOG_*: this runs before AdvancedLogger::begin(), which drops anything earlier.
void rollBackIfWrongImage() {
    ImageDescriptor::DeviceIdentity dev = deviceIdentity();
    Serial.printf("Board PSRAM: %lu MB (image built for %lu MB)\n", (unsigned long)dev.psramMb,
                  (unsigned long)ENERGYME_APP_DESC.psramMb);
    ImageDescriptor::Verdict running = ImageDescriptor::validate(&ENERGYME_APP_DESC, dev, false);
    if (ImageDescriptor::accepts(running)) return;

    Serial.printf("The running firmware is not built for this board (%s)\n", ImageDescriptor::verdictToString(running));
    const esp_partition_t* passive = esp_ota_get_next_update_partition(NULL);
    ImageDescriptor::Verdict passiveVerdict = validatePartition(passive, false);
    if (!ImageDescriptor::accepts(passiveVerdict)) {
        Serial.printf("The previous firmware does not fit this board either (%s) - upload a build for this board\n",
                      ImageDescriptor::verdictToString(passiveVerdict));
        return;
    }
    esp_err_t err = esp_ota_set_boot_partition(passive);
    if (err != ESP_OK) {
        Serial.printf("Rolling back to the previous firmware failed: %s\n", esp_err_to_name(err));
        return;
    }
    Serial.printf("Rolled back to the previous firmware (%s), restarting\n", passive->label);
    Serial.flush();
    ESP.restart();
}

ImageDescriptor::Verdict validatePartition(const esp_partition_t* partition, bool rejectDevOnProd) {
    // A missing partition or failed flash read is a hard reject, never the
    // legacy-image allowance: "cannot read the staged image" is not evidence
    // it is a pre-descriptor release.
    uint8_t buf[ImageDescriptor::IMAGE_OFFSET + sizeof(ImageDescriptor::Descriptor)];
    if (partition == nullptr || esp_partition_read(partition, 0, buf, sizeof(buf)) != ESP_OK) {
        return ImageDescriptor::Verdict::REJECT_NO_DESCRIPTOR;
    }
    return validateImageBuffer(buf, sizeof(buf), rejectDevOnProd);
}

ImageDescriptor::Verdict validateImageBuffer(const uint8_t* data, size_t len, bool rejectDevOnProd) {
    ImageDescriptor::Descriptor desc;
    const ImageDescriptor::Descriptor* descPtr =
        ImageDescriptor::parseFromImageStart(data, len, desc) ? &desc : nullptr;
    return ImageDescriptor::validate(descPtr, deviceIdentity(), rejectDevOnProd);
}

} // namespace AppImageDescriptor
