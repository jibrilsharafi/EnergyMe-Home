#include "customserver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "customserver";

CustomServer::CustomServer(
    AsyncWebServer &server,
    AdvancedLogger &logger,
    Led &led,
    Ade7953 &ade7953,
    CustomWifi &customWifi,
    CustomMqtt &customMqtt,
    InfluxDbClient &influxDbClient,
    ButtonHandler &buttonHandler) : _server(server),
                                    _logger(logger),
                                    _led(led),
                                    _ade7953(ade7953),
                                    _customWifi(customWifi),
                                    _customMqtt(customMqtt),
                                    _influxDbClient(influxDbClient),
                                    _buttonHandler(buttonHandler) {}

CustomServer::~CustomServer()
{
    // Clean up semaphores
    if (_configurationMutex != NULL) {
        vSemaphoreDelete(_configurationMutex);
        _configurationMutex = NULL;
    }
    if (_channelMutex != NULL) {
        vSemaphoreDelete(_channelMutex);
        _channelMutex = NULL;
    }
    if (_otaMutex != NULL) {
        vSemaphoreDelete(_otaMutex);
        _otaMutex = NULL;
    }
}

void CustomServer::begin()
{
    // Initialize semaphores for concurrency control
    _configurationMutex = xSemaphoreCreateMutex();
    _channelMutex = xSemaphoreCreateMutex();
    _otaMutex = xSemaphoreCreateMutex();
    
    if (_configurationMutex == NULL || _channelMutex == NULL || _otaMutex == NULL) {
        _logger.error("Failed to create semaphores for concurrency control", TAG);
        // Continue anyway, but log the error
    }

    _setHtmlPages();
    _setOta();
    _setRestApi();
    _setOtherEndpoints();

    _server.begin();

    Update.onProgress([](size_t progress, size_t total) {});

    initializeAuthentication();    
    initializeRateLimiting();
}

void CustomServer::_serverLog(const char *message, const char *function, LogLevel logLevel, AsyncWebServerRequest *request)
{    
    char fullUrl[FULLURL_BUFFER_SIZE];
    snprintf(fullUrl, sizeof(fullUrl), "%s", request->url().c_str());
    
    if (request->args() != 0)
    {
        size_t currentLength = strlen(fullUrl);
        if (currentLength + 1 < sizeof(fullUrl)) {
            strcat(fullUrl, "?");
            currentLength++;
        }
        
        for (uint8_t i = 0; i < request->args() && currentLength < sizeof(fullUrl) - 1; i++)
        {
            if (i != 0 && currentLength + 1 < sizeof(fullUrl)) {
                strcat(fullUrl, "&");
                currentLength++;
            }
            
            char argPart[128];
            snprintf(argPart, sizeof(argPart), "%s=%s", request->argName(i).c_str(), request->arg(i).c_str());
            
            if (currentLength + strlen(argPart) < sizeof(fullUrl)) {
                strcat(fullUrl, argPart);
                currentLength += strlen(argPart);
            }
        }
    }

    if (logLevel == LogLevel::DEBUG)
    {
        _logger.debug("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl);
    }
    else if (logLevel == LogLevel::INFO)
    {
        _logger.info("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl);
    }
    else if (logLevel == LogLevel::WARNING)
    {
        _logger.warning("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl);
    }
    else if (logLevel == LogLevel::ERROR)
    {
        _logger.error("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl);
    }
    else if (logLevel == LogLevel::FATAL)
    {
        _logger.fatal("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl);
    }
}

void CustomServer::_setHtmlPages()
{
    // Login page (no authentication required)
    _server.on("/login", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get login page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", login_html); });

    // Protected HTML pages
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;        }
        _serverLog("Request to get index page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", index_html); });

    _server.on("/configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
                }
        _serverLog("Request to get configuration page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", configuration_html); });

    _server.on("/calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
        }
        _serverLog("Request to get calibration page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", calibration_html); });

    _server.on("/channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
        }
        _serverLog("Request to get channel page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", channel_html); });

    _server.on("/info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
        }
        _serverLog("Request to get info page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", info_html); });

    _server.on("/log", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
        }
        _serverLog("Request to get log page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", log_html); });

    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            AsyncWebServerResponse *response = request->beginResponse(302);
            response->addHeader("Location", "/login");
            request->send(response);
            return;
        }        _serverLog("Request to get update page", TAG, LogLevel::DEBUG, request);
        request->send_P(HTTP_CODE_OK, "text/html", update_html); });

    // CSS
    _server.on("/css/styles.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get custom CSS", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/css", styles_css); });

    _server.on("/css/button.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get custom CSS", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/css", button_css); });

    _server.on("/css/section.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get custom CSS", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/css", section_css); });

    _server.on("/css/typography.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get custom CSS", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/css", typography_css); });

    // Swagger UI
    _server.on("/swagger-ui", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get Swagger UI", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/html", swagger_ui_html); });

    _server.on("/swagger.yaml", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get Swagger YAML", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "text/yaml", swagger_yaml); }); // Favicon - SVG format seems to be the only one working with embedded binary data
    _server.on("/favicon.svg", HTTP_GET, [this](AsyncWebServerRequest *request)
               {        _serverLog("Request to get favicon", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "image/svg+xml", favicon_svg); });

    // JavaScript files
    _server.on("/html/auth.js", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get auth JavaScript", TAG, LogLevel::VERBOSE, request);
        request->send_P(HTTP_CODE_OK, "application/javascript", auth_js); });
}

void CustomServer::_setOta()
{
    _server.on("/do-update", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        } }, [this](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final)
               { _handleDoUpdate(request, filename.c_str(), index, data, len, final); });

    // This had to be a GET request as otherwise I could not manage to use a query
    // parameter in a POST request. This is a workaround to set the MD5 hash for the
    // firmware update. Ideally, in the same endpoint in which the firmware is uploaded
    // the MD5 hash should be sent as well as a query parameter.
    _server.on("/set-md5", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to set MD5", TAG, LogLevel::DEBUG, request);

        if (request->hasParam("md5"))
        {
            char md5Str[MD5_BUFFER_SIZE];
            snprintf(md5Str, sizeof(md5Str), "%s", request->getParam("md5")->value().c_str());
            
            // Convert to lowercase
            for (int i = 0; md5Str[i]; i++) {
                md5Str[i] = tolower(md5Str[i]);
            }
            
            _logger.debug("MD5 included in the request: %s", TAG, md5Str);

            if (strlen(md5Str) != 32)
            {
                _logger.warning("MD5 not 32 characters long. Skipping MD5 verification", TAG);
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"MD5 not 32 characters long\"}");
            }
            else
            {
                snprintf(_md5, sizeof(_md5), "%s", md5Str);
                request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"MD5 set\"}");
            }
        }
        else
        {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing MD5 parameter\"}");
        } });

    _server.on("/rest/update-status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to get update status", TAG, LogLevel::DEBUG, request);

        if (Update.isRunning())
        {
            char statusResponse[JSON_RESPONSE_BUFFER_SIZE];
            snprintf(statusResponse, sizeof(statusResponse), 
                     "{\"status\":\"running\",\"size\":%zu,\"progress\":%zu,\"remaining\":%zu}",
                     Update.size(), Update.progress(), Update.remaining());
            request->send(HTTP_CODE_OK, "application/json", statusResponse);
        }        else
        {
            request->send(HTTP_CODE_OK, "application/json", "{\"status\":\"idle\"}");
        } });

    _server.on("/rest/update-rollback", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to rollback firmware", TAG, LogLevel::WARNING, request);

        if (Update.isRunning())
        {
            Update.abort();
        }

        if (Update.canRollBack())
        {
            Update.rollBack();
            request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Rollback in progress. Restarting ESP32...\"}");
            setRestartEsp32(TAG, "Firmware rollback in progress requested from REST API");
        }
        else
        {
            _logger.error("Rollback not possible. Reason: %s", TAG, Update.errorString());
            request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"message\":\"Rollback not possible\"}");
        } });
}

