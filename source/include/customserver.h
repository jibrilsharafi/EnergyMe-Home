#pragma once

#include <WiFiManager.h>
#include <Arduino.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <Update.h>
#include "esp_ota_ops.h"

#include "constants.h"
#include "globals.h"
#include "binaries.h"
#include "utils.h"

#define WEBSERVER_MAX_REQUESTS 180
#define WEBSERVER_WINDOW_SIZE_SECONDS 60 // in seconds

#define MINIMUM_FREE_HEAP_OTA 20000 // Minimum free heap required for OTA updates
#define SIZE_REPORT_UPDATE_OTA (128 * 1024) // Report progress every 128KB

#define HEALTH_CHECK_TASK_NAME "health_check_task"
#define HEALTH_CHECK_TASK_STACK_SIZE (4 * 1024)
#define HEALTH_CHECK_TASK_PRIORITY 1
#define HEALTH_CHECK_INTERVAL_MS (30 * 1000) // 30 seconds
#define HEALTH_CHECK_TIMEOUT_MS (5 * 1000) // 5 seconds timeout for health requests
#define HEALTH_CHECK_MAX_FAILURES 3 // Maximum consecutive failures before restart

// Authentication
#define PREFERENCES_NAMESPACE_AUTH "auth_ns" 
#define PREFERENCES_KEY_PASSWORD "password"
#define WEBSERVER_DEFAULT_USERNAME "admin"
#define WEBSERVER_DEFAULT_PASSWORD "energyme"
#define WEBSERVER_REALM "EnergyMe-Home"
#define MAX_PASSWORD_LENGTH 64
#define MIN_PASSWORD_LENGTH 4

namespace CustomServer {
    void begin();
    void updateAuthPassword();

    bool resetWebPassword();
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

//     void _handleDoUpdate(AsyncWebServerRequest *request, const char* filename, size_t index, unsigned char *data, size_t len, bool final);
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