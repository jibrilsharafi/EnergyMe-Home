// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <LittleFS.h>

#include "constants.h"
#include "customtime.h"
#include "globals.h"
#include "offline_energy_record.h"
#include "offline_energy_segments.h"
#include "utils.h"

// Flash-backed buffer of active energy counters for the minutes whose live
// energy snapshot could not reach the cloud, replayed on the energy topic once
// it is reachable again (openspec offline-energy-buffer). One segment file per
// wall-clock hour, trimmed oldest-first by age, byte budget and free space.

#define OFFLINE_ENERGY_DIR "/offline_energy" // Outside /energy: the hourly CSV task migrates and compresses files there
#define OFFLINE_ENERGY_MAX_AGE_SECONDS (14UL * 24 * 60 * 60) // The cloud rejects a whole message holding anything older than 15 days
#define OFFLINE_ENERGY_BUDGET_BYTES (3584UL * 1024) // 14 days of 16-channel records (174 B/min)
#define OFFLINE_ENERGY_HEADROOM_BYTES (512UL * 1024) // Free LittleFS kept for logs and the energy CSVs
#define OFFLINE_ENERGY_MAX_SEGMENTS 400 // Above the 336 hourly segments of 14 days, to absorb clock jumps
#define OFFLINE_ENERGY_SEGMENT_MAX_BYTES (60 * OfflineEnergyRecord::MAX_RECORD_BYTES) // One record per minute of the hour
#define OFFLINE_ENERGY_ELEMENT_MAX_JSON_BYTES 114 // Worst-case replay element (counters in exponent form) plus its separating comma

namespace OfflineEnergy
{
    void begin();

    // Appends one record holding `readings`, trimming old data first. The
    // record is based on its earliest reading rather than the nominal minute,
    // so a capture that ran late still fits the codec's per-channel offset.
    // Dropped when `generation` is stale, i.e. the buffer was cleared since the
    // readings were taken. Returns false when nothing was stored.
    bool append(const OfflineEnergyRecord::ChannelReading* readings, size_t count, uint32_t generation);

    // Incremented by every clear()
    uint32_t getGeneration();

    bool hasBacklog();

    // Fills `doc` (a bare JSON array) with the oldest buffered readings, at most
    // `maxPayloadBytes` once serialized, skipping anything older than the max
    // age. Returns false when there is nothing to publish. After a successful
    // publish the caller must call commitReplayChunk(), otherwise the same
    // readings are offered again on the next call.
    bool prepareReplayChunk(JsonDocument& doc, size_t maxPayloadBytes);
    void commitReplayChunk();

    void clear();

    uint32_t getSegmentCount();
    uint32_t getBufferedBytes();
}
