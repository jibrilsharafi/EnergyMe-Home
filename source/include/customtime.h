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

    static void timestampFromUnix(long unix, char* buffer);
    static void timestampFromUnix(long unix, const char *timestampFormat, char* buffer);

    static unsigned long getUnixTime();
    static unsigned long long getUnixTimeMilliseconds();
    static void getTimestamp(char* buffer);

private:
    bool _getTime();

    const char *_ntpServer;
    int _timeSyncInterval;

    bool _isTimeSynched = false;
    unsigned long _lastTimeSyncAttempt = 0;

    GeneralConfiguration &_generalConfiguration;
    AdvancedLogger &_logger;
};