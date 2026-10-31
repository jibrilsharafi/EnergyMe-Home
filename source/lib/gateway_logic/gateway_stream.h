// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Receive-side decisions for the gateway modules (openspec capability:
// gateway-modules): an incremental HTTP chunked decoder that writes decoded
// bytes straight into the body buffer, and the raw TCP read termination rule.

namespace GatewayLogic {

inline constexpr size_t CHUNK_SIZE_LINE_MAX = 128; // size, extensions and CRLF
inline constexpr size_t CHUNK_TRAILER_MAX = 2048;
inline constexpr uint32_t TCP_IDLE_DEFAULT_MS = 300;
inline constexpr uint32_t TCP_IDLE_MIN_MS = 50;
inline constexpr uint32_t TCP_IDLE_MAX_MS = 5000;
inline constexpr size_t GATEWAY_BODY_MAX = 131072;
inline constexpr size_t GATEWAY_REQUEST_MAX = 5760; // one non-blocking lwIP send (CONFIG_LWIP_TCP_SND_BUF_DEFAULT)

class ChunkDecoder {
public:
    enum class Status { MORE, DONE, OVERFLOW, MALFORMED };

    // Feeds raw body bytes. Decoded data goes to `out` from `*outLength` up to
    // `cap`. OVERFLOW when a data byte arrives with `out` full (the reply is
    // truncated); DONE after the last chunk and its trailers. `*consumed` is
    // how many input bytes were used.
    Status feed(const uint8_t *in, size_t length, uint8_t *out, size_t cap, size_t *outLength, size_t *consumed = nullptr);
    bool done() const { return _state == State::DONE; }

private:
    enum class State { SIZE, SIZE_SPACE, SIZE_EXT, SIZE_LF, DATA, DATA_CR, DATA_LF, TRAILER_START, TRAILER_LINE, TRAILER_LF, FINAL_LF, DONE, ERROR };
    State _state = State::SIZE;
    uint32_t _remaining = 0; // data bytes left in the current chunk
    size_t _lineLength = 0;  // bytes of the current size line
    size_t _sizeDigits = 0;
    size_t _trailerBytes = 0;
};

uint32_t clampIdleMs(int64_t requested); // <= 0 -> default, else 50-5000

enum class TcpReadStop { CONTINUE, CLOSED, CAP, IDLE, DEADLINE };

// Read until the peer closes, `cap` is reached, `idleMs` passes without a byte
// after the first one, or the deadline.
TcpReadStop tcpReadStop(bool peerClosed, size_t received, size_t cap, uint32_t msSinceLastByte, uint32_t idleMs, bool deadlinePassed);

// Cap hit, or the deadline cut a reply that had started arriving.
bool tcpReplyTruncated(TcpReadStop stop, size_t received);

} // namespace GatewayLogic
