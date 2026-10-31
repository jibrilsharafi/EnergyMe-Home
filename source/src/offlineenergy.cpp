// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "offlineenergy.h"

#define OFFLINE_ENERGY_PATH_BUFFER_SIZE (sizeof(OFFLINE_ENERGY_DIR) + OfflineEnergySegments::SEGMENT_NAME_BUFFER_SIZE + 1)
#define OFFLINE_ENERGY_MUTEX_TIMEOUT_MS 5000

namespace OfflineEnergy
{
    using OfflineEnergySegments::Segment;

    static SemaphoreHandle_t _mutex = nullptr;

    // Oldest-first mirror of the segment files, so the per-minute append and the
    // replay never have to scan the directory
    static Segment* _segments = nullptr;
    static size_t _segmentCount = 0;
    static volatile uint32_t _totalBytes = 0; // Kept under the mutex, read lock-free by statistics from other tasks

    // Replay position inside the oldest segment; offered by prepareReplayChunk()
    // and only applied by commitReplayChunk() once the chunk is published
    static uint32_t _replayHour = 0;
    static size_t _replayOffset = 0;
    static size_t _pendingOffset = 0;
    static bool _pendingSegmentDone = false;
    static bool _pendingValid = false;

    static uint8_t* _readBuffer = nullptr; // One whole segment, in PSRAM
    static OfflineEnergyRecord::DecodedChannel _decoded[OfflineEnergyRecord::MAX_CHANNELS]; // Static: the MQTT task stack is tight
    static volatile uint32_t _generation = 0;

    static bool _isReady() {
        return _segments != nullptr && _readBuffer != nullptr;
    }

    static void _pathFor(uint32_t hour, char* out, size_t outSize) {
        char name[OfflineEnergySegments::SEGMENT_NAME_BUFFER_SIZE];
        OfflineEnergySegments::formatName(hour, name, sizeof(name));
        snprintf(out, outSize, "%s/%s", OFFLINE_ENERGY_DIR, name);
    }

    static void _resetReplayCursor() {
        _replayHour = 0;
        _replayOffset = 0;
        _pendingValid = false;
    }

    static void _dropFront(size_t count) {
        if (count > _segmentCount) count = _segmentCount;
        for (size_t i = 0; i < count; i++) {
            char path[OFFLINE_ENERGY_PATH_BUFFER_SIZE];
            _pathFor(_segments[i].hourUnixSeconds, path, sizeof(path));
            if (!LittleFS.remove(path)) LOG_WARNING("Failed to remove offline energy segment %s", path);
            if (_segments[i].hourUnixSeconds == _replayHour) _resetReplayCursor();
            _totalBytes = _totalBytes - (uint32_t)_segments[i].bytes;
        }
        memmove(_segments, _segments + count, (_segmentCount - count) * sizeof(Segment));
        _segmentCount -= count;
    }

    // Keeps the mirror sorted even if the clock jumps back into an older hour
    static bool _addBytes(uint32_t hour, size_t bytes) {
        size_t pos = _segmentCount;
        while (pos > 0 && _segments[pos - 1].hourUnixSeconds > hour) pos--;
        if (pos > 0 && _segments[pos - 1].hourUnixSeconds == hour) {
            _segments[pos - 1].bytes += bytes;
            _totalBytes = _totalBytes + (uint32_t)bytes;
            return true;
        }
        if (_segmentCount >= OFFLINE_ENERGY_MAX_SEGMENTS) return false;
        memmove(_segments + pos + 1, _segments + pos, (_segmentCount - pos) * sizeof(Segment));
        _segments[pos] = {hour, bytes};
        _segmentCount++;
        _totalBytes = _totalBytes + (uint32_t)bytes;
        return true;
    }

