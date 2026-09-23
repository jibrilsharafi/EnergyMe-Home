// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free parsing and validation of the EnergyMe firmware image
// descriptor (openspec capability: firmware-image-compatibility).
//
// Every image embeds a Descriptor in .rodata_custom_desc, which the IDF linker
// script places immediately after esp_app_desc_t - a fixed 0x120 offset from
// the start of the .bin. The still-running firmware reads it off the staged
// OTA partition (or the first upload chunk) and refuses to activate an image
// built for different hardware. A wrong-PSRAM image fails PSRAM init before
// any application code runs, so no post-boot mechanism can recover from it -
// this pre-activation gate is the only defense.
//
// Reading flash and building the DeviceIdentity is the caller's job;
// everything here is host-testable.

namespace ImageDescriptor {

inline constexpr uint32_t MAGIC = 0x57484D45; // "EMHW" little-endian
inline constexpr uint32_t LAYOUT_VERSION = 1;

// 24 B image header + 8 B segment header + 256 B esp_app_desc_t
inline constexpr size_t IMAGE_OFFSET = 0x120;

inline constexpr size_t PRODUCT_LEN = 16;
inline constexpr size_t FW_VERSION_LEN = 16;
inline constexpr size_t BUILD_ENV_LEN = 8;
inline constexpr size_t GIT_REV_LEN = 12;

// On-flash layout: append-only, versioned. Future layouts take bytes from
// reserved[] and bump `layout`; existing offsets never move, so a validator
// knowing layout N can validate any image with layout >= N.
struct Descriptor {
    uint32_t magic;
    uint32_t layout;
    char product[PRODUCT_LEN];
    uint32_t psramMb;            // PSRAM class the image requires: 2 (quad) / 8 (octal)
    char fwVersion[FW_VERSION_LEN];
    char buildEnv[BUILD_ENV_LEN]; // "dev" / "prod"
    char gitRev[GIT_REV_LEN];
    uint16_t minPcbVersion;      // packed major*10+minor; 0 = no lower bound
    uint16_t maxPcbVersion;      // 0 = no upper bound
    uint32_t partitionLayoutId;  // 0 = unspecified (accepted); mismatch rejects
    uint8_t reserved[56];
};
static_assert(sizeof(Descriptor) == 128, "fixed on-flash layout");

// The validating device's identity. Product and PCB version come from the
// runtime hardware profile; psramMb, partitionLayoutId and runningProdEnv come
// from the RUNNING image's own descriptor (the running build boots, therefore
// its compiled PSRAM mode matches the silicon - no probing needed).
struct DeviceIdentity {
    char product[PRODUCT_LEN];
    uint32_t psramMb;
    uint16_t pcbVersion;
    uint32_t partitionLayoutId;
    bool runningProdEnv;
    bool allowMissingDescriptor; // true only on Home: every pre-descriptor
                                 // official release is a Home/quad image
};

enum class Verdict {
    ACCEPT,
    ACCEPT_LEGACY_NO_DESCRIPTOR, // missing descriptor tolerated on Home
    ACCEPT_DEV_ON_PROD,          // dev image on prod device, path allows it - caller warns
    REJECT_NO_DESCRIPTOR,
    REJECT_PRODUCT_MISMATCH,
    REJECT_PSRAM_MISMATCH,
    REJECT_PCB_UNSUPPORTED,
    REJECT_PARTITION_LAYOUT_MISMATCH,
    REJECT_DEV_IMAGE_ON_PROD,
};

// Extract the descriptor from a buffer that starts at image offset 0 (an
// upload's first chunk, or a flash read from the partition base). Returns
// false when the buffer is too short to contain the descriptor region or the
// magic/layout mark it invalid. String fields in `out` are force-terminated.
bool parseFromImageStart(const uint8_t* imageBytes, size_t len, Descriptor& out);

// True when `len` bytes from image start are enough to attempt a parse - lets
// a streaming caller distinguish "no descriptor" from "not enough data yet".
inline bool coversDescriptor(size_t len) { return len >= IMAGE_OFFSET + sizeof(Descriptor); }

// Policy. `desc` may be nullptr ("no valid descriptor" - legacy or foreign
// image). `rejectDevOnProd` selects the per-path env policy: true for cloud
// OTA, false for manual upload (bench path - warn, don't block).
Verdict validate(const Descriptor* desc, const DeviceIdentity& device, bool rejectDevOnProd);

inline bool accepts(Verdict v) {
    return v == Verdict::ACCEPT || v == Verdict::ACCEPT_LEGACY_NO_DESCRIPTOR ||
           v == Verdict::ACCEPT_DEV_ON_PROD;
}

// Short stable token for logs and OTA failure reasons.
const char* verdictToString(Verdict v);

} // namespace ImageDescriptor
