// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "customtime.h"
#include "customnet.h"
#include "duration_format.h"
#include "unix_time.h"

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static uint64_t _lastSyncAttempt = 0;

    // Backing storage for the gateway server string passed into configTime(); must
    // outlive the call (static, not stack-local) since the SNTP client only stores
    // the pointer it is given, not a copy.
    static char _gatewayNtpServer[IP_ADDRESS_BUFFER_SIZE];

    static bool _getTime();
    static void _checkAndSyncTime();
    static void _configureNtpServers();

    bool begin() {
        // Initial sync attempt
        _configureNtpServers();

        _lastSyncAttempt = millis64();
        _isTimeSynched = _getTime();

        return _isTimeSynched;
    }

    bool isTimeSynched() {
        _checkAndSyncTime();
        return _isTimeSynched;
    }

    bool isNowCloseToHour(uint64_t toleranceMillis) {
        uint64_t millisFromHour = UnixTime::millisFromNearestUtcHour(getUnixTimeMilliseconds());
        char toleranceHuman[DURATION_FORMAT_BUFFER_SIZE], fromHourHuman[DURATION_FORMAT_BUFFER_SIZE];
        DurationFormat::humanizeDuration(toleranceMillis, toleranceHuman, sizeof(toleranceHuman));
        DurationFormat::humanizeDuration(millisFromHour, fromHourHuman, sizeof(fromHourHuman));
        if (millisFromHour <= toleranceMillis) {
            LOG_DEBUG("Current time is %s from the nearest UTC hour (within %s)", fromHourHuman, toleranceHuman);
            return true;
        }
        LOG_DEBUG("Current time is not close to any UTC hour (%s away)", fromHourHuman);
        return false;
    }

    // True when the nearest UTC hour (the one the hourly save is stamped with) is 00
    bool isNowHourZero() {
        return UnixTime::nearestUtcHourSeconds(getUnixTime()) % 86400ULL == 0;
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
        time_t rounded = (time_t)UnixTime::nearestUtcHourSeconds(getUnixTime());
        struct tm utc_tm;
        gmtime_r(&rounded, &utc_tm);

        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                uint32_t(0)); // No milliseconds in rounded timestamp. Cast needed to match format specifier
    }

    void getDateIsoOfNearestHour(char* buffer, size_t bufferSize, int offsetDays) {
        time_t rounded = (time_t)UnixTime::nearestUtcHourSeconds(getUnixTime()) + (time_t)offsetDays * 86400;
        struct tm utc_tm;
        gmtime_r(&rounded, &utc_tm);
        snprintf(buffer, bufferSize, DATE_ISO_FORMAT, utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);
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
        return UnixTime::millisUntilNextUtcHour(getUnixTimeMilliseconds());
    }

    bool isUnixTimeValid(uint64_t unixTime, bool isMilliseconds) {
        return UnixTime::isValid(unixTime, isMilliseconds);
    }

    bool setUnixTime(uint64_t unixSeconds) {
        if (!isUnixTimeValid(unixSeconds, false)) {
            LOG_WARNING("Invalid Unix time provided: %llu", unixSeconds);
            return false;
        }
        
        struct timeval tv;
        tv.tv_sec = (time_t)unixSeconds;
        tv.tv_usec = 0;
        
        if (settimeofday(&tv, NULL) != 0) {
            LOG_ERROR("Failed to set system time");
            return false;
        }
        
        _isTimeSynched = true;
        LOG_INFO("Time manually synchronized: %llu", unixSeconds);
        return true;
    }

    // Gateway first (many routers answer NTP on their own LAN IP), then the two
    // compiled-in public fallbacks. Reads the gateway fresh every call so a DHCP
    // renewal, static IP change, interface failover (ETH<->STA on Pro), or
    // reconnect to a different network is picked up with no stale state.
    static void _configureNtpServers() {
        IPAddress gateway = (CustomEth::activeInterface() == InterfaceArbitration::Interface::ETHERNET)
                                ? ETH.gatewayIP()
                                : WiFi.gatewayIP();
        snprintf(_gatewayNtpServer, sizeof(_gatewayNtpServer), "%s", gateway.toString().c_str());
        configTime(0, 0, _gatewayNtpServer, NTP_SERVER_1, NTP_SERVER_2);
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

    // A flag rather than zeroing _lastSyncAttempt: that only reads as "due" once the uptime
    // itself exceeds the sync interval, so a failover in the first hour kept the old gateway
    // as the NTP server. Set from the eth task, consumed here: a single-byte store.
    static volatile bool _resyncRequested = false;

    void requestResync() {
        _resyncRequested = true;
    }

    static void _checkAndSyncTime() {
        uint64_t currentTime = millis64();

        // Either enough time has passed since last successful sync, or we failed previously and we retry earlier
        bool isTimeToSync = (currentTime - _lastSyncAttempt >= (uint64_t)TIME_SYNC_INTERVAL);
        bool needToRetry = !_isTimeSynched && (currentTime - _lastSyncAttempt >= (uint64_t)TIME_SYNC_RETRY_IF_NOT_SYNCHED);

        if (isTimeToSync || needToRetry || _resyncRequested) {
            if (!CustomNet::isFullyConnected(true)) {
                LOG_DEBUG("Skipping time sync - no network connectivity");
                return;
            }
            _resyncRequested = false;
            _lastSyncAttempt = currentTime;

            // Re-configure time to trigger a new sync
            _configureNtpServers();

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