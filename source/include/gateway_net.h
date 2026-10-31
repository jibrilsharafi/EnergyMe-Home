// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "mbedtls/ssl.h"

// TCP connection for the gateway modules (openspec capability: gateway-modules):
// a non-blocking lwIP socket where the connect, an optional TLS handshake and
// every read and write are bounded by one absolute deadline (millis()).
// TLS is raw mbedtls_ssl with no SNI and no CA or hostname validation: the
// caller pins the leaf certificate with peerCert() before writing (design D9).
// NetworkClientSecure is not used because its handshake timeout is whole
// seconds and defaults to 120 s.

namespace GatewayNet
{
    enum class Status { OK, TIMEOUT, CONNECT_FAILED, TLS_FAILED, NO_MEMORY, CLOSED };

    struct CertInfo {
        uint8_t sha256[32];
        char subjectCn[65];
        uint64_t notAfter; // Unix seconds, 0 when unparseable
    };

    class Connection {
    public:
        Connection() = default;
        ~Connection() { close(); }
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        // TCP connect within the deadline. On success nothing has been sent.
        Status connect(const uint8_t octets[4], uint16_t port, uint32_t deadline);
        // ClientHello onwards; later reads and writes go through TLS. Frees the
        // TLS context on failure.
        Status startTls(uint32_t deadline);
        bool peerCert(CertInfo* info) const;
        Status writeAll(const uint8_t* data, size_t length, uint32_t deadline);
        // OK with *got > 0 when bytes arrived, OK with *got == 0 when nothing came
        // within `waitMs`, CLOSED at EOF, TIMEOUT once the deadline has passed.
        Status read(uint8_t* buffer, size_t capacity, size_t* got, uint32_t deadline, uint32_t waitMs);
        void close();

    private:
        static int _send(void* ctx, const unsigned char* buf, size_t len);
        static int _recv(void* ctx, unsigned char* buf, size_t len);
        bool _wait(bool forWrite, uint32_t deadline, uint32_t capMs) const;

        int _fd = -1;
        bool _tlsReady = false;
        mbedtls_ssl_context* _ssl = nullptr;
        mbedtls_ssl_config* _conf = nullptr;
    };
}
