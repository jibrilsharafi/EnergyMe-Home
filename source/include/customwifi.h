#pragma once

#include <Arduino.h>
#include <AdvancedLogger.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

#include "constants.h"
#include "led.h"
#include "structs.h"
#include "utils.h"

class CustomWifi
{
public:
    CustomWifi(
        AdvancedLogger &logger
    );

    bool begin();
    void loop();
    void resetWifi();

    WifiInfo getWifiInfo();
    void getWifiInfoJson(JsonDocument &jsonDocument);
    void printWifiInfo();

private:
    bool _connectToWifi();

    WiFiManager _wifiManager;

    AdvancedLogger &_logger;

    unsigned long _lastMillisWifiLoop = 0;
    unsigned long _lastConnectionAttemptTime = 0;
    int _failedConnectionAttempts = 0;
    
    // Track if portal was actually used in this session
    bool _portalWasStarted = false;
};