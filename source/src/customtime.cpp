#include "customtime.h"

// TAG
static const char *TAG = "customtime";

CustomTime::CustomTime(const char *ntpServer, int timeSyncInterval, const char *timestampFormat, GeneralConfiguration &generalConfiguration, AdvancedLogger &logger)
    : _ntpServer(ntpServer), _timeSyncInterval(timeSyncInterval), _timestampFormat(timestampFormat), _generalConfiguration(generalConfiguration), _logger(logger) {
}

bool CustomTime::begin() {
    configTime(
        _generalConfiguration.gmtOffset, 
        _generalConfiguration.dstOffset, 
        _ntpServer
    );

    setSyncInterval(_timeSyncInterval);

    if (_getTime()) {
        _isTimeSynched = true;
        _logger.info("Time synchronized: %s", TAG, getTimestamp().c_str());
        return true;
    } else {
        _isTimeSynched = false;
        _logger.error("Failed to synchronize time. Retrying in %d seconds", TAG, TIME_SYNC_RETRY_INTERVAL / 1000);
        _lastTimeSyncAttempt = millis();
        return false;
    }
}

void CustomTime::loop() {
    if (!_isTimeSynched && (millis() - _lastTimeSyncAttempt > TIME_SYNC_RETRY_INTERVAL)) {
      _logger.info("Attempting to sync time in loop...", TAG);
      
      _isTimeSynched = begin();
      if (_isTimeSynched) {
          _logger.info("Time sync successful in loop.", TAG);
      } else {
          _logger.warning("Time sync retry failed. Retrying in %d seconds", TAG, TIME_SYNC_RETRY_INTERVAL / 1000);
      }
      _lastTimeSyncAttempt = millis();
    }
}

bool CustomTime::_getTime() {
    time_t _now;
    struct tm _timeinfo;
    if(!getLocalTime(&_timeinfo)){
        return false;
    }
    time(&_now);
    return true;
}

String CustomTime::timestampFromUnix(long unix){
    struct tm* _timeinfo;
    char _timestamp[26];

    _timeinfo = localtime(&unix);
    strftime(_timestamp, sizeof(_timestamp), _timestampFormat, _timeinfo);
    return String(_timestamp);
}

// Static method for other classes to use
String CustomTime::timestampFromUnix(long unix, const char *timestampFormat){
    struct tm* _timeinfo;
    char _timestamp[26];

    _timeinfo = localtime(&unix);
    strftime(_timestamp, sizeof(_timestamp), timestampFormat, _timeinfo);
    return String(_timestamp);
}

unsigned long CustomTime::getUnixTime(){
    return static_cast<unsigned long>(time(nullptr));
}

unsigned long long CustomTime::getUnixTimeMilliseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
}

String CustomTime::getTimestamp(){
    return timestampFromUnix(getUnixTime());
}