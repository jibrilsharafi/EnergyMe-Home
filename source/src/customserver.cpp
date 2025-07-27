#include "customserver.h"

static const char *TAG = "customserver";

namespace CustomServer {
  static AsyncWebServer server(WEBSERVER_PORT);
  static AsyncAuthenticationMiddleware digestAuth;
  static AsyncRateLimitMiddleware rateLimit;
  static AsyncLoggingMiddleware requestLogger;

  static void _setupMiddleware();
  static void _serveStaticContent();
  static void _serveApi();


  void begin() {
    logger.debug("Setting up web server...", TAG);

    _setupMiddleware();
    _serveStaticContent();
    _serveApi();

    server.begin();

    logger.info("Web server started on port %d", TAG, WEBSERVER_PORT);
  }

  void _setupMiddleware() {
        // ---- Authentication Middleware Setup ----
    // Configure digest authentication (more secure than basic auth)
    digestAuth.setUsername(WEBSERVER_DEFAULT_USERNAME);
    
    // Load password from Preferences or use default
    char webPassword[AUTH_PASSWORD_BUFFER_SIZE];
    if (PreferencesConfig::getWebPassword(webPassword, sizeof(webPassword))) 
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
        if (PreferencesConfig::setWebPassword(WEBSERVER_DEFAULT_PASSWORD)) {
            logger.debug("Default password saved to Preferences for future use", TAG);
        }
    }
    
    digestAuth.setRealm(WEBSERVER_REALM);
    digestAuth.setAuthFailureMessage("The password is incorrect. Please try again.");
    digestAuth.setAuthType(AsyncAuthType::AUTH_DIGEST);
    digestAuth.generateHash();  // precompute hash for better performance
    
    server.addMiddleware(&digestAuth);

    logger.debug("Digest authentication configured", TAG);

    // ---- Rate Limiting Middleware Setup ----
    // Set rate limiting to prevent abuse
    rateLimit.setMaxRequests(WEBSERVER_MAX_REQUESTS);
    rateLimit.setWindowSize(WEBSERVER_WINDOW_SIZE_SECONDS);

    server.addMiddleware(&rateLimit);

    logger.debug("Rate limiting configured: max requests = %d, window size = %d seconds", TAG, WEBSERVER_MAX_REQUESTS, WEBSERVER_WINDOW_SIZE_SECONDS);

    // ---- Logging Middleware Setup ----
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

  void updateAuthPassword() {
    char webPassword[AUTH_PASSWORD_BUFFER_SIZE];
    if (PreferencesConfig::getWebPassword(webPassword, sizeof(webPassword))) {
        digestAuth.setPassword(webPassword);
        digestAuth.generateHash();  // regenerate hash with new password
        logger.info("Authentication password updated", TAG);
    } else {
        logger.error("Failed to load new password for authentication", TAG);
    }
  }

  void _serveApi() {

    // === API ENDPOINTS (requires authentication) ===
    server.on("/api/v1/system", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        
        // Get both static and dynamic info
        JsonDocument staticDoc, dynamicDoc;
        getJsonDeviceStaticInfo(staticDoc);
        getJsonDeviceDynamicInfo(dynamicDoc);

        // Combine into a single response
        doc["static"] = staticDoc;
        doc["dynamic"] = dynamicDoc;
        
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on("/api/v1/statistics", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        
        statisticsToJson(statistics, doc);
        
        serializeJson(doc, *response);
        request->send(response);
    });

    server.on("/api/v1/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        doc["status"] = "ok";
        doc["uptime"] = millis();
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["timestamp"] = millis();
        serializeJson(doc, *response);
        request->send(response);
    });

    // === AUTHENTICATION ENDPOINTS ===
    
