#include "influxdbclient.h"

namespace InfluxDbClient
{
    // Static variables
    // ==========================================================
    
    // State variables
    static bool _isSetupDone = false;
    static uint8_t _currentSendAttempt = 0;
    static uint64_t _nextSendAttemptMillis = 0;
    static InfluxDbConfiguration _influxDbConfiguration;

    // InfluxDB helper variables
    static char _fullUrl[FULL_URL_BUFFER_SIZE];
    static char _authHeader[AUTH_HEADER_BUFFER_SIZE];
    
    // Runtime connection status - kept in memory only, not saved to preferences
    static char _status[STATUS_BUFFER_SIZE];
    static uint64_t _statusTimestampUnix;

    // Task variables
    static TaskHandle_t _influxDbTaskHandle = nullptr;
    static bool _taskShouldRun = false;

    // Thread safety
    static SemaphoreHandle_t _configMutex = nullptr;

    // Private function declarations
    // =========================================================

    // Configuration management
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    
    // InfluxDB helper functions
    static void _setInfluxFullUrl();
    static void _setInfluxHeader();

    // Data sending
    static void _sendData(const char* url, const char* authHeader, const char* measurement);
    static void _formatLineProtocol(
        const char *measurement,
        const char *deviceId,
        uint8_t channel, 
        const char *label,
        const MeterValues &meterValues, 
        char *lineProtocolBuffer, 
        size_t lineProtocolBufferSize, 
        bool isEnergyData
    );
    
