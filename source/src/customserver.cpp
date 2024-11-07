#include "customserver.h"

CustomServer::CustomServer(
    AsyncWebServer &server,
    AdvancedLogger &logger,
    Led &led,
    Ade7953 &ade7953,
    CustomTime &customTime,
    CustomWifi &customWifi,
    CustomMqtt &customMqtt) :
        _server(server), 
        _logger(logger), 
        _led(led), 
        _ade7953(ade7953), 
        _customTime(customTime), 
        _customWifi(customWifi), 
        _customMqtt(customMqtt) {}

void CustomServer::begin()
{
    _setHtmlPages();
    _setOta();
    _setRestApi();
    _setOtherEndpoints();

    _server.begin();

    Update.onProgress([](size_t progress, size_t total) {});
    _md5 = "";
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
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get index page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", index_html); });

    _server.on("/configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get configuration page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", configuration_html); });

    _server.on("/calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get calibration page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", calibration_html); });

    _server.on("/channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get channel page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", channel_html); });

    _server.on("/info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get info page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", info_html); });

    _server.on("/log", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log page", "customserver::_setHtmlPages", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", log_html); });

    _server.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get update page", "customserver::_setOta", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", update_html); });

    // CSS
    _server.on("/css/styles.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", styles_css); });

    _server.on("/css/button.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", button_css); });

    _server.on("/css/section.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", section_css); });

    _server.on("/css/typography.css", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/css", typography_css); });

    // Swagger UI
    _server.on("/swagger-ui", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get Swagger UI", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/html", swagger_ui_html); });

    _server.on("/swagger.yaml", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get Swagger YAML", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "text/yaml", swagger_yaml); });

    // Favicon - SVG format seems to be the only one working with embedded binary data
    _server.on("/favicon.svg", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get favicon", "customserver::_setHtmlPages", LogLevel::VERBOSE, request);
        request->send_P(200, "image/svg+xml", favicon_svg); });
}

void CustomServer::_setOta()
{
    _server.on("/do-update", HTTP_POST, [this](AsyncWebServerRequest *request) {}, [this](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
               {_handleDoUpdate(request, filename, index, data, len, final);});

    _server.on("/set-md5", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to set MD5", "customserver::_setOta", LogLevel::DEBUG, request);

        if (request->hasParam("md5"))
        {
            _md5 = request->getParam("md5")->value();
            _md5.toLowerCase();
            _logger.debug("MD5 included in the request: %s", "customserver::_setOta", _md5.c_str());

            if (_md5.length() != 32)
            {
                _logger.warning("MD5 not 32 characters long. Skipping MD5 verification", "customserver::_setOta");
                request->send(400, "application/json", "{\"message\":\"MD5 not 32 characters long\"}");
            }
            else
            {
                _md5 = request->getParam("md5")->value();
                request->send(200, "application/json", "{\"message\":\"MD5 set\"}");
            }
        }
        else
        {
            request->send(400, "application/json", "{\"message\":\"Missing MD5 parameter\"}");
        }
    });

    _server.on("/rest/update-status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get update status", "customserver::_setOta", LogLevel::DEBUG, request);

        if (Update.isRunning())
        {
            request->send(200, "application/json", "{\"status\":\"running\",\"size\":" + String(Update.size()) + ",\"progress\":" + String(Update.progress()) + ",\"remaining\":" + String(Update.remaining()) + "}");
        }
        else
        {
            request->send(200, "application/json", "{\"status\":\"idle\"}");
        }
    });

    _server.on("/rest/update-rollback", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to rollback firmware", "customserver::_setOta", LogLevel::WARNING, request);

        if (Update.isRunning())
        {
            Update.abort();
        }

        if (Update.canRollBack())
        {
            Update.rollBack();
            request->send(200, "application/json", "{\"message\":\"Rollback in progress. Restarting ESP32...\"}");
            setRestartEsp32("customserver::_setOta", "Firmware rollback in progress requested from REST API");
        }
        else
        {
            _logger.error("Rollback not possible. Reason: %s", "customserver::_setOta", Update.errorString());
            request->send(500, "application/json", "{\"message\":\"Rollback not possible\"}");
        }
    });
}