    // Check authentication status
    server.on("/api/v1/auth/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        
        // Check if using default password
        char currentPassword[AUTH_PASSWORD_BUFFER_SIZE];
        bool isDefault = true;
        if (PreferencesConfig::getWebPassword(currentPassword, sizeof(currentPassword))) {
            isDefault = (strcmp(currentPassword, WEBSERVER_DEFAULT_PASSWORD) == 0);
        }
        
        doc["usingDefaultPassword"] = isDefault;
        doc["defaultUsername"] = WEBSERVER_DEFAULT_USERNAME;
        doc["authRequired"] = true;
        
        serializeJson(doc, *response);
        request->send(response);
    });

    // Change password (requires current password)
    server.on("/api/v1/auth/change-password", HTTP_POST, [](AsyncWebServerRequest *request) {
        // This endpoint should be protected by authentication middleware
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static String jsonData;
        
        if (index == 0) {
            jsonData = "";
        }
        
        for (size_t i = 0; i < len; i++) {
            jsonData += (char)data[i];
        }
        
        if (index + len == total) {
            // Parse JSON
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, jsonData);
            
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }
            
            const char* currentPassword = doc["currentPassword"];
            const char* newPassword = doc["newPassword"];
            
            if (!currentPassword || !newPassword) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing currentPassword or newPassword\"}");
                return;
            }
            
            // Validate current password
            char storedPassword[AUTH_PASSWORD_BUFFER_SIZE];
            if (!PreferencesConfig::getWebPassword(storedPassword, sizeof(storedPassword))) {
                request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to retrieve current password\"}");
                return;
            }
            
            if (strcmp(currentPassword, storedPassword) != 0) {
                request->send(401, "application/json", "{\"success\":false,\"message\":\"Current password is incorrect\"}");
                return;
            }
            
            // Validate and save new password
            if (!PreferencesConfig::setWebPassword(newPassword)) {
                request->send(400, "application/json", "{\"success\":false,\"message\":\"New password does not meet requirements or failed to save\"}");
                return;
            }
            
            logger.info("Password changed successfully via API", TAG);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Password changed successfully\"}");

            vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for the response to be sent before updating the password

            // Update authentication middleware with new password (but after sending response otherwise the client will not receive it)
            updateAuthPassword();
            
            jsonData = "";
        }
    });

    // Reset password to default (admin operation)
    server.on("/api/v1/auth/reset-password", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (PreferencesConfig::resetWebPassword()) {
            // Update authentication middleware with new password
            updateAuthPassword();
            
            logger.warning("Password reset to default via API", TAG);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Password reset to default\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to reset password\"}");
        }
    });

    // === OTA UPDATE ENDPOINTS ===
    
    // Firmware upload endpoint with integrated MD5 support
    server.on("/api/v1/ota/upload", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
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
                
                // Flash red LED pattern for error
                Led::setOff(true);
                Led::unblock();
                for (int i = 0; i < 3; i++) {
                    Led::setRed(true);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    Led::setOff(true);
                    vTaskDelay(pdMS_TO_TICKS(500));
                }
            } else {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                JsonDocument doc;
                doc["success"] = true;
                doc["message"] = "Firmware update completed successfully";
                doc["md5"] = Update.md5String();
                serializeJson(doc, *response);
                request->send(response);
                
                logger.info("OTA update completed successfully", TAG);
                logger.info("New firmware MD5: %s", TAG, Update.md5String().c_str());
                
                // Set firmware status for testing
                if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) {
                    logger.error("Failed to set firmware status for testing", TAG);
                }
                
                // Schedule restart
                setRestartEsp32(TAG, "Restart needed after firmware update");
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // Handle firmware upload chunks
            static bool otaInitialized = false;
            
            if (!index) {
                // First chunk - initialize OTA
                logger.info("Starting OTA update with file: %s", TAG, filename.c_str());
                
                // Validate file extension
                if (!filename.endsWith(".bin")) {
                    logger.error("Invalid file type. Only .bin files are supported", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"File must be in .bin format\"}");
                    return;
                }
                
                // Get content length from header
                size_t contentLength = request->header("Content-Length").toInt();
                if (contentLength == 0) {
                    logger.error("No Content-Length header found", TAG);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing Content-Length header\"}");
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
                
                // Start LED indication
                Led::block();
                Led::setPurple(true);
                
                // Begin OTA update with known size
                if (!Update.begin(contentLength, U_FLASH)) {
                    logger.error("Failed to begin OTA update: %s", TAG, Update.errorString());
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to begin update\"}");
                    
                    // Restore LED and resume operations
                    Led::setOff(true);
                    Led::unblock();
                    // _ade7953.resumeMeterReadingTask(); // Uncomment if you have this method
                    return;
                }
                
                // Check for MD5 header and set it if provided
                String md5Header = request->header("X-MD5");
                if (md5Header.length() == 32) {
                    // Convert to lowercase
                    md5Header.toLowerCase();
                    Update.setMD5(md5Header.c_str());
                    logger.info("MD5 verification enabled: %s", TAG, md5Header.c_str());
                } else if (md5Header.length() > 0) {
                    logger.warning("Invalid MD5 length (%d), skipping verification", TAG, md5Header.length());
                }
                
                otaInitialized = true;
                logger.info("OTA update started, expected size: %zu bytes", TAG, contentLength);
            }
            
            // Write chunk to flash
            if (len && otaInitialized) {
                size_t written = Update.write(data, len);
                if (written != len) {
                    logger.error("OTA write failed: expected %zu bytes, wrote %zu bytes", TAG, len, written);
                    request->send(400, "application/json", "{\"success\":false,\"message\":\"Write failed\"}");
                    Update.abort();
                    otaInitialized = false;
                    
                    // Restore LED and resume operations
                    Led::setOff(true);
                    Led::unblock();
                    // _ade7953.resumeMeterReadingTask(); // Uncomment if you have this method
                    return;
                }
                
                // Log progress every 32KB
                if (index % 32768 == 0 && index > 0) {
                    float progress = (float)Update.progress() / Update.size() * 100.0;
                    logger.debug("OTA progress: %.1f%% (%zu / %zu bytes)", TAG, progress, Update.progress(), Update.size());
                }
            }
            
            // Final chunk - complete the update
            if (final && otaInitialized) {
                logger.info("Finalizing OTA update...", TAG);
                
                if (!Update.end(true)) {
                    logger.error("OTA finalization failed: %s", TAG, Update.errorString());
                    // Error response will be handled in the main handler above
                    
                    // Restore operations on error
                    // _ade7953.resumeMeterReadingTask(); // Uncomment if you have this method
                } else {
                    logger.info("OTA update finalization successful", TAG);
                    
                    // Show success LED pattern
                    Led::setGreen(true);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                
                // Restore LED
                Led::setOff(true);
                Led::unblock();
                otaInitialized = false;
            }
        }
    );
    
    // Get OTA update status
    server.on("/api/v1/ota/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        
        if (Update.isRunning()) {
            doc["status"] = "running";
            doc["size"] = Update.size();
            doc["progress"] = Update.progress();
            doc["remaining"] = Update.remaining();
            doc["progressPercent"] = Update.size() > 0 ? (float)Update.progress() / Update.size() * 100.0 : 0.0;
        } else {
            doc["status"] = "idle";
            doc["canRollback"] = Update.canRollBack();
            if (Update.hasError()) {
                doc["lastError"] = Update.errorString();
            }
        }
        
        // Add current firmware info
        doc["currentVersion"] = FIRMWARE_BUILD_VERSION;
        doc["currentMD5"] = ESP.getSketchMD5();
        doc["firmwareState"] = CrashMonitor::getFirmwareStatusString(CrashMonitor::getFirmwareStatus());
        
        serializeJson(doc, *response);
        request->send(response);
    });

    // OTA rollback endpoint
    server.on("/api/v1/ota/rollback", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (Update.isRunning()) {
            Update.abort();
            logger.info("Aborted running OTA update", TAG);
        }

        if (Update.canRollBack()) {
            logger.warning("Performing firmware rollback", TAG);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Rollback initiated. Device will restart.\"}");

            vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for response to be sent
            Update.rollBack();
            ESP.restart();
        } else {
            logger.error("Rollback not possible: %s", TAG, Update.errorString());
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Rollback not possible\"}");
        }
    });
  }
}