void CustomServer::_setRestApi()
{
    // Authentication endpoints
    _server.on("/rest/auth/login", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
               {
        if (index == 0) {
            request->_tempObject = new std::vector<uint8_t>();
        }
        
        std::vector<uint8_t> *buffer = static_cast<std::vector<uint8_t> *>(request->_tempObject);
        buffer->insert(buffer->end(), data, data + len);
          if (index + len == total) {
            JsonDocument jsonDoc;
            deserializeJson(jsonDoc, buffer->data(), buffer->size());
            
            char clientIp[IP_ADDRESS_BUFFER_SIZE];
            snprintf(clientIp, sizeof(clientIp), "%s", request->client()->remoteIP().toString().c_str());
            
            // Check if IP is blocked due to too many failed attempts
            if (isIpBlocked(clientIp)) {
                request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", "{\"success\":false,\"message\":\"Too many failed login attempts. Please try again later.\"}");
                _serverLog("Login blocked - IP rate limited", TAG, LogLevel::WARNING, request);
                delete buffer;
                request->_tempObject = nullptr;
                return;
            }
            
            char password[PASSWORD_BUFFER_SIZE];
            snprintf(password, sizeof(password), "%s", jsonDoc["password"].as<const char*>());
              if (validatePassword(password)) {
                // Record successful login to reset rate limiting for this IP
                recordSuccessfulLogin(clientIp);
                
                // Check if we can accept more sessions
                if (!canAcceptMoreTokens()) {
                    // Maximum concurrent sessions reached
                    request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", "{\"success\":false,\"message\":\"Maximum concurrent sessions reached. Please try again later or ask another user to logout.\"}");
                    _serverLog("Login rejected - maximum sessions reached", TAG, LogLevel::WARNING, request);
                } else {
                    char token[AUTH_TOKEN_BUFFER_SIZE];
                    generateAuthToken(token, sizeof(token));
                    
                    JsonDocument response;
                    response["success"] = true;
                    response["token"] = token;
                    response["message"] = "Login successful";
                    
                    char responseBuffer[JSON_RESPONSE_BUFFER_SIZE];
                    serializeJson(response, responseBuffer, sizeof(responseBuffer));
                    
                    char cookieValue[AUTH_TOKEN_BUFFER_SIZE + 50];
                    snprintf(cookieValue, sizeof(cookieValue), "auth_token=%s; Path=/; Max-Age=86400; HttpOnly", token);
                    
                    AsyncWebServerResponse *resp = request->beginResponse(HTTP_CODE_OK, "application/json", responseBuffer);
                    resp->addHeader("Set-Cookie", cookieValue);
                    request->send(resp);
                    
                    _serverLog("Successful login", TAG, LogLevel::INFO, request);
                }
            } else {
                // Record failed login attempt for rate limiting
                recordFailedLogin(clientIp);
                
                request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"success\":false,\"message\":\"Invalid password\"}");
                _serverLog("Failed login attempt", TAG, LogLevel::WARNING, request);
            }
            
            delete buffer;
            request->_tempObject = nullptr;
        } });

    _server.on("/rest/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_checkAuth(request)) {
            // Extract token and clear it
            if (request->hasHeader("Authorization")) {
                char authHeader[AUTH_HEADER_BUFFER_SIZE];
                snprintf(authHeader, sizeof(authHeader), "%s", request->getHeader("Authorization")->value().c_str());
                if (strncmp(authHeader, "Bearer ", 7) == 0) {
                    char token[AUTH_TOKEN_BUFFER_SIZE];
                    snprintf(token, sizeof(token), "%s", authHeader + 7);
                    clearAuthToken(token);
                }
            }
            
            AsyncWebServerResponse *resp = request->beginResponse(HTTP_CODE_OK, "application/json", "{\"success\":true,\"message\":\"Logged out successfully\"}");
            resp->addHeader("Set-Cookie", "auth_token=; Path=/; Max-Age=0; HttpOnly");
            request->send(resp);
            
            _serverLog("User logged out", TAG, LogLevel::INFO, request);
        } else {
            _sendUnauthorized(request);
        } });

    _server.on("/rest/auth/change-password", HTTP_POST, [this](AsyncWebServerRequest *request) {}, NULL, [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        
        if (index == 0) {
            request->_tempObject = new std::vector<uint8_t>();
        }
        
        std::vector<uint8_t> *buffer = static_cast<std::vector<uint8_t> *>(request->_tempObject);
        buffer->insert(buffer->end(), data, data + len);
        
        if (index + len == total) {
            JsonDocument jsonDoc;
            deserializeJson(jsonDoc, buffer->data(), buffer->size());
            
            char currentPassword[PASSWORD_BUFFER_SIZE];
            char newPassword[PASSWORD_BUFFER_SIZE];
            snprintf(currentPassword, sizeof(currentPassword), "%s", jsonDoc["currentPassword"].as<const char*>());
            snprintf(newPassword, sizeof(newPassword), "%s", jsonDoc["newPassword"].as<const char*>());
            
            if (!validatePassword(currentPassword)) {
                request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"success\":false,\"message\":\"Current password is incorrect\"}");
            } else if (strlen(newPassword) < MIN_PASSWORD_LENGTH || strlen(newPassword) > MAX_PASSWORD_LENGTH) {
                char errorMessage[JSON_RESPONSE_BUFFER_SIZE];
                snprintf(errorMessage, sizeof(errorMessage), 
                         "{\"success\":false,\"message\":\"New password must be between %d and %d characters\"}", 
                         MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH);
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", errorMessage);
            } else if (setAuthPassword(newPassword)) {
                request->send(HTTP_CODE_OK, "application/json", "{\"success\":true,\"message\":\"Password changed successfully\"}");
                _serverLog("Password changed", TAG, LogLevel::INFO, request);
            } else {
                request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"success\":false,\"message\":\"Failed to change password\"}");
            }
            
            delete buffer;
            request->_tempObject = nullptr;
        } });    
        
        _server.on("/rest/auth/status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        JsonDocument response;
        bool isAuthenticated = _checkAuth(request);
        response["authenticated"] = isAuthenticated;
        response["username"] = DEFAULT_WEB_USERNAME;
        
        // Add session management information
        response["activeSessions"] = getActiveTokenCount();
        response["maxSessions"] = MAX_CONCURRENT_SESSIONS;
        response["canAcceptMoreSessions"] = canAcceptMoreTokens();
        
        // Check if user must change default password
        if (isAuthenticated) {
            response["mustChangePassword"] = isUsingDefaultPassword();
        }
        
        char responseBuffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(response, responseBuffer, sizeof(responseBuffer));
        request->send(HTTP_CODE_OK, "application/json", responseBuffer); });

    _server.on("/rest/is-alive", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the ESP32 is alive", TAG, LogLevel::DEBUG, request);

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"True\"}"); });

    _server.on("/rest/product-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get product info", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonProductInfo(_jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/device-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get device info", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonDeviceInfo(_jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/wifi-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get WiFi values", TAG, LogLevel::DEBUG, request);
        
        JsonDocument _jsonDocument;
        _customWifi.getWifiStatus(_jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/meter", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.fullMeterValuesToJson(_jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/meter-single", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values single", TAG, LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt < CHANNEL_COUNT) {
                ChannelNumber _channel = static_cast<ChannelNumber>(_indexInt);
                if (_ade7953.channelData[_channel].active) {
                    JsonDocument jsonDocument;
                    _ade7953.singleMeterValuesToJson(jsonDocument, _channel);

                    char _buffer[JSON_RESPONSE_BUFFER_SIZE];
                    serializeJson(jsonDocument, _buffer, sizeof(_buffer));

                    request->send(HTTP_CODE_OK, "application/json", _buffer);
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel not active\"}");
                }
            } else {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel index out of range\"}");
            }
        } else {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing index parameter\"}");
        } });

    _server.on("/rest/active-power", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get active power", TAG, LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (_ade7953.channelData[_indexInt].active) {
                    char response[JSON_RESPONSE_BUFFER_SIZE];
                    snprintf(response, sizeof(response), "{\"value\":%.6f}", _ade7953.meterValues[_indexInt].activePower);
                    request->send(HTTP_CODE_OK, "application/json", response);
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel not active\"}");
                }
            } else {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Channel index out of range\"}");
            }
        } else {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing index parameter\"}");
        } });

    _server.on("/rest/get-ade7953-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get ADE7953 configuration", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, CONFIGURATION_ADE7953_JSON_PATH); });

    _server.on("/rest/get-channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get channel data", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.channelDataToJson(_jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/get-calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get configuration", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, CALIBRATION_JSON_PATH); });

    _server.on("/rest/calibration-reset", HTTP_POST, [&](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to reset calibration values", TAG, LogLevel::WARNING, request);
        
        _ade7953.setDefaultCalibrationValues();
        _ade7953.setDefaultChannelData();

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Calibration values reset\"}"); });

    _server.on("/rest/reset-energy", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to reset energy counters", TAG, LogLevel::WARNING, request);

        _ade7953.resetEnergyValues();

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Energy counters reset\"}"); });

    _server.on("/rest/get-energy", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get energy values", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            if (_ade7953.channelData[i].active) {
                _jsonDocument[i]["activeEnergyImported"] = _ade7953.meterValues[i].activeEnergyImported;
                _jsonDocument[i]["activeEnergyExported"] = _ade7953.meterValues[i].activeEnergyExported;
                _jsonDocument[i]["reactiveEnergyImported"] = _ade7953.meterValues[i].reactiveEnergyImported;
                _jsonDocument[i]["reactiveEnergyExported"] = _ade7953.meterValues[i].reactiveEnergyExported;
                _jsonDocument[i]["apparentEnergy"] = _ade7953.meterValues[i].apparentEnergy;
            }
        }

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/get-log-level", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log level", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["print"] = _logger.logLevelToString(_logger.getPrintLevel());
        _jsonDocument["save"] = _logger.logLevelToString(_logger.getSaveLevel());

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/set-log-level", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog(
            "Request to set log level",
            TAG,
            LogLevel::DEBUG,
            request
        );

        if (request->hasParam("level") && request->hasParam("type")) {
            int _level = request->getParam("level")->value().toInt();
            char _type[32];
            snprintf(_type, sizeof(_type), "%s", request->getParam("type")->value().c_str());
            if (strcmp(_type, "print") == 0) {
                _logger.setPrintLevel(LogLevel(_level));
            } else if (strcmp(_type, "save") == 0) {
                _logger.setSaveLevel(LogLevel(_level));
            } else {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid type parameter. Supported values: print, save\"}");
            }
            request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Success\"}");
        } else {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameter. Required: level (int), type (string)\"}");
        } });

    _server.on("/rest/get-general-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get get general configuration", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        generalConfigurationToJson(generalConfiguration, _jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/get-has-secrets", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get has_secrets", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["has_secrets"] = HAS_SECRETS ? true : false;

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/ade7953-read-register", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to get ADE7953 register value",
            TAG,
            LogLevel::DEBUG,
            request
        );

        if (request->hasParam("address") && request->hasParam("nBits") && request->hasParam("signed")) {
            int _address = request->getParam("address")->value().toInt();
            int _nBits = request->getParam("nBits")->value().toInt();
            bool _signed = request->getParam("signed")->value().equalsIgnoreCase("true");

            if (_nBits == 8 || _nBits == 16 || _nBits == 24 || _nBits == 32) {
                if (_address >= 0 && _address <= 0x3FF) {
                    long registerValue = _ade7953.readRegister(_address, _nBits, _signed);

                    char response[JSON_RESPONSE_BUFFER_SIZE];
                    snprintf(response, sizeof(response), "{\"value\":%ld}", registerValue);
                    request->send(HTTP_CODE_OK, "application/json", response);
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Address out of range. Supported values: 0-0x3FF (0-1023)\"}");
                }
            } else {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Number of bits not supported. Supported values: 8, 16, 24, 32\"}");
            }
        } else {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameter. Required: address (int), nBits (int), signed (bool)\"}");
        } });

    _server.on("/rest/ade7953-write-register", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog(
            "Request to get ADE7953 register value",
            TAG,
            LogLevel::INFO,
            request
        );

        if (request->hasParam("address") && request->hasParam("nBits") && request->hasParam("data")) {
            int _address = request->getParam("address")->value().toInt();
            int _nBits = request->getParam("nBits")->value().toInt();
            int _data = request->getParam("data")->value().toInt();

            if (_nBits == 8 || _nBits == 16 || _nBits == 24 || _nBits == 32) {
                if (_address >= 0 && _address <= 0x3FF) {
                    _ade7953.writeRegister(_address, _nBits, _data);

                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Success\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Address out of range. Supported values: 0-0x3FF (0-1023)\"}");
                }
            } else {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Number of bits not supported. Supported values: 8, 16, 24, 32\"}");
            }
        } else {
            request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing parameter. Required: address (int), nBits (int), data (int)\"}");
        } });

    _server.on("/rest/firmware-update-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update info", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, FW_UPDATE_INFO_JSON_PATH); });

    _server.on("/rest/firmware-update-status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update status", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, FW_UPDATE_STATUS_JSON_PATH); });

    _server.on("/rest/is-latest-firmware-installed", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the latest firmware is installed", TAG, LogLevel::DEBUG, request);

        if (isLatestFirmwareInstalled()) {
            request->send(HTTP_CODE_OK, "application/json", "{\"latest\":true}");
        } else {
            request->send(HTTP_CODE_OK, "application/json", "{\"latest\":false}");
        } });

    _server.on("/rest/get-current-monitor-data", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get current monitor data", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        if (!CrashMonitor::getJsonReport(_jsonDocument, crashData)) {
            request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"message\":\"Error getting monitoring data\"}");
            return;
        }

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/get-crash-data", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get crash data", TAG, LogLevel::DEBUG, request);

        TRACE();
        if (!CrashMonitor::checkIfCrashDataExists()) {
            request->send(HTTP_CODE_NOT_FOUND, "application/json", "{\"message\":\"LUCKILY, no crash data is available, YET. Come back when the device goes loco.\"}");
            return;
        }

        TRACE();
        CrashData _crashData;
        if (!CrashMonitor::getSavedCrashData(_crashData)) {
            request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"message\":\"Could not get crash data\"}");
            return;
        }

        TRACE();
        JsonDocument _jsonDocument;
        if (!CrashMonitor::getJsonReport(_jsonDocument, _crashData)) {
            request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"message\":\"Could not create JSON report\"}");
            return;
        }

        TRACE();
        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        TRACE();
        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to factory reset", TAG, LogLevel::WARNING, request);

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Factory reset in progress. Check the log for more information.\"}");
        factoryReset(); });

    _server.on("/rest/clear-log", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to clear log", TAG, LogLevel::DEBUG, request);

        _logger.clearLog();

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Log cleared\"}"); });

    _server.on("/rest/restart", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to restart the ESP32", TAG, LogLevel::INFO, request);

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Restarting...\"}");
        setRestartEsp32(TAG, "Request to restart the ESP32 from REST API"); });
    _server.on("/rest/reset-wifi", HTTP_POST, [this](AsyncWebServerRequest *request) // TODO: add the possibility to set the new wifi at the same time here
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to erase WiFi credentials", TAG, LogLevel::WARNING, request);

        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
        _customWifi.resetWifi(); });

    _server.on("/rest/get-button-operation", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get last button operation", TAG, LogLevel::DEBUG, request);

        const char* operationName = _buttonHandler.getCurrentOperationName();
        unsigned long operationTimestamp = _buttonHandler.getCurrentOperationTimestamp();
        
        char response[JSON_RESPONSE_BUFFER_SIZE];
        
        if (operationTimestamp > 0) {
            char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
            CustomTime::timestampFromUnix(operationTimestamp, timestampBuffer);
            snprintf(response, sizeof(response), "{\"operationName\":\"%s\",\"operationTimestamp\":%lu,\"operationTimestampFormatted\":\"%s\"}",
                operationName, operationTimestamp, timestampBuffer);
        } else {
            snprintf(response, sizeof(response), "{\"operationName\":\"%s\",\"operationTimestamp\":%lu}",
                operationName, operationTimestamp);
        }
        
        request->send(HTTP_CODE_OK, "application/json", response); });

    _server.on("/rest/clear-button-operation", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to clear last button operation", TAG, LogLevel::DEBUG, request);

        _buttonHandler.clearCurrentOperationName();
        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Button operation history cleared\"}"); });

    _server.on("/rest/get-custom-mqtt-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom MQTT configuration", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, CUSTOM_MQTT_CONFIGURATION_JSON_PATH); });

    _server.on("/rest/get-influxdb-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get InfluxDB configuration", TAG, LogLevel::DEBUG, request);

        _serveJsonFile(request, INFLUXDB_CONFIGURATION_JSON_PATH); });

    _server.on("/rest/get-statistics", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get statistics", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        statisticsToJson(statistics, _jsonDocument);

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.onRequestBody([this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                          {
        // Check authentication for sensitive POST endpoints
        if (request->url().startsWith("/rest/set-") || 
            request->url() == "/rest/upload-file") {
            if (_requireAuth(request)) {
                _sendUnauthorized(request);
                return;
            }
        }
        
        if (index == 0) {
            // This is the first chunk of data, initialize the buffer
            request->_tempObject = new std::vector<uint8_t>();
        }
    
        // Append the current chunk to the buffer
        std::vector<uint8_t> *buffer = static_cast<std::vector<uint8_t> *>(request->_tempObject);
        buffer->insert(buffer->end(), data, data + len);
    
        if (index + len == total) {
            // All chunks have been received, process the complete data
            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, buffer->data(), buffer->size());

            char _buffer[JSON_RESPONSE_BUFFER_SIZE];
            serializeJson(_jsonDocument, _buffer, sizeof(_buffer));
            _serverLog(_buffer, TAG, LogLevel::DEBUG, request);

            if (request->url() == "/rest/set-calibration") {
                _serverLog("Request to set calibration values", TAG, LogLevel::INFO, request);
    
                if (_ade7953.setCalibrationValues(_jsonDocument)) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Calibration values set\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid calibration values\"}");
                }
    
                request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Calibration values set\"}");
    
            } else if (request->url() == "/rest/set-ade7953-configuration") {
                _serverLog("Request to set ADE7953 configuration", TAG, LogLevel::INFO, request);
    
                if (_ade7953.setConfiguration(_jsonDocument)) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid configuration\"}");
                }
    
                request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Configuration updated\"}");
    
            } else if (request->url() == "/rest/set-general-configuration") {
                _serverLog("Request to set general configuration", TAG, LogLevel::INFO, request);
                
                // Rate limiting: Check if enough time has passed since last update
                unsigned long currentTime = millis();
                if (currentTime - _lastConfigUpdateTime < API_UPDATE_THROTTLE_MS) {
                    _serverLog("Config update throttled - too frequent requests", TAG, LogLevel::WARNING, request);
                    request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", 
                        "{\"error\":\"Rate limited\", \"message\":\"Please wait before sending another request\", \"retryAfter\":500}");
                    return;
                }
                
                // Try to acquire the configuration mutex with timeout
                if (!_acquireMutex(_configurationMutex, "configuration", pdMS_TO_TICKS(2000))) {
                    _serverLog("Config update rejected - unable to acquire lock", TAG, LogLevel::WARNING, request);
                    request->send(HTTP_CODE_CONFLICT, "application/json", 
                        "{\"error\":\"Resource locked\", \"message\":\"Another configuration operation is in progress, please try again\"}");
                    return;
                }
                
                _lastConfigUpdateTime = currentTime;
    
                // Weird thing we have to do here: to ensure the generalConfiguration.sendPowerData 
                // is not settable by API, we force here to be like the existing value.
                if (_jsonDocument["sendPowerData"].is<bool>()) _jsonDocument["sendPowerData"] = generalConfiguration.sendPowerData;

                bool updateSuccess = setGeneralConfiguration(_jsonDocument);
                
                // Always release the mutex
                _releaseMutex(_configurationMutex, "configuration");
                
                if (updateSuccess) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Configuration updated (sendPowerData ignored)\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid configuration\"}");
                }} else if (request->url() == "/rest/set-channel") {
                _serverLog("Request to set channel data", TAG, LogLevel::INFO, request);
                
                // Rate limiting: Check if enough time has passed since last update
                unsigned long currentTime = millis();
                if (currentTime - _lastChannelUpdateTime < API_UPDATE_THROTTLE_MS) {
                    _serverLog("Channel update throttled - too frequent requests", TAG, LogLevel::WARNING, request);
                    request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", 
                        "{\"error\":\"Rate limited\", \"message\":\"Please wait before sending another request\", \"retryAfter\":500}");
                    return;
                }
                
                // Try to acquire the channel mutex with timeout
                if (!_acquireMutex(_channelMutex, "channel", pdMS_TO_TICKS(2000))) {
                    _serverLog("Channel update rejected - unable to acquire lock", TAG, LogLevel::WARNING, request);
                    request->send(HTTP_CODE_CONFLICT, "application/json", 
                        "{\"error\":\"Resource locked\", \"message\":\"Another channel operation is in progress, please try again\"}");
                    return;
                }
                
                // Validate that channel 0 is not being disabled
                bool channel0DisableAttempt = false;
                for (JsonPair kv : _jsonDocument.as<JsonObject>()) {
                    int channelIndex = atoi(kv.key().c_str());
                    if (channelIndex == 0 && !kv.value()["active"].as<bool>()) {
                        channel0DisableAttempt = true;
                        break;
                    }
                }
                
                if (channel0DisableAttempt) {
                    _releaseMutex(_channelMutex, "channel");
                    _serverLog("Attempt to disable channel 0 blocked", TAG, LogLevel::WARNING, request);
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", 
                        "{\"error\":\"Channel 0 cannot be disabled\", \"message\":\"Channel 0 is the main voltage channel and must remain active\"}");
                    return;
                }
                
                _lastChannelUpdateTime = currentTime;
    
                // Perform the channel update
                bool updateSuccess = _ade7953.setChannelData(_jsonDocument);
                
                // Always release the mutex
                _releaseMutex(_channelMutex, "channel");
                
                if (updateSuccess) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Channel data set\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid channel data\"}");
                }
            } else if (request->url() == "/rest/set-custom-mqtt-configuration") {
                _serverLog("Request to set custom MQTT configuration", TAG, LogLevel::INFO, request);
    
                if (_customMqtt.setConfiguration(_jsonDocument)) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid configuration\"}");
                }         
            } else if (request->url() == "/rest/set-influxdb-configuration") {
                _serverLog("Request to set InfluxDB configuration", TAG, LogLevel::INFO, request);
    
                if (_influxDbClient.setConfiguration(_jsonDocument)) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid configuration\"}");
                }         
            } else if (request->url() == "/rest/set-energy") {
                _serverLog("Request to set selective energy values", TAG, LogLevel::WARNING, request);
    
                if (_ade7953.setEnergyValues(_jsonDocument)) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"Energy values updated\"}");
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Invalid energy values\"}");
                }
            } else if (request->url() == "/rest/upload-file") {
                _serverLog("Request to upload file", TAG, LogLevel::INFO, request);
    
                if (_jsonDocument["filename"] && _jsonDocument["data"]) {
                    const char* filename = _jsonDocument["filename"];
                    const char* data = _jsonDocument["data"];
    
                    File _file = SPIFFS.open(filename, FILE_WRITE);
                    if (_file) {
                        _file.print(data);
                        _file.close();
    
                        request->send(HTTP_CODE_OK, "application/json", "{\"message\":\"File uploaded\"}");
                    } else {
                        request->send(HTTP_CODE_INTERNAL_SERVER_ERROR, "application/json", "{\"message\":\"Failed to open file\"}");
                    }
                } else {
                    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"Missing filename or data\"}");
                }
            } else {
                _serverLog(
                    ("Request to POST to unknown endpoint: " + request->url()).c_str(),
                    TAG,
                    LogLevel::WARNING,
                    request
                );
                request->send(HTTP_CODE_NOT_FOUND, "application/json", "{\"message\":\"Unknown endpoint\"}");
            }
                    
    
            // Clean up the buffer
            delete buffer;
            request->_tempObject = nullptr;
        } else {
            _serverLog("Getting more data...", TAG, LogLevel::DEBUG, request);
        } });

    _server.on("/rest/list-files", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to get list of files", TAG, LogLevel::DEBUG, request);

        File _root = SPIFFS.open("/");
        File _file = _root.openNextFile();

        JsonDocument _jsonDocument;
        unsigned int _loops = 0;
        while (_file && _loops < MAX_LOOP_ITERATIONS)
        {
            _loops++;

            // Skip if private in name
            const char* filename = _file.path();

            if (strstr(filename, "secret") == nullptr) {
                _jsonDocument[filename] = _file.size();
            }
            
            _file = _root.openNextFile();
        }

        char _buffer[JSON_RESPONSE_BUFFER_SIZE];
        serializeJson(_jsonDocument, _buffer, sizeof(_buffer));

        request->send(HTTP_CODE_OK, "application/json", _buffer); });

    _server.on("/rest/file/*", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to get file", TAG, LogLevel::DEBUG, request);
    
        char filename[256];
        const char* url = request->url().c_str();
        if (strlen(url) > 10) {
            snprintf(filename, sizeof(filename), "%s", url + 10);
        } else {
            filename[0] = '\0';
        }

        if (strstr(filename, "secret") != nullptr) {
            request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"message\":\"Unauthorized\"}");
            return;
        }
    
        File _file = SPIFFS.open(filename, FILE_READ);
        if (_file) {
            const char* contentType = "text/plain";
            
            if (strstr(filename, ".json") != nullptr) {
                contentType = "application/json";
            } else if (strstr(filename, ".html") != nullptr) {
                contentType = "text/html";
            } else if (strstr(filename, ".css") != nullptr) {
                contentType = "text/css";
            } else if (strstr(filename, ".ico") != nullptr) {
                contentType = "image/png";
            }

            request->send(_file, filename, contentType);
            _file.close();
        }
        else {
            request->send(HTTP_CODE_BAD_REQUEST, "text/plain", "File not found");
        } });

    _server.serveStatic("/api-docs", SPIFFS, "/swagger-ui.html");
    _server.serveStatic("/swagger.yaml", SPIFFS, "/swagger.yaml");
    _server.serveStatic("/log-raw", SPIFFS, LOG_PATH);
    _server.serveStatic("/daily-energy", SPIFFS, DAILY_ENERGY_JSON_PATH);
}

