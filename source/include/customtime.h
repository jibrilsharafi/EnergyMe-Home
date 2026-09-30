// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Preferences.h>

#include "constants.h"
#include "utils.h"
#include "unix_time.h"

// Fallback servers, tried after the default gateway (see CustomTime::begin/_checkAndSyncTime), and
// used alone for the resync after a rejected answer (below the time floor, or a zero transmit timestamp) -
// NTP_SERVER_1 is a DNS-dependent public pool, NTP_SERVER_2 a raw IP so it still works if DNS fails.
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "162.159.200.1" // Cloudflare NTP server IP

#define TIME_SYNC_INTERVAL (60 * 60 * 1000)
#define TIME_SYNC_RETRY_IF_NOT_SYNCHED (60 * 1000)

// PREFERENCES_NAMESPACE_TIME: the persisted half of the time floor, the latest accepted NTP time capped at the uptime ceiling (lower after a manual set)
#define TIME_FLOOR_KEY "floor_s"

#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_ISO_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ" // ISO 8601 format with milliseconds
#define DATE_FORMAT "%Y-%m-%d"
#define DATE_ISO_FORMAT "%04d-%02d-%02d"

namespace CustomTime {
    bool begin();
    // No need to stop anything here since once it executes at the beginning, there is no other use for this

    // This function is called frequently from other functions, ensuring that we check and sync time if needed
    bool isTimeSynched();
    bool isNowCloseToHour(uint64_t toleranceMillis = 60000);
    bool isNowHourZero();

    uint64_t getUnixTime();
    uint64_t getUnixTimeMilliseconds();
    void getTimestampIso(char* buffer, size_t bufferSize);
    void getTimestampIsoRoundedToHour(char* buffer, size_t bufferSize);
    void getCurrentDateIso(char* buffer, size_t bufferSize);
    void getDateIsoOffset(char *outBuf, size_t outBufLen, int offsetDays);
    // UTC date of the nearest hour (+ offsetDays): the date an hourly save belongs to
    void getDateIsoOfNearestHour(char* buffer, size_t bufferSize, int offsetDays = 0);

    uint64_t getMillisecondsUntilNextHour();

    void timestampIsoFromUnix(time_t unix, char* buffer, size_t bufferSize);

    bool isUnixTimeValid(uint64_t unixTime, bool isMilliseconds = true);

    // Manual time sync for devices without internet connectivity. Can only lower the time floor
    // (the way out of a bad persisted floor), and never lets NTP raise it past this time plus the
    // uptime since. The build floor still applies after a reboot.
    bool setUnixTime(uint64_t unixSeconds);

    // Forces the next sync check to run immediately. Called on interface failover:
    // the gateway-derived NTP server belongs to the old interface until then.
    void requestResync();
}