// CustomServer::CustomServer(
//     AsyncWebServer &server,
//     AdvancedLogger &logger,
//     Ade7953 &ade7953,
//     CustomWifi &customWifi,
//     CustomMqtt &customMqtt,
//     InfluxDbClient &influxDbClient,
//     ButtonHandler &buttonHandler) : _server(server),
//                                     _logger(logger),
//                                     _ade7953(ade7953),
//                                     _customWifi(customWifi),
//                                     _customMqtt(customMqtt),
//                                     _influxDbClient(influxDbClient),
//                                     _buttonHandler(buttonHandler) {}

// CustomServer::~CustomServer()
// {
//     // Clean up semaphores
//     if (_configurationMutex != NULL) {
//         vSemaphoreDelete(_configurationMutex);
//         _configurationMutex = NULL;
//     }
//     if (_channelMutex != NULL) {
//         vSemaphoreDelete(_channelMutex);
//         _channelMutex = NULL;
//     }
//     if (_otaMutex != NULL) {
//         vSemaphoreDelete(_otaMutex);
//         _otaMutex = NULL;
//     }
// }

// void CustomServer::begin()
// {
//     // Initialize semaphores for concurrency control
//     _configurationMutex = xSemaphoreCreateMutex();
//     _channelMutex = xSemaphoreCreateMutex();
//     _otaMutex = xSemaphoreCreateMutex();
    
