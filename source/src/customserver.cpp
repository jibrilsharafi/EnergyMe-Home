#include "customserver.h"

static const char *TAG = "customserver";

namespace CustomServer
{
    static AsyncWebServer server(WEBSERVER_PORT);
    static AsyncAuthenticationMiddleware digestAuth;
    static AsyncRateLimitMiddleware rateLimit;
    static AsyncLoggingMiddleware requestLogger;

    // Health check task variables
    static TaskHandle_t _healthCheckTaskHandle = NULL;
    static unsigned int _consecutiveFailures = 0;

// Task notification bits
#define HEALTH_CHECK_STOP_BIT (1UL << 0)

    static void _setupMiddleware();
    static void _serveStaticContent();
    static void _serveApi();

    static void _startHealthCheckTask();
    static void _stopHealthCheckTask();
    static void _healthCheckTask(void *parameter);
    static bool _performHealthCheck();

    static bool _setWebPassword(const char *password);
    static bool _getWebPassword(char *buffer, size_t bufferSize);
    static bool _validatePasswordStrength(const char *password);

    void begin()
    {
        logger.debug("Setting up web server...", TAG);

        _setupMiddleware();
        _serveStaticContent();
        _serveApi();

        server.begin();

        logger.info("Web server started on port %d", TAG, WEBSERVER_PORT);

        // Start health check task to ensure the web server is responsive, and if it is not, restart the ESP32
        _startHealthCheckTask();
    }

    void _setupMiddleware()
    {
        // ---- Authentication Middleware Setup ----
        // Configure digest authentication (more secure than basic auth)
        digestAuth.setUsername(WEBSERVER_DEFAULT_USERNAME);

        // Load password from Preferences or use default
        char webPassword[AUTH_PASSWORD_BUFFER_SIZE];
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
            if (_setWebPassword(WEBSERVER_DEFAULT_PASSWORD))
            {
                logger.debug("Default password saved to Preferences for future use", TAG);
            }
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

        // ---- Logging Middleware Setup ----
        // TODO: remove this in production, it's for development only
        requestLogger.setEnabled(true);
        requestLogger.setOutput(Serial);

        server.addMiddleware(&requestLogger);

        logger.debug("Logging middleware configured", TAG);
    }

    void _serveStaticContent()
    {
        // === STATIC CONTENT (no auth required) ===

        // CSS files
        server.on("/css/styles.css", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/css", styles_css); });

        server.on("/css/button.css", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/css", button_css); });

        server.on("/css/section.css", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/css", section_css); });

        server.on("/css/typography.css", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/css", typography_css); });

        // Resources
        server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "image/svg+xml", favicon_svg); });

        // === AUTHENTICATED PAGES ===

        // Main dashboard
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", index_html); });

        // Configuration pages
        server.on("/configuration", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", configuration_html); });

        server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", calibration_html); });

        server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", channel_html); });

        server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", info_html); });

        server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", log_html); });

        server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", update_html); });

        // Swagger UI
        server.on("/swagger-ui", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", swagger_ui_html); });

        server.on("/swagger.yaml", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/yaml", swagger_yaml); });
    }

    void updateAuthPassword()
    {
        char webPassword[AUTH_PASSWORD_BUFFER_SIZE];
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

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc)
    {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    }

    static void _sendSuccessResponse(AsyncWebServerRequest *request, const char *message)
    {
        JsonDocument doc;
        doc["success"] = true;
        doc["message"] = message;
        _sendJsonResponse(request, doc);
    }

    static void _sendErrorResponse(AsyncWebServerRequest *request, int statusCode, const char *message)
    {
        JsonDocument doc;
        doc["success"] = false;
        doc["error"] = message;
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    }

    // API endpoint groups
    static void _serveSystemEndpoints();
    static void _serveNetworkEndpoints();
    static void _serveLoggingEndpoints();

    void _serveApi()
    {
        // Group endpoints by functionality
        _serveSystemEndpoints();
        _serveNetworkEndpoints();
        _serveLoggingEndpoints();

        server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        
        JsonDocument doc;
        doc["status"] = "ok";
        doc["uptime"] = millis64();
        char timestamp[20];
        CustomTime::getTimestamp(timestamp, sizeof(timestamp));
        doc["timestamp"] = timestamp;
        
        serializeJson(doc, *response);
        request->send(response); })
            .skipServerMiddlewares()
            .addMiddleware(&requestLogger); // Only the logger is required,no auth or rate limiting

        // === AUTHENTICATION ENDPOINTS ===

        // Check authentication status
        server.on("/api/v1/auth/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        
        // Check if using default password
        char currentPassword[AUTH_PASSWORD_BUFFER_SIZE];
        bool isDefault = true;
        if (_getWebPassword(currentPassword, sizeof(currentPassword))) {
            isDefault = (strcmp(currentPassword, WEBSERVER_DEFAULT_PASSWORD) == 0);
        }
        
        doc["usingDefaultPassword"] = isDefault;
        doc["username"] = WEBSERVER_DEFAULT_USERNAME; // Only one username available in any case
        
        serializeJson(doc, *response);
        request->send(response); });

        // Change password (requires current password)
        // Using AsyncCallbackJsonWebHandler for cleaner JSON handling
        static AsyncCallbackJsonWebHandler *changePasswordHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/auth/change-password",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                // Parse JSON (automatically handled by AsyncCallbackJsonWebHandler)
                JsonDocument doc;
                doc.set(json);

                const char *currentPassword = doc["currentPassword"];
                const char *newPassword = doc["newPassword"];

                if (!currentPassword || !newPassword)
                {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing currentPassword or newPassword\"}");
                    return;
                }

                // Validate current password
                char storedPassword[AUTH_PASSWORD_BUFFER_SIZE];
                if (!_getWebPassword(storedPassword, sizeof(storedPassword)))
                {
                    request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to retrieve current password\"}");
                    return;
                }

