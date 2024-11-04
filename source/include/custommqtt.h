#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFi.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "structs.h"
#include "utils.h"

class CustomMqtt
{
public:
    CustomMqtt(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        PubSubClient &customClientMqtt,
        CustomMqttConfiguration &customMqttConfiguration,
        MainFlags &mainFlags);

    void begin();
    void loop();

    bool setConfiguration(JsonDocument &jsonDocument);

private:
    void _setDefaultConfiguration();
    void _setConfigurationFromSpiffs();
    void _saveConfigurationToSpiffs();

    void _disable();

    bool _connectMqtt();

    void _publishMeter();

    bool _publishMessage(const char *topic, const char *message);

    bool _validateJsonConfiguration(JsonDocument &jsonDocument);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    PubSubClient &_customClientMqtt;
    CustomMqttConfiguration &_customMqttConfiguration;
    MainFlags &_mainFlags;

    bool _isSetupDone = false;

    unsigned long _lastMillisMqttLoop = 0;
    unsigned long _lastMillisMqttFailed = 0;
    unsigned long _mqttConnectionAttempt = 0;

    unsigned long _lastMillisMeterPublish = 0;
};