//     if (_configurationMutex == NULL || _channelMutex == NULL || _otaMutex == NULL) {
//         _logger.error("Failed to create semaphores for concurrency control", TAG);
//     }

//     // // Refactored: Initialize Authentication Middleware
//     // // This protects all endpoints by default unless explicitly skipped.
//     // _authMiddleware.setUsername(DEFAULT_WEB_USERNAME);
//     // _authMiddleware.setPassword(DEFAULT_WEB_PASSWORD); // For initial setup. Should be updated dynamically.
//     // _authMiddleware.setAuthType(AsyncAuthType::AUTH_BASIC);
//     // _authMiddleware.setRealm("EnergyMe Login");
//     // _server.addMiddleware(&_authMiddleware);

//     // // Refactored: Initialize Rate Limiting Middleware for sensitive APIs
//     // _apiRateLimitMiddleware.setWindowSize(1); // in seconds
//     // _apiRateLimitMiddleware.setMaxRequests(5); // max requests per window

//     // // Refactored: Initialize Logging Middleware
//     // _loggingMiddleware.setEnabled(true);
//     // _loggingMiddleware.setOutput(Serial); // Or any other Print object
//     // _server.addMiddleware(&_loggingMiddleware);

//     // // Setup all handlers
//     // _setStaticContent();
//     // _setApiHandlers();
//     // _setOtaHandlers();
//     // _setNotFoundHandler();

//     // _server.begin();

//     // Update.onProgress([](size_t progress, size_t total) {});

//     // initializeAuthentication();
//     // initializeRateLimiting();
// }

// void CustomServer::_setStaticContent()
// {
//     // --- Unauthenticated HTML, CSS, JS, and other static content ---
//     // Refactored: All these handlers skip the global authentication middleware.
//     _server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)login_html);
//     }).setSkipServerMiddlewares(true);

//     _server.on("/css/styles.css", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/css", (const char*)styles_css);
//     }).setSkipServerMiddlewares(true);
    
//     _server.on("/css/button.css", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/css", (const char*)button_css);
//     }).setSkipServerMiddlewares(true);

//     _server.on("/css/section.css", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/css", (const char*)section_css);
//     }).setSkipServerMiddlewares(true);

//     _server.on("/css/typography.css", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/css", (const char*)typography_css);
//     }).setSkipServerMiddlewares(true);

//     _server.on("/html/auth.js", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "application/javascript", (const char*)auth_js);
//     }).setSkipServerMiddlewares(true);

//     _server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "image/svg+xml", (const char*)favicon_svg);
//     }).setSkipServerMiddlewares(true);
    
//     // --- Authenticated HTML Pages ---
//     // These will be protected by the global _authMiddleware.
//     _server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)index_html);
//     });
//     _server.on("/configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)configuration_html);
//     });
//     _server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)calibration_html);
//     });
//     _server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)channel_html);
//     });
//     _server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)info_html);
//     });
//     _server.on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)log_html);
//     });
//     _server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)update_html);
//     });

//     // Swagger UI
//     _server.on("/swagger-ui", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/html", (const char*)swagger_ui_html);
//     });
//     _server.on("/swagger.yaml", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "text/yaml", (const char*)swagger_yaml);
//     });
// }

// void CustomServer::_setApiHandlers()
// {
//     // --- Authentication API (Unauthenticated) ---
//     _setupAuthApi();

//     // --- General GET APIs (Authenticated by default) ---
//     _setupGetApi();

//     // --- Configuration POST/PUT APIs (Authenticated, with JSON handlers) ---
//     _setupPostApi();
// }

// void CustomServer::_setupAuthApi() {
//     // Refactored: Use AsyncCallbackJsonWebHandler for login
//     _loginHandler = new AsyncCallbackJsonWebHandler("/rest/auth/login", [this](AsyncWebServerRequest *request, JsonVariant &json) {
//         // Refactored: Large buffers are now static to avoid stack overflow.
//         static char responseBuffer[JSON_RESPONSE_BUFFER_SIZE];
//         static char cookieValue[AUTH_TOKEN_LENGTH + 1 + 50];
        