                if (strcmp(currentPassword, storedPassword) != 0)
                {
                    request->send(401, "application/json", "{\"success\":false,\"message\":\"Current password is incorrect\"}");
                    return;
                }

                // Validate and save new password
                if (!_setWebPassword(newPassword))
                {
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"New password does not meet requirements or failed to save\"}");
                    return;
                }

                logger.info("Password changed successfully via API", TAG);
                request->send(200, "application/json", "{\"success\":true,\"message\":\"Password changed successfully\"}");

                delay(1000); // Give time for the response to be sent before updating the password

                // Update authentication middleware with new password (but after sending response otherwise the client will not receive it)
                updateAuthPassword();
            });

        changePasswordHandler->setMethod(HTTP_POST);
        server.addHandler(changePasswordHandler);

        // Reset password to default (admin operation)
        server.on("/api/v1/auth/reset-password", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        if (resetWebPassword()) {
            // Update authentication middleware with new password
            updateAuthPassword();
            
            logger.warning("Password reset to default via API", TAG);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Password reset to default\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to reset password\"}");
        } });

        // === OTA UPDATE ENDPOINTS ===

        // Firmware upload endpoint with integrated MD5 support
        server.on("/api/v1/ota/upload", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            // Handle the completion of the upload
            if (request->getResponse()) {
                // Response already set due to error
                return;
            }
            
            if (Update.hasError()) {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                JsonDocument doc;
                doc["success"] = false;
                doc["message"] = Update.errorString();
                serializeJson(doc, *response);
                request->send(response);
                
                logger.error("OTA update failed: %s", TAG, Update.errorString());
                Update.printError(Serial);
                
                // Clear OTA progress pattern and flash red LED pattern for error
                Led::blinkRed(Led::PRIO_CRITICAL, 5000ULL);
            } else {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                JsonDocument doc;
                doc["success"] = true;
                doc["message"] = "Firmware update completed successfully";
                doc["md5"] = Update.md5String();
                serializeJson(doc, *response);
                request->send(response);
                
                logger.info("OTA update completed successfully", TAG);
                logger.debug("New firmware MD5: %s", TAG, Update.md5String().c_str());
                
                // Clear OTA progress pattern and show success LED pattern
                Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
                
                // Schedule restart
                setRestartSystem(TAG, "Restart needed after firmware update");
            } }, [](AsyncWebServerRequest *request, String filename, size_t index, unsigned char *data, size_t len, bool final)
                  {
            // Handle firmware upload chunks
            static bool otaInitialized = false;
            
            if (!index) {
                // First chunk - initialize OTA
                logger.info("Starting OTA update with file: %s", TAG, filename.c_str());

                // TODO: mutex or similar to block/pause other processes, and also remember to resume them after OTA
                
                // Validate file extension
                if (!filename.endsWith(".bin")) {
                    logger.error("Invalid file type. Only .bin files are supported", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"File must be in .bin format\"}");
                    return;
                }
                
                // Get content length from header
                size_t contentLength = request->header("Content-Length").toInt();
                if (contentLength == 0) {
                    logger.error("No Content-Length header found or empty file", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing Content-Length header or empty file\"}");
                    return;
                }
                
                // Validate minimum firmware size (reasonable minimum for ESP32 firmware)
                if (contentLength < MINIMUM_FIRMWARE_SIZE) {
                    logger.error("Firmware file too small: %zu bytes (minimum: %d bytes)", TAG, contentLength, MINIMUM_FIRMWARE_SIZE);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Firmware file too small\"}");
                    return;
                }
                
                // Check free heap
                size_t freeHeap = ESP.getFreeHeap();
                logger.debug("Free heap before OTA: %zu bytes", TAG, freeHeap);
                if (freeHeap < MINIMUM_FREE_HEAP_OTA) { // Minimum free heap for OTA
                    logger.error("Insufficient memory for OTA update", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Insufficient memory for update\"}");
                    return;
                }
                
                // Pause ADE7953 during update to free resources
                // _ade7953.pauseMeterReadingTask(); // Uncomment if you have this method
                
                // Begin OTA update with known size
                if (!Update.begin(contentLength, U_FLASH)) {
                    logger.error("Failed to begin OTA update: %s", TAG, Update.errorString());
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to begin update\"}");
                    
                    // Restore LED and resume operations
                    Led::doubleBlinkYellow(Led::PRIO_URGENT, 1000ULL);
                    // _ade7953.resumeMeterReadingTask(); // Uncomment if you have this method
                    return;
                }
                
                // Check for MD5 header and set it if provided
                String md5Header = request->header("X-MD5");
                if (md5Header.length() == 32) {
                    // Convert to lowercase
                    md5Header.toLowerCase();
                    Update.setMD5(md5Header.c_str());
                    logger.debug("MD5 verification enabled: %s", TAG, md5Header.c_str());
                } else if (md5Header.length() > 0) {
                    logger.warning("Invalid MD5 length (%d), skipping verification", TAG, md5Header.length());
                } else {
                    logger.warning("No MD5 header provided, skipping verification", TAG);
                }

                // Start LED indication for OTA progress (indefinite duration)
                Led::blinkPurpleFast(Led::PRIO_MEDIUM); // Indefinite duration - will run until cleared
                
                otaInitialized = true;
                logger.debug("OTA update started, expected size: %zu bytes", TAG, contentLength);
            }
            
            // Write chunk to flash
            if (len && otaInitialized) {
                size_t written = Update.write(data, len);
                if (written != len) {
                    logger.error("OTA write failed: expected %zu bytes, wrote %zu bytes", TAG, len, written);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Write failed\"}");
                    Update.abort();
                    otaInitialized = false;
                    
                    return;
                }
                
                // Log progress sometimes
                static size_t lastProgressIndex = 0;
                if (index >= lastProgressIndex + SIZE_REPORT_UPDATE_OTA || index == 0) {
                    float progress = (float)Update.progress() / Update.size() * 100.0;
                    logger.debug("OTA progress: %.1f%% (%zu / %zu bytes)", TAG, progress, Update.progress(), Update.size());
                    lastProgressIndex = index;
                }
            }
            
            // Final chunk - complete the update
            if (final && otaInitialized) {
                logger.debug("Finalizing OTA update...", TAG);
                
                // Validate that we actually received data
                if (Update.progress() == 0) {
                    logger.error("OTA finalization failed: No data received", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"No firmware data received\"}");
                    Update.abort();
                    otaInitialized = false;
                    return;
                }
                
                // Validate minimum size
                if (Update.progress() < MINIMUM_FIRMWARE_SIZE) {
                    logger.error("OTA finalization failed: Firmware too small (%zu bytes)", TAG, Update.progress());
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Firmware file too small\"}");
                    Update.abort();
                    otaInitialized = false;
                    return;
                }
                
                if (!Update.end(true)) {
                    logger.error("OTA finalization failed: %s", TAG, Update.errorString());
                    // Error response will be handled in the main handler above
                    
                    // Restore operations on error
                    // TODO: fill this
                } else {
                    logger.debug("OTA update finalization successful", TAG);
                    
                    // Clear OTA progress pattern and show success LED pattern
                    Led::blinkGreenFast(Led::PRIO_CRITICAL, 3000ULL);
                }
                otaInitialized = false;
            } });

        // Get OTA update status
        server.on("/api/v1/ota/status", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            JsonDocument doc;
            
            if (Update.isRunning()) {
                doc["status"] = "running";
            } else {
                doc["status"] = "idle";
            }
            
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
            
            serializeJson(doc, *response);
            request->send(response); 
        });

        // OTA rollback endpoint
        server.on("/api/v1/ota/rollback", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
        if (Update.isRunning()) {
            Update.abort();
            logger.info("Aborted running OTA update", TAG);
        }

        if (Update.canRollBack()) {
            logger.warning("Firmware rollback requested via API", TAG);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Rollback initiated. Device will restart.\"}");

            Update.rollBack();
            setRestartSystem(TAG, "Firmware rollback requested via API");
        } else {
            logger.error("Rollback not possible: %s", TAG, Update.errorString());
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Rollback not possible\"}");
        } });
    }

    // === SYSTEM MANAGEMENT ENDPOINTS ===
    void _serveSystemEndpoints()
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
            _sendSuccessResponse(request, "System restart initiated");
            
            // Short delay to ensure response is sent
            delay(100);
            setRestartSystem(TAG, "System restart requested via API"); });

        // Factory reset
        server.on("/api/v1/system/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            _sendSuccessResponse(request, "Factory reset initiated");
            
            // Delay to ensure response is sent
            delay(2000);
            factoryReset(); });

        // Check if secrets exist
        server.on("/api/v1/system/secrets", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            doc["hasSecrets"] = HAS_SECRETS;
            _sendJsonResponse(request, doc); });
    }

    // === NETWORK MANAGEMENT ENDPOINTS ===
    void _serveNetworkEndpoints()
    {
        // WiFi reset
        server.on("/api/v1/network/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            _sendSuccessResponse(request, "WiFi credentials reset. Device will restart and enter configuration mode.");
            
            // Short delay to ensure response is sent
            delay(2000);
            CustomWifi::resetWifi(); });

        // WiFi information
        server.on("/api/v1/network/wifi/info", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            CustomWifi::getWifiInfoJson(doc);
            _sendJsonResponse(request, doc); });
    }

    // === LOGGING ENDPOINTS ===
    void _serveLoggingEndpoints()
    {
        // Get log levels
        server.on("/api/v1/logs/level", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            JsonDocument doc;
            doc["print"] = logger.logLevelToString(logger.getPrintLevel());
            doc["save"] = logger.logLevelToString(logger.getSaveLevel());
            _sendJsonResponse(request, doc); });

        // Set log levels (using AsyncCallbackJsonWebHandler for JSON body)
        static AsyncCallbackJsonWebHandler *setLogLevelHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/logs/level",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                JsonDocument doc;
                doc.set(json);

                const char *printLevel = doc["print"];
                const char *saveLevel = doc["save"];

                if (!printLevel && !saveLevel)
                {
                    _sendErrorResponse(request, 400, "At least one of 'print' or 'save' level must be specified");
                    return;
                }

                bool success = true;
                char resultMsg[256];
                snprintf(resultMsg, sizeof(resultMsg), "Log levels updated:");

                // Set print level if provided
                if (printLevel)
                {
                    // Convert string to LogLevel enum
                    LogLevel level;
                    if (strcmp(printLevel, "VERBOSE") == 0)
                        level = LogLevel::VERBOSE;
                    else if (strcmp(printLevel, "DEBUG") == 0)
                        level = LogLevel::DEBUG;
                    else if (strcmp(printLevel, "INFO") == 0)
                        level = LogLevel::INFO;
                    else if (strcmp(printLevel, "WARNING") == 0)
                        level = LogLevel::WARNING;
                    else if (strcmp(printLevel, "ERROR") == 0)
                        level = LogLevel::ERROR;
                    else if (strcmp(printLevel, "FATAL") == 0)
                        level = LogLevel::FATAL;
                    else
                    {
                        success = false;
                    }

                    if (success)
                    {
                        logger.setPrintLevel(level);
                        snprintf(resultMsg + strlen(resultMsg), sizeof(resultMsg) - strlen(resultMsg),
                                 " print=%s", printLevel);
                    }
                }

                // Set save level if provided
                if (saveLevel)
                {
                    // Convert string to LogLevel enum
                    LogLevel level;
                    if (strcmp(saveLevel, "VERBOSE") == 0)
                        level = LogLevel::VERBOSE;
                    else if (strcmp(saveLevel, "DEBUG") == 0)
                        level = LogLevel::DEBUG;
                    else if (strcmp(saveLevel, "INFO") == 0)
                        level = LogLevel::INFO;
                    else if (strcmp(saveLevel, "WARNING") == 0)
                        level = LogLevel::WARNING;
                    else if (strcmp(saveLevel, "ERROR") == 0)
                        level = LogLevel::ERROR;
                    else if (strcmp(saveLevel, "FATAL") == 0)
                        level = LogLevel::FATAL;
                    else
                    {
                        success = false;
                    }

                    if (success)
                    {
                        logger.setSaveLevel(level);
                        snprintf(resultMsg + strlen(resultMsg), sizeof(resultMsg) - strlen(resultMsg),
                                 " save=%s", saveLevel);
                    }
                }

                if (success)
                {
                    _sendSuccessResponse(request, resultMsg);
                    logger.info("Log levels updated via API", TAG);
                }
                else
                {
                    _sendErrorResponse(request, 400, "Invalid log level specified. Valid levels: VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL");
                }
            });

        setLogLevelHandler->setMethod(HTTP_PUT);
        server.addHandler(setLogLevelHandler);

        // Clear logs
        server.on("/api/v1/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            logger.clearLog();
            _sendSuccessResponse(request, "Logs cleared successfully");
            logger.info("Logs cleared via API", TAG); });
    }

    void _startHealthCheckTask()
    {
        if (_healthCheckTaskHandle != NULL)
        {
            logger.warning("Health check task already running", TAG);
            return;
        }

        _consecutiveFailures = 0;

        // Pass the current task handle so the health check task can notify us on exit
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();

        BaseType_t result = xTaskCreate(
            _healthCheckTask,
            HEALTH_CHECK_TASK_NAME,
            HEALTH_CHECK_TASK_STACK_SIZE,
            currentTask, // Pass caller's handle as parameter
            HEALTH_CHECK_TASK_PRIORITY,
            &_healthCheckTaskHandle);

        if (result == pdPASS)
        {
            logger.debug("Health check task started successfully", TAG);
        }
        else
        {
            logger.error("Failed to create health check task", TAG);
        }
    }

    void _stopHealthCheckTask()
    {
        if (_healthCheckTaskHandle == NULL)
        {
            logger.debug("Health check task not running", TAG);
            return;
        }

        logger.debug("Stopping health check task...", TAG);

        // Signal the task to stop using notification
        xTaskNotify(_healthCheckTaskHandle, HEALTH_CHECK_STOP_BIT, eSetBits);

        // Wake up the task if it's sleeping in vTaskDelayUntil
        xTaskAbortDelay(_healthCheckTaskHandle);

        // Wait for the task to acknowledge shutdown (up to 5 seconds)
        uint32_t notificationValue = 0;
        BaseType_t result = xTaskNotifyWait(0, 0, &notificationValue, pdMS_TO_TICKS(5000));

        if (result == pdTRUE)
        {
            logger.info("Health check task stopped gracefully", TAG);
        }
        else
        {
            logger.warning("Health check task did not acknowledge shutdown, assuming stopped", TAG);
        }

        // Task handle will be set to NULL by the task itself
        _healthCheckTaskHandle = NULL;
    }

    static void _healthCheckTask(void *parameter)
    {
        logger.debug("Health check task started", TAG);

        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS);
        TaskHandle_t callerHandle = (TaskHandle_t)parameter;

        while (true)
        {
            // Wait for the next cycle OR until we get a stop notification
            uint32_t notificationValue = 0;
            BaseType_t result = xTaskNotifyWait(0, UINT32_MAX, &notificationValue, xFrequency);

            // Check if we received a stop signal
            if (notificationValue & HEALTH_CHECK_STOP_BIT)
            {
                logger.debug("Health check task received stop signal", TAG);
                break;
            }

            // If we were woken by notification but no stop bit, continue normally
            // If we timed out (no notification), also continue normally

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
        }

        logger.debug("Health check task exiting", TAG);

        // Acknowledge shutdown to the caller
        if (callerHandle != NULL)
        {
            xTaskNotify(callerHandle, 1, eSetValueWithOverwrite);
        }

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
        unsigned long long startTime = millis64();
        while (client.connected() && (millis64() - startTime) < HEALTH_CHECK_TIMEOUT_MS)
        {
            if (client.available())
            {
                String line = client.readStringUntil('\n');
                if (line.startsWith("HTTP/1.1 "))
                {
                    int statusCode = line.substring(9, 12).toInt();
                    client.stop();

                    if (statusCode == 200)
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

        if (success)
        {
            logger.info("Web password updated successfully", TAG);
        }
        else
        {
            logger.error("Failed to save web password", TAG);
        }

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

    bool resetWebPassword()
    { // Has to be accessible from buttonHandler to physically reset the password
        logger.info("Resetting web password to default", TAG);
        return _setWebPassword(WEBSERVER_DEFAULT_PASSWORD);
    }

    // Only check length - there is no need to be picky here
    static bool _validatePasswordStrength(const char *password)
    {
        if (password == nullptr)
        {
            return false;
        }

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
            logger.warning("Password too long", TAG);
            return false;
        }
        return true;
    }
}
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         _ade7953.fullMeterValuesToJson(doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/meter-single", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("index")) {
//             int indexInt = request->getParam("index")->value().toInt();

//             if (indexInt >= 0 && indexInt < CHANNEL_COUNT) {
//                 ChannelNumber channel = static_cast<ChannelNumber>(indexInt);
//                 if (_ade7953.channelData[channel].active) {
//                     static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//                     JsonDocument doc;
//                     _ade7953.singleMeterValuesToJson(doc, channel);
//                     safeSerializeJson(doc, buffer, sizeof(buffer));
//                     request->send(200, "application/json", buffer);
//                 } else {
//                     request->send(400, "application/json", "{\"message\":\"Channel not active\"}");
//                 }
//             } else {
//                 request->send(400, "application/json", "{\"message\":\"Channel index out of range\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing index parameter\"}");
//         }
//     });

//     _server.on("/rest/active-power", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("index")) {
//             int indexInt = request->getParam("index")->value().toInt();

//             if (indexInt >= 0 && indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
//                 if (_ade7953.channelData[indexInt].active) {
//                     static char response[JSON_RESPONSE_BUFFER_SIZE];
//                     snprintf(response, sizeof(response), "{\"value\":%.6f}", _ade7953.meterValues[indexInt].activePower);
//                     request->send(200, "application/json", response);
//                 } else {
//                     request->send(400, "application/json", "{\"message\":\"Channel not active\"}");
//                 }
//             } else {
//                 request->send(400, "application/json", "{\"message\":\"Channel index out of range\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing index parameter\"}");
//         }
//     });

//     _server.on("/rest/get-channel", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         _ade7953.channelDataToJson(doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-energy", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         for (int i = 0; i < CHANNEL_COUNT; i++) {
//             if (_ade7953.channelData[i].active) {
//                 doc[i]["activeEnergyImported"] = _ade7953.meterValues[i].activeEnergyImported;
//                 doc[i]["activeEnergyExported"] = _ade7953.meterValues[i].activeEnergyExported;
//                 doc[i]["reactiveEnergyImported"] = _ade7953.meterValues[i].reactiveEnergyImported;
//                 doc[i]["reactiveEnergyExported"] = _ade7953.meterValues[i].reactiveEnergyExported;
//                 doc[i]["apparentEnergy"] = _ade7953.meterValues[i].apparentEnergy;
//             }
//         }
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     // Serve JSON configuration files
//     _server.on("/rest/get-ade7953-configuration", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, CONFIGURATION_ADE7953_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/get-calibration", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, CALIBRATION_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/get-custom-mqtt-configuration", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, CUSTOM_MQTT_CONFIGURATION_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/get-influxdb-configuration", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, INFLUXDB_CONFIGURATION_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/get-general-configuration", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         generalConfigurationToJson(generalConfiguration, doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-has-secrets", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         doc["has_secrets"] = HAS_SECRETS ? true : false;
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-log-level", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         doc["print"] = _logger.logLevelToString(_logger.getPrintLevel());
//         doc["save"] = _logger.logLevelToString(_logger.getSaveLevel());
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-statistics", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         statisticsToJson(statistics, doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-button-operation", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         const char* operationName = _buttonHandler.getCurrentOperationName();
//         unsigned long operationTimestamp = _buttonHandler.getCurrentOperationTimestamp();

//         static char response[JSON_RESPONSE_BUFFER_SIZE];

//         if (operationTimestamp > 0) {
//             char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
//             CustomTime::timestampFromUnix(operationTimestamp, timestampBuffer);
//             snprintf(response, sizeof(response), "{\"operationName\":\"%s\",\"operationTimestamp\":%lu,\"operationTimestampFormatted\":\"%s\"}",
//                 operationName, operationTimestamp, timestampBuffer);
//         } else {
//             snprintf(response, sizeof(response), "{\"operationName\":\"%s\",\"operationTimestamp\":%lu}",
//                 operationName, operationTimestamp);
//         }

//         request->send(200, "application/json", response);
//     });
// }

// void CustomServer::_setupPostApi() {
//     // Refactored: Use dedicated JSON handlers for each POST endpoint
//     _setChannelHandler = new AsyncCallbackJsonWebHandler("/rest/set-channel", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         // Rate limiting
//         unsigned long currentTime = millis64();
//         if (currentTime - _lastChannelUpdateTime < API_UPDATE_THROTTLE_MS) {
//             request->send(429, "application/json", "{\"error\":\"Rate limited\"}");
//             return;
//         }

//         if (!_acquireMutex(_channelMutex, "channel", pdMS_TO_TICKS(2000))) {
//             request->send(409, "application/json", "{\"error\":\"Resource locked\"}");
//             return;
//         }

//         // Validate that channel 0 is not being disabled
//         JsonDocument doc;
//         doc.set(json);
//         for (JsonPair kv : doc.as<JsonObject>()) {
//             int channelIndex = atoi(kv.key().c_str());
//             if (channelIndex == 0 && !kv.value()["active"].as<bool>()) {
//                 _releaseMutex(_channelMutex, "channel");
//                 request->send(400, "application/json", "{\"error\":\"Channel 0 cannot be disabled\"}");
//                 return;
//             }
//         }

//         _lastChannelUpdateTime = currentTime;
//         bool success = _ade7953.setChannelData(doc);
//         _releaseMutex(_channelMutex, "channel");
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setChannelHandler->setMethod(HTTP_POST);
//     _setChannelHandler->addMiddleware(&_apiRateLimitMiddleware);
//     _server.addHandler(_setChannelHandler);

//     _setGeneralConfigHandler = new AsyncCallbackJsonWebHandler("/rest/set-general-configuration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         // Rate limiting
//         unsigned long currentTime = millis64();
//         if (currentTime - _lastConfigUpdateTime < API_UPDATE_THROTTLE_MS) {
//             request->send(429, "application/json", "{\"error\":\"Rate limited\"}");
//             return;
//         }

//         if (!_acquireMutex(_configurationMutex, "configuration", pdMS_TO_TICKS(2000))) {
//             request->send(409, "application/json", "{\"error\":\"Resource locked\"}");
//             return;
//         }

//         JsonDocument doc;
//         doc.set(json);
//         // Force sendPowerData to existing value (not settable by API)
//         if (doc["sendPowerData"].is<bool>()) doc["sendPowerData"] = generalConfiguration.sendPowerData;

//         _lastConfigUpdateTime = currentTime;
//         bool success = setGeneralConfiguration(doc);
//         _releaseMutex(_configurationMutex, "configuration");
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setGeneralConfigHandler->setMethod(HTTP_POST);
//     _setGeneralConfigHandler->addMiddleware(&_apiRateLimitMiddleware);
//     _server.addHandler(_setGeneralConfigHandler);

//     // Add other POST handlers
//     _setCalibrationHandler = new AsyncCallbackJsonWebHandler("/rest/set-calibration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setCalibrationValues(doc);
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setCalibrationHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setCalibrationHandler);

//     _setAdeConfigHandler = new AsyncCallbackJsonWebHandler("/rest/set-ade7953-configuration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setConfiguration(doc);
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setAdeConfigHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setAdeConfigHandler);

