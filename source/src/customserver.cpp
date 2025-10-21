#include "customserver.h"

namespace CustomServer
{
    // Private variables
    // ==============================
    // ==============================

    static AsyncWebServer server(WEBSERVER_PORT);
    static AsyncAuthenticationMiddleware digestAuth;
    static AsyncRateLimitMiddleware rateLimit;
    static CustomMiddleware customMiddleware;

    // Health check task variables
    static TaskHandle_t _healthCheckTaskHandle = NULL;
    static bool _healthCheckTaskShouldRun = false;
    static uint32_t _consecutiveFailures = 0;

    // OTA timeout task variables
    static TaskHandle_t _otaTimeoutTaskHandle = NULL;
    static bool _otaTimeoutTaskShouldRun = false;

    // API request synchronization
    static SemaphoreHandle_t _apiMutex = NULL;

    // Private functions declarations
    // ==============================
    // ==============================

    // Handlers and middlewares
    static void _setupMiddleware();
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
    static void _serveFileEndpoints();
    
    // Authentication endpoints
    static void _serveAuthStatusEndpoint();
    static void _serveChangePasswordEndpoint();
    static void _serveResetPasswordEndpoint();
    
    // OTA endpoints
    static void _serveOtaUploadEndpoint();
    static void _serveOtaStatusEndpoint();
    static void _serveOtaRollbackEndpoint();
    #ifndef HAS_SECRETS
    static bool _fetchGitHubReleaseInfo(JsonDocument &doc);
    static int _compareVersions(const char* current, const char* available);
    #endif
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
    static void _restoreTaskWatchdog();
    
    // Logging helper functions
    static bool _parseLogLevel(const char *levelStr, LogLevel &level);
    
    // HTTP method validation helper
    static bool _validateRequest(AsyncWebServerRequest *request, const char *expectedMethod, size_t maxContentLength = 0);
    static bool _isPartialUpdate(AsyncWebServerRequest *request);
    
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
        