//         JsonDocument doc;
//         doc.set(json);

//         char clientIp[OCTET_BUFFER_SIZE];
//         snprintf(clientIp, sizeof(clientIp), "%s", request->client()->remoteIP().toString().c_str());

//         if (isIpBlocked(clientIp)) {
//             request->send(429, "application/json", "{\"success\":false,\"message\":\"Too many failed login attempts.\"}");
//             return;
//         }

//         if (validatePassword(doc["password"].as<const char*>())) {
//             recordSuccessfulLogin(clientIp);
//             if (!canAcceptMoreTokens()) {
//                 request->send(429, "application/json", "{\"success\":false,\"message\":\"Maximum concurrent sessions reached.\"}");
//             } else {
//                 char token[AUTH_TOKEN_LENGTH + 1];
//                 generateAuthToken(token, sizeof(token));
                
//                 JsonDocument response;
//                 response["success"] = true;
//                 response["token"] = token;
                
//                 safeSerializeJson(response, responseBuffer, sizeof(responseBuffer));
//                 snprintf(cookieValue, sizeof(cookieValue), "auth_token=%s; Path=/; Max-Age=86400; HttpOnly", token);
                
//                 AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", responseBuffer);
//                 resp->addHeader("Set-Cookie", cookieValue);
//                 request->send(resp);
//             }
//         } else {
//             recordFailedLogin(clientIp);
//             request->send(401, "application/json", "{\"success\":false,\"message\":\"Invalid password\"}");
//         }
//     });
//     _loginHandler->setMethod(HTTP_POST);
//     _loginHandler->setSkipServerMiddlewares(true); // Allow access without auth
//     _server.addHandler(_loginHandler);

//     // Logout endpoint
//     _server.on("/rest/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         // This endpoint is protected by the global auth middleware
//         if (request->hasHeader("Authorization")) {
//             const char* authHeader = request->getHeader("Authorization")->value().c_str();
//             if (strncmp(authHeader, "Bearer ", 7) == 0) {
//                 clearAuthToken(authHeader + 7);
//             }
//         }
//         AsyncWebServerResponse *resp = request->beginResponse(200, "application/json", "{\"success\":true}");
//         resp->addHeader("Set-Cookie", "auth_token=; Path=/; Max-Age=0; HttpOnly");
//         request->send(resp);
//     });

//     // Change password endpoint
//     _changePasswordHandler = new AsyncCallbackJsonWebHandler("/rest/auth/change-password", [this](AsyncWebServerRequest *request, JsonVariant &json) {
//         JsonDocument doc;
//         doc.set(json);
//         const char* currentPassword = doc["currentPassword"];
//         const char* newPassword = doc["newPassword"];
        
//         if (!validatePassword(currentPassword)) {
//             request->send(401, "application/json", "{\"success\":false,\"message\":\"Current password is incorrect\"}");
//         } else if (strlen(newPassword) < MIN_PASSWORD_LENGTH || strlen(newPassword) > MAX_PASSWORD_LENGTH) {
//             request->send(400, "application/json", "{\"success\":false,\"message\":\"New password has invalid length\"}");
//         } else if (setAuthPassword(newPassword)) {
//             request->send(200, "application/json", "{\"success\":true,\"message\":\"Password changed successfully\"}");
//         } else {
//             request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to change password\"}");
//         }
//     });
//     _changePasswordHandler->setMethod(HTTP_POST);
//     _server.addHandler(_changePasswordHandler);

//     // Auth status endpoint (Unauthenticated)
//     _server.on("/rest/auth/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char responseBuffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument response;
//         response["authenticated"] = _authMiddleware.allowed(request);
//         response["username"] = DEFAULT_WEB_USERNAME;
//         response["activeSessions"] = getActiveTokenCount();
//         response["maxSessions"] = MAX_CONCURRENT_SESSIONS;
//         response["mustChangePassword"] = isUsingDefaultPassword();
        
//         safeSerializeJson(response, responseBuffer, sizeof(responseBuffer));
//         request->send(200, "application/json", responseBuffer);
//     }).setSkipServerMiddlewares(true);
// }

// void CustomServer::_setupGetApi() {
//     _server.on("/rest/is-alive", HTTP_GET, [](AsyncWebServerRequest *request) {
//         request->send(200, "application/json", "{\"message\":\"True\"}");
//     }).setSkipServerMiddlewares(true); // Publicly accessible

