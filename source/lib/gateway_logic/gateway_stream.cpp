// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_stream.h"

namespace GatewayLogic {

namespace {

int hexDigit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

ChunkDecoder::Status ChunkDecoder::feed(const uint8_t *in, size_t length, uint8_t *out, size_t cap, size_t *outLength, size_t *consumed) {
    size_t i = 0;
    Status status = Status::MORE;
    while (i < length && status == Status::MORE) {
        uint8_t c = in[i];
        switch (_state) {
            case State::SIZE:
            case State::SIZE_SPACE:
            case State::SIZE_EXT:
                if (++_lineLength > CHUNK_SIZE_LINE_MAX) {
                    _state = State::ERROR;
                    break;
                }
                if (c == '\r') {
                    _state = _sizeDigits > 0 ? State::SIZE_LF : State::ERROR;
                } else if (_state == State::SIZE && hexDigit(c) >= 0) {
                    if (_remaining > 0x07FFFFFF) {
                        _state = State::ERROR;
                        break;
                    }
                    _remaining = (_remaining << 4) | static_cast<uint32_t>(hexDigit(c));
                    _sizeDigits++;
                } else if (c == ';' && _sizeDigits > 0 && _state != State::SIZE_EXT) {
                    _state = State::SIZE_EXT;
                } else if ((c == ' ' || c == '\t') && _sizeDigits > 0 && _state != State::SIZE_EXT) {
                    _state = State::SIZE_SPACE;
                } else if (_state != State::SIZE_EXT || c == '\n') {
                    _state = State::ERROR;
                }
                i++;
                break;
            case State::SIZE_LF:
                if (c != '\n' || ++_lineLength > CHUNK_SIZE_LINE_MAX) {
                    _state = State::ERROR;
                    break;
                }
                i++;
                _lineLength = 0;
                _sizeDigits = 0;
                _state = _remaining == 0 ? State::TRAILER_START : State::DATA;
                break;
            case State::DATA: {
                if (*outLength >= cap) {
                    status = Status::OVERFLOW;
                    break;
                }
                size_t take = length - i;
                if (take > _remaining) take = _remaining;
                if (take > cap - *outLength) take = cap - *outLength;
                for (size_t k = 0; k < take; k++) out[*outLength + k] = in[i + k];
                *outLength += take;
                _remaining -= static_cast<uint32_t>(take);
                i += take;
                if (_remaining == 0) _state = State::DATA_CR;
                break;
            }
            case State::DATA_CR:
                _state = c == '\r' ? State::DATA_LF : State::ERROR;
                i++;
                break;
            case State::DATA_LF:
                _state = c == '\n' ? State::SIZE : State::ERROR;
                i++;
                break;
            case State::TRAILER_START:
                _state = c == '\r' ? State::FINAL_LF : State::TRAILER_LINE;
                if (++_trailerBytes > CHUNK_TRAILER_MAX) _state = State::ERROR;
                i++;
                break;
            case State::TRAILER_LINE:
                if (c == '\r') _state = State::TRAILER_LF;
                if (++_trailerBytes > CHUNK_TRAILER_MAX) _state = State::ERROR;
                i++;
                break;
            case State::TRAILER_LF:
                _state = c == '\n' ? State::TRAILER_START : State::ERROR;
                i++;
                break;
            case State::FINAL_LF:
                _state = c == '\n' ? State::DONE : State::ERROR;
                i++;
                break;
            case State::DONE:
                status = Status::DONE;
                break;
            case State::ERROR:
                status = Status::MALFORMED;
                break;
        }
        if (_state == State::ERROR) status = Status::MALFORMED;
        if (_state == State::DONE) status = Status::DONE;
    }
    if (consumed != nullptr) *consumed = i;
    return status;
}

uint32_t clampIdleMs(int64_t requested) {
    if (requested <= 0) return TCP_IDLE_DEFAULT_MS;
    if (requested < TCP_IDLE_MIN_MS) return TCP_IDLE_MIN_MS;
    if (requested > TCP_IDLE_MAX_MS) return TCP_IDLE_MAX_MS;
    return static_cast<uint32_t>(requested);
}

TcpReadStop tcpReadStop(bool peerClosed, size_t received, size_t cap, uint32_t msSinceLastByte, uint32_t idleMs, bool deadlinePassed) {
    if (received >= cap) return TcpReadStop::CAP;
    if (peerClosed) return TcpReadStop::CLOSED;
    if (received > 0 && msSinceLastByte >= idleMs) return TcpReadStop::IDLE;
    if (deadlinePassed) return TcpReadStop::DEADLINE;
    return TcpReadStop::CONTINUE;
}

bool tcpReplyTruncated(TcpReadStop stop, size_t received) {
    return stop == TcpReadStop::CAP || (stop == TcpReadStop::DEADLINE && received > 0);
}

} // namespace GatewayLogic
