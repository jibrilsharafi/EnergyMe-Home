// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>
#include <cstdint>

// Pure logic for the gateway discovery ops (openspec change add-gateway-discovery):
// argument clamps, the SSDP M-SEARCH request and reply parser, the Wake-on-LAN
// packet. Sockets, the WiFi scan and mDNS live in gateway.cpp.

namespace GatewayLogic {

inline constexpr uint32_t DISCOVER_WAIT_DEFAULT_MS = 2000;
inline constexpr uint32_t DISCOVER_WAIT_MIN_MS = 200;
inline constexpr uint32_t DISCOVER_WAIT_MAX_MS = 5000;
inline constexpr uint32_t LISTEN_WAIT_DEFAULT_MS = 3000;
inline constexpr uint32_t LISTEN_WAIT_MIN_MS = 100;
inline constexpr uint32_t LISTEN_WAIT_MAX_MS = 10000;
inline constexpr uint32_t MDNS_TIMEOUT_DEFAULT_MS = 1500;
inline constexpr uint32_t MDNS_TIMEOUT_MIN_MS = 200;
inline constexpr uint32_t MDNS_TIMEOUT_MAX_MS = 3000;
inline constexpr uint16_t LISTEN_PORT_MIN = 1024;
inline constexpr uint16_t MDNS_PORT = 5353;
inline constexpr uint8_t WIFI_CHANNEL_MAX = 13;

inline constexpr uint16_t SSDP_PORT = 1900;
inline constexpr uint8_t SSDP_GROUP[4] = {239, 255, 255, 250};
inline constexpr size_t SSDP_ST_MAX = 128;
inline constexpr size_t SSDP_REQUEST_MAX = 256;
inline constexpr size_t SSDP_VALUE_MAX = 192; // per header value, cut beyond

inline constexpr size_t WOL_PACKET_BYTES = 102;

uint32_t clampDiscoverWaitMs(int64_t requested); // <= 0 -> default, else 200-5000
uint32_t clampListenWaitMs(int64_t requested);   // <= 0 -> default, else 100-10000
uint32_t clampMdnsTimeoutMs(int64_t requested);  // <= 0 -> default, else 200-3000

// 1024-65535 and not the mDNS port.
bool isListenPortAllowed(int64_t port);

// `ST`: 1-128 printable ASCII characters, no space.
bool isValidSsdpTarget(const char *st);

// M-SEARCH with MX = wait seconds (1-5). Returns the length, 0 if `st` is invalid or `cap` too small.
size_t buildSsdpSearch(const char *st, uint32_t waitMs, char *out, size_t cap);

struct SsdpReply {
    char location[SSDP_VALUE_MAX + 1];
    char st[SSDP_VALUE_MAX + 1];
    char usn[SSDP_VALUE_MAX + 1];
    char server[SSDP_VALUE_MAX + 1];
};

// An `HTTP/1.x 200` reply to an M-SEARCH. Header names are case-insensitive;
// values are trimmed, cut at SSDP_VALUE_MAX, and left empty when absent or not
// printable ASCII. False when the status line is not a 200.
bool parseSsdpReply(const uint8_t *data, size_t len, SsdpReply *out);

// Six 0xFF then sixteen copies of the MAC. Returns WOL_PACKET_BYTES, 0 if `cap` is too small.
size_t buildWakeOnLan(const uint8_t mac[6], uint8_t *out, size_t cap);

// Host-order directed broadcast of the subnet; 0 for a /31 or /32, which has none.
uint32_t directedBroadcast(uint32_t address, uint32_t netmask);

} // namespace GatewayLogic