//     _setCustomMqttHandler = new AsyncCallbackJsonWebHandler("/rest/set-custom-mqtt-configuration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _customMqtt.setConfiguration(doc);
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setCustomMqttHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setCustomMqttHandler);

//     _setInfluxDbHandler = new AsyncCallbackJsonWebHandler("/rest/set-influxdb-configuration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = InfluxDbClient::setConfiguration(doc);
//         request->send(success ? 200 : 400, "application/json", success ? "{"message":"OK"}" : "{"error":"Invalid data"}");
//     });
//     _setInfluxDbHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setInfluxDbHandler);

//     _setEnergyHandler = new AsyncCallbackJsonWebHandler("/rest/set-energy", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setEnergyValues(doc);
//         request->send(success ? 200 : 400, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setEnergyHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setEnergyHandler);

//     _uploadFileHandler = new AsyncCallbackJsonWebHandler("/rest/upload-file", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         if (doc["filename"] && doc["data"]) {
//             const char* filename = doc["filename"];
//             const char* data = doc["data"];

//             File file = SPIFFS.open(filename, FILE_WRITE);
//             if (file) {
//                 file.print(data);
//                 file.close();
//                 request->send(200, "application/json", "{\"message\":\"File uploaded\"}");
//             } else {
//                 request->send(500, "application/json", "{\"message\":\"Failed to open file\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing filename or data\"}");
//         }
//     });
//     _uploadFileHandler->setMethod(HTTP_POST);
//     _server.addHandler(_uploadFileHandler);

