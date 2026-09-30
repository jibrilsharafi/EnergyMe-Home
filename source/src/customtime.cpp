// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include <atomic>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_timer.h"

#include "customtime.h"
#include "customnet.h"
#include "duration_format.h"
#include "time_floor.h"
#include "unix_time.h"

#if __has_include("git_rev.h")
#include "git_rev.h"
#endif
#ifndef GIT_COMMIT_UNIX_TIME
#define GIT_COMMIT_UNIX_TIME 0ULL
#endif

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static uint64_t _lastSyncAttempt = 0;

    // Backing storage for the gateway server string passed into configTime(); must
    // outlive the call (static, not stack-local) since the SNTP client only stores
    // the pointer it is given, not a copy.
    static char _gatewayNtpServer[IP_ADDRESS_BUFFER_SIZE];

    // Time floor state (see TimeFloor). Written from the sntp_sync_time override on the lwIP
    // tcpip thread, where NVS and the logger are off limits, so it hands its work over to task
    // context (_handleRejectedAnswer, persistPendingFloor) through these atomics. The floor
    // starts at the build floor so an answer landing before begin() is already guarded.
    static std::atomic<uint32_t> _floorSeconds{static_cast<uint32_t>(GIT_COMMIT_UNIX_TIME)};
    static std::atomic<uint32_t> _anchorSeconds{static_cast<uint32_t>(GIT_COMMIT_UNIX_TIME)}; // TimeFloor::ceiling
    static std::atomic<uint32_t> _pendingFloorPersist{0}; // 0: nothing to persist

    static std::atomic<uint32_t> _rejectedCandidate{0};
    static std::atomic<bool> _rejectionPending{false};
    static std::atomic<uint32_t> _lastRejectionWarningSeconds{0}; // uptime; 0: never warned

    static std::atomic<bool> _skipGatewayOnce{false};
    static std::atomic<bool> _gatewaySkipped{false}; // current SNTP server set has no gateway

    static uint32_t _uptimeSeconds() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
    }

    static bool _getTime();
    static void _checkAndSyncTime();
    static void _configureNtpServers();

    static void _persistFloor(uint32_t floorSeconds) {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, false)) {
            LOG_ERROR("Failed to open preferences namespace: %s", PREFERENCES_NAMESPACE_TIME);
            return;
        }
        if (preferences.putUInt(TIME_FLOOR_KEY, floorSeconds) == 0) {
            LOG_ERROR("Failed to persist the time floor %llu", (uint64_t)floorSeconds);
        }
        preferences.end();
    }

    static uint32_t _loadPersistedFloor() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, true)) return 0;
        uint32_t floorSeconds = preferences.getUInt(TIME_FLOOR_KEY, 0);
        preferences.end();
        return floorSeconds;
    }

    bool begin() {
        uint32_t persistedFloor = _loadPersistedFloor();
        uint32_t bootFloor = TimeFloor::effective(GIT_COMMIT_UNIX_TIME, persistedFloor);
        _floorSeconds.store(bootFloor);
        _anchorSeconds.store(bootFloor);
        LOG_DEBUG("Time floor %llu (build %llu, persisted %llu)",
                  (uint64_t)bootFloor, (uint64_t)GIT_COMMIT_UNIX_TIME, (uint64_t)persistedFloor);

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

        // Lower-only (TimeFloor::onManualSet). Clear a queued sync persist first so it cannot land
        // in NVS after this one. A persist already in flight still can, but it is capped at the
        // ceiling, so at worst it undoes a voluntary lowering of a correct floor.
        _pendingFloorPersist.store(0);
        TimeFloor::ManualOutcome outcome =
            TimeFloor::onManualSet(_floorSeconds.load(), _anchorSeconds.load(), _uptimeSeconds(), unixSeconds);
        _anchorSeconds.store(outcome.anchor);
        _floorSeconds.store(outcome.floor);
        _persistFloor(outcome.floor);

        struct timeval tv;
        tv.tv_sec = (time_t)unixSeconds;
        tv.tv_usec = 0;
        
        if (settimeofday(&tv, NULL) != 0) {
            LOG_ERROR("Failed to set system time");
            return false;
        }
        
        _isTimeSynched = true;
        LOG_INFO("Time manually synchronized: %llu (time floor %llu)", unixSeconds, (uint64_t)outcome.floor);
        return true;
    }

    // Gateway first (many routers answer NTP on their own LAN IP), then the two
    // compiled-in public fallbacks. Reads the gateway fresh every call so a DHCP
    // renewal, static IP change, interface failover (ETH<->STA on Pro), or
    // reconnect to a different network is picked up with no stale state.
    static void _configureNtpServers() {
        if (_skipGatewayOnce.exchange(false)) {
            // lwIP does not move on to another server after a plausible-but-wrong answer, and its
            // current server index survives sntp_stop()/sntp_init(): the next request goes to
            // the same slot. So every slot gets a server other than the one it held in the
            // regular set, and no slot is left NULL (lwIP would reuse the slot's stale address).
            configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_1);
            _gatewaySkipped.store(true);
            return;
        }

        IPAddress gateway = (CustomEth::activeInterface() == InterfaceArbitration::Interface::ETHERNET)
                                ? ETH.gatewayIP()
                                : WiFi.gatewayIP();
        snprintf(_gatewayNtpServer, sizeof(_gatewayNtpServer), "%s", gateway.toString().c_str());
        configTime(0, 0, _gatewayNtpServer, NTP_SERVER_1, NTP_SERVER_2);
        _gatewaySkipped.store(false);
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

    void persistPendingFloor() {
        uint32_t pendingFloor = _pendingFloorPersist.exchange(0);
        if (pendingFloor != 0) _persistFloor(pendingFloor);
    }

    // Task-context half of a rejection in the sntp_sync_time override. Consume-and-clear with
    // exchange(): isTimeSynched() runs from several tasks and each rejection must be handled once.
    static void _handleRejectedAnswer() {
        if (!_rejectionPending.exchange(false)) return;

        uint64_t candidate = _rejectedCandidate.load();
        uint64_t floorSeconds = _floorSeconds.load();

        // Throttled: with a floor that is itself wrong (future-dated) every answer is rejected,
        // and an unsynced device retries every minute - that must not flood the saved log.
        uint32_t nowSeconds = _uptimeSeconds();
        uint32_t lastWarning = _lastRejectionWarningSeconds.load();
        if (lastWarning == 0 || nowSeconds - lastWarning >= TIME_SYNC_INTERVAL / 1000) {
            _lastRejectionWarningSeconds.store(nowSeconds);
            if (TimeFloor::isZeroTransmitArtifact(candidate)) {
                LOG_WARNING("Rejected NTP time %llu: zero transmit timestamp (unsynchronized server). Keeping the current clock",
                            candidate);
            } else if (!isUnixTimeValid(candidate, false)) {
                LOG_WARNING("Rejected NTP time %llu: outside the valid range. Keeping the current clock", candidate);
            } else {
                LOG_WARNING("Rejected NTP time %llu: more than %d s before the time floor %llu. Keeping the current clock",
                            candidate, TIME_FLOOR_TOLERANCE_SECONDS, floorSeconds);
            }
        } else {
            LOG_DEBUG("Rejected NTP time %llu (time floor %llu)", candidate, floorSeconds);
        }

        // lwIP counts the rejected answer as a success and would not ask again for hours. Retry
        // right away without the gateway, once: if the gateway-free set was already the one
        // rejected, the regular schedule takes over instead of hammering the public servers.
        if (!_gatewaySkipped.load()) {
            _skipGatewayOnce.store(true);
            _resyncRequested = true;
        }
    }

    static bool _isClockValid() {
        time_t now;
        time(&now);
        return isUnixTimeValid((uint64_t)now, false);
    }

    // Never waits for the answer: this runs on caller tasks, including async_tcp, which is on
    // the 5 s task watchdog, and getLocalTime()'s 5 s poll could trip it on an unsynced device.
    // The answer lands asynchronously (sntp_sync_time) and a later call picks it up here.
    static void _checkAndSyncTime() {
        _handleRejectedAnswer();

        if (!_isTimeSynched && _isClockValid()) {
            _isTimeSynched = true;
            LOG_INFO("Time synchronized (system clock is valid)"); // An NTP answer, or the clock kept across a soft reset
        }

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
            if (!_isTimeSynched) LOG_DEBUG("Time sync requested, waiting for an NTP answer");
        }
    }
};

