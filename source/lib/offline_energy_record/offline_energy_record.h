// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure, dependency-free binary codec for one offline energy record: the active
// energy counters of every active channel at one wall-clock minute whose live
// energy snapshot could not be published (see openspec offline-energy-buffer).
//
// Layout, little-endian:
//   magic u16 | version u8 | channel count u8 | boundary unix s u32 | channel mask u32
//   count x (read-time offset from the boundary, ms, i16 | imported 0.1 Wh u32 | exported 0.1 Wh u32)
//   crc16 (CCITT-FALSE over everything before it)
//
// Counters are absolute, at the 0.1 Wh resolution the cloud receives
// (ENERGY_DECIMALS), so storage is lossless with respect to what is published.
// The per-channel offset keeps each channel's real read time, which is what
// makes a replayed reading land on the exact cloud key a live one would have.
// The mask makes every record self-describing when channels change mid-outage,
// and the magic + CRC let a reader skip a record torn by power loss.

namespace OfflineEnergyRecord {

constexpr uint16_t MAGIC = 0x454F; // "OE" on disk
constexpr uint8_t VERSION = 1;
constexpr uint8_t MAX_CHANNELS = 32; // Width of the channel mask
constexpr size_t HEADER_BYTES = 12;
constexpr size_t CHANNEL_BYTES = 10;
constexpr size_t CRC_BYTES = 2;
constexpr size_t MAX_RECORD_BYTES = HEADER_BYTES + MAX_CHANNELS * CHANNEL_BYTES + CRC_BYTES;

struct ChannelReading {
    uint8_t channel;
    uint64_t unixTimeMs;
    double activeEnergyImportedWh;
    double activeEnergyExportedWh;
};

struct DecodedChannel {
    uint8_t channel;
    uint64_t unixTimeMs;
    uint32_t activeEnergyImportedDeciWh;
    uint32_t activeEnergyExportedDeciWh;
};

constexpr size_t recordBytes(uint8_t channelCount) {
    return HEADER_BYTES + (size_t)channelCount * CHANNEL_BYTES + CRC_BYTES;
}

// Wh -> 0.1 Wh units, rounded to nearest. False for NaN, negative or a value
// that does not fit a uint32 (429 MWh), so the caller skips the channel
// instead of storing a wrapped counter.
bool toDeciWh(double wh, uint32_t& out);

uint16_t crc16(const uint8_t* data, size_t length);

// Encodes `readings` (channels strictly ascending; any out-of-order or
// duplicate channel is skipped) into `out`. A channel is skipped when its
// channel index is >= MAX_CHANNELS, its read time is not within an i16 ms
// offset of the boundary, or a counter fails toDeciWh. Returns the bytes
// written, or 0 when no channel was encodable or `outSize` is too small.
size_t encode(uint32_t boundaryUnixSeconds, const ChannelReading* readings, size_t readingCount,
              uint8_t* out, size_t outSize);

// Decodes the record at the start of `data`. Returns the bytes it occupies,
// or 0 when it is truncated, has a bad magic/version/CRC, its count does not
// match its mask, or it holds more channels than `outCapacity`.
size_t decode(const uint8_t* data, size_t length, uint32_t& boundaryUnixSeconds,
              DecodedChannel* out, size_t outCapacity, size_t& outCount);

// Offset of the next possible record start (magic bytes) at or after `from`,
// or `length` when there is none. Lets a reader resynchronize past a torn record.
size_t findNextMagic(const uint8_t* data, size_t length, size_t from);

}  // namespace OfflineEnergyRecord
