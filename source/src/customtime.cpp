#include "customtime.h"

static const char *TAG = "customtime";

namespace CustomTime {
    // Static variables to maintain state
    static bool _isTimeSynched = false;
    static int _gmtOffsetSeconds = DEFAULT_GMT_OFFSET_SECONDS;
    static int _dstOffsetSeconds = DEFAULT_DST_OFFSET_SECONDS;
    static unsigned long long _lastSyncAttempt = 0;

    // Private helper function
    static bool _getPublicLocation(PublicLocation* publicLocation);
    static bool _getPublicTimezone(int* gmtOffsetSeconds, int* dstOffsetSeconds);

    static bool _getTime();
    static void _checkAndSyncTime();

    static bool _loadConfiguration();
    static void _saveConfiguration();

    static bool _isUnixTimeValid(unsigned long long unixTime, bool isMilliseconds = true);

    bool begin() {
        _loadConfiguration();

        // Initial sync attempt
        _lastSyncAttempt = millis64();
        _isTimeSynched = _getTime();

        if (_isTimeSynched)
        {
            int gmtOffset, dstOffset;
            if (_getPublicTimezone(&gmtOffset, &dstOffset)) { CustomTime::setOffset(gmtOffset, dstOffset); }
            else { logger.warning("Time synced, but the timezone could not be fetched. It will be shown as UTC", TAG); }
        }

        return _isTimeSynched;
    }

    void resetToDefaults() {
        _gmtOffsetSeconds = DEFAULT_GMT_OFFSET_SECONDS;
        _dstOffsetSeconds = DEFAULT_DST_OFFSET_SECONDS;
        _saveConfiguration();
    }

    void setOffset(int gmtOffset, int dstOffset) {
        _gmtOffsetSeconds = gmtOffset;
        _dstOffsetSeconds = dstOffset;

        configTime(_gmtOffsetSeconds, _dstOffsetSeconds, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        
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
            _gmtOffsetSeconds = DEFAULT_GMT_OFFSET_SECONDS;
            _dstOffsetSeconds = DEFAULT_DST_OFFSET_SECONDS;
            _saveConfiguration();
            setOffset(_gmtOffsetSeconds, _dstOffsetSeconds);
            return false;
        }

        _gmtOffsetSeconds = preferences.getInt(PREFERENCES_GMT_OFFSET_KEY, DEFAULT_GMT_OFFSET_SECONDS);
        _dstOffsetSeconds = preferences.getInt(PREFERENCES_DST_OFFSET_KEY, DEFAULT_DST_OFFSET_SECONDS);
        preferences.end();
        
        setOffset(_gmtOffsetSeconds, _dstOffsetSeconds);
        return true;
    }