    // JSON validation
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);
    
    // Task management
    static void _influxDbTask(void* parameter);
    static void _startTask();
    static void _stopTask();
    static void _disable(); // Needed for halting the functionality if we have too many failures

    // Public API functions
    // =========================================================

    void begin()
    {
        LOG_DEBUG("Setting up InfluxDB client...");
        
        if (!createMutexIfNeeded(&_configMutex)) return;
        
        _isSetupDone = true; // Must set before since we have checks on the setup later
        _setConfigurationFromPreferences();
        
        _startTask();

        LOG_DEBUG("InfluxDB client setup complete");
    }

    void stop()
    {
        LOG_DEBUG("Stopping InfluxDB client...");
        _stopTask();
        
        deleteMutex(&_configMutex);
        
        _isSetupDone = false;
        LOG_INFO("InfluxDB client stopped");
    }

    void getConfiguration(InfluxDbConfiguration &config)
    {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return;
        }
        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for getConfiguration");
            return;
        }
        config = _influxDbConfiguration; // Full copy
        releaseMutex(&_configMutex);
    }

    bool setConfiguration(const InfluxDbConfiguration &config)
    {
        LOG_DEBUG("Setting InfluxDB configuration...");
        
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return false;
        }

        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for setConfiguration");
            return false;
        }
        _influxDbConfiguration = config; // Copy to the static configuration variable
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        _nextSendAttemptMillis = 0; // Immediately attempt to send data
        _currentSendAttempt = 0;

        _saveConfigurationToPreferences();

        _setInfluxFullUrl();
        _setInfluxHeader();

        releaseMutex(&_configMutex);

        LOG_DEBUG("InfluxDB configuration set");
        return true;
    }

    void resetConfiguration() {
        LOG_DEBUG("Resetting InfluxDB configuration to default");
        
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return;
        }

        InfluxDbConfiguration defaultConfig;
        setConfiguration(defaultConfig);

        LOG_INFO("InfluxDB configuration reset to default");
    }

    void getConfigurationAsJson(JsonDocument &jsonDocument) {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return;
        }
        InfluxDbConfiguration configuration;
        getConfiguration(configuration); // Not using directly the local static variable to ensure we use the mutex protection
        configurationToJson(configuration, jsonDocument);
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return false;
        }

        InfluxDbConfiguration config;
        getConfiguration(config);
        if (!configurationFromJson(jsonDocument, config, partial)) {
            LOG_ERROR("Failed to set configuration from JSON");
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(InfluxDbConfiguration &config, JsonDocument &jsonDocument)
    {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return;
        }

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
        jsonDocument["useSsl"] = config.useSsl;

        LOG_DEBUG("Successfully converted configuration to JSON");
    }

    bool configurationFromJson(JsonDocument &jsonDocument, InfluxDbConfiguration &config, bool partial)
    {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return false;
        }

        if (!_validateJsonConfiguration(jsonDocument, partial))
        {
            LOG_WARNING("Invalid JSON configuration");
            return false;
        }

        if (partial) {
            // Update only fields that are present in JSON
            if (jsonDocument["enabled"].is<bool>())             config.enabled = jsonDocument["enabled"].as<bool>();
            if (jsonDocument["server"].is<const char*>())       snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
            if (jsonDocument["port"].is<uint16_t>())            config.port = jsonDocument["port"].as<uint16_t>();
            if (jsonDocument["version"].is<uint8_t>())          config.version = jsonDocument["version"].as<uint8_t>();
            if (jsonDocument["database"].is<const char*>())     snprintf(config.database, sizeof(config.database), "%s", jsonDocument["database"].as<const char*>());
            if (jsonDocument["username"].is<const char*>())     snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
            if (jsonDocument["password"].is<const char*>())     snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
            if (jsonDocument["organization"].is<const char*>()) snprintf(config.organization, sizeof(config.organization), "%s", jsonDocument["organization"].as<const char*>());
            if (jsonDocument["bucket"].is<const char*>())       snprintf(config.bucket, sizeof(config.bucket), "%s", jsonDocument["bucket"].as<const char*>());
            if (jsonDocument["token"].is<const char*>())        snprintf(config.token, sizeof(config.token), "%s", jsonDocument["token"].as<const char*>());
            if (jsonDocument["measurement"].is<const char*>())  snprintf(config.measurement, sizeof(config.measurement), "%s", jsonDocument["measurement"].as<const char*>());
            if (jsonDocument["frequency"].is<int32_t>())        config.frequencySeconds = jsonDocument["frequency"].as<int32_t>();
            if (jsonDocument["useSsl"].is<bool>())              config.useSsl = jsonDocument["useSsl"].as<bool>();
        } else {
            // Full update - set all fields
            config.enabled = jsonDocument["enabled"].as<bool>();
            snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
            config.port = jsonDocument["port"].as<uint16_t>();
            config.version = jsonDocument["version"].as<uint8_t>();
            snprintf(config.database, sizeof(config.database), "%s", jsonDocument["database"].as<const char*>());
            snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
            snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
            snprintf(config.organization, sizeof(config.organization), "%s", jsonDocument["organization"].as<const char*>());
            snprintf(config.bucket, sizeof(config.bucket), "%s", jsonDocument["bucket"].as<const char*>());
            snprintf(config.token, sizeof(config.token), "%s", jsonDocument["token"].as<const char*>());
            snprintf(config.measurement, sizeof(config.measurement), "%s", jsonDocument["measurement"].as<const char*>());
            config.frequencySeconds = jsonDocument["frequency"].as<int32_t>();
            config.useSsl = jsonDocument["useSsl"].as<bool>();
        }
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        LOG_DEBUG("Successfully converted JSON to configuration%s", partial ? " (partial)" : "");
        return true;
    }

    void getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize)
    {
        if (!_isSetupDone) {
            LOG_WARNING("InfluxDB client is not set up yet, returning early");
            return;
        }

        if (statusBuffer && statusSize > 0) snprintf(statusBuffer, statusSize, "%s", _status);
        if (timestampBuffer && timestampSize > 0) CustomTime::timestampIsoFromUnix(_statusTimestampUnix, timestampBuffer, timestampSize);
    }

    // Private function implementations
    // =========================================================

    static void _startTask()
    {
        if (_influxDbTaskHandle != nullptr) {
            LOG_DEBUG("InfluxDB task is already running");
            return;
        }

        LOG_DEBUG("Starting InfluxDB task");
        BaseType_t result = xTaskCreate(
            _influxDbTask,
            INFLUXDB_TASK_NAME,
            INFLUXDB_TASK_STACK_SIZE,
            nullptr,
            INFLUXDB_TASK_PRIORITY,
            &_influxDbTaskHandle
        );

        if (result != pdPASS) {
            LOG_ERROR("Failed to create InfluxDB task");
            _influxDbTaskHandle = nullptr;
        }
    }

    static void _stopTask() { stopTaskGracefully(&_influxDbTaskHandle, "InfluxDB task"); }

    static void _influxDbTask(void* parameter)
    {
        LOG_DEBUG("InfluxDB task started");
        
        _taskShouldRun = true;
        uint64_t lastSendTime = 0;

        while (_taskShouldRun) {
            if (CustomWifi::isFullyConnected() && CustomTime::isTimeSynched()) { // We are connected and time is synched (needed as InfluxDB requires timestamps)
                if (_influxDbConfiguration.enabled) { // We have the InfluxDB enabled
                    uint64_t currentTime = millis64();
                    if ((currentTime - lastSendTime) >= (_influxDbConfiguration.frequencySeconds * 1000)) { // Enough time has passed since last send
                        if (currentTime >= _nextSendAttemptMillis) { // Enough time has passed since last attempt (in case of failures)
                            
                            // TODO: to really do things well, we should implement a queue method.. But maybe later

                            // Copy variables to ensure non-concurrency with configuration (and thus no need to acquire mutex in send data)
                            char url[sizeof(_fullUrl)];
                            char authHeader[sizeof(_authHeader)];
                            char measurement[sizeof(_influxDbConfiguration.measurement)];

                            if (!acquireMutex(&_configMutex)) {
                                LOG_ERROR("Failed to acquire configuration mutex for InfluxDB send");
                                continue;
                            }
                            snprintf(url, sizeof(url), "%s", _fullUrl);
                            snprintf(authHeader, sizeof(authHeader), "%s", _authHeader);
                            snprintf(measurement, sizeof(measurement), "%s", _influxDbConfiguration.measurement);
                            releaseMutex(&_configMutex);

                            _sendData(url, authHeader, measurement);

                            lastSendTime = currentTime;
                        } else {
                            LOG_VERBOSE("Delaying InfluxDB send due to previous failures");
                        }
                    }
                }
            }

            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(INFLUXDB_TASK_CHECK_INTERVAL));
            if (notificationValue > 0) {
                _taskShouldRun = false;
                break;
            }
        }

        LOG_DEBUG("InfluxDB task stopping");
        _influxDbTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static void _setConfigurationFromPreferences()
    {
        LOG_DEBUG("Setting InfluxDB configuration from Preferences...");

        InfluxDbConfiguration config; // Start with default configuration

        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, true)) {
            config.enabled = preferences.getBool(INFLUXDB_ENABLED_KEY, INFLUXDB_ENABLED_DEFAULT);
            snprintf(config.server, sizeof(config.server), "%s", preferences.getString(INFLUXDB_SERVER_KEY, INFLUXDB_SERVER_DEFAULT).c_str());
            config.port = preferences.getUShort(INFLUXDB_PORT_KEY, INFLUXDB_PORT_DEFAULT);
            config.version = preferences.getUChar(INFLUXDB_VERSION_KEY, INFLUXDB_VERSION_DEFAULT);
            snprintf(config.database, sizeof(config.database), "%s", preferences.getString(INFLUXDB_DATABASE_KEY, INFLUXDB_DATABASE_DEFAULT).c_str());
            snprintf(config.username, sizeof(config.username), "%s", preferences.getString(INFLUXDB_USERNAME_KEY, INFLUXDB_USERNAME_DEFAULT).c_str());
            snprintf(config.password, sizeof(config.password), "%s", preferences.getString(INFLUXDB_PASSWORD_KEY, INFLUXDB_PASSWORD_DEFAULT).c_str());
            snprintf(config.organization, sizeof(config.organization), "%s", preferences.getString(INFLUXDB_ORGANIZATION_KEY, INFLUXDB_ORGANIZATION_DEFAULT).c_str());
            snprintf(config.bucket, sizeof(config.bucket), "%s", preferences.getString(INFLUXDB_BUCKET_KEY, INFLUXDB_BUCKET_DEFAULT).c_str());
            snprintf(config.token, sizeof(config.token), "%s", preferences.getString(INFLUXDB_TOKEN_KEY, INFLUXDB_TOKEN_DEFAULT).c_str());
            snprintf(config.measurement, sizeof(config.measurement), "%s", preferences.getString(INFLUXDB_MEASUREMENT_KEY, INFLUXDB_MEASUREMENT_DEFAULT).c_str());
            config.frequencySeconds = preferences.getInt(INFLUXDB_FREQUENCY_KEY, INFLUXDB_FREQUENCY_DEFAULT);
            config.useSsl = preferences.getBool(INFLUXDB_USE_SSL_KEY, INFLUXDB_USE_SSL_DEFAULT);
            
            snprintf(_status, sizeof(_status), "Configuration loaded from Preferences");
            _statusTimestampUnix = CustomTime::getUnixTime();

            preferences.end();
        } else {
            LOG_ERROR("Failed to open Preferences namespace for InfluxDB. Using default configuration");
        }

        setConfiguration(config);

        LOG_DEBUG("Successfully set InfluxDB configuration from Preferences");
    }

    static void _saveConfigurationToPreferences()
    {
        LOG_DEBUG("Saving InfluxDB configuration to Preferences...");

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false)) {
            LOG_ERROR("Failed to open Preferences namespace for InfluxDB");
            return;
        }

        preferences.putBool(INFLUXDB_ENABLED_KEY, _influxDbConfiguration.enabled);
        preferences.putString(INFLUXDB_SERVER_KEY, _influxDbConfiguration.server);
        preferences.putUShort(INFLUXDB_PORT_KEY, _influxDbConfiguration.port);
        preferences.putUChar(INFLUXDB_VERSION_KEY, _influxDbConfiguration.version);
        preferences.putString(INFLUXDB_DATABASE_KEY, _influxDbConfiguration.database);
        preferences.putString(INFLUXDB_USERNAME_KEY, _influxDbConfiguration.username);
        preferences.putString(INFLUXDB_PASSWORD_KEY, _influxDbConfiguration.password);
        preferences.putString(INFLUXDB_ORGANIZATION_KEY, _influxDbConfiguration.organization);
        preferences.putString(INFLUXDB_BUCKET_KEY, _influxDbConfiguration.bucket);
        preferences.putString(INFLUXDB_TOKEN_KEY, _influxDbConfiguration.token);
        preferences.putString(INFLUXDB_MEASUREMENT_KEY, _influxDbConfiguration.measurement);
        preferences.putInt(INFLUXDB_FREQUENCY_KEY, _influxDbConfiguration.frequencySeconds);
        preferences.putBool(INFLUXDB_USE_SSL_KEY, _influxDbConfiguration.useSsl);

        preferences.end();

        LOG_DEBUG("Successfully saved InfluxDB configuration to Preferences");
    }

    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            LOG_WARNING("Invalid JSON document");
            return false;
        }

        if (partial) {
            // Partial validation - at least one valid field must be present
            if (jsonDocument["enabled"].is<bool>())             return true;        
            if (jsonDocument["server"].is<const char*>())       return true;        
            if (jsonDocument["port"].is<int16_t>())             return true;        
            if (jsonDocument["version"].is<uint8_t>())          return true;        
            if (jsonDocument["database"].is<const char*>())     return true;        
            if (jsonDocument["username"].is<const char*>())     return true;        
            if (jsonDocument["password"].is<const char*>())     return true;        
            if (jsonDocument["organization"].is<const char*>()) return true;        
            if (jsonDocument["bucket"].is<const char*>())       return true;        
            if (jsonDocument["token"].is<const char*>())        return true;        
            if (jsonDocument["measurement"].is<const char*>())  return true;        
            if (jsonDocument["frequency"].is<int32_t>())        return true;        
            if (jsonDocument["useSsl"].is<bool>())              return true;

            LOG_WARNING("No valid fields found in JSON document");
            return false;
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["enabled"].is<bool>())            { LOG_WARNING("enabled field is not a boolean"); return false; }
            if (!jsonDocument["server"].is<const char*>())      { LOG_WARNING("server field is not a string"); return false; }
            if (!jsonDocument["port"].is<uint16_t>())           { LOG_WARNING("port field is not an integer"); return false; }
            if (!jsonDocument["version"].is<uint8_t>())         { LOG_WARNING("version field is not an integer"); return false; }
            if (!jsonDocument["database"].is<const char*>())    { LOG_WARNING("database field is not a string"); return false; }
            if (!jsonDocument["username"].is<const char*>())    { LOG_WARNING("username field is not a string"); return false; }
            if (!jsonDocument["password"].is<const char*>())    { LOG_WARNING("password field is not a string"); return false; }
            if (!jsonDocument["organization"].is<const char*>()){ LOG_WARNING("organization field is not a string"); return false; }
            if (!jsonDocument["bucket"].is<const char*>())      { LOG_WARNING("bucket field is not a string"); return false; }
            if (!jsonDocument["token"].is<const char*>())       { LOG_WARNING("token field is not a string"); return false; }
            if (!jsonDocument["measurement"].is<const char*>()) { LOG_WARNING("measurement field is not a string"); return false; }
            if (!jsonDocument["frequency"].is<int32_t>())       { LOG_WARNING("frequency field is not an integer"); return false; }
            if (!jsonDocument["useSsl"].is<bool>())             { LOG_WARNING("useSsl field is not a boolean"); return false; }

            if (jsonDocument["frequency"].as<int32_t>() < INFLUXDB_MINIMUM_FREQUENCY || jsonDocument["frequency"].as<int32_t>() > INFLUXDB_MAXIMUM_FREQUENCY)
            {
                LOG_WARNING("frequency field must be between %d and %d seconds", INFLUXDB_MINIMUM_FREQUENCY, INFLUXDB_MAXIMUM_FREQUENCY);
                return false;
            }

            return true;
        }
    }

    static void _disable() {
        LOG_DEBUG("Disabling InfluxDB due to persistent failures...");

        InfluxDbConfiguration config;
        getConfiguration(config);

        config.enabled = false; // Disable InfluxDB
        setConfiguration(config);

        snprintf(_status, sizeof(_status), "Disabled due to persistent failures");
        _statusTimestampUnix = CustomTime::getUnixTime();

        LOG_DEBUG("InfluxDB disabled");
    }

    static void _setInfluxFullUrl() {
        char baseUrl[URL_BUFFER_SIZE + 15]; // Extra space for "http(s)://", server, and port

        snprintf(baseUrl, sizeof(baseUrl), "http%s://%s:%u", 
                _influxDbConfiguration.useSsl ? "s" : "", 
                _influxDbConfiguration.server, 
                _influxDbConfiguration.port);

        if (_influxDbConfiguration.version == 2)
        {
            snprintf(_fullUrl, sizeof(_fullUrl), "%s/api/v2/write?org=%s&bucket=%s",
                     baseUrl,
                     _influxDbConfiguration.organization,
                     _influxDbConfiguration.bucket);
        }
        else if (_influxDbConfiguration.version == 1)
        {
            snprintf(_fullUrl, sizeof(_fullUrl), "%s/write?db=%s",
                     baseUrl,
                     _influxDbConfiguration.database);
        } else {
            LOG_ERROR("Unsupported InfluxDB version: %u", _influxDbConfiguration.version);
            return;
        }

        LOG_DEBUG("InfluxDB full URL set to: %s", _fullUrl);
    }

    static void _setInfluxHeader() {
        if (_influxDbConfiguration.version == 2)
        {
            snprintf(_authHeader, sizeof(_authHeader), "Token %s", _influxDbConfiguration.token);
        }
        else if (_influxDbConfiguration.version == 1)
        {
            char credentials[sizeof(_influxDbConfiguration.username) + sizeof(_influxDbConfiguration.password) + 2];
            snprintf(credentials, sizeof(credentials), "%s:%s", _influxDbConfiguration.username, _influxDbConfiguration.password);

            String encodedCredentials = base64::encode((const uint8_t*)credentials, strnlen(credentials, sizeof(credentials)));

            snprintf(_authHeader, sizeof(_authHeader), "Basic %s", encodedCredentials.c_str());
        } else {
            LOG_ERROR("Unsupported InfluxDB version for authorization header: %u", _influxDbConfiguration.version);
            return;
        }

        LOG_DEBUG("InfluxDB authorization header set");
    }

    static void _sendData(const char* url, const char* authHeader, const char* measurement)
    {
        HTTPClient http;
        http.begin(url);
        http.addHeader("Authorization", authHeader);
        http.addHeader("Content-Type", "text/plain");

        // Allocate payload buffer in PSRAM for better memory utilization
        char *payload = (char*)ps_malloc(PAYLOAD_BUFFER_SIZE);
        if (!payload) {
            LOG_ERROR("Failed to allocate payload buffer in PSRAM");
            http.end();
            return;
        }
        memset(payload, 0, PAYLOAD_BUFFER_SIZE);
        
        char *ptr = payload;
        size_t remaining = PAYLOAD_BUFFER_SIZE;
        bool bufferFull = false;

        for (uint8_t i = 0; i < CHANNEL_COUNT && !bufferFull; i++)
        {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i))
            {
                MeterValues meterValues;
                Ade7953::getMeterValues(meterValues, i);
                
                char label[NAME_BUFFER_SIZE];
                Ade7953::getChannelLabel(i, label, sizeof(label));

                // Add separator if not first entry
                if (ptr != payload) {
                    int written = snprintf(ptr, remaining, "\n");
                    if (written >= remaining) { bufferFull = true; break; }
                    ptr += written;
                    remaining -= written;
                }
                
                // Add realtime line protocol
                char *realtimeLineProtocol = (char*)ps_malloc(LINE_PROTOCOL_BUFFER_SIZE);
                if (!realtimeLineProtocol) {
                    LOG_ERROR("Failed to allocate realtime line protocol buffer in PSRAM");
                    bufferFull = true;
                    break;
                }

                _formatLineProtocol(
                    measurement,
                    DEVICE_ID,
                    i,
                    label,
                    meterValues,
                    realtimeLineProtocol,
                    LINE_PROTOCOL_BUFFER_SIZE,
                    false
                );
                int written = snprintf(ptr, remaining, "%s", realtimeLineProtocol);
                free(realtimeLineProtocol);
                if (written >= remaining) { bufferFull = true; break; }
                ptr += written;
                remaining -= written;
                
                // Add energy line protocol
                char *energyLineProtocol = (char*)ps_malloc(LINE_PROTOCOL_BUFFER_SIZE);
                if (!energyLineProtocol) {
                    LOG_ERROR("Failed to allocate energy line protocol buffer in PSRAM");
                    bufferFull = true;
                    break;
                }
                _formatLineProtocol(
                    measurement,
                    DEVICE_ID,
                    i,
                    label,
                    meterValues,
                    energyLineProtocol,
                    LINE_PROTOCOL_BUFFER_SIZE,
                    true
                );
                written = snprintf(ptr, remaining, "\n%s", energyLineProtocol);
                free(energyLineProtocol);

                if (written >= remaining) { bufferFull = true; break; }
                ptr += written;
                remaining -= written;
            }
        }

        if (bufferFull) LOG_WARNING("Payload buffer filled completely, some data may be truncated");

        if (ptr == payload)
        {
            LOG_DEBUG("No data to send to InfluxDB");
            free(payload);
            http.end();
            return;
        }

        int32_t httpCode = http.POST(payload);

        if (httpCode >= HTTP_CODE_OK && httpCode < HTTP_CODE_MULTIPLE_CHOICES)
        {
            LOG_DEBUG("Successfully sent data %zu bytes to InfluxDB (HTTP %d)", PAYLOAD_BUFFER_SIZE - remaining, httpCode);
            statistics.influxdbUploadCount++;
            snprintf(_status, sizeof(_status), "Data sent successfully");
            _currentSendAttempt = 0;
        }
        else
        {
            statistics.influxdbUploadCountError++;
            _currentSendAttempt++;

            // Check for specific errors that warrant disabling InfluxDB
            if (httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) {
                LOG_ERROR("InfluxDB send failed due to authorization error (%d). Disabling InfluxDB.", httpCode);
                _disable();
                free(payload);
                http.end();
                return;
            }
            
            // Check if we've exceeded the maximum number of failures
            if (_currentSendAttempt >= INFLUXDB_MAX_CONSECUTIVE_FAILURES) {
                LOG_ERROR("InfluxDB send failed %u consecutive times. Disabling InfluxDB.", _currentSendAttempt);
                _disable();
                free(payload);
                http.end();
                return;
            }
            
            // Calculate next attempt time using exponential backoff
            uint64_t backoffDelay = calculateExponentialBackoff(_currentSendAttempt, INFLUXDB_INITIAL_RETRY_INTERVAL, INFLUXDB_MAX_RETRY_INTERVAL, INFLUXDB_RETRY_MULTIPLIER);
            _nextSendAttemptMillis = millis64() + backoffDelay;

            snprintf(_status, sizeof(_status), "Failed to send data (HTTP %ld) - Attempt %u", httpCode, _currentSendAttempt);
            _statusTimestampUnix = CustomTime::getUnixTime();
            
            LOG_WARNING("Failed to send data to InfluxDB (HTTP %d). Next attempt in %llu ms", httpCode, _nextSendAttemptMillis - millis64());
        }
        
        _statusTimestampUnix = CustomTime::getUnixTime();
        free(payload);
        http.end();
    }

    static void _formatLineProtocol(
        const char *measurement,
        const char *deviceId,
        uint8_t channel, 
        const char *label,
        const MeterValues &meterValues, 
        char *lineProtocolBuffer, 
        size_t lineProtocolBufferSize, 
        bool isEnergyData
    ) {
        char sanitizedLabel[NAME_BUFFER_SIZE + 20]; // Give some extra space for sanitization

        size_t labelLen = strnlen(label, NAME_BUFFER_SIZE);
        size_t writePos = 0;
        for (size_t i = 0; i < labelLen && writePos < sizeof(sanitizedLabel) - 1; i++)
        {
            char c = label[i];
            // Remove spaces, commas, equal signs, and colons and replace with underscores
            if (c == ' ' || c == ',' || c == '=' || c == ':') sanitizedLabel[writePos++] = '_';
            else sanitizedLabel[writePos++] = c;
        }
        sanitizedLabel[writePos] = '\0';

        if (isEnergyData)
        {
            snprintf(lineProtocolBuffer, lineProtocolBufferSize,
                     "%s,channel=%u,label=%s,device_id=%s active_energy_imported=%.*f,active_energy_exported=%.*f,reactive_energy_imported=%.*f,reactive_energy_exported=%.*f,apparent_energy=%.*f %llu000000",
                     measurement,
                     channel,
                     sanitizedLabel,
                     deviceId,
                     ENERGY_DECIMALS, roundToDecimals(meterValues.activeEnergyImported, ENERGY_DECIMALS),
                     ENERGY_DECIMALS, roundToDecimals(meterValues.activeEnergyExported, ENERGY_DECIMALS),
                     ENERGY_DECIMALS, roundToDecimals(meterValues.reactiveEnergyImported, ENERGY_DECIMALS),
                     ENERGY_DECIMALS, roundToDecimals(meterValues.reactiveEnergyExported, ENERGY_DECIMALS),
                     ENERGY_DECIMALS, roundToDecimals(meterValues.apparentEnergy, ENERGY_DECIMALS),
                     meterValues.lastUnixTimeMilliseconds);
        }
        else
        {
            snprintf(lineProtocolBuffer, lineProtocolBufferSize,
                     "%s,channel=%u,label=%s,device_id=%s voltage=%.*f,current=%.*f,active_power=%.*f,reactive_power=%.*f,apparent_power=%.*f,power_factor=%.*f %llu000000",
                     measurement,
                     channel,
                     sanitizedLabel,
                     deviceId,
                     VOLTAGE_DECIMALS, roundToDecimals(meterValues.voltage, VOLTAGE_DECIMALS),
                     CURRENT_DECIMALS, roundToDecimals(meterValues.current, CURRENT_DECIMALS),
                     POWER_DECIMALS, roundToDecimals(meterValues.activePower, POWER_DECIMALS),
                     POWER_DECIMALS, roundToDecimals(meterValues.reactivePower, POWER_DECIMALS),
                     POWER_DECIMALS, roundToDecimals(meterValues.apparentPower, POWER_DECIMALS),
                     POWER_FACTOR_DECIMALS, roundToDecimals(meterValues.powerFactor, POWER_FACTOR_DECIMALS),
                     meterValues.lastUnixTimeMilliseconds);
        }
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_influxDbTaskHandle, INFLUXDB_TASK_STACK_SIZE);
    }
} // namespace InfluxDbClient