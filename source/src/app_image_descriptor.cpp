// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "app_image_descriptor.h"

#include <cstdio>
#include <cstring>

#include "constants.h"
#include "hardware_profile.h"
#include "sdkconfig.h"

#ifndef GIT_REV
#define GIT_REV "unknown"
#endif

#if defined(PRODUCT_FALLBACK) && PRODUCT_FALLBACK == 1
#define ENERGYME_IMAGE_PRODUCT PRODUCT_LINE_HOME_PRO_STR
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

#ifdef ENV_PROD
#define ENERGYME_IMAGE_ENV "prod"
#else
#define ENERGYME_IMAGE_ENV "dev"
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

ImageDescriptor::DeviceIdentity deviceIdentity() {
    ImageDescriptor::DeviceIdentity dev = {};
    snprintf(dev.product, sizeof(dev.product), "%s", productLineToString(globalHwProfile->product));
    dev.psramMb = ENERGYME_APP_DESC.psramMb;
    dev.pcbVersion = globalHwProfile->version;
    dev.partitionLayoutId = ENERGYME_APP_DESC.partitionLayoutId;
    dev.runningProdEnv = strncmp(ENERGYME_APP_DESC.buildEnv, "prod", sizeof(ENERGYME_APP_DESC.buildEnv)) == 0;
    dev.allowMissingDescriptor = (globalHwProfile->product == ProductLine::HOME);
    return dev;
}

ImageDescriptor::Verdict validatePartition(const esp_partition_t* partition, bool rejectDevOnProd) {
    uint8_t buf[ImageDescriptor::IMAGE_OFFSET + sizeof(ImageDescriptor::Descriptor)];
    ImageDescriptor::Descriptor desc;
    const ImageDescriptor::Descriptor* descPtr = nullptr;
    if (partition != nullptr &&
        esp_partition_read(partition, 0, buf, sizeof(buf)) == ESP_OK &&
        ImageDescriptor::parseFromImageStart(buf, sizeof(buf), desc)) {
        descPtr = &desc;
    }
    return ImageDescriptor::validate(descPtr, deviceIdentity(), rejectDevOnProd);
}

ImageDescriptor::Verdict validateImageBuffer(const uint8_t* data, size_t len, bool rejectDevOnProd) {
    ImageDescriptor::Descriptor desc;
    const ImageDescriptor::Descriptor* descPtr =
        ImageDescriptor::parseFromImageStart(data, len, desc) ? &desc : nullptr;
    return ImageDescriptor::validate(descPtr, deviceIdentity(), rejectDevOnProd);
}

} // namespace AppImageDescriptor
