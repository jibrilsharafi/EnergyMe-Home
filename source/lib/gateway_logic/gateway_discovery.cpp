// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_discovery.h"

#include <cstdio>
#include <cstring>

namespace GatewayLogic {

namespace {

uint32_t clamp(int64_t requested, uint32_t fallback, uint32_t low, uint32_t high) {
    if (requested <= 0) return fallback;
    if (requested < low) return low;
    if (requested > high) return high;
    return static_cast<uint32_t>(requested);
}

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool namesEqual(const char *line, size_t length, const char *name) {
    size_t n = strlen(name);
    if (length != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (lower(line[i]) != name[i]) return false;
    }
    return true;
}

// Trims spaces and tabs around the value; empty unless every byte is printable ASCII.
void copyValue(const char *value, size_t length, char *out) {
    while (length > 0 && (*value == ' ' || *value == '\t')) {
        value++;
        length--;
    }
    while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t')) length--;
    if (length > SSDP_VALUE_MAX) length = SSDP_VALUE_MAX;
    for (size_t i = 0; i < length; i++) {
        if (value[i] < 0x20 || value[i] > 0x7E) {
            out[0] = '\0';
            return;
        }
    }
    memcpy(out, value, length);
    out[length] = '\0';
}

} // namespace

uint32_t clampDiscoverWaitMs(int64_t requested) {
    return clamp(requested, DISCOVER_WAIT_DEFAULT_MS, DISCOVER_WAIT_MIN_MS, DISCOVER_WAIT_MAX_MS);
}

uint32_t clampListenWaitMs(int64_t requested) {
    return clamp(requested, LISTEN_WAIT_DEFAULT_MS, LISTEN_WAIT_MIN_MS, LISTEN_WAIT_MAX_MS);
}

uint32_t clampMdnsTimeoutMs(int64_t requested) {
    return clamp(requested, MDNS_TIMEOUT_DEFAULT_MS, MDNS_TIMEOUT_MIN_MS, MDNS_TIMEOUT_MAX_MS);
}

bool isListenPortAllowed(int64_t port) { return port >= LISTEN_PORT_MIN && port <= 65535 && port != MDNS_PORT; }

bool isValidSsdpTarget(const char *st) {
    if (st == nullptr || st[0] == '\0') return false;
    size_t n = 0;
    for (; st[n] != '\0'; n++) {
        if (n >= SSDP_ST_MAX || st[n] <= 0x20 || st[n] > 0x7E) return false;
    }
    return true;
}

size_t buildSsdpSearch(const char *st, uint32_t waitMs, char *out, size_t cap) {
    if (!isValidSsdpTarget(st) || out == nullptr) return 0;
    uint32_t mx = waitMs / 1000;
    if (mx < 1) mx = 1;
    if (mx > 5) mx = 5;
    int n = snprintf(out, cap,
                     "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: %u\r\nST: %s\r\n\r\n",
                     static_cast<unsigned>(mx), st);
    return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

bool parseSsdpReply(const uint8_t *data, size_t len, SsdpReply *out) {
    if (data == nullptr || out == nullptr) return false;
    memset(out, 0, sizeof(*out));
    const char *text = reinterpret_cast<const char *>(data);
    if (len < 12 || memcmp(text, "HTTP/1.", 7) != 0 || text[8] != ' ' || memcmp(text + 9, "200", 3) != 0) return false;
    if (len > 12 && text[12] != ' ' && text[12] != '\r') return false;

    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && text[end] != '\n') end++;
        size_t lineLength = end - pos;
        if (lineLength > 0 && text[pos + lineLength - 1] == '\r') lineLength--;
        if (lineLength == 0 && pos > 0) break; // blank line: end of headers
        const char *line = text + pos;
        const char *colon = static_cast<const char *>(memchr(line, ':', lineLength));
        if (colon != nullptr) {
            size_t nameLength = static_cast<size_t>(colon - line);
            const char *value = colon + 1;
            size_t valueLength = lineLength - nameLength - 1;
            if (namesEqual(line, nameLength, "location")) copyValue(value, valueLength, out->location);
            else if (namesEqual(line, nameLength, "st")) copyValue(value, valueLength, out->st);
            else if (namesEqual(line, nameLength, "usn")) copyValue(value, valueLength, out->usn);
            else if (namesEqual(line, nameLength, "server")) copyValue(value, valueLength, out->server);
        }
        pos = end + 1;
    }
    return true;
}

size_t buildWakeOnLan(const uint8_t mac[6], uint8_t *out, size_t cap) {
    if (mac == nullptr || out == nullptr || cap < WOL_PACKET_BYTES) return 0;
    memset(out, 0xFF, 6);
    for (size_t i = 0; i < 16; i++) memcpy(out + 6 + i * 6, mac, 6);
    return WOL_PACKET_BYTES;
}

uint32_t directedBroadcast(uint32_t address, uint32_t netmask) {
    if ((~netmask) <= 1u) return 0;
    return address | ~netmask;
}

} // namespace GatewayLogic