void CustomServer::_setOtherEndpoints()
{
    _server.onNotFound([this](AsyncWebServerRequest *request)
                       {
        TRACE();
        _serverLog(
            ("Request to get unknown page: " + request->url()).c_str(),
            TAG,
            LogLevel::DEBUG,
            request
        );
        request->send(HTTP_CODE_NOT_FOUND, "text/plain", "Not found"); });
}

void CustomServer::_handleDoUpdate(AsyncWebServerRequest *request, const char* filename, size_t index, uint8_t *data, size_t len, bool final)
{
    // Protect OTA updates with mutex - only allow one at a time
    if (!index) { // First chunk of upload
        if (!_acquireMutex(_otaMutex, "ota", pdMS_TO_TICKS(1000))) {
            _serverLog("OTA update rejected - another update in progress", TAG, LogLevel::WARNING, request);
            _onUpdateFailed(request, "Another firmware update is already in progress");
            return;
        }
    }

    _led.block();
    _led.setPurple(true);

    TRACE();
    if (!index)
    {
        if (strstr(filename, ".bin") != nullptr)
        {
            _logger.info("Update requested for firmware", TAG);
        }
        else
        {
            _onUpdateFailed(request, "File must be in .bin format");
            return;
        }
        
        _ade7953.pauseMeterReadingTask();
    
        size_t freeHeap = ESP.getFreeHeap();
        _logger.debug("Free heap before OTA: %zu bytes", TAG, freeHeap);
        
        if (freeHeap < MINIMUM_FREE_HEAP_OTA) {
            _onUpdateFailed(request, "Insufficient memory for update");
            return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
        {           
            _onUpdateFailed(request, Update.errorString());
            return;
        }

        Update.setMD5(_md5);
    }    

    TRACE();
    if (Update.write(data, len) != len)
    {
        _onUpdateFailed(request, Update.errorString());
        return;
    }    
    
    TRACE();
    if (final)
    {
        if (!Update.end(true))
        {   
            _onUpdateFailed(request, Update.errorString());
        }
        else
        {
            _onUpdateSuccessful(request);
        }
    }

    TRACE();
    _led.setOff(true);
    _led.unblock();
}

void CustomServer::_onUpdateSuccessful(AsyncWebServerRequest *request)
{
    TRACE();
    char response[256];
    snprintf(response, sizeof(response), "{\"status\":\"success\", \"md5\":\"%s\"}", Update.md5String().c_str());
    request->send(HTTP_CODE_OK, "application/json", response);

    TRACE();
    _logger.info("Update complete", TAG);
    updateJsonFirmwareStatus("success", "");

    _logger.debug("MD5 of new firmware: %s", TAG, Update.md5String().c_str());

    TRACE();
    char _firmwareStatus[64];
    CrashMonitor::getFirmwareStatusString(NEW_TO_TEST, _firmwareStatus);
    _logger.debug("Setting rollback flag to %s", TAG, _firmwareStatus);
    
    if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) _logger.error("Failed to set firmware status", TAG);

    TRACE();
    setRestartEsp32(TAG, "Restart needed after update");
    _releaseMutex(_otaMutex, "ota");
}