    static void _scanDirectory() {
        _segmentCount = 0;
        _totalBytes = 0;
        if (!LittleFS.exists(OFFLINE_ENERGY_DIR) && !LittleFS.mkdir(OFFLINE_ENERGY_DIR)) {
            LOG_ERROR("Failed to create %s", OFFLINE_ENERGY_DIR);
            return;
        }

        File dir = LittleFS.open(OFFLINE_ENERGY_DIR);
        if (!dir) return;

        uint32_t loops = 0;
        File file = dir.openNextFile();
        while (file && loops++ < MAX_LOOP_ITERATIONS) {
            uint32_t hour = 0;
            if (!file.isDirectory() && OfflineEnergySegments::parseName(file.name(), hour)) {
                if (!_addBytes(hour, file.size())) LOG_WARNING("Offline energy segment table full, ignoring %s", file.name());
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }

    void begin() {
        if (!createMutexIfNeeded(&_mutex)) {
            LOG_ERROR("Failed to create offline energy mutex");
            return;
        }
        if (_segments == nullptr) _segments = (Segment*)ps_malloc(OFFLINE_ENERGY_MAX_SEGMENTS * sizeof(Segment));
        if (_readBuffer == nullptr) _readBuffer = (uint8_t*)ps_malloc(OFFLINE_ENERGY_SEGMENT_MAX_BYTES);
        if (_segments == nullptr || _readBuffer == nullptr) {
            LOG_ERROR("Failed to allocate offline energy buffers");
            return;
        }

        if (!acquireMutex(&_mutex, OFFLINE_ENERGY_MUTEX_TIMEOUT_MS)) return;
        _scanDirectory();
        _resetReplayCursor();
        uint32_t segments = (uint32_t)_segmentCount;
        releaseMutex(&_mutex);

        if (segments > 0) LOG_INFO("Offline energy buffer holds %lu segments to replay", (unsigned long)segments);
    }

    uint32_t getGeneration() {
        return _generation;
    }

    bool append(const OfflineEnergyRecord::ChannelReading* readings, size_t count, uint32_t generation) {
        if (!_isReady() || readings == nullptr || count == 0) return false;

        // Based on the latest reading: a channel whose reads stalled keeps an old timestamp
        // and is the one that falls out of the offset range, not every fresh channel
        uint64_t latestMs = 0;
        for (size_t i = 0; i < count; i++) {
            if (readings[i].unixTimeMs > latestMs) latestMs = readings[i].unixTimeMs;
        }
        uint32_t boundaryUnixSeconds = (uint32_t)(latestMs / 1000);

        uint8_t record[OfflineEnergyRecord::MAX_RECORD_BYTES];
        size_t length = OfflineEnergyRecord::encode(boundaryUnixSeconds, readings, count, record, sizeof(record));
        if (length == 0) {
            LOG_WARNING("No encodable channel for offline energy record at %lu, dropping it", (unsigned long)boundaryUnixSeconds);
            return false;
        }

        if (!acquireMutex(&_mutex, OFFLINE_ENERGY_MUTEX_TIMEOUT_MS)) return false;

        if (generation != _generation) {
            releaseMutex(&_mutex);
            LOG_DEBUG("Offline energy record taken before a buffer clear, dropping it");
            return false;
        }

        uint32_t hour = OfflineEnergySegments::segmentHour(boundaryUnixSeconds);
        size_t fsFree = LittleFS.totalBytes() - LittleFS.usedBytes();
        OfflineEnergySegments::TrimPlan plan = OfflineEnergySegments::planTrim(
            _segments, _segmentCount, hour, CustomTime::getUnixTime(), OFFLINE_ENERGY_MAX_AGE_SECONDS,
            length, OFFLINE_ENERGY_BUDGET_BYTES, fsFree, OFFLINE_ENERGY_HEADROOM_BYTES);

        if (plan.dropCount > 0) {
            LOG_INFO("Dropping %u oldest offline energy segments (age, budget or free space)", (unsigned)plan.dropCount);
            _dropFront(plan.dropCount);
            statistics.mqttOfflineEnergySegmentsDropped += plan.dropCount;
        }

        // A full table means the clock jumped across more hours than 14 days can hold
        if (_segmentCount >= OFFLINE_ENERGY_MAX_SEGMENTS && _segments[_segmentCount - 1].hourUnixSeconds != hour) {
            _dropFront(1);
            statistics.mqttOfflineEnergySegmentsDropped++;
        }

        if (!plan.canAppend) {
            releaseMutex(&_mutex);
            LOG_WARNING("No room for offline energy record at %lu, dropping it", (unsigned long)boundaryUnixSeconds);
            return false;
        }

        char path[OFFLINE_ENERGY_PATH_BUFFER_SIZE];
        _pathFor(hour, path, sizeof(path));
        File file = LittleFS.open(path, FILE_APPEND);
        size_t written = file ? file.write(record, length) : 0;
        if (file) file.close();

        bool ok = written == length;
        if (written > 0) _addBytes(hour, written); // A short write still occupies flash; the reader skips it by CRC
        releaseMutex(&_mutex);

        if (!ok) {
            LOG_ERROR("Failed to append offline energy record to %s (%u of %u bytes)", path, (unsigned)written, (unsigned)length);
            return false;
        }
        statistics.mqttOfflineEnergyRecordsCaptured++;
        LOG_DEBUG("Buffered offline energy record at %lu (%u bytes)", (unsigned long)boundaryUnixSeconds, (unsigned)length);
        return true;
    }

    bool hasBacklog() {
        return _isReady() && _segmentCount > 0;
    }

    bool prepareReplayChunk(JsonDocument& doc, size_t maxPayloadBytes) {
        if (!_isReady()) return false;
        if (!acquireMutex(&_mutex, OFFLINE_ENERGY_MUTEX_TIMEOUT_MS)) return false;

        _pendingValid = false;
        uint64_t nowUnixSeconds = CustomTime::getUnixTime();

        size_t expired = 0;
        while (expired < _segmentCount &&
               OfflineEnergySegments::isExpired(_segments[expired].hourUnixSeconds, nowUnixSeconds, OFFLINE_ENERGY_MAX_AGE_SECONDS)) {
            expired++;
        }
        if (expired > 0) {
            _dropFront(expired);
            statistics.mqttOfflineEnergySegmentsDropped += expired;
        }

        if (_segmentCount == 0) {
            releaseMutex(&_mutex);
            return false;
        }

        uint32_t hour = _segments[0].hourUnixSeconds;
        if (hour != _replayHour) {
            _replayHour = hour;
            _replayOffset = 0;
        }

        char path[OFFLINE_ENERGY_PATH_BUFFER_SIZE];
        _pathFor(hour, path, sizeof(path));
        File file = LittleFS.open(path, FILE_READ);
        if (!file) {
            LOG_WARNING("Offline energy segment %s missing, dropping it from the table", path);
            _dropFront(1);
            statistics.mqttOfflineEnergySegmentsDropped++;
            releaseMutex(&_mutex);
            return false;
        }

        size_t fileSize = file.size();
        size_t length = 0;
        bool readFailed = false;
        if (_replayOffset < fileSize) {
            size_t toRead = fileSize - _replayOffset;
            if (toRead > OFFLINE_ENERGY_SEGMENT_MAX_BYTES) toRead = OFFLINE_ENERGY_SEGMENT_MAX_BYTES;
            readFailed = !file.seek(_replayOffset);
            if (!readFailed) length = file.read(_readBuffer, toRead);
            readFailed = readFailed || length == 0;
        }
        file.close();

        // An unreadable segment would otherwise block every newer one until it ages out
        if (readFailed) {
            LOG_WARNING("Failed to read offline energy segment %s, dropping it", path);
            _dropFront(1);
            statistics.mqttOfflineEnergySegmentsDropped++;
            releaseMutex(&_mutex);
            return false;
        }

        size_t maxElements = maxPayloadBytes / OFFLINE_ENERGY_ELEMENT_MAX_JSON_BYTES;
        size_t elements = 0;
        size_t pos = 0;
        uint32_t loops = 0;

        while (pos < length && loops++ < MAX_LOOP_ITERATIONS) {
            uint32_t boundary = 0;
            size_t channelCount = 0;
            size_t used = OfflineEnergyRecord::decode(_readBuffer + pos, length - pos, boundary,
                                                      _decoded, OfflineEnergyRecord::MAX_CHANNELS, channelCount);
            if (used == 0) {
                // A record cut by the read limit is read whole on the next chunk, not skipped as torn
                bool moreInFile = _replayOffset + length < fileSize;
                if (moreInFile && length - pos < OfflineEnergyRecord::MAX_RECORD_BYTES) break;
                pos = OfflineEnergyRecord::findNextMagic(_readBuffer, length, pos + 1);
                continue;
            }
            if (elements > 0 && elements + channelCount > maxElements) break;

            for (size_t i = 0; i < channelCount; i++) {
                const OfflineEnergyRecord::DecodedChannel& c = _decoded[i];
                if (!OfflineEnergySegments::isReadingFresh(c.unixTimeMs, nowUnixSeconds, OFFLINE_ENERGY_MAX_AGE_SECONDS)) continue;
                JsonObject obj = doc.add<JsonObject>();
                obj["unixTime"] = c.unixTimeMs;
                obj["channel"] = c.channel;
                obj["activeEnergyImported"] = c.activeEnergyImportedDeciWh / 10.0;
                obj["activeEnergyExported"] = c.activeEnergyExportedDeciWh / 10.0;
                elements++;
            }
            pos += used;
        }

        _pendingOffset = _replayOffset + pos;
        _pendingSegmentDone = _pendingOffset >= fileSize;
        _pendingValid = true;
        releaseMutex(&_mutex);

        // Nothing publishable in what was read (stale or corrupt): move past it now
        if (elements == 0) {
            commitReplayChunk();
            return false;
        }
        return true;
    }

    void commitReplayChunk() {
        if (!acquireMutex(&_mutex, OFFLINE_ENERGY_MUTEX_TIMEOUT_MS)) return;
        if (_pendingValid && _segmentCount > 0 && _segments[0].hourUnixSeconds == _replayHour) {
            if (_pendingSegmentDone) {
                _dropFront(1);
            } else {
                _replayOffset = _pendingOffset;
            }
        }
        _pendingValid = false;
        releaseMutex(&_mutex);
    }

    void clear() {
        if (!createMutexIfNeeded(&_mutex)) return;
        if (!acquireMutex(&_mutex, OFFLINE_ENERGY_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire offline energy mutex to clear the buffer");
            return;
        }

        if (_segments != nullptr) _dropFront(_segmentCount);

        // Also catch files the table never knew about (table full, or begin() not run).
        // One file per directory open: removing entries mid-iteration can skip some.
        uint32_t loops = 0;
        while (loops++ < MAX_LOOP_ITERATIONS) {
            File dir = LittleFS.open(OFFLINE_ENERGY_DIR);
            if (!dir) break;
            char path[OFFLINE_ENERGY_PATH_BUFFER_SIZE] = "";
            File file = dir.openNextFile();
            while (file && file.isDirectory()) {
                file.close();
                file = dir.openNextFile();
            }
            if (file) {
                snprintf(path, sizeof(path), "%s/%s", OFFLINE_ENERGY_DIR, file.name());
                file.close();
            }
            dir.close();
            if (path[0] == '\0' || !LittleFS.remove(path)) break;
        }

        _resetReplayCursor();
        _generation = _generation + 1;
        releaseMutex(&_mutex);
        LOG_INFO("Offline energy buffer cleared");
    }

    uint32_t getSegmentCount() {
        return (uint32_t)_segmentCount;
    }

    uint32_t getBufferedBytes() {
        return _totalBytes;
    }
}
