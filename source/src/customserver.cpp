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
    String fullUrl = request->url();
    if (request->args() != 0)
    {
        fullUrl += "?";
        for (uint8_t i = 0; i < request->args(); i++)
        {
            if (i != 0)
            {
                fullUrl += "&";
            }
            fullUrl += request->argName(i) + "=" + request->arg(i);
        }
    }

    if (logLevel == LogLevel::DEBUG)
    {
        _logger.debug("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    }
    else if (logLevel == LogLevel::INFO)
    {
        _logger.info("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    }
    else if (logLevel == LogLevel::WARNING)
    {
        _logger.warning("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    }
    else if (logLevel == LogLevel::ERROR)
    {
        _logger.error("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    }
    else if (logLevel == LogLevel::FATAL)
    {
        _logger.fatal("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
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
        } }, [this](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
               { _handleDoUpdate(request, filename, index, data, len, final); });

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
            _md5 = request->getParam("md5")->value();
            _md5.toLowerCase();
            _logger.debug("MD5 included in the request: %s", TAG, _md5.c_str());

            if (_md5.length() != 32)
            {
                _logger.warning("MD5 not 32 characters long. Skipping MD5 verification", TAG);
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"message\":\"MD5 not 32 characters long\"}");
            }
            else
            {
                _md5 = request->getParam("md5")->value();
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
            request->send(HTTP_CODE_OK, "application/json", "{\"status\":\"running\",\"size\":" + String(Update.size()) + ",\"progress\":" + String(Update.progress()) + ",\"remaining\":" + String(Update.remaining()) + "}");
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
            
            String clientIp = request->client()->remoteIP().toString();
            
            // Check if IP is blocked due to too many failed attempts
            if (isIpBlocked(clientIp)) {
                request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", "{\"success\":false,\"message\":\"Too many failed login attempts. Please try again later.\"}");
                _serverLog("Login blocked - IP rate limited", TAG, LogLevel::WARNING, request);
                delete buffer;
                request->_tempObject = nullptr;
                return;
            }
            
            String password = jsonDoc["password"].as<String>();
              if (validatePassword(password)) {
                // Record successful login to reset rate limiting for this IP
                recordSuccessfulLogin(clientIp);
                
                String token = generateAuthToken();
                if (token.isEmpty()) {
                    // Maximum concurrent sessions reached
                    request->send(HTTP_CODE_TOO_MANY_REQUESTS, "application/json", "{\"success\":false,\"message\":\"Maximum concurrent sessions reached. Please try again later or ask another user to logout.\"}");
                    _serverLog("Login rejected - maximum sessions reached", TAG, LogLevel::WARNING, request);
                } else {
                    JsonDocument response;
                    response["success"] = true;
                    response["token"] = token;
                    response["message"] = "Login successful";
                    
                    String responseBuffer;
                    serializeJson(response, responseBuffer);
                    
                    AsyncWebServerResponse *resp = request->beginResponse(HTTP_CODE_OK, "application/json", responseBuffer);
                    resp->addHeader("Set-Cookie", "auth_token=" + token + "; Path=/; Max-Age=86400; HttpOnly");
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
                String authHeader = request->getHeader("Authorization")->value();
                if (authHeader.startsWith("Bearer ")) {
                    String token = authHeader.substring(7);
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
            
            String currentPassword = jsonDoc["currentPassword"].as<String>();
            String newPassword = jsonDoc["newPassword"].as<String>();
            
            if (!validatePassword(currentPassword)) {
                request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"success\":false,\"message\":\"Current password is incorrect\"}");
            } else if (newPassword.length() < MIN_PASSWORD_LENGTH || newPassword.length() > MAX_PASSWORD_LENGTH) {
                request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"success\":false,\"message\":\"New password must be between " + String(MIN_PASSWORD_LENGTH) + " and " + String(MAX_PASSWORD_LENGTH) + " characters\"}");
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
        
        String responseBuffer;
        serializeJson(response, responseBuffer);
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

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/device-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get device info", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonDeviceInfo(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/wifi-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get WiFi values", TAG, LogLevel::DEBUG, request);
        
        JsonDocument _jsonDocument;
        _customWifi.getWifiStatus(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/meter", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.meterValuesToJson(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/meter-single", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", TAG, LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (_ade7953.channelData[_indexInt].active) {
                    String _buffer;
                    serializeJson(_ade7953.singleMeterValuesToJson(_indexInt), _buffer);

                    request->send(HTTP_CODE_OK, "application/json", _buffer.c_str());
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
        _serverLog("Request to get meter values", TAG, LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (_ade7953.channelData[_indexInt].active) {
                    request->send(HTTP_CODE_OK, "application/json", "{\"value\":" + String(_ade7953.meterValues[_indexInt].activePower) + "}");
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

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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
                _jsonDocument[String(i)]["activeEnergyImported"] = _ade7953.meterValues[i].activeEnergyImported;
                _jsonDocument[String(i)]["activeEnergyExported"] = _ade7953.meterValues[i].activeEnergyExported;
                _jsonDocument[String(i)]["reactiveEnergyImported"] = _ade7953.meterValues[i].reactiveEnergyImported;
                _jsonDocument[String(i)]["reactiveEnergyExported"] = _ade7953.meterValues[i].reactiveEnergyExported;
                _jsonDocument[String(i)]["apparentEnergy"] = _ade7953.meterValues[i].apparentEnergy;
            }
        }

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/get-log-level", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log level", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["print"] = _logger.logLevelToString(_logger.getPrintLevel());
        _jsonDocument["save"] = _logger.logLevelToString(_logger.getSaveLevel());

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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
            String _type = request->getParam("type")->value();
            if (_type == "print") {
                _logger.setPrintLevel(LogLevel(_level));
            } else if (_type == "save") {
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

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/get-has-secrets", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get has_secrets", TAG, LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["has_secrets"] = HAS_SECRETS ? true : false;

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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

                    request->send(HTTP_CODE_OK, "application/json", "{\"value\":" + String(registerValue) + "}");
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

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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
        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        TRACE();
        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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

        String operationName = _buttonHandler.getCurrentOperationName();
        unsigned long operationTimestamp = _buttonHandler.getCurrentOperationTimestamp();
        
        String response = "{";
        response += "\"operationName\":\"" + operationName + "\",";
        response += "\"operationTimestamp\":" + String(operationTimestamp);
        if (operationTimestamp > 0) {
            String timestampStr = CustomTime::timestampFromUnix(operationTimestamp);
            response += ",\"operationTimestampFormatted\":\"" + timestampStr + "\"";
        }
        response += "}";
        
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

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

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

            String _buffer;
            serializeJson(_jsonDocument, _buffer);
            _serverLog(_buffer.c_str(), TAG, LogLevel::DEBUG, request);

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
                    String _filename = _jsonDocument["filename"];
                    String _data = _jsonDocument["data"];
    
                    File _file = SPIFFS.open(_filename, FILE_WRITE);
                    if (_file) {
                        _file.print(_data);
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
            String _filename = String(_file.path());

            if (_filename.indexOf("secret") == -1) _jsonDocument[_filename] = _file.size();
            _jsonDocument[_filename] = _file.size();
            
            _file = _root.openNextFile();
        }

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(HTTP_CODE_OK, "application/json", _buffer.c_str()); });

    _server.on("/rest/file/*", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        if (_requireAuth(request)) {
            _sendUnauthorized(request);
            return;
        }
        _serverLog("Request to get file", TAG, LogLevel::DEBUG, request);
    
        String _filename = request->url().substring(10);

        if (_filename.indexOf("secret") != -1) {
            request->send(HTTP_CODE_UNAUTHORIZED, "application/json", "{\"message\":\"Unauthorized\"}");
            return;
        }
    
        File _file = SPIFFS.open(_filename, FILE_READ);
        if (_file) {
            String contentType = "text/plain";
            
            if (_filename.indexOf(".json") != -1) {
                contentType = "application/json";
            } else if (_filename.indexOf(".html") != -1) {
                contentType = "text/html";
            } else if (_filename.indexOf(".css") != -1) {
                contentType = "text/css";
            } else if (_filename.indexOf(".ico") != -1) {
                contentType = "image/png";
            }

            request->send(_file, _filename, contentType);
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

void CustomServer::_handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
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
        if (filename.indexOf(".bin") > -1)
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

        Update.setMD5(_md5.c_str());
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
    request->send(HTTP_CODE_OK, "application/json", "{\"status\":\"success\", \"md5\":\"" + Update.md5String() + "\"}");

    TRACE();
    _logger.info("Update complete", TAG);
    updateJsonFirmwareStatus("success", "");

    _logger.debug("MD5 of new firmware: %s", TAG, Update.md5String().c_str());

    TRACE();
    _logger.debug("Setting rollback flag to %s", TAG, CrashMonitor::getFirmwareStatusString(NEW_TO_TEST));
    if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST))
        _logger.error("Failed to set firmware status", TAG);

    TRACE();
    setRestartEsp32(TAG, "Restart needed after update");
    _releaseMutex(_otaMutex, "ota");
}

void CustomServer::_onUpdateFailed(AsyncWebServerRequest *request, const char *reason)
{
    TRACE();
      
    _ade7953.resumeMeterReadingTask();
    _logger.debug("Reattached ADE7953 interrupt after OTA failure", TAG);
    
    request->send(HTTP_CODE_BAD_REQUEST, "application/json", "{\"status\":\"failed\", \"reason\":\"" + String(reason) + "\"}");

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
        String authHeader = request->getHeader("Authorization")->value();
        if (authHeader.startsWith("Bearer "))
        {
            String token = authHeader.substring(7);
            return validateAuthToken(token);
        }
    }

    // Check for auth token in cookie
    if (request->hasHeader("Cookie"))
    {
        String cookieHeader = request->getHeader("Cookie")->value();
        int tokenStart = cookieHeader.indexOf("auth_token=");
        if (tokenStart != -1)
        {
            tokenStart += 11; // Length of "auth_token="
            int tokenEnd = cookieHeader.indexOf(";", tokenStart);
            if (tokenEnd == -1)
                tokenEnd = cookieHeader.length();
            String token = cookieHeader.substring(tokenStart, tokenEnd);
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