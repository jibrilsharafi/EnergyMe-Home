#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <SPIFFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ade7953.h"
#include "led.h"
#include "customtime.h"
#include "customwifi.h"
#include "crashmonitor.h"
#include <ESPAsyncWebServer.h> // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "constants.h"
#include "influxdbclient.h"
#include "utils.h"
#include "custommqtt.h"
#include "binaries.h"
#include "buttonhandler.h"

class CustomServer
{
public:
    CustomServer(
        AsyncWebServer &server,
        AdvancedLogger &logger,
        Led &led,
        Ade7953 &ade7953,
        CustomWifi &customWifi,
        CustomMqtt &customMqtt,
        InfluxDbClient &influxDbClient,
        ButtonHandler &buttonHandler);

    ~CustomServer(); // Destructor to clean up semaphores
    void begin();

private:
    void _setHtmlPages();
    void _setOta();
    void _setRestApi();
    void _setOtherEndpoints();
    void _handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final);
    void _onUpdateSuccessful(AsyncWebServerRequest *request);
    void _onUpdateFailed(AsyncWebServerRequest *request, const char *reason);
    void _updateJsonFirmwareStatus(const char *status, const char *reason);
    void _serveJsonFile(AsyncWebServerRequest *request, const char *filePath);

    // Authentication methods
    bool _requireAuth(AsyncWebServerRequest *request);
    bool _checkAuth(AsyncWebServerRequest *request);
    void _sendUnauthorized(AsyncWebServerRequest *request);

    void _serverLog(const char *message, const char *function, LogLevel logLevel, AsyncWebServerRequest *request);

    AsyncWebServer &_server;
    AdvancedLogger &_logger;
    Led &_led;
    Ade7953 &_ade7953;
    CustomWifi &_customWifi;
    CustomMqtt &_customMqtt;
    InfluxDbClient &_influxDbClient;
    ButtonHandler &_buttonHandler;
    char _md5[33]; // MD5 is 32 characters + null terminator
    
    // Concurrency control using FreeRTOS semaphores
    SemaphoreHandle_t _configurationMutex;
    SemaphoreHandle_t _channelMutex;
    SemaphoreHandle_t _otaMutex;
    
    // Rate limiting for API requests
    unsigned long _lastChannelUpdateTime = 0;
    unsigned long _lastConfigUpdateTime = 0;
    
    // Helper methods for concurrency control
    bool _acquireMutex(SemaphoreHandle_t mutex, const char* mutexName, TickType_t timeout = pdMS_TO_TICKS(1000));
    void _releaseMutex(SemaphoreHandle_t mutex, const char* mutexName);
};