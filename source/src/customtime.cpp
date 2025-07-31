#include "customtime.h"

static const char *TAG = "customtime";

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static int _gmtOffset = DEFAULT_GMT_OFFSET;
    static int _dstOffset = DEFAULT_DST_OFFSET;
    static unsigned long long _lastSyncAttempt = 0;

    // Private helper function
    static bool _getTime();
    static void _checkAndSyncTime();

    static bool _loadConfiguration();
    static void _saveConfiguration();

    bool begin() {
        _loadConfiguration();
        
        // Initial sync attempt
        _lastSyncAttempt = millis64();
        _isTimeSynched = _getTime();
        return _isTimeSynched;
    }

    void resetToDefaults() {
        _gmtOffset = DEFAULT_GMT_OFFSET;
        _dstOffset = DEFAULT_DST_OFFSET;
        _saveConfiguration();
    }

    void setOffset(int gmtOffset, int dstOffset) {
        _gmtOffset = gmtOffset;
        _dstOffset = dstOffset;

        configTime(_gmtOffset, _dstOffset, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        
        _saveConfiguration();
        
        _isTimeSynched = _getTime();
    }

    bool isTimeSynched() {
        _checkAndSyncTime();
        return _isTimeSynched;
    }

    bool _loadConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, true)) {
            // If preferences can't be opened, fallback to default
            _gmtOffset = DEFAULT_GMT_OFFSET;
            _dstOffset = DEFAULT_DST_OFFSET;
            _saveConfiguration();
            setOffset(_gmtOffset, _dstOffset);
            return false;
        }

        _gmtOffset = preferences.getInt(PREFERENCES_GMT_OFFSET_KEY, DEFAULT_GMT_OFFSET);
        _dstOffset = preferences.getInt(PREFERENCES_DST_OFFSET_KEY, DEFAULT_DST_OFFSET);
        preferences.end();
        
        setOffset(_gmtOffset, _dstOffset);
        return true;
    }

    void _saveConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, false)) {
            return;
        }
        preferences.putInt(PREFERENCES_GMT_OFFSET_KEY, _gmtOffset);
        preferences.putInt(PREFERENCES_DST_OFFSET_KEY, _dstOffset);
        preferences.end();
    }

    bool _getTime() {
        time_t _now;
        struct tm _timeinfo;
        if(!getLocalTime(&_timeinfo)){
            return false;
        }
        time(&_now);
        return true;
    }

    unsigned long long getUnixTime() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return static_cast<unsigned long long>(tv.tv_sec);
    }

    unsigned long long getUnixTimeMilliseconds() {
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

    void timestampFromUnix(time_t unixSeconds, char* buffer, size_t bufferSize) {
        struct tm timeinfo;
        localtime_r(&unixSeconds, &timeinfo);
        strftime(buffer, bufferSize, TIMESTAMP_FORMAT, &timeinfo);
    }

    void getTimestampIso(char* buffer, size_t bufferSize) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        struct tm utc_tm;
        gmtime_r(&tv.tv_sec, &utc_tm);
        int milliseconds = tv.tv_usec / 1000;
        
        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                milliseconds);
    }

    void timestampIsoFromUnix(time_t unixSeconds, char* buffer, size_t bufferSize) {
        struct tm utc_tm;
        gmtime_r(&unixSeconds, &utc_tm);
        int milliseconds = 0; // No milliseconds in time_t
        
        snprintf(buffer, bufferSize, TIMESTAMP_ISO_FORMAT,
                utc_tm.tm_year + 1900,
                utc_tm.tm_mon + 1,
                utc_tm.tm_mday,
                utc_tm.tm_hour,
                utc_tm.tm_min,
                utc_tm.tm_sec,
                milliseconds);
    }

    void _checkAndSyncTime() {
        unsigned long long currentTime = millis64();
        
        // Check if it's time to sync (every TIME_SYNC_INTERVAL_S seconds)
        if (currentTime - _lastSyncAttempt >= (unsigned long long)TIME_SYNC_INTERVAL_S * 1000) {
            _lastSyncAttempt = currentTime;
            
            // Re-configure time to trigger a new sync
            configTime(_gmtOffset, _dstOffset, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            
            // Check if sync was successful
            _isTimeSynched = _getTime();
        }
    }
}