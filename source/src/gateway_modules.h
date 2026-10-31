// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

// Internal interface between the gateway executor and its protocol modules.
// Not part of the public API (include/gateway_executor.h).

#include "gateway_executor.h"

namespace GatewayExec
{
    // A request that passed every pre-socket check, with its expanded wire bytes.
    struct Prepared {
        const Request* request;
        uint8_t octets[4];
        uint32_t deadline;
        uint32_t start;
        const uint8_t* wire; // http(s): the whole request; tcp/udp: the payload
        size_t wireLength;
        uint8_t pin[GatewayLogic::DIGEST_BYTES]; // https
        // ARP compare to run after connect, before the first byte.
        bool checkMac;
        uint8_t mac[6];
        const char* macReason; // reasonCode on a mismatch
    };

    void setOutcome(Result& result, Status status, const char* code, const char* detailFormat = nullptr, ...);

    // False (and `result` set to REJECTED) when the ARP compare fails.
    // `udp` sends one ARP request and polls when the entry is missing.
    bool checkMacBeforeFirstByte(const Prepared& prepared, Result& result, bool udp);

    // Connects, runs the MAC compare, the TLS handshake and pin check when `tls`,
    // and sends `data` in one write. Sets `connected` and the connect timing;
    // false with `result` set on failure.
    bool connectAndSend(GatewayNet::Connection& connection, const Prepared& prepared, Result& result, bool tls, const uint8_t* data,
                        size_t length);

    void runHttp(const Prepared& prepared, Result& result, bool tls);
    void runModbus(const Prepared& prepared, Result& result);
    void runTcp(const Prepared& prepared, Result& result);
    void runUdp(const Prepared& prepared, Result& result);

    inline uint32_t elapsedSince(uint32_t start) { return millis() - start; }
    inline bool beforeDeadline(uint32_t deadline) { return (int32_t)(deadline - millis()) > 0; }
}
