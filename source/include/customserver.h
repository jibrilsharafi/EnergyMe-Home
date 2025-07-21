#pragma once

#include <Arduino.h>
#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <WiFi.h>
#endif

#include <WebServer.h> // Must define before to avoid conflicts with ESPAsyncWebServer HTTP definitions
#include <ESPAsyncWebServer.h>

void server_begin();

#include <Update.h>
#include "AsyncJson.h"
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <SPIFFS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ade7953.h"
#include "led.h"
#include "customtime.h"
// #include "customwifi.h"
#include "crashmonitor.h"
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
        Ade7953 &ade7953,
        CustomMqtt &customMqtt,
        InfluxDbClient &influxDbClient);

    ~CustomServer();
    void begin();

private:
    // Refactored: Replaced discrete setup functions with more focused ones
    void _setStaticContent();
    void _setApiHandlers();
    void _setupAuthApi();
    void _setupGetApi();
    void _setupPostApi();
    void _setOtaHandlers();
    void _setNotFoundHandler();

    void _handleDoUpdate(AsyncWebServerRequest *request, const char* filename, size_t index, uint8_t *data, size_t len, bool final);
    void _onUpdateSuccessful(AsyncWebServerRequest *request);
    void _onUpdateFailed(AsyncWebServerRequest *request, const char *reason);
    
    AsyncWebServer &_server;
    AdvancedLogger &_logger;
    Ade7953 &_ade7953;
    CustomMqtt &_customMqtt;
    InfluxDbClient &_influxDbClient;
    char _md5[MD5_BUFFER_SIZE];
    
    SemaphoreHandle_t _configurationMutex;
    SemaphoreHandle_t _channelMutex;
    SemaphoreHandle_t _otaMutex;
    
    // Rate limiting timestamps
    unsigned long _lastConfigUpdateTime = 0;
    unsigned long _lastChannelUpdateTime = 0;
    
    // Refactored: Middleware objects are now members of the class
    AsyncAuthenticationMiddleware _authMiddleware;
    AsyncLoggingMiddleware _loggingMiddleware;
    AsyncRateLimitMiddleware _apiRateLimitMiddleware;
    
    // Refactored: Dedicated handlers for JSON POST endpoints
    AsyncCallbackJsonWebHandler* _setCalibrationHandler;
    AsyncCallbackJsonWebHandler* _setAdeConfigHandler;
    AsyncCallbackJsonWebHandler* _setGeneralConfigHandler;
    AsyncCallbackJsonWebHandler* _setChannelHandler;
    AsyncCallbackJsonWebHandler* _setCustomMqttHandler;
    AsyncCallbackJsonWebHandler* _setInfluxDbHandler;
    AsyncCallbackJsonWebHandler* _setEnergyHandler;
    AsyncCallbackJsonWebHandler* _uploadFileHandler;
    AsyncCallbackJsonWebHandler* _changePasswordHandler;
    AsyncCallbackJsonWebHandler* _loginHandler;

    // Helper methods
    bool _acquireMutex(SemaphoreHandle_t mutex, const char* mutexName, TickType_t timeout = pdMS_TO_TICKS(1000));
    void _releaseMutex(SemaphoreHandle_t mutex, const char* mutexName);
};