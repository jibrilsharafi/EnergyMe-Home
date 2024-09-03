#include "customserver.h"

CustomServer::CustomServer(
    AdvancedLogger &logger,
    Led &led,
    Ade7953 &ade7953,
    CustomTime &customTime,
    CustomWifi &customWifi) : _logger(logger), _led(led), _ade7953(ade7953), _customTime(customTime), _customWifi(customWifi) {}

void CustomServer::begin()
{
    _setHtmlPages();
    _setOta();
    _setRestApi();
    _setOtherEndpoints();

    server.begin();

    Update.onProgress([](size_t progress, size_t total) {});
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
    // HTML pages
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get index page", "customserver::_setHtmlPages::/", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", index_html); });

    server.on("/configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get configuration page", "customserver::_setHtmlPages::/configuration", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", configuration_html); });

    server.on("/calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get calibration page", "customserver::_setHtmlPages::/calibration", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", calibration_html); });

    server.on("/channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get channel page", "customserver::_setHtmlPages::/channel", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", channel_html); });

    server.on("/info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get info page", "customserver::_setHtmlPages::/info", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", info_html); });

    server.on("/log", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log page", "customserver::_setHtmlPages::/log", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", log_html); });

    server.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get update page", "customserver::_setOta::/update", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", update_html); });

    // CSS
    server.on("/css/styles.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", styles_css); });

    server.on("/css/button.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", button_css); });

    server.on("/css/section.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", section_css); });

    server.on("/css/typography.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", typography_css); });

    // Other
    server.on("/favicon.svg", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get favicon", "customserver::_setHtmlPages::/favicon.png", LogLevel::VERBOSE, request);
        request->send_P(200, "image/svg+xml", favicon_svg); });
}

