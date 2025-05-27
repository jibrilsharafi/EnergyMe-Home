#include "influxdbclient.h"
#include <base64.h>

static const char *TAG = "influxdbclient";

InfluxDbClient::InfluxDbClient(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    InfluxDbConfiguration &influxDbConfiguration,
    CustomTime &customTime) : _ade7953(ade7953),
                            _logger(logger),
                            _influxDbConfiguration(influxDbConfiguration),
                            _customTime(customTime) {}

void InfluxDbClient::begin()
{
    _logger.debug("Setting up InfluxDB client...", TAG);

    _setConfigurationFromSpiffs();

    _baseUrl = String("http") + (_influxDbConfiguration.useSSL ? "s" : "") + "://" + _influxDbConfiguration.server + ":" + String(_influxDbConfiguration.port);
    _logger.debug("Base URL set to: %s", TAG, _baseUrl.c_str());

    if (_influxDbConfiguration.enabled) {
        _isSetupDone = true;
        _nextInfluxDbConnectionAttemptMillis = millis(); // Try connecting immediately
        _influxDbConnectionAttempt = 0;
        
        // Test connection to InfluxDB
        if (!_testConnection()) {
            _logger.warning("Failed to connect to InfluxDB on startup. Retrying later...", TAG);
            _lastMillisInfluxDbFailed = millis();
            _influxDbConfiguration.lastConnectionStatus = "Failed to ping InfluxDB";
            _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
        } else {
            _logger.info("Successfully connected to InfluxDB", TAG);
            _influxDbConfiguration.lastConnectionStatus = "Ping successful";
            _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();

            if (!_testCredentials()) {
                _logger.warning("Failed to validate InfluxDB credentials on startup. Retrying later...", TAG);
                _lastMillisInfluxDbFailed = millis();
                _influxDbConfiguration.lastConnectionStatus = "Failed to validate InfluxDB credentials";
                _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
            } else {
                _logger.info("Successfully validated InfluxDB credentials", TAG);
                _influxDbConfiguration.lastConnectionStatus = "Credentials valid";
                _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
            }
        }
    }
}

void InfluxDbClient::loop()
{
    if ((millis() - _lastMillisInfluxDbLoop) < INFLUXDB_LOOP_INTERVAL) return;
    _lastMillisInfluxDbLoop = millis();

    if (!WiFi.isConnected()) return;

    if (!_influxDbConfiguration.enabled)
    {
        if (_isSetupDone)
        {
            _logger.info("InfluxDB disabled, clearing buffer", TAG);
            _bufferMeterValues.clear();
            _isSetupDone = false;
        }
        return;
    }

    if (!_isSetupDone) { begin(); return; }

    // Buffer meter data at the specified frequency
    if ((millis() - _lastMillisMeterPublish) > _influxDbConfiguration.frequency * 1000)
    {
        _lastMillisMeterPublish = millis();
        _bufferMeterData();
    }

    // Upload data if buffer is full or enough time has passed since last upload
    bool bufferFull = _bufferMeterValues.isFull();
    bool timeToUpload = (millis() - _lastMillisInfluxDbFailed) > (_influxDbConfiguration.frequency * 1000 * 2); // Upload at least every 2x frequency
    
    if (!_bufferMeterValues.isEmpty() && (bufferFull || timeToUpload)) {
        _uploadBufferedData();
    }
}

void InfluxDbClient::_bufferMeterData()
{
    _logger.debug("Buffering meter data for InfluxDB...", TAG);

    if (!_customTime.isTimeSynched()) {
        _logger.warning("Time not synced, skipping data buffering", TAG);
        return;
    }

    unsigned long long timestamp = _customTime.getUnixTimeMilliseconds();

    // Buffer all active channels
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (_ade7953.channelData[i].active) {
            BufferedPoint point(_ade7953.meterValues[i], i, timestamp);
            _bufferMeterValues.push(point);
            
            if (_bufferMeterValues.isFull()) {
                _logger.debug("Buffer full, will upload on next loop", TAG);
                break;
            }
        }
    }

    _logger.debug("Meter data buffered. Buffer size: %d/%d", TAG, _bufferMeterValues.size(), INFLUXDB_BUFFER_SIZE);
}

