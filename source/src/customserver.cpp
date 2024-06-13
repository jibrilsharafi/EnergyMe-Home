#include "customserver.h"

AsyncWebServer server(80);
Ticker tickerOnSuccess;

void setupServer() {
    _setHtmlPages();
    _setOta();
    _setRestApi();
    _setOtherEndpoints();

    server.begin();
    Update.onProgress([](size_t progress, size_t total) {
        logger.debug("Progress: %f%%", "customserver::setupServer", progress / (total / 100.0));
    });
}

void _serverLog(const char* message, const char* function, LogLevel logLevel, AsyncWebServerRequest *request) {
    String fullUrl = request->url();
    if (request->args() != 0) {
        fullUrl += "?";
        for (uint8_t i = 0; i < request->args(); i++) {
            if (i != 0) {
                fullUrl += "&";
            }
            fullUrl += request->argName(i) + "=" + request->arg(i);
        }
    }

    if (logLevel == LogLevel::DEBUG) {
        logger.debug("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    } else if (logLevel == LogLevel::INFO) {
        logger.info("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    } else if (logLevel == LogLevel::WARNING) {
        logger.warning("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    } else if (logLevel == LogLevel::ERROR) {
        logger.error("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    } else if (logLevel == LogLevel::FATAL) {
        logger.fatal("%s | IP: %s - Method: %s - URL: %s", function, message, request->client()->remoteIP().toString().c_str(), request->methodToString(), fullUrl.c_str());
    }
}

void _setHtmlPages() {
    // HTML pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get index page", "customserver::_setHtmlPages::/", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", index_html);
    });

    server.on("/configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get configuration page", "customserver::_setHtmlPages::/configuration", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", configuration_html);
    });

    server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get calibration page", "customserver::_setHtmlPages::/calibration", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", calibration_html);
    });

    server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get channel page", "customserver::_setHtmlPages::/channel", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", channel_html);
    });

    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get info page", "customserver::_setHtmlPages::/info", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", info_html);
    });

    server.on("/update-successful", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get update successful page", "customserver::_setHtmlPages::/update-successful", LogLevel::DEBUG, request);
        request->send_P(200, "text/html", update_successful_html);
    });

    // CSS
    server.on("/css/main.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::DEBUG, request);
        request->send_P(200, "text/css", main_css);
    });
    
    server.on("/css/button.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::DEBUG, request);
        request->send_P(200, "text/css", button_css);
    });

    server.on("/css/section.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::DEBUG, request);
        request->send_P(200, "text/css", section_css);
    });
    
    server.on("/css/typography.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", LogLevel::DEBUG, request);
        request->send_P(200, "text/css", typography_css);
    });

    // Other
    server.on("/images/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get favicon", "customserver::_setHtmlPages::/favicon.ico", LogLevel::DEBUG, request);
        request->send(SPIFFS, "/images/favicon.png", "image/png");
    });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get favicon", "customserver::_setHtmlPages::/favicon.ico", LogLevel::DEBUG, request);
        request->send(SPIFFS, "/images/favicon.png", "image/png");
    });
}

void _setOta() {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    _serverLog("Request to get update page", "customserver::_setOta::/update", LogLevel::DEBUG, request);
    request->send_P(200, "text/html", update_html);
    });

    server.on("/do-update", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                    size_t len, bool final) {_handleDoUpdate(request, filename, index, data, len, final);});
}