void CustomServer::_setOta()
{

    server.on("/do-update", HTTP_POST, [this](AsyncWebServerRequest *request) {}, [this](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
               { _handleDoUpdate(request, filename, index, data, len, final); });
}

void CustomServer::_setRestApi()
{
    server.on("/rest/is-alive", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the ESP32 is alive", "customserver::_setRestApi::/rest/is-alive", LogLevel::DEBUG, request);

        request->send(200, "application/json", "{\"message\":\"True\"}"); });

    server.on("/rest/project-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get project info from REST API", "customserver::_setRestApi::/rest/project-info", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonProjectInfo(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/device-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get device info from REST API", "customserver::_setRestApi::/rest/device-info", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonDeviceInfo(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/wifi-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get WiFi values from REST API", "customserver::_setRestApi::/rest/wifi-info", LogLevel::DEBUG, request);
        
        JsonDocument _jsonDocument;
        _customWifi.getWifiStatus(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/meter", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(_ade7953.meterValuesToJson(), _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/meter-single", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter-single", LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (_ade7953.channelData[_indexInt].active) {
                    String _buffer;
                    serializeJson(_ade7953.singleMeterValuesToJson(_indexInt), _buffer);

                    request->send(200, "application/json", _buffer.c_str());
                } else {
                    request->send(400, "application/json", "{\"message\":\"Channel not active\"}");
                }
            } else {
                request->send(400, "application/json", "{\"message\":\"Channel index out of range\"}");
            }
        } else {
            request->send(400, "application/json", "{\"message\":\"Missing index parameter\"}");
        } });

    server.on("/rest/active-power", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/active-power", LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (_ade7953.channelData[_indexInt].active) {
                    request->send(200, "application/json", "{\"value\":" + String(_ade7953.meterValues[_indexInt].activePower) + "}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Channel not active\"}");
                }
            } else {
                request->send(400, "application/json", "{\"message\":\"Channel index out of range\"}");
            }
        } else {
            request->send(400, "application/json", "{\"message\":\"Missing index parameter\"}");
        } });

    server.on("/rest/get-ade7953-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get ADE7953 configuration from REST API", "customserver::_setRestApi::/rest/get-ade7953-configuration", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);
        
        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/get-channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get channel data from REST API", "customserver::_setRestApi::/rest/get-channel", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.channelDataToJson(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/get-calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get configuration from REST API", "customserver::_setRestApi::/rest/get-calibration", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/calibration-reset", HTTP_POST, [&](AsyncWebServerRequest *request)
               {
        _serverLog("Request to reset calibration values from REST API", "customserver::_setRestApi::/rest/calibration-reset", LogLevel::WARNING, request);
        
        _ade7953.setDefaultCalibrationValues();
        _ade7953.setDefaultChannelData();

        request->send(200, "application/json", "{\"message\":\"Calibration values reset\"}");
    });

    server.on("/rest/reset-energy", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to reset energy counters from REST API", "customserver::_setRestApi::/rest/reset-energy", LogLevel::WARNING, request);

        _ade7953.resetEnergyValues();

        request->send(200, "application/json", "{\"message\":\"Energy counters reset\"}"); });

    server.on("/rest/get-log-level", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log level from REST API", "customserver::_setRestApi::/rest/get-log-level", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["print"] = _logger.logLevelToString(_logger.getPrintLevel());
        _jsonDocument["save"] = _logger.logLevelToString(_logger.getSaveLevel());

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/set-log-level", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to set log level from REST API",
            "customserver::_setRestApi::/rest/set-log-level",
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
                request->send(400, "application/json", "{\"message\":\"Invalid type parameter. Supported values: print, save\"}");
            }
            request->send(200, "application/json", "{\"message\":\"Success\"}");
        } else {
            request->send(400, "application/json", "{\"message\":\"Missing parameter. Required: level (int), type (string)\"}");
        } });

    server.on("/rest/get-general-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get get general configuration from REST API", "customserver::_setRestApi::/rest/get-general-configuration", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        generalConfigurationToJson(generalConfiguration, _jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    server.on("/rest/ade7953-read-register", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to get ADE7953 register value from REST API",
            "customserver::_setRestApi::/rest/ade7953-read-register",
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

                    request->send(200, "application/json", "{\"value\":" + String(registerValue) + "}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Address out of range. Supported values: 0-0x3FF (0-1023)\"}");
                }
            } else {
                request->send(400, "application/json", "{\"message\":\"Number of bits not supported. Supported values: 8, 16, 24, 32\"}");
            }
        } else {
            request->send(400, "application/json", "{\"message\":\"Missing parameter. Required: address (int), nBits (int), signed (bool)\"}");
        } });

    server.on("/rest/ade7953-write-register", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to get ADE7953 register value from REST API",
            "customserver::_setRestApi::/rest/ade7953-read-register",
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

                    request->send(200, "application/json", "{\"message\":\"Success\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Address out of range. Supported values: 0-0x3FF (0-1023)\"}");
                }
            } else {
                request->send(400, "application/json", "{\"message\":\"Number of bits not supported. Supported values: 8, 16, 24, 32\"}");
            }
        } else {
            request->send(400, "application/json", "{\"message\":\"Missing parameter. Required: address (int), nBits (int), data (int)\"}");
        } });

    server.on("/rest/firmware-update-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update info from REST API", "customserver::_setRestApi::/rest/firmware-update-info", LogLevel::DEBUG, request);

        File _file = SPIFFS.open(FW_UPDATE_INFO_JSON_PATH, FILE_READ);
        if (_file) {
            request->send(_file, "application/json");
            _file.close();
        } else {
            request->send(200, "application/json", "{}");
        } });

    server.on("/rest/firmware-update-status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update status from REST API", "customserver::_setRestApi::/rest/firmware-update-status", LogLevel::DEBUG, request);

        File _file = SPIFFS.open(FW_UPDATE_STATUS_JSON_PATH, FILE_READ);
        if (_file) {
            request->send(_file, "application/json");
            _file.close();
        } else {
            request->send(200, "application/json", "{}");
        } });

    server.on("/rest/is-latest-firmware-installed", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the latest firmware is installed from REST API", "customserver::_setRestApi::/rest/is-latest-firmware-installed", LogLevel::DEBUG, request);

        if (isLatestFirmwareInstalled()) {
            request->send(200, "application/json", "{\"latest\":true}");
        } else {
            request->send(200, "application/json", "{\"latest\":false}");
        } });

    server.on("/rest/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to factory reset from REST API", "customserver::_setRestApi::/rest/factory-reset", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Factory reset in progress. Check the log for more information.\"}");
        factoryReset(); });

    server.on("/rest/clear-log", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to clear log from REST API", "customserver::_setRestApi::/rest/clear-log", LogLevel::DEBUG, request);

        _logger.clearLog();

        request->send(200, "application/json", "{\"message\":\"Log cleared\"}"); });

    server.on("/rest/restart", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to restart the ESP32 from REST API", "customserver::_setRestApi::/rest/restart", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Restarting...\"}");
        setRestartEsp32("customserver::_setRestApi", "Request to restart the ESP32 from REST API"); });

    server.on("/rest/reset-wifi", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to erase WiFi credentials from REST API", "customserver::_setRestApi::/rest/reset-wifi", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
        _customWifi.resetWifi();
        setRestartEsp32("customserver::_setRestApi", "Request to erase WiFi credentials from REST API"); });

    server.onRequestBody([this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
            _serverLog(_buffer.c_str(), "customserver::_setRestApi::onRequestBody", LogLevel::DEBUG, request);

            if (request->url() == "/rest/set-calibration") {
                _serverLog("Request to set calibration values from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-calibration", LogLevel::INFO, request);
    
                _ade7953.setCalibrationValues(_jsonDocument);
    
                request->send(200, "application/json", "{\"message\":\"Calibration values set\"}");
    
            } else if (request->url() == "/rest/set-ade7953-configuration") {
                _serverLog("Request to set ADE7953 configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-ade7953-configuration", LogLevel::INFO, request);
    
                _ade7953.setConfiguration(_jsonDocument);
    
                request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
    
            } else if (request->url() == "/rest/set-general-configuration") {
                _serverLog("Request to set general configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-general-configuration", LogLevel::INFO, request);
    
                GeneralConfiguration previousGeneralConfiguration = generalConfiguration;
                jsonToGeneralConfiguration(_jsonDocument, generalConfiguration);
                setGeneralConfiguration(generalConfiguration);
    
                request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
    
            } else if (request->url() == "/rest/set-channel") {
                _serverLog("Request to set channel data from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-channel", LogLevel::INFO, request);
    
                _ade7953.setChannelData(_jsonDocument);
    
                request->send(200, "application/json", "{\"message\":\"Channel data set\"}");
            } else {
                _serverLog(
                    ("Request to POST to unknown endpoint: " + request->url()).c_str(),
                    "customserver::_setRestApi::onRequestBody",
                    LogLevel::WARNING,
                    request
                );
                request->send(404, "application/json", "{\"message\":\"Unknown endpoint\"}");
            }
                    
    
            // Clean up the buffer
            delete buffer;
            request->_tempObject = nullptr;
        } else {
            _serverLog("Getting more data...", "customserver::_setRestApi::onRequestBody", LogLevel::DEBUG, request);
        }
    });

    server.on("/rest/list-files", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        _serverLog("Request to get list of files from REST API", "customserver::_setRestApi::/rest/files", LogLevel::DEBUG, request);

        File _root = SPIFFS.open("/");
        File _file = _root.openNextFile();

        JsonDocument _jsonDocument;
        while (_file)
        {
            // Skip if private in name
            String _filename = String(_file.name());

            if (_filename.indexOf("secret") != -1) {
                _file = _root.openNextFile();
            } else {
                _jsonDocument[_file.name()] = _file.size();
                _file = _root.openNextFile();
            }
        }

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/file/*", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        _serverLog("Request to get file from REST API", "customserver::_setRestApi::/rest/file/*", LogLevel::DEBUG, request);
    
        String _filename = request->url().substring(10);
    
        if (_filename.indexOf("secret") != -1) {
            request->send(401, "application/json", "{\"message\":\"Unauthorized\"}");
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

            request->send(_file, contentType);
            _file.close();
        }
        else {
            request->send(400, "text/plain", "File not found");
        }
    });


    server.serveStatic("/log-raw", SPIFFS, LOG_PATH);
    server.serveStatic("/daily-energy", SPIFFS, DAILY_ENERGY_JSON_PATH);
}

void CustomServer::_setOtherEndpoints()
{
    server.onNotFound([this](AsyncWebServerRequest *request)
                       {
        _serverLog(
            ("Request to get unknown page: " + request->url()).c_str(),
            "customserver::_setOtherEndpoints::onNotFound",
            LogLevel::INFO,
            request
        );
        request->send(404, "text/plain", "Not found"); });
}

void CustomServer::_handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
    _led.block();
    _led.setPurple(true);

    if (!index)
    {
        int _cmd;
        if (filename.indexOf("spiffs") > -1)
        {
            _onUpdateFailed(request, "SPIFFS update is unsupported");
            return;
        }
        else if (filename.indexOf("firmware") > -1)
        {
            _logger.warning("Update requested for firmware", "customserver::handleDoUpdate");
            _cmd = U_FLASH;
        }
        else
        {
            _onUpdateFailed(request, "Unknown file type");
            return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, _cmd))
        {
            _onUpdateFailed(request, "Error during update begin");
            return;
        }
    }

    if (Update.write(data, len) != len)
    {
        _onUpdateFailed(request, "Data length mismatch");
        return;
    }

    if (final)
    {
        if (!Update.end(true))
        {
            _onUpdateFailed(request, "Error during last part of update");
        }
        else
        {
            _onUpdateSuccessful(request);
        }
    }

    _led.setOff(true);
    _led.unblock();
}

void CustomServer::_updateJsonFirmwareStatus(const char *status, const char *reason)
{
    JsonDocument _jsonDocument;

    _jsonDocument["status"] = status;
    _jsonDocument["reason"] = reason;
    _jsonDocument["timestamp"] = _customTime.getTimestamp();

    File _file = SPIFFS.open(FW_UPDATE_STATUS_JSON_PATH, FILE_WRITE);
    if (_file)
    {
        serializeJson(_jsonDocument, _file);
        _file.close();
    }
}

void CustomServer::_onUpdateSuccessful(AsyncWebServerRequest *request)
{
    request->send(200, "application/json", "{\"status\":\"success\", \"reason\":\"\"}");

    _logger.warning("Update complete", "customserver::handleDoUpdate");
    _updateJsonFirmwareStatus("success", "");

    setRestartEsp32("customserver::_handleDoUpdate", "Restart needed after update");
}

void CustomServer::_onUpdateFailed(AsyncWebServerRequest *request, const char *reason)
{
    request->send(400, "application/json", "{\"status\":\"failed\", \"reason\":\"" + String(reason) + "\"}");

    Update.printError(Serial);
    _logger.error("Update fai_led. Reason: %s", "customserver::_onUpdateFailed", reason);
    _updateJsonFirmwareStatus("failed", reason);

    for (int i = 0; i < 3; i++)
    {
        _led.setRed(true);
        delay(500);
        _led.setOff(true);
        delay(500);
    }
}