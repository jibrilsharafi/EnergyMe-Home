// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "modbus_mbap.h"

namespace ModbusMbap {

HeaderCheck checkHeader(const uint8_t header[MIN_FRAME_SIZE], size_t* frameLength) {
    size_t length = (static_cast<size_t>(header[4]) << 8 | header[5]) + MBAP_SIZE;
    if (frameLength) *frameLength = length;

    if (header[2] != 0 || header[3] != 0) return HeaderCheck::BAD_PROTOCOL;
    if (length < MIN_FRAME_SIZE || length > MAX_FRAME_SIZE) return HeaderCheck::BAD_LENGTH;
    return HeaderCheck::OK;
}

} // namespace ModbusMbap
