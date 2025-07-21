#include "influxdbclient.h"
#include <base64.h>

static const char *TAG = "influxdbclient";

InfluxDbClient::InfluxDbClient(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    InfluxDbConfiguration &influxDbConfiguration
    ) : _ade7953(ade7953),
                              _logger(logger),
                              _influxDbConfiguration(influxDbConfiguration) {}

void InfluxDbClient::begin()
{
    _logger.debug("Setting up InfluxDB client...", TAG);

    _setConfigurationFromSpiffs();

    // _baseUrl = "http) + (_influxDbConfiguration.useSSL ? "s" : "") + "://" + _influxDbConfiguration.server + ":" + _influxDbConfigurationport);
    snprintf(_baseUrl, sizeof(_baseUrl), "http%s://%s:%d", 
            _influxDbConfiguration.useSSL ? "s" : "", 
            _influxDbConfiguration.server, 
            _influxDbConfiguration.port);
    _logger.debug("Base URL set to: %s", TAG, _baseUrl);
    
    _isConnected = false;
    _bufferMeterValues.clear();
    _isSetupDone = true;
}

void InfluxDbClient::loop()
{
    if ((millis() - _lastMillisInfluxDbLoop) < INFLUXDB_LOOP_INTERVAL) return;
    _lastMillisInfluxDbLoop = millis();

    if (!CustomWifi::isFullyConnected()) return;
    if (!_isSetupDone) begin();
    if (!_influxDbConfiguration.enabled) return;

    if (!_isConnected)
    {
        // Use exponential backoff timing
        if (millis() >= _nextInfluxDbConnectionAttemptMillis) {
            _connectInfluxDb(); // _connectInfluxDb handles setting the next attempt time
        }
    } else {
        // If connected, reset connection attempts counter for backoff calculation
        if (_influxDbConnectionAttempt > 0) {
            _logger.debug("InfluxDB reconnected successfully after %d attempts.", TAG, _influxDbConnectionAttempt);
            _influxDbConnectionAttempt = 0;
        }
    }

    // Only buffer and upload if connected
    if (_isConnected) {
        // Buffer meter data at the specified frequency
        if ((millis() - _lastMillisMeterBuffer) > _influxDbConfiguration.frequency * 1000)
        {
            _lastMillisMeterBuffer = millis();
            _bufferMeterData();
        }

        // Upload data if buffer is full or enough time has passed since last upload
        bool bufferEmpty = _bufferMeterValues.isEmpty();
        bool bufferFull = _bufferMeterValues.size() >= (INFLUXDB_BUFFER_SIZE * 0.9);
        bool timeToUpload = (millis() - _lastMillisMeterPublish) > (_influxDbConfiguration.frequency * 1000 * INFLUXDB_FREQUENCY_UPLOAD_MULTIPLIER) ||
                            (millis() - _lastMillisMeterPublish) > INFLUXDB_MAX_INTERVAL_METER_PUBLISH;

        if (!bufferEmpty && (bufferFull || timeToUpload)) _uploadBufferedData();
    }
}

