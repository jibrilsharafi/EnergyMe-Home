// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "customserver.h"
#include "modbustcp.h" // Local integrations are started/stopped to follow the STA link
#include "taskprofiler.h"
#include "duration_format.h"
#include "shadow.h"
#include "factory_keys.h"
#include <Preferences.h>
#include "nvs.h"
#include "nvs_flash.h"
#include <esp_random.h>

namespace CustomServer
{
    static TaskHeartbeat _healthCheckHeartbeat;

    // Private variables
    // ==============================
    // ==============================

    static AsyncWebServer server(WEBSERVER_PORT);
    static AsyncAuthenticationMiddleware digestAuth;
    static AsyncRateLimitMiddleware rateLimit;
    static CustomMiddleware customMiddleware;
    static DefaultPasswordGuardMiddleware defaultPasswordGuard;
    static AuthLockoutMiddleware authLockout;
    static UnauthenticatedRateLimitMiddleware unauthRateLimit;

    // Whether the stored web password is still the shipped default. Read on the AsyncTCP
    // task for every request to every path, so it must never become an NVS read: it is
    // refreshed only when the password can actually have changed (boot, and
    // updateAuthPasswordWithOneFromPreferences(), which is already the single funnel
    // through which a change reaches the live digestAuth object).
    //
    // volatile rather than a mutex, matching how CustomWifi publishes getProvisioningState()
    // and isApAddress() across tasks: one aligned byte, written by the API or button task,
    // read by AsyncTCP. A one-request-stale read is harmless in either direction because the
    // guard re-evaluates on the next request.
    //
    // Starts true and stays true if NVS cannot be read - a device that cannot prove its
    // password was changed is treated as not having changed it.
    static volatile bool _usingDefaultPassword = true;

    // Health check task variables
    static TaskHandle_t _healthCheckTaskHandle = NULL;
    static bool _healthCheckTaskShouldRun = false;
    static uint32_t _consecutiveFailures = 0;

    // OTA timeout task variables
    static TaskHandle_t _otaTimeoutTaskHandle = NULL;
    static bool _otaTimeoutTaskShouldRun = false;

    // API request synchronization
    static SemaphoreHandle_t _apiMutex = NULL;

    // ETag for proper caching
    static char _cachedEtag[MD5_BUFFER_SIZE + 3] = {0}; // +3 for quotes and null terminator
    static bool _etagComputed = false;

    // Private functions declarations
    // ==============================
    // ==============================

    // Handlers and middlewares
    static void _setupMiddleware();
    static void _refreshDefaultPasswordFlag();
    static bool _isApOrigin(AsyncWebServerRequest *request);
    static void _serveStaticContent();
    static void _serveApi();

    // Tasks
    static void _startHealthCheckTask();
    static void _stopHealthCheckTask();
    static void _healthCheckTask(void *parameter);
    static bool _performHealthCheck();

    // OTA timeout task
    static void _startOtaTimeoutTask();
    static void _stopOtaTimeoutTask();
    static void _otaTimeoutTask(void *parameter);

    // Authentication management
    static bool _setWebPassword(const char *password);
    static bool _getWebPasswordFromPreferences(char *buffer, size_t bufferSize);
    static bool _validatePasswordStrength(const char *password);

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc, int32_t statusCode = HTTP_CODE_OK);
    static void _sendSuccessResponse(AsyncWebServerRequest *request, const char *message);
    static void _sendErrorResponse(AsyncWebServerRequest *request, int32_t statusCode, const char *message);

    // API request synchronization helpers
    static bool _acquireApiMutex(AsyncWebServerRequest *request);

    // API endpoint groups
    static void _serveSystemEndpoints();
    static void _serveIssueEndpoints();
    static void _serveNetworkEndpoints();
    static void _serveLoggingEndpoints();
    static void _serveHealthEndpoints();
    static void _serveAuthEndpoints();
    static void _serveOtaEndpoints();
    static void _serveAde7953Endpoints();
    static void _serveCustomMqttEndpoints();
    static void _serveInfluxDbEndpoints();
    static void _serveCrashEndpoints();
    static void _serveLedEndpoints();
    static void _serveBackupEndpoints();
    static void _serveRestoreEndpoints();
    static void _serveFileEndpoints();
#ifdef ENV_DEV
    static void _serveShadowDevEndpoints();
    static void _serveCrashTestEndpoints();
    static void __attribute__((noinline)) _crashTestStackOverflow(uint32_t depth);
    static void _serveNvsDebugEndpoints();
