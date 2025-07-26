#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AdvancedLogger.h>

// #include "constants.h"
#include "globals.h"

#define WEBSERVER_PORT 80

namespace CustomServer {
    void begin();
}

// class CustomServer
// {
// public:
//     CustomServer(
//         AsyncWebServer &server,
//         AdvancedLogger &logger,
//         Ade7953 &ade7953,
//         CustomMqtt &customMqtt);

//     ~CustomServer();
//     void begin();

// private:
//     // Refactored: Replaced discrete setup functions with more focused ones
//     void _setStaticContent();
//     void _setApiHandlers();
//     void _setupAuthApi();
//     void _setupGetApi();
//     void _setupPostApi();
//     void _setOtaHandlers();
//     void _setNotFoundHandler();

//     void _handleDoUpdate(AsyncWebServerRequest *request, const char* filename, size_t index, uint8_t *data, size_t len, bool final);
//     void _onUpdateSuccessful(AsyncWebServerRequest *request);
//     void _onUpdateFailed(AsyncWebServerRequest *request, const char *reason);
    
//     AsyncWebServer &_server;
//     AdvancedLogger &_logger;
//     Ade7953 &_ade7953;
//     CustomMqtt &_customMqtt;
//     char _md5[MD5_BUFFER_SIZE];
    
//     SemaphoreHandle_t _configurationMutex;
//     SemaphoreHandle_t _channelMutex;
//     SemaphoreHandle_t _otaMutex;
    
//     // Rate limiting timestamps
//     unsigned long _lastConfigUpdateTime = 0;
//     unsigned long _lastChannelUpdateTime = 0;
    
//     // Refactored: Middleware objects are now members of the class
//     AsyncAuthenticationMiddleware _authMiddleware;
//     AsyncLoggingMiddleware _loggingMiddleware;
//     AsyncRateLimitMiddleware _apiRateLimitMiddleware;
    
//     // Refactored: Dedicated handlers for JSON POST endpoints
//     AsyncCallbackJsonWebHandler* _setCalibrationHandler;
//     AsyncCallbackJsonWebHandler* _setAdeConfigHandler;
//     AsyncCallbackJsonWebHandler* _setGeneralConfigHandler;
//     AsyncCallbackJsonWebHandler* _setChannelHandler;
//     AsyncCallbackJsonWebHandler* _setCustomMqttHandler;
//     AsyncCallbackJsonWebHandler* _setInfluxDbHandler;
//     AsyncCallbackJsonWebHandler* _setEnergyHandler;
//     AsyncCallbackJsonWebHandler* _uploadFileHandler;
//     AsyncCallbackJsonWebHandler* _changePasswordHandler;
//     AsyncCallbackJsonWebHandler* _loginHandler;

//     // Helper methods
//     bool _acquireMutex(SemaphoreHandle_t mutex, const char* mutexName, TickType_t timeout = pdMS_TO_TICKS(1000));
//     void _releaseMutex(SemaphoreHandle_t mutex, const char* mutexName);
// };