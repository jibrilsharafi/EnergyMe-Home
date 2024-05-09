#ifndef CUSTOMTIME_H
#define CUSTOMTIME_H

#include <Arduino.h>

#include <TimeLib.h>

#include "constants.h"
#include "global.h"

class CustomTime{
    public:
        CustomTime(
          const char* ntpServer,
          int timeSyncInterval
        );
        
        bool begin();

        long unixFromTimestamp(String timestamp);
        String timestampFromUnix(long unix);

        long getUnixTime();
        String getTimestamp();

    private:
        bool _getTime();

        const char* _ntpServer;
        int _timeSyncInterval;
};

#endif