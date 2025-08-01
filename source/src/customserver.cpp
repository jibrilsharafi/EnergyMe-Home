#include "customserver.h"

static const char *TAG = "customserver";

namespace CustomServer
{
    static AsyncWebServer server(WEBSERVER_PORT);
    static AsyncAuthenticationMiddleware digestAuth;
    static AsyncRateLimitMiddleware rateLimit;

    // Health check task variables
    static TaskHandle_t _healthCheckTaskHandle = NULL;
    static bool _healthCheckTaskShouldRun = false;
    static unsigned int _consecutiveFailures = 0;

    // API request synchronization
    static SemaphoreHandle_t _apiMutex = NULL;

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

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc, int statusCode = HTTP_CODE_OK);
    static void _sendSuccessResponse(AsyncWebServerRequest *request, const char *message);
    static void _sendErrorResponse(AsyncWebServerRequest *request, int statusCode, const char *message);

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
    static void _serveCustomMqttEndpoints();
    static void _serveInfluxDbEndpoints();
    static void _serveCrashEndpoints();
    static void _serveLedEndpoints();
    
    // Authentication endpoints
    static void _serveAuthStatusEndpoint();
    static void _serveChangePasswordEndpoint();
    static void _serveResetPasswordEndpoint();
    
    // OTA endpoints
    static void _serveOtaUploadEndpoint();
    static void _serveOtaStatusEndpoint();
    static void _serveOtaRollbackEndpoint();
    static void _handleOtaUploadComplete(AsyncWebServerRequest *request);
    static void _handleOtaUploadData(AsyncWebServerRequest *request, const String& filename, 
                                   size_t index, unsigned char *data, size_t len, bool final);
    
    // OTA helper functions
    static bool _initializeOtaUpload(AsyncWebServerRequest *request, const String& filename);
    static void _setupOtaMd5Verification(AsyncWebServerRequest *request);
    static bool _writeOtaChunk(AsyncWebServerRequest *request, unsigned char *data, size_t len, size_t index);
    static void _finalizeOtaUpload(AsyncWebServerRequest *request);
    
    // Logging helper functions
    static bool _parseLogLevel(const char *levelStr, LogLevel &level);
    
    // HTTP method validation helper
    static bool _validateRequest(AsyncWebServerRequest *request, const char *expectedMethod, size_t maxContentLength = 0);
    
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

    static void _serveStaticContent()
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

    // Helper functions for common response patterns
    static void _sendJsonResponse(AsyncWebServerRequest *request, const JsonDocument &doc, int statusCode)
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

    static void _sendErrorResponse(AsyncWebServerRequest *request, int statusCode, const char *message)
    {
        JsonDocument doc;
        doc["success"] = false;
        doc["error"] = message;
        _sendJsonResponse(request, doc, statusCode);
        statistics.webServerRequestsError++;

        _releaseApiMutex(); // Release mutex on error to avoid deadlocks
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
        _serveCustomMqttEndpoints();
        _serveInfluxDbEndpoints();
        _serveCrashEndpoints();
        _serveLedEndpoints();
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

                delay(HTTP_RESPONSE_DELAY); // Give time for response to be sent

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
        if (request->getResponse()) { return; }  // Response already set due to error
        
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
                                   size_t index, unsigned char *data, size_t len, bool final)
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

    static bool _writeOtaChunk(AsyncWebServerRequest *request, unsigned char *data, size_t len, size_t index)
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
            float progress = (float)Update.progress() / Update.size() * 100.0;
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
            if (!_validateRequest(request, "POST")) { return; }

            _sendSuccessResponse(request, "System restart initiated");
            
            // Short delay to ensure response is sent
            delay(HTTP_RESPONSE_DELAY);
            
            setRestartSystem(TAG, "System restart requested via API"); });

        // Factory reset
        server.on("/api/v1/system/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            if (!_validateRequest(request, "POST")) { return; }

            _sendSuccessResponse(request, "Factory reset initiated");
            
            // Delay to ensure response is sent
            delay(HTTP_RESPONSE_DELAY);

            factoryReset(); });

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
            if (!_validateRequest(request, "POST")) { return; }

            _sendSuccessResponse(request, "WiFi credentials reset. Device will restart and enter configuration mode.");
            
            // Short delay to ensure response is sent
            delay(HTTP_RESPONSE_DELAY);
    
            CustomWifi::resetWifi(); });
    }

    // === API SYNCHRONIZATION HELPERS ===
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

        logger.debug("API mutex acquired", TAG);
        return true;
    }

    static void _releaseApiMutex()
    {
        if (_apiMutex != NULL) {
            xSemaphoreGive(_apiMutex);
            logger.debug("API mutex released", TAG);
        }
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
            _sendJsonResponse(request, doc); });

        // Set log levels (using AsyncCallbackJsonWebHandler for JSON body)
        static AsyncCallbackJsonWebHandler *_setLogLevelHandler = new AsyncCallbackJsonWebHandler("/api/v1/logs/level");
        _setLogLevelHandler->setMaxContentLength(HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL);
        _setLogLevelHandler->onRequest([](AsyncWebServerRequest *request, JsonVariant &json) {
                // Validate HTTP method
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_LOGS_LEVEL))  { return; }

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
                snprintf(resultMsg, sizeof(resultMsg), "Log levels updated:");
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
                    logger.info("Log levels updated via API", TAG);
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid log level specified. Valid levels: VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL");
                }
            });
        server.addHandler(_setLogLevelHandler);

        // Clear logs
        server.on("/api/v1/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            logger.clearLog();
            _sendSuccessResponse(request, "Logs cleared successfully");
            logger.info("Logs cleared via API", TAG); });
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
            unsigned long notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL_MS));
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
        unsigned long long startTime = millis64();
        while (client.connected() && (millis64() - startTime) < HEALTH_CHECK_TIMEOUT_MS)
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
                    int statusCode = atoi(statusStr);
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

    bool resetWebPassword()
    { // Has to be accessible from buttonHandler to physically reset the password
        logger.info("Resetting web password to default", TAG);
        return _setWebPassword(WEBSERVER_DEFAULT_PASSWORD);
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
            logger.warning("Password too long", TAG);
            return false;
        }
        
        return true;
    }
    
    // === CUSTOM MQTT ENDPOINTS ===
    static void _serveCustomMqttEndpoints()
    {
        // Get custom MQTT configuration
        server.on("/api/v1/mqtt/config", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            CustomMqttConfiguration config;
            CustomMqtt::getConfiguration(config);
            
            JsonDocument doc;
            CustomMqtt::configurationToJson(config, doc);
            _sendJsonResponse(request, doc);
        });

        // Set custom MQTT configuration
        static AsyncCallbackJsonWebHandler *setCustomMqttHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/mqtt/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_CUSTOM_MQTT)) { return; }

                JsonDocument doc;
                doc.set(json);

                if (CustomMqtt::setConfigurationFromJson(doc))
                {
                    logger.info("Custom MQTT configuration updated via API", TAG);
                    _sendSuccessResponse(request, "Custom MQTT configuration updated successfully");
                }
                else
                {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Invalid custom MQTT configuration");
                }
            });
        server.addHandler(setCustomMqttHandler);

        // Get custom MQTT status
        server.on("/api/v1/mqtt/status", HTTP_GET, [](AsyncWebServerRequest *request)
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
            InfluxDbConfiguration config;
            InfluxDbClient::getConfiguration(config);
            
            JsonDocument doc;
            InfluxDbClient::configurationToJson(config, doc);
            _sendJsonResponse(request, doc);
        });

        // Set InfluxDB configuration
        static AsyncCallbackJsonWebHandler *setInfluxDbHandler = new AsyncCallbackJsonWebHandler(
            "/api/v1/influxdb/config",
            [](AsyncWebServerRequest *request, JsonVariant &json)
            {
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_INFLUXDB))  { return; }

                JsonDocument doc;
                doc.set(json);

                if (InfluxDbClient::setConfigurationFromJson(doc))
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
            if (!_validateRequest(request, "POST")) { return; }

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
                if (!_validateRequest(request, "PUT", HTTP_MAX_CONTENT_LENGTH_LED_BRIGHTNESS)) { return; }

                JsonDocument doc;
                doc.set(json);

                // Check if brightness field is provided and is a number
                if (!doc["brightness"].is<unsigned int>()) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Missing or invalid brightness parameter");
                    return;
                }

                unsigned int brightness = doc["brightness"].as<unsigned int>();

                // Validate brightness range
                if (brightness > DEFAULT_LED_BRIGHTNESS_PERCENT) {
                    _sendErrorResponse(request, HTTP_CODE_BAD_REQUEST, "Brightness value out of range");
                    return;
                }

                // Set the brightness
                Led::setBrightness(brightness);
                _sendSuccessResponse(request, "LED brightness updated successfully");
            });
        server.addHandler(setLedBrightnessHandler);
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
//                     request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel not active\"}");
//                 }
//             } else {
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel index out of range\"}");
//             }
//         } else {
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing index parameter\"}");
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
//                     request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel not active\"}");
//                 }
//             } else {
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel index out of range\"}");
//             }
//         } else {
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing index parameter\"}");
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
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"error\":\"Channel 0 cannot be disabled\"}");
//                 return;
//             }
//         }

