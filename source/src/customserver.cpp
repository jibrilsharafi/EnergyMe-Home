#include "customserver.h"

static const char *TAG = "customserver";

namespace CustomServer
{
    // Private variables
    // ==============================
    // ==============================

    static AsyncWebServer server(WEBSERVER_PORT);
    static AsyncAuthenticationMiddleware digestAuth;
    static AsyncRateLimitMiddleware rateLimit;
    //TODO: custom middleware for statistics

    // Health check task variables
    static TaskHandle_t _healthCheckTaskHandle = NULL;
    static bool _healthCheckTaskShouldRun = false;
    static uint32_t _consecutiveFailures = 0;

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

    // Authentication management
    static bool _setWebPassword(const char *password);
    static bool _getWebPassword(char *buffer, size_t bufferSize);
    static bool _validatePasswordStrength(const char *password);

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc, int32_t statusCode = HTTP_CODE_OK);
    static void _sendSuccessResponse(AsyncWebServerRequest *request, const char *message);
    static void _sendErrorResponse(AsyncWebServerRequest *request, int32_t statusCode, const char *message);

    // API request synchronization helpers
    static bool _acquireApiMutex(AsyncWebServerRequest *request);
    static void _releaseApiMutex();

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
    static void _serveFirmwareStatusEndpoint();
    static void _handleOtaUploadComplete(AsyncWebServerRequest *request);
    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename, 
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

    // Public functions
    // ================
    // ================

    void begin()
    {
        logger.debug("Setting up web server...", TAG);

        // Initialize API synchronization mutex
        _apiMutex = xSemaphoreCreateMutex();
        if (_apiMutex == NULL) {
            logger.error("Failed to create API mutex", TAG);
            return;
        }
        logger.debug("API mutex created successfully", TAG);

        _setupMiddleware();
        _serveStaticContent();
        _serveApi();

        server.begin();

        logger.info("Web server started on port %d", TAG, WEBSERVER_PORT);

        // Start health check task to ensure the web server is responsive, and if it is not, restart the ESP32
        _startHealthCheckTask();
    }

    void stop()
    {
        logger.debug("Stopping web server...", TAG);

        // Stop health check task
        _stopHealthCheckTask();

        // Stop the server
        server.end();

        // Delete API mutex
        if (_apiMutex != NULL) {
            vSemaphoreDelete(_apiMutex);
            _apiMutex = NULL;
            logger.debug("API mutex deleted", TAG);
        }
        
        logger.info("Web server stopped", TAG);
    }

    void updateAuthPassword()
    {
        char webPassword[PASSWORD_BUFFER_SIZE];
        if (_getWebPassword(webPassword, sizeof(webPassword)))
        {
            digestAuth.setPassword(webPassword);
            digestAuth.generateHash(); // regenerate hash with new password
            logger.info("Authentication password updated", TAG);
        }
        else
        {
            logger.error("Failed to load new password for authentication", TAG);
        }
    }

    bool resetWebPassword()
    {
        logger.info("Resetting web password to default", TAG);
        return _setWebPassword(WEBSERVER_DEFAULT_PASSWORD);
    }

    // Private functions
    // =================
    // =================

    static void _setupMiddleware()
    {
        // ---- Authentication Middleware Setup ----
        // Configure digest authentication (more secure than basic auth)
        digestAuth.setUsername(WEBSERVER_DEFAULT_USERNAME);

        // Load password from Preferences or use default
        char webPassword[PASSWORD_BUFFER_SIZE];
        if (_getWebPassword(webPassword, sizeof(webPassword)))
        {
            digestAuth.setPassword(webPassword);
            logger.debug("Web password loaded from Preferences", TAG);
        }
        else
        {
            // Fallback to default password if Preferences failed
            digestAuth.setPassword(WEBSERVER_DEFAULT_PASSWORD);
            logger.warning("Failed to load web password, using default", TAG);

            // Try to initialize the password in Preferences for next time
            if (_setWebPassword(WEBSERVER_DEFAULT_PASSWORD)) { logger.debug("Default password saved to Preferences for future use", TAG); }
        }

        digestAuth.setRealm(WEBSERVER_REALM);
        digestAuth.setAuthFailureMessage("The password is incorrect. Please try again.");
        digestAuth.setAuthType(AsyncAuthType::AUTH_DIGEST);
        digestAuth.generateHash(); // precompute hash for better performance

        server.addMiddleware(&digestAuth);

        logger.debug("Digest authentication configured", TAG);

        // ---- Rate Limiting Middleware Setup ----
        // Set rate limiting to prevent abuse
        rateLimit.setMaxRequests(WEBSERVER_MAX_REQUESTS);
        rateLimit.setWindowSize(WEBSERVER_WINDOW_SIZE_SECONDS);

        server.addMiddleware(&rateLimit);

        logger.debug("Rate limiting configured: max requests = %d, window size = %d seconds", TAG, WEBSERVER_MAX_REQUESTS, WEBSERVER_WINDOW_SIZE_SECONDS);

        logger.debug("Logging middleware configured", TAG);
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
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = message;
        _sendJsonResponse(request, doc, HTTP_CODE_OK);
        statistics.webServerRequests++;

        _releaseApiMutex(); // Release mutex on error to avoid deadlocks
    }

    static void _sendErrorResponse(AsyncWebServerRequest *request, int32_t statusCode, const char *message)
    {
        JsonDocument doc;
        doc["success"] = false;
        doc["error"] = message;
        _sendJsonResponse(request, doc, statusCode);
        statistics.webServerRequestsError++;

        _releaseApiMutex(); // Release mutex on error to avoid deadlocks
    }

    static bool _acquireApiMutex(AsyncWebServerRequest *request)
    {
        if (_apiMutex == NULL) {
            logger.error("API mutex not initialized", TAG);
            _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Server synchronization error");
            return false;
        }

        BaseType_t result = xSemaphoreTake(_apiMutex, pdMS_TO_TICKS(API_MUTEX_TIMEOUT_MS));
        if (result != pdTRUE) {
            logger.warning("Failed to acquire API mutex within timeout", TAG);
            _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Server busy, please try again");
            return false;
        }

        logger.debug("API mutex acquired for request: %s", TAG, request->url().c_str());
        return true;
    }

    static void _releaseApiMutex()
    {
        if (_apiMutex != NULL) {
            xSemaphoreGive(_apiMutex);
            logger.debug("API mutex released", TAG);
        }
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
        const char* expectedMethod = isPartialUpdate ? "PATCH" : "PUT";

        return isPartialUpdate;
    }

    static void _startHealthCheckTask()
    {
        if (_healthCheckTaskHandle != NULL)
        {
            logger.debug("Health check task is already running", TAG);
            return;
        }

        logger.debug("Starting health check task", TAG);
        _consecutiveFailures = 0;

        BaseType_t result = xTaskCreate(
            _healthCheckTask,
            HEALTH_CHECK_TASK_NAME,
            HEALTH_CHECK_TASK_STACK_SIZE,
            NULL,
            HEALTH_CHECK_TASK_PRIORITY,
            &_healthCheckTaskHandle);

        if (result != pdPASS) { logger.error("Failed to create health check task", TAG); }
    }

    static void _stopHealthCheckTask() { stopTaskGracefully(&_healthCheckTaskHandle, "Health check task"); }

    static void _healthCheckTask(void *parameter)
    {
        logger.debug("Health check task started", TAG);

        _healthCheckTaskShouldRun = true;
        while (_healthCheckTaskShouldRun)
        {
            // Perform health check
            if (_performHealthCheck())
            {
                // Reset failure counter on success
                if (_consecutiveFailures > 0)
                {
                    logger.info("Health check recovered after %d failures", TAG, _consecutiveFailures);
                    _consecutiveFailures = 0;
                }
                logger.debug("Health check passed", TAG);
            }
            else
            {
                _consecutiveFailures++;
                logger.warning("Health check failed (attempt %d/%d)", TAG, _consecutiveFailures, HEALTH_CHECK_MAX_FAILURES);

                if (_consecutiveFailures >= HEALTH_CHECK_MAX_FAILURES)
                {
                    logger.error("Health check failed %d consecutive times, requesting system restart", TAG, HEALTH_CHECK_MAX_FAILURES);
                    setRestartSystem(TAG, "Server health check failures exceeded maximum threshold");
                    break; // Exit the task as we're restarting
                }
            }

            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS));
            if (notificationValue > 0)
            {
                _healthCheckTaskShouldRun = false;
                break;
            }
        }

        logger.debug("Health check task stopping", TAG);
        _healthCheckTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    static bool _performHealthCheck()
    {
        // Check if WiFi is connected
        if (!CustomWifi::isFullyConnected())
        {
            logger.warning("Health check: WiFi not connected", TAG);
            return false;
        }

        // Perform a simple HTTP self-request to verify server responsiveness
        WiFiClient client;
        client.setTimeout(HEALTH_CHECK_TIMEOUT_MS);

        if (!client.connect("127.0.0.1", WEBSERVER_PORT))
        {
            logger.warning("Health check: Cannot connect to local web server", TAG);
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
                        return true;
                    }
                    else
                    {
                        logger.warning("Health check: HTTP status code %d", TAG, statusCode);
                        return false;
                    }
                }
            }
            delay(10); // Small delay to prevent busy waiting
        }

        client.stop();
        logger.warning("Health check: HTTP request timeout", TAG);
        return false;
    }

    // Password management functions
    // ------------------------------
    static bool _setWebPassword(const char *password)
    {
        if (!_validatePasswordStrength(password))
        {
            logger.error("Password does not meet strength requirements", TAG);
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, false))
        {
            logger.error("Failed to open auth preferences for writing", TAG);
            return false;
        }

        bool success = prefs.putString(PREFERENCES_KEY_PASSWORD, password) > 0;
        prefs.end();

        if (success) { logger.info("Web password updated successfully", TAG); }
        else { logger.error("Failed to save web password", TAG); }

        return success;
    }

    static bool _getWebPassword(char *buffer, size_t bufferSize)
    {
        logger.debug("Getting web password", TAG);

        if (buffer == nullptr || bufferSize == 0)
        {
            logger.error("Invalid buffer for getWebPassword", TAG);
            return false;
        }

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, true))
        {
            logger.error("Failed to open auth preferences for reading", TAG);
            return false;
        }

        prefs.getString(PREFERENCES_KEY_PASSWORD, buffer, bufferSize);
        prefs.end();

        return true;
    }
    // Only check length - there is no need to be picky here
    static bool _validatePasswordStrength(const char *password)
    {
        if (password == nullptr) { return false; }

        size_t length = strlen(password);

        // Check minimum length
        if (length < MIN_PASSWORD_LENGTH)
        {
            logger.warning("Password too short", TAG);
            return false;
        }

        // Check maximum length
        if (length > MAX_PASSWORD_LENGTH)
        {
            logger.warning("Password too int32_t", TAG);
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

        // Resources
        server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "image/svg+xml", favicon_svg); });

        // Main dashboard
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->send(HTTP_CODE_OK, "text/html", index_html); });

        // Configuration pages
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
            
            JsonDocument doc;
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
            JsonDocument doc;
            
            // Check if using default password
            char currentPassword[PASSWORD_BUFFER_SIZE];
            bool isDefault = true;
            if (_getWebPassword(currentPassword, sizeof(currentPassword))) {
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
                JsonDocument doc;
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
                if (!_getWebPassword(storedPassword, sizeof(storedPassword)))
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

                logger.info("Password changed successfully via API", TAG);
                _sendSuccessResponse(request, "Password changed successfully");
                
                // Update authentication middleware with new password
                updateAuthPassword();
            });

        changePasswordHandler->setMethod(HTTP_POST);
        server.addHandler(changePasswordHandler);
    }

    static void _serveResetPasswordEndpoint()
    {
        server.on("/api/v1/auth/reset-password", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (resetWebPassword()) {
                updateAuthPassword();
                logger.warning("Password reset to default via API", TAG);
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
        
        if (Update.hasError()) {
            JsonDocument doc;
            doc["success"] = false;
            doc["message"] = Update.errorString();
            _sendJsonResponse(request, doc);
            
            logger.error("OTA update failed: %s", TAG, Update.errorString());
            Update.printError(Serial);
            
            Led::blinkRedFast(Led::PRIO_CRITICAL, 5000ULL);
        } else {
            JsonDocument doc;
            doc["success"] = true;
            doc["message"] = "Firmware update completed successfully";
            doc["md5"] = Update.md5String();
            _sendJsonResponse(request, doc);
            
            logger.info("OTA update completed successfully", TAG);
            logger.debug("New firmware MD5: %s", TAG, Update.md5String().c_str());
            
            Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
            setRestartSystem(TAG, "Restart needed after firmware update");
        }
    }

    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename, 
                                   size_t index, uint8_t *data, size_t len, bool final)
    {
        static bool otaInitialized = false;

        // TODO: we should stop tasks here probably, and resume then only later. But how can we do it safely?
        
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

    static bool _initializeOtaUpload(AsyncWebServerRequest *request, const String& filename)
    {
        logger.info("Starting OTA update with file: %s", TAG, filename.c_str());
        
        // Validate file extension
        if (!filename.endsWith(".bin")) {
            logger.error("Invalid file type. Only .bin files are supported", TAG);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "File must be in .bin format");
            return false;
        }
        
        // Get content length from header
        size_t contentLength = request->header("Content-Length").toInt();
        if (contentLength == 0) {
            logger.error("No Content-Length header found or empty file", TAG);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing Content-Length header or empty file");
            return false;
        }
        
        // Validate minimum firmware size
        if (contentLength < MINIMUM_FIRMWARE_SIZE) {
            logger.error("Firmware file too small: %zu bytes (minimum: %d bytes)", TAG, contentLength, MINIMUM_FIRMWARE_SIZE);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Firmware file too small");
            return false;
        }
        
        // Check free heap
        size_t freeHeap = ESP.getFreeHeap();
        logger.debug("Free heap before OTA: %zu bytes", TAG, freeHeap);
        if (freeHeap < MINIMUM_FREE_HEAP_OTA) {
            logger.error("Insufficient memory for OTA update", TAG);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Insufficient memory for update");
            return false;
        }
        
        // Begin OTA update with known size
        if (!Update.begin(contentLength, U_FLASH)) {
            logger.error("Failed to begin OTA update: %s", TAG, Update.errorString());
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Failed to begin update");
            Led::doubleBlinkYellow(Led::PRIO_URGENT, 1000ULL);
            return false;
        }
        
        // Handle MD5 verification if provided
        _setupOtaMd5Verification(request);
        
        // Start LED indication for OTA progress
        Led::blinkPurpleFast(Led::PRIO_MEDIUM);
        
        logger.debug("OTA update started, expected size: %zu bytes", TAG, contentLength);
        return true;
    }

    static void _setupOtaMd5Verification(AsyncWebServerRequest *request)
    {
        if (!request->hasHeader("X-MD5")) {
            logger.warning("No MD5 header provided, skipping verification", TAG);
            return;
        }
        
        const char* md5HeaderCStr = request->header("X-MD5").c_str();
        size_t headerLength = strlen(md5HeaderCStr);
        
        if (headerLength == MD5_BUFFER_SIZE - 1) {
            char md5Header[MD5_BUFFER_SIZE];
            strncpy(md5Header, md5HeaderCStr, sizeof(md5Header) - 1);
            md5Header[sizeof(md5Header) - 1] = '\0';
            
            // Convert to lowercase
            for (size_t i = 0; md5Header[i]; i++) {
                md5Header[i] = tolower(md5Header[i]);
            }
            
            Update.setMD5(md5Header);
            logger.debug("MD5 verification enabled: %s", TAG, md5Header);
        } else if (headerLength > 0) {
            logger.warning("Invalid MD5 length (%zu), skipping verification", TAG, headerLength);
        } else {
            logger.warning("No MD5 header provided, skipping verification", TAG);
        }
    }

    static bool _writeOtaChunk(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index)
    {
        size_t written = Update.write(data, len);
        if (written != len) {
            logger.error("OTA write failed: expected %zu bytes, wrote %zu bytes", TAG, len, written);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Write failed");
            Update.abort();
            return false;
        }
        
        // Log progress periodically
        static size_t lastProgressIndex = 0;
        if (index >= lastProgressIndex + SIZE_REPORT_UPDATE_OTA || index == 0) {
            float progress = Update.size() > 0 ? (float)Update.progress() / Update.size() * 100.0 : 0.0;
            logger.debug("OTA progress: %.1f%% (%zu / %zu bytes)", TAG, progress, Update.progress(), Update.size());
            lastProgressIndex = index;
        }
        
        return true;
    }

    static void _finalizeOtaUpload(AsyncWebServerRequest *request)
    {
        logger.debug("Finalizing OTA update...", TAG);
        
        // Validate that we actually received data
        if (Update.progress() == 0) {
            logger.error("OTA finalization failed: No data received", TAG);
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "No firmware data received");
            Update.abort();
            return;
        }
        
        // Validate minimum size
        if (Update.progress() < MINIMUM_FIRMWARE_SIZE) {
            logger.error("OTA finalization failed: Firmware too small (%zu bytes)", TAG, Update.progress());
            _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Firmware file too small");
            Update.abort();
            return;
        }
        
        if (!Update.end(true)) {
            logger.error("OTA finalization failed: %s", TAG, Update.errorString());
            // Error response will be handled in the main handler
        } else {
            logger.debug("OTA update finalization successful", TAG);
            Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
        }
    }

    static void _serveOtaStatusEndpoint()
    {
        server.on("/api/v1/ota/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            
            doc["status"] = Update.isRunning() ? "running" : "idle";
            doc["canRollback"] = Update.canRollBack();
            
            const esp_partition_t *running = esp_ota_get_running_partition();
            doc["currentPartition"] = running->label;
            doc["hasError"] = Update.hasError();
            doc["lastError"] = Update.errorString();
            doc["size"] = Update.size();
            doc["progress"] = Update.progress();
            doc["remaining"] = Update.remaining();
            doc["progressPercent"] = Update.size() > 0 ? (float)Update.progress() / Update.size() * 100.0 : 0.0;
            
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
                logger.info("Aborted running OTA update", TAG);
            }

            if (Update.canRollBack()) {
                logger.warning("Firmware rollback requested via API", TAG);
                _sendSuccessResponse(request, "Rollback initiated. Device will restart.");
                
                Update.rollBack();
                setRestartSystem(TAG, "Firmware rollback requested via API");
            } else {
                logger.error("Rollback not possible: %s", TAG, Update.errorString());
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Rollback not possible");
            }
        });
    }

    static void _serveFirmwareStatusEndpoint()
    {

        // Get firmware update information
        server.on("/api/v1/firmware/update-info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            
            // Get current firmware info
            doc["currentVersion"] = FIRMWARE_BUILD_VERSION;
            doc["buildDate"] = FIRMWARE_BUILD_DATE;
            doc["buildTime"] = FIRMWARE_BUILD_TIME;
            
            // Get available update info from MQTT configuration
            char availableVersion[VERSION_BUFFER_SIZE];
            char updateUrl[URL_BUFFER_SIZE];
            
            Mqtt::getFirmwareUpdatesVersion(availableVersion, sizeof(availableVersion));
            Mqtt::getFirmwareUpdatesUrl(updateUrl, sizeof(updateUrl));
            
            if (strlen(availableVersion) > 0) {
                doc["availableVersion"] = availableVersion;
            }
            
            if (strlen(updateUrl) > 0) {
                doc["updateUrl"] = updateUrl;
            }
            
            doc["isLatest"] = Mqtt::isLatestFirmwareInstalled();
            
            _sendJsonResponse(request, doc);
        });
    }

    // === SYSTEM MANAGEMENT ENDPOINTS ===
    static void _serveSystemEndpoints()
    {
        // System information
        server.on("/api/v1/system/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            
            // Get both static and dynamic info
            JsonDocument staticDoc, dynamicDoc;
            getJsonDeviceStaticInfo(staticDoc);
            getJsonDeviceDynamicInfo(dynamicDoc);

            // Combine into a single response
            doc["static"] = staticDoc;
            doc["dynamic"] = dynamicDoc;
            
            _sendJsonResponse(request, doc); });

        // Statistics
        server.on("/api/v1/system/statistics", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            statisticsToJson(statistics, doc);
            _sendJsonResponse(request, doc); });

        // System restart
        server.on("/api/v1/system/restart", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            _sendSuccessResponse(request, "System restart initiated");
            setRestartSystem(TAG, "System restart requested via API"); });

        // Factory reset
        server.on("/api/v1/system/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            _sendSuccessResponse(request, "Factory reset initiated");
            setRestartSystem(TAG, "Factory reset requested via API", true); });

        // Check if secrets exist
        server.on("/api/v1/system/secrets", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            doc["hasSecrets"] = HAS_SECRETS;
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
            JsonDocument doc;
            doc["print"] = logger.logLevelToString(logger.getPrintLevel());
            doc["save"] = logger.logLevelToString(logger.getSaveLevel());
            _sendJsonResponse(request, doc);
        });

        // Set log levels (using AsyncCallbackJsonWebHandler for JSON body)
        static AsyncCallbackJsonWebHandler *setLogLevelHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/logs/level",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL)) return;

                JsonDocument doc;
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
                        logger.setPrintLevel(level);
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
                        logger.setSaveLevel(level);
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
                    logger.info("Log levels %s via API", TAG, isPartialUpdate ? "partially updated" : "updated");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid log level specified. Valid levels: VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL");
                }
            });
        server.addHandler(setLogLevelHandler);

        // Clear logs
        server.on("/api/v1/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            logger.clearLog();
            _sendSuccessResponse(request, "Logs cleared successfully");
            logger.info("Logs cleared via API", TAG);
        });
    }

    // === ADE7953 ENDPOINTS ===
    static void _serveAde7953Endpoints() {
        // === CONFIGURATION ENDPOINTS ===
        
        // Get ADE7953 configuration
        server.on("/api/v1/ade7953/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
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

                JsonDocument doc;
                doc.set(json);

                if (Ade7953::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    logger.info("ADE7953 configuration %s via API", TAG, isPartialUpdate ? "partially updated" : "updated");
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
            JsonDocument doc;
            doc["sampleTime"] = Ade7953::getSampleTime();
            
            _sendJsonResponse(request, doc);
        });

        // Set sample time
        static AsyncCallbackJsonWebHandler *setSampleTimeHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/sample-time",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_SAMPLE_TIME)) return;

                JsonDocument doc;
                doc.set(json);

                if (!doc["sampleTime"].is<uint32_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "sampleTime field must be a positive integer");
                    return;
                }

                uint32_t sampleTime = doc["sampleTime"].as<uint32_t>();

                if (Ade7953::setSampleTime(sampleTime))
                {
                    logger.info("ADE7953 sample time updated to %lu ms via API", TAG, sampleTime);
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
            if (!request->hasParam("index")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing channel index parameter");
                return;
            }

            uint32_t channelIndex = request->getParam("index")->value().toInt();
            
            JsonDocument doc;
            Ade7953::getChannelDataAsJson(doc, channelIndex);
            
            _sendJsonResponse(request, doc);
        });

        // Set single channel data (PUT/PATCH)
        static AsyncCallbackJsonWebHandler *setChannelDataHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/channel",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_CHANNEL_DATA)) return;

                JsonDocument doc;
                doc.set(json);

                if (Ade7953::setChannelDataFromJson(doc, isPartialUpdate))
                {
                    uint32_t channelIndex = doc["index"].as<uint32_t>();
                    logger.info("ADE7953 channel %lu data %s via API", TAG, channelIndex, isPartialUpdate ? "partially updated" : "updated");
                    _sendSuccessResponse(request, "ADE7953 channel data updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid ADE7953 channel data");
                }
            });
        server.addHandler(setChannelDataHandler);

        // Reset single channel data
        server.on("/api/v1/ade7953/channel/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;
            
            if (!request->hasParam("index")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing channel index parameter");
                return;
            }

            uint32_t channelIndex = request->getParam("index")->value().toInt();
            Ade7953::resetChannelData(channelIndex);
            
            logger.info("ADE7953 channel %lu data reset via API", TAG, channelIndex);
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

            int32_t address = request->getParam("address")->value().toInt();
            int32_t bits = request->getParam("bits")->value().toInt();
            bool signedData = request->hasParam("signed") ? request->getParam("signed")->value().equals("true") : false;

            int32_t value = Ade7953::readRegister(address, bits, signedData);
            
            JsonDocument doc;
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

                JsonDocument doc;
                doc.set(json);

                if (!doc["address"].is<int32_t>() || !doc["bits"].is<int32_t>() || !doc["value"].is<int32_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "address, bits, and value fields must be integers");
                    return;
                }

                int32_t address = doc["address"].as<int32_t>();
                int32_t bits = doc["bits"].as<int32_t>();
                int32_t value = doc["value"].as<int32_t>();

                Ade7953::writeRegister(address, bits, value);
                
                logger.info("ADE7953 register 0x%X (%d bits) written with value 0x%X via API", TAG, address, bits, value);
                _sendSuccessResponse(request, "ADE7953 register written successfully");
            });
        server.addHandler(writeRegisterHandler);

        // === METER VALUES ENDPOINTS ===
        
        // Get all meter values
        server.on("/api/v1/ade7953/meter-values", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            Ade7953::fullMeterValuesToJson(doc);
            
            if (!doc.isNull()) _sendJsonResponse(request, doc);
            else _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No meter values available");
        });

        // Get single channel meter values
        server.on("/api/v1/ade7953/meter-values/channel", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            if (!request->hasParam("index")) {
                _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing channel index parameter");
                return;
            }

            uint32_t channelIndex = request->getParam("index")->value().toInt();
            
            JsonDocument doc;
            Ade7953::singleMeterValuesToJson(doc, channelIndex);

            if (!doc.isNull()) _sendJsonResponse(request, doc);
            else _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "No meter values available");
        });

        // === GRID FREQUENCY ENDPOINT ===
        
        // Get grid frequency
        server.on("/api/v1/ade7953/grid-frequency", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            doc["gridFrequency"] = Ade7953::getGridFrequency();
            
            _sendJsonResponse(request, doc);
        });

        // === ENERGY VALUES ENDPOINTS ===
        
        // Reset all energy values
        server.on("/api/v1/ade7953/energy/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) return;

            Ade7953::resetEnergyValues();
            logger.info("ADE7953 energy values reset via API", TAG);
            _sendSuccessResponse(request, "ADE7953 energy values reset successfully");
        });

        // Set energy values for a specific channel
        static AsyncCallbackJsonWebHandler *setEnergyValuesHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/ade7953/energy",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_ADE7953_ENERGY)) return;

                JsonDocument doc;
                doc.set(json);

                if (!doc["channel"].is<uint32_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "channel field must be a positive integer");
                    return;
                }

                uint32_t channel = doc["channel"].as<uint32_t>();
                float activeEnergyImported = doc["activeEnergyImported"].as<float>();
                float activeEnergyExported = doc["activeEnergyExported"].as<float>();
                float reactiveEnergyImported = doc["reactiveEnergyImported"].as<float>();
                float reactiveEnergyExported = doc["reactiveEnergyExported"].as<float>();
                float apparentEnergy = doc["apparentEnergy"].as<float>();

                if (Ade7953::setEnergyValues(channel, activeEnergyImported, activeEnergyExported, 
                                           reactiveEnergyImported, reactiveEnergyExported, apparentEnergy))
                {
                    logger.info("ADE7953 energy values set for channel %lu via API", TAG, channel);
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
            JsonDocument doc;
            CustomMqtt::getConfigurationAsJson(doc);
            
            _sendJsonResponse(request, doc);
        });

        static AsyncCallbackJsonWebHandler *setCustomMqttHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/custom-mqtt/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {                
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT)) return;

                JsonDocument doc;
                doc.set(json);

                if (CustomMqtt::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    logger.info("Custom MQTT configuration %s via API", TAG, isPartialUpdate ? "partially updated" : "updated");
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
            
            JsonDocument doc;
            
            // Add runtime status information
            char statusBuffer[STATUS_BUFFER_SIZE];
            char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
            CustomMqtt::getRuntimeStatus(statusBuffer, sizeof(statusBuffer), timestampBuffer, sizeof(timestampBuffer));
            doc["status"] = statusBuffer;
            doc["statusTimestamp"] = timestampBuffer;
            
            _sendJsonResponse(request, doc);
        });
    }

    // === INFLUXDB ENDPOINTS ===
    static void _serveInfluxDbEndpoints()
    {
        // Get InfluxDB configuration
        server.on("/api/v1/influxdb/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            InfluxDbClient::getConfigurationAsJson(doc);

            _sendJsonResponse(request, doc);
        });

        // Set InfluxDB configuration
        static AsyncCallbackJsonWebHandler *setInfluxDbHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/influxdb/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                bool isPartialUpdate = _isPartialUpdate(request);
                if (!_validateRequest(request, isPartialUpdate ? "PATCH" : "PUT", HTTP_MAX_CONTENT_LENGTH_INFLUXDB)) return;

                JsonDocument doc;
                doc.set(json);

                if (InfluxDbClient::setConfigurationFromJson(doc, isPartialUpdate))
                {
                    logger.info("InfluxDB configuration updated via API", TAG);
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
            
            JsonDocument doc;
            
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
            JsonDocument doc;
            
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
                    logger.debug("Chunk size too large, limiting to %zu bytes", TAG, CRASH_DUMP_MAX_CHUNK_SIZE);
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

            JsonDocument doc;
            
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
                logger.info("Core dump cleared via API", TAG);
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
            JsonDocument doc;
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

                JsonDocument doc;
                doc.set(json);

                // Check if brightness field is provided and is a number
                if (!doc["brightness"].is<uint32_t>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid brightness parameter");
                    return;
                }

                uint32_t brightness = doc["brightness"].as<uint32_t>();

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
        // List files in SPIFFS. The endpoint cannot be only "files" as it conflicts with the file serving endpoint (defined below)
        server.on("/api/v1/list-files", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            
            if (listSpiffsFiles(doc)) {
                _sendJsonResponse(request, doc);
            } else {
                _sendErrorResponse(request, HTTP_CODE_INTERNAL_SERVER_ERROR, "Failed to list SPIFFS files");
            }
        });

        // Get specific file content
        // Note: Using "/api/v1/files/*" pattern works with ESPAsyncWebServer wildcard matching
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
            if (!SPIFFS.exists(filename)) {
                _sendErrorResponse(request, HTTP_CODE_NOT_FOUND, "File not found");
                return;
            }

            // Determine content type based on file extension
            const char* contentType = getContentTypeFromFilename(filename.c_str());

            // Serve the file directly from SPIFFS with proper content type
            request->send(SPIFFS, filename, contentType);
        });
    }
}