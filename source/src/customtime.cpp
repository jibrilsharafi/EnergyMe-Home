#include "customtime.h"

// TAG
static const char *TAG = "customtime";

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    
    // Private helper function
    static bool _getTime();

    bool begin() {
        setSyncInterval(TIME_SYNC_INTERVAL_S);
        
        // TimeLib will automatically retry sync at the interval we set
        // We just check if initial sync worked, but don't need manual retry logic
        _isTimeSynched = _getTime();
        return _isTimeSynched;
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