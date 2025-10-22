// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <WiFiManager.h>
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"

#include "constants.h"
#include "crashmonitor.h"
#include "custommqtt.h"
#include "mqtt.h"
#include "influxdbclient.h"
#include "ade7953.h"
#include "globals.h"
#include "binaries.h"
#include "utils.h"
#include "led.h"

// Rate limiting
#define WEBSERVER_MAX_REQUESTS 6000
#define WEBSERVER_WINDOW_SIZE_SECONDS 600

#define MINIMUM_FREE_HEAP_OTA (10 * 1024) // Minimum free heap required for OTA updates
#define SIZE_REPORT_UPDATE_OTA (128 * 1024) // Print progress every X bytes during OTA update
#define OTA_TIMEOUT (3 * 60 * 1000) // Maximum time allowed for OTA process
#define OTA_TIMEOUT_TASK_NAME "ota_timeout_task"
#define OTA_TIMEOUT_TASK_STACK_SIZE (6 * 1024) // Strangely, this seemed to be starved with 4 kB
#define OTA_TIMEOUT_TASK_PRIORITY 2
// Here used to lie the delay before restarting or doing some operations to ensure the response is sent
// but then I undestood that the delay was (also) blocking the AsyncTCP task itself, so it was useless ¯\_(ツ)_/¯

// Health check task
#define HEALTH_CHECK_TASK_NAME "health_check_task"
#define HEALTH_CHECK_TASK_STACK_SIZE (6 * 1024)
#define HEALTH_CHECK_TASK_PRIORITY 1
#define HEALTH_CHECK_INTERVAL_MS (30 * 1000)
#define HEALTH_CHECK_TIMEOUT_MS (5 * 1000)
#define HEALTH_CHECK_MAX_FAILURES 3 // Maximum consecutive failures before restart

// Authentication
#define PREFERENCES_KEY_PASSWORD "password"
#define WEBSERVER_DEFAULT_USERNAME "admin"
#define WEBSERVER_DEFAULT_PASSWORD "energyme"
#define WEBSERVER_REALM "EnergyMe-Home"
#define MAX_PASSWORD_LENGTH 64
#define MIN_PASSWORD_LENGTH 4

// API Request Synchronization
#define API_MUTEX_TIMEOUT_MS (2 * 1000) // Time to wait for API mutex for non-GET operations before giving up. Long timeouts cause wdt crash (like in async tcp)

// Buffer sizes
#define HTTP_HEALTH_CHECK_RESPONSE_BUFFER_SIZE 256 // Only needed for health check HTTP response to own server

// Content length validations
#define HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL 64
#define HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT 512
#define HTTP_MAX_CONTENT_LENGTH_INFLUXDB 1024
#define HTTP_MAX_CONTENT_LENGTH_LED_BRIGHTNESS 64
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_CONFIG 1024
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_SAMPLE_TIME 64
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA 512
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_REGISTER 128
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_ENERGY 256
#define HTTP_MAX_CONTENT_LENGTH_MQTT_CLOUD_SERVICES 64
#define HTTP_MAX_CONTENT_LENGTH_PASSWORD 256

// Crash dump chunk sizes
#define CRASH_DUMP_DEFAULT_CHUNK_SIZE (1 * 1024)
#define CRASH_DUMP_MAX_CHUNK_SIZE (4 * 1024) // Maximum chunk size for core dump retrieval. Can be set high thanks to chunked transfer, but above 4-8 kB it will crash the wdt

class CustomMiddleware : public AsyncMiddleware {
public:
    void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {
        // Log incoming request details
        LOG_VERBOSE("Request received: %s %s from %s", 
                    request->methodToString(), 
                    request->url().c_str(),
                    request->client()->remoteIP().toString().c_str());
        
        // Increment request count before processing
        statistics.webServerRequests++;
        
        // Continue with the middleware chain
        next();
        
        // Check for error responses after processing
        AsyncWebServerResponse* response = request->getResponse();
        if (response && response->code() >= HTTP_CODE_BAD_REQUEST) {
            statistics.webServerRequests--;
            statistics.webServerRequestsError++;
            LOG_DEBUG("Request  from %s completed with error: %s %s -> HTTP %d",
                        request->client()->remoteIP().toString().c_str(),
                        request->methodToString(), 
                        request->url().c_str(), 
                        response->code());
        } else if (response) {
            LOG_VERBOSE("Request from %s completed successfully: %s %s -> HTTP %d",
                        request->client()->remoteIP().toString().c_str(),
                        request->methodToString(),
                        request->url().c_str(),
                        response->code());
        }
    }
};

namespace CustomServer {
    // Web server management
    void begin();
    void stop();

    // Authentication management
    void updateAuthPasswordWithOneFromPreferences();
    bool resetWebPassword(); // This has to be accessible from buttonHandler to physically reset the password 

    // Task information
    TaskInfo getHealthCheckTaskInfo();
    TaskInfo getOtaTimeoutTaskInfo();
}