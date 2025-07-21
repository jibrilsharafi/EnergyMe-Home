#include "customtime.h"

// TAG
static const char *TAG = "customtime";

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static int _gmtOffset = DEFAULT_GMT_OFFSET;
    static int _dstOffset = DEFAULT_DST_OFFSET;

    // Private helper function
    static bool _getTime();

    static bool _loadConfiguration();
    static void _saveConfiguration();

    bool begin() {
        setSyncInterval(TIME_SYNC_INTERVAL_S);

        _loadConfiguration();
        
        // TimeLib will automatically retry sync at the interval we set
        // We just check if initial sync worked, but don't need manual retry logic
        _isTimeSynched = _getTime();
        return _isTimeSynched;
    }

    void resetToDefaults() {
        _gmtOffset = DEFAULT_GMT_OFFSET;
        _dstOffset = DEFAULT_DST_OFFSET;
        _saveConfiguration();
    }

    void setOffset(int gmtOffset, int dstOffset) {
        configTime(gmtOffset, dstOffset, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        setSyncInterval(TIME_SYNC_INTERVAL_S);
        
        if (_getTime()) {
            _isTimeSynched = true;
            char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
            getTimestamp(timestampBuffer);
        } else {
            _isTimeSynched = false;
        }
    }

    bool isTimeSynched() {
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
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, false)) { // false = read-write mode
            return;
        }
        preferences.putInt(PREFERENCES_GMT_OFFSET_KEY, _gmtOffset);
        preferences.putInt(PREFERENCES_DST_OFFSET_KEY, _dstOffset);
        preferences.end();
    }

    static bool _getTime() {
        time_t _now;
        struct tm _timeinfo;
        if(!getLocalTime(&_timeinfo)){
            return false;
        }
        time(&_now);
        return true;
    }

    void timestampFromUnix(time_t unix, char* buffer) {
        struct tm* _timeinfo;

        _timeinfo = localtime(&unix);
        strftime(buffer, TIMESTAMP_BUFFER_SIZE, TIMESTAMP_FORMAT, _timeinfo);
    }

    void timestampFromUnix(time_t unix, const char *timestampFormat, char* buffer) {
        struct tm* _timeinfo;

        _timeinfo = localtime(&unix);
        strftime(buffer, TIMESTAMP_BUFFER_SIZE, timestampFormat, _timeinfo);
    }

    unsigned long getUnixTime() {
        return static_cast<unsigned long>(time(nullptr));
    }

    unsigned long long getUnixTimeMilliseconds() {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
    }

    void getTimestamp(char* buffer) {
        timestampFromUnix(getUnixTime(), buffer);
    }
}