bool InfluxDbClient::_connectInfluxDb()
{
    _logger.debug("Attempt to connect to InfluxDB (attempt %d)...", TAG, _influxDbConnectionAttempt + 1);

    // Test connection to InfluxDB
    if (!_testConnection())
    {
        _logger.warning("Failed to ping InfluxDB (attempt %d). Retrying...", TAG, _influxDbConnectionAttempt + 1);
        _lastMillisInfluxDbFailed = millis();
        _influxDbConnectionAttempt++;

        snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), "Failed to ping InfluxDB (Attempt %d)", _influxDbConnectionAttempt);

        char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer);
        snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);
        
        _saveConfigurationToSpiffs();

        _isConnected = false;

        // Check if we've exceeded max attempts
        if (_influxDbConnectionAttempt >= INFLUXDB_MAX_CONNECTION_ATTEMPTS) {
            _logger.error("InfluxDB connection failed after %d attempts. Disabling InfluxDB.", TAG, _influxDbConnectionAttempt);
            _disable();
            _nextInfluxDbConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts until re-enabled
            return false;
        }

        // Calculate next attempt time using exponential backoff
        unsigned long _backoffDelay = INFLUXDB_INITIAL_RECONNECT_INTERVAL;
        for (int i = 0; i < _influxDbConnectionAttempt - 1 && _backoffDelay < INFLUXDB_MAX_RECONNECT_INTERVAL; ++i) {
            _backoffDelay *= INFLUXDB_RECONNECT_MULTIPLIER;
        }
        _backoffDelay = min(_backoffDelay, (unsigned long)INFLUXDB_MAX_RECONNECT_INTERVAL);

        _nextInfluxDbConnectionAttemptMillis = millis() + _backoffDelay;
        _logger.info("Next InfluxDB connection attempt in %lu ms", TAG, _backoffDelay);

        return false;
    }

    _logger.debug("Successfully pinged InfluxDB", TAG);

    if (!_testCredentials())
    {
        _logger.warning("Failed to validate InfluxDB credentials (attempt %d). Retrying...", TAG, _influxDbConnectionAttempt + 1);
        _lastMillisInfluxDbFailed = millis();
        _influxDbConnectionAttempt++;

        snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), "Failed to validate credentials (Attempt %d)", _influxDbConnectionAttempt);
        
        char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer);
        snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);
        _saveConfigurationToSpiffs();

        _isConnected = false;

        // Check if we've exceeded max attempts
        if (_influxDbConnectionAttempt >= INFLUXDB_MAX_CONNECTION_ATTEMPTS) {
            _logger.error("InfluxDB credential validation failed after %d attempts. Disabling InfluxDB.", TAG, _influxDbConnectionAttempt);
            _disable();
            _nextInfluxDbConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts until re-enabled
            return false;
        }

        // Calculate next attempt time using exponential backoff
        unsigned long _backoffDelay = INFLUXDB_INITIAL_RECONNECT_INTERVAL;
        for (int i = 0; i < _influxDbConnectionAttempt - 1 && _backoffDelay < INFLUXDB_MAX_RECONNECT_INTERVAL; ++i) {
            _backoffDelay *= INFLUXDB_RECONNECT_MULTIPLIER;
        }
        _backoffDelay = min(_backoffDelay, (unsigned long)INFLUXDB_MAX_RECONNECT_INTERVAL);

        _nextInfluxDbConnectionAttemptMillis = millis() + _backoffDelay;
        _logger.info("Next InfluxDB connection attempt in %lu ms", TAG, _backoffDelay);

        return false;
    }

    _logger.info("Successfully connected to InfluxDB", TAG);
    _influxDbConnectionAttempt = 0; // Reset attempt counter on success
    snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), "Connected to InfluxDB");

    char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(timestampBuffer);
    snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

    _saveConfigurationToSpiffs();
    _isConnected = true;

    return true;
}

void InfluxDbClient::_disable() 
{
    _logger.debug("Disabling InfluxDB...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    _jsonDocument["enabled"] = false;

    setConfiguration(_jsonDocument);
    
    _isSetupDone = false;
    _isConnected = false;
    _influxDbConnectionAttempt = 0;
    _bufferMeterValues.clear();

    _logger.debug("InfluxDB disabled", TAG);
}

void InfluxDbClient::_bufferMeterData()
{
    if (!CustomTime::isTimeSynched() || millis() < MINIMUM_TIME_BEFORE_VALID_METER)
    {
        _logger.debug("Time not synced or not enough time passed, skipping data buffering", TAG);
        return;
    }

    // Buffer only real-time data (power, voltage, current) for active channels
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (_ade7953.channelData[i].active)
        {
            if (_ade7953.meterValues[i].lastUnixTimeMilliseconds == 0) // Default value when initialized
            {
                _logger.debug("Channel does not have real measurements yet, skipping channel %d", TAG, i);
                continue;
            }

            if (!validateUnixTime(_ade7953.meterValues[i].lastUnixTimeMilliseconds))
            {
                _logger.warning("Invalid unixTime for channel %d in payload meter: %llu", TAG, i, _ade7953.meterValues[i].lastUnixTimeMilliseconds);
                continue;
            }

            // Create a simplified point with only real-time measurements
            MeterValues realtimeValues;
            realtimeValues.voltage = _ade7953.meterValues[i].voltage;
            realtimeValues.current = _ade7953.meterValues[i].current;
            realtimeValues.activePower = _ade7953.meterValues[i].activePower;
            realtimeValues.reactivePower = _ade7953.meterValues[i].reactivePower;
            realtimeValues.apparentPower = _ade7953.meterValues[i].apparentPower;
            realtimeValues.powerFactor = _ade7953.meterValues[i].powerFactor;
            
            BufferedPoint point(realtimeValues, i, _ade7953.meterValues[i].lastUnixTimeMilliseconds);
            _bufferMeterValues.push(point);

            if (_bufferMeterValues.isFull())
            {
                _logger.debug("Buffer full, will upload on next loop", TAG);
                break;
            }
        }
    }

    _logger.debug("Meter data buffered. Buffer size: %d/%d", TAG, _bufferMeterValues.size(), INFLUXDB_BUFFER_SIZE);
}

