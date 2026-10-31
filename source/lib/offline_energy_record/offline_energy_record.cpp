// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "offline_energy_record.h"

#include <math.h>

namespace OfflineEnergyRecord {

namespace {

void putU16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

void putU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

uint16_t getU16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t popcount32(uint32_t v) {
    uint8_t n = 0;
    while (v) { v &= v - 1; n++; }
    return n;
}

}  // namespace

bool toDeciWh(double wh, uint32_t& out) {
    if (isnan(wh) || wh < 0.0) return false;
    double deci = floor(wh * 10.0 + 0.5);
    if (deci > (double)UINT32_MAX) return false;
    out = (uint32_t)deci;
    return true;
}

uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

size_t encode(uint32_t boundaryUnixSeconds, const ChannelReading* readings, size_t readingCount,
              uint8_t* out, size_t outSize) {
    if (readings == nullptr || out == nullptr) return 0;

    const int64_t boundaryMs = (int64_t)boundaryUnixSeconds * 1000;
    uint32_t mask = 0;
    uint8_t count = 0;
    int lastChannel = -1;
    uint8_t* body = out + HEADER_BYTES;

    for (size_t i = 0; i < readingCount; i++) {
        const ChannelReading& r = readings[i];
        if (r.channel >= MAX_CHANNELS || (int)r.channel <= lastChannel) continue;

        int64_t offsetMs = (int64_t)r.unixTimeMs - boundaryMs;
        if (offsetMs < INT16_MIN || offsetMs > INT16_MAX) continue;

        uint32_t imported = 0;
        uint32_t exported = 0;
        if (!toDeciWh(r.activeEnergyImportedWh, imported) || !toDeciWh(r.activeEnergyExportedWh, exported)) continue;

        if (recordBytes((uint8_t)(count + 1)) > outSize) return 0;

        uint8_t* entry = body + (size_t)count * CHANNEL_BYTES;
        putU16(entry, (uint16_t)(int16_t)offsetMs);
        putU32(entry + 2, imported);
        putU32(entry + 6, exported);

        mask |= (1UL << r.channel);
        lastChannel = r.channel;
        count++;
    }

    if (count == 0) return 0;

    putU16(out, MAGIC);
    out[2] = VERSION;
    out[3] = count;
    putU32(out + 4, boundaryUnixSeconds);
    putU32(out + 8, mask);

    size_t crcAt = HEADER_BYTES + (size_t)count * CHANNEL_BYTES;
    putU16(out + crcAt, crc16(out, crcAt));
    return crcAt + CRC_BYTES;
}

size_t decode(const uint8_t* data, size_t length, uint32_t& boundaryUnixSeconds,
              DecodedChannel* out, size_t outCapacity, size_t& outCount) {
    outCount = 0;
    if (data == nullptr || length < HEADER_BYTES + CRC_BYTES) return 0;
    if (getU16(data) != MAGIC || data[2] != VERSION) return 0;

    uint8_t count = data[3];
    uint32_t mask = getU32(data + 8);
    if (count == 0 || popcount32(mask) != count) return 0;
    if (count > outCapacity) return 0;

    size_t total = recordBytes(count);
    if (length < total) return 0;

    size_t crcAt = total - CRC_BYTES;
    if (getU16(data + crcAt) != crc16(data, crcAt)) return 0;

    boundaryUnixSeconds = getU32(data + 4);
    const int64_t boundaryMs = (int64_t)boundaryUnixSeconds * 1000;
    const uint8_t* entry = data + HEADER_BYTES;

    for (uint8_t channel = 0; channel < MAX_CHANNELS; channel++) {
        if (!(mask & (1UL << channel))) continue;
        DecodedChannel& d = out[outCount++];
        d.channel = channel;
        d.unixTimeMs = (uint64_t)(boundaryMs + (int16_t)getU16(entry));
        d.activeEnergyImportedDeciWh = getU32(entry + 2);
        d.activeEnergyExportedDeciWh = getU32(entry + 6);
        entry += CHANNEL_BYTES;
    }
    return total;
}

size_t findNextMagic(const uint8_t* data, size_t length, size_t from) {
    if (data == nullptr) return length;
    for (size_t i = from; i + 1 < length; i++) {
        if (getU16(data + i) == MAGIC) return i;
    }
    return length;
}

}  // namespace OfflineEnergyRecord