//     // Non-JSON POST endpoints
//     _server.on("/rest/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         request->send(200, "application/json", "{\"message\":\"Restarting...\"}");
//         setRestartEsp32(TAG, "Restart requested from API");
//     });

//     _server.on("/rest/reset-wifi", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         request->send(200, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
//         _customWifi.resetWifi();
//     });

//     _server.on("/rest/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         request->send(200, "application/json", "{\"message\":\"Factory reset in progress.\"}");
//         factoryReset();
//     });

//     _server.on("/rest/clear-log", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         _logger.clearLog();
//         request->send(200, "application/json", "{\"message\":\"Log cleared\"}");
//     });

//     _server.on("/rest/calibration-reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         _ade7953.setDefaultCalibrationValues();
//         _ade7953.setDefaultChannelData();
//         request->send(200, "application/json", "{\"message\":\"Calibration values reset\"}");
//     });

//     _server.on("/rest/reset-energy", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         _ade7953.resetEnergyValues();
//         request->send(200, "application/json", "{\"message\":\"Energy counters reset\"}");
//     });

//     _server.on("/rest/clear-button-operation", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         _buttonHandler.clearCurrentOperationName();
//         request->send(200, "application/json", "{\"message\":\"Button operation history cleared\"}");
//     });

//     _server.on("/rest/set-log-level", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("level") && request->hasParam("type")) {
//             int level = request->getParam("level")->value().toInt();
//             const char* type = request->getParam("type")->value().c_str();
//             if (strcmp(type, "print") == 0) {
//                 _logger.setPrintLevel(LogLevel(level));
//             } else if (strcmp(type, "save") == 0) {
//                 _logger.setSaveLevel(LogLevel(level));
//             } else {
//                 request->send(400, "application/json", "{\"message\":\"Invalid type parameter\"}");
//                 return;
//             }
//             request->send(200, "application/json", "{\"message\":\"Success\"}");
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing parameter\"}");
//         }
//     });