        LOG_INFO("Web server stopped");
    }

    void updateAuthPasswordWithOneFromPreferences()
    {
        char webPassword[PASSWORD_BUFFER_SIZE];
        if (_getWebPasswordFromPreferences(webPassword, sizeof(webPassword)))
        {
            digestAuth.setPassword(webPassword);
            digestAuth.generateHash(); // regenerate hash with new password
            LOG_INFO("Authentication password updated");
        }
        else
        {
            LOG_ERROR("Failed to load new password for authentication");
        }
    }

    bool resetWebPassword()
    {
        LOG_INFO("Resetting web password to default");
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
        char webPassword[PASSWORD_BUFFER_SIZE];
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

        server.addMiddleware(&digestAuth);

        LOG_DEBUG("Digest authentication configured");

        // ---- Rate Limiting Middleware Setup ----
        // Set rate limiting to prevent abuse
        rateLimit.setMaxRequests(WEBSERVER_MAX_REQUESTS);
        rateLimit.setWindowSize(WEBSERVER_WINDOW_SIZE_SECONDS);

        server.addMiddleware(&rateLimit);

        LOG_DEBUG("Rate limiting configured: max requests = %d, window size = %d seconds", WEBSERVER_MAX_REQUESTS, WEBSERVER_WINDOW_SIZE_SECONDS);

        LOG_DEBUG("Logging middleware configured");
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

        LOG_DEBUG("Starting health check task with %d bytes stack in internal RAM (performs TCP network operations)", HEALTH_CHECK_TASK_STACK_SIZE);
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

        LOG_DEBUG("Starting OTA timeout task with %d bytes stack in internal RAM (uses flash I/O)", OTA_TIMEOUT_TASK_STACK_SIZE);

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
        // Check if WiFi is connected
        if (!CustomWifi::isFullyConnected())
        {
            LOG_DEBUG("Health check: WiFi not connected");
            return false;
        }

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
            LOG_WARNING("Password too short");
            return false;
        }

        // Check maximum length
        if (length > MAX_PASSWORD_LENGTH)
        {
            LOG_WARNING("Password too int32_t");
            return false;
        }
        
        return true;
    }

    static void _serveApi()
    {
        // Group endpoints by functionality
        _serveSystemEndpoints();
        _serveNetworkEndpoints();
        _serveLoggingEndpoints();
        _serveHealthEndpoints();
        _serveAuthEndpoints();
        _serveOtaEndpoints();
        _serveAde7953Endpoints();
        _serveCustomMqttEndpoints();
        _serveInfluxDbEndpoints();
        _serveCrashEndpoints();
        _serveLedEndpoints();
        _serveFileEndpoints();
    }

    static void _serveStaticContent()
    {
        // === STATIC CONTENT (no auth required) ===

        // CSS files
        server.on("/css/styles.css", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/css", styles_css); });
        server.on("/css/button.css", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/css", button_css); });
        server.on("/css/section.css", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/css", section_css); });
        server.on("/css/typography.css", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/css", typography_css); });

        // JavaScript files
        server.on("/js/api-client.js", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "application/javascript", api_client_js); });

        // Resources
        server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "image/svg+xml", favicon_svg); });

        // Main dashboard
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", index_html); });

        // Configuration pages
        server.on("/ade7953-tester", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", ade7953_tester_html); });
        server.on("/configuration", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", configuration_html); });
        server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", calibration_html); });
        server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", channel_html); });
        server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", info_html); });
        server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", log_html); });
        server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", update_html); });

        // Swagger UI
        server.on("/swagger-ui", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", swagger_ui_html); });
        server.on("/swagger.yaml", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/yaml", swagger_yaml); });
    }

    // === HEALTH ENDPOINTS ===
    static void _serveHealthEndpoints()
    {
        server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            
            SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
            doc["status"] = "ok";
            doc["uptime"] = millis64();
            char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
            CustomTime::getTimestampIso(timestamp, sizeof(timestamp));
            doc["timestamp"] = timestamp;
            
            serializeJson(doc, *response);
            request->send(response); 
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
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            // Check if using default password
            char currentPassword[PASSWORD_BUFFER_SIZE];
            bool isDefault = true;
            if (_getWebPasswordFromPreferences(currentPassword, sizeof(currentPassword))) {
                isDefault = (strcmp(currentPassword, WEBSERVER_DEFAULT_PASSWORD) == 0);
            }
            
            doc["usingDefaultPassword"] = isDefault;
            doc["username"] = WEBSERVER_DEFAULT_USERNAME;
            
            serializeJson(doc, *response);
            request->send(response); 
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
                char storedPassword[PASSWORD_BUFFER_SIZE];
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

                // Validate and save new password
                if (!_setWebPassword(newPassword))
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "New password does not meet requirements or failed to save");
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
        
        // Re-initialize task watchdog after OTA
        _restoreTaskWatchdog();
        
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
            
            Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
            setRestartSystem("Restart needed after firmware update");
        }
    }

    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename, 
                                   size_t index, uint8_t *data, size_t len, bool final)
    {
        static bool otaInitialized = false;
        
        if (!index) {
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

    static void _restoreTaskWatchdog()
    {
        // Re-initialize task watchdog after OTA (it was suspended during OTA flash operations)
        LOG_DEBUG("Re-initializing task watchdog after OTA");
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000,
            .idle_core_mask = 0b11, // both cores are enabled (enable by setting the bit of the i core to 1)
            .trigger_panic = true
        };
        
        // Try to reconfigure first (in case deinit didn't fully stop it)
        esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
        if (err == ESP_ERR_INVALID_STATE) {
            // Watchdog not initialized, initialize it fresh
            LOG_DEBUG("Watchdog not active, initializing fresh");
            err = esp_task_wdt_init(&wdt_config);
        }
        
        if (err != ESP_OK) {
            LOG_ERROR("Failed to restore task watchdog: %s", esp_err_to_name(err));
        } else {
            LOG_DEBUG("Task watchdog restored successfully");
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
        
        // Suspend task watchdog to prevent timeout during flash operations
        // Flash writes disable interrupts/cache which prevents IDLE task from feeding watchdog
        LOG_DEBUG("Suspending task watchdog for OTA flash operations");
        esp_task_wdt_deinit(); // Deinitialize watchdog - will be re-initialized after OTA
        
        // Begin OTA update with known size
        if (!Update.begin(contentLength, U_FLASH)) {
            LOG_ERROR("Failed to begin OTA update: %s", Update.errorString());
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Failed to begin update");
            Led::doubleBlinkYellow(Led::PRIO_URGENT, 1000ULL);
            _stopOtaTimeoutTask(); // Stop timeout task on failure
            _restoreTaskWatchdog(); // Restore watchdog on failure
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
        size_t written = Update.write(data, len);
        if (written != len) {
            LOG_ERROR("OTA write failed: expected %zu bytes, wrote %zu bytes", len, written);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Write failed");
            Update.abort();
            _stopOtaTimeoutTask(); // Stop timeout task on write failure
            _restoreTaskWatchdog(); // Restore watchdog on failure
            return false;
        }
        
        // Log progress periodically
        static size_t lastProgressIndex = 0;
        if (index >= lastProgressIndex + SIZE_REPORT_UPDATE_OTA || index == 0) {
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
        
        if (!Update.end(true)) {
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
            doc["canRollback"] = Update.canRollBack();
            
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
            
            _sendJsonResponse(request, doc);
        });
    }

    static void _serveOtaRollbackEndpoint()
    {
        server.on("/api/v1/ota/rollback", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (Update.isRunning()) {
                Update.abort();
                LOG_INFO("Aborted running OTA update");
                _stopOtaTimeoutTask(); // Stop timeout task when aborting OTA
            }

            if (Update.canRollBack()) {
                LOG_WARNING("Firmware rollback requested via API");
                _sendSuccessResponse(request, "Rollback initiated. Device will restart.");
                
                Update.rollBack();
                setRestartSystem("Firmware rollback requested via API");
            } else {
                LOG_ERROR("Rollback not possible: %s", Update.errorString());
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Rollback not possible");
            }
        });
    }

    #ifndef HAS_SECRETS
    static bool _fetchGitHubReleaseInfo(JsonDocument &doc) // Used only if no secrets are compiled
    {
        HTTPClient http;
        http.begin(GITHUB_API_RELEASES_URL);
        http.addHeader("User-Agent", "EnergyMe-Home-ESP32");
        http.addHeader("Accept", "application/vnd.github.v3+json");
        
        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            LOG_ERROR("GitHub API request failed with code: %d", httpCode);
            http.end();
            return false;
        }
        
        // Parse GitHub API response
        String response = http.getString();
        http.end();

        DeserializationError error = deserializeJson(doc, response);
        if (error) {
            LOG_ERROR("Failed to parse GitHub API response: %s", error.c_str());
            return false;
        }
        
        // Extract release information
        if (!doc["tag_name"].is<const char*>()) {
            LOG_ERROR("Invalid GitHub API response: missing tag_name");
            return false;
        }
        
        const char* tagName = doc["tag_name"];
        const char* releaseDate = doc["published_at"].as<const char*>();
        const char* changelog = doc["html_url"].as<const char*>();
        
        // Find .bin asset
        JsonArray assets = doc["assets"];
        const char* downloadUrl = nullptr;
        const char* md5Hash = nullptr;
        
        for (JsonObject asset : assets) {
            const char* name = asset["name"];
            if (name && strstr(name, ".bin") != nullptr) {
                downloadUrl = asset["browser_download_url"];
                break;
            }
        }
        
        // Set response fields
        doc["availableVersion"] = tagName;
        if (releaseDate) doc["releaseDate"] = releaseDate;
        if (downloadUrl) doc["updateUrl"] = downloadUrl;
        if (changelog) doc["changelogUrl"] = changelog;
        if (md5Hash) doc["md5"] = md5Hash;
        
        // Compare versions to determine if update is available
        doc["isLatest"] = _compareVersions(FIRMWARE_BUILD_VERSION, tagName) >= 0;
        
        LOG_DEBUG("GitHub release info fetched: version=%s, isLatest=%s", 
                 tagName, doc["isLatest"].as<bool>() ? "true" : "false");
        
        return true;
    }

    static int _compareVersions(const char* current, const char* available)
    {
        // Parse version strings (assuming semantic versioning: x.y.z)
        int currentMajor = 0, currentMinor = 0, currentPatch = 0;
        int availableMajor = 0, availableMinor = 0, availablePatch = 0;
        
        // Remove 'v' prefix if present
        const char* currentStr = (current[0] == 'v') ? current + 1 : current;
        const char* availableStr = (available[0] == 'v') ? available + 1 : available;
        
        sscanf(currentStr, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);
        sscanf(availableStr, "%d.%d.%d", &availableMajor, &availableMinor, &availablePatch);
        
        // Compare versions
        if (currentMajor != availableMajor) return currentMajor - availableMajor;
        if (currentMinor != availableMinor) return currentMinor - availableMinor;
        return currentPatch - availablePatch;
    }
    #endif

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
            doc["buildTime"] = FIRMWARE_BUILD_TIME;
            
            #ifdef HAS_SECRETS
            doc["isLatest"] = true; // TODO: actually implement a notification system on the device (maybe..)
            #else
            // Fetch from GitHub API when no secrets are available
            if (!_fetchGitHubReleaseInfo(doc)) {
                // If GitHub fetch fails, just return current version info
                doc["isLatest"] = true; // Assume latest if we can't check
                LOG_WARNING("Failed to fetch GitHub release info, assuming current version is latest");
            }
            #endif

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

            #ifdef HAS_SECRETS // Like this it returns true or false, otherwise it returns 1 or 0
            doc["hasSecrets"] = true;
            #else
            doc["hasSecrets"] = false;
            #endif
            _sendJsonResponse(request, doc); });
    }

    // === NETWORK MANAGEMENT ENDPOINTS ===
    static void _serveNetworkEndpoints()
    {
        // WiFi reset
        server.on("/api/v1/network/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            _sendSuccessResponse(request, "WiFi credentials reset. Device will restart and enter configuration mode.");
            CustomWifi::resetWifi(); });
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
    }

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
                    LOG_INFO("ADE7953 sample time updated to %lu ms via API", sampleTime);
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
                long indexValue = request->getParam("index")->value().toInt();
                if (indexValue < 0 || indexValue > UINT8_MAX) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Channel index out of range (0-255)");
                } else {
                    uint8_t channelIndex = (uint8_t)(indexValue);
                    if (Ade7953::getChannelDataAsJson(doc, channelIndex)) _sendJsonResponse(request, doc);
                    else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching single channel data");
                }
            } else {
                // Get all channels data
                if (Ade7953::getAllChannelDataAsJson(doc)) _sendJsonResponse(request, doc);
                else _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Error fetching all channels data");
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

                if (Ade7953::setChannelDataFromJson(doc, isPartialUpdate))
                {
                    uint32_t channelIndex = doc["index"].as<uint32_t>();
                    LOG_INFO("ADE7953 channel %lu data %s via API", channelIndex, isPartialUpdate ? "partially updated" : "updated");
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
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA * CHANNEL_COUNT)) return;

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

                    if (!Ade7953::setChannelDataFromJson(channelDoc, false)) {
                        _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid channel configuration in array");
                        return;
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

            long indexValue = request->getParam("index")->value().toInt();
            if (indexValue < 0 || indexValue > UINT8_MAX) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Channel index out of range (0-255)");
                return;
            }
            uint8_t channelIndex = (uint8_t)(indexValue);
            Ade7953::resetChannelData(channelIndex);

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

            if (addressValue < 0 || addressValue > UINT16_MAX) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register address out of range (0-65535)");
                return;
            }
            if (bitsValue < 0 || bitsValue > UINT8_MAX) {
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

            if (addressValue < 0 || addressValue > UINT16_MAX) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Register address out of range (0-65535)");
                return;
            }
            if (bitsValue < 0 || bitsValue > UINT8_MAX) {
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
                if (indexValue < 0 || indexValue > UINT8_MAX) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Channel index out of range (0-255)");
                } else {
                    uint8_t channelIndex = (uint8_t)(indexValue);
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
        
        // Reset all energy values
        server.on("/api/v1/ade7953/energy/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            Ade7953::resetEnergyValues();
            LOG_INFO("ADE7953 energy values reset via API");
            _sendSuccessResponse(request, "ADE7953 energy values reset successfully");
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
                float activeEnergyImported = doc["activeEnergyImported"].as<float>();
                float activeEnergyExported = doc["activeEnergyExported"].as<float>();
                float reactiveEnergyImported = doc["reactiveEnergyImported"].as<float>();
                float reactiveEnergyExported = doc["reactiveEnergyExported"].as<float>();
                float apparentEnergy = doc["apparentEnergy"].as<float>();

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

        // Get cloud services status
        server.on("/api/v1/mqtt/cloud-services", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            #ifdef HAS_SECRETS
            doc["enabled"] = Mqtt::isCloudServicesEnabled();
            #else
            doc["enabled"] = false; // If no secrets, cloud services are not enabled
            #endif

            _sendJsonResponse(request, doc);
        });

        // Set cloud services status
        static AsyncCallbackJsonWebHandler *setCloudServicesHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/mqtt/cloud-services",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                #ifdef HAS_SECRETS
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
                #else
                _sendErrorResponse(request, HTTP_CODE_FORBIDDEN, "Cloud services are not available without secrets");
                return;
                #endif
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
    static void _serveCrashEndpoints()
    {
        // Get crash information and analysis
        server.on("/api/v1/crash/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            if (CrashMonitor::getCoreDumpInfoJson(doc)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve crash information");
            }
        });

        // Get core dump data (with offset and chunk size parameters)
        server.on("/api/v1/crash/dump", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            // Parse query parameters
            size_t offset = 0;
            size_t chunkSize = CRASH_DUMP_DEFAULT_CHUNK_SIZE;

            if (request->hasParam("offset")) {
                offset = request->getParam("offset")->value().toInt();
            }
            
            if (request->hasParam("size")) {
                chunkSize = request->getParam("size")->value().toInt();
                // Limit maximum chunk size to prevent memory issues
                if (chunkSize > CRASH_DUMP_MAX_CHUNK_SIZE) {
                    LOG_DEBUG("Chunk size too large, limiting to %zu bytes", CRASH_DUMP_MAX_CHUNK_SIZE);
                    chunkSize = CRASH_DUMP_MAX_CHUNK_SIZE;
                }
                if (chunkSize == 0) {
                    chunkSize = CRASH_DUMP_DEFAULT_CHUNK_SIZE;
                }
            }

            if (!CrashMonitor::hasCoreDump()) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No core dump available");
                return;
            }

            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            if (CrashMonitor::getCoreDumpChunkJson(doc, offset, chunkSize)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to retrieve core dump data");
            }
        });

        // Clear core dump from flash
        server.on("/api/v1/crash/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            if (CrashMonitor::hasCoreDump()) {
                CrashMonitor::clearCoreDump();
                LOG_INFO("Core dump cleared via API");
                _sendSuccessResponse(request, "Core dump cleared successfully");
            } else {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No core dump available to clear");
            }
        });
    }

    // === LED ENDPOINTS ===
    static void _serveLedEndpoints()
    {
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

    // === FILE OPERATION ENDPOINTS ===
    static void _serveFileEndpoints()
    {
        // List files in LittleFS. The endpoint cannot be only "files" as it conflicts with the file serving endpoint (defined below)
        server.on("/api/v1/list-files", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            SpiRamAllocator allocator;
            JsonDocument doc(&allocator);
            
            if (listLittleFsFiles(doc)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to list LittleFS files");
            }
        });

        // GET - Download file from LittleFS
        server.on("/api/v1/files/*", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            String url = request->url();
            String filename = url.substring(url.indexOf("/api/v1/files/") + 14); // Remove "/api/v1/files/" prefix
            
            if (filename.length() == 0) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File path cannot be empty");
                return;
            }
            
            // URL decode the filename to handle encoded slashes properly
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

            // Determine content type based on file extension
            const char* contentType = getContentTypeFromFilename(filename.c_str());

            // Check if download is forced via query parameter
            bool forceDownload = request->hasParam("download");

            // Serve the file directly from LittleFS with proper content type
            request->send(LittleFS, filename, contentType, forceDownload);
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
        // HACK: using POST with JSON body to avoid wildcard DELETE issues with AsyncWebServer, and also
        // not using the same endpoint as the * would catch files/delete
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
        return getTaskInfoSafely(_healthCheckTaskHandle, HEALTH_CHECK_TASK_STACK_SIZE);
    }

    TaskInfo getOtaTimeoutTaskInfo()
    {
        return getTaskInfoSafely(_otaTimeoutTaskHandle, OTA_TIMEOUT_TASK_STACK_SIZE);
    }
}