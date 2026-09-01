// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <esp_partition.h>

#include "image_descriptor.h"

// This build's own descriptor, emitted into .rodata_custom_desc (fixed image
// offset 0x120, right after esp_app_desc_t). Kept through --gc-sections by
// -Wl,-u,ENERGYME_APP_DESC in platformio.ini.
extern const ImageDescriptor::Descriptor ENERGYME_APP_DESC;

namespace AppImageDescriptor {

// Bump whenever the partition table changes in a way that makes images built
// for the old table unsafe on the new one (moved/resized data partitions).
inline constexpr uint32_t PARTITION_LAYOUT_ID = 1;

// The running device's identity for staged-image validation. Requires
// initHardwareProfile() to have run.
ImageDescriptor::DeviceIdentity deviceIdentity();

// Read the staged image's descriptor off `partition` and validate it against
// this device. A read failure or invalid descriptor follows the legacy policy
// (accepted on Home, rejected elsewhere).
ImageDescriptor::Verdict validatePartition(const esp_partition_t* partition, bool rejectDevOnProd);

// Same validation from an in-memory buffer that starts at image offset 0
// (a manual upload's first chunk). Call only when
// ImageDescriptor::coversDescriptor(len) is true.
ImageDescriptor::Verdict validateImageBuffer(const uint8_t* data, size_t len, bool rejectDevOnProd);

} // namespace AppImageDescriptor
