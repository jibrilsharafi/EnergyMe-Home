// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure, dependency-free rendering of a raw 32-byte SHA-256 digest as lowercase
// hex. Extracted from utils.cpp so the OTA/rollback fingerprint path is
// host-testable; reading the digest itself (esp_app_desc_t.app_elf_sha256,
// mbedtls) stays with the callers.

namespace Sha256Hex {

inline constexpr size_t DIGEST_LEN = 32;
inline constexpr size_t HEX_LEN = DIGEST_LEN * 2;
inline constexpr size_t BUFFER_SIZE = HEX_LEN + 1;

// Renders digest as 64 lowercase hex chars + '\0'. Returns false (out untouched)
// if out is null or outSize < BUFFER_SIZE.
bool bytesToHex(const uint8_t digest[DIGEST_LEN], char *out, size_t outSize);

}  // namespace Sha256Hex
