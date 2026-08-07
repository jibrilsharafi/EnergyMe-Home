// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "crash_archive_policy.h"

namespace CrashArchivePolicy {

namespace {

// A PUBLISH packet's remaining length is encoded in at most 16 bits by
// PubSubClient 2.8.0 (see the header for why), and covers the 2-byte topic
// length prefix, the topic itself and the payload.
const size_t MQTT_REMAINING_LENGTH_MAX = 65535;
const size_t MQTT_TOPIC_LENGTH_PREFIX = 2;

// Largest input base64EncodedSize() can size without its 4/3 expansion
// overflowing size_t. Inputs are bounded by the 64 KB coredump partition in
// practice; this only keeps a synthetic value from wrapping into a small
// result that would read as "fits".
const size_t BASE64_MAX_SIZEABLE_INPUT = ((size_t)-1 / 4) * 3;

}  // namespace

size_t base64EncodedSize(size_t rawSize) {
    if (rawSize > BASE64_MAX_SIZEABLE_INPUT) return (size_t)-1;
    return ((rawSize + 2) / 3) * 4;
}

size_t maxPublishPayloadBytes(size_t topicLength) {
    const size_t overhead = topicLength + MQTT_TOPIC_LENGTH_PREFIX;
    if (overhead >= MQTT_REMAINING_LENGTH_MAX) return 0;
    return MQTT_REMAINING_LENGTH_MAX - overhead;
}

bool fitsPublishLimit(size_t compressedSize, size_t metadataBytes, size_t topicLength) {
    const size_t budget = maxPublishPayloadBytes(topicLength);
    if (budget == 0) return false;

    const size_t encoded = base64EncodedSize(compressedSize);
    // Compared before the subtraction below so the remaining budget cannot wrap.
    if (encoded > budget) return false;

    return metadataBytes <= budget - encoded;
}

bool canStore(size_t incomingBytes, size_t maxBytes) {
    return incomingBytes <= maxBytes;
}

size_t evictionCount(const size_t *recordBytes, size_t recordCount, size_t incomingBytes,
                     size_t maxRecords, size_t maxBytes) {
    if (recordBytes == nullptr) recordCount = 0;
    if (maxRecords == 0 || !canStore(incomingBytes, maxBytes)) return recordCount;

    size_t total = 0;
    for (size_t i = 0; i < recordCount; i++) total += recordBytes[i];

    // Drops oldest-first until the incoming record fits under both caps. It
    // always terminates: with everything evicted the archive holds one record
    // of incomingBytes, which canStore() has already cleared.
    size_t evicted = 0;
    while (evicted < recordCount &&
           ((recordCount - evicted) + 1 > maxRecords || total + incomingBytes > maxBytes)) {
        total -= recordBytes[evicted];
        evicted++;
    }

    return evicted;
}

}  // namespace CrashArchivePolicy