//     // ADE7953 register access endpoints
//     _server.on("/rest/ade7953-read-register", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("address") && request->hasParam("nBits") && request->hasParam("signed")) {
//             int address = request->getParam("address")->value().toInt();
//             int nBits = request->getParam("nBits")->value().toInt();
//             bool signedVal = request->getParam("signed")->value().equalsIgnoreCase("true");

//             if ((nBits == 8 || nBits == 16 || nBits == 24 || nBits == 32) && address >= 0 && address <= 0x3FF) {
//                 long registerValue = _ade7953.readRegister(address, nBits, signedVal);
//                 static char response[JSON_RESPONSE_BUFFER_SIZE];
//                 snprintf(response, sizeof(response), "{\"value\":%ld}", registerValue);
//                 request->send(200, "application/json", response);
//             } else {
//                 request->send(400, "application/json", "{\"message\":\"Invalid parameters\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing parameters\"}");
//         }
//     });

//     _server.on("/rest/ade7953-write-register", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("address") && request->hasParam("nBits") && request->hasParam("data")) {
//             int address = request->getParam("address")->value().toInt();
//             int nBits = request->getParam("nBits")->value().toInt();
//             int data = request->getParam("data")->value().toInt();

//             if ((nBits == 8 || nBits == 16 || nBits == 24 || nBits == 32) && address >= 0 && address <= 0x3FF) {
//                 _ade7953.writeRegister(address, nBits, data);
//                 request->send(200, "application/json", "{\"message\":\"Success\"}");
//             } else {
//                 request->send(400, "application/json", "{\"message\":\"Invalid parameters\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing parameters\"}");
//         }
//     });