// Replaces the weak default in ESP-IDF components/lwip/apps/sntp/sntp.c (esp_sntp.h documents
// the override). lwIP steps the clock in here, on its own schedule as well as after configTime(),
// so checking the time afterwards in _getTime() is too late: the clock has already jumped.
// Runs on the lwIP tcpip thread (4 KB stack, core lock held): atomics only, no NVS, no logger.
extern "C" void sntp_sync_time(struct timeval *tv) {
    uint64_t candidate = (uint64_t)tv->tv_sec;
    uint32_t floorSeconds = CustomTime::_floorSeconds.load();
    TimeFloor::AnswerOutcome outcome =
        TimeFloor::onAnswer(floorSeconds, CustomTime::_anchorSeconds.load(), CustomTime::_uptimeSeconds(), candidate,
                            TIME_FLOOR_TOLERANCE_SECONDS);
    if (!outcome.accept) {
        // Clock and sync status untouched: a device that was synced keeps its running clock
        CustomTime::_rejectedCandidate.store(static_cast<uint32_t>(candidate));
        CustomTime::_rejectionPending.store(true);
        return;
    }

    // The default body (IDF release/v5.5), minus its debug logs. Its time_sync_notification_cb
    // is static in sntp.c and unreachable from here; nothing in this firmware registers one.
    sntp_sync_mode_t mode = sntp_get_sync_mode();
    if (mode == SNTP_SYNC_MODE_IMMED) {
        settimeofday(tv, NULL);
        sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
    } else if (mode == SNTP_SYNC_MODE_SMOOTH) {
        struct timeval now;
        gettimeofday(&now, NULL);
        int64_t cpuTime = (int64_t)now.tv_sec * 1000000LL + (int64_t)now.tv_usec;
        int64_t sntpTime = (int64_t)tv->tv_sec * 1000000LL + (int64_t)tv->tv_usec;
        int64_t delta = sntpTime - cpuTime;
        struct timeval tvDelta;
        tvDelta.tv_sec = (time_t)(delta / 1000000LL);
        tvDelta.tv_usec = (suseconds_t)(delta % 1000000LL);
        if (adjtime(&tvDelta, NULL) == -1) {
            settimeofday(tv, NULL);
            sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
        } else {
            sntp_set_sync_status(SNTP_SYNC_STATUS_IN_PROGRESS);
        }
    }

    if (outcome.floor != floorSeconds) {
        CustomTime::_floorSeconds.store(outcome.floor);
        CustomTime::_pendingFloorPersist.store(outcome.floor);
    }
}