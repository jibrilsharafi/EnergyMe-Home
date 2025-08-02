#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Preferences.h>

#include "constants.h"
#include "utils.h"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.apple.com"

#define TIME_SYNC_INTERVAL_SECONDS (60 * 60)

#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_ISO_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ" // ISO 8601 format with milliseconds
#define DATE_FORMAT "%Y-%m-%d"
#define DATE_ISO_FORMAT "%04d-%02d-%02d"

#define PREFERENCES_GMT_OFFSET_KEY "gmt_offset"
#define PREFERENCES_DST_OFFSET_KEY "dst_offset"

#define DEFAULT_GMT_OFFSET_SECONDS 0
#define DEFAULT_DST_OFFSET_SECONDS 0

#define PUBLIC_LOCATION_ENDPOINT "http://ip-api.com/json/"
#define PUBLIC_TIMEZONE_ENDPOINT "http://api.geonames.org/timezoneJSON?"
#define PUBLIC_TIMEZONE_USERNAME "energymehome"

// Time utilities
#define MINIMUM_UNIX_TIME_SECONDS 1000000000 // Corresponds to 2001
#define MINIMUM_UNIX_TIME_MILLISECONDS 1000000000000 // Corresponds to 2001
#define MAXIMUM_UNIX_TIME_SECONDS 4102444800 // Corresponds to 2100
#define MAXIMUM_UNIX_TIME_MILLISECONDS 4102444800000 // Corresponds to 2100

struct PublicLocation 
{
  float latitude;
  float longitude;

  PublicLocation() : latitude(45.0), longitude(9.0) {} // Default to Milan coordinates
};

namespace CustomTime {
    bool begin();
    // No need to stop anything here since once it executes at the beginning, there is no other use for this

    void resetToDefaults();
    // This function is called frequently from other functions, ensuring that we check and sync time if needed
    bool isTimeSynched();
    void setOffset(int32_t gmtOffset, int32_t dstOffset);

    uint64_t getUnixTime();
    uint64_t getUnixTimeMilliseconds();
    void getTimestamp(char* buffer, size_t bufferSize);
    void getTimestampIso(char* buffer, size_t bufferSize);
    void getDate(char* buffer, size_t bufferSize);
    void getDateIso(char* buffer, size_t bufferSize);

    uint64_t getMillisecondsUntilNextHour();

    void timestampFromUnix(time_t unix, char* buffer, size_t bufferSize);
    void timestampIsoFromUnix(time_t unix, char* buffer, size_t bufferSize);
}