// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "image_descriptor.h"

#include <cstring>

namespace ImageDescriptor {

bool parseFromImageStart(const uint8_t* imageBytes, size_t len, Descriptor& out) {
    if (imageBytes == nullptr || !coversDescriptor(len)) return false;

    memcpy(&out, imageBytes + IMAGE_OFFSET, sizeof(Descriptor));

    // Flash content is untrusted: never let a char field escape unterminated.
    out.product[PRODUCT_LEN - 1] = '\0';
    out.fwVersion[FW_VERSION_LEN - 1] = '\0';
    out.buildEnv[BUILD_ENV_LEN - 1] = '\0';
    out.gitRev[GIT_REV_LEN - 1] = '\0';

    return out.magic == MAGIC && out.layout >= LAYOUT_VERSION;
}

Verdict validate(const Descriptor* desc, const DeviceIdentity& device, bool rejectDevOnProd) {
    if (desc == nullptr) {
        return device.allowMissingDescriptor ? Verdict::ACCEPT_LEGACY_NO_DESCRIPTOR
                                             : Verdict::REJECT_NO_DESCRIPTOR;
    }

    // PSRAM first: the one mismatch no post-boot mechanism can recover from.
    if (desc->psramMb != device.psramMb) return Verdict::REJECT_PSRAM_MISMATCH;

    if (strncmp(desc->product, device.product, PRODUCT_LEN) != 0) {
        return Verdict::REJECT_PRODUCT_MISMATCH;
    }

    if ((desc->minPcbVersion != 0 && device.pcbVersion < desc->minPcbVersion) ||
        (desc->maxPcbVersion != 0 && device.pcbVersion > desc->maxPcbVersion)) {
        return Verdict::REJECT_PCB_UNSUPPORTED;
    }

    // 0 = unspecified: tolerated so a future table change can be staged in two
    // steps (first ship firmware that knows the new id, then flip it).
    if (desc->partitionLayoutId != 0 && desc->partitionLayoutId != device.partitionLayoutId) {
        return Verdict::REJECT_PARTITION_LAYOUT_MISMATCH;
    }

    if (device.runningProdEnv && strncmp(desc->buildEnv, "dev", BUILD_ENV_LEN) == 0) {
        return rejectDevOnProd ? Verdict::REJECT_DEV_IMAGE_ON_PROD : Verdict::ACCEPT_DEV_ON_PROD;
    }

    return Verdict::ACCEPT;
}

const char* verdictToString(Verdict v) {
    switch (v) {
        case Verdict::ACCEPT: return "accept";
        case Verdict::ACCEPT_LEGACY_NO_DESCRIPTOR: return "accept_legacy";
        case Verdict::ACCEPT_DEV_ON_PROD: return "accept_dev_on_prod";
        case Verdict::REJECT_NO_DESCRIPTOR: return "no_descriptor";
        case Verdict::REJECT_PRODUCT_MISMATCH: return "product_mismatch";
        case Verdict::REJECT_PSRAM_MISMATCH: return "psram_mismatch";
        case Verdict::REJECT_PCB_UNSUPPORTED: return "pcb_unsupported";
        case Verdict::REJECT_PARTITION_LAYOUT_MISMATCH: return "partition_layout_mismatch";
        case Verdict::REJECT_DEV_IMAGE_ON_PROD: return "dev_image_on_prod";
    }
    return "unknown";
}

} // namespace ImageDescriptor