void CustomServer::_setRestApi()
{
    _server.on("/rest/is-alive", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the ESP32 is alive", "customserver::_setRestApi", LogLevel::DEBUG, request);

        request->send(200, "application/json", "{\"message\":\"True\"}"); });

    _server.on("/rest/project-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get project info", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonProjectInfo(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/device-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get device info", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        getJsonDeviceInfo(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/wifi-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get WiFi values", "customserver::_setRestApi", LogLevel::DEBUG, request);
        
        JsonDocument _jsonDocument;
        _customWifi.getWifiStatus(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/meter", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.meterValuesToJson(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/meter-single", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", "customserver::_setRestApi ", LogLevel::DEBUG, request);

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

    _server.on("/rest/active-power", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get meter values", "customserver::_setRestApi", LogLevel::DEBUG, request);

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

    _server.on("/rest/get-ade7953-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get ADE7953 configuration", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _serveJsonFile(request, CONFIGURATION_ADE7953_JSON_PATH); });

    _server.on("/rest/get-channel", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get channel data", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _ade7953.channelDataToJson(_jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/get-calibration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get configuration", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _serveJsonFile(request, CALIBRATION_JSON_PATH); });

    _server.on("/rest/calibration-reset", HTTP_POST, [&](AsyncWebServerRequest *request)
               {
        _serverLog("Request to reset calibration values", "customserver::_setRestApi", LogLevel::WARNING, request);
        
        _ade7953.setDefaultCalibrationValues();
        _ade7953.setDefaultChannelData();

        request->send(200, "application/json", "{\"message\":\"Calibration values reset\"}");
    });

    _server.on("/rest/reset-energy", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to reset energy counters", "customserver::_setRestApi", LogLevel::WARNING, request);

        _ade7953.resetEnergyValues();

        request->send(200, "application/json", "{\"message\":\"Energy counters reset\"}"); });

    _server.on("/rest/get-log-level", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get log level", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        _jsonDocument["print"] = _logger.logLevelToString(_logger.getPrintLevel());
        _jsonDocument["save"] = _logger.logLevelToString(_logger.getSaveLevel());

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/set-log-level", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to set log level",
            "customserver::_setRestApi",
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

    _server.on("/rest/get-general-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get get general configuration", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        generalConfigurationToJson(generalConfiguration, _jsonDocument);

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/ade7953-read-register", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to get ADE7953 register value",
            "customserver::_setRestApi",
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

    _server.on("/rest/ade7953-write-register", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog(
            "Request to get ADE7953 register value",
            "customserver::_setRestApi",
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

    _server.on("/rest/firmware-update-info", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update info", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _serveJsonFile(request, FW_UPDATE_INFO_JSON_PATH); });

    _server.on("/rest/firmware-update-status", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get firmware update status", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _serveJsonFile(request, FW_UPDATE_STATUS_JSON_PATH); });

    _server.on("/rest/is-latest-firmware-installed", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to check if the latest firmware is installed", "customserver::_setRestApi", LogLevel::DEBUG, request);

        if (isLatestFirmwareInstalled()) {
            request->send(200, "application/json", "{\"latest\":true}");
        } else {
            request->send(200, "application/json", "{\"latest\":false}");
        } });

    _server.on("/rest/get-current-monitor-data", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get current monitor data", "customserver::_setRestApi", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        if (!CrashMonitor::getJsonReport(_jsonDocument, crashData)) {
            request->send(500, "application/json", "{\"message\":\"Error getting crash data\"}");
            return;
        }

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/get-crash-data", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get crash data", "customserver::_setRestApi", LogLevel::DEBUG, request);

        TRACE
        CrashData _crashData;
        if (!CrashMonitor::getSavedCrashData(_crashData)) {
            request->send(500, "application/json", "{\"message\":\"Could not get crash data\"}");
            return;
        }

        TRACE
        JsonDocument _jsonDocument;
        if (!CrashMonitor::getJsonReport(_jsonDocument, _crashData)) {
            request->send(500, "application/json", "{\"message\":\"Could not create JSON report\"}");
            return;
        }

        TRACE
        if (_jsonDocument.size() > 10000) { // FIMXE: temporary
            request->send(500, "application/json", "{\"message\":\"Crash data too big\"}");
            return;
        }

        TRACE
        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        TRACE
        request->send(200, "application/json", _buffer.c_str()); });

    _server.on("/rest/factory-reset", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to factory reset", "customserver::_setRestApi", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Factory reset in progress. Check the log for more information.\"}");
        factoryReset(); });

    _server.on("/rest/clear-log", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to clear log", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _logger.clearLog();

        request->send(200, "application/json", "{\"message\":\"Log cleared\"}"); });

    _server.on("/rest/restart", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to restart the ESP32", "customserver::_setRestApi", LogLevel::INFO, request);

        request->send(200, "application/json", "{\"message\":\"Restarting...\"}");
        setRestartEsp32("customserver::_setRestApi", "Request to restart the ESP32 from REST API"); });

    _server.on("/rest/reset-wifi", HTTP_POST, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to erase WiFi credentials", "customserver::_setRestApi", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
        _customWifi.resetWifi();
    });

    _server.on("/rest/get-custom-mqtt-configuration", HTTP_GET, [this](AsyncWebServerRequest *request)
               {
        _serverLog("Request to get custom MQTT configuration", "customserver::_setRestApi", LogLevel::DEBUG, request);

        _serveJsonFile(request, CUSTOM_MQTT_CONFIGURATION_JSON_PATH); });

    _server.onRequestBody([this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
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
                _serverLog("Request to set calibration values", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (_ade7953.setCalibrationValues(_jsonDocument)) {
                    request->send(200, "application/json", "{\"message\":\"Calibration values set\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Invalid calibration values\"}");
                }
    
                request->send(200, "application/json", "{\"message\":\"Calibration values set\"}");
    
            } else if (request->url() == "/rest/set-ade7953-configuration") {
                _serverLog("Request to set ADE7953 configuration", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (_ade7953.setConfiguration(_jsonDocument)) {
                    request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Invalid configuration\"}");
                }
    
                request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
    
            } else if (request->url() == "/rest/set-general-configuration") {
                _serverLog("Request to set general configuration", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (setGeneralConfiguration(_jsonDocument)) {
                    request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Invalid configuration\"}");
                }    
    
            } else if (request->url() == "/rest/set-channel") {
                _serverLog("Request to set channel data", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (_ade7953.setChannelData(_jsonDocument)) {
                    request->send(200, "application/json", "{\"message\":\"Channel data set\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Invalid channel data\"}");
                }    
            } else if (request->url() == "/rest/set-custom-mqtt-configuration") {
                _serverLog("Request to set custom MQTT configuration", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (_customMqtt.setConfiguration(_jsonDocument)) {
                    request->send(200, "application/json", "{\"message\":\"Configuration updated\"}");
                } else {
                    request->send(400, "application/json", "{\"message\":\"Invalid configuration\"}");
                }         
            } else if (request->url() == "/rest/upload-file") {
                _serverLog("Request to upload file", "customserver::_setRestApi", LogLevel::INFO, request);
    
                if (_jsonDocument["filename"] && _jsonDocument["data"]) {
                    String _filename = _jsonDocument["filename"];
                    String _data = _jsonDocument["data"];
    
                    File _file = SPIFFS.open(_filename, FILE_WRITE);
                    if (_file) {
                        _file.print(_data);
                        _file.close();
    
                        request->send(200, "application/json", "{\"message\":\"File uploaded\"}");
                    } else {
                        request->send(500, "application/json", "{\"message\":\"Failed to open file\"}");
                    }
                } else {
                    request->send(400, "application/json", "{\"message\":\"Missing filename or data\"}");
                }
            } else {
                _serverLog(
                    ("Request to POST to unknown endpoint: " + request->url()).c_str(),
                    "customserver::_setRestApi",
                    LogLevel::WARNING,
                    request
                );
                request->send(404, "application/json", "{\"message\":\"Unknown endpoint\"}");
            }
                    
    
            // Clean up the buffer
            delete buffer;
            request->_tempObject = nullptr;
        } else {
            _serverLog("Getting more data...", "customserver::_setRestApi", LogLevel::DEBUG, request);
        }
    });

    _server.on("/rest/list-files", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        _serverLog("Request to get list of files", "customserver::_setRestApi", LogLevel::DEBUG, request);

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

        request->send(200, "application/json", _buffer.c_str());
    });

    _server.on("/rest/file/*", HTTP_GET, [this](AsyncWebServerRequest *request)
    {
        _serverLog("Request to get file", "customserver::_setRestApi", LogLevel::DEBUG, request);
    
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

            request->send(_file, _filename, contentType);
            _file.close();
        }
        else {
            request->send(400, "text/plain", "File not found");
        }
    });

    _server.serveStatic("/api-docs", SPIFFS, "/swagger-ui.html");
    _server.serveStatic("/swagger.yaml", SPIFFS, "/swagger.yaml");
    _server.serveStatic("/log-raw", SPIFFS, LOG_PATH);
    _server.serveStatic("/daily-energy", SPIFFS, DAILY_ENERGY_JSON_PATH);
}

