// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "customtime.h"

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static uint64_t _lastSyncAttempt = 0;

    static bool _getTime();
    static void _checkAndSyncTime();

    bool begin() {
        // Initial sync attempt
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        _lastSyncAttempt = millis64();
        _isTimeSynched = _getTime();

        return _isTimeSynched;
    }

    bool isTimeSynched() {
        _checkAndSyncTime();
        return _isTimeSynched;
    }

    bool isNowCloseToHour(uint64_t toleranceMillis) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);
        
        // Calculate milliseconds since the current hour started (minutes and seconds)
        uint64_t millisSinceCurrentHour = ((uint64_t)(timeinfo.tm_min) * 60ULL + (uint64_t)(timeinfo.tm_sec)) * 1000ULL;
        
        // Calculate milliseconds until the next hour
        uint64_t millisUntilNextHour = 3600000ULL - millisSinceCurrentHour;

        // Check if we're close to either the current hour (just passed) or the next hour (approaching)
        if (millisSinceCurrentHour <= toleranceMillis) {
            LOG_DEBUG("Current time is close to the current hour (within %llu ms since hour start)", toleranceMillis);
            return true;
        } else if (millisUntilNextHour <= toleranceMillis) {
            LOG_DEBUG("Current time is close to the next hour (within %llu ms)", toleranceMillis);
            return true;
        } else {
            LOG_DEBUG("Current time is not close to any hour (since hour: %llu ms, until next: %llu ms)", millisSinceCurrentHour, millisUntilNextHour);
            return false;
        }
    }

    // returns true when current UTC hour is 0
    bool isNowHourZero() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm utc_tm;
        gmtime_r(&tv.tv_sec, &utc_tm);
        return (utc_tm.tm_hour == 0);
    }

    uint64_t getUnixTime() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (uint64_t)(tv.tv_sec);
    }

    uint64_t getUnixTimeMilliseconds() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
    }

    void getTimestamp(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);
        strftime(buffer, bufferSize, TIMESTAMP_FORMAT, &timeinfo);
    }

    void getTimestampIso(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm utc_tm;
        gmtime_r(&tv.tv_sec, &utc_tm);
        int32_t milliseconds = tv.tv_usec / 1000;
        
        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                milliseconds);
    }

    void getTimestampIsoRoundedToHour(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm utc_tm;
        gmtime_r(&tv.tv_sec, &utc_tm);
        
        // Round to the nearest hour
        int32_t seconds = (utc_tm.tm_min * 60 + utc_tm.tm_sec);
        if (seconds >= 1800) {
            utc_tm.tm_hour += 1; // Round up
        }
        utc_tm.tm_min = 0;
        utc_tm.tm_sec = 0;

        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                uint32_t(0)); // No milliseconds in rounded timestamp. Cast needed to match format specifier
    }

    void timestampFromUnix(time_t unixSeconds, char* buffer, size_t bufferSize) {
        struct tm timeinfo;
        localtime_r(&unixSeconds, &timeinfo);
        strftime(buffer, bufferSize, TIMESTAMP_FORMAT, &timeinfo);
    }

    void timestampIsoFromUnix(time_t unixSeconds, char* buffer, size_t bufferSize) {
        struct tm utc_tm;
        gmtime_r(&unixSeconds, &utc_tm);
        int32_t milliseconds = 0; // No milliseconds in time_t
        
        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                milliseconds);
    }

    void getDate(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);
        strftime(buffer, bufferSize, DATE_FORMAT, &timeinfo);
    }

    void getCurrentDateIso(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm utc_tm;
        gmtime_r(&tv.tv_sec, &utc_tm);
        
        snprintf(buffer, bufferSize, DATE_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday
            );
    }

    void getDateIsoOffset(char *outBuf, size_t outBufLen, int offsetDays) {
        time_t now;
        time(&now);
        now += (time_t)offsetDays * 86400; // offset in seconds

        struct tm tm;
        gmtime_r(&now, &tm); // UTC time

        snprintf(
            outBuf, outBufLen, DATE_ISO_FORMAT, 
            tm.tm_year + 1900, 
            tm.tm_mon + 1, 
            tm.tm_mday
        );
    }

    uint64_t getMillisecondsUntilNextHour() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm timeinfo;
        localtime_r(&tv.tv_sec, &timeinfo);
        
        // Calculate the number of seconds until the next hour
        int32_t secondsUntilNextHour = 3600 - (timeinfo.tm_min * 60 + timeinfo.tm_sec);
        
        // Convert to milliseconds
        return (uint64_t)(secondsUntilNextHour) * 1000ULL;
    }

    bool isUnixTimeValid(uint64_t unixTime, bool isMilliseconds) {
        if (isMilliseconds) { return (unixTime >= MINIMUM_UNIX_TIME_MILLISECONDS && unixTime <= MAXIMUM_UNIX_TIME_MILLISECONDS); }
        else { return (unixTime >= MINIMUM_UNIX_TIME_SECONDS && unixTime <= MAXIMUM_UNIX_TIME_SECONDS); }
    }

    static bool _getTime() {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            LOG_DEBUG("Failed to get local time from NTP");
            return false;
        }
        
        time_t now;
        time(&now);
        
        if (!isUnixTimeValid(now, false)) {
            LOG_DEBUG("Retrieved time is outside valid range: %ld", now);
            return false;
        }
        
        LOG_DEBUG("Time sync successful: %ld", now);
        return true;
    }

    static void _checkAndSyncTime() {
        uint64_t currentTime = millis64();

        // Either enough time has passed since last successful sync, or we failed previously and we retry earlier
        bool isTimeToSync = (currentTime - _lastSyncAttempt >= (uint64_t)TIME_SYNC_INTERVAL);
        bool needToRetry = !_isTimeSynched && (currentTime - _lastSyncAttempt >= (uint64_t)TIME_SYNC_RETRY_IF_NOT_SYNCHED);

        if (isTimeToSync || needToRetry) {
            if (!CustomWifi::isFullyConnected()) {
                LOG_DEBUG("Skipping time sync - WiFi not connected");
                return;
            }
            _lastSyncAttempt = currentTime;
            
            // Re-configure time to trigger a new sync
            configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            
            // Check if sync was successful
            bool previousSyncState = _isTimeSynched;
            _isTimeSynched = _getTime();
            
            if (_isTimeSynched && !previousSyncState) {
                LOG_INFO("Time successfully synchronized with NTP");
            } else if (!_isTimeSynched && previousSyncState) {
                LOG_WARNING("Time synchronization lost");
            } else if (!_isTimeSynched) {
                LOG_DEBUG("Time synchronization attempt failed, will retry");
            }
        }
    }
};