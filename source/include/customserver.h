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
#include "crashmonitor.h"
#include "custommqtt.h"
#include "influxdbclient.h"
#include "globals.h"
#include "binaries.h"
#include "utils.h"
#include "led.h"

// Rate limiting
#define WEBSERVER_MAX_REQUESTS 180
#define WEBSERVER_WINDOW_SIZE_SECONDS 60 // in seconds

#define MINIMUM_FREE_HEAP_OTA (20 * 1024) // Minimum free heap required for OTA updates
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

// API Request Synchronization
#define API_MUTEX_TIMEOUT_MS (5 * 1000) // Time to wait for API mutex for non-GET operations before giving up

// Buffer sizes
#define HTTP_HEALTH_CHECK_RESPONSE_BUFFER_SIZE 256 // Only needed for health check responses, n

// Content length validations
#define HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL 64
#define HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT 512
#define HTTP_MAX_CONTENT_LENGTH_INFLUXDB 1024
#define HTTP_MAX_CONTENT_LENGTH_LED_BRIGHTNESS 64

// Crash dump chunk sizes
#define CRASH_DUMP_DEFAULT_CHUNK_SIZE (2 * 1024)
#define CRASH_DUMP_MAX_CHUNK_SIZE (8 * 1024) // Maximum chunk size for core dump retrieval. Can be set high thanks to chunked transfer, but above 8 kB it will crash the wdt

namespace CustomServer {
    void begin();
    void stop();

    void updateAuthPassword();
    bool resetWebPassword();
}