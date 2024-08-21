#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

#include "constants.h"
#include "utils.h"

class CustomWifi
{
public:
    CustomWifi(
        AdvancedLogger &logger);

    bool begin();
    void loop();
    void resetWifi();

    void getWifiStatus(JsonDocument &jsonDocument);
    void printWifiStatus();

private:
    bool _connectToWifi();

    WiFiManager _wifiManager;

    AdvancedLogger &_logger;

    unsigned long _lastMillisWifiLoop = 0;
};

#endif