    void _saveConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_TIME, false)) {
            return;
        }
        preferences.putInt(PREFERENCES_GMT_OFFSET_KEY, _gmtOffsetSeconds);
        preferences.putInt(PREFERENCES_DST_OFFSET_KEY, _dstOffsetSeconds);
        preferences.end();
    }

    bool _getTime() {
        time_t _now;
        struct tm _timeinfo;
        if(!getLocalTime(&_timeinfo)){
            return false;
        }
        time(&_now);
        return _isUnixTimeValid(_now, false);
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

    static void _checkAndSyncTime() {
        unsigned long long currentTime = millis64();
        
        // Check if it's time to sync (every TIME_SYNC_INTERVAL_SECONDS seconds)
        if (currentTime - _lastSyncAttempt >= (unsigned long long)TIME_SYNC_INTERVAL_SECONDS * 1000) {
            _lastSyncAttempt = currentTime;
            
            // Re-configure time to trigger a new sync
            configTime(_gmtOffsetSeconds, _dstOffsetSeconds, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
            
            // Check if sync was successful
            _isTimeSynched = _getTime();
        }
    }

    static bool _getPublicLocation(PublicLocation* publicLocation) {
        if (!publicLocation) {
            logger.error("Null pointer passed to getPublicLocation", TAG);
            return false;
        }

        HTTPClient _http;
        JsonDocument jsonDocument;

        _http.begin(PUBLIC_LOCATION_ENDPOINT);

        int httpCode = _http.GET();
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                // Use stream directly - efficient and simple
                DeserializationError error = deserializeJson(jsonDocument, _http.getStream());
                
                if (error) {
                    logger.error("JSON parsing failed: %s", TAG, error.c_str());
                    _http.end();
                    return false;
                }
                
                // Validate API response
                if (jsonDocument["status"] != "success") {
                    logger.error("API returned error status: %s", TAG, 
                            jsonDocument["status"].as<const char*>());
                    _http.end();
                    return false;
                }

                // Extract strings safely using const char* and copy to char arrays
                float latitude = jsonDocument["lat"].as<float>();
                float longitude = jsonDocument["lon"].as<float>();

                // Extract strings safely - use empty string if NULL
                publicLocation->latitude = latitude;
                publicLocation->longitude = longitude;

                logger.debug(
                    "Location: Lat: %f | Lon: %f",
                    TAG,
                    publicLocation->latitude,
                    publicLocation->longitude
                );
            } else {
                logger.warning("HTTP request failed with code: %d", TAG, httpCode);
                return false;
            }
        } else {
            logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
            return false;
        }

        _http.end();
        return true;
    }

    static bool _getPublicTimezone(int* gmtOffsetSeconds, int* dstOffsetSeconds) {
        if (!gmtOffsetSeconds || !dstOffsetSeconds) {
            logger.error("Null pointer passed to getPublicTimezone", TAG);
            return false;
        }

        PublicLocation _publicLocation;
        if (!_getPublicLocation(&_publicLocation)) {
            logger.warning("Could not retrieve the public location. Skipping timezone fetching", TAG);
            return false;
        }

        HTTPClient _http;
        JsonDocument jsonDocument;

        char url[URL_BUFFER_SIZE];
        // The URL for the timezone API endpoint requires passing as params the latitude, longitude and username (which is a sort of free "public" api key)
        snprintf(url, sizeof(url), "%slat=%f&lng=%f&username=%s",
            PUBLIC_TIMEZONE_ENDPOINT,
            _publicLocation.latitude,
            _publicLocation.longitude,
            PUBLIC_TIMEZONE_USERNAME);

        _http.begin(url);
        int httpCode = _http.GET();

        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                // Example JSON response:
                // {"sunrise":"2025-07-27 05:55","lng":14.168099,"countryCode":"IT","gmtOffset":1,"rawOffset":1,"sunset":"2025-07-27 20:24","timezoneId":"Europe/Rome","dstOffset":2,"countryName":"Italy","time":"2025-07-27 20:53","lat":40.813198}
                DeserializationError error = deserializeJson(jsonDocument, _http.getString()); // Unfortunately, the stream method returns null so we have to use string
                
                if (error) {
                    logger.error("JSON parsing failed: %s", TAG, error.c_str());
                    _http.end();
                    return false;
                }

                *gmtOffsetSeconds = jsonDocument["rawOffset"].as<int>() * 3600; // Convert hours to seconds
                *dstOffsetSeconds = jsonDocument["dstOffset"].as<int>() * 3600 - *gmtOffsetSeconds; // Convert hours to seconds. Remove GMT offset as it is already included in the dst offset

                logger.debug(
                    "GMT offset: %d | DST offset: %d",
                    TAG,
                    jsonDocument["rawOffset"].as<int>(),
                    jsonDocument["dstOffset"].as<int>()
                );
            } else {
                logger.warning("HTTP request failed with code: %d", TAG, httpCode);
                return false;
            }
        } else {
            logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
            return false;
        }

        _http.end();
        return true;
    }

    static bool _isUnixTimeValid(unsigned long long unixTime, bool isMilliseconds) {
        if (isMilliseconds) { return (unixTime >= MINIMUM_UNIX_TIME_MILLISECONDS && unixTime <= MAXIMUM_UNIX_TIME_MILLISECONDS); }
        else { return (unixTime >= MINIMUM_UNIX_TIME_SECONDS && unixTime <= MAXIMUM_UNIX_TIME_SECONDS); }
    }
};