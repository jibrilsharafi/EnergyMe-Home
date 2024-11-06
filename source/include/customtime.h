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
        const char *timestampFormat,
        GeneralConfiguration &generalConfiguration,
        AdvancedLogger &logger);

    bool begin();

    String timestampFromUnix(long unix);
    static String timestampFromUnix(long unix, const char *timestampFormat);

    static unsigned long getUnixTime();
    unsigned long long getUnixTimeMilliseconds(); 
    String getTimestamp();

private:
    bool _getTime();

    const char *_ntpServer;
    int _timeSyncInterval;
    const char *_timestampFormat;

    GeneralConfiguration &_generalConfiguration;
    AdvancedLogger &_logger;
};