#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <Preferences.h>
#include <TimeLib.h>

#include "constants.h"

#define PREFERENCES_GMT_OFFSET_KEY "gmt_offset"
#define PREFERENCES_DST_OFFSET_KEY "dst_offset"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.apple.com"
#define TIME_SYNC_INTERVAL_S (60 * 60) // Sync time every hour (in seconds)
#define TIME_SYNC_RETRY_INTERVAL_MS (60 * 1000) // Retry sync if failed
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_BUFFER_SIZE 20  // Size needed for TIMESTAMP_FORMAT (19 chars + null terminator)
#define DEFAULT_GMT_OFFSET 0
#define DEFAULT_DST_OFFSET 0

namespace CustomTime {
    bool begin();
    void resetToDefaults();

    bool isTimeSynched();

    void setOffset(int gmtOffset, int dstOffset);

    void timestampFromUnix(time_t unix, char* buffer, size_t bufferSize);

    unsigned long getUnixTime();
    unsigned long long getUnixTimeMilliseconds();
    void getTimestamp(char* buffer, size_t bufferSize);
}