void InfluxDbClient::_uploadBufferedData()
{
    if (_bufferMeterValues.isEmpty())
        return;

    HTTPClient http;
    char url[URL_BUFFER_SIZE];

    if (_influxDbConfiguration.version == 2)
    {
        snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s", 
                _baseUrl, 
                _influxDbConfiguration.organization, 
                _influxDbConfiguration.bucket);
    }
    else
    {
        snprintf(url, sizeof(url), "%s/write?db=%s", 
                _baseUrl, 
                _influxDbConfiguration.database);
    }

    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    // Set authorization header based on InfluxDB version
    if (_influxDbConfiguration.version == 2)
    {
        char authHeader[AUTH_HEADER_BUFFER_SIZE];
        snprintf(authHeader, sizeof(authHeader), "Token %s", _influxDbConfiguration.token);
        http.addHeader("Authorization", authHeader);
    }
    else if (_influxDbConfiguration.version == 1)
    {
        char credentials[USERNAME_BUFFER_SIZE + PASSWORD_BUFFER_SIZE + 2]; // +2 for ':' and null terminator
        snprintf(credentials, sizeof(credentials), "%s:%s", _influxDbConfiguration.username, _influxDbConfiguration.password);
        
        String encodedCredentialsStr = base64::encode(credentials);
        char encodedCredentials[ENCODED_CREDENTIALS_BUFFER_SIZE];
        snprintf(encodedCredentials, sizeof(encodedCredentials), "%s", encodedCredentialsStr.c_str());
        
        char authHeader[AUTH_HEADER_BUFFER_SIZE];
        snprintf(authHeader, sizeof(authHeader), "Basic %s", encodedCredentials);
        http.addHeader("Authorization", authHeader);
    }

    // Build line protocol payload with both buffered real-time data and current energy data
    char payload[PAYLOAD_BUFFER_SIZE] = "";
    size_t payloadLength = 0;
    unsigned int pointCount = 0;

    // First, add all buffered real-time data points
    unsigned int _loops = 0;
    while (!_bufferMeterValues.isEmpty() && pointCount < _bufferMeterValues.size() && _loops < MAX_LOOP_ITERATIONS)
    {
        _loops++;
        BufferedPoint point = _bufferMeterValues.shift();
        char lineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
        _formatRealtimeLineProtocol(point.meterValues, point.channel, point.unixTimeMilliseconds, lineProtocol, sizeof(lineProtocol));

        if (payloadLength + strlen(lineProtocol) > 25000)
        { // Keep under safe HTTP limit (reduced to leave room for energy data)
            _logger.warning("Payload too large, pushing point back to buffer", TAG);
            _bufferMeterValues.unshift(point);
            break;
        }

        if (payloadLength > 0)
        {
            if (payloadLength + 1 < PAYLOAD_BUFFER_SIZE) 
            {
                payload[payloadLength] = '\n';
                payloadLength++;
                payload[payloadLength] = '\0';
            }
        }
        
        if (payloadLength + strlen(lineProtocol) < PAYLOAD_BUFFER_SIZE)
        {
            snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", lineProtocol);
            payloadLength += strlen(lineProtocol);
        }
        else
        {
            _logger.warning("Payload buffer overflow prevented", TAG);
            _bufferMeterValues.unshift(point);
            break;
        }
        pointCount++;
    }

    // Then, add current energy data for all active channels
    unsigned long long currentTimestamp = CustomTime::getUnixTimeMilliseconds();
    for (int i = 0; i < CHANNEL_COUNT; i++)
    {
        if (_ade7953.channelData[i].active)
        {
            char energyLineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
            _formatEnergyLineProtocol(_ade7953.meterValues[i], i, currentTimestamp, energyLineProtocol, sizeof(energyLineProtocol));

            if (payloadLength + strlen(energyLineProtocol) > 30000)
            { // Check if adding energy data would exceed limit
                _logger.warning("Payload too large to include energy data for channel %d", TAG, i);
                break;
            }

            if (payloadLength > 0)
            {
                if (payloadLength + 1 < PAYLOAD_BUFFER_SIZE) 
                {
                    payload[payloadLength] = '\n';
                    payloadLength++;
                    payload[payloadLength] = '\0';
                }
            }
            
            if (payloadLength + strlen(energyLineProtocol) < PAYLOAD_BUFFER_SIZE)
            {
                snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", energyLineProtocol);
                payloadLength += strlen(energyLineProtocol);
            }
        }
    }

    int httpCode = http.POST(payload);

    if (httpCode >= 200 && httpCode < 300)
    {
        _logger.debug("Successfully uploaded %d real-time points + energy data to InfluxDB", TAG, pointCount);
        snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), "Upload successful");
        char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer);
        snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

        // Reset connection state on successful upload
        _isConnected = true;
        _influxDbConnectionAttempt = 0;

        _lastMillisMeterPublish = millis();
        statistics.influxdbUploadCount++;
    }
    else
    {
        char response[HTTP_RESPONSE_BUFFER_SIZE];
        String httpResponseStr = http.getString();
        snprintf(response, sizeof(response), "%.500s", httpResponseStr.c_str());
        _logger.error("Failed to upload to InfluxDB. HTTP code: %d, Response: %.100s", TAG, httpCode, response);
        _lastMillisInfluxDbFailed = millis();
        _influxDbConnectionAttempt++;

        snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), 
                 "Upload failed (HTTP %d) (Attempt %d)", httpCode, _influxDbConnectionAttempt);
        
                 char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer);
        snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);
        
        _saveConfigurationToSpiffs();

        // Mark as disconnected to trigger reconnection attempts
        _isConnected = false;
        statistics.influxdbUploadCountError++;

        // Check if we've exceeded max attempts for persistent errors like 404 (wrong bucket/org)
        if (_influxDbConnectionAttempt >= INFLUXDB_MAX_CONNECTION_ATTEMPTS)
        {
            _logger.error("InfluxDB upload failed after %d attempts. Disabling InfluxDB.", TAG, _influxDbConnectionAttempt);
            _disable();
            _nextInfluxDbConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts until re-enabled
        }
        else
        {
            // Calculate next attempt time using exponential backoff (same as connection failures)
            unsigned long _backoffDelay = INFLUXDB_INITIAL_RECONNECT_INTERVAL;
            for (int i = 0; i < _influxDbConnectionAttempt - 1 && _backoffDelay < INFLUXDB_MAX_RECONNECT_INTERVAL; ++i)
            {
                _backoffDelay *= INFLUXDB_RECONNECT_MULTIPLIER;
            }
            _backoffDelay = min(_backoffDelay, (unsigned long)INFLUXDB_MAX_RECONNECT_INTERVAL);

            _nextInfluxDbConnectionAttemptMillis = millis() + _backoffDelay;
            _logger.info("Next InfluxDB connection attempt in %lu ms", TAG, _backoffDelay);
        }

        // Put points back in buffer for retry (if there's space)
        unsigned int _loops = 0;
        while (pointCount-- > 0 && !_bufferMeterValues.isFull() && _loops < MAX_LOOP_ITERATIONS)
        {
            _loops++;
            BufferedPoint point = _bufferMeterValues.shift();
            _bufferMeterValues.push(point);
        }
    }

    http.end();
}