void CustomServer::_onUpdateFailed(AsyncWebServerRequest *request, const char *reason)
{
    TRACE();
      
    _ade7953.resumeMeterReadingTask();
    _logger.debug("Reattached ADE7953 interrupt after OTA failure", TAG);
    
    char response[512];
    snprintf(response, sizeof(response), "{\"status\":\"failed\", \"reason\":\"%s\"}", reason);
    request->send(HTTP_CODE_BAD_REQUEST, "application/json", response);

    Update.printError(Serial);
    _logger.debug("Size: %d bytes | Progress: %d bytes | Remaining: %d bytes", TAG, Update.size(), Update.progress(), Update.remaining());
    _logger.error("Update failed, keeping current firmware. Reason: %s", TAG, reason);
    updateJsonFirmwareStatus("failed", reason);

    for (int i = 0; i < 3; i++)
    {
        _led.setRed(true);
        delay(500);
        _led.setOff(true);
        delay(500);
    }

    _releaseMutex(_otaMutex, "ota");
}

void CustomServer::_serveJsonFile(AsyncWebServerRequest *request, const char *filePath)
{
    TRACE();

    File file = SPIFFS.open(filePath, FILE_READ);

    if (file)
    {
        request->send(file, filePath, "application/json");
        file.close();
    }
    else
    {
        request->send(HTTP_CODE_NOT_FOUND, "application/json", "{\"error\":\"File not found\"}");
    }
}

