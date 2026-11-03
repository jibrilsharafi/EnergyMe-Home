// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Validation of the Modbus TCP MBAP header for the server's receive path
// (src/modbus_server_tcp_async.cpp). Kept here, free of Arduino, so it is host-tested
// (test/test_modbus_mbap).
//
// eModbus 1.7.4-1.7.6 accepted an MBAP length field of 0 or 1, i.e. a frame shorter than
// the 8 bytes it had already buffered. Its receive loop then never completed the frame and
// never consumed another byte, spinning in async_tcp until the task watchdog reset the
// device (#277). Any LAN client can send such a frame.
namespace ModbusMbap {

constexpr size_t MBAP_SIZE = 6;       // Transaction id, protocol id, length
constexpr size_t MIN_FRAME_SIZE = 8;  // MBAP + unit id + function code
constexpr size_t MAX_FRAME_SIZE = 262; // MBAP + 256-byte PDU with unit id

enum class HeaderCheck {
    OK,
    BAD_PROTOCOL, // Protocol id is not 0 (Modbus)
    BAD_LENGTH,   // Length field gives a frame outside [MIN_FRAME_SIZE, MAX_FRAME_SIZE]
};

// Checks the first MIN_FRAME_SIZE bytes of a frame. On OK, *frameLength is the full frame
// size including the MBAP; it is set on BAD_LENGTH too, for logging.
HeaderCheck checkHeader(const uint8_t header[MIN_FRAME_SIZE], size_t* frameLength);

} // namespace ModbusMbap
