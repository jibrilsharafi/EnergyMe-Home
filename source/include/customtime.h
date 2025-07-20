#pragma once

#include <Arduino.h>
#include <TimeLib.h>
#include <AdvancedLogger.h>

// Time
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.apple.com"
#define TIME_SYNC_INTERVAL_S (60 * 60) // Sync time every hour (in seconds)
#define TIME_SYNC_RETRY_INTERVAL_MS (60 * 1000) // Retry sync if failed
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_BUFFER_SIZE 20  // Size needed for TIMESTAMP_FORMAT (19 chars + null terminator)

namespace CustomTime {
    bool begin();

    bool isTimeSynched();

    void setOffset(int gmtOffset, int dstOffset);

    void timestampFromUnix(time_t unix, char* buffer);
    void timestampFromUnix(time_t unix, const char *timestampFormat, char* buffer);

    unsigned long getUnixTime();
    unsigned long long getUnixTimeMilliseconds();
    void getTimestamp(char* buffer);
}