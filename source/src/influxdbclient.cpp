#include "influxdbclient.h"

extern Ade7953 ade7953;

static const char *TAG = "influxdbclient";

namespace InfluxDbClient
{
    // Static variables
    static InfluxDbConfiguration _influxDbConfiguration;

    // State variables
    static bool _isSetupDone = false;
    static unsigned long long _sendAttempt = 0;
    static unsigned long long _nextSendAttemptMillis = 0;
    
    // Runtime connection status - kept in memory only, not saved to preferences
    static char _status[STATUS_BUFFER_SIZE];
    static unsigned long long _statusTimestampUnix;

    // Task variables
    static TaskHandle_t _influxDbTaskHandle = nullptr;
    static bool _taskShouldRun = false;

    // Private function declarations
    static void _setDefaultConfiguration();
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    
    static void _disable();
    
    static void _sendData();
    static void _formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char *buffer, size_t bufferSize, bool isEnergyData);
    
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument);
    static bool _validateJsonConfigurationPartial(JsonDocument &jsonDocument);

    static void _influxDbTask(void* parameter);
    static void _startTask();
    static void _stopTask();

    void begin()
    {
        logger.debug("Setting up InfluxDB client...", TAG);
        
        _setConfigurationFromPreferences();
        
        if (_influxDbConfiguration.enabled) {
            _startTask();
        } else {
            logger.debug("InfluxDB not enabled", TAG);
        }
        
        _isSetupDone = true;
        logger.debug("InfluxDB client setup complete", TAG);
    }

    void stop()
    {
        logger.debug("Stopping InfluxDB client...", TAG);
        _stopTask();
        _isSetupDone = false;
        logger.debug("InfluxDB client stopped", TAG);
    }

    static void _setDefaultConfiguration()
    {
        logger.debug("Setting default InfluxDB configuration...", TAG);

        InfluxDbConfiguration defaultConfig;
        setConfiguration(defaultConfig);

        logger.debug("Default InfluxDB configuration set", TAG);
    }

    bool setConfiguration(InfluxDbConfiguration &config)
    {
        logger.debug("Setting InfluxDB configuration...", TAG);

        _stopTask();

        _influxDbConfiguration = config;
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        _nextSendAttemptMillis = millis64();
        _sendAttempt = 0;

        _saveConfigurationToPreferences();

        if (_influxDbConfiguration.enabled) {
            _startTask();
        }

        logger.debug("InfluxDB configuration set", TAG);
        return true;
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument)
    {
        logger.debug("Setting InfluxDB configuration from JSON...", TAG);

        if (!_validateJsonConfigurationPartial(jsonDocument))
        {
            logger.error("Invalid InfluxDB configuration JSON", TAG);
            return false;
        }

        InfluxDbConfiguration config = _influxDbConfiguration;
        
        if (!configurationFromJsonPartial(jsonDocument, config))
        {
            logger.error("Failed to convert JSON to configuration struct", TAG);
            return false;
        }

        return setConfiguration(config);
    }

    static void _setConfigurationFromPreferences()
    {
        logger.debug("Setting InfluxDB configuration from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, true)) {
            logger.error("Failed to open Preferences namespace for InfluxDB. Using default configuration", TAG);
            _setDefaultConfiguration();
            return;
        }

        InfluxDbConfiguration config;
        config.enabled = preferences.getBool(INFLUXDB_ENABLED_KEY, INFLUXDB_ENABLED_DEFAULT);
        snprintf(config.server, sizeof(config.server), "%s", preferences.getString(INFLUXDB_SERVER_KEY, INFLUXDB_SERVER_DEFAULT).c_str());
        config.port = preferences.getInt(INFLUXDB_PORT_KEY, INFLUXDB_PORT_DEFAULT);
        config.version = preferences.getInt(INFLUXDB_VERSION_KEY, INFLUXDB_VERSION_DEFAULT);
        snprintf(config.database, sizeof(config.database), "%s", preferences.getString(INFLUXDB_DATABASE_KEY, INFLUXDB_DATABASE_DEFAULT).c_str());
        snprintf(config.username, sizeof(config.username), "%s", preferences.getString(INFLUXDB_USERNAME_KEY, INFLUXDB_USERNAME_DEFAULT).c_str());
        snprintf(config.password, sizeof(config.password), "%s", preferences.getString(INFLUXDB_PASSWORD_KEY, INFLUXDB_PASSWORD_DEFAULT).c_str());
        snprintf(config.organization, sizeof(config.organization), "%s", preferences.getString(INFLUXDB_ORGANIZATION_KEY, INFLUXDB_ORGANIZATION_DEFAULT).c_str());
        snprintf(config.bucket, sizeof(config.bucket), "%s", preferences.getString(INFLUXDB_BUCKET_KEY, INFLUXDB_BUCKET_DEFAULT).c_str());
        snprintf(config.token, sizeof(config.token), "%s", preferences.getString(INFLUXDB_TOKEN_KEY, INFLUXDB_TOKEN_DEFAULT).c_str());
        snprintf(config.measurement, sizeof(config.measurement), "%s", preferences.getString(INFLUXDB_MEASUREMENT_KEY, INFLUXDB_MEASUREMENT_DEFAULT).c_str());
        config.frequencySeconds = preferences.getInt(INFLUXDB_FREQUENCY_KEY, INFLUXDB_FREQUENCY_DEFAULT);
        config.useSSL = preferences.getBool(INFLUXDB_USE_SSL_KEY, INFLUXDB_USE_SSL_DEFAULT);
        
        snprintf(_status, sizeof(_status), "Configuration loaded from Preferences");
        _statusTimestampUnix = CustomTime::getUnixTime();

        preferences.end();

        _influxDbConfiguration = config;

        logger.debug("Successfully set InfluxDB configuration from Preferences", TAG);
    }

    static void _saveConfigurationToPreferences()
    {
        logger.debug("Saving InfluxDB configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false)) {
            logger.error("Failed to open Preferences namespace for InfluxDB", TAG);
            return;
        }

        preferences.putBool(INFLUXDB_ENABLED_KEY, _influxDbConfiguration.enabled);
        preferences.putString(INFLUXDB_SERVER_KEY, _influxDbConfiguration.server);
        preferences.putInt(INFLUXDB_PORT_KEY, _influxDbConfiguration.port);
        preferences.putInt(INFLUXDB_VERSION_KEY, _influxDbConfiguration.version);
        preferences.putString(INFLUXDB_DATABASE_KEY, _influxDbConfiguration.database);
        preferences.putString(INFLUXDB_USERNAME_KEY, _influxDbConfiguration.username);
        preferences.putString(INFLUXDB_PASSWORD_KEY, _influxDbConfiguration.password);
        preferences.putString(INFLUXDB_ORGANIZATION_KEY, _influxDbConfiguration.organization);
        preferences.putString(INFLUXDB_BUCKET_KEY, _influxDbConfiguration.bucket);
        preferences.putString(INFLUXDB_TOKEN_KEY, _influxDbConfiguration.token);
        preferences.putString(INFLUXDB_MEASUREMENT_KEY, _influxDbConfiguration.measurement);
        preferences.putInt(INFLUXDB_FREQUENCY_KEY, _influxDbConfiguration.frequencySeconds);
        preferences.putBool(INFLUXDB_USE_SSL_KEY, _influxDbConfiguration.useSSL);

        preferences.end();

        logger.debug("Successfully saved InfluxDB configuration to Preferences", TAG);
    }

    static bool _validateJsonConfiguration(JsonDocument &jsonDocument)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            logger.warning("Invalid JSON document", TAG);
            return false;
        }

        if (!jsonDocument["enabled"].is<bool>()) { logger.warning("enabled field is not a boolean", TAG); return false; }
        if (!jsonDocument["server"].is<const char*>()) { logger.warning("server field is not a string", TAG); return false; }
        if (!jsonDocument["port"].is<int>()) { logger.warning("port field is not an integer", TAG); return false; }
        if (!jsonDocument["version"].is<int>()) { logger.warning("version field is not an integer", TAG); return false; }
        if (!jsonDocument["database"].is<const char*>()) { logger.warning("database field is not a string", TAG); return false; }
        if (!jsonDocument["username"].is<const char*>()) { logger.warning("username field is not a string", TAG); return false; }
        if (!jsonDocument["password"].is<const char*>()) { logger.warning("password field is not a string", TAG); return false; }
        if (!jsonDocument["organization"].is<const char*>()) { logger.warning("organization field is not a string", TAG); return false; }
        if (!jsonDocument["bucket"].is<const char*>()) { logger.warning("bucket field is not a string", TAG); return false; }
        if (!jsonDocument["token"].is<const char*>()) { logger.warning("token field is not a string", TAG); return false; }
        if (!jsonDocument["measurement"].is<const char*>()) { logger.warning("measurement field is not a string", TAG); return false; }
        if (!jsonDocument["frequency"].is<int>()) { logger.warning("frequency field is not an integer", TAG); return false; }
        if (!jsonDocument["useSSL"].is<bool>()) { logger.warning("useSSL field is not a boolean", TAG); return false; }

        if (jsonDocument["frequency"].as<int>() < 1 || jsonDocument["frequency"].as<int>() > 3600)
        {
            logger.warning("frequency field must be between 1 and 3600 seconds", TAG);
            return false;
        }

        return true;
    }

    static bool _validateJsonConfigurationPartial(JsonDocument &jsonDocument)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            logger.warning("Invalid or empty JSON document", TAG);
            return false;
        }

        if (jsonDocument["enabled"].is<bool>()) {return true;}        
        if (jsonDocument["server"].is<const char*>()) {return true;}        
        if (jsonDocument["port"].is<int>()) {return true;}        
        if (jsonDocument["version"].is<int>()) {return true;}        
        if (jsonDocument["database"].is<const char*>()) {return true;}        
        if (jsonDocument["username"].is<const char*>()) {return true;}        
        if (jsonDocument["password"].is<const char*>()) {return true;}        
        if (jsonDocument["organization"].is<const char*>()) {return true;}        
        if (jsonDocument["bucket"].is<const char*>()) {return true;}        
        if (jsonDocument["token"].is<const char*>()) {return true;}        
        if (jsonDocument["measurement"].is<const char*>()) {return true;}        
        if (jsonDocument["frequency"].is<int>()) {return true;}        
        if (jsonDocument["useSSL"].is<bool>()) {return true;}

        logger.warning("No valid fields found in JSON document", TAG);
        return false;
    }

    static void _disable() {
        logger.debug("Disabling InfluxDB due to persistent failures...", TAG);

        _stopTask();

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false)) {
            logger.error("Failed to open Preferences namespace for InfluxDB", TAG);
            return;
        }

        preferences.putBool(INFLUXDB_ENABLED_KEY, false);
        preferences.end();

        _influxDbConfiguration.enabled = false;
        
        _sendAttempt = 0;
        _nextSendAttemptMillis = 0;

        snprintf(_status, sizeof(_status), "Disabled due to persistent failures");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("InfluxDB disabled", TAG);
    }

    static void _sendData()
    {
        logger.debug("Sending data to InfluxDB...", TAG);

        HTTPClient http;
        char url[SERVER_NAME_BUFFER_SIZE];
        char baseUrl[SERVER_NAME_BUFFER_SIZE];

        snprintf(baseUrl, sizeof(baseUrl), "http%s://%s:%d", 
                _influxDbConfiguration.useSSL ? "s" : "", 
                _influxDbConfiguration.server, 
                _influxDbConfiguration.port);

        if (_influxDbConfiguration.version == 2)
        {
            snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s",
                     baseUrl,
                     _influxDbConfiguration.organization,
                     _influxDbConfiguration.bucket);
        }
        else
        {
            snprintf(url, sizeof(url), "%s/write?db=%s",
                     baseUrl,
                     _influxDbConfiguration.database);
        }

        http.begin(url);
        http.addHeader("Content-Type", "text/plain");

        if (_influxDbConfiguration.version == 2)
        {
            char authHeader[AUTH_HEADER_BUFFER_SIZE];
            snprintf(authHeader, sizeof(authHeader), "Token %s", _influxDbConfiguration.token);
            http.addHeader("Authorization", authHeader);
        }
        else if (_influxDbConfiguration.version == 1)
        {
            char credentials[sizeof(_influxDbConfiguration.username) + sizeof(_influxDbConfiguration.password) + 2];
            snprintf(credentials, sizeof(credentials), "%s:%s", _influxDbConfiguration.username, _influxDbConfiguration.password);

            String encodedCredentials = base64::encode((const unsigned char*)credentials, strlen(credentials));

            char authHeader[AUTH_HEADER_BUFFER_SIZE];
            snprintf(authHeader, sizeof(authHeader), "Basic %s", encodedCredentials.c_str());
            http.addHeader("Authorization", authHeader);
        }

        char payload[PAYLOAD_BUFFER_SIZE] = "";
        size_t payloadLength = 0;
        unsigned long long currentTimestamp = CustomTime::getUnixTimeMilliseconds();

        for (int i = 0; i < CHANNEL_COUNT; i++)
        {
            if (ade7953.channelData[i].active)
            {
                if (ade7953.meterValues[i].lastUnixTimeMilliseconds == 0)
                {
                    logger.debug("Channel %d does not have real measurements yet, skipping", TAG, i);
                    continue;
                }

                if (!validateUnixTime(ade7953.meterValues[i].lastUnixTimeMilliseconds))
                {
                    logger.warning("Invalid unixTime for channel %d: %llu", TAG, i, ade7953.meterValues[i].lastUnixTimeMilliseconds);
                    continue;
                }

                char realtimeLineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
                _formatLineProtocol(ade7953.meterValues[i], i, ade7953.meterValues[i].lastUnixTimeMilliseconds, realtimeLineProtocol, sizeof(realtimeLineProtocol), false);

                if (payloadLength > 0 && payloadLength + 1 < PAYLOAD_BUFFER_SIZE)
                {
                    payload[payloadLength] = '\n';
                    payloadLength++;
                    payload[payloadLength] = '\0';
                }

                if (payloadLength + strlen(realtimeLineProtocol) < PAYLOAD_BUFFER_SIZE)
                {
                    snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", realtimeLineProtocol);
                    payloadLength += strlen(realtimeLineProtocol);
                }

                char energyLineProtocol[LINE_PROTOCOL_BUFFER_SIZE];
                _formatLineProtocol(ade7953.meterValues[i], i, currentTimestamp, energyLineProtocol, sizeof(energyLineProtocol), true);

                if (payloadLength + 1 < PAYLOAD_BUFFER_SIZE)
                {
                    payload[payloadLength] = '\n';
                    payloadLength++;
                    payload[payloadLength] = '\0';
                }

                if (payloadLength + strlen(energyLineProtocol) < PAYLOAD_BUFFER_SIZE)
                {
                    snprintf(payload + payloadLength, PAYLOAD_BUFFER_SIZE - payloadLength, "%s", energyLineProtocol);
                    payloadLength += strlen(energyLineProtocol);
                }
            }
        }

        if (payloadLength == 0)
        {
            logger.debug("No data to send to InfluxDB", TAG);
            http.end();
            return;
        }

        int httpCode = http.POST(payload);

        if (httpCode >= 200 && httpCode < 300)
        {
            logger.debug("Successfully sent data to InfluxDB (HTTP %d)", TAG, httpCode);
            statistics.influxdbUploadCount++;
            snprintf(_status, sizeof(_status), "Data sent successfully");
            _sendAttempt = 0;
        }
        else
        {
            logger.warning("Failed to send data to InfluxDB (HTTP %d)", TAG, httpCode);
            statistics.influxdbUploadCountError++;
            
            _sendAttempt++;
            snprintf(_status, sizeof(_status), "Failed to send data (HTTP %d) - Attempt %lu", httpCode, _sendAttempt);
            
            // Check for specific errors that warrant disabling InfluxDB
            if (httpCode == 401 || httpCode == 403) {
                logger.error("InfluxDB send failed due to authorization error (%d). Disabling InfluxDB.", TAG, httpCode);
                _disable();
                _nextSendAttemptMillis = UINT32_MAX;
                _statusTimestampUnix = CustomTime::getUnixTime();
                http.end();
                return;
            }
            
            // Check if we've exceeded the maximum number of failures
            if (_sendAttempt >= INFLUXDB_MAX_CONSECUTIVE_FAILURES) {
                logger.error("InfluxDB send failed %lu consecutive times. Disabling InfluxDB.", TAG, _sendAttempt);
                _disable();
                _nextSendAttemptMillis = UINT32_MAX;
                _statusTimestampUnix = CustomTime::getUnixTime();
                http.end();
                return;
            }
            
            // Calculate next attempt time using exponential backoff
            unsigned long long backoffDelay = calculateExponentialBackoff(_sendAttempt, INFLUXDB_INITIAL_RETRY_INTERVAL, INFLUXDB_MAX_RETRY_INTERVAL, INFLUXDB_RETRY_MULTIPLIER);
            
            _nextSendAttemptMillis = millis64() + backoffDelay;
            
            logger.info("Next InfluxDB send attempt in %llu ms", TAG, backoffDelay);
        }
        
        _statusTimestampUnix = CustomTime::getUnixTime();
        http.end();
    }

    static void _formatLineProtocol(const MeterValues &meterValues, int channel, unsigned long long timestamp, char *buffer, size_t bufferSize, bool isEnergyData)
    {
        char sanitizedLabel[sizeof(ade7953.channelData[channel].label) + 20];
        const char *originalLabel = ade7953.channelData[channel].label;

        size_t labelLen = strlen(originalLabel);
        size_t writePos = 0;
        for (size_t i = 0; i < labelLen && writePos < sizeof(sanitizedLabel) - 1; i++)
        {
            char c = originalLabel[i];
            if (c == ' ' || c == ',' || c == '=' || c == ':')
            {
                sanitizedLabel[writePos++] = '_';
            }
            else
            {
                sanitizedLabel[writePos++] = c;
            }
        }
        sanitizedLabel[writePos] = '\0';

        if (isEnergyData)
        {
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
                     timestamp);
        }
        else
        {
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
                     timestamp);
        }
    }

    void getConfiguration(InfluxDbConfiguration &config)
    {
        logger.debug("Getting InfluxDB configuration...", TAG);

        if (!_isSetupDone)
        {
            begin();
        }

        config = _influxDbConfiguration;
    }

    void getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize)
    {
        if (statusBuffer && statusSize > 0) {
            snprintf(statusBuffer, statusSize, "%s", _status);
        }
        if (timestampBuffer && timestampSize > 0) {
            CustomTime::timestampIsoFromUnix(_statusTimestampUnix, timestampBuffer, timestampSize);
        }
    }

    bool configurationToJson(InfluxDbConfiguration &config, JsonDocument &jsonDocument)
    {
        logger.debug("Converting InfluxDB configuration to JSON...", TAG);

        jsonDocument["enabled"] = config.enabled;
        jsonDocument["server"] = config.server;
        jsonDocument["port"] = config.port;
        jsonDocument["version"] = config.version;
        jsonDocument["database"] = config.database;
        jsonDocument["username"] = config.username;
        jsonDocument["password"] = config.password;
        jsonDocument["organization"] = config.organization;
        jsonDocument["bucket"] = config.bucket;
        jsonDocument["token"] = config.token;
        jsonDocument["measurement"] = config.measurement;
        jsonDocument["frequency"] = config.frequencySeconds;
        jsonDocument["useSSL"] = config.useSSL;

        logger.debug("Successfully converted configuration to JSON", TAG);
        return true;
    }

    bool configurationFromJson(JsonDocument &jsonDocument, InfluxDbConfiguration &config)
    {
        logger.debug("Converting JSON to InfluxDB configuration...", TAG);

        if (!_validateJsonConfiguration(jsonDocument))
        {
            logger.error("Invalid JSON configuration", TAG);
            return false;
        }

        config.enabled = jsonDocument["enabled"].as<bool>();
        snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
        config.port = jsonDocument["port"].as<int>();
        config.version = jsonDocument["version"].as<int>();
        snprintf(config.database, sizeof(config.database), "%s", jsonDocument["database"].as<const char*>());
        snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
        snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        snprintf(config.organization, sizeof(config.organization), "%s", jsonDocument["organization"].as<const char*>());
        snprintf(config.bucket, sizeof(config.bucket), "%s", jsonDocument["bucket"].as<const char*>());
        snprintf(config.token, sizeof(config.token), "%s", jsonDocument["token"].as<const char*>());
        snprintf(config.measurement, sizeof(config.measurement), "%s", jsonDocument["measurement"].as<const char*>());
        config.frequencySeconds = jsonDocument["frequency"].as<int>();
        config.useSSL = jsonDocument["useSSL"].as<bool>();
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Successfully converted JSON to configuration", TAG);
        return true;
    }

    bool configurationFromJsonPartial(JsonDocument &jsonDocument, InfluxDbConfiguration &config)
    {
        logger.debug("Converting JSON to InfluxDB configuration (partial update)...", TAG);

        if (!_validateJsonConfigurationPartial(jsonDocument))
        {
            logger.error("Invalid JSON configuration", TAG);
            return false;
        }

        if (jsonDocument["enabled"].is<bool>()) {
            config.enabled = jsonDocument["enabled"].as<bool>();
        }
        if (jsonDocument["server"].is<const char*>()) {
            snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
        }
        if (jsonDocument["port"].is<int>()) {
            config.port = jsonDocument["port"].as<int>();
        }
        if (jsonDocument["version"].is<int>()) {
            config.version = jsonDocument["version"].as<int>();
        }
        if (jsonDocument["database"].is<const char*>()) {
            snprintf(config.database, sizeof(config.database), "%s", jsonDocument["database"].as<const char*>());
        }
        if (jsonDocument["username"].is<const char*>()) {
            snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
        }
        if (jsonDocument["password"].is<const char*>()) {
            snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        }
        if (jsonDocument["organization"].is<const char*>()) {
            snprintf(config.organization, sizeof(config.organization), "%s", jsonDocument["organization"].as<const char*>());
        }
        if (jsonDocument["bucket"].is<const char*>()) {
            snprintf(config.bucket, sizeof(config.bucket), "%s", jsonDocument["bucket"].as<const char*>());
        }
        if (jsonDocument["token"].is<const char*>()) {
            snprintf(config.token, sizeof(config.token), "%s", jsonDocument["token"].as<const char*>());
        }
        if (jsonDocument["measurement"].is<const char*>()) {
            snprintf(config.measurement, sizeof(config.measurement), "%s", jsonDocument["measurement"].as<const char*>());
        }
        if (jsonDocument["frequency"].is<int>()) {
            config.frequencySeconds = jsonDocument["frequency"].as<int>();
        }
        if (jsonDocument["useSSL"].is<bool>()) {
            config.useSSL = jsonDocument["useSSL"].as<bool>();
        }
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Successfully converted JSON to configuration", TAG);
        return true;
    }

    static void _influxDbTask(void* parameter)
    {
        logger.debug("InfluxDB task started", TAG);
        
        _taskShouldRun = true;
        unsigned long long lastSendTime = 0;

        while (_taskShouldRun) {
            uint32_t notificationValue = ulTaskNotifyTake(pdFALSE, 0);
            if (notificationValue > 0) {
                _taskShouldRun = false;
                break;
            }

            if (!CustomWifi::isFullyConnected() || !CustomTime::isTimeSynched()) {
                vTaskDelay(pdMS_TO_TICKS(INFLUXDB_TASK_CHECK_INTERVAL));
                continue;
            }

            if (!_influxDbConfiguration.enabled) {
                vTaskDelay(pdMS_TO_TICKS(INFLUXDB_TASK_CHECK_INTERVAL));
                continue;
            }

            unsigned long long currentTime = millis64();
            if ((currentTime - lastSendTime) >= (_influxDbConfiguration.frequencySeconds * 1000)) {
                // Check if we should wait due to previous failures
                if (currentTime >= _nextSendAttemptMillis) {
                    _sendData();
                    lastSendTime = currentTime;
                } else {
                    logger.debug("Delaying InfluxDB send due to previous failures", TAG);
                }
            }

            vTaskDelay(pdMS_TO_TICKS(INFLUXDB_TASK_CHECK_INTERVAL));
        }

        logger.debug("InfluxDB task ending", TAG);
        _influxDbTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static void _startTask()
    {
        if (_influxDbTaskHandle != nullptr) {
            logger.warning("InfluxDB task already running", TAG);
            return;
        }

        BaseType_t result = xTaskCreate(
            _influxDbTask,
            INFLUXDB_TASK_NAME,
            INFLUXDB_TASK_STACK_SIZE,
            nullptr,
            INFLUXDB_TASK_PRIORITY,
            &_influxDbTaskHandle
        );

        if (result != pdPASS) {
            logger.error("Failed to create InfluxDB task", TAG);
            _influxDbTaskHandle = nullptr;
        } else {
            logger.debug("InfluxDB task started successfully", TAG);
        }
    }

    static void _stopTask()
    {
        if (_influxDbTaskHandle == nullptr) {
            return;
        }

        logger.debug("Stopping InfluxDB task...", TAG);
        
        xTaskNotifyGive(_influxDbTaskHandle);
        
        int timeout = TASK_STOPPING_TIMEOUT;
        while (_influxDbTaskHandle != nullptr && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(TASK_STOPPING_CHECK_INTERVAL));
            timeout -= TASK_STOPPING_CHECK_INTERVAL;
        }
        
        if (_influxDbTaskHandle != nullptr) {
            logger.warning("Force deleting InfluxDB task", TAG);
            vTaskDelete(_influxDbTaskHandle);
            _influxDbTaskHandle = nullptr;
        }

        logger.debug("InfluxDB task stopped", TAG);
    }

} // namespace InfluxDbClient