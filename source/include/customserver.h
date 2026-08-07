// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include <AdvancedLogger.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"

#include "constants.h"
#include "crashmonitor.h"
#include "customlog.h"
#include "custommqtt.h"
#include "customtime.h"
#include "mqtt.h"
#include "influxdbclient.h"
#include "issueregistry.h"
#include "ade7953.h"
#include "globals.h"
#include "binaries.h"
#include "utils.h"
#include "led.h"
#include "web_auth_gate.h"

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
#define HEALTH_CHECK_TIMEOUT_MS (15 * 1000) // Allow handlers that legitimately take a few seconds (large config dumps, NVS writes)
#define HEALTH_CHECK_MAX_FAILURES 5 // Maximum consecutive failures before restart. 5 * 30s = 2.5 min of sustained failure - tolerant of transient stalls but still catches a real wedge

// Authentication
#define PREFERENCES_KEY_PASSWORD "password"
#define WEBSERVER_DEFAULT_USERNAME "admin"
#define WEBSERVER_DEFAULT_PASSWORD "energyme"
#define WEBSERVER_REALM "EnergyMe-Home"
#define MAX_PASSWORD_LENGTH 64
// Validated when a password is SET, never when one is used to authenticate, so a device
// already holding a shorter password keeps working. Raised from 4 to match what the UI
// has always promised - the firmware, not the browser, is the authority.
#define MIN_PASSWORD_LENGTH 8

// API Request Synchronization
#define API_MUTEX_TIMEOUT_MS (2 * 1000) // Time to wait for API mutex for non-GET operations before giving up. Long timeouts cause wdt crash (like in async tcp)

// Buffer sizes
#define HTTP_HEALTH_CHECK_RESPONSE_BUFFER_SIZE 256 // Only needed for health check HTTP response to own server

// Content length validations
#define HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL 64
#define HTTP_MAX_CONTENT_LENGTH_UDP_DESTINATIONS 256
#define HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT 512
#define HTTP_MAX_CONTENT_LENGTH_INFLUXDB 1024
#define HTTP_MAX_CONTENT_LENGTH_LED_BRIGHTNESS 64
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_CONFIG 1024
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_SAMPLE_TIME 64
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA 512
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_REGISTER 128
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_ENERGY 256
#define HTTP_MAX_CONTENT_LENGTH_ADE7953_WAVEFORM_ARM 64
#define HTTP_MAX_CONTENT_LENGTH_MQTT_CLOUD_SERVICES 64
#define HTTP_MAX_CONTENT_LENGTH_PASSWORD 256
#define HTTP_MAX_CONTENT_LENGTH_NETWORK 256
#define HTTP_MAX_CONTENT_LENGTH_ISSUES_ACK 128
#define HTTP_MAX_CONTENT_LENGTH_NVS_ENTRY 8192 // Large enough for a cert/key PEM value

// Dev-only NVS browser/editor (see _serveNvsDebugEndpoints)
#define NVS_DEBUG_MAX_ENTRIES 512   // 64 KB nvs partition / ~32 B min entry size, generous upper bound
#define NVS_DEBUG_MAX_NAMESPACES 32
#define NVS_NAME_BUFFER_SIZE 16     // NVS namespace/key names are capped at 15 chars + null
#define NVS_DEBUG_STRING_VALUE_BUFFER_SIZE 512 // Non-sensitive string values only; credential-looking keys are redacted

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

// Refuses to serve a device that is still holding the shipped default web password.
//
// The rule itself is not here - it lives in lib/web_auth_gate and is unit-tested on the
// host. This class is the adapter: read the cached flag, ask, act. Do not restate the
// allowlist here, or in customserver.cpp. wifi_provisioning::isAuthBypassAllowed() is what
// happens when that discipline slips: a tested rule nobody calls, and an untested inline
// copy that has already drifted from it.
class DefaultPasswordGuardMiddleware : public AsyncMiddleware {
public:
    // The flag lives in customserver.cpp; the middleware is handed a reader for it rather
    // than reaching across, so the "no NVS, no lock, no allocation on the request path"
    // property stays visible at the call site.
    void setStateReader(std::function<bool()> usingDefaultPassword) { _usingDefaultPassword = usingDefaultPassword; }

    void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {
        // Fail closed: an unconfigured guard refuses rather than waves through.
        bool locked = _usingDefaultPassword ? _usingDefaultPassword() : true;

        switch (WebAuthGate::evaluate(locked, request->url().c_str())) {
            case WebAuthGate::Action::ALLOW:
                next();
                return;

            case WebAuthGate::Action::REDIRECT_TO_ROOT: {
                // "/" serves the password change page while locked down.
                AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Change the default password first");
                response->addHeader("Location", "/");
                request->send(response);
                return;
            }

            case WebAuthGate::Action::DENY:
                // Machine-readable, so the browser client can bounce a stale tab to the gate
                // and a script can tell this apart from an ordinary authorization failure.
                request->send(HTTP_CODE_FORBIDDEN, "application/json",
                              "{\"success\":false,\"error\":\"The default web password is still in use. Change it at / before using the API.\",\"reason\":\"default_password\"}");
                return;
        }
    }

private:
    // Never store `next` and call it later: AsyncMiddlewareChain::_runChain captures it and
    // the list iterator by reference into a lambda that lives on its own stack frame.
    std::function<bool()> _usingDefaultPassword = nullptr;
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