void InfluxDbClient::_formatRealtimeLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char* buffer, size_t bufferSize)
{
    // Create sanitized label directly in a char array
    char sanitizedLabel[LABEL_BUFFER_SIZE];  // Use defined constant
    const char* originalLabel = _ade7953.channelData[channel].label;
    
    // Sanitize the label in-place without heap allocation
    size_t labelLen = strlen(originalLabel);
    size_t writePos = 0;
    for (size_t i = 0; i < labelLen && writePos < sizeof(sanitizedLabel) - 1; i++) {
        char c = originalLabel[i];
        if (c == ' ' || c == ',' || c == '=' || c == ':') {
            sanitizedLabel[writePos++] = '_';
        } else {
            sanitizedLabel[writePos++] = c;
        }
    }
    sanitizedLabel[writePos] = '\0';

    snprintf(buffer, bufferSize,
        "%s,channel=%d,label=%s,device_id=%s voltage=%.2f,current=%.3f,active_power=%.2f,reactive_power=%.2f,apparent_power=%.2f,power_factor=%.3f %llu000000",
        _influxDbConfiguration.measurement,
        channel,
        sanitizedLabel,
        DEVICE_ID,
        meterValues.voltage,
        meterValues.current,
        meterValues.activePower,
        meterValues.reactivePower,
        meterValues.apparentPower,
        meterValues.powerFactor,
        timestamp
    );
}

