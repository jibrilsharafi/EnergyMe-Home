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
        logger.log(
            (
                "Progress: " + 
                String((progress / (total / 100.0))) + 
                "%"
            ).c_str(),
            "customserver::setupServer",
            CUSTOM_LOG_LEVEL_DEBUG
        );
    });
}

void _setHtmlPages() {
    // HTML pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get index page", "customserver::_setHtmlPages::/", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", index_html);
    });

    server.on("/configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get configuration page", "customserver::_setHtmlPages::/configuration", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", configuration_html);
    });

    server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get calibration page", "customserver::_setHtmlPages::/calibration", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", calibration_html);
    });

    server.on("/channel", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get channel page", "customserver::_setHtmlPages::/channel", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", channel_html);
    });

    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get info page", "customserver::_setHtmlPages::/info", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", info_html);
    });

    server.on("/update-successful", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get update successful page", "customserver::_setHtmlPages::/update-successful", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/html", update_successful_html);
    });

    // CSS
    server.on("/css/main.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/css", main_css);
    });
    
    server.on("/css/button.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/css", button_css);
    });

    server.on("/css/section.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/css", section_css);
    });
    
    server.on("/css/typography.css", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get custom CSS", "customserver::_setHtmlPages::/css/style.css", CUSTOM_LOG_LEVEL_DEBUG);
        request->send_P(200, "text/css", typography_css);
    });

    // Other
    server.on("/images/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get favicon", "customserver::_setHtmlPages::/favicon.ico", CUSTOM_LOG_LEVEL_DEBUG);
        request->send(SPIFFS, "/images/favicon.png", "image/png");
    });

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get favicon", "customserver::_setHtmlPages::/favicon.ico", CUSTOM_LOG_LEVEL_DEBUG);
        request->send(SPIFFS, "/images/favicon.png", "image/png");
    });
}

void _setOta() {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
      logger.log("Request to get update page", "customserver::_setOta::/update", CUSTOM_LOG_LEVEL_DEBUG);
    request->send_P(200, "text/html", update_html);
    });

    server.on("/do-update", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                    size_t len, bool final) {_handleDoUpdate(request, filename, index, data, len, final);});
}

void _setRestApi() {
    server.on("/rest/is-alive", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to check if the ESP32 is alive", "customserver::_setRestApi::/rest/is-alive", CUSTOM_LOG_LEVEL_DEBUG);
        request->send(200, "application/json", "{\"message\":\"True\"}");
    });

    server.on("/rest/device-info", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get device info from REST API", "customserver::_setRestApi::/rest/device-info", CUSTOM_LOG_LEVEL_DEBUG);

        String _buffer;
        serializeJson(getDeviceStatus(), _buffer);
        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get WiFi values from REST API", "customserver::_setRestApi::/rest/wifi-info", CUSTOM_LOG_LEVEL_DEBUG);
        
        String _buffer;
        serializeJson(getWifiStatus(), _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/meter", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter", CUSTOM_LOG_LEVEL_DEBUG);

        String _buffer;
        serializeJson(ade7953.meterValuesToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/meter-single", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get meter values from REST API", "customserver::_setRestApi::/rest/meter-single", CUSTOM_LOG_LEVEL_DEBUG);

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
        logger.log("Request to get meter values from REST API", "customserver::_setRestApi::/rest/active-power", CUSTOM_LOG_LEVEL_DEBUG);

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

    server.on("/rest/configuration/ade7953", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get ADE7953 configuration from REST API", "customserver::_setRestApi::/rest/configuration/ade7953", CUSTOM_LOG_LEVEL_DEBUG);

        String _buffer;
        serializeJson(ade7953.configurationToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/get-channel", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get channel data from REST API", "customserver::_setRestApi::/rest/get-channel", CUSTOM_LOG_LEVEL_DEBUG);

        String _buffer;
        serializeJson(ade7953.channelDataToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/set-channel", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to set channel data from REST API", "customserver::_setRestApi::/rest/set-channel", CUSTOM_LOG_LEVEL_WARNING);

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

    server.on("/rest/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get configuration from REST API", "customserver::_setRestApi::/rest/calibration", CUSTOM_LOG_LEVEL_DEBUG);

        String _buffer;
        serializeJson(ade7953.calibrationValuesToJson(), _buffer);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _buffer.c_str());
        request->send(response);
    });

    server.on("/rest/calibration/reset", HTTP_POST, [&](AsyncWebServerRequest *request) {
        logger.log("Request to reset calibration values from REST API", "customserver::_setRestApi::/rest/calibration/reset", CUSTOM_LOG_LEVEL_WARNING);

        ade7953.setDefaultCalibrationValues();
        ade7953.setDefaultChannelData();

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Calibration values reset\"}");
        request->send(response);
    });

    server.on("/rest/reset-energy", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to reset energy counters from REST API", "customserver::_setRestApi::/rest/reset-energy", CUSTOM_LOG_LEVEL_WARNING);

        ade7953.resetEnergyValues();

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Energy counters reset\"}");
        request->send(response);
    });

    server.on("/rest/get-log", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get log level from REST API", "customserver::_setRestApi::/rest/get-log", CUSTOM_LOG_LEVEL_DEBUG);

        JsonDocument _jsonDocument;
        _jsonDocument["print"] = logger.getPrintLevel();
        _jsonDocument["save"] = logger.getSaveLevel();

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/set-log", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to print log from REST API", "customserver::_setRestApi::/rest/set-log", CUSTOM_LOG_LEVEL_DEBUG);

        // Both level and type
        if (request->hasParam("level") && request->hasParam("type")) {
            int _level = request->getParam("level")->value().toInt();
            String _type = request->getParam("type")->value();
            
            if (_type == "print") {
                logger.setPrintLevel(_level);
            } else if (_type == "save") {
                logger.setSaveLevel(_level);
            } else {
                logger.log("Unknown type parameter provided. Using default value.", "customserver::_setRestApi::/rest/set-log", CUSTOM_LOG_LEVEL_WARNING);
                request->send(400, "text/plain", "Unknown type parameter provided. Using default value.");
            }
            request->send(200, "application/json", "{\"message\":\"Success\"}");
        } else {
            logger.log("Both level and type parameter must be provided. Using default value.", "customserver::_setRestApi::/rest/set-log", CUSTOM_LOG_LEVEL_WARNING);
            request->send(400, "text/plain", "No level parameter provided. Using default value.");
        }
    });

    server.on("/rest/get-general-configuration", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get get general configuration from REST API", "customserver::_setRestApi::/rest/get-general-configuration", CUSTOM_LOG_LEVEL_DEBUG);

        JsonDocument _jsonDocument;

        _jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/ade7953/read-register", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get ADE7953 register value from REST API", "customserver::_setRestApi::/rest/ade7953/read-register", CUSTOM_LOG_LEVEL_DEBUG);

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

    server.on("/rest/ade7953/write-register", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to get ADE7953 register value from REST API", "customserver::_setRestApi::/rest/ade7953/read-register", CUSTOM_LOG_LEVEL_DEBUG);

        if (request->hasParam("address") && request->hasParam("nBits") && request->hasParam("data")) {
            int _address = request->getParam("address")->value().toInt();
            int _nBits = request->getParam("nBits")->value().toInt();
            int      _data = request->getParam("data")->value().toInt();

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

    server.on("/rest/list-files", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to list SPIFFS files from REST API", "customserver::_setRestApi::/rest/list-files", CUSTOM_LOG_LEVEL_DEBUG);

        JsonDocument _jsonDocument;

        File _root = SPIFFS.open("/");
        File _file = _root.openNextFile();

        while (_file)
        {
            _jsonDocument[_file.name()] = _file.size();
            _file = _root.openNextFile();
        }

        String _buffer;
        serializeJson(_jsonDocument, _buffer);

        request->send(200, "application/json", _buffer.c_str());
    });

    server.on("/rest/file/*", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log("Request to get file from REST API", "customserver::_setRestApi::/rest/file/*", CUSTOM_LOG_LEVEL_DEBUG);

        String _filename = request->url().substring(10);
        File _file = SPIFFS.open(_filename, "r");
        if (_file) {
            request->send(_file, "text/plain");
            _file.close();
        }
        else {request->send(400, "text/plain", "File not found");}
    });

    server.on("/rest/clear-log", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to clear log from REST API", "customserver::_setRestApi::/rest/clear-log", CUSTOM_LOG_LEVEL_WARNING);

        logger.clearLog();

        request->send(200, "application/json", "{\"message\":\"Log cleared\"}");
    });
    
    server.on("/rest/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to restart the ESP32 from REST API", "customserver::_setRestApi::/rest/restart", CUSTOM_LOG_LEVEL_WARNING);

        request->send(200, "application/json", "{\"message\":\"Restarting...\"}");
        restartEsp32("customserver::_setRestApi", "Request to restart the ESP32 from REST API");
    });

    server.on("/rest/reset-wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log("Request to erase WiFi credentials from REST API", "customserver::_setRestApi::/rest/reset-wifi", CUSTOM_LOG_LEVEL_WARNING);

        request->send(200, "application/json", "{\"message\":\"Erasing WiFi credentials and restarting...\"}");
        resetWifi();
        restartEsp32("customserver::_setRestApi", "Request to erase WiFi credentials from REST API");
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){

        if (request->url() == "/rest/calibration") {   
            logger.log("Request to set calibration values from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/calibration", CUSTOM_LOG_LEVEL_WARNING);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);
            JsonArray jsonArray = _jsonDocument.as<JsonArray>();

            ade7953.setCalibrationValues(ade7953.parseJsonCalibrationValues(jsonArray));
            ade7953.updateDataChannel();
            
            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Calibration values set\"}");
            request->send(response);

        } else if (request->url() == "/rest/configuration/ade7953") {
            logger.log("Request to set ADE7953 configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/configuration/ade7953", CUSTOM_LOG_LEVEL_WARNING);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);

            ade7953.setConfiguration(ade7953.parseJsonConfiguration(_jsonDocument));

            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Configuration updated\"}");
            request->send(response);

        } else if (request->url() == "/rest/configuration/general") {
            logger.log("Request to set general configuration from REST API (POST)", "customserver::_setRestApi::onRequestBody::/rest/configuration/general", CUSTOM_LOG_LEVEL_WARNING);

            JsonDocument _jsonDocument;
            deserializeJson(_jsonDocument, data);

            setGeneralConfiguration(jsonToGeneralConfiguration(_jsonDocument));
            saveGeneralConfigurationToSpiffs();
            publishGeneralConfiguration();

            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"message\":\"Configuration updated\"}");
            request->send(response);

        } else {
            logger.log(
                ("Request to POST to unknown endpoint: " + request->url()).c_str(),
                "customserver::_setRestApi::onRequestBody",
                CUSTOM_LOG_LEVEL_WARNING
            );
            request->send(404, "text/plain", "Not found");
        }
    });

    server.serveStatic("/log", SPIFFS, "/log.txt");
    server.serveStatic("/daily-energy", SPIFFS, "/energy.json");
}

void _setOtherEndpoints() {
    server.onNotFound([](AsyncWebServerRequest *request) {
        logger.log(
            ("Request to get unknown page: " + request->url()).c_str(),
            "customserver::_setOtherEndpoints::onNotFound",
            CUSTOM_LOG_LEVEL_WARNING
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
            logger.log("Update requested for SPIFFS", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_WARNING);
            _cmd = U_SPIFFS;
        } else if (filename.indexOf("firmware") > -1) {
            logger.log("Update requested for firmware", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_WARNING);
            _cmd = U_FLASH;
        } else {
            logger.log("Update requested with unknown file type. Aborting...", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_ERROR);
            return;
        } 
            
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, _cmd)) {
        Update.printError(Serial);
        logger.log("Update failed", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_ERROR);
        }
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
        logger.log("Update failed", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_ERROR);
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
            logger.log("Update failed", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_ERROR);
            request->send_P(200, "text/html", update_failed_html);

            for (int i = 0; i < 3; i++) {
                led.setRed(true);
                delay(200);
                led.setOff(true);
                delay(200);
            }
        } else {
            logger.log("Update complete", "customserver::handleDoUpdate", CUSTOM_LOG_LEVEL_WARNING);
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