//         _lastChannelUpdateTime = currentTime;
//         bool success = _ade7953.setChannelData(doc);
//         _releaseMutex(_channelMutex, "channel");
//         request->send(success ? 200 : HTTP_CODE_BAD_REQUEST, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
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
//         request->send(success ? 200 : HTTP_CODE_BAD_REQUEST, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setGeneralConfigHandler->setMethod(HTTP_POST);
//     _setGeneralConfigHandler->addMiddleware(&_apiRateLimitMiddleware);
//     _server.addHandler(_setGeneralConfigHandler);

//     // Add other POST handlers
//     _setCalibrationHandler = new AsyncCallbackJsonWebHandler("/rest/set-calibration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setCalibrationValues(doc);
//         request->send(success ? 200 : HTTP_CODE_BAD_REQUEST, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setCalibrationHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setCalibrationHandler);

//     _setAdeConfigHandler = new AsyncCallbackJsonWebHandler("/rest/set-ade7953-configuration", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setConfiguration(doc);
//         request->send(success ? 200 : HTTP_CODE_BAD_REQUEST, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
//     });
//     _setAdeConfigHandler->setMethod(HTTP_POST);
//     _server.addHandler(_setAdeConfigHandler);

//     _setEnergyHandler = new AsyncCallbackJsonWebHandler("/rest/set-energy", [this](AsyncWebServerRequest* request, JsonVariant& json) {
//         JsonDocument doc;
//         doc.set(json);
//         bool success = _ade7953.setEnergyValues(doc);
//         request->send(success ? 200 : HTTP_CODE_BAD_REQUEST, "application/json", success ? "{\"message\":\"OK\"}" : "{\"error\":\"Invalid data\"}");
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
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing filename or data\"}");
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
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid type parameter\"}");
//                 return;
//             }
//             request->send(200, "application/json", "{\"message\":\"Success\"}");
//         } else {
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameter\"}");
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
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid parameters\"}");
//             }
//         } else {
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameters\"}");
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
//                 request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid parameters\"}");
//             }
//         } else {
//             request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameters\"}");
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
//             request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"message\":\"Unauthorized\"}");
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