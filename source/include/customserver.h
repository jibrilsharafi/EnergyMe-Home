#pragma once

#include <WiFiManager.h>
#include <Arduino.h>
#include <AsyncTCP.h>
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

// Rate limiting
#define WEBSERVER_MAX_REQUESTS 180
#define WEBSERVER_WINDOW_SIZE_SECONDS 60 // in seconds

#define MINIMUM_FREE_HEAP_OTA 20000 // Minimum free heap required for OTA updates
#define SIZE_REPORT_UPDATE_OTA (128 * 1024) // Report progress every 128KB
#define HTTP_RESPONSE_DELAY 2000 // Short delay to ensure HTTP response is sent before system actions (such as restart)

// Health check task
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

// Buffer sizes
#define HTTP_RESPONSE_BUFFER_SIZE 256

namespace CustomServer {
    void begin();
    void updateAuthPassword();

    bool resetWebPassword();
}