void InfluxDbClient::_formatEnergyLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char* buffer, size_t bufferSize)
{
    // Create sanitized label directly in a char array
    char sanitizedLabel[LABEL_BUFFER_SIZE];  // Use defined constant
    const char* originalLabel = _ade7953.channelData[channel].label;
    
    // Sanitize the label in-place without heap allocation
    size_t labelLen = strlen(originalLabel);
    size_t writePos = 0;
    for (size_t i = 0; i < labelLen && writePos < sizeof(sanitizedLabel) - 1; i++) {
        char c = originalLabel[i];
        if (c == ' ' || c == ',' || c == '=' || c == ':') {
            sanitizedLabel[writePos++] = '_';
        } else {
            sanitizedLabel[writePos++] = c;
        }
    }
    sanitizedLabel[writePos] = '\0';

    snprintf(buffer, bufferSize,
        "%s,channel=%d,label=%s,device_id=%s active_energy_imported=%.3f,active_energy_exported=%.3f,reactive_energy_imported=%.3f,reactive_energy_exported=%.3f,apparent_energy=%.3f %llu000000",
        _influxDbConfiguration.measurement,
        channel,
        sanitizedLabel,
        DEVICE_ID,
        meterValues.activeEnergyImported,
        meterValues.activeEnergyExported,
        meterValues.reactiveEnergyImported,
        meterValues.reactiveEnergyExported,
        meterValues.apparentEnergy,
        timestamp
    );
}

