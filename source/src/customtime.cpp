#include "customtime.h"

CustomTime::CustomTime(
    const char* ntpServer,
    int timeSyncInterval) {
        _ntpServer = ntpServer;
        _timeSyncInterval = timeSyncInterval;
}

bool CustomTime::begin(){
    configTime(
        generalConfiguration.gmtOffset, 
        generalConfiguration.dstOffset, 
        _ntpServer
    );
    setSyncInterval(_timeSyncInterval);
    if (_getTime()){
        logger.info("Time synchronized: %s", "customtime::begin", getTimestamp().c_str());
        return true;
    } else {
        logger.error("Failed to synchronize time", "customtime::begin");
        return false;
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
    strftime(_timestamp, sizeof(_timestamp), LOG_TIMESTAMP_FORMAT, _timeinfo);
    return String(_timestamp);
}

long CustomTime::getUnixTime(){
    return static_cast<long>(time(nullptr));
}

String CustomTime::getTimestamp(){
    return timestampFromUnix(getUnixTime());
}