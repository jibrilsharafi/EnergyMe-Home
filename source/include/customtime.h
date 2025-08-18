#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Preferences.h>

#include "constants.h"
#include "utils.h"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.apple.com"

#define TIME_SYNC_INTERVAL (60 * 60 * 1000)
#define TIME_SYNC_RETRY_IF_NOT_SYNCHED (60 * 1000)

#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_ISO_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ" // ISO 8601 format with milliseconds
#define DATE_FORMAT "%Y-%m-%d"
#define DATE_ISO_FORMAT "%04d-%02d-%02d"

// Time utilities
#define MINIMUM_UNIX_TIME_SECONDS 1000000000 // Corresponds to 2001
#define MINIMUM_UNIX_TIME_MILLISECONDS 1000000000000 // Corresponds to 2001
#define MAXIMUM_UNIX_TIME_SECONDS 4102444800 // Corresponds to 2100
#define MAXIMUM_UNIX_TIME_MILLISECONDS 4102444800000 // Corresponds to 2100

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

    uint64_t getMillisecondsUntilNextHour();

    void timestampIsoFromUnix(time_t unix, char* buffer, size_t bufferSize);

    bool isUnixTimeValid(uint64_t unixTime, bool isMilliseconds = true);
}