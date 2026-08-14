// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "sha256_hex.h"

namespace Sha256Hex {

bool bytesToHex(const uint8_t digest[DIGEST_LEN], char *out, size_t outSize) {
    if (!digest || !out || outSize < BUFFER_SIZE) return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < DIGEST_LEN; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0F];
    }
    out[HEX_LEN] = '\0';
    return true;
}

}  // namespace Sha256Hex