void CustomServer::_setOtherEndpoints()
{
    _server.onNotFound([this](AsyncWebServerRequest *request)
                       {
        TRACE
        _serverLog(
            ("Request to get unknown page: " + request->url()).c_str(),
            "customserver::_setOtherEndpoints",
            LogLevel::DEBUG,
            request
        );
        request->send(404, "text/plain", "Not found"); });
}

void CustomServer::_handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
    _led.block();
    _led.setPurple(true);

    TRACE
    if (!index)
    {
        if (filename.indexOf(".bin") > -1)
        {
            _logger.warning("Update requested for firmware", "customserver::handleDoUpdate");
        }
        else
        {
            _onUpdateFailed(request, "File must be in .bin format");
            return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
        {
            _onUpdateFailed(request, Update.errorString());
            return;
        }

        Update.setMD5(_md5.c_str());
    }

    TRACE
    if (Update.write(data, len) != len)
    {
        _onUpdateFailed(request, Update.errorString());
        return;
    }

    TRACE
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

    TRACE
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
    TRACE
    request->send(200, "application/json", "{\"status\":\"success\", \"md5\":\"" + Update.md5String() + "\"}");

    TRACE
    _logger.warning("Update complete", "customserver::handleDoUpdate");
    _updateJsonFirmwareStatus("success", "");

    _logger.debug("MD5 of new firmware: %s", "customserver::_onUpdateSuccessful", Update.md5String().c_str());

    TRACE
    _logger.debug("Setting rollback flag to %s", "customserver::_onUpdateSuccessful", CrashMonitor::getFirmwareStatusString(NEW_TO_TEST));
    if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) _logger.error("Failed to set firmware status", "customserver::_onUpdateSuccessful");

    TRACE
    setRestartEsp32("customserver::_handleDoUpdate", "Restart needed after update");
}

void CustomServer::_onUpdateFailed(AsyncWebServerRequest *request, const char *reason)
{
    TRACE
    request->send(400, "application/json", "{\"status\":\"failed\", \"reason\":\"" + String(reason) + "\"}");

    Update.printError(Serial);
    _logger.debug("Size: %d bytes | Progress: %d bytes | Remaining: %d bytes", "customserver::_onUpdateFailed", Update.size(), Update.progress(), Update.remaining());
    _logger.warning("Update failed, keeping current firmware. Reason: %s", "customserver::_onUpdateFailed", reason);
    _updateJsonFirmwareStatus("failed", reason);

    for (int i = 0; i < 3; i++)
    {
        _led.setRed(true);
        delay(500);
        _led.setOff(true);
        delay(500);
    }
}

void CustomServer::_serveJsonFile(AsyncWebServerRequest *request, const char *filePath) {
    TRACE

    File file = SPIFFS.open(filePath, FILE_READ);

    if (file) {
        request->send(file, filePath, "application/json");
        file.close();
    } else {
        request->send(404, "application/json", "{\"message\":\"File not found\"}");
    }
}