//     // File operations
//     _server.on("/rest/list-files", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         File root = SPIFFS.open("/");
//         File file = root.openNextFile();

//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         unsigned int loops = 0;
//         while (file && loops < MAX_LOOP_ITERATIONS) {
//             loops++;
//             const char* filename = file.path();
//             if (strstr(filename, "secret") == nullptr) {
//                 doc[filename] = file.size();
//             }
//             file = root.openNextFile();
//         }

//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/file/*", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         char filename[FILENAME_BUFFER_SIZE];
//         const char* url = request->url().c_str();
//         if (strlen(url) > 10) {
//             snprintf(filename, sizeof(filename), "%s", url + 10);
//         } else {
//             filename[0] = '\0';
//         }

//         if (strstr(filename, "secret") != nullptr) {
//             request->send(401, "application/json", "{\"message\":\"Unauthorized\"}");
//             return;
//         }

//         // Use the simpler SPIFFS file serving approach
//         request->send(SPIFFS, filename);
//     });

//     // Additional GET endpoints for monitoring and status
//     _server.on("/rest/firmware-update-info", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, FW_UPDATE_INFO_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/firmware-update-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         request->send(SPIFFS, FW_UPDATE_STATUS_JSON_PATH, "application/json");
//     });

//     _server.on("/rest/is-latest-firmware-installed", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (isLatestFirmwareInstalled()) {
//             request->send(200, "application/json", "{\"latest\":true}");
//         } else {
//             request->send(200, "application/json", "{\"latest\":false}");
//         }
//     });

//     _server.on("/rest/get-current-monitor-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         if (!CrashMonitor::getJsonReport(doc, crashData)) {
//             request->send(500, "application/json", "{\"message\":\"Error getting monitoring data\"}");
//             return;
//         }
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/get-crash-data", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (!CrashMonitor::checkIfCrashDataExists()) {
//             request->send(404, "application/json", "{\"message\":\"No crash data available\"}");
//             return;
//         }

//         CrashData crashData;
//         if (!CrashMonitor::getSavedCrashData(crashData)) {
//             request->send(500, "application/json", "{\"message\":\"Could not get crash data\"}");
//             return;
//         }

//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         if (!CrashMonitor::getJsonReport(doc, crashData)) {
//             request->send(500, "application/json", "{\"message\":\"Could not create JSON report\"}");
//             return;
//         }
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     // Static file serving
//     _server.serveStatic("/api-docs", SPIFFS, "/swagger-ui.html");
//     _server.serveStatic("/swagger.yaml", SPIFFS, "/swagger.yaml");
//     _server.serveStatic("/log-raw", SPIFFS, LOG_PATH);
//     _server.serveStatic("/daily-energy", SPIFFS, DAILY_ENERGY_JSON_PATH);
// }