#endif
    
    // Authentication endpoints
    static void _serveAuthStatusEndpoint();
    static void _serveChangePasswordEndpoint();
    static void _serveResetPasswordEndpoint();
    
    // OTA endpoints
    static void _serveOtaUploadEndpoint();
    static void _serveOtaStatusEndpoint();
    static void _serveOtaRollbackEndpoint();
    static bool _fetchGitHubReleaseInfo(JsonDocument &doc);
    static void _serveFirmwareStatusEndpoint();
    static void _handleOtaUploadComplete(AsyncWebServerRequest *request);
    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename, 
                                   size_t index, uint8_t *data, size_t len, bool final);
    
    // File upload handler
    static void _handleFileUploadData(AsyncWebServerRequest *request, const String& filename,
                                    size_t index, uint8_t *data, size_t len, bool final);

    // OTA helper functions
    static bool _initializeOtaUpload(AsyncWebServerRequest *request, const String& filename);
    static void _setupOtaMd5Verification(AsyncWebServerRequest *request);
    static bool _writeOtaChunk(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index);
    static void _finalizeOtaUpload(AsyncWebServerRequest *request);
    
    // Logging helper functions
    static bool _parseLogLevel(const char *levelStr, LogLevel &level);
    
    // HTTP method validation helper
    static bool _validateRequest(AsyncWebServerRequest *request, const char *expectedMethod, size_t maxContentLength = 0);
    static bool _isPartialUpdate(AsyncWebServerRequest *request);
    
    // ETag validation helper
    static bool _checkEtagAndSend304(AsyncWebServerRequest *request, const char* etag);
    static void _sendResponseWithEtag(AsyncWebServerRequest *request, AsyncWebServerResponse *response, const char* etag);
    
    // Public functions
    // ================
    // ================

    void begin()
    {
        LOG_DEBUG("Setting up web server...");

        // Initialize API synchronization mutex
        if (!createMutexIfNeeded(&_apiMutex)) {
            LOG_ERROR("Failed to create API mutex");
            return;
        }
        LOG_DEBUG("API mutex created successfully");

        _setupMiddleware();
        _serveStaticContent();
        _serveApi();

        server.begin();

        LOG_INFO("Web server started on port %d", WEBSERVER_PORT);

        // Start health check task to ensure the web server is responsive, and if it is not, restart the ESP32
        _startHealthCheckTask();
    }

    void stop()
    {
        LOG_DEBUG("Stopping web server...");

        // Stop health check task
        _stopHealthCheckTask();

        // Stop OTA timeout task
        _stopOtaTimeoutTask();

        // Stop the server
        server.end();

        // Delete API mutex
        deleteMutex(&_apiMutex);
        
        LOG_DEBUG("Web server stopped");
    }

    void updateAuthPasswordWithOneFromPreferences()
    {
        char webPassword[WEB_PASSWORD_BUFFER_SIZE];
        if (_getWebPasswordFromPreferences(webPassword, sizeof(webPassword)))
        {
            digestAuth.setPassword(webPassword);
            digestAuth.generateHash(); // regenerate hash with new password
            LOG_DEBUG("Authentication password updated");
        }
        else
        {
            LOG_ERROR("Failed to load new password for authentication");
        }

        // Every path that changes the password comes through here - the change-password
        // handler, the reset-password handler, and the physical button - so this is the
        // one place the lockdown flag needs refreshing. Deliberately outside the branch
        // above: a failed read must still be reflected, and it fails closed.
        _refreshDefaultPasswordFlag();
    }

    // Recomputes the cached lockdown flag. The only NVS read behind it, and it happens
    // once per password change rather than once per request.
    static void _refreshDefaultPasswordFlag()
    {
        char storedPassword[WEB_PASSWORD_BUFFER_SIZE];
        if (!_getWebPasswordFromPreferences(storedPassword, sizeof(storedPassword)))
        {
            // Fail closed. _setupMiddleware() already writes the default back when the read
            // fails, so the next boot reads cleanly and this becomes truthful rather than
            // merely conservative.
            _usingDefaultPassword = true;
            LOG_WARNING("Could not read the stored password - assuming the default is still in use");
            return;
        }

        _usingDefaultPassword = (strcmp(storedPassword, WEBSERVER_DEFAULT_PASSWORD) == 0);

        if (_usingDefaultPassword) LOG_WARNING("Default web password is in use - the device will refuse to serve anything but the password change");
        else LOG_DEBUG("Web password is not the default");
    }

    bool resetWebPassword()
    {
        LOG_DEBUG("Resetting web password to default");
        return _setWebPassword(WEBSERVER_DEFAULT_PASSWORD);
    }

    // Private functions
    // =================
    // =================

    static void _setupMiddleware()
    {
        // ---- Statistics Middleware Setup ----
        // Add statistics tracking middleware first to capture all requests
        server.addMiddleware(&customMiddleware);
        LOG_DEBUG("Statistics middleware configured");

        // ---- Authentication Middleware Setup ----
        // Configure digest authentication (more secure than basic auth)
        digestAuth.setUsername(WEBSERVER_DEFAULT_USERNAME);

        // Load password from Preferences or use default
        char webPassword[WEB_PASSWORD_BUFFER_SIZE];
        if (_getWebPasswordFromPreferences(webPassword, sizeof(webPassword)))
        {
            digestAuth.setPassword(webPassword);
            LOG_DEBUG("Web password loaded from Preferences");
        }
        else
        {
            // Fallback to default password if Preferences failed
            digestAuth.setPassword(WEBSERVER_DEFAULT_PASSWORD);
            LOG_INFO("Failed to load web password, using default");

            // Try to initialize the password in Preferences for next time
            if (_setWebPassword(WEBSERVER_DEFAULT_PASSWORD)) { LOG_DEBUG("Default password saved to Preferences for future use"); }
        }

        digestAuth.setRealm(WEBSERVER_REALM);
        digestAuth.setAuthFailureMessage("The password is incorrect. Please try again.");
        digestAuth.setAuthType(AsyncAuthType::AUTH_DIGEST);
        digestAuth.generateHash(); // precompute hash for better performance

        // Known limitation (accepted risk, issue #222): ESPAsyncWebServer issues a fresh
        // nonce/opaque per 401 but AsyncWebServerRequest::authenticate() passes them as NULL
        // into checkDigestAuthentication(), which skips validating any field it receives as
        // NULL (WebRequest.cpp / WebAuthentication.cpp in the library). So a captured
        // Authorization header for a given request is replayable indefinitely. No easy fix
        // without the firmware tracking issued nonces itself; the LAN link is the trust
        // boundary for this device. No action taken here.

        // ---- Auth Lockout Setup ----
        // Ahead of everything else that can refuse a request, so a source already locked out
        // for guessing costs a table scan rather than a digest MD5.
        authLockout.begin([]() { return millis64(); });
        server.addMiddleware(&authLockout);

        LOG_DEBUG("Auth lockout configured: %d consecutive failures -> %d s, doubling to %d s",
                  AUTH_LOCKOUT_MAX_FAILURES, AUTH_LOCKOUT_BASE_SECONDS, AUTH_LOCKOUT_MAX_SECONDS);

        // ---- Rate Limiting Middleware Setup ----
        // BEFORE digestAuth, deliberately. AsyncAuthenticationMiddleware::run short-circuits
        // with requestAuthentication() instead of calling next() (Middleware.cpp:143), so
        // anything registered after it never runs on a 401 - which meant the limiter was not
        // merely mistuned on the failed-login path, it was unreachable. Measured before this
        // change: 400 consecutive failed logins, zero 429 (issue #197).
        rateLimit.setMaxRequests(WEBSERVER_MAX_REQUESTS);
        rateLimit.setWindowSize(WEBSERVER_WINDOW_SIZE_SECONDS);

        // Only unauthenticated requests count toward the ceiling - the owner's authenticated
        // traffic is never throttled. See UnauthenticatedRateLimitMiddleware.
        unauthRateLimit.setInner(&rateLimit);
        server.addMiddleware(&unauthRateLimit);

        LOG_DEBUG("Rate limiting (unauthenticated only) configured: max requests = %d, window size = %d seconds", WEBSERVER_MAX_REQUESTS, WEBSERVER_WINDOW_SIZE_SECONDS);

        server.addMiddleware(&digestAuth);

        LOG_DEBUG("Digest authentication configured");

        // ---- Default Password Lockdown Setup ----
        // Registered last, and that position is load-bearing in both directions.
        //
        // After digestAuth: a caller who does not authenticate must get the ordinary 401 and
        // learn nothing. Only a caller who HAS authenticated with the published default is
        // told, via 403, why the device will not serve them. Putting this first would
        // advertise the device's password state to any unauthenticated scanner on the LAN.
        //
        // After rateLimit: refused requests still count against the limiter, so the lockdown
        // cannot be used as a cheap way around it.
        //
        // Note this covers only routes that run the server chain. The provisioning twins and
        // /api/v1/health call skipServerMiddlewares(), which replaces the whole server chain
        // (WebRequest.cpp:877-891), so the guard never runs on them. That is correct - a
        // factory-fresh device is by definition on the default password, and a lockdown that
        // blocked WiFi setup would strand it before it ever had a network - but it is
        // emergent rather than written down anywhere, so do not "tidy" those registrations
        // back onto the server chain.
        _refreshDefaultPasswordFlag();
        defaultPasswordGuard.setStateReader([]() { return _usingDefaultPassword; });
        defaultPasswordGuard.setApOriginReader(_isApOrigin);
        server.addMiddleware(&defaultPasswordGuard);

        LOG_DEBUG("Default password guard configured");

        LOG_DEBUG("Logging middleware configured");
    }

    // True only when the device has no stored credentials AND the request was addressed to
    // the SoftAP's own address. This is the whole auth carve-out (D3/D9).
    //
    // Runs on the AsyncTCP task for every request to every path, so it must stay a state
    // read plus an address compare: no mutex, no NVS, no logging, no allocation. Both reads
    // are volatile loads of values the WiFi task publishes, which is why isApAddress() exists
    // instead of WiFi.softAPIP() - the latter reads the netif through esp_netif_get_ip_info()
    // on every request to every path.
    //
    // IMPORTANT, do not build on a guarantee this does not give: client()->localIP() reads
    // _pcb->local_ip (AsyncTCP.cpp:1341-1352), the destination address lwIP recorded for the
    // accepted connection. That is NOT proof of which physical netif the packet arrived on,
    // and it is the same field ON_AP_FILTER reads. What makes this safe is the state check
    // below, not the address compare. Bench-4 exists precisely because only hardware can
    // show whether a LAN host static-routed to the AP subnet satisfies the compare.
    //
    // Still preferred over ON_AP_FILTER (`WiFi.localIP() != client()->localIP()`), which
    // returns true for the device's own 127.0.0.1 health probe and true for EVERYTHING
    // whenever WiFi.localIP() is 0.0.0.0 - exactly the unprovisioned case, so it would open
    // the entire UI on the LAN the moment STA dropped. Comparing against the SoftAP address
    // fixes both of those; it does not turn the check into an interface check.
    static bool _isProvisioningOrigin(AsyncWebServerRequest *request)
    {
        // The state check carries the safety: outside UNPROVISIONED there is no bypass at
        // all, whatever the address comparison says.
        if (CustomWifi::getProvisioningState() != WifiProvisioning::State::UNPROVISIONED) return false;

        AsyncClient *client = request->client();
        if (client == nullptr) return false;

        // False whenever no AP is up, so there is nothing to carve out until one exists.
        return CustomWifi::isApAddress(client->localIP());
    }

    // Same origin test, but also true during GRACE: the window right after credentials are
    // accepted, while the AP is deliberately held up so the user can see where the meter
    // went. Without this the setup page 401s the moment it succeeds - reloading it gets a
    // browser password prompt, and the grace window has nothing to show, which defeats its
    // entire purpose.
    //
    // Used only for reading: the page, its assets, the captive probes and the status
    // endpoint. Anything that changes the device (credentials, scan) stays on the strict
    // UNPROVISIONED test above, and AP_ASSIST remains fully closed either way.
    static bool _isProvisioningSession(AsyncWebServerRequest *request)
    {
        WifiProvisioning::State state = CustomWifi::getProvisioningState();
        if (state != WifiProvisioning::State::UNPROVISIONED &&
            state != WifiProvisioning::State::GRACE) return false;

        AsyncClient *client = request->client();
        if (client == nullptr) return false;

        return CustomWifi::isApAddress(client->localIP());
    }

    // True whenever the request was addressed to the device's own SoftAP, in ANY provisioning
    // state. Deliberately weaker than the two tests above: it does not authorise a bypass of
    // anything, it only tells the default-password guard to widen its allowlist to the WiFi
    // provisioning surface (see lib/web_auth_gate).
    //
    // The state-carrying tests above cannot serve that purpose. They are UNPROVISIONED-only
    // (and GRACE for reads), which is precisely the gap: in GRACE and AP_ASSIST the
    // provisioning routes run the full middleware chain, so a meter on the default password
    // whose router changed would raise its assist AP and then refuse the credentials endpoint
    // it exists to offer.
    //
    // Same cost rules as the others - volatile load plus address compare, on the AsyncTCP task.
    static bool _isApOrigin(AsyncWebServerRequest *request)
    {
        // Restricted to the states where the device has NO working network of its own, which
        // is the entire justification for the widening. GRACE is deliberately excluded: STA is
        // connected there, so the user can reach the meter on the LAN and change the password
        // the ordinary way - widening would buy nothing and would cost something real.
        //
        // That matters because localIP() is the destination lwIP recorded for the connection,
        // not proof of arrival interface (see the comment on _isProvisioningOrigin). GRACE and
        // AP_ASSIST are the states where AP and STA are up simultaneously, so a LAN host that
        // can get a packet addressed to the AP address is only conceivable there. Excluding
        // GRACE removes the case where that host could re-point a *connected* meter at another
        // SSID; AP_ASSIST keeps it, but there the meter is already off the network, which is
        // the situation the widening exists to fix.
        WifiProvisioning::State state = CustomWifi::getProvisioningState();
        if (state != WifiProvisioning::State::UNPROVISIONED &&
            state != WifiProvisioning::State::AP_ASSIST) return false;

        AsyncClient *client = request->client();
        if (client == nullptr) return false;

        // False whenever no AP is up.
        return CustomWifi::isApAddress(client->localIP());
    }

    // Registers a route twice: an open handler that only matches provisioning-origin
    // requests, then the normal authenticated one.
    //
    // Insertion order is what makes this work - the first handler whose filter passes wins
    // (WebServer.cpp:145-154). skipServerMiddlewares() drops the whole server chain, which
    // includes rate limiting as well as auth, so the rate limiter is added back explicitly:
    // an unauthenticated route still must not be floodable.
    static void _onOpenDuringProvisioning(const char *uri, WebRequestMethodComposite method,
                                          ArRequestHandlerFunction handler)
    {
        server.on(uri, method, handler)
              .setFilter(_isProvisioningOrigin)
              .skipServerMiddlewares()
              .addMiddleware(&rateLimit);

        server.on(uri, method, handler);
    }

    // As above, but open for the whole provisioning session including GRACE. Read-only
    // routes only.
    static void _onOpenDuringSession(const char *uri, WebRequestMethodComposite method,
                                     ArRequestHandlerFunction handler)
    {
        server.on(uri, method, handler)
              .setFilter(_isProvisioningSession)
              .skipServerMiddlewares()
              .addMiddleware(&rateLimit);

        server.on(uri, method, handler);
    }

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc, int32_t statusCode)
    {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->setCode(statusCode);
        serializeJson(doc, *response);
        request->send(response);
    }

    static void _sendSuccessResponse(AsyncWebServerRequest *request, const char *message)
    {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["success"] = true;
        doc["message"] = message;
        _sendJsonResponse(request, doc, HTTP_CODE_OK);

        releaseMutex(&_apiMutex);
    }

    static void _sendErrorResponse(AsyncWebServerRequest *request, int32_t statusCode, const char *message)
    {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["success"] = false;
        doc["error"] = message;
        _sendJsonResponse(request, doc, statusCode);

        releaseMutex(&_apiMutex);
    }

    static bool _acquireApiMutex(AsyncWebServerRequest *request)
    {
        if (!acquireMutex(&_apiMutex, API_MUTEX_TIMEOUT_MS)) {
            LOG_WARNING("Failed to acquire API mutex within timeout");
            _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Server busy, please try again");
            return false;
        }

        LOG_DEBUG("API mutex acquired for request: %s", request->url().c_str());
        return true;
    }

    // Helper function to parse log level strings
    static bool _parseLogLevel(const char *levelStr, LogLevel &level)
    {
        if (!levelStr) return false;
        
        if (strcmp(levelStr, "VERBOSE") == 0)
            level = LogLevel::VERBOSE;
        else if (strcmp(levelStr, "DEBUG") == 0)
            level = LogLevel::DEBUG;
        else if (strcmp(levelStr, "INFO") == 0)
            level = LogLevel::INFO;
        else if (strcmp(levelStr, "WARNING") == 0)
            level = LogLevel::WARNING;
        else if (strcmp(levelStr, "ERROR") == 0)
            level = LogLevel::ERROR;
        else if (strcmp(levelStr, "FATAL") == 0)
            level = LogLevel::FATAL;
        else
            return false;
            
        return true;
    }

    // Helper function to validate HTTP method
    // We cannot do setMethod since it makes all PUT requests fail (404) for some weird reason
    // It is not too bad anyway since like this we have full control over the response
    static bool _validateRequest(AsyncWebServerRequest *request, const char *expectedMethod, size_t maxContentLength)
    {
        if (maxContentLength > 0 && request->contentLength() > maxContentLength)
        {
            char errorMsg[STATUS_BUFFER_SIZE];
            snprintf(errorMsg, sizeof(errorMsg), "Payload Too Large. Max: %zu", maxContentLength);
            _sendErrorResponse(request, HTTP_CODE_PAYLOAD_TOO_LARGE, errorMsg);
            return false;
        }

        if (strcmp(request->methodToString(), expectedMethod) != 0)
        {
            char errorMsg[STATUS_BUFFER_SIZE];
            snprintf(errorMsg, sizeof(errorMsg), "Method Not Allowed. Use %s.", expectedMethod);
            _sendErrorResponse(request, HTTP_CODE_METHOD_NOT_ALLOWED, errorMsg);
            return false;
        }

        return _acquireApiMutex(request);
    }

    static bool _isPartialUpdate(AsyncWebServerRequest *request)
    {
        // Check if the request method is PATCH (partial update) or PUT (full update)
        if (!request) return false; // Safety check

        const char* method = request->methodToString();
        bool isPartialUpdate = (strcmp(method, "PATCH") == 0);

        return isPartialUpdate;
    }

    static void _startHealthCheckTask()
    {
        if (_healthCheckTaskHandle != NULL)
        {
            LOG_DEBUG("Health check task is already running");
            return;
        }

        LOG_DEBUG("Starting health check task with %d bytes stack", HEALTH_CHECK_TASK_STACK_SIZE);
        _consecutiveFailures = 0;

        BaseType_t result = xTaskCreate(
            _healthCheckTask,
            HEALTH_CHECK_TASK_NAME,
            HEALTH_CHECK_TASK_STACK_SIZE,
            NULL,
            HEALTH_CHECK_TASK_PRIORITY,
            &_healthCheckTaskHandle);

        if (result != pdPASS) { 
            LOG_ERROR("Failed to create health check task"); 
        }
    }

    static void _stopHealthCheckTask() { 
        stopTaskGracefully(&_healthCheckTaskHandle, "Health check task");
    }

    static void _startOtaTimeoutTask()
    {
        if (_otaTimeoutTaskHandle != NULL)
        {
            LOG_DEBUG("OTA timeout task is already running");
            return;
        }

        LOG_DEBUG("Starting OTA timeout task with %d bytes stack", OTA_TIMEOUT_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _otaTimeoutTask,
            OTA_TIMEOUT_TASK_NAME,
            OTA_TIMEOUT_TASK_STACK_SIZE,
            NULL,
            OTA_TIMEOUT_TASK_PRIORITY,
            &_otaTimeoutTaskHandle);

        if (result != pdPASS) { 
            LOG_ERROR("Failed to create OTA timeout task"); 
            _otaTimeoutTaskHandle = NULL;
        }
    }

    static void _stopOtaTimeoutTask() { 
        stopTaskGracefully(&_otaTimeoutTaskHandle, "OTA timeout task"); 
    }

    static void _otaTimeoutTask(void *parameter)
    {
        LOG_DEBUG("OTA timeout task started - system will reboot in %d seconds if OTA doesn't complete", OTA_TIMEOUT / 1000);

        _otaTimeoutTaskShouldRun = true;
        
        uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(OTA_TIMEOUT));
        
        // If everything goes well, we will never reach here
        if (notificationValue == 0 && _otaTimeoutTaskShouldRun) {
            // Timeout occurred and task wasn't stopped - force reboot
            LOG_ERROR("OTA timeout exceeded (%d seconds), forcing system restart", OTA_TIMEOUT / 1000);
            setRestartSystem("OTA process timeout - forcing restart for system recovery");
        } else {
            LOG_DEBUG("OTA timeout task stopped normally");
        }

        _otaTimeoutTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    static void _healthCheckTask(void *parameter)
    {
        LOG_DEBUG("Health check task started");

        _healthCheckTaskShouldRun = true;
        while (_healthCheckTaskShouldRun)
        {
            TASK_HEARTBEAT(_healthCheckHeartbeat);

            // Perform health check
            if (_performHealthCheck())
            {
                // Reset failure counter on success
                if (_consecutiveFailures > 0)
                {
                    LOG_INFO("Health check recovered after %d failures", _consecutiveFailures);
                    _consecutiveFailures = 0;
                }
                LOG_DEBUG("Health check passed");
            }
            else
            {
                _consecutiveFailures++;
                LOG_WARNING("Health check failed (attempt %d/%d)", _consecutiveFailures, HEALTH_CHECK_MAX_FAILURES);

                if (_consecutiveFailures >= HEALTH_CHECK_MAX_FAILURES)
                {
                    LOG_ERROR("Health check failed %d consecutive times, requesting system restart", HEALTH_CHECK_MAX_FAILURES);
                    setRestartSystem("Server health check failures exceeded maximum threshold");
                    break; // Exit the task as we're restarting
                }
            }

            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS)) > 0)
            {
                _healthCheckTaskShouldRun = false;
                break;
            }
        }

        LOG_DEBUG("Health check task stopping");
        _healthCheckTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    static bool _performHealthCheck()
    {
        // Serving on the SoftAP with no upstream network is a working device, not a
        // sick one. Gating on isFullyConnected() here fails every 30 s during AP-only
        // operation, and five failures request a restart: at ~150 s uptime that is
        // above QUICK_RESTART_THRESHOLD so safe mode never arms, and below
        // COUNTERS_RESET_TIMEOUT so the consecutive-reset counter never clears. About
        // 25 minutes of that reaches MAX_RESET_COUNT, which rolls the firmware back
        // and wipes the user's NVS.
        if (!CustomWifi::isNetworkServiceable())
        {
            LOG_DEBUG("Health check: no serviceable network interface");
            return false;
        }

        // Follow the station link with the unauthenticated local integrations. This runs on
        // the periodic health-check task rather than from the WiFi task so customwifi keeps
        // no knowledge of the services layered on top of it. Idempotent, so the worst case
        // is Modbus appearing up to one check interval after STA comes up.
        ModbusTcp::syncWithNetwork(CustomWifi::isFullyConnected(), CustomWifi::isApServing());

        // Perform a simple HTTP self-request to verify server responsiveness
        WiFiClient client;
        client.setTimeout(HEALTH_CHECK_TIMEOUT_MS);

        if (!client.connect("127.0.0.1", WEBSERVER_PORT))
        {
            LOG_WARNING("Health check failed: Cannot connect to local web server");
            return false;
        }

        // Send a simple GET request to the health endpoint
        client.print("GET /api/v1/health HTTP/1.1\r\n");
        client.print("Host: 127.0.0.1\r\n");
        client.print("Connection: close\r\n\r\n");

        // Wait for response with timeout
        uint64_t startTime = millis64();
        uint32_t loops = 0;
        while (client.connected() && (millis64() - startTime) < HEALTH_CHECK_TIMEOUT_MS && loops < MAX_LOOP_ITERATIONS)
        {
            loops++;

            // Reset task watchdog periodically during HTTP wait
            if (loops % 50 == 0) {
                esp_task_wdt_reset();
            }

            if (client.available())
            {
                char line[HTTP_HEALTH_CHECK_RESPONSE_BUFFER_SIZE];
                size_t bytesRead = client.readBytesUntil('\n', line, sizeof(line) - 1);
                line[bytesRead] = '\0';

                if (strncmp(line, "HTTP/1.1 ", 9) == 0 && bytesRead >= 12)
                {
                    // Extract status code from characters 9-11
                    char statusStr[4] = {line[9], line[10], line[11], '\0'};
                    int32_t statusCode = atoi(statusStr);
                    client.stop();

                    if (statusCode == HTTP_CODE_OK)
                    {
                        LOG_DEBUG("Health check passed: HTTP OK");
                        return true;
                    }
                    else
                    {
                        LOG_WARNING("Health check failed: HTTP status code %d", statusCode);
                        return false;
                    }
                }
            }
            delay(10); // Small delay to prevent busy waiting
        }

        client.stop();
        LOG_WARNING("Health check failed: HTTP request timeout");
        return false;
    }

    // Password management functions
    // ------------------------------
    static bool _setWebPassword(const char *password)
    {
        if (!_validatePasswordStrength(password))
        {
            LOG_ERROR("Password does not meet strength requirements");
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, false))
        {
            LOG_ERROR("Failed to open auth preferences for writing");
            return false;
        }

        bool success = prefs.putString(PREFERENCES_KEY_PASSWORD, password) > 0;
        prefs.end();

        if (success) { LOG_INFO("Web password updated successfully"); }
        else { LOG_ERROR("Failed to save web password"); }

        return success;
    }

    static bool _getWebPasswordFromPreferences(char *buffer, size_t bufferSize)
    {
        LOG_DEBUG("Getting web password");

        if (buffer == nullptr || bufferSize == 0)
        {
            LOG_ERROR("Invalid buffer for getWebPassword");
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, true))
        {
            LOG_ERROR("Failed to open auth preferences for reading");
            return false;
        }

        size_t res = prefs.getString(PREFERENCES_KEY_PASSWORD, buffer, bufferSize);
        prefs.end();

        return res > 0 && res < bufferSize; // Ensure we don't return true if the password is actually null or too long
    }
    
    // Only check length - there is no need to be picky here
    static bool _validatePasswordStrength(const char *password)
    {
        if (password == nullptr) { return false; }

        size_t length = strlen(password);

        // Check minimum length
        if (length < MIN_PASSWORD_LENGTH)
        {
            LOG_WARNING("Password too short (min %d characters)", MIN_PASSWORD_LENGTH);
            return false;
        }

        // Check maximum length
        if (length > MAX_PASSWORD_LENGTH)
        {
            LOG_WARNING("Password too long (max %d characters)", MAX_PASSWORD_LENGTH);
            return false;
        }
        
        return true;
    }

    static void _serveApi()
    {
        // Group endpoints by functionality
        _serveSystemEndpoints();
        _serveIssueEndpoints();
        _serveNetworkEndpoints();
        _serveLoggingEndpoints();
        _serveHealthEndpoints();
        _serveAuthEndpoints();
        _serveOtaEndpoints();
        _serveAde7953Endpoints();
#ifdef ENV_DEV
        _serveShadowDevEndpoints();
        _serveCrashTestEndpoints();
        _serveNvsDebugEndpoints();
#endif
        _serveCustomMqttEndpoints();
        _serveInfluxDbEndpoints();
        _serveCrashEndpoints();
        _serveLedEndpoints();
        _serveBackupEndpoints();
        _serveRestoreEndpoints();
        _serveFileEndpoints();
    }

    // ETag helper functions for caching
    // =================================

    /**
     * Check ETag validity and send 304 Not Modified if matched
     * Returns true if 304 was sent (caller should return), false otherwise (continue processing)
     */
    static bool _checkEtagAndSend304(AsyncWebServerRequest *request, const char* etag)
    {
        if (request->hasHeader("If-None-Match")) {
            const String& clientEtag = request->header("If-None-Match");
            if (clientEtag == etag) {
                request->send(HTTP_CODE_NOT_MODIFIED);
                return true;
            }
        }
        return false;
    }

    /**
     * Add ETag and Cache-Control headers, then send response
     */
    static void _sendResponseWithEtag(AsyncWebServerRequest *request, AsyncWebServerResponse *response, const char* etag)
    {
        response->addHeader("Cache-Control", "no-cache"); // Always validate with server
        response->addHeader("ETag", etag);
        request->send(response);
    }

    /**
     * Get sketch MD5 hash as ETag for cache busting
     * Cached in static variable - computed once on first call
     * Returns ETag in format: "abc123def456..."
     */
    static const char* _getSketchEtag()
    {        
        if (!_etagComputed) {
            snprintf(_cachedEtag, sizeof(_cachedEtag), "\"%s\"", ESP.getSketchMD5().c_str());
            _etagComputed = true;
            LOG_DEBUG("Sketch ETag created (and saved in static variable): %s", _cachedEtag);
        }
        
        return _cachedEtag;
    }

    /**
     * Get a sketch ETag qualified by which body is being served.
     *
     * "/" is registered three times and serves three different documents - the WiFi setup
     * page, the password gate, and the dashboard - chosen by filters that change at runtime.
     * The plain sketch ETag identifies the FIRMWARE, not the body, so all three would share
     * one validator at one URL. A browser holding the gate page then sends
     * If-None-Match after the password changes, the dashboard twin answers 304, and the
     * browser re-renders the cached gate - which probes auth/status, sees the password is no
     * longer default, and navigates to "/" again. That is an unbreakable reload loop, and the
     * mirror case (cached dashboard, password reset by the button) hides the gate entirely.
     *
     * Only routes whose body varies at a fixed URL need this; everything else keeps the plain
     * sketch ETag, which is still correct because their content is fixed per firmware.
     */
    static const char* _getVariantEtag(const char* variant)
    {
        // One buffer per variant, so each returned pointer stays valid for the server's
        // lifetime - the route lambdas capture it once at registration.
        static char gateEtag[MD5_BUFFER_SIZE + 16] = {0};
        static char dashEtag[MD5_BUFFER_SIZE + 16] = {0};
        static char wifiEtag[MD5_BUFFER_SIZE + 16] = {0};

        char* buffer;
        if (strcmp(variant, "gate") == 0) buffer = gateEtag;
        else if (strcmp(variant, "dash") == 0) buffer = dashEtag;
        else buffer = wifiEtag;

        if (buffer[0] == '\0') {
            // _getSketchEtag() is already quoted, so splice the variant inside the quotes
            // rather than appending after them.
            const char* sketchEtag = _getSketchEtag();
            size_t length = strlen(sketchEtag);
            if (length >= 2 && sketchEtag[length - 1] == '"') {
                snprintf(buffer, MD5_BUFFER_SIZE + 16, "%.*s-%s\"", (int)(length - 1), sketchEtag, variant);
            } else {
                snprintf(buffer, MD5_BUFFER_SIZE + 16, "\"%s-%s\"", sketchEtag, variant);
            }
        }

        return buffer;
    }

    /**
     * Generate ETag from file metadata
     * Uses file size as primary identifier 
     * Should be very accurate for append-only files like csv energy data and txt logs)
     */
    static const char* _generateFileEtag(const char* filename) {
        File file = LittleFS.open(filename, "r");
        if (!file) return "";
        
        size_t fileSize = file.size();
        file.close();
        
        // Use file size as ETag - simple and effective for append-only files
        // Format: "size-{bytes}" e.g. "size-59635"
        static char etagBuffer[32];
        snprintf(etagBuffer, sizeof(etagBuffer), "\"size-%u\"", (unsigned)fileSize);
        return etagBuffer;
    }

    /**
     * Send static content with ETag validation
     * If client sends matching If-None-Match header, responds with 304 Not Modified (no body)
     * Otherwise sends full content with ETag and Cache-Control headers
     */
    static void _sendStaticWithEtag(AsyncWebServerRequest *request, const char* contentType, const uint8_t* content, size_t len, const char* etag)
    {
        // Check if client sent matching ETag
        if (_checkEtagAndSend304(request, etag)) return;

        // ETag doesn't match or not provided - stream full content from flash
        // (uint8_t* + size_t overload constructs AsyncProgmemResponse, which reads
        // chunks directly from flash via memcpy_P; no heap copy of the body, vs
        // the const char* overload that copies the entire file into a String and
        // can corrupt under concurrent parallel requests on a fragmented heap)
        AsyncWebServerResponse *response = request->beginResponse(HTTP_CODE_OK, contentType, content, len);
        _sendResponseWithEtag(request, response, etag);
    }

    /**
     * Send file with ETag validation
     * If client sends matching If-None-Match header, responds with 304 Not Modified
     * Otherwise streams file content with ETag and Cache-Control headers
     */
    static void _sendFileWithEtag(AsyncWebServerRequest *request, const char* filename, const char* contentType, bool forceDownload = false)
    {
        // Generate ETag from file metadata
        const char* etag = _generateFileEtag(filename);
        
        if (strlen(etag) == 0) {
            _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to generate ETag");
            return;
        }
        
        // Check if client sent matching ETag
        if (_checkEtagAndSend304(request, etag)) return;
        
        // ETag doesn't match or not provided - send full file
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, filename, contentType, forceDownload);
        
        if (!response) {
            _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to create response");
            return;
        }
        
        _sendResponseWithEtag(request, response, etag);
    }

    // === STATIC CONTENT SERVING ===
    static void _serveStaticContent()
    {
        // Cache strategy: "no-cache" with ETag validation
        // - Browser always asks server before using cache
        // - Server responds 304 Not Modified (no body) if ETag matches → fast
        // - Server responds 200 OK with full content if ETag differs → always fresh
        // When firmware is updated, sketch MD5 changes → ETag differs → fresh content

        // This needs to be a solid pointer, so we need a static variable
        // that will persist for the server's lifetime
        const char* etag = _getSketchEtag();

        // CSS files
        _onOpenDuringSession("/css/button.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(button_css), etag);
        });
        _onOpenDuringSession("/css/forms.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(forms_css), etag);
        });
        _onOpenDuringSession("/css/index.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(index_css), etag);
        });
        _onOpenDuringSession("/css/styles.css", HTTP_GET, [etag](AsyncWebServerRequest *request) { 
            _sendStaticWithEtag(request, "text/css", EMBEDDED(styles_css), etag);
        });
        _onOpenDuringSession("/css/section.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(section_css), etag);
        });
        _onOpenDuringSession("/css/tooltip.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(tooltip_css), etag);
        });
        _onOpenDuringSession("/css/typography.css", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/css", EMBEDDED(typography_css), etag);
        });

        // JavaScript files
        _onOpenDuringSession("/js/api-client.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(api_client_js), etag);
        });
        _onOpenDuringSession("/js/chart-helpers.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(chart_helpers_js), etag);
        });
        _onOpenDuringSession("/js/data-helpers.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(data_helpers_js), etag);
        });
        _onOpenDuringSession("/js/issues.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(issues_js), etag);
        });
        _onOpenDuringSession("/js/power-flow.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(power_flow_js), etag);
        });
        _onOpenDuringSession("/js/tooltip.js", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "application/javascript", EMBEDDED(tooltip_js), etag);
        });

        // Resources
        _onOpenDuringSession("/favicon.svg", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "image/svg+xml", EMBEDDED(favicon_svg), etag);
        });

        // Main dashboard
        // "/" is the one route whose twins differ. An unprovisioned device reached over its
        // own SoftAP lands on WiFi setup - which is what makes the captive-portal redirect
        // useful, since the OS opens "/" and nothing else. Everywhere else "/" is the
        // dashboard, unchanged.
        // Each twin carries its own validator - see _getVariantEtag(). Sharing one would let a
        // browser revalidate a cached gate page against the dashboard twin, get 304, and loop.
        const char* wifiEtag = _getVariantEtag("wifi");
        const char* gateEtag = _getVariantEtag("gate");
        const char* dashEtag = _getVariantEtag("dash");

        server.on("/", HTTP_GET, [wifiEtag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(wifi_setup_html), wifiEtag);
        }).setFilter(_isProvisioningSession)
          .skipServerMiddlewares()
          .addMiddleware(&rateLimit);

        // Then, on a device that has a network but is still holding the shipped default
        // password, the gate. Ordered between the two: provisioning comes first because an
        // unprovisioned device is necessarily also on the default password, and stranding it
        // here - before it has a network at all - would leave no way forward.
        //
        // Deliberately no skipServerMiddlewares(): the user must still authenticate to see the
        // gate, and the guard allows "/" anyway.
        server.on("/", HTTP_GET, [gateEtag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(password_setup_html), gateEtag);
        }).setFilter([](AsyncWebServerRequest *request) { (void)request; return _usingDefaultPassword; });

        server.on("/", HTTP_GET, [dashEtag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(index_html), dashEtag);
        });

        // Also reachable by name, so a provisioned device can be re-pointed at another
        // network from the normal UI without erasing its credentials first.
        _onOpenDuringSession("/wifi-setup.html", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(wifi_setup_html), etag);
        });

        // Configuration pages
        server.on("/ade7953-tester", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(ade7953_tester_html), etag);
        });
        server.on("/configuration", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(configuration_html), etag);
        });
        server.on("/calibration", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(calibration_html), etag);
        });
        server.on("/channel", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(channel_html), etag);
        });
        server.on("/info", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(info_html), etag);
        });
        server.on("/integrations", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(integrations_html), etag);
        });
        server.on("/log", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(log_html), etag);
        });
        server.on("/update", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(update_html), etag);
        });
        server.on("/waveform", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(waveform_html), etag);
        });

        // Swagger UI
        server.on("/swagger-ui", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/html", EMBEDDED(swagger_ui_html), etag);
        });
        server.on("/swagger.yaml", HTTP_GET, [etag](AsyncWebServerRequest *request) {
            _sendStaticWithEtag(request, "text/yaml", EMBEDDED(swagger_yaml), etag);
        });
    }

    // === HEALTH ENDPOINTS ===
    static void _serveHealthEndpoints()
    {
        server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            doc["status"] = "ok";
            doc["uptime"] = millis64();
            char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
            CustomTime::getTimestampIso(timestamp, sizeof(timestamp));
            doc["timestamp"] = timestamp;

            _sendJsonResponse(request, doc);
        }).skipServerMiddlewares(); // For the health endpoint, no authentication or rate limiting
    }

    // === AUTHENTICATION ENDPOINTS ===
    static void _serveAuthEndpoints()
    {
        _serveAuthStatusEndpoint();
        _serveChangePasswordEndpoint();
        _serveResetPasswordEndpoint();
    }

    static void _serveAuthStatusEndpoint()
    {
        server.on("/api/v1/auth/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            // The same cached flag the lockdown enforces on, not a second NVS read and
            // strcmp. Two independent computations of "is this the default password" could
            // disagree, and the one the user can see would be the one that is wrong.
            doc["usingDefaultPassword"] = _usingDefaultPassword;
            // Lets the gate page decide whether to offer the WiFi setup link. That link is
            // only reachable from the SoftAP while locked down - on the LAN it redirects
            // straight back to the gate - so offering it there would be a dead end.
            doc["apOrigin"] = _isApOrigin(request);
            doc["username"] = WEBSERVER_DEFAULT_USERNAME;
            
            _sendJsonResponse(request, doc);
        });
    }

    static void _serveChangePasswordEndpoint()
    {
        static AsyncCallbackJsonWebHandler *changePasswordHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/auth/change-password",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_PASSWORD)) return;

                SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
                doc.set(json);

                const char *currentPassword = doc["currentPassword"];
                const char *newPassword = doc["newPassword"];

                if (!currentPassword || !newPassword)
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing currentPassword or newPassword");
                    return;
                }

                // Validate current password
                char storedPassword[WEB_PASSWORD_BUFFER_SIZE];
                if (!_getWebPasswordFromPreferences(storedPassword, sizeof(storedPassword)))
                {
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve current password");
                    return;
                }

                if (strcmp(currentPassword, storedPassword) != 0)
                {
                    _sendErrorResponse(request, HTTP_CODE_UNAUTHORIZED, "Current password is incorrect");
                    return;
                }

                // Nothing else stops a user from "changing" the password to the published
                // default and staying exactly where they started. Checked here rather than in
                // _validatePasswordStrength() because resetWebPassword() sets the default on
                // purpose and must keep working - it backs the physical button, which is the
                // only recovery path for a forgotten password.
                if (strcmp(newPassword, WEBSERVER_DEFAULT_PASSWORD) == 0)
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "The new password cannot be the default password");
                    return;
                }

                // Validate and save new password
                if (!_setWebPassword(newPassword))
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "New password must be between 8 and 64 characters");
                    return;
                }

                LOG_INFO("Password changed successfully via API");
                _sendSuccessResponse(request, "Password changed successfully");
                
                // Update authentication middleware with new password
                updateAuthPasswordWithOneFromPreferences();
            });
        server.addHandler(changePasswordHandler);
    }

    static void _serveResetPasswordEndpoint()
    {
        server.on("/api/v1/auth/reset-password", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (resetWebPassword()) {
                updateAuthPasswordWithOneFromPreferences();
                LOG_WARNING("Password reset to default via API");
                _sendSuccessResponse(request, "Password reset to default");
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to reset password");
            }
        });
    }

    // === OTA UPDATE ENDPOINTS ===
    static void _serveOtaEndpoints()
    {
        _serveOtaUploadEndpoint();
        _serveOtaStatusEndpoint();
        _serveOtaRollbackEndpoint();
        _serveFirmwareStatusEndpoint();
    }

    static void _serveOtaUploadEndpoint()
    {
        server.on("/api/v1/ota/upload", HTTP_POST, 
            _handleOtaUploadComplete,
            _handleOtaUploadData);
    }

    static void _handleOtaUploadComplete(AsyncWebServerRequest *request)
    {
        // Handle the completion of the upload
        if (request->getResponse()) return;  // Response already set due to error

        // Stop OTA timeout task since OTA process is completing
        _stopOtaTimeoutTask();

        if (Update.hasError()) {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["success"] = false;
            doc["message"] = Update.errorString();
            _sendJsonResponse(request, doc);
            
            LOG_ERROR("OTA update failed: %s", Update.errorString());
            Update.printError(Serial);
            
            Led::blinkRedFast(Led::PRIO_CRITICAL, 5000ULL);
            
            // Schedule restart even on failure for system recovery
            setRestartSystem("Restart needed after failed firmware update for system recovery");
        } else {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            doc["success"] = true;
            doc["message"] = "Firmware update completed successfully";
            doc["md5"] = Update.md5String();
            _sendJsonResponse(request, doc);
            
            LOG_INFO("OTA update completed successfully");
            LOG_DEBUG("New firmware MD5: %s", Update.md5String().c_str());

            // Fresh image in the partition - give it its own rollback chance,
            // independent of whatever the previous firmware already consumed
            CrashMonitor::clearRollbackTried();

            Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
            setRestartSystem("Restart needed after firmware update");
        }
    }

    // Upload and body callbacks run DURING body parsing - WebRequest.cpp calls
    // _handler->handleUpload() at :612 and :783 while _parseState is PARSE_REQ_BODY, and only
    // reaches _runMiddlewareChain() at :233 once _parsedLength == _contentLength. Every server
    // middleware therefore fires AFTER the bytes have already been handled, digestAuth
    // included. For a firmware image that is far too late: Update.begin() has erased the
    // partition and Update.end(true) has already called esp_ota_set_boot_partition().
    //
    // The library knows: WebRequest.cpp:610 carries a "check if authenticated before calling
    // the upload" comment and then does not do it.
    //
    // So the upload routes must check for themselves, on the first chunk, before anything is
    // written. Returns true when the caller should stop; the response is already set, and
    // _handleOtaUploadComplete/_handleFileUploadData bail on request->getResponse().
    static bool _rejectUploadIfNotPermitted(AsyncWebServerRequest *request)
    {
        // Throttle before authenticating. This runs during body parsing, ahead of the lockout
        // middleware, so without this an attacker could move password guessing to the upload
        // routes and never be slowed: every attempt would run a full digest check here, and a
        // correct guess would reach Update.begin() before the middleware's 429 could mask it.
        // The middleware still records the failure afterwards from the 401 this leaves set.
        uint32_t retryAfterSeconds = 0;
        if (authLockout.isSourceLocked(request, retryAfterSeconds))
        {
            LOG_WARNING("Refused an upload from a locked-out source before authenticating");
            AsyncWebServerResponse *response = request->beginResponse(HTTP_CODE_TOO_MANY_REQUESTS, "application/json",
                "{\"success\":false,\"error\":\"Too many failed login attempts. Try again later.\"}");
            response->addHeader("Retry-After", String(retryAfterSeconds));
            request->send(response);
            return true;
        }

        char storedPassword[WEB_PASSWORD_BUFFER_SIZE];
        bool passwordReadable = _getWebPasswordFromPreferences(storedPassword, sizeof(storedPassword));

        if (!passwordReadable || !request->authenticate(WEBSERVER_DEFAULT_USERNAME, storedPassword, WEBSERVER_REALM))
        {
            LOG_WARNING("Refused an unauthenticated upload from %s before any data was written",
                        request->client()->remoteIP().toString().c_str());
            request->requestAuthentication(AsyncAuthType::AUTH_DIGEST, WEBSERVER_REALM, "The password is incorrect. Please try again.");
            return true;
        }

        // The same allowlist the middleware applies, for the same reason - just early enough
        // to matter.
        if (WebAuthGate::evaluate(_usingDefaultPassword, _isApOrigin(request), request->url().c_str()) != WebAuthGate::Action::ALLOW)
        {
            LOG_WARNING("Refused an upload while the default password is still in use");
            sendDefaultPasswordDeniedResponse(request);
            return true;
        }

        return false;
    }

    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename,
                                   size_t index, uint8_t *data, size_t len, bool final)
    {
        static bool otaInitialized = false;

        if (!index) {
            // Before Update.begin() erases anything. See _rejectUploadIfNotPermitted().
            if (_rejectUploadIfNotPermitted(request)) {
                otaInitialized = false;
                return;
            }

            // First chunk - initialize OTA
            if (!_initializeOtaUpload(request, filename)) {
                return;
            }
            otaInitialized = true;
        }
        
        // Write chunk to flash
        if (len && otaInitialized) {
            if (!_writeOtaChunk(request, data, len, index)) {
                otaInitialized = false;
                return;
            }
        }
        
        // Final chunk - complete the update
        if (final && otaInitialized) {
            _finalizeOtaUpload(request);
            otaInitialized = false;
        }
    }

    static bool _initializeOtaUpload(AsyncWebServerRequest *request, const String& filename)
    {
        LOG_INFO("Starting OTA update with file: %s", filename.c_str());
        
        // Validate file extension
        if (!filename.endsWith(".bin")) {
            LOG_ERROR("Invalid file type. Only .bin files are supported");
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File must be in .bin format");
            return false;
        }
        
        // Get content length from header
        size_t contentLength = request->header("Content-Length").toInt();
        if (contentLength == 0) {
            LOG_ERROR("No Content-Length header found or empty file");
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing Content-Length header or empty file");
            return false;
        }
        
        // Validate minimum firmware size
        if (contentLength < MINIMUM_FIRMWARE_SIZE) {
            LOG_ERROR("Firmware file too small: %zu bytes (minimum: %d bytes)", contentLength, MINIMUM_FIRMWARE_SIZE);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Firmware file too small");
            return false;
        }
        
        // Check free heap
        size_t freeHeap = ESP.getFreeHeap();
        LOG_DEBUG("Free heap before OTA: %zu bytes", freeHeap);
        if (freeHeap < MINIMUM_FREE_HEAP_OTA) {
            LOG_ERROR("Insufficient memory for OTA update");
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Insufficient memory for update");
            return false;
        }
        
        // Start OTA timeout watchdog task before beginning the actual OTA process
        _startOtaTimeoutTask();

        // Begin OTA update with known size
        if (!Update.begin(contentLength, U_FLASH)) {
            LOG_ERROR("Failed to begin OTA update: %s", Update.errorString());
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Failed to begin update");
            Led::doubleBlinkYellow(Led::PRIO_URGENT, 1000ULL);
            _stopOtaTimeoutTask(); // Stop timeout task on failure
            return false;
        }
        
        // Handle MD5 verification if provided
        _setupOtaMd5Verification(request);
        
        // Start LED indication for OTA progress
        Led::blinkPurpleFast(Led::PRIO_MEDIUM);
        
        LOG_DEBUG("OTA update started, expected size: %zu bytes", contentLength);
        return true;
    }

    static void _setupOtaMd5Verification(AsyncWebServerRequest *request)
    {
        if (!request->hasHeader("X-MD5")) {
            LOG_WARNING("No MD5 header provided, skipping verification");
            return;
        }
        
        const char* md5HeaderCStr = request->header("X-MD5").c_str();
        size_t headerLength = strlen(md5HeaderCStr);
        
        if (headerLength == MD5_BUFFER_SIZE - 1) {
            char md5Header[MD5_BUFFER_SIZE];
            snprintf(md5Header, sizeof(md5Header), "%s", md5HeaderCStr);

            // Convert to lowercase
            for (size_t i = 0; md5Header[i]; i++) {
                md5Header[i] = (char)tolower((unsigned char)md5Header[i]);
            }
            
            Update.setMD5(md5Header);
            LOG_DEBUG("MD5 verification enabled: %s", md5Header);
        } else if (headerLength > 0) {
            LOG_WARNING("Invalid MD5 length (%zu), skipping verification", headerLength);
        } else {
            LOG_WARNING("No MD5 header provided, skipping verification");
        }
    }

    static bool _writeOtaChunk(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index)
    {
        // Reset watchdog before flash write
        size_t written = Update.write(data, len);

        if (written != len) {
            LOG_ERROR("OTA write failed: expected %zu bytes, wrote %zu bytes", len, written);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Write failed");
            Update.abort();
            _stopOtaTimeoutTask(); // Stop timeout task on write failure
            return false;
        }

        // Log progress periodically
        static size_t lastProgressIndex = 0;
        if (index >= lastProgressIndex + SIZE_REPORT_UPDATE_OTA || index == 0) {
            esp_task_wdt_reset(); // Only do it once in a while
            float progress = Update.size() > 0UL ? (float)Update.progress() / (float)Update.size() * 100.0f : 0.0f;
            LOG_DEBUG("OTA progress: %.1f%% (%zu / %zu bytes)", progress, Update.progress(), Update.size());
            lastProgressIndex = index;
        }

        return true;
    }

    static void _finalizeOtaUpload(AsyncWebServerRequest *request)
    {
        LOG_DEBUG("Finalizing OTA update...");

        // Validate that we actually received data
        if (Update.progress() == 0) {
            LOG_ERROR("OTA finalization failed: No data received");
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "No firmware data received");
            Update.abort();
            _stopOtaTimeoutTask(); // Stop timeout task on failure
            return;
        }

        // Validate minimum size
        if (Update.progress() < MINIMUM_FIRMWARE_SIZE) {
            LOG_ERROR("OTA finalization failed: Firmware too small (%zu bytes)", Update.progress());
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Firmware file too small");
            Update.abort();
            _stopOtaTimeoutTask(); // Stop timeout task on failure
            return;
        }

        // Reset watchdog before flash verification and finalization
        bool success = Update.end(true);

        if (!success) {
            LOG_ERROR("OTA finalization failed: %s", Update.errorString());
            _stopOtaTimeoutTask(); // Stop timeout task on failure
            // Error response will be handled in the main handler
        } else {
            LOG_DEBUG("OTA update finalization successful");
            Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
            // Note: timeout task will be stopped in _handleOtaUploadComplete
        }
    }

    static void _handleFileUploadData(AsyncWebServerRequest *request, const String& filename, 
                                    size_t index, uint8_t *data, size_t len, bool final)
    {
        static File uploadFile;
        static String targetPath;

        if (!index) {
            // Before any file is opened or truncated. See _rejectUploadIfNotPermitted().
            if (_rejectUploadIfNotPermitted(request)) return;

            // First chunk - extract path from URL and create file
            String url = request->url();
            targetPath = url.substring(url.indexOf("/api/v1/files/") + 14); // Remove "/api/v1/files/" prefix
            
            if (targetPath.length() == 0) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File path cannot be empty");
                return;
            }
            
            // URL decode the filename to handle encoded slashes properly
            targetPath.replace("%2F", "/");
            targetPath.replace("%2f", "/");
            
            // Ensure filename starts with "/"
            if (!targetPath.startsWith("/")) {
                targetPath = "/" + targetPath;
            }
            
            LOG_DEBUG("Starting file upload to: %s", targetPath.c_str());
            
            // Check available space
            size_t freeSpace = LittleFS.totalBytes() - LittleFS.usedBytes();
            if (freeSpace < MINIMUM_FREE_LITTLEFS_SIZE) { // Require at least 1KB free space
                LOG_WARNING("Insufficient storage space for file upload: %zu bytes free", freeSpace);
                _sendErrorResponse(request, HTTP_CODE_INSUFFICIENT_STORAGE, "Insufficient storage space");
                return;
            }
            
            // Create file for writing
            uploadFile = LittleFS.open(targetPath, FILE_WRITE);
            if (!uploadFile) {
                LOG_ERROR("Failed to create file for upload: %s", targetPath.c_str());
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to create file");
                return;
            }
        }
        
        // Write data chunk
        if (len && uploadFile) {
            size_t written = uploadFile.write(data, len);

            if (written != len) {
                LOG_ERROR("Failed to write data chunk at index %zu", index);
                uploadFile.close();
                LittleFS.remove(targetPath); // Clean up partial file
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to write file data");
                return;
            }
        }
        
        // Final chunk - complete the upload
        if (final) {
            if (uploadFile) {
                uploadFile.close();
                LOG_INFO("File upload completed successfully: %s (%zu bytes)", targetPath.c_str(), index + len);
                _sendSuccessResponse(request, "File uploaded successfully");
            } else {
                LOG_ERROR("File upload failed: file handle not available");
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "File upload failed");
            }
        }
    }

    static void _serveOtaStatusEndpoint()
    {
        server.on("/api/v1/ota/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            doc["status"] = Update.isRunning() ? "running" : "idle";
            
            const esp_partition_t *running = esp_ota_get_running_partition();
            doc["currentPartition"] = running->label;
            doc["hasError"] = Update.hasError();
            doc["lastError"] = Update.errorString();
            doc["size"] = Update.size();
            doc["progress"] = Update.progress();
            doc["remaining"] = Update.remaining();
            doc["progressPercent"] = Update.size() > 0 ? (float)Update.progress() / (float)Update.size() * 100.0 : 0.0;
            
            // Add current firmware info
            doc["currentVersion"] = FIRMWARE_BUILD_VERSION;
            doc["currentMD5"] = ESP.getSketchMD5();

            // Rollback target fingerprint; null when the passive slot holds no
            // valid descriptor. Not read mid-upload: Update.write() is streaming
            // into that very partition (2 s poll would race the flash writes and
            // report a half-overwritten descriptor), and canRollback derives from
            // the same read so status and actuator agree on one predicate.
            char otherSha[SHA256_HEX_BUFFER_SIZE];
            bool otherShaReadable = !Update.isRunning() && getOtherPartitionSha256(otherSha, sizeof(otherSha));
            doc["canRollback"] = otherShaReadable;
            if (otherShaReadable) {
                doc["otherPartitionSha256"] = otherSha;
            } else {
                doc["otherPartitionSha256"] = nullptr;
            }
            
            _sendJsonResponse(request, doc);
        });
    }

    static void _serveOtaRollbackEndpoint()
    {
        server.on("/api/v1/ota/rollback", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (Update.isRunning()) {
                Update.abort();
                LOG_INFO("Aborted running OTA update");
                _stopOtaTimeoutTask(); // Stop timeout task when aborting OTA
            }

            // Act first, respond from the actual outcome. The old shape gated on
            // Update.canRollBack() (a one-byte flash[0]==0xE9 check, true even for
            // a half-downloaded image) and reported success before switching.
            LOG_WARNING("Firmware rollback requested via API");
            FirmwareRollbackResult result = attemptFirmwareRollback("Firmware rollback requested via API");

            switch (result) {
                case FirmwareRollbackResult::SUCCESS:
                    _sendSuccessResponse(request, "Rollback initiated. Device will restart.");
                    break;
                case FirmwareRollbackResult::INVALID_IMAGE:
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "No valid firmware in the other partition");
                    break;
                case FirmwareRollbackResult::RESTART_BLOCKED:
                    _sendErrorResponse(request, HTTP_CODE_LOCKED, "Restart currently blocked. Boot partition unchanged");
                    break;
            }
        });
    }

    static bool _fetchGitHubReleaseInfo(JsonDocument &doc)
    {
        // Check internet connectivity before attempting API call
        if (!CustomWifi::isFullyConnected(true)) {
            LOG_DEBUG("Cannot fetch GitHub release info: no internet connectivity");
            return false;
        }

        HTTPClient http;
        http.begin(GITHUB_API_LATEST_RELEASE_URL);
        http.addHeader("User-Agent", "EnergyMe-Home-ESP32");
        http.addHeader("Accept", "application/vnd.github.v3+json");

        // Reset watchdog before network call
        int httpCode = http.GET();

        if (httpCode != HTTP_CODE_OK) {
            LOG_WARNING("GitHub API request failed with code: %d", httpCode);
            http.end();
            return false;
        }

        // Parse GitHub API response - reset before and after data fetch
        String response = http.getString();
        http.end();


        // Create the JSON document for parsing the response (different from the function argument doc)
        SpiRamAllocator allocator;
        JsonDocument responseDoc(&allocator);

        DeserializationError error = deserializeJson(responseDoc, response);
        if (error) {
            LOG_WARNING("Failed to parse GitHub API response: %s", error.c_str());
            return false;
        }

        if (!responseDoc.is<JsonObject>()) {
            LOG_WARNING("Invalid GitHub API response: expected object");
            return false;
        }

        // /releases/latest returns the newest published release repo-wide and already
        // excludes drafts and prereleases, so no filtering loop is needed here.
        JsonObject release = responseDoc.as<JsonObject>();

        const char* tagName = release["tag_name"].as<const char*>();
        if (!tagName) {
            LOG_WARNING("No tag_name in GitHub latest release response");
            return false;
        }

        const char* releaseDate = release["published_at"].as<const char*>();
        const char* changelog = release["html_url"].as<const char*>();
        const char* downloadUrl = nullptr;

        for (JsonObject asset : release["assets"].as<JsonArray>()) {
            const char* name = asset["name"].as<const char*>();
            if (name && strstr(name, ".bin") != nullptr && strstr(name, "energyme_home") != nullptr) {
                downloadUrl = asset["browser_download_url"].as<const char*>();
                break;
            }
        }

        // Compare versions to determine if update is available
        bool isLatest = compareVersions(FIRMWARE_BUILD_VERSION, tagName) >= 0;
        doc["isLatest"] = isLatest;

        // Only populate update fields when a newer version is available
        if (!isLatest) {
            doc["availableVersion"] = tagName;
            if (releaseDate) doc["releaseDate"] = releaseDate;
            if (downloadUrl) doc["updateUrl"] = downloadUrl;
            if (changelog) doc["changelogUrl"] = changelog;
        }

        LOG_DEBUG("GitHub release info fetched: version=%s, isLatest=%s",
                 tagName, isLatest ? "true" : "false");

        return true;
    }

    static void _serveFirmwareStatusEndpoint()
    {
        // Get firmware update information
        server.on("/api/v1/firmware/update-info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Get current firmware info
            doc["currentVersion"] = FIRMWARE_BUILD_VERSION;
            doc["buildDate"] = FIRMWARE_BUILD_DATE;

            // In non-community mode, we assume updates are handled via app/cloud, so we consider it always up to date            
            if (!globalCommunityMode) doc["isLatest"] = true; 
            else if (CustomWifi::isFullyConnected(true)) { // Check internet connectivity before checking anything            
                // Fetch from GitHub API in community mode
                bool githubInfoFetched = _fetchGitHubReleaseInfo(doc);
                
                if (!githubInfoFetched) {
                    doc["isLatest"] = true; // Assume latest since we can't check
                    // Here we log the warning since internet should be present, but most likely GitHub/the logic broke
                    LOG_WARNING("Failed to fetch GitHub release info, assuming current version is latest");
                }
            } else {
                doc["isLatest"] = true; // Assume latest since we can't check
                LOG_DEBUG("No internet connectivity, cannot check for firmware updates"); // Only debug since internet may not be available
            }

            _sendJsonResponse(request, doc);
        });
    }

    // === SYSTEM MANAGEMENT ENDPOINTS ===
    static void _serveSystemEndpoints()
    {
        // System information
        server.on("/api/v1/system/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Get both static and dynamic info
            SpiRamAllocator allocatorStatic, allocatorDynamic;
            JsonDocument docStatic(&allocatorStatic);
            JsonDocument docDynamic(&allocatorDynamic);
            getJsonDeviceStaticInfo(docStatic);
            getJsonDeviceDynamicInfo(docDynamic);

            // Combine into a single response
            doc["static"] = docStatic;
            doc["dynamic"] = docDynamic;
            
            _sendJsonResponse(request, doc); });

        // Statistics
        server.on("/api/v1/system/statistics", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            statisticsToJson(statistics, doc);
            _sendJsonResponse(request, doc); });

        // Raw CPU ring samples - forensic timeline endpoint
        // Query params: lastSeconds (1..3600, default 600), core (0|1|both, default both)
        server.on("/api/v1/system/cpu/samples", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            uint32_t lastSeconds = TaskProfiler::CPU_DEFAULT_FETCH_LAST_SECONDS;
            if (request->hasParam("lastSeconds")) {
                long parsed = request->getParam("lastSeconds")->value().toInt();
                if (parsed <= 0) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "lastSeconds must be a positive integer");
                    return;
                }
                lastSeconds = (uint32_t)parsed;
                if (lastSeconds > TaskProfiler::CPU_RING_CAPACITY_SECONDS)
                    lastSeconds = TaskProfiler::CPU_RING_CAPACITY_SECONDS;
            }

            bool wantCore0 = true, wantCore1 = true;
            if (request->hasParam("core")) {
                const String& coreParam = request->getParam("core")->value();
                if (coreParam == "0")         { wantCore1 = false; }
                else if (coreParam == "1")    { wantCore0 = false; }
                else if (coreParam != "both") {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "core must be 0, 1, or both");
                    return;
                }
            }

            // Allocate temp buffer on heap (max 3600 entries x 8 B = 28.8 KB).
            // ESPAsyncWebServer dispatches handlers serially so the buffer never
            // overlaps with another invocation of this handler.
            size_t maxSamples = lastSeconds > 0 ? lastSeconds : TaskProfiler::CPU_RING_CAPACITY_SECONDS;
            TaskProfiler::CpuSample* buf = (TaskProfiler::CpuSample*)ps_malloc(maxSamples * sizeof(TaskProfiler::CpuSample));
            if (buf == nullptr) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Out of PSRAM");
                return;
            }

            size_t count = TaskProfiler::getRawSamples(buf, maxSamples, lastSeconds);

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            doc["intervalSeconds"] = 1;
            doc["fromEpoch"] = count > 0 ? buf[0].epochSecond : 0;
            doc["toEpoch"]   = count > 0 ? buf[count - 1].epochSecond : 0;
            doc["samplesReturned"] = count;

            if (wantCore0) {
                JsonArray a0 = doc["core0"].to<JsonArray>();
                for (size_t i = 0; i < count; i++) a0.add(buf[i].core0Percent);
            }
            if (wantCore1) {
                JsonArray a1 = doc["core1"].to<JsonArray>();
                for (size_t i = 0; i < count; i++) a1.add(buf[i].core1Percent);
            }

            free(buf);
            _sendJsonResponse(request, doc); });

        // System restart
        server.on("/api/v1/system/restart", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (setRestartSystem("System restart requested via API")) {
                _sendSuccessResponse(request, "System restart initiated");
            } else {
                _sendErrorResponse(request, HTTP_CODE_LOCKED, "Failed to initiate restart. Another restart may already be in progress or restart is currently locked");
            } });

        // Factory reset
        server.on("/api/v1/system/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            _sendSuccessResponse(request, "Factory reset initiated");
            setRestartSystem("Factory reset requested via API", true); });

        // Safe mode info
        server.on("/api/v1/system/safe-mode", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["active"] = CrashMonitor::isInSafeMode();
            doc["canRestartNow"] = CrashMonitor::canRestartNow();
            doc["minimumUptimeRemainingMs"] = CrashMonitor::getMinimumUptimeRemaining();
            if (CrashMonitor::isInSafeMode()) {
                doc["message"] = "Device in safe mode - restart protection active to prevent loops";
                doc["action"] = "Wait for minimum uptime or perform OTA update to fix underlying issue";
            }

            _sendJsonResponse(request, doc); });

        // Clear safe mode
        server.on("/api/v1/system/safe-mode/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (CrashMonitor::isInSafeMode()) {
                CrashMonitor::clearSafeModeCounters();
                _sendSuccessResponse(request, "Safe mode cleared. Device will restart.");
                setRestartSystem("Safe mode manually cleared via API");
            } else {
                _sendSuccessResponse(request, "Device is not in safe mode");
            } });

        // Check if secrets exist
        server.on("/api/v1/system/secrets", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["hasSecrets"] = !globalCommunityMode;
            _sendJsonResponse(request, doc); });

        // Get system time
        server.on("/api/v1/system/time", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["synced"] = CustomTime::isTimeSynched();
            doc["unixTime"] = CustomTime::getUnixTime();

            char isoBuffer[TIMESTAMP_BUFFER_SIZE];
            CustomTime::getTimestampIso(isoBuffer, sizeof(isoBuffer));
            doc["isoTime"] = isoBuffer;

            _sendJsonResponse(request, doc); });

        // Set system time (for devices without internet connectivity)
        server.on(
            "/api/v1/system/time",
            HTTP_POST,
            [](AsyncWebServerRequest *request) {},
            NULL,
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
                if (!_validateRequest(request, "POST")) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                DeserializationError error = deserializeJson(doc, data, len);

                if (error || !doc["unixTime"].is<uint64_t>())
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid JSON. Required: {\"unixTime\": <unix_seconds>}");
                    return;
                }

                uint64_t unixTime = doc["unixTime"].as<uint64_t>();
                if (CustomTime::setUnixTime(unixTime))
                {
                    SpiRamAllocator respAllocator;
                    JsonDocument respDoc(&respAllocator);
                    respDoc["success"] = true;
                    respDoc["message"] = "Time synchronized";

                    char isoBuffer[TIMESTAMP_BUFFER_SIZE];
                    CustomTime::getTimestampIso(isoBuffer, sizeof(isoBuffer));
                    respDoc["newTime"] = isoBuffer;

                    _sendJsonResponse(request, respDoc);
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Failed to set time. Value out of valid range.");
                }
            });
    }

    // === NETWORK MANAGEMENT ENDPOINTS ===
    // === ISSUE REGISTRY ENDPOINTS ===
    static void _serveIssueEndpoints()
    {
        // Currently visible device issues (issue #145)
        server.on("/api/v1/system/issues", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            if (!IssueRegistry::issuesToJson(doc)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve issues");
                return;
            }
            _sendJsonResponse(request, doc); });

        // Acknowledge issues: {"all": true} or {"code": "...", "channel": <optional>}
        static AsyncCallbackJsonWebHandler *ackIssueHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/system/issues/ack",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_ISSUES_ACK)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (doc["all"].is<bool>() && doc["all"].as<bool>()) {
                    uint32_t ackedCount = IssueRegistry::ackAll();
                    char message[STATUS_BUFFER_SIZE];
                    snprintf(message, sizeof(message), "Acknowledged %lu issue(s)", (unsigned long)ackedCount);
                    _sendSuccessResponse(request, message);
                    return;
                }

                if (!doc["code"].is<const char*>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'code' (or 'all') parameter");
                    return;
                }

                uint8_t channel = ISSUE_GLOBAL_SCOPE;
                if (doc["channel"].is<uint8_t>()) channel = doc["channel"].as<uint8_t>();

                if (IssueRegistry::ack(doc["code"].as<const char*>(), channel)) {
                    _sendSuccessResponse(request, "Issue acknowledged");
                } else {
                    _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No such issue instance");
                } });
        server.addHandler(ackIssueHandler);
    }

    // Captive-portal detection probes. Registered as explicit handlers rather than relying
    // on onNotFound, which ignores filters (so it could not be limited to the AP netif) but
    // still runs middleware (so it would demand auth on a probe that cannot authenticate).
    //
    // Answering with a redirect rather than the expected 204/success is what makes the OS
    // show its "sign in to network" sheet.
    static void _serveCaptivePortalProbes()
    {
        static const char *const kProbePaths[] = {
            "/generate_204",            // Android
            "/gen_204",                 // Android, older
            "/hotspot-detect.html",     // iOS / macOS
            "/library/test/success.html",
            "/ncsi.txt",                // Windows
            "/connecttest.txt",         // Windows 10+
            "/redirect",                // Windows, follow-up
        };

        for (size_t i = 0; i < sizeof(kProbePaths) / sizeof(kProbePaths[0]); i++) {
            _onOpenDuringSession(kProbePaths[i], HTTP_GET, [](AsyncWebServerRequest *request) {
                char location[IP_ADDRESS_BUFFER_SIZE + 16];
                snprintf(location, sizeof(location), "http://%s/", WiFi.softAPIP().toString().c_str());

                AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
                response->addHeader("Location", location);
                request->send(response);
            });
        }
    }

    static void _serveNetworkEndpoints()
    {
        _serveCaptivePortalProbes();

        // Provisioning status: what the device is doing, and where to reach it afterwards.
        // Open on the AP while unprovisioned because the setup page polls this before any
        // password could have been entered.
        _onOpenDuringSession("/api/v1/network/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["state"] = (uint8_t)CustomWifi::getProvisioningState();
            doc["connected"] = CustomWifi::isFullyConnected();
            doc["apServing"] = CustomWifi::isApServing();

            char ssid[WIFI_SSID_BUFFER_SIZE];
            CustomWifi::getStoredSsid(ssid, sizeof(ssid));
            doc["ssid"] = ssid;

            doc["ip"] = WiFi.localIP().toString();
            doc["apIp"] = WiFi.softAPIP().toString();
            doc["hostname"] = MDNS_HOSTNAME ".local";
            doc["rssi"] = WiFi.RSSI();

            // Identity, so the setup page can show which meter you are talking to before
            // any of the authenticated endpoints are reachable. Nothing sensitive: the
            // device id is already the SoftAP's SSID suffix.
            doc["deviceId"] = DEVICE_ID;
            doc["firmwareVersion"] = FIRMWARE_BUILD_VERSION;
            doc["uptime"] = millis64();

            // The credentials POST answers as soon as the request is queued, so its 200 says
            // nothing about whether the driver accepted the write. This is where the client
            // finds that out, which is why the POST's message points here.
            doc["credentialWriteFailed"] = CustomWifi::lastCredentialWriteFailed();

            _sendJsonResponse(request, doc);
        });

        // Network scan. Async and cached in customwifi; this just relays state so the client
        // can poll rather than hold a request open for the length of a scan.
        _onOpenDuringProvisioning("/api/v1/network/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            // ?refresh is the "Scan again" press, which must touch the radio rather than
            // re-serve the cached set. The client's polling requests deliberately omit it.
            CustomWifi::getScanResultsAsJson(doc, request->hasParam("refresh"));
            _sendJsonResponse(request, doc);
        });

        // Why the last association failed. Replaces the WiFiManager /diagnostic page (D11);
        // nothing else ever exposed these fields.
        _onOpenDuringProvisioning("/api/v1/network/wifi/diagnostics", HTTP_GET, [](AsyncWebServerRequest *request) {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            CustomWifi::getDisconnectDiagnosticsAsJson(doc);
            _sendJsonResponse(request, doc);
        });

        // WiFi reset
        server.on("/api/v1/network/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            _sendSuccessResponse(request, "WiFi credentials reset. Device will restart and enter configuration mode.");
            CustomWifi::resetWifi(); 
        });

        // Set WiFi credentials.
        //
        // Registered twice like the routes above, and this one is load-bearing: submitting
        // credentials is the ONE thing an unprovisioned user must be able to do, and they
        // cannot authenticate to do it. A single authenticated handler here would 401 the
        // whole provisioning flow at its last step.
        //
        // JSON body handlers are AsyncWebHandler subclasses, so they take the same filter
        // and middleware treatment; they just cannot go through the server.on() helper.
        ArJsonRequestHandlerFunction credentialsCallback =
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST")) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Validate required fields
                if (!doc["ssid"].is<const char*>())
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid 'ssid' field");
                    return;
                }

                if (!doc["password"].is<const char*>())
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'password' field");
                    return;
                }

                const char* ssid = doc["ssid"];
                const char* password = doc["password"];

                // Validate SSID length (1-31 characters)
                if (!isStringLengthValid(ssid, 1, WIFI_SSID_BUFFER_SIZE - 1))
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "SSID must be 1-31 characters");
                    return;
                }

                // Validate password length (0-63 characters)
                if (!isStringLengthValid(password, 0, WIFI_PASSWORD_BUFFER_SIZE - 1))
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Password must be 0-63 characters");
                    return;
                }

                LOG_INFO("Received request to set WiFi credentials for SSID: %s", ssid);

                if (CustomWifi::setCredentials(ssid, password)) _sendSuccessResponse(request, "WiFi credentials saved. Connecting to the new network without restarting - poll /api/v1/network/wifi/status for the result.");
                else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to save credentials for the specified network. Please verify them and try again.");
            };

        // Open twin first, so it wins by insertion order when the filter passes.
        AsyncCallbackJsonWebHandler *openCredentialsHandler =
            new AsyncCallbackJsonWebHandler("/api/v1/network/wifi/credentials", credentialsCallback);
        openCredentialsHandler->setFilter(_isProvisioningOrigin);
        openCredentialsHandler->skipServerMiddlewares();
        openCredentialsHandler->addMiddleware(&rateLimit);
        server.addHandler(openCredentialsHandler);

        AsyncCallbackJsonWebHandler *wifiCredentialsHandler =
            new AsyncCallbackJsonWebHandler("/api/v1/network/wifi/credentials", credentialsCallback);
        server.addHandler(wifiCredentialsHandler);

        // Get network configuration (static IP)
        server.on("/api/v1/network/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            if (CustomWifi::getConfigurationAsJson(doc)) _sendJsonResponse(request, doc);
            else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to get network configuration");
        });

        // Set network configuration (full PUT or partial PATCH). The device restarts to apply.
        static AsyncCallbackJsonWebHandler *setNetworkConfigHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/network/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_NETWORK)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (CustomWifi::setConfigurationFromJson(doc, isPartialUpdate)) {
                    LOG_INFO("Network configuration %s via API", isPartialUpdate ? "partially updated" : "updated");
                    _sendSuccessResponse(request, "Network configuration updated successfully. The device will restart to apply the new settings.");
                    setRestartSystem("Restart to apply new network configuration");
                } else {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid network configuration");
                }
            });
        server.addHandler(setNetworkConfigHandler);

        // Reset network configuration to defaults (DHCP)
        server.on("/api/v1/network/config/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (CustomWifi::resetConfiguration()) {
                _sendSuccessResponse(request, "Network configuration reset successfully. The device will restart to apply the defaults.");
                setRestartSystem("Restart to apply network configuration reset");
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to reset network configuration");
            }
        });
    }

    // === LOGGING ENDPOINTS ===
    static void _serveLoggingEndpoints()
    {
        // Get log levels
        server.on("/api/v1/logs/level", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["print"] = AdvancedLogger::logLevelToString(AdvancedLogger::getPrintLevel());
            doc["save"] = AdvancedLogger::logLevelToString(AdvancedLogger::getSaveLevel());
            _sendJsonResponse(request, doc);
        });

        // Set log levels (using AsyncCallbackJsonWebHandler for JSON body)
        static AsyncCallbackJsonWebHandler *setLogLevelHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/logs/level",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                const char *printLevel = doc["print"].as<const char *>();
                const char *saveLevel = doc["save"].as<const char *>();

                if (!printLevel && !saveLevel)
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "At least one of 'print' or 'save' level must be specified");
                    return;
                }

                char resultMsg[STATUS_BUFFER_SIZE];
                snprintf(resultMsg, sizeof(resultMsg), "Log levels %s:", isPartialUpdate ? "partially updated" : "updated");
                bool success = true;

                // Set print level if provided
                if (printLevel && success)
                {
                    LogLevel level;
                    if (_parseLogLevel(printLevel, level))
                    {
                        AdvancedLogger::setPrintLevel(level);
                        snprintf(resultMsg + strlen(resultMsg), sizeof(resultMsg) - strlen(resultMsg),
                                 " print=%s", printLevel);
                    }
                    else
                    {
                        success = false;
                    }
                }

                // Set save level if provided
                if (saveLevel && success)
                {
                    LogLevel level;
                    if (_parseLogLevel(saveLevel, level))
                    {
                        AdvancedLogger::setSaveLevel(level);
                        snprintf(resultMsg + strlen(resultMsg), sizeof(resultMsg) - strlen(resultMsg),
                                 " save=%s", saveLevel);
                    }
                    else
                    {
                        success = false;
                    }
                }

                if (success)
                {
                    _sendSuccessResponse(request, resultMsg);
                    LOG_INFO("Log levels %s via API", isPartialUpdate ? "partially updated" : "updated");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid log level specified. Valid levels: VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL");
                }
            });
        server.addHandler(setLogLevelHandler);

        // Get all logs
        server.on("/api/v1/logs", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            request->send(LittleFS, LOG_PATH, "text/plain");
        });

        // Clear logs
        server.on("/api/v1/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            AdvancedLogger::clearLog();
            _sendSuccessResponse(request, "Logs cleared successfully");
            LOG_INFO("Logs cleared via API");
        });

        // Get UDP log destination (cannot use logs/ as it interferes with the previous /logs endpoint)
        server.on("/api/v1/logs-udp-destination", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            char ipAddress[IP_ADDRESS_BUFFER_SIZE];
            
            if (CustomLog::getUdpDestination(ipAddress, sizeof(ipAddress))) {
                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc["destination"] = ipAddress;
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve UDP destination");
            }
        });

        // Set UDP log destination
        static AsyncCallbackJsonWebHandler *setUdpDestinationHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/logs-udp-destination",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_UDP_DESTINATIONS)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                const char* destination = doc["destination"].as<const char*>();
                if (!destination) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'destination' field");
                    return;
                }

                if (CustomLog::setUdpDestination(destination)) {
                    _sendSuccessResponse(request, "UDP destination updated successfully");
                    LOG_INFO("UDP destination updated via API: %s", destination);
                } else {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid IP address format");
                }
            });
        server.addHandler(setUdpDestinationHandler);
    }

