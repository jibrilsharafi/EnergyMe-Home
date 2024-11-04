#pragma once

#include <Arduino.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

#include "constants.h"
#include "utils.h"

class CustomWifi
{
public:
    CustomWifi(
        AdvancedLogger &logger,
        Led &led);

    bool begin();
    void loop();
    void resetWifi();

    void getWifiStatus(JsonDocument &jsonDocument);
    void printWifiStatus();

private:
    bool _connectToWifi();

    WiFiManager _wifiManager;

    AdvancedLogger &_logger;
    Led &_led;

    unsigned long _lastMillisWifiLoop = 0;
};