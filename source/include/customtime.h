#pragma once

#include <Arduino.h>
#include <TimeLib.h>
#include <AdvancedLogger.h>

#include "constants.h"
#include "structs.h"

class CustomTime
{
public:
    CustomTime(
        const char *ntpServer,
        int timeSyncInterval,
        GeneralConfiguration &generalConfiguration,
        AdvancedLogger &logger);

    bool begin();
    void loop();

    bool isTimeSynched() { return _isTimeSynched; }

    static String timestampFromUnix(long unix);
    static String timestampFromUnix(long unix, const char *timestampFormat);

    static unsigned long getUnixTime();
    static unsigned long long getUnixTimeMilliseconds();
    static String getTimestamp();

private:
    bool _getTime();

    const char *_ntpServer;
    int _timeSyncInterval;

    bool _isTimeSynched = false;
    unsigned long _lastTimeSyncAttempt = 0;

    GeneralConfiguration &_generalConfiguration;
    AdvancedLogger &_logger;
};