// Authentication methods
// -----------------------------

bool CustomServer::_requireAuth(AsyncWebServerRequest *request)
{
    // Allow login endpoint without authentication
    if (request->url().equals("/rest/auth/login"))
    {
        return false;
    }

    return !_checkAuth(request);
}

bool CustomServer::_checkAuth(AsyncWebServerRequest *request)
{
    // Check for auth token in header
    if (request->hasHeader("Authorization"))
    {
        const char* authHeaderValue = request->getHeader("Authorization")->value().c_str();
        if (strncmp(authHeaderValue, "Bearer ", 7) == 0)
        {
            const char* token = authHeaderValue + 7; // Skip "Bearer "
            return validateAuthToken(token);
        }
    }

    // Check for auth token in cookie
    if (request->hasHeader("Cookie"))
    {
        const char* cookieHeaderValue = request->getHeader("Cookie")->value().c_str();
        const char* tokenStart = strstr(cookieHeaderValue, "auth_token=");
        if (tokenStart != nullptr)
        {
            tokenStart += 11; // Length of "auth_token="
            const char* tokenEnd = strchr(tokenStart, ';');
            char token[256]; // Buffer for token
            if (tokenEnd != nullptr) {
                size_t tokenLen = tokenEnd - tokenStart;
                if (tokenLen >= sizeof(token)) tokenLen = sizeof(token) - 1;
                snprintf(token, tokenLen + 1, "%.*s", (int)tokenLen, tokenStart);
            } else {
                snprintf(token, sizeof(token), "%s", tokenStart);
            }
            return validateAuthToken(token);
        }
    }

    return false;
}

void CustomServer::_sendUnauthorized(AsyncWebServerRequest *request)
{
    request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"error\":\"Authentication required\"}");
}

// Concurrency control helper methods
bool CustomServer::_acquireMutex(SemaphoreHandle_t mutex, const char* mutexName, TickType_t timeout)
{
    if (mutex == NULL) {
        _logger.warning("Mutex %s is NULL, skipping lock", TAG, mutexName);
        return false;
    }
    
    if (xSemaphoreTake(mutex, timeout) == pdTRUE) {
        _logger.verbose("Successfully acquired mutex: %s", TAG, mutexName);
        return true;
    } else {
        _logger.warning("Failed to acquire mutex %s within timeout", TAG, mutexName);
        return false;
    }
}

void CustomServer::_releaseMutex(SemaphoreHandle_t mutex, const char* mutexName)
{
    if (mutex == NULL) {
        _logger.warning("Mutex %s is NULL, skipping unlock", TAG, mutexName);
        return;
    }
    
    if (xSemaphoreGive(mutex) == pdTRUE) {
        _logger.debug("Successfully released mutex: %s", TAG, mutexName);
    } else {
        _logger.warning("Failed to release mutex: %s", TAG, mutexName);
    }
}