bool InfluxDbClient::_testConnection()
{
    _logger.debug("Testing InfluxDB connection...", TAG);

    if (!_influxDbConfiguration.enabled) {
        _logger.warning("InfluxDB is not enabled. Skipping connection test", TAG);
        return false;
    }

    HTTPClient http;
    char url[URL_BUFFER_SIZE];
    snprintf(url, sizeof(url), "%s/ping", _baseUrl);

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
        String responseStr = http.getString();
        _logger.debug("InfluxDB connection response: %s", TAG, responseStr.c_str());
        http.end();
        return httpCode >= 200 && httpCode < 300;
    }
    else
    {
        String errorStr = http.errorToString(httpCode);
        _logger.error("InfluxDB connection failed: %s", TAG, errorStr.c_str());
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
    char url[URL_BUFFER_SIZE];
    snprintf(url, sizeof(url), "%s/api/v2", _baseUrl);

    http.begin(url);

    // Set authorization header based on InfluxDB version
    if (_influxDbConfiguration.version == 2)
    {
        // InfluxDB v2 uses Token authentication
        char authHeader[AUTH_HEADER_BUFFER_SIZE];
        snprintf(authHeader, sizeof(authHeader), "Token %s", _influxDbConfiguration.token);
        http.addHeader("Authorization", authHeader);
    }
    else if (_influxDbConfiguration.version == 1)
    {
        // InfluxDB v1.x compatibility API uses Basic authentication
        char credentials[USERNAME_BUFFER_SIZE + PASSWORD_BUFFER_SIZE + 2]; // +2 for ':' and null terminator
        snprintf(credentials, sizeof(credentials), "%s:%s", _influxDbConfiguration.username, _influxDbConfiguration.password);
        String encodedCredentialsStr = base64::encode(credentials);
        char encodedCredentials[ENCODED_CREDENTIALS_BUFFER_SIZE];
        snprintf(encodedCredentials, sizeof(encodedCredentials), "%s", encodedCredentialsStr.c_str());
        
        char authHeader[AUTH_HEADER_BUFFER_SIZE];
        snprintf(authHeader, sizeof(authHeader), "Basic %s", encodedCredentials);
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
        String responseStr = http.getString();
        _logger.debug("InfluxDB credentials test response: %s", TAG, responseStr.c_str());
        http.end();
        return httpCode >= 200 && httpCode < 300;
    }
    else
    {
        String errorStr = http.errorToString(httpCode);
        _logger.error("InfluxDB credentials test failed: %s", TAG, errorStr.c_str());
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
    snprintf(_influxDbConfiguration.server, sizeof(_influxDbConfiguration.server), "%s", jsonDocument["server"].as<const char*>());
    _influxDbConfiguration.port = jsonDocument["port"].as<int>();
    _influxDbConfiguration.version = jsonDocument["version"].as<int>();
    snprintf(_influxDbConfiguration.database, sizeof(_influxDbConfiguration.database), "%s", jsonDocument["database"].as<const char*>());
    snprintf(_influxDbConfiguration.username, sizeof(_influxDbConfiguration.username), "%s", jsonDocument["username"].as<const char*>());
    snprintf(_influxDbConfiguration.password, sizeof(_influxDbConfiguration.password), "%s", jsonDocument["password"].as<const char*>());
    snprintf(_influxDbConfiguration.organization, sizeof(_influxDbConfiguration.organization), "%s", jsonDocument["organization"].as<const char*>());
    snprintf(_influxDbConfiguration.bucket, sizeof(_influxDbConfiguration.bucket), "%s", jsonDocument["bucket"].as<const char*>());
    snprintf(_influxDbConfiguration.token, sizeof(_influxDbConfiguration.token), "%s", jsonDocument["token"].as<const char*>());
    snprintf(_influxDbConfiguration.measurement, sizeof(_influxDbConfiguration.measurement), "%s", jsonDocument["measurement"].as<const char*>());
    _influxDbConfiguration.frequency = jsonDocument["frequency"].as<int>();
    _influxDbConfiguration.useSSL = jsonDocument["useSSL"].as<bool>();

    _saveConfigurationToSpiffs();

    // Reset connection state and attempt counters
    _influxDbConnectionAttempt = 0;
    _nextInfluxDbConnectionAttemptMillis = millis(); // Try connecting immediately
    _isConnected = false;
    snprintf(_influxDbConfiguration.lastConnectionStatus, sizeof(_influxDbConfiguration.lastConnectionStatus), "Disconnected");
    char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(timestampBuffer);
    snprintf(_influxDbConfiguration.lastConnectionAttemptTimestamp, sizeof(_influxDbConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

    _logger.debug("InfluxDB configuration set", TAG);

    // Reset state
    _isSetupDone = false;
    _bufferMeterValues.clear();

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
    {_logger.warning("Invalid JSON document", TAG); return false; }

    if (!jsonDocument["enabled"].is<bool>()) {_logger.warning("enabled field is not a boolean", TAG); return false; }
    if (!jsonDocument["server"].is<const char*>()) {_logger.warning("server field is not a string", TAG); return false; }
    if (!jsonDocument["port"].is<int>()) {_logger.warning("port field is not an integer", TAG); return false; }
    if (!jsonDocument["version"].is<int>()) {_logger.warning("version field is not an integer", TAG); return false; }
    if (!jsonDocument["database"].is<const char*>()) {_logger.warning("database field is not a string", TAG); return false; }
    if (!jsonDocument["username"].is<const char*>()) {_logger.warning("username field is not a string", TAG); return false; }
    if (!jsonDocument["password"].is<const char*>()) {_logger.warning("password field is not a string", TAG); return false; }
    if (!jsonDocument["organization"].is<const char*>()) {_logger.warning("organization field is not a string", TAG); return false; }
    if (!jsonDocument["bucket"].is<const char*>()) {_logger.warning("bucket field is not a string", TAG); return false; }
    if (!jsonDocument["token"].is<const char*>()) {_logger.warning("token field is not a string", TAG); return false; }
    if (!jsonDocument["measurement"].is<const char*>()) {_logger.warning("measurement field is not a string", TAG); return false; }
    if (!jsonDocument["frequency"].is<int>()) {_logger.warning("frequency field is not an integer", TAG); return false; }
    // Also ensure frequency is between 1 and 3600
    if (jsonDocument["frequency"].as<int>() < 1 || jsonDocument["frequency"].as<int>() > 3600) {
        _logger.warning("frequency field must be between 1 and 3600 seconds", TAG); 
        return false; 
    }
    if (!jsonDocument["useSSL"].is<bool>()) {_logger.warning("useSSL field is not a boolean", TAG); return false; }

    return true;
}