void _setRestApi() {
    server.on("/rest/is-alive", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to check if the ESP32 is alive", "customserver::_setRestApi::/rest/is-alive", LogLevel::DEBUG, request);

        request->send(200, "application/json", "{\"message\":\"True\"}");
    });

    server.on("/rest/device-info", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get device info from REST API", "customserver::_setRestApi::/rest/device-info", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(getDeviceStatus(), _buffer);
        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get WiFi values from REST API", "customserver::_setRestApi::/rest/wifi-info", LogLevel::DEBUG, request);
        
        String _buffer;
        serializeJson(getWifiStatus(), _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/meter", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(ade7953.meterValuesToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/meter-single", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter-single", LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (ade7953.channelData[_indexInt].active) {
                    String _buffer;
                    serializeJson(ade7953.singleMeterValuesToJson(_indexInt), _buffer);

                    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
                    request->send(response);
                } else {
                    request->send(400, "text/plain", "Channel not active");
                }
            } else {
                request->send(400, "text/plain", "Channel index out of range");
            }
        } else {
            request->send(400, "text/plain", "Missing index parameter");
        }
    });

    server.on("/rest/active-power", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get meter values from REST API", "customserver::_setRestApi::/rest/active-power", LogLevel::DEBUG, request);

        if (request->hasParam("index")) {
            int _indexInt = request->getParam("index")->value().toInt();

            if (_indexInt >= 0 && _indexInt <= MULTIPLEXER_CHANNEL_COUNT) {
                if (ade7953.channelData[_indexInt].active) {
                    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", String(ade7953.meterValues[_indexInt].activePower));
                    request->send(response);
                } else {
                    request->send(400, "text/plain", "Channel not active");
                }
            } else {
                request->send(400, "text/plain", "Channel index out of range");
            }
        } else {
            request->send(400, "text/plain", "Missing index parameter");
        }
    });

    server.on("/rest/get-ade7953-configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get ADE7953 configuration from REST API", "customserver::_setRestApi::/rest/get-ade7953-configuration", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(ade7953.configurationToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/get-channel", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get channel data from REST API", "customserver::_setRestApi::/rest/get-channel", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(ade7953.channelDataToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/set-channel", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog(
            "Request to set channel data from REST API",
            "customserver::_setRestApi::/rest/set-channel",
            LogLevel::WARNING,
            request
        );

        if (request->hasParam("index", false) && request->hasParam("label", false) && request->hasParam("calibration", false) && request->hasParam("active", false) && request->hasParam("reverse", false)) {
            int _index = request->getParam("index", false)->value().toInt();
            String _label = request->getParam("label", false)->value();
            String _calibration = request->getParam("calibration", false)->value();
            bool _active = request->getParam("active", false)->value().equalsIgnoreCase("true");
            bool _reverse = request->getParam("reverse", false)->value().equalsIgnoreCase("true");

            if (_index >= 0 && _index <= MULTIPLEXER_CHANNEL_COUNT) {
                ade7953.channelData[_index].label = _label;
                ade7953.channelData[_index].calibrationValues.label = _calibration;
                ade7953.channelData[_index].active = _active;
                ade7953.channelData[_index].reverse = _reverse;

                ade7953.updateDataChannel();
                ade7953.saveChannelDataToSpiffs();

                publishChannel();

                AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Channel data set\"}");
                request->send(response);
            } else {
                AsyncWebServerResponse *response = request->beginResponse(400, "text/plain", "Channel index out of range");
                request->send(response);
            }
        } else {
            AsyncWebServerResponse *response = request->beginResponse(400, "text/plain", "Missing parameter");
            request->send(response);
        }
    });

    server.on("/rest/get-calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get configuration from REST API", "customserver::_setRestApi::/rest/get-calibration", LogLevel::DEBUG, request);

        String _buffer;
        serializeJson(ade7953.calibrationValuesToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/calibration-reset", HTTP_POST, [&](AsyncWebServerRequest *request) {
        _serverLog("Request to reset calibration values from REST API", "customserver::_setRestApi::/rest/calibration-reset", LogLevel::WARNING, request);
        ade7953.setDefaultCalibrationValues();
        ade7953.setDefaultChannelData();

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Calibration values reset\"}");
        request->send(response);
    });

    server.on("/rest/reset-energy", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to reset energy counters from REST API", "customserver::_setRestApi::/rest/reset-energy", LogLevel::WARNING, request);

        ade7953.resetEnergyValues();

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Energy counters reset\"}");
        request->send(response);
    });

    server.on("/rest/get-log-level", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get log level from REST API", "customserver::_setRestApi::/rest/get-log-level", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;
        // _jsonDocument["print"] = logger.getPrintLevel();
        _jsonDocument["print"] = logger.logLevelToString(logger.getPrintLevel());
        _jsonDocument["save"] = logger.logLevelToString(logger.getSaveLevel());

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/set-log-level", HTTP_GET, [](AsyncWebServerRequest *request) {
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
                logger.setPrintLevel(LogLevel(_level));
            } else if (_type == "save") {
                logger.setSaveLevel(LogLevel(_level));
            } else {
                request->send(400, "text/plain", "Unknown type parameter provided. No changes made");
            }
            request->send(200, "application/json", "{\"message\":\"Success\"}");
        } else {
            request->send(400, "text/plain", "No level parameter provided. No changes made");
        }
    });

    server.on("/rest/get-general-configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get get general configuration from REST API", "customserver::_setRestApi::/rest/get-general-configuration", LogLevel::DEBUG, request);

        JsonDocument _jsonDocument;

        _jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
        _jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
        _jsonDocument["dstOffset"] = generalConfiguration.dstOffset;

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/ade7953-read-register", HTTP_GET, [](AsyncWebServerRequest *request) {
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
                    long registerValue = ade7953.readRegister(_address, _nBits, _signed);

                    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", String(registerValue).c_str());
                    request->send(response);
                } else {
                    request->send(400, "text/plain", "Address out of range. Supported values: 0-0x3FF (0-1023)");
                }
            } else {
                request->send(400, "text/plain", "Number of bits not supported. Supported values: 8, 16, 24, 32");
            }
        } else {
            request->send(400, "text/plain", "Missing parameter. Required: address (int), nBits (int), signed (bool)");
        }
    });

    server.on("/rest/ade7953-write-register", HTTP_POST, [](AsyncWebServerRequest *request) {
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
                    ade7953.writeRegister(_address, _nBits, _data);

                    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Success");
                    request->send(response);
                } else {
                    request->send(400, "text/plain", "Address out of range. Supported values: 0-0x3FF (0-1023)");
                }
            } else {
                request->send(400, "text/plain", "Number of bits not supported. Supported values: 8, 16, 24, 32");
            }
        } else {
            request->send(400, "text/plain", "Missing parameter. Required: address (int), nBits (int), data (int)");
        }
    });

    server.on("/rest/save-daily-energy", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to save daily energy to SPIFFS from REST API", "customserver::_setRestApi::/rest/save-daily-energy", LogLevel::DEBUG, request);

        ade7953.saveDailyEnergyToSpiffs();

        request->send(200, "application/json", "{\"message\":\"Daily energy saved\"}");
    });

    server.on("/rest/file/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        _serverLog("Request to get file from REST API", "customserver::_setRestApi::/rest/file/*", LogLevel::DEBUG, request);

        String _filename = request->url().substring(10);
        File _file = SPIFFS.open(_filename, "r");
        if (_file) {
            request->send(_file, "text/plain");
            _file.close();
        }
        else {request->send(400, "text/plain", "File not found");}
    });

    server.on("/rest/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to factory reset from REST API", "customserver::_setRestApi::/rest/factory-reset", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Factory reset in progress. Check the logs for more information.\"}");
        factoryReset();
    });

    server.on("/rest/clear-log", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to clear log from REST API", "customserver::_setRestApi::/rest/clear-log", LogLevel::DEBUG, request);

        logger.clearLog();

        request->send(200, "application/json", "{\"message\":\"Log cleared\"}");
    });
    
    server.on("/rest/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to restart the ESP32 from REST API", "customserver::_setRestApi::/rest/restart", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Restarting...\"}");
        restartEsp32("customserver::_setRestApi", "Request to restart the ESP32 from REST API");
    });

    server.on("/rest/reset-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        _serverLog("Request to erase WiFi credentials from REST API", "customserver::_setRestApi::/rest/reset-wifi", LogLevel::WARNING, request);

        request->send(200, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
        resetWifi();
        restartEsp32("customserver::_setRestApi", "Request to erase WiFi credentials from REST API");
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){

        if (request->url() == "/rest/set-calibration") {   
            _serverLog("Request to set calibration values from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-calibration", LogLevel::INFO, request);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);
            JsonArray jsonArray = _jsonDocument.as<JsonArray>();

            ade7953.setCalibrationValues(ade7953.parseJsonCalibrationValues(jsonArray));
            ade7953.updateDataChannel();
            
            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Calibration values set\"}");
            request->send(response);

        } else if (request->url() == "/rest/set-ade7953-configuration") {
            _serverLog("Request to set ADE7953 configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-ade7953-configuration", LogLevel::INFO, request);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);

            ade7953.setConfiguration(ade7953.parseJsonConfiguration(_jsonDocument));

            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Configuration updated\"}");
            request->send(response);

        } else if (request->url() == "/rest/set-general-configuration") {
            _serverLog("Request to set general configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/set-general-configuration", LogLevel::INFO, request);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);

            setGeneralConfiguration(jsonToGeneralConfiguration(_jsonDocument));
            saveGeneralConfigurationToSpiffs();
            publishGeneralConfiguration();

            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Configuration updated\"}");
            request->send(response);

        } else {
            _serverLog(
                ("Request to POST to unknown endpoint: " + request->url()).c_str(),
                "customserver::_setRestApi::onRequestBody",
                LogLevel::WARNING,
                request
            );
            request->send(404, "text/plain", "Not found");
        }
    });

    server.serveStatic("/log", SPIFFS, LOG_PATH);
    server.serveStatic("/daily-energy", SPIFFS, DAILY_ENERGY_JSON_PATH);
}

void _setOtherEndpoints() {
    server.onNotFound([](AsyncWebServerRequest *request) {
        _serverLog(
            ("Request to get unknown page: " + request->url()).c_str(),
            "customserver::_setOtherEndpoints::onNotFound",
            LogLevel::INFO,
            request
        );
        request->send(404, "text/plain", "Not found");
    });
}

void _handleDoUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    led.block();
    led.setPurple(true);
    if (!index){
        int _cmd;
        if (filename.indexOf("spiffs") > -1) {
            logger.warning("Update requested for SPIFFS", "customserver::handleDoUpdate");
            _cmd = U_SPIFFS;
        } else if (filename.indexOf("firmware") > -1) {
            logger.warning("Update requested for firmware", "customserver::handleDoUpdate");
            _cmd = U_FLASH;
        } else {
            logger.error("Update requested with unknown file type. Aborting...", "customserver::handleDoUpdate");
            return;
        } 
            
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, _cmd)) {
        Update.printError(Serial);
        logger.error("Update failed", "customserver::handleDoUpdate");
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        logger.error("Update failed", "customserver::handleDoUpdate");
        for (int i = 0; i < 3; i++) {
            led.setRed(true);
            delay(1000);
            led.setOff(true);
            delay(1000);
        }
    }

    if (final) {
        if (!Update.end(true)) {
            Update.printError(Serial);
            logger.error("Update failed", "customserver::handleDoUpdate");
            request->send_P(200, "text/html", update_failed_html);

            for (int i = 0; i < 3; i++) {
                led.setRed(true);
                delay(200);
                led.setOff(true);
                delay(200);
            }
        } else {
            logger.warning("Update complete", "customserver::handleDoUpdate");
            request->send_P(200, "text/html", update_successful_html);
            tickerOnSuccess.once_ms(500, _onUpdateSuccessful);
        }
    }
    led.setOff(true);
    led.unblock();
}

void _onUpdateSuccessful() {
    ade7953.saveEnergyToSpiffs();
    restartEsp32("customserver::_handleDoUpdate", "Restart needed after update");
}