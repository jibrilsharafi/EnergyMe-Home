#ifndef CUSTOMTIME_H
#define CUSTOMTIME_H

#include <Arduino.h>
#include <TimeLib.h>
#include <AdvancedLogger.h>

#include "constants.h"
#include "global.h"

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

    long getUnixTime();
    String getTimestamp();

private:
    bool _getTime();

    const char *_ntpServer;
    int _timeSyncInterval;
    const char *_timestampFormat;

    GeneralConfiguration &_generalConfiguration;
    AdvancedLogger &_logger;
};

#endif