#ifdef ENV_DEV
    // Dev-only: feed a synthetic shadow delta through the real inbound path
    // (routeMessage -> task drain -> ApplyFn -> ack publish) so the apply logic
    // can be exercised on hardware without a cloud desired-state writer. Never
    // compiled into the production firmware.
    // Body: {"name":"system","doc":{"version":1,"state":{"led_brightness":20}}}
    static void _serveShadowDevEndpoints() {
        static AsyncCallbackJsonWebHandler *injectShadowDeltaHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/shadow/inject-delta",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA * MAX_CHANNEL_COUNT)) return;

                const char *name = json["name"].as<const char *>();
                if (!name || json["doc"].isNull()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Body requires 'name' and 'doc'");
                    return;
                }

                SpiRamAllocator allocator;
                JsonDocument deltaDoc(&allocator);
                deltaDoc.set(json["doc"]);

                size_t len = measureJson(deltaDoc) + 1;
                char *payload = (char *)ps_malloc(len);
                if (!payload) {
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Allocation failed");
                    return;
                }
                serializeJson(deltaDoc, payload, len);

                bool ok = Shadow::injectDelta(name, payload);
                free(payload);

                if (ok) _sendSuccessResponse(request, "Synthetic shadow delta injected");
                else _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Unknown shadow name");
            });
        server.addHandler(injectShadowDeltaHandler);

        // Dev-only: inject a synthetic IoT Command execution through the real
        // handler (no cloud dispatcher needed). The device acts on the operation;
        // the response publish targets a synthetic execution id.
        // Body: {"executionId":"dev-1","payload":{"operation":"energy_reset","channels":[3]}}
        static AsyncCallbackJsonWebHandler *injectCommandHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/shadow/inject-command",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA)) return;

                const char *execId = json["executionId"].as<const char *>();
                if (!execId || json["payload"].isNull()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Body requires 'executionId' and 'payload'");
                    return;
                }

                SpiRamAllocator allocator;
                JsonDocument payloadDoc(&allocator);
                payloadDoc.set(json["payload"]);

                size_t len = measureJson(payloadDoc) + 1;
                char *payload = (char *)ps_malloc(len);
                if (!payload) {
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Allocation failed");
                    return;
                }
                serializeJson(payloadDoc, payload, len);

                Mqtt::injectCommandExecution(execId, payload);
                free(payload);
                _sendSuccessResponse(request, "Synthetic command injected");
            });
        server.addHandler(injectCommandHandler);

        LOG_DEBUG("Registered dev-only shadow delta + command injection endpoints");
    }

    static const char* _nvsTypeToString(nvs_type_t type) {
        switch (type) {
            case NVS_TYPE_U8:   return "u8";
            case NVS_TYPE_I8:   return "i8";
            case NVS_TYPE_U16:  return "u16";
            case NVS_TYPE_I16:  return "i16";
            case NVS_TYPE_U32:  return "u32";
            case NVS_TYPE_I32:  return "i32";
            case NVS_TYPE_U64:  return "u64";
            case NVS_TYPE_I64:  return "i64";
            case NVS_TYPE_STR:  return "str";
            case NVS_TYPE_BLOB: return "blob";
            default:            return "unknown";
        }
    }

    // Keys never returned by value (only presence/type) - private key material,
    // certs and anything whose name marks it as a credential. Matched on the key
    // name rather than an explicit allowlist so a newly added password key is
    // redacted by default instead of leaking until someone remembers it: the
    // endpoint is gated by the very admin password it would otherwise hand out.
    static bool _isNvsValueSensitive(const char* key) {
        static const char* const SENSITIVE_FRAGMENTS[] = {
            "pass", "pwd", "token", "secret", "key", "cert", "cred"
        };

        char lowered[NVS_NAME_BUFFER_SIZE];
        size_t i = 0;
        for (; key[i] != '\0' && i < sizeof(lowered) - 1; i++) {
            char c = key[i];
            lowered[i] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
        }
        lowered[i] = '\0';

        for (size_t f = 0; f < sizeof(SENSITIVE_FRAGMENTS) / sizeof(SENSITIVE_FRAGMENTS[0]); f++) {
            if (strstr(lowered, SENSITIVE_FRAGMENTS[f]) != nullptr) return true;
        }
        return false;
    }

    // Dev-only: generic NVS namespace/key browser and editor, for inspecting and
    // recovering bench devices without a full manufacturing reflash.
    // GET    /api/v1/debug/nvs/namespaces                 -> [{"namespace":"factory_ns","entryCount":3}, ...]
    // GET    /api/v1/debug/nvs/entries?namespace=X         -> [{"key":"pcb_revision","type":"str","value":"v5.0"}, ...]
    // POST   /api/v1/debug/nvs/entry {"namespace":"factory_ns","key":"pcb_revision","type":"string","value":"v5.0"}
    //        type: string (default) | i32 | u32 | i64 | u64 | float | bool
    // DELETE /api/v1/debug/nvs/entry?namespace=X&key=Y
    // DELETE /api/v1/debug/nvs/namespace?namespace=X       -> clears every key in the namespace
    static void _serveNvsDebugEndpoints() {
        server.on("/api/v1/debug/nvs/namespaces", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            char names[NVS_DEBUG_MAX_NAMESPACES][NVS_NAME_BUFFER_SIZE];
            uint32_t counts[NVS_DEBUG_MAX_NAMESPACES];
            size_t namespaceCount = 0;

            nvs_iterator_t it = nullptr;
            esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
            size_t iterations = 0;
            while (err == ESP_OK && it != nullptr && iterations < NVS_DEBUG_MAX_ENTRIES) {
                nvs_entry_info_t info;
                nvs_entry_info(it, &info);

                bool found = false;
                for (size_t i = 0; i < namespaceCount; i++) {
                    if (strcmp(names[i], info.namespace_name) == 0) {
                        counts[i]++;
                        found = true;
                        break;
                    }
                }
                if (!found && namespaceCount < NVS_DEBUG_MAX_NAMESPACES) {
                    snprintf(names[namespaceCount], NVS_NAME_BUFFER_SIZE, "%s", info.namespace_name);
                    counts[namespaceCount] = 1;
                    namespaceCount++;
                }

                err = nvs_entry_next(&it);
                iterations++;
            }
            nvs_release_iterator(it);

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            JsonArray arr = doc.to<JsonArray>();
            for (size_t i = 0; i < namespaceCount; i++) {
                JsonObject obj = arr.add<JsonObject>();
                obj["namespace"] = names[i];
                obj["entryCount"] = counts[i];
            }
            _sendJsonResponse(request, doc); });

        server.on("/api/v1/debug/nvs/entries", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("namespace")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'namespace' query parameter");
                return;
            }
            char targetNamespace[NVS_NAME_BUFFER_SIZE];
            snprintf(targetNamespace, sizeof(targetNamespace), "%s", request->getParam("namespace")->value().c_str());

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            JsonArray arr = doc.to<JsonArray>();

            nvs_iterator_t it = nullptr;
            esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, targetNamespace, NVS_TYPE_ANY, &it);
            size_t iterations = 0;
            while (err == ESP_OK && it != nullptr && iterations < NVS_DEBUG_MAX_ENTRIES) {
                nvs_entry_info_t info;
                nvs_entry_info(it, &info);

                JsonObject obj = arr.add<JsonObject>();
                obj["key"] = info.key;
                obj["type"] = _nvsTypeToString(info.type);

                if (_isNvsValueSensitive(info.key)) {
                    obj["value"] = "<redacted>";
                } else if (info.type == NVS_TYPE_STR) {
                    Preferences prefs;
                    if (prefs.begin(targetNamespace, true)) {
                        // getString() returns 0 without touching the buffer when
                        // the stored string is longer than it, so the buffer must
                        // be primed and the result checked - otherwise 512 bytes
                        // of uninitialised stack get serialised into the reply.
                        char value[NVS_DEBUG_STRING_VALUE_BUFFER_SIZE];
                        value[0] = '\0';
                        if (prefs.getString(info.key, value, sizeof(value)) > 0) {
                            obj["value"] = value;
                        } else {
                            obj["value"] = "<unreadable or too long>";
                        }
                        prefs.end();
                    }
                } else if (info.type == NVS_TYPE_I32) {
                    Preferences prefs;
                    if (prefs.begin(targetNamespace, true)) { obj["value"] = prefs.getInt(info.key); prefs.end(); }
                } else if (info.type == NVS_TYPE_U32) {
                    Preferences prefs;
                    if (prefs.begin(targetNamespace, true)) { obj["value"] = prefs.getUInt(info.key); prefs.end(); }
                } else if (info.type == NVS_TYPE_I64) {
                    Preferences prefs;
                    if (prefs.begin(targetNamespace, true)) { obj["value"] = prefs.getLong64(info.key); prefs.end(); }
                } else if (info.type == NVS_TYPE_U64) {
                    Preferences prefs;
                    if (prefs.begin(targetNamespace, true)) { obj["value"] = prefs.getULong64(info.key); prefs.end(); }
                }
                // Narrower int types and blobs: key/type only, value decode not worth the branches here.

                err = nvs_entry_next(&it);
                iterations++;
            }
            nvs_release_iterator(it);

            _sendJsonResponse(request, doc); });

        static AsyncCallbackJsonWebHandler *writeEntryHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/debug/nvs/entry",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_NVS_ENTRY)) return;

                const char *ns = json["namespace"].as<const char *>();
                const char *key = json["key"].as<const char *>();
                if (!ns || !key || json["value"].isNull()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Body requires 'namespace', 'key', 'value'");
                    return;
                }
                const char *type = json["type"].is<const char *>() ? json["type"].as<const char *>() : "string";

                Preferences prefs;
                if (!prefs.begin(ns, false)) {
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to open namespace");
                    return;
                }

                bool ok = true;
                if (strcmp(type, "string") == 0) {
                    ok = prefs.putString(key, json["value"].as<const char *>()) > 0;
                } else if (strcmp(type, "i32") == 0) {
                    ok = prefs.putInt(key, json["value"].as<int32_t>()) > 0;
                } else if (strcmp(type, "u32") == 0) {
                    ok = prefs.putUInt(key, json["value"].as<uint32_t>()) > 0;
                } else if (strcmp(type, "i64") == 0) {
                    ok = prefs.putLong64(key, json["value"].as<int64_t>()) > 0;
                } else if (strcmp(type, "u64") == 0) {
                    ok = prefs.putULong64(key, json["value"].as<uint64_t>()) > 0;
                } else if (strcmp(type, "float") == 0) {
                    ok = prefs.putFloat(key, json["value"].as<float>()) > 0;
                } else if (strcmp(type, "bool") == 0) {
                    ok = prefs.putBool(key, json["value"].as<bool>()) > 0;
                } else {
                    ok = false;
                }
                prefs.end();

                if (ok) {
                    LOG_WARNING("Dev endpoint: wrote NVS %s::%s (type=%s)", ns, key, type);
                    // This is the one writer of auth_ns that does not go through
                    // _setWebPassword, so the cached lockdown flag and the live digestAuth
                    // credential would both go stale until the next boot - silently
                    // disabling the lockdown on a device someone just set back to the
                    // default password.
                    if (strcmp(ns, PREFERENCES_NAMESPACE_AUTH) == 0) {
                        LOG_WARNING("Dev endpoint touched the auth namespace - reloading the web password");
                        updateAuthPasswordWithOneFromPreferences();
                    }
                    _sendSuccessResponse(request, "NVS entry written");
                } else {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Unknown type, or write failed");
                }
            });
        server.addHandler(writeEntryHandler);

        server.on("/api/v1/debug/nvs/entry", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "DELETE")) return;
            if (!request->hasParam("namespace") || !request->hasParam("key")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'namespace' or 'key' query parameter");
                return;
            }
            char ns[NVS_NAME_BUFFER_SIZE];
            char key[NVS_NAME_BUFFER_SIZE];
            snprintf(ns, sizeof(ns), "%s", request->getParam("namespace")->value().c_str());
            snprintf(key, sizeof(key), "%s", request->getParam("key")->value().c_str());

            Preferences prefs;
            if (!prefs.begin(ns, false)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to open namespace");
                return;
            }
            bool ok = prefs.remove(key);
            prefs.end();

            if (ok) {
                LOG_WARNING("Dev endpoint: removed NVS %s::%s", ns, key);
                // Same reason as the write path above.
                if (strcmp(ns, PREFERENCES_NAMESPACE_AUTH) == 0) {
                    LOG_WARNING("Dev endpoint removed an auth entry - reloading the web password");
                    updateAuthPasswordWithOneFromPreferences();
                }
                _sendSuccessResponse(request, "NVS entry removed");
            } else {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "Key not found");
            } });

        server.on("/api/v1/debug/nvs/namespace", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "DELETE")) return;
            if (!request->hasParam("namespace")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'namespace' query parameter");
                return;
            }
            char ns[NVS_NAME_BUFFER_SIZE];
            snprintf(ns, sizeof(ns), "%s", request->getParam("namespace")->value().c_str());

            Preferences prefs;
            if (!prefs.begin(ns, false)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to open namespace");
                return;
            }
            bool ok = prefs.clear();
            prefs.end();

            if (ok) {
                LOG_WARNING("Dev endpoint: cleared NVS namespace %s", ns);
                _sendSuccessResponse(request, "NVS namespace cleared");
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to clear namespace");
            } });

        LOG_DEBUG("Registered dev-only NVS management endpoints");
    }

    // noinline so an optimising build can't flatten the recursion into a loop.
    // Deliberately unbounded - the MAX_LOOP_ITERATIONS convention doesn't apply
    // here since running out of stack IS the point.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
    static void __attribute__((noinline)) _crashTestStackOverflow(uint32_t depth) {
        volatile uint8_t padding[512]; // Real per-frame stack consumption, not just a return address
        memset((void *)padding, (int)depth, sizeof(padding));
        _crashTestStackOverflow(depth + 1);
    }
