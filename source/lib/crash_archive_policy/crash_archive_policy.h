// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <stddef.h>

// Pure, dependency-free sizing and retention rules for the on-flash crash
// archive and for the single-message MQTT crash publish.
//
// Two ceilings shape the publish guard:
//
//  1. PubSubClient 2.8.0 builds the MQTT "remaining length" field through
//     buildHeader(uint8_t, uint8_t*, uint16_t) while beginPublish() hands it a
//     32-bit length. A payload at or above 64 KiB is therefore truncated
//     modulo 2^16, the broker mis-frames the packet and the connection drops.
//     That bites well before AWS IoT Core's documented 128 KB publish limit,
//     so this - not the AWS limit - is what the guard is written against.
//
//  2. The coredump partition is 64 KB (partitions_esp32s3_n16r2.csv), so a raw
//     dump can never exceed that, and dumps are gzipped before they reach the
//     archive (typically 3-5x on task stacks and register frames). A published
//     payload normally lands around 20 KB. The guard exists to fail loudly if
//     some future dump ever compresses badly, not because it is expected to
//     be hit.

namespace CrashArchivePolicy {

// Bytes produced by base64-encoding `rawSize` bytes, excluding the null
// terminator: standard padded base64, 4 characters per 3 input bytes.
size_t base64EncodedSize(size_t rawSize);

// Largest JSON payload publishable on a topic of `topicLength` characters
// without tripping the remaining-length truncation described above. A PUBLISH
// packet's remaining length is 2 + topicLength + payload and must stay within
// 16 bits. Returns 0 when the topic alone leaves no room.
size_t maxPublishPayloadBytes(size_t topicLength);

// True when a crash message carrying `compressedSize` bytes of gzipped core
// dump, base64-encoded, plus `metadataBytes` of surrounding JSON, fits on a
// topic of `topicLength` characters.
bool fitsPublishLimit(size_t compressedSize, size_t metadataBytes, size_t topicLength);

// True when a record of `incomingBytes` could ever be stored under a
// `maxBytes` archive budget. Callers must check this before acting on
// evictionCount(), which cannot distinguish "evict everything, then store"
// from "this can never fit".
bool canStore(size_t incomingBytes, size_t maxBytes);

// How many of the oldest records must be dropped for the archive to satisfy
// both caps once a record of `incomingBytes` is added. `recordBytes` holds the
// existing record sizes oldest-first. Assumes canStore() already passed.
size_t evictionCount(const size_t *recordBytes, size_t recordCount, size_t incomingBytes,
                     size_t maxRecords, size_t maxBytes);

}  // namespace CrashArchivePolicy
