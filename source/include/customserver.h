#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <SPIFFS.h>

#include "ade7953.h"
#include "led.h"
#include "customtime.h"
#include "customwifi.h"
#include "crashmonitor.h"
#include <ESPAsyncWebServer.h> // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "constants.h"
#include "utils.h"
#include "custommqtt.h"
#include "binaries.h"

class CustomServer
{
public:
    CustomServer(
        AsyncWebServer &server,
        AdvancedLogger &logger,
        Led &led,
        Ade7953 &ade7953,
        CustomTime &customTime,
        CustomWifi &customWifi,
        CustomMqtt &customMqtt
    );

    void begin();

private:  
    void _setHtmlPages();
    void _setOta();
    void _setRestApi();
    void _setOtherEndpoints();
    void _handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final);
    void _onUpdateSuccessful(AsyncWebServerRequest *request);
    void _onUpdateFailed(AsyncWebServerRequest *request, const char* reason);
    void _updateJsonFirmwareStatus(const char* status, const char* reason);
    void _serveJsonFile(AsyncWebServerRequest *request, const char* filePath);

    void _serverLog(const char* message, const char* function, LogLevel logLevel, AsyncWebServerRequest *request);

    AsyncWebServer &_server;
    AdvancedLogger &_logger;
    Led &_led;
    Ade7953 &_ade7953;
    CustomTime &_customTime;
    CustomWifi &_customWifi;
    CustomMqtt &_customMqtt;

    String _md5;
};