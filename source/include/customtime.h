#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Preferences.h>

#include "constants.h"
#include "utils.h"

#define PREFERENCES_GMT_OFFSET_KEY "gmt_offset"
#define PREFERENCES_DST_OFFSET_KEY "dst_offset"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.apple.com"
#define TIME_SYNC_INTERVAL_S (60 * 60) // Sync time every hour (in seconds)
#define TIME_SYNC_RETRY_INTERVAL_MS (60 * 1000) // Retry sync if failed
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_ISO_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ" // ISO 8601 format with milliseconds
#define DEFAULT_GMT_OFFSET 0
#define DEFAULT_DST_OFFSET 0
    
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
    bool isTimeSynched();
    void setOffset(int gmtOffset, int dstOffset);

    unsigned long long getUnixTime();
    unsigned long long getUnixTimeMilliseconds();
    void getTimestamp(char* buffer, size_t bufferSize);
    void getTimestampIso(char* buffer, size_t bufferSize);
    void timestampFromUnix(time_t unix, char* buffer, size_t bufferSize);
    void timestampIsoFromUnix(time_t unix, char* buffer, size_t bufferSize);
}