#pragma GCC diagnostic pop

    // Dev-only: crash the device on purpose to exercise the archive and publish
    // pipeline on real hardware. Each route faults through a different mechanism
    // so the panic handler and backtrace unwinder are exercised more than one
    // way. No response is ever sent - the device resets before the handler can
    // return, so a client just sees the connection drop.
    static void _serveCrashTestEndpoints() {
        server.on("/api/v1/debug/crash/null-deref", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            LOG_WARNING("Debug endpoint: forcing a null pointer dereference");
            volatile uint32_t *nullPtr = nullptr;
            *nullPtr = 0xDEADBEEF;
        });

        server.on("/api/v1/debug/crash/abort", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            LOG_WARNING("Debug endpoint: forcing abort()");
            abort();
        });

        server.on("/api/v1/debug/crash/stack-overflow", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            LOG_WARNING("Debug endpoint: forcing a stack overflow");
            _crashTestStackOverflow(0);
        });

        LOG_DEBUG("Registered dev-only crash test endpoints: null-deref, abort, stack-overflow");
    }
#endif

    // === ADE7953 ENDPOINTS ===
    static void _serveAde7953Endpoints() {
        // === CONFIGURATION ENDPOINTS ===
        
        // Get ADE7953 configuration
        server.on("/api/v1/ade7953/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            Ade7953::getConfigurationAsJson(doc);
            
            _sendJsonResponse(request, doc);
        });

        // Set ADE7953 configuration (PUT/PATCH)
        static AsyncCallbackJsonWebHandler *setAde7953ConfigHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_CONFIG)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (Ade7953::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    LOG_INFO("ADE7953 configuration %s via API", isPartialUpdate ? "partially updated" : "updated");
                    _sendSuccessResponse(request, "ADE7953 configuration updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid ADE7953 configuration");
                }
            });
        server.addHandler(setAde7953ConfigHandler);

        // Reset ADE7953 configuration
        server.on("/api/v1/ade7953/config/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            Ade7953::resetConfiguration();
            _sendSuccessResponse(request, "ADE7953 configuration reset successfully");
        });

        // === SAMPLE TIME ENDPOINTS ===
        
        // Get sample time
        server.on("/api/v1/ade7953/sample-time", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["sampleTime"] = Ade7953::getSampleTime();
            
            _sendJsonResponse(request, doc);
        });

        // Set sample time
        static AsyncCallbackJsonWebHandler *setSampleTimeHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/sample-time",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_SAMPLE_TIME)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (!doc["sampleTime"].is<uint64_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "sampleTime field must be a positive integer");
                    return;
                }

                uint64_t sampleTime = doc["sampleTime"].as<uint64_t>();

                if (Ade7953::setSampleTime(sampleTime))
                {
                    char sampleTimeHuman[DURATION_FORMAT_BUFFER_SIZE];
                    DurationFormat::humanizeDuration(sampleTime, sampleTimeHuman, sizeof(sampleTimeHuman));
                    LOG_INFO("ADE7953 sample time updated to %s via API", sampleTimeHuman);
                    _sendSuccessResponse(request, "ADE7953 sample time updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid sample time value");
                }
            });
        server.addHandler(setSampleTimeHandler);

        // === CHANNEL DATA ENDPOINTS ===
        
        // Get single channel data
        server.on("/api/v1/ade7953/channel", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            if (request->hasParam("index")) {
                // Get single channel data
                uint8_t channelIndex = (uint8_t)(request->getParam("index")->value().toInt());
                if (!isChannelValid(channelIndex)) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                } else {
                    if (Ade7953::getChannelDataAsJson(doc, channelIndex)) _sendJsonResponse(request, doc);
                    else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching single channel data");
                }
            } else {
                // Get all channels data - with ETag caching
                uint32_t configHash = Ade7953::computeAllChannelDataHash();
                if (configHash == 0) {
                    // Error computing hash, send data without caching
                    if (Ade7953::getAllChannelDataAsJson(doc)) _sendJsonResponse(request, doc);
                    else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching all channels data");
                    return;
                }

                // Generate ETag
                char etag[16];
                snprintf(etag, sizeof(etag), "\"%08x\"", configHash); // It says the format is incorrect but it works in the end

                // Check If-None-Match header and send 304 if matched
                if (_checkEtagAndSend304(request, etag)) {
                    return;
                }

                // Data has changed or no cached version, send full response with ETag
                if (Ade7953::getAllChannelDataAsJson(doc)) {
                    AsyncResponseStream *response = request->beginResponseStream("application/json");
                    serializeJson(doc, *response);
                    _sendResponseWithEtag(request, response, etag);
                } else {
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching all channels data");
                }
            }
        });

        // Set single channel data (PUT/PATCH)
        static AsyncCallbackJsonWebHandler *setChannelDataHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/channel",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                bool roleChanged = false;
                if (Ade7953::setChannelDataFromJson(doc, isPartialUpdate, &roleChanged))
                {
                    uint8_t channelIndex = doc["index"].as<uint8_t>();
                    
                    if (!isChannelValid(channelIndex)) {
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                        return;
                    }
                    
                    LOG_INFO("ADE7953 channel %u data %s via API", channelIndex, isPartialUpdate ? "partially updated" : "updated");
                    if (roleChanged) {
                        Ade7953::resetChannelEnergyValues(channelIndex);
                        LOG_DEBUG("Auto-reset energy and cleared history for channel %u due to role change", channelIndex);
                    }
                    _sendSuccessResponse(request, "ADE7953 channel data updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid ADE7953 channel data");
                }
            });
        server.addHandler(setChannelDataHandler);

        // Set all channels data (PUT only - bulk update)
        static AsyncCallbackJsonWebHandler *setAllChannelsDataHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/channels",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA * MAX_CHANNEL_COUNT)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Validate that it's an array
                if (!doc.is<JsonArrayConst>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Request body must be an array of channel configurations");
                    return;
                }

                // Update all channels - let setChannelDataFromJson handle all validation
                SpiRamAllocator channelAllocator;
                JsonDocument channelDoc(&channelAllocator);
                for (JsonDocument channelDoc : doc.as<JsonArrayConst>()) {
                    bool roleChanged = false;
                    if (!Ade7953::setChannelDataFromJson(channelDoc, false, &roleChanged)) {
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel configuration in array");
                        return;
                    }
                    if (roleChanged) {
                        uint8_t idx = channelDoc["index"].as<uint8_t>();
                        Ade7953::resetChannelEnergyValues(idx);
                        LOG_INFO("Auto-reset energy and cleared history for channel %u due to role change", idx);
                    }
                }

                LOG_INFO("Bulk updated %u ADE7953 channels via API", doc.size());
                _sendSuccessResponse(request, "All channels updated successfully");
            });
        server.addHandler(setAllChannelsDataHandler);

        // Reset single channel data
        server.on("/api/v1/ade7953/channel/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;
            
            if (!request->hasParam("index")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing channel index parameter");
                return;
            }

            uint8_t channelIndex = (uint8_t)(request->getParam("index")->value().toInt());
            if (!isChannelValid(channelIndex)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                return;
            }
            if (!Ade7953::resetChannelData(channelIndex)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to reset channel data");
                return;
            }

            LOG_INFO("ADE7953 channel %u data reset via API", channelIndex);
            _sendSuccessResponse(request, "ADE7953 channel data reset successfully");
        });

        // === REGISTER ENDPOINTS ===
        
        // Read single register
        server.on("/api/v1/ade7953/register", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("address")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing register address parameter");
                return;
            }
            
            if (!request->hasParam("bits")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing register bits parameter");
                return;
            }

            int32_t addressValue = request->getParam("address")->value().toInt();
            int32_t bitsValue = request->getParam("bits")->value().toInt();

            if (!isValueInRange(addressValue, 0, (int32_t)UINT16_MAX)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register address out of range (0-65535)");
                return;
            }
            if (!isValueInRange(bitsValue, 0, (int32_t)UINT8_MAX)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register bits out of range (0-255)");
                return;
            }

            uint16_t address = (uint16_t)(addressValue);
            uint8_t bits = (uint8_t)(bitsValue);
            bool signedData = request->hasParam("signed") ? request->getParam("signed")->value().equals("true") : false;

            int32_t value = Ade7953::readRegister(address, bits, signedData);
            
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["address"] = address;
            doc["bits"] = bits;
            doc["signed"] = signedData;
            doc["value"] = value;
            
            _sendJsonResponse(request, doc);
        });

        // Write single register
        static AsyncCallbackJsonWebHandler *writeRegisterHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/register",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_REGISTER)) return;

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            doc.set(json);

            if (!doc["address"].is<int32_t>() || !doc["bits"].is<int32_t>() || !doc["value"].is<int32_t>()) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "address, bits, and value fields must be integers");
                return;
            }

            int32_t addressValue = doc["address"].as<int32_t>();
            int32_t bitsValue = doc["bits"].as<int32_t>();

            if (!isValueInRange(addressValue, 0, (int32_t)UINT16_MAX)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register address out of range (0-65535)");
                return;
            }
            if (!isValueInRange(bitsValue, 0, (int32_t)UINT8_MAX)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register bits out of range (0-255)");
                return;
            }

            uint16_t address = (uint16_t)(addressValue);
            uint8_t bits = (uint8_t)(bitsValue);
            int32_t value = doc["value"].as<int32_t>();

            Ade7953::writeRegister(address, bits, value);

            LOG_INFO("ADE7953 register 0x%X (%d bits) written with value 0x%X via API", address, bits, value);
            _sendSuccessResponse(request, "ADE7953 register written successfully");
            });
        server.addHandler(writeRegisterHandler);

        // === METER VALUES ENDPOINTS ===
        
        // Get meter values (all channels or single channel with optional index parameter)
        server.on("/api/v1/ade7953/meter-values", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            if (request->hasParam("index")) {
                // Get single channel meter values
                long indexValue = request->getParam("index")->value().toInt();
                uint8_t channelIndex = (uint8_t)(indexValue);
                if (!isChannelValid(channelIndex)) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                } else {
                    if (Ade7953::singleMeterValuesToJson(doc, channelIndex)) _sendJsonResponse(request, doc);
                    else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching single meter values");
                }
            } else {
                // Get all meter values
                if (Ade7953::fullMeterValuesToJson(doc)) _sendJsonResponse(request, doc);
                else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching all meter values");
            }
        });

        // === GRID FREQUENCY ENDPOINT ===
        
        // Get grid frequency
        server.on("/api/v1/ade7953/grid-frequency", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["gridFrequency"] = Ade7953::getGridFrequency();
            
            _sendJsonResponse(request, doc);
        });

        // === ENERGY VALUES ENDPOINTS ===

        // Reset energy values (all channels, or single channel with ?index=N)
        server.on("/api/v1/ade7953/energy/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (request->hasParam("index")) {
                uint8_t channelIndex = static_cast<uint8_t>(request->getParam("index")->value().toInt());
                if (!isChannelValid(channelIndex)) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                    return;
                }
                Ade7953::resetChannelEnergyValues(channelIndex);
                LOG_INFO("ADE7953 energy values reset for channel %u via API", channelIndex);
                _sendSuccessResponse(request, "ADE7953 energy values reset for channel");
            } else {
                Ade7953::resetEnergyValues();
                LOG_INFO("ADE7953 energy values reset via API");
                _sendSuccessResponse(request, "ADE7953 energy values reset successfully");
            }
        });

        // Set energy values for a specific channel
        static AsyncCallbackJsonWebHandler *setEnergyValuesHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/energy",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_ENERGY)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (!doc["channel"].is<uint8_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "channel field must be a positive integer");
                    return;
                }

                uint8_t channel = doc["channel"].as<uint8_t>();

                if (!isChannelValid(channel)) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                    return;
                }

                if (!doc["activeEnergyImported"].is<double>() ||
                    !doc["activeEnergyExported"].is<double>() ||
                    !doc["reactiveEnergyImported"].is<double>() ||
                    !doc["reactiveEnergyExported"].is<double>() ||
                    !doc["apparentEnergy"].is<double>()) 
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "All energy value fields must be present and of type double");
                    return;
                }

                double activeEnergyImported = doc["activeEnergyImported"].as<double>();
                double activeEnergyExported = doc["activeEnergyExported"].as<double>();
                double reactiveEnergyImported = doc["reactiveEnergyImported"].as<double>();
                double reactiveEnergyExported = doc["reactiveEnergyExported"].as<double>();
                double apparentEnergy = doc["apparentEnergy"].as<double>();

                if (Ade7953::setEnergyValues(channel, activeEnergyImported, activeEnergyExported, 
                                           reactiveEnergyImported, reactiveEnergyExported, apparentEnergy))
                {
                    LOG_INFO("ADE7953 energy values set for channel %lu via API", channel);
                    _sendSuccessResponse(request, "ADE7953 energy values updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid energy values or channel");
                }
            });
        server.addHandler(setEnergyValuesHandler);
    }
    
    // === CUSTOM MQTT ENDPOINTS ===
    static void _serveCustomMqttEndpoints()
    {
        server.on("/api/v1/custom-mqtt/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {            
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            if (CustomMqtt::getConfigurationAsJson(doc)) _sendJsonResponse(request, doc);
            else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to get Custom MQTT configuration");
        });

        static AsyncCallbackJsonWebHandler *setCustomMqttHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/custom-mqtt/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {                
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (CustomMqtt::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    LOG_INFO("Custom MQTT configuration %s via API", isPartialUpdate ? "partially updated" : "updated");
                    _sendSuccessResponse(request, "Custom MQTT configuration updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid Custom MQTT configuration");
                }
            });
        server.addHandler(setCustomMqttHandler);

        // Reset configuration
        server.on("/api/v1/custom-mqtt/config/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            CustomMqtt::resetConfiguration();
            _sendSuccessResponse(request, "Custom MQTT configuration reset successfully");
        });

        server.on("/api/v1/custom-mqtt/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Add runtime status information
            char statusBuffer[STATUS_BUFFER_SIZE];
            char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
            CustomMqtt::getRuntimeStatus(statusBuffer, sizeof(statusBuffer), timestampBuffer, sizeof(timestampBuffer));
            doc["status"] = statusBuffer;
            doc["statusTimestamp"] = timestampBuffer;
            
            _sendJsonResponse(request, doc);
        });

        // === WAVEFORM CAPTURE ENDPOINTS ===
        
        // Arm waveform capture for a channel
        static AsyncCallbackJsonWebHandler *armWaveformCaptureHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/waveform/arm",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
            if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_ADE7953_WAVEFORM_ARM)) return;

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            doc.set(json);

            if (!doc["channelIndex"].is<uint8_t>()) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid channelIndex");
                return;
            }

            uint8_t channelIndex = doc["channelIndex"].as<uint8_t>();

            if (!isChannelValid(channelIndex)) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel index");
                return;
            }

            Ade7953::startWaveformCapture(channelIndex);
            
            Ade7953::CaptureState state = Ade7953::getWaveformCaptureStatus();
            switch (state) {
                case Ade7953::CaptureState::IDLE:
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to arm waveform capture due to unknown error");
                    break;
                case Ade7953::CaptureState::ARMED:
                    LOG_DEBUG("Waveform capture armed via API for channel %u", channelIndex);
                    _sendSuccessResponse(request, "Waveform capture armed successfully");
                    break;
                case Ade7953::CaptureState::CAPTURING:
                    _sendErrorResponse(request, HTTP_CODE_CONFLICT, "Waveform capture already in progress");
                    break;
                case Ade7953::CaptureState::COMPLETE:
                    _sendErrorResponse(request, HTTP_CODE_CONFLICT, "Previous waveform capture complete. Please retrieve data before arming a new capture");
                    break;
                case Ade7953::CaptureState::ERROR:
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Waveform capture buffer allocation failed");
                    break;
                default:
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to arm waveform capture due to unknown error");
                    break;
            }
            return;
        });
        server.addHandler(armWaveformCaptureHandler);

        // Get waveform capture status
        server.on("/api/v1/ade7953/waveform/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            Ade7953::CaptureState state = Ade7953::getWaveformCaptureStatus();
            
            switch (state) {
                case Ade7953::CaptureState::IDLE:
                    doc["state"] = "idle";
                    break;
                case Ade7953::CaptureState::ARMED:
                    doc["state"] = "armed";
                    break;
                case Ade7953::CaptureState::CAPTURING:
                    doc["state"] = "capturing";
                    break;
                case Ade7953::CaptureState::COMPLETE:
                    doc["state"] = "complete";
                    break;
                case Ade7953::CaptureState::ERROR:
                    doc["state"] = "error";
                    break;
            }
            doc["channel"] = Ade7953::getWaveformCaptureChannel();

            LOG_DEBUG("Waveform capture status retrieved via API. Status for channel %u: %s",
                      doc["channel"].as<uint8_t>(),
                      doc["state"].as<const char *>());
            _sendJsonResponse(request, doc);
        });

        // Get waveform capture data
        server.on("/api/v1/ade7953/waveform/data", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            Ade7953::CaptureState state = Ade7953::getWaveformCaptureStatus();
            
            if (state != Ade7953::CaptureState::COMPLETE) {
                JsonDocument doc;
                doc["message"] = "Waveform capture data not available";
                _sendJsonResponse(request, doc, HTTP_CODE_NO_CONTENT);
                return;
            }

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            bool success = Ade7953::getWaveformCaptureAsJson(doc);
            
            if (!success) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve waveform data");
                return;
            }

            LOG_DEBUG("Waveform data retrieved via API");
            _sendJsonResponse(request, doc);
        });

        // Get cloud services status
        server.on("/api/v1/mqtt/cloud-services", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            doc["enabled"] = !globalCommunityMode && Mqtt::isCloudServicesEnabled();

            _sendJsonResponse(request, doc);
        });

        // Set cloud services status
        static AsyncCallbackJsonWebHandler *setCloudServicesHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/mqtt/cloud-services",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (globalCommunityMode) {
                    _sendErrorResponse(request, HTTP_CODE_FORBIDDEN, "Cloud services are not available in community mode");
                    return;
                }
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_MQTT_CLOUD_SERVICES)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Validate JSON structure
                if (!doc.is<JsonObject>() || !doc["enabled"].is<bool>())
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid JSON structure. Expected: {\"enabled\": true/false}");
                    return;
                }

                bool enabled = doc["enabled"];
                Mqtt::setCloudServicesEnabled(enabled);

                LOG_INFO("Cloud services %s via API", enabled ? "enabled" : "disabled");
                _sendSuccessResponse(request, enabled ? "Cloud services enabled successfully" : "Cloud services disabled successfully");
            });
        server.addHandler(setCloudServicesHandler);
    }

    // === INFLUXDB ENDPOINTS ===
    static void _serveInfluxDbEndpoints()
    {
        // Get InfluxDB configuration
        server.on("/api/v1/influxdb/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            if (InfluxDbClient::getConfigurationAsJson(doc)) _sendJsonResponse(request, doc);
            else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching InfluxDB configuration");
        });

        // Set InfluxDB configuration
        static AsyncCallbackJsonWebHandler *setInfluxDbHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/influxdb/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_INFLUXDB)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                if (InfluxDbClient::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    LOG_INFO("InfluxDB configuration updated via API");
                    _sendSuccessResponse(request, "InfluxDB configuration updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid InfluxDB configuration");
                }
            });
        server.addHandler(setInfluxDbHandler);

        // Reset configuration
        server.on("/api/v1/influxdb/config/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            InfluxDbClient::resetConfiguration();
            _sendSuccessResponse(request, "InfluxDB configuration reset successfully");
        });

        // Get InfluxDB status
        server.on("/api/v1/influxdb/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Add runtime status information
            char statusBuffer[STATUS_BUFFER_SIZE];
            char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
            InfluxDbClient::getRuntimeStatus(statusBuffer, sizeof(statusBuffer), timestampBuffer, sizeof(timestampBuffer));
            doc["status"] = statusBuffer;
            doc["statusTimestamp"] = timestampBuffer;
            
            _sendJsonResponse(request, doc);
        });
    }

    // === CRASH MONITOR ENDPOINTS ===
    // These read the on-flash archive, not the coredump partition: dumps are
    // copied off the partition on the boot they are detected.
    static void _serveCrashEndpoints()
    {
        server.on("/api/v1/crash/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            if (CrashMonitor::listArchivedCrashes(doc)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve crash information");
            }
        });

        // Serves the stored gzip bytes directly, with no JSON wrapper
        server.on("/api/v1/crash/dump", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
            bool found = false;

            if (request->hasParam("id")) {
                found = CrashMonitor::findArchivedCrashById(
                    request->getParam("id")->value().c_str(), baseName, sizeof(baseName));
            } else {
                found = CrashMonitor::getArchivedCrashAt(0, baseName, sizeof(baseName));
            }

            if (!found) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No matching core dump available");
                return;
            }

            char path[CRASH_ARCHIVE_PATH_BUFFER_SIZE];
            CrashMonitor::buildArchivedCrashDumpPath(baseName, path, sizeof(path));

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "application/gzip");
            if (!response) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to open the archived core dump");
                return;
            }

            // Deliberately no Content-Encoding: gzip here. The body IS a gzip
            // file, not a gzip-encoded representation of something else, and the
            // header would be sent regardless of Accept-Encoding - browsers would
            // inflate it and save the raw dump under a .gz name, while a client
            // that never asked for gzip (plain curl) would get a body it does not
            // decode. application/gzip + a .gz filename says exactly that.
            char disposition[CRASH_ARCHIVE_NAME_BUFFER_SIZE + 40];
            snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s%s\"", baseName, CRASH_ARCHIVE_DUMP_SUFFIX);
            response->addHeader("Content-Disposition", disposition);

            request->send(response);
        });

        server.on("/api/v1/crash/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            uint32_t removed = CrashMonitor::clearArchivedCrashes();
            if (removed == 0) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No archived crashes to clear");
                return;
            }

            LOG_INFO("Cleared %lu archived crash record(s) via API", removed);

            char message[STATUS_BUFFER_SIZE];
            snprintf(message, sizeof(message), "Cleared %lu archived crash record(s)", removed);
            _sendSuccessResponse(request, message);
        });

        // The item endpoint: /crash/info is the collection (list, no id), this is
        // one entry addressed by id - GET to read it, DELETE to remove it. Same
        // ?id= param as /crash/dump.
        server.on("/api/v1/crash/entry", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("id")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'id' query parameter");
                return;
            }

            char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
            if (!CrashMonitor::findArchivedCrashById(request->getParam("id")->value().c_str(), baseName, sizeof(baseName))) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No archived crash with that id");
                return;
            }

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            if (!CrashMonitor::getArchivedCrashMetadata(baseName, doc)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to read archived crash metadata");
                return;
            }

            _sendJsonResponse(request, doc);
        });

        server.on("/api/v1/crash/entry", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "DELETE")) return;

            if (!request->hasParam("id")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing 'id' query parameter");
                return;
            }

            char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
            if (!CrashMonitor::findArchivedCrashById(request->getParam("id")->value().c_str(), baseName, sizeof(baseName))) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No archived crash with that id");
                return;
            }

            if (!CrashMonitor::removeArchivedCrash(baseName)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to remove the archived crash");
                return;
            }

            LOG_INFO("Removed archived crash %s via API", baseName);
            _sendSuccessResponse(request, "Archived crash removed");
        });
    }

    // === LED ENDPOINTS ===

    // Reads one 0-255 channel out of the body. Rejects anything that is not an
    // integer in range, including a float that happens to land on a whole number -
    // an automation sending 255.0 has a bug worth surfacing.
    static bool _readColorChannel(AsyncWebServerRequest *request, const JsonDocument &doc,
                                  const char *key, uint8_t &out)
    {
        char errorMsg[STATUS_BUFFER_SIZE];

        if (!doc[key].is<int32_t>())
        {
            snprintf(errorMsg, sizeof(errorMsg), "Missing or invalid %s parameter", key);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, errorMsg);
            return false;
        }

        const int32_t value = doc[key].as<int32_t>();
        if (value < 0 || value > UINT8_MAX)
        {
            snprintf(errorMsg, sizeof(errorMsg), "Parameter %s out of range (0-%d)", key, UINT8_MAX);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, errorMsg);
            return false;
        }

        out = (uint8_t)value;
        return true;
    }

    // Reads an optional 0..maxValue integer, leaving `out` at its caller-supplied
    // default when the field is absent. Same shape as _readColorChannel above, for
    // the two fields on this endpoint that are optional rather than required.
    static bool _readOptionalRangedInt(AsyncWebServerRequest *request, const JsonDocument &doc,
                                       const char *key, int64_t maxValue, int64_t &out)
    {
        if (doc[key].isNull()) return true;

        char errorMsg[STATUS_BUFFER_SIZE];
        if (!doc[key].is<int64_t>() || doc[key].as<int64_t>() < 0 || doc[key].as<int64_t>() > maxValue)
        {
            snprintf(errorMsg, sizeof(errorMsg), "Invalid %s parameter", key);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, errorMsg);
            return false;
        }

        out = doc[key].as<int64_t>();
        return true;
    }

    static void _serveLedEndpoints()
    {
        // Current LED state: what is actually being shown, not what the user asked
        // for. An automation needs to see that a system indication has taken over.
        //
        // exact(), not the plain string: a matcher built from a string is
        // BackwardCompatible, which matches the path *and everything under it*, and
        // _attachHandler() takes the first match in registration order - so
        // "/api/v1/led" would otherwise swallow GET /api/v1/led/brightness.
        server.on(AsyncURIMatcher::exact("/api/v1/led"), HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            const Led::Snapshot state = Led::getState();
            if (!state.valid) {
                // Could not read the layer table. Reporting that as "off" would put a
                // wrong answer on the wire, which is worse than no answer.
                _sendErrorResponse(request, HTTP_CODE_SERVICE_UNAVAILABLE, "LED state temporarily unavailable");
                return;
            }

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);

            doc["pattern"] = LedState::patternName(state.active.pattern);
            doc["layer"] = state.active.any ? LedState::layerName(state.active.layer) : nullptr;

            JsonObject color = doc["color"].to<JsonObject>();
            color["red"] = state.active.color.red;
            color["green"] = state.active.color.green;
            color["blue"] = state.active.color.blue;

            if (state.active.indefinite) { doc["remaining_ms"] = nullptr; }
            else { doc["remaining_ms"] = state.active.remainingMs; }

            doc["is_lit"] = state.isLit;
            doc["brightness"] = state.brightness;

            _sendJsonResponse(request, doc);
        });

        // Set the user layer. It sits just above the ambient status layer, so it
        // replaces the healthy indication but is still overridden by anything
        // eventful, and it is deliberately not persisted: a device must not boot
        // into a colour that hides its own status.
        static AsyncCallbackJsonWebHandler *setLedColorHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/led/color",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_LED_COLOR)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Parsed before the channels: disco picks its own colours, so it is the
                // pattern that decides whether they are required at all.
                LedPattern pattern = LedPattern::SOLID;
                if (!doc["pattern"].isNull())
                {
                    if (!doc["pattern"].is<const char *>() ||
                        !LedState::patternFromName(doc["pattern"].as<const char *>(), pattern))
                    {
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Unknown pattern");
                        return;
                    }
                }
                const bool isDisco = pattern == LedPattern::DISCO;

                uint8_t red = 0, green = 0, blue = 0;
                if (!isDisco)
                {
                    if (!_readColorChannel(request, doc, "red", red)) return;
                    if (!_readColorChannel(request, doc, "green", green)) return;
                    if (!_readColorChannel(request, doc, "blue", blue)) return;
                }

                int64_t durationValue = 0; // Indefinite
                if (!_readOptionalRangedInt(request, doc, "duration_ms", INT64_MAX, durationValue)) return;
                uint64_t durationMs = (uint64_t)durationValue;

                // Disco always ends on its own. Over-long values are clamped rather
                // than rejected: no other pattern has an upper bound, so a 400 here
                // would surprise a caller who read the rest of the contract.
                if (isDisco)
                {
                    if (durationMs == 0) { durationMs = DISCO_DEFAULT_DURATION_MS; }
                    if (durationMs > DISCO_MAX_DURATION_MS) { durationMs = DISCO_MAX_DURATION_MS; }
                }

                // Absent seed means "surprise me", so two presses of the same button do
                // not replay the same sequence. esp_random() rather than millis(): two
                // requests in the same millisecond would otherwise get the same seed.
                int64_t seedValue = (int64_t)esp_random();
                if (!_readOptionalRangedInt(request, doc, "seed", (int64_t)UINT32_MAX, seedValue)) return;
                uint32_t seed = (uint32_t)seedValue;

                Led::setPattern(LedState::Layer::USER, pattern, Led::Color(red, green, blue), durationMs, seed);
                _sendSuccessResponse(request, "LED color updated successfully");
            });
        server.addHandler(setLedColorHandler);

        // Release the user layer, revealing whatever layer is occupied beneath it.
        server.on("/api/v1/led/color", HTTP_DELETE, [](AsyncWebServerRequest *request)
                  {
            // Not optional: _sendSuccessResponse() releases the API mutex, so a
            // handler that skips the acquire here gives back a lock it never took.
            if (!_validateRequest(request, "DELETE")) return;

            Led::clearLayer(LedState::Layer::USER);
            _sendSuccessResponse(request, "LED color cleared successfully");
        });

        // Get LED brightness
        server.on("/api/v1/led/brightness", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            doc["brightness"] = Led::getBrightness();
            doc["max_brightness"] = LED_MAX_BRIGHTNESS_PERCENT;
            _sendJsonResponse(request, doc);
        });

        // Set LED brightness
        static AsyncCallbackJsonWebHandler *setLedBrightnessHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/led/brightness",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_LED_BRIGHTNESS)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Check if brightness field is provided and is a number
                if (!doc["brightness"].is<uint8_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid brightness parameter");
                    return;
                }

                uint8_t brightness = doc["brightness"].as<uint8_t>();

                // Validate brightness range
                if (!Led::isBrightnessValid(brightness)) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Brightness value out of range");
                    return;
                }

                // Set the brightness
                Led::setBrightness(brightness);
                _sendSuccessResponse(request, "LED brightness updated successfully");
            });
        server.addHandler(setLedBrightnessHandler);

    }

    // === BACKUP ENDPOINTS ===
    static void _serveBackupEndpoints()
    {
        server.on("/api/v1/backup/configuration", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            // nvsDataToJson() resets watchdog periodically during iteration
            AsyncJsonResponse * response = new AsyncJsonResponse();
            JsonObject doc = response->getRoot().to<JsonObject>();

            if (!nvsDataToJson(doc)) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve NVS data");
                LOG_ERROR("Failed to retrieve NVS data for backup request via API");
                delete response;
                return;
            }

            // Add download headers
            char deviceId[DEVICE_ID_BUFFER_SIZE];
            getDeviceId(deviceId, sizeof(deviceId));
            char timestamp[TIMESTAMP_BUFFER_SIZE];
            CustomTime::getTimestampIso(timestamp, sizeof(timestamp));

            char filename[128];
            snprintf(filename, sizeof(filename), "attachment; filename=\"config_backup_%s_%s.json\"",
                     deviceId, timestamp);
            response->addHeader("Content-Disposition", filename);

            LOG_INFO("Configuration backup requested via API: config_backup_%s_%s.json", deviceId, timestamp);
            response->setLength();
            request->send(response);
        });

        // LittleFS filesystem backup (tar) - streams directly to HTTP response, no temp files
        server.on("/api/v1/backup/filesystem", HTTP_GET, [](AsyncWebServerRequest *request) {
            LOG_DEBUG("LittleFS streaming backup requested");

            // Start async TAR creation task
            RingBufferStream* stream = startStreamingBackup();
            if (!stream) {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to start backup stream");
                LOG_ERROR("Failed to start LittleFS backup stream via API");
                return;
            }

            // Set up chunked response that reads from RingBufferStream
            // Use simpler callback to avoid compiler memory issues
            AsyncWebServerResponse *response = request->beginChunkedResponse("application/x-tar",
                [stream](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                    // Feed watchdog on every callback to prevent timeout during semaphore waits
                    esp_task_wdt_reset();

                    if (stream->hasError()) {
                        delete stream;
                        return 0;
                    }
                    size_t bytesRead = stream->readBytes(buffer, maxLen);
                    if (bytesRead == 0) {
                        delete stream;
                    }
                    return bytesRead;
                }
            );

            // Add download headers with device ID and timestamp
            char deviceId[DEVICE_ID_BUFFER_SIZE];
            getDeviceId(deviceId, sizeof(deviceId));
            char timestamp[TIMESTAMP_BUFFER_SIZE];
            CustomTime::getTimestampIso(timestamp, sizeof(timestamp));

            char filename[128];
            snprintf(filename, sizeof(filename), "attachment; filename=\"littlefs_backup_%s_%s.tar\"",
                     deviceId, timestamp);
            response->addHeader("Content-Disposition", filename);

            LOG_INFO("LittleFS backup streaming started: littlefs_backup_%s_%s.tar", deviceId, timestamp);
            request->send(response);
        });
    }

    // === RESTORE ENDPOINTS ===
    static void _serveRestoreEndpoints()
    {
        // POST - Restore configuration from JSON backup file (multipart upload)
        server.on("/api/v1/restore/configuration", HTTP_POST,
            [](AsyncWebServerRequest *request) {
                // Final response after file upload completes
                if (request->_tempObject) {
                    bool* success = (bool*)request->_tempObject;
                    if (*success) {
                        _sendSuccessResponse(request, "Configuration restore initiated. Device will restart.");
                    } else {
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Failed to process backup file. Check logs");
                    }
                    delete success;
                    request->_tempObject = nullptr;
                }
            },
            [](AsyncWebServerRequest *request, const String& filename,
               size_t index, uint8_t *data, size_t len, bool final) {

                static File restoreFile;
                static String tempPath = "/restore/nvs_restore_upload.json";
                static bool isValid = true;
                static bool restoreInProgress = false;

                if (!index) {
                    // Before any file is opened. See _rejectUploadIfNotPermitted().
                    if (_rejectUploadIfNotPermitted(request)) return;

                    // First chunk - check if restore already in progress
                    if (restoreInProgress) {
                        LOG_WARNING("Configuration restore already in progress, rejecting new request");
                        _sendErrorResponse(request, HTTP_CODE_CONFLICT,
                            "Restore already in progress. Please wait for current restore to complete.");
                        return;
                    }
                    restoreInProgress = true;

                    // First chunk - create restore directory and file
                    LOG_INFO("Starting configuration restore upload");

                    if (!LittleFS.exists("/restore")) {
                        LittleFS.mkdir("/restore");
                    }

                    // Remove old temp file if exists
                    if (LittleFS.exists(tempPath)) {
                        LittleFS.remove(tempPath);
                    }

                    restoreFile = LittleFS.open(tempPath, FILE_WRITE);
                    if (!restoreFile) {
                        LOG_ERROR("Failed to create temp restore file");
                        isValid = false;
                        restoreInProgress = false;
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to create restore file");
                        return;
                    }
                    isValid = true;
                }

                // Write chunk
                if (len && isValid && restoreFile) {
                    size_t written = restoreFile.write(data, len);
                    if (written != len) {
                        LOG_ERROR("Failed to write restore file chunk");
                        isValid = false;
                        restoreFile.close();
                        LittleFS.remove(tempPath);
                        restoreInProgress = false;
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to write restore data");
                        return;
                    }
                }

                // Final chunk - validate and process JSON
                if (final && isValid && restoreFile) {
                    restoreInProgress = false;
                    restoreFile.close();
                    LOG_DEBUG("Backup file upload complete, validating...");

                    // Parse and validate JSON
                    File uploadedFile = LittleFS.open(tempPath, FILE_READ);
                    if (!uploadedFile) {
                        LOG_ERROR("Failed to read uploaded restore file");
                        isValid = false;
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to read restore file");
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    SpiRamAllocator allocator;
                    JsonDocument doc(&allocator);
                    DeserializationError jsonError = deserializeJson(doc, uploadedFile);
                    uploadedFile.close();

                    if (jsonError) {
                        LOG_ERROR("Invalid JSON in backup: %s", jsonError.c_str());
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid JSON format");
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    // Validate backup structure
                    if (!doc["version"].is<int>() || doc["version"] != 1 ||
                        !doc["type"].is<const char*>() || strcmp(doc["type"], "configuration") != 0 ||
                        !doc["nvs"].is<JsonObject>()) {
                        LOG_ERROR("Invalid backup format or version");
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid backup format");
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    // Check firmware version compatibility
                    if (!doc["firmwareVersion"].is<const char*>()) {
                        LOG_ERROR("Backup missing firmware version");
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Backup missing firmware version");
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    const char* backupFwVersion = doc["firmwareVersion"];
                    if (!isBackupVersionCompatible(backupFwVersion)) {
                        char msg[256];
                        snprintf(msg, sizeof(msg),
                            "Firmware version incompatible: backup from %s, current %s. "
                            "Can only restore backups from same major version <= current.",
                            backupFwVersion, FIRMWARE_BUILD_VERSION);
                        LOG_WARNING("%s", msg);
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, msg);
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    // Check device ID mismatch (warn but allow with ?force=true)
                    bool forceRestore = request->hasParam("force") &&
                                       request->getParam("force")->value() == "true";

                    const char* backupDeviceId = doc["deviceId"];
                    char currentDeviceId[DEVICE_ID_BUFFER_SIZE];
                    getDeviceId(currentDeviceId, sizeof(currentDeviceId));

                    if (backupDeviceId && strcmp(backupDeviceId, currentDeviceId) != 0 && !forceRestore) {
                        char msg[200];
                        snprintf(msg, sizeof(msg),
                            "Device ID mismatch: backup from %s, current device %s. Use ?force=true to override.",
                            backupDeviceId, currentDeviceId);
                        LOG_WARNING("%s", msg);
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, msg);
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    // Validate namespace exclusions (ensure no sensitive data)
                    const char* excludedNamespaces[] = {EXCLUDED_NVS_NAMESPACES_LIST};
                    for (JsonPair nsPair : doc["nvs"].as<JsonObject>()) {
                        for (const char* excluded : excludedNamespaces) {
                            if (strcmp(nsPair.key().c_str(), excluded) == 0) {
                                LOG_ERROR("Backup contains excluded namespace: %s", nsPair.key().c_str());
                                LittleFS.remove(tempPath);
                                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST,
                                    "Backup contains excluded namespace (security/device-specific data)");
                                bool* result = new bool(false);
                                request->_tempObject = result;
                                return;
                            }
                        }
                    }

                    // All validation passed - save for boot-time restore
                    File finalRestoreFile = LittleFS.open("/restore/nvs_restore.json", FILE_WRITE);
                    if (!finalRestoreFile) {
                        LOG_ERROR("Failed to create final restore file");
                        LittleFS.remove(tempPath);
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to save restore file");
                        bool* result = new bool(false);
                        request->_tempObject = result;
                        return;
                    }

                    serializeJson(doc, finalRestoreFile);
                    finalRestoreFile.close();
                    LittleFS.remove(tempPath);

                    // Set restore pending flag in NVS
                    Preferences prefs;
                    if (prefs.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
                        prefs.putBool("restore_pending", true);
                        prefs.end();
                    }

                    LOG_INFO("Configuration restore staged. Device will restart.");
                    bool* result = new bool(true);
                    request->_tempObject = result;

                    // Trigger restart after response sent
                    setRestartSystem("Configuration restore");
                }
            }
        );

        // POST - Restore filesystem from TAR file upload (saves to LittleFS then extracts)
        server.on("/api/v1/restore/filesystem", HTTP_POST,
            [](AsyncWebServerRequest *request) {
                // Final response after file upload completes
                if (request->_tempObject) {
                    bool* success = (bool*)request->_tempObject;
                    if (*success) {
                        _sendSuccessResponse(request, "Filesystem restored successfully");
                    } else {
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Filesystem restore failed");
                    }
                    delete success;
                    request->_tempObject = nullptr;
                }
            },
            [](AsyncWebServerRequest *request, const String& filename,
               size_t index, uint8_t *data, size_t len, bool final) {

                static File restoreFile;
                static String tempPath = "/restore/filesystem_restore.tar";
                static bool isValid = true;
                static bool restoreInProgress = false;

                if (!index) {
                    // Before any file is opened. See _rejectUploadIfNotPermitted().
                    if (_rejectUploadIfNotPermitted(request)) return;

                    // First chunk - check if restore already in progress
                    if (restoreInProgress) {
                        LOG_WARNING("Filesystem restore already in progress, rejecting new request");
                        _sendErrorResponse(request, HTTP_CODE_CONFLICT,
                            "Restore already in progress. Please wait for current restore to complete.");
                        return;
                    }
                    restoreInProgress = true;

                    // First chunk - create restore directory and file
                    LOG_INFO("Starting filesystem restore upload: %s", filename.c_str());

                    if (!LittleFS.exists("/restore")) {
                        LittleFS.mkdir("/restore");
                    }

                    // Remove old temp file if exists
                    if (LittleFS.exists(tempPath)) {
                        LittleFS.remove(tempPath);
                    }

                    restoreFile = LittleFS.open(tempPath, FILE_WRITE);
                    if (!restoreFile) {
                        LOG_ERROR("Failed to create temp restore file");
                        isValid = false;
                        restoreInProgress = false;
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to create restore file");
                        return;
                    }
                    isValid = true;
                }

                // Write chunk
                if (len && isValid && restoreFile) {
                    size_t written = restoreFile.write(data, len);
                    if (written != len) {
                        LOG_ERROR("Failed to write restore file chunk");
                        isValid = false;
                        restoreFile.close();
                        LittleFS.remove(tempPath);
                        restoreInProgress = false;
                        _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR,
                            "Failed to write restore data");
                        return;
                    }
                }

                // Final chunk - extract TAR to filesystem
                if (final && isValid && restoreFile) {
                    restoreInProgress = false;
                    restoreFile.close();
                    LOG_DEBUG("TAR file upload complete, extracting...");

                    bool extractSuccess = false;

                    TarUnpacker *tarUnpacker = new TarUnpacker();
                    tarUnpacker->haltOnError(true);
                    tarUnpacker->setTarVerify(true);

                    // Feed watchdog on each file extracted to prevent HTTP timeout during long extraction
                    tarUnpacker->setTarStatusProgressCallback([](const char* name, size_t size, size_t total_unpacked) {
                        esp_task_wdt_reset();
                        LOG_DEBUG("Extracting: %s (%zu bytes, total unpacked: %zu)", name, size, total_unpacked);
                    });

                    // Extract: sourceFS, sourcePath, destFS, destPath
                    if (tarUnpacker->tarExpander(LittleFS, tempPath.c_str(), LittleFS, "/")) {
                        LOG_INFO("Filesystem restore extraction successful");
                        extractSuccess = true;
                    } else {
                        LOG_ERROR("TAR extraction failed with error code: %d", tarUnpacker->tarGzGetError());
                    }

                    delete tarUnpacker;

                    // Clean up temp file
                    LittleFS.remove(tempPath);

                    // Store result for completion handler
                    bool* result = new bool(extractSuccess);
                    request->_tempObject = result;
                }
            }
        );
    }

    // === FILE OPERATION ENDPOINTS ===
    static void _serveFileEndpoints()
    {
        // List files in LittleFS. The endpoint cannot be only "files" as it conflicts with the file serving endpoint (defined below)
        // Optional query parameter: folder (e.g., /api/v1/list-files?folder=energy/daily)
        server.on("/api/v1/list-files", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Check for optional folder parameter
            const char* folderPath = nullptr;
            if (request->hasParam("folder")) {
                folderPath = request->getParam("folder")->value().c_str();
            }
            
            if (listLittleFsFiles(doc, folderPath)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to list LittleFS files");
            }
        });

        // GET - Download file from LittleFS
        server.on("/api/v1/files/*", HTTP_GET, [](AsyncWebServerRequest *request)
        {
            String url = request->url();
            String filename = url.substring(url.indexOf("/api/v1/files/") + 14);
            
            if (filename.length() == 0) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File path cannot be empty");
                return;
            }
            
            // URL decode the filename
            filename.replace("%2F", "/");
            filename.replace("%2f", "/");
            
            // Ensure filename starts with "/"
            if (!filename.startsWith("/")) {
                filename = "/" + filename;
            }

            // Check if file exists
            if (!LittleFS.exists(filename)) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "File not found");
                return;
            }

            // Determine content type
            const char* contentType = getContentTypeFromFilename(filename.c_str());

            // Check if download is forced
            bool forceDownload = request->hasParam("download");

            // Determine if this file should use caching
            bool shouldCache = filename.endsWith(".csv") || 
                            filename.endsWith(".csv.gz") ||
                            filename.startsWith("/energy/monthly/") ||
                            filename.startsWith("/energy/yearly/");
            
            if (shouldCache) {
                // Send with ETag caching
                _sendFileWithEtag(request, filename.c_str(), contentType, forceDownload);
            } else {
                // Send directly without caching (for frequently changing files)
                request->send(LittleFS, filename, contentType, forceDownload);
            }
        });


        // POST - Upload file to LittleFS
        server.on("/api/v1/files/*", HTTP_POST, 
            [](AsyncWebServerRequest *request) {
                // Final response is handled in _handleFileUploadData
            },
            [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
                _handleFileUploadData(request, filename, index, data, len, final);
            }
        );

        // DELETE - Remove file from LittleFS
        // HACK: using POST with JSON body to avoid wildcard DELETE issues with AsyncWebServer, and also not using the same endpoint as the * would catch files/delete
        static AsyncCallbackJsonWebHandler *deleteFileHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/delete-file",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "POST", HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT)) return;

                SpiRamAllocator allocator;
                JsonDocument doc(&allocator);
                doc.set(json);

                // Validate path field
                if (!doc["path"].is<const char*>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid 'path' field in JSON body");
                    return;
                }

                String filename = doc["path"].as<const char*>();
                
                if (filename.length() == 0) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File path cannot be empty");
                    return;
                }
                
                // Ensure filename starts with "/"
                if (!filename.startsWith("/")) {
                    filename = "/" + filename;
                }

                // Check if file exists
                if (!LittleFS.exists(filename)) {
                    LOG_DEBUG("Tried to delete non-existent file: %s", filename.c_str());
                    char buffer[NAME_BUFFER_SIZE];
                    snprintf(buffer, sizeof(buffer), "File not found: %s", filename.c_str());
                    _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, buffer);
                    return;
                }

                // Attempt to delete the file
                if (LittleFS.remove(filename)) {
                    LOG_INFO("File deleted successfully: %s", filename.c_str());
                    _sendSuccessResponse(request, "File deleted successfully");
                } else {
                    LOG_ERROR("Failed to delete file: %s", filename.c_str());
                    _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to delete file");
                }
            });
        server.addHandler(deleteFileHandler);
    }

    TaskInfo getHealthCheckTaskInfo()
    {
        return getTaskInfoSafely(_healthCheckTaskHandle, HEALTH_CHECK_TASK_STACK_SIZE, &_healthCheckHeartbeat);
    }

    TaskInfo getOtaTimeoutTaskInfo()
    {
        return getTaskInfoSafely(_otaTimeoutTaskHandle, OTA_TIMEOUT_TASK_STACK_SIZE);
    }
}