//     _server.on("/rest/product-info", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         getJsonProductInfo(doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/device-info", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         getJsonDeviceInfo(doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/wifi-info", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         static char buffer[JSON_RESPONSE_BUFFER_SIZE];
//         JsonDocument doc;
//         _customWifi.getWifiInfoJson(doc);
//         safeSerializeJson(doc, buffer, sizeof(buffer));
//         request->send(200, "application/json", buffer);
//     });

//     _server.on("/rest/meter", HTTP_GET, [this](AsyncWebServerRequest *request) {
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
//         unsigned long currentTime = millis();
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
//         unsigned long currentTime = millis();
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

// void CustomServer::_setOtaHandlers()
// {
//     // The OTA handler remains largely the same, but it is now protected by the global auth middleware.
//     _server.on("/do-update", HTTP_POST, 
//         [](AsyncWebServerRequest *request){}, // On success (empty, handled by upload handler)
//         [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
//             _handleDoUpdate(request, filename.c_str(), index, data, len, final);
//         }
//     );
    
//     _server.on("/set-md5", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (request->hasParam("md5")) {
//             char md5Str[MD5_BUFFER_SIZE];
//             snprintf(md5Str, sizeof(md5Str), "%s", request->getParam("md5")->value().c_str());
            
//             // Convert to lowercase
//             for (int i = 0; md5Str[i]; i++) {
//                 md5Str[i] = tolower(md5Str[i]);
//             }
            
//             if (strlen(md5Str) != 32) {
//                 request->send(400, "application/json", "{\"message\":\"MD5 not 32 characters long\"}");
//             } else {
//                 snprintf(_md5, sizeof(_md5), "%s", md5Str);
//                 request->send(200, "application/json", "{\"message\":\"MD5 set\"}");
//             }
//         } else {
//             request->send(400, "application/json", "{\"message\":\"Missing MD5 parameter\"}");
//         }
//     });

//     _server.on("/rest/update-status", HTTP_GET, [this](AsyncWebServerRequest *request) {
//         if (Update.isRunning()) {
//             static char statusResponse[JSON_RESPONSE_BUFFER_SIZE];
//             snprintf(statusResponse, sizeof(statusResponse), 
//                      "{\"status\":\"running\",\"size\":%zu,\"progress\":%zu,\"remaining\":%zu}",
//                      Update.size(), Update.progress(), Update.remaining());
//             request->send(200, "application/json", statusResponse);
//         } else {
//             request->send(200, "application/json", "{\"status\":\"idle\"}");
//         }
//     });

//     _server.on("/rest/update-rollback", HTTP_POST, [this](AsyncWebServerRequest *request) {
//         if (Update.isRunning()) {
//             Update.abort();
//         }

//         if (Update.canRollBack()) {
//             Update.rollBack();
//             request->send(200, "application/json", "{\"message\":\"Rollback in progress. Restarting ESP32...\"}");
//             setRestartEsp32(TAG, "Firmware rollback in progress requested from REST API");
//         } else {
//             _logger.error("Rollback not possible. Reason: %s", TAG, Update.errorString());
//             request->send(500, "application/json", "{\"message\":\"Rollback not possible\"}");
//         }
//     });
// }

// void CustomServer::_setNotFoundHandler() {
//     _server.onNotFound([](AsyncWebServerRequest *request) {
//         request->send(404, "text/plain", "Not found");
//     });
// }

// void CustomServer::_handleDoUpdate(AsyncWebServerRequest *request, const char* filename, size_t index, uint8_t *data, size_t len, bool final)
// {
//     // Protect OTA updates with mutex - only allow one at a time
//     if (!index) { // First chunk of upload
//         if (!_acquireMutex(_otaMutex, "ota", pdMS_TO_TICKS(1000))) {
//             _logger.warning("OTA update rejected - another update in progress", TAG);
//             _onUpdateFailed(request, "Another firmware update is already in progress");
//             return;
//         }
//     }

//     Led::block();
//     Led::setPurple(true);

//     
//     if (!index)
//     {
//         if (strstr(filename, ".bin") != nullptr)
//         {
//             _logger.info("Update requested for firmware", TAG);
//         }
//         else
//         {
//             _onUpdateFailed(request, "File must be in .bin format");
//             return;
//         }
        
//         _ade7953.pauseMeterReadingTask();
    
//         size_t freeHeap = ESP.getFreeHeap();
//         _logger.debug("Free heap before OTA: %zu bytes", TAG, freeHeap);
        
//         if (freeHeap < MINIMUM_FREE_HEAP_OTA) {
//             _onUpdateFailed(request, "Insufficient memory for update");
//             return;
//         }

//         if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
//         {           
//             _onUpdateFailed(request, Update.errorString());
//             return;
//         }

//         Update.setMD5(_md5);
//     }    

//     
//     if (Update.write(data, len) != len)
//     {
//         _onUpdateFailed(request, Update.errorString());
//         return;
//     }    
    
//     
//     if (final)
//     {
//         if (!Update.end(true))
//         {   
//             _onUpdateFailed(request, Update.errorString());
//         }
//         else
//         {
//             _onUpdateSuccessful(request);
//         }
//     }

//     
//     Led::setOff(true);
//     Led::unblock();
// }

// void CustomServer::_onUpdateSuccessful(AsyncWebServerRequest *request)
// {
//     
//     // Refactored: Using a static buffer to prevent stack overflow
//     static char response[HTTP_RESPONSE_BUFFER_SIZE];
//     snprintf(response, sizeof(response), "{\"status\":\"success\", \"md5\":\"%s\"}", Update.md5String().c_str());
//     request->send(200, "application/json", response);

//     
//     _logger.info("Update complete", TAG);
//     updateJsonFirmwareStatus("success", "");

//     _logger.debug("MD5 of new firmware: %s", TAG, Update.md5String().c_str());

//     
//     char firmwareStatus[FIRMWARE_STATUS_BUFFER_SIZE];
//     CrashMonitor::getFirmwareStatusString(NEW_TO_TEST, firmwareStatus);
//     _logger.debug("Setting rollback flag to %s", TAG, firmwareStatus);
    
//     if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) _logger.error("Failed to set firmware status", TAG);

//     
//     setRestartEsp32(TAG, "Restart needed after update");
//     _releaseMutex(_otaMutex, "ota");
// }

// void CustomServer::_onUpdateFailed(AsyncWebServerRequest *request, const char *reason)
// {
//     
      
//     _ade7953.resumeMeterReadingTask();
//     _logger.debug("Reattached ADE7953 interrupt after OTA failure", TAG);
    
//     // Refactored: Using a static buffer to prevent stack overflow
//     static char response[HTTP_RESPONSE_BUFFER_SIZE];
//     snprintf(response, sizeof(response), "{\"status\":\"failed\", \"reason\":\"%s\"}", reason);
//     request->send(400, "application/json", response);

//     Update.printError(Serial);
//     _logger.debug("Size: %d bytes | Progress: %d bytes | Remaining: %d bytes", TAG, Update.size(), Update.progress(), Update.remaining());
//     _logger.error("Update failed, keeping current firmware. Reason: %s", TAG, reason);
//     updateJsonFirmwareStatus("failed", reason);

//     for (int i = 0; i < 3; i++)
//     {
//         Led::setRed(true);
//         delay(500);
//         Led::setOff(true);
//         delay(500);
//     }

//     _releaseMutex(_otaMutex, "ota");
// }

// // Concurrency control helper methods
// bool CustomServer::_acquireMutex(SemaphoreHandle_t mutex, const char* mutexName, TickType_t timeout)
// {
//     if (mutex == NULL) {
//         _logger.warning("Mutex %s is NULL, skipping lock", TAG, mutexName);
//         return false;
//     }
    
//     if (xSemaphoreTake(mutex, timeout) == pdTRUE) {
//         _logger.verbose("Successfully acquired mutex: %s", TAG, mutexName);
//         return true;
//     } else {
//         _logger.warning("Failed to acquire mutex %s within timeout", TAG, mutexName);
//         return false;
//     }
// }

// void CustomServer::_releaseMutex(SemaphoreHandle_t mutex, const char* mutexName)
// {
//     if (mutex == NULL) {
//         _logger.warning("Mutex %s is NULL, skipping unlock", TAG, mutexName);
//         return;
//     }
    
//     if (xSemaphoreGive(mutex) == pdTRUE) {
//         _logger.debug("Successfully released mutex: %s", TAG, mutexName);
//     } else {
//         _logger.warning("Failed to release mutex: %s", TAG, mutexName);
//     }
// }