void InfluxDbClient::_uploadBufferedData()
{
    if (_bufferMeterValues.isEmpty()) return;

    _logger.debug("Uploading %d buffered points to InfluxDB...", TAG, _bufferMeterValues.size());

    HTTPClient http;
    String url;
    
    if (_influxDbConfiguration.version == 2) {
        url = _baseUrl + "/api/v2/write?org=" + _influxDbConfiguration.organization + "&bucket=" + _influxDbConfiguration.bucket;
    } else {
        url = _baseUrl + "/write?db=" + _influxDbConfiguration.database;
    }

    http.begin(url);
    http.addHeader("Content-Type", "text/plain");
    
    // Set authorization header based on InfluxDB version
    if (_influxDbConfiguration.version == 2) {
        String authHeader = "Token " + _influxDbConfiguration.token;
        http.addHeader("Authorization", authHeader);
    } else if (_influxDbConfiguration.version == 1) {
        String credentials = _influxDbConfiguration.username + ":" + _influxDbConfiguration.password;
        String encodedCredentials = base64::encode(credentials);
        String authHeader = "Basic " + encodedCredentials;
        http.addHeader("Authorization", authHeader);
    }

    // Build line protocol payload
    String payload = "";
    unsigned int pointCount = 0;
    
    while (!_bufferMeterValues.isEmpty() && pointCount < _bufferMeterValues.size()) {
        BufferedPoint point = _bufferMeterValues.shift();
        String lineProtocol = _formatLineProtocol(point.meterValues, point.channel, point.timestamp);
        
        if (payload.length() + lineProtocol.length() > 30000) { // Keep under safe HTTP limit
            _logger.warning("Payload too large, pushing point back to buffer", TAG);
            _bufferMeterValues.unshift(point);
            break;
        }
        
        if (payload.length() > 0) payload += "\n";
        payload += lineProtocol;
        pointCount++;
    }

    int httpCode = http.POST(payload);

    if (httpCode >= 200 && httpCode < 300) {
        _logger.debug("Successfully uploaded %d points to InfluxDB", TAG, pointCount);
        _influxDbConfiguration.lastConnectionStatus = "Upload successful";
        _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
        _saveConfigurationToSpiffs();
    } else {
        _logger.error("Failed to upload to InfluxDB. HTTP code: %d, Response: %s", TAG, httpCode, http.getString().c_str());
        _lastMillisInfluxDbFailed = millis();
        _influxDbConfiguration.lastConnectionStatus = "Upload failed (HTTP " + String(httpCode) + ")";
        _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
        _saveConfigurationToSpiffs();
        
        // Put points back in buffer for retry (if there's space)
        while (pointCount-- > 0 && !_bufferMeterValues.isFull()) {
            BufferedPoint point = _bufferMeterValues.shift();
            _bufferMeterValues.push(point);
        }
    }

    http.end();
}

String InfluxDbClient::_formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp)
{
    String channelLabel = _ade7953.channelData[channel].label;
    channelLabel.replace(" ", "_");
    channelLabel.replace(",", "_");
    channelLabel.replace("=", "_");
    channelLabel.replace(":", "_");
    
    String line = _influxDbConfiguration.measurement;
    line += ",channel=" + String(channel);
    line += ",label=" + channelLabel;
    
    line += " voltage=" + String(meterValues.voltage, 2);
    line += ",current=" + String(meterValues.current, 3);
    line += ",active_power=" + String(meterValues.activePower, 2);
    line += ",reactive_power=" + String(meterValues.reactivePower, 2);
    line += ",apparent_power=" + String(meterValues.apparentPower, 2);
    line += ",power_factor=" + String(meterValues.powerFactor, 3);
    line += ",active_energy_imported=" + String(meterValues.activeEnergyImported, 3);
    line += ",active_energy_exported=" + String(meterValues.activeEnergyExported, 3);
    line += ",reactive_energy_imported=" + String(meterValues.reactiveEnergyImported, 3);
    line += ",reactive_energy_exported=" + String(meterValues.reactiveEnergyExported, 3);
    line += ",apparent_energy=" + String(meterValues.apparentEnergy, 3);
    
    line += " " + String(timestamp) + "000000"; // Convert to nanoseconds
    
    return line;
}

void InfluxDbClient::_disable() {
    _logger.debug("Disabling InfluxDB...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    _jsonDocument["enabled"] = false;

    setConfiguration(_jsonDocument);
    
    _isSetupDone = false;
    _influxDbConnectionAttempt = 0;
    _bufferMeterValues.clear();

    _logger.debug("InfluxDB disabled", TAG);
}

bool InfluxDbClient::_testConnection()
{
    _logger.debug("Testing InfluxDB connection...", TAG);

    if (!_influxDbConfiguration.enabled)
    {
        _logger.warning("InfluxDB is not enabled. Skipping connection test", TAG);
        return false;
    }

    HTTPClient http;
    String url = _baseUrl + "/ping";

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 204)
    {
        _logger.debug("InfluxDB connection successful (204 No Content)", TAG);
        http.end();
        return true;
    }
    else if (httpCode > 0)
    {
        _logger.debug("InfluxDB connection response: %s", TAG, http.getString().c_str());
        http.end();
        return httpCode >= 200 && httpCode < 300;
    }
    else
    {
        _logger.error("InfluxDB connection failed: %s", TAG, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }
}

bool InfluxDbClient::_testCredentials()
{
    _logger.debug("Testing InfluxDB credentials...", TAG);

    if (!_influxDbConfiguration.enabled)
    {
        _logger.warning("InfluxDB is not enabled. Skipping credentials test", TAG);
        return false;
    }

    HTTPClient http;
    String url = _baseUrl + "/api/v2";

    http.begin(url);
    
    // Set authorization header based on InfluxDB version
    if (_influxDbConfiguration.version == 2)
    {
        // InfluxDB v2 uses Token authentication
        String authHeader = "Token " + _influxDbConfiguration.token;
        http.addHeader("Authorization", authHeader);
    }
    else if (_influxDbConfiguration.version == 1)
    {
        // InfluxDB v1.x compatibility API uses Basic authentication
        String credentials = _influxDbConfiguration.username + ":" + _influxDbConfiguration.password;
        String encodedCredentials = base64::encode(credentials);
        String authHeader = "Basic " + encodedCredentials;
        http.addHeader("Authorization", authHeader);
    }

    int httpCode = http.GET();

    if (httpCode == 200)
    {
        _logger.debug("InfluxDB credentials test successful (200 OK)", TAG);
        http.end();
        return true;
    }
    else if (httpCode == 401)
    {
        _logger.error("InfluxDB credentials test failed: Unauthorized (401)", TAG);
        http.end();
        return false;
    }
    else if (httpCode > 0)
    {
        _logger.debug("InfluxDB credentials test response: %s", TAG, http.getString().c_str());
        http.end();
        return httpCode >= 200 && httpCode < 300;
    }
    else
    {
        _logger.error("InfluxDB credentials test failed: %s", TAG, http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }
}

void InfluxDbClient::_setDefaultConfiguration()
{
    _logger.debug("Setting default InfluxDB configuration...", TAG);

    createDefaultInfluxDbConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    setConfiguration(_jsonDocument);

    _logger.debug("Default InfluxDB configuration set", TAG);
}

bool InfluxDbClient::setConfiguration(JsonDocument &jsonDocument)
{
    _logger.debug("Setting InfluxDB configuration...", TAG);

    if (!_validateJsonConfiguration(jsonDocument))
    {
        _logger.error("Invalid InfluxDB configuration", TAG);
        return false;
    }

    _influxDbConfiguration.enabled = jsonDocument["enabled"].as<bool>();
    _influxDbConfiguration.server = jsonDocument["server"].as<String>();
    _influxDbConfiguration.port = jsonDocument["port"].as<int>();
    _influxDbConfiguration.version = jsonDocument["version"].as<int>();
    _influxDbConfiguration.database = jsonDocument["database"].as<String>();
    _influxDbConfiguration.username = jsonDocument["username"].as<String>();
    _influxDbConfiguration.password = jsonDocument["password"].as<String>();
    _influxDbConfiguration.organization = jsonDocument["organization"].as<String>();
    _influxDbConfiguration.bucket = jsonDocument["bucket"].as<String>();
    _influxDbConfiguration.token = jsonDocument["token"].as<String>();
    _influxDbConfiguration.measurement = jsonDocument["measurement"].as<String>();
    _influxDbConfiguration.frequency = jsonDocument["frequency"].as<int>();
    _influxDbConfiguration.useSSL = jsonDocument["useSSL"].as<bool>();

    _saveConfigurationToSpiffs();
    
    _influxDbConfiguration.lastConnectionStatus = "Disconnected";
    _influxDbConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();

    _logger.debug("InfluxDB configuration set", TAG);

    return true;
}

void InfluxDbClient::_setConfigurationFromSpiffs()
{
    _logger.debug("Setting InfluxDB configuration from SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (!setConfiguration(_jsonDocument))
    {
        _logger.error("Failed to set InfluxDB configuration from SPIFFS. Using default one", TAG);
        _setDefaultConfiguration();
        return;
    }

    _logger.debug("Successfully set InfluxDB configuration from SPIFFS", TAG);
}

void InfluxDbClient::_saveConfigurationToSpiffs()
{
    _logger.debug("Saving InfluxDB configuration to SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    _jsonDocument["enabled"] = _influxDbConfiguration.enabled;
    _jsonDocument["server"] = _influxDbConfiguration.server;
    _jsonDocument["port"] = _influxDbConfiguration.port;
    _jsonDocument["version"] = _influxDbConfiguration.version;
    _jsonDocument["database"] = _influxDbConfiguration.database;
    _jsonDocument["username"] = _influxDbConfiguration.username;
    _jsonDocument["password"] = _influxDbConfiguration.password;
    _jsonDocument["organization"] = _influxDbConfiguration.organization;
    _jsonDocument["bucket"] = _influxDbConfiguration.bucket;
    _jsonDocument["token"] = _influxDbConfiguration.token;
    _jsonDocument["measurement"] = _influxDbConfiguration.measurement;
    _jsonDocument["frequency"] = _influxDbConfiguration.frequency;
    _jsonDocument["useSSL"] = _influxDbConfiguration.useSSL;
    _jsonDocument["lastConnectionStatus"] = _influxDbConfiguration.lastConnectionStatus;
    _jsonDocument["lastConnectionAttemptTimestamp"] = _influxDbConfiguration.lastConnectionAttemptTimestamp;

    serializeJsonToSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    _logger.debug("Successfully saved InfluxDB configuration to SPIFFS", TAG);
}

bool InfluxDbClient::_validateJsonConfiguration(JsonDocument &jsonDocument)
{
    if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
    {
        _logger.warning("Invalid JSON document", TAG);
        return false;
    }

    if (!jsonDocument["enabled"].is<bool>()) { _logger.warning("enabled field is not a boolean", TAG); return false; }
    if (!jsonDocument["server"].is<String>()) { _logger.warning("server field is not a string", TAG); return false; }
    if (!jsonDocument["port"].is<int>()) { _logger.warning("port field is not an integer", TAG); return false; }
    if (!jsonDocument["version"].is<int>()) { _logger.warning("version field is not an integer", TAG); return false; }
    if (!jsonDocument["database"].is<String>()) { _logger.warning("database field is not a string", TAG); return false; }
    if (!jsonDocument["username"].is<String>()) { _logger.warning("username field is not a string", TAG); return false; }
    if (!jsonDocument["password"].is<String>()) { _logger.warning("password field is not a string", TAG); return false; }
    if (!jsonDocument["organization"].is<String>()) { _logger.warning("organization field is not a string", TAG); return false; }
    if (!jsonDocument["bucket"].is<String>()) { _logger.warning("bucket field is not a string", TAG); return false; }
    if (!jsonDocument["token"].is<String>()) { _logger.warning("token field is not a string", TAG); return false; }
    if (!jsonDocument["measurement"].is<String>()) { _logger.warning("measurement field is not a string", TAG); return false; }
    if (!jsonDocument["frequency"].is<int>()) { _logger.warning("frequency field is not an integer", TAG); return false; }
    if (!jsonDocument["useSSL"].is<bool>()) { _logger.warning("useSSL field is not a boolean", TAG); return false; }

    return true;
}