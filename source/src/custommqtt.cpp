#include "custommqtt.h"

namespace CustomMqtt
{
    // Static variables
    // ==========================================================
    
    // MQTT client objects
    static WiFiClient _net;
    static PubSubClient _customClientMqtt(_net);
    
    // State variables
    static bool _isSetupDone = false;
    static uint32_t _currentMqttConnectionAttempt = 0;
    static uint64_t _nextMqttConnectionAttemptMillis = 0;
    static uint32_t _currentFailedMessagePublishAttempt = 0;

    // Custom MQTT configuration
    static CustomMqttConfiguration _customMqttConfiguration;
    
    // Runtime connection status - kept in memory only, not saved to preferences
    static char _status[STATUS_BUFFER_SIZE];
    static uint64_t _statusTimestampUnix;

    // Task variables
    static TaskHandle_t _customMqttTaskHandle = nullptr;
    static bool _taskShouldRun = false;

    // Thread safety
    static SemaphoreHandle_t _configMutex = nullptr;

    // Private function declarations
    // =========================================================

    // Configuration management
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    
    // MQTT operations
    static bool _connectMqtt();
    static void _publishMeter();
    static bool _publishMeterStreaming(JsonDocument &jsonDocument);
    static bool _publishMessage(const char *topic, const char *message);
    
    // JSON validation
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);
    
    // Task management
    static void _customMqttTask(void* parameter);
    static void _startTask();
    static void _stopTask();
    static void _disable(); // Needed for halting the functionality if we have too many failure

    // Utils
    const char* _getMqttStateReason(int32_t state);

    // Public API functions
    // =========================================================

    void begin()
    {
        LOG_DEBUG("Setting up Custom MQTT client...");
        
        if (!createMutexIfNeeded(&_configMutex)) return;
        
        _isSetupDone = true; // Must set before since we have checks on the setup later        
        _customClientMqtt.setBufferSize(STREAM_UTILS_PACKET_SIZE);
        _setConfigurationFromPreferences(); // Here we load and set the config. The setConfig will handle starting the task if enabled
        
        LOG_DEBUG("Custom MQTT client setup complete");
    }

    void stop()
    {
        LOG_DEBUG("Stopping Custom MQTT client...");
        _stopTask();
        
        // Disconnect and cleanup network client
        if (_customClientMqtt.connected()) {
            _customClientMqtt.disconnect();
        }
        
        // Clean up mutex
        if (_configMutex != nullptr) {
            vSemaphoreDelete(_configMutex);
            _configMutex = nullptr;
        }
        
        _isSetupDone = false;
        LOG_INFO("Custom MQTT client stopped");
    }

    void getConfiguration(CustomMqttConfiguration &config)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return;
        }
        config = _customMqttConfiguration;
    }

    bool setConfiguration(const CustomMqttConfiguration &config)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return false;
        }

        // Acquire mutex with timeout
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for setConfiguration");
            return false;
        }

        _stopTask();

        _customMqttConfiguration = config; // Copy to the static configuration variable
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        // Reset counter to start fresh
        _nextMqttConnectionAttemptMillis = 0; // Immediately attempt to connect
        _currentMqttConnectionAttempt = 0;
        _currentFailedMessagePublishAttempt = 0;

        _saveConfigurationToPreferences();

        if (_customMqttConfiguration.enabled) _startTask();

        // Release mutex
        releaseMutex(&_configMutex);

        LOG_DEBUG("Custom MQTT configuration set");
        return true;
    }

    void resetConfiguration() 
    {
        LOG_DEBUG("Resetting Custom MQTT configuration to default");
        
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return;
        }

        CustomMqttConfiguration defaultConfig;
        setConfiguration(defaultConfig);

        LOG_INFO("Custom MQTT configuration reset to default");
    }

    void getConfigurationAsJson(JsonDocument &jsonDocument) 
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return;
        }
        configurationToJson(_customMqttConfiguration, jsonDocument);
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return false;
        }

        CustomMqttConfiguration config;
        config = _customMqttConfiguration; // Start with current configuration (yeah I know it's cumbersome)
        if (!configurationFromJson(jsonDocument, config, partial)) {
            LOG_ERROR("Failed to set configuration from JSON");
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return;
        }

        jsonDocument["enabled"] = config.enabled;
        jsonDocument["server"] = config.server;
        jsonDocument["port"] = config.port;
        jsonDocument["clientid"] = config.clientid;
        jsonDocument["topic"] = config.topic;
        jsonDocument["frequency"] = config.frequencySeconds;
        jsonDocument["useCredentials"] = config.useCredentials;
        jsonDocument["username"] = config.username;
        jsonDocument["password"] = config.password;

        LOG_DEBUG("Successfully converted configuration to JSON");
    }

    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config, bool partial)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
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
            if (jsonDocument["clientid"].is<const char*>())     snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
            if (jsonDocument["topic"].is<const char*>())        snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
            if (jsonDocument["frequency"].is<uint32_t>())       config.frequencySeconds = jsonDocument["frequency"].as<uint32_t>();
            if (jsonDocument["useCredentials"].is<bool>())      config.useCredentials = jsonDocument["useCredentials"].as<bool>();
            if (jsonDocument["username"].is<const char*>())     snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
            if (jsonDocument["password"].is<const char*>())     snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        } else {
            // Full update - set all fields
            config.enabled = jsonDocument["enabled"].as<bool>();
            snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
            config.port = jsonDocument["port"].as<uint16_t>();
            snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
            snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
            config.frequencySeconds = jsonDocument["frequency"].as<uint32_t>();
            config.useCredentials = jsonDocument["useCredentials"].as<bool>();
            snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
            snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        }
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        LOG_DEBUG("Successfully converted JSON to configuration%s", partial ? " (partial)" : "");
        return true;
    }

    void getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return;
        }

        if (statusBuffer && statusSize > 0) snprintf(statusBuffer, statusSize, "%s", _status);
        if (timestampBuffer && timestampSize > 0) CustomTime::timestampIsoFromUnix(_statusTimestampUnix, timestampBuffer, timestampSize);
    }

    // Private function implementations
    // =========================================================

    static void _startTask()
    {
        if (_customMqttTaskHandle != nullptr) {
            LOG_DEBUG("Custom MQTT task is already running");
            return;
        }

        LOG_DEBUG("Starting Custom MQTT task");

        // Configure MQTT client before starting task
        _customClientMqtt.setServer(_customMqttConfiguration.server, _customMqttConfiguration.port);
        
        BaseType_t result = xTaskCreate(
            _customMqttTask,
            CUSTOM_MQTT_TASK_NAME,
            CUSTOM_MQTT_TASK_STACK_SIZE,
            nullptr,
            CUSTOM_MQTT_TASK_PRIORITY,
            &_customMqttTaskHandle
        );

        if (result != pdPASS) {
            LOG_ERROR("Failed to create Custom MQTT task");
            _customMqttTaskHandle = nullptr;
        }
    }

    static void _stopTask() { stopTaskGracefully(&_customMqttTaskHandle, "Custom MQTT task"); }

    static void _customMqttTask(void* parameter)
    {
        LOG_DEBUG("Custom MQTT task started");
        
        _taskShouldRun = true;
        uint64_t lastPublishTime = 0;

        while (_taskShouldRun) {
            if (CustomWifi::isFullyConnected()) { // We are connected
                if (_customMqttConfiguration.enabled) { // We have the custom MQTT enabled
                    if (_customClientMqtt.connected()) { // We are connected to MQTT
                        if (_currentMqttConnectionAttempt > 0) { // If we were having problems, reset the attempt counter since we are now connected
                            LOG_DEBUG("Custom MQTT reconnected successfully after %d attempts.", _currentMqttConnectionAttempt);
                            _currentMqttConnectionAttempt = 0;
                        }

                        _customClientMqtt.loop(); // Required by the MQTT library to process incoming messages and maintain connection

                        uint64_t currentTime = millis64();
                        // Check if enough time has passed since last publish
                        if ((currentTime - lastPublishTime) >= (_customMqttConfiguration.frequencySeconds * 1000)) {
                            _publishMeter();
                            lastPublishTime = currentTime;
                        }
                    } else {
                        // Enough time has passed since last attempt (in case of failures) to retry connection
                        if (millis64() >= _nextMqttConnectionAttemptMillis) {
                            LOG_DEBUG("Custom MQTT client not connected. Attempting to connect...");
                            _connectMqtt();
                        }
                    }
                }
            }

            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CUSTOM_MQTT_TASK_CHECK_INTERVAL));
            if (notificationValue > 0) {
                _taskShouldRun = false;
                break;
            }
        }
        
        _customClientMqtt.disconnect();
        LOG_DEBUG("Custom MQTT task stopping");
        _customMqttTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static void _setConfigurationFromPreferences()
    {
        LOG_DEBUG("Setting Custom MQTT configuration from Preferences...");

        CustomMqttConfiguration config; // Start with default configuration

        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, true)) {
            config.enabled = preferences.getBool(CUSTOM_MQTT_ENABLED_KEY, DEFAULT_IS_CUSTOM_MQTT_ENABLED);
            snprintf(config.server, sizeof(config.server), "%s", preferences.getString(CUSTOM_MQTT_SERVER_KEY, MQTT_CUSTOM_SERVER_DEFAULT).c_str());
            config.port = preferences.getUShort(CUSTOM_MQTT_PORT_KEY, MQTT_CUSTOM_PORT_DEFAULT);
            snprintf(config.clientid, sizeof(config.clientid), "%s", preferences.getString(CUSTOM_MQTT_CLIENT_ID_KEY, MQTT_CUSTOM_CLIENTID_DEFAULT).c_str());
            snprintf(config.topic, sizeof(config.topic), "%s", preferences.getString(CUSTOM_MQTT_TOPIC_KEY, MQTT_CUSTOM_TOPIC_DEFAULT).c_str());
            config.frequencySeconds = preferences.getUInt(CUSTOM_MQTT_FREQUENCY_KEY, MQTT_CUSTOM_FREQUENCY_SECONDS_DEFAULT);
            config.useCredentials = preferences.getBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT);
            snprintf(config.username, sizeof(config.username), "%s", preferences.getString(CUSTOM_MQTT_USERNAME_KEY, MQTT_CUSTOM_USERNAME_DEFAULT).c_str());
            snprintf(config.password, sizeof(config.password), "%s", preferences.getString(CUSTOM_MQTT_PASSWORD_KEY, MQTT_CUSTOM_PASSWORD_DEFAULT).c_str());
            
            snprintf(_status, sizeof(_status), "Configuration loaded from Preferences");
            _statusTimestampUnix = CustomTime::getUnixTime();

            preferences.end();
        } else {
            LOG_ERROR("Failed to open Preferences namespace for Custom MQTT. Using default configuration");
        }

        setConfiguration(config);

        LOG_DEBUG("Successfully set Custom MQTT configuration from Preferences");
    }

    static void _saveConfigurationToPreferences()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) {
            LOG_ERROR("Failed to open Preferences namespace for Custom MQTT");
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, _customMqttConfiguration.enabled);
        preferences.putString(CUSTOM_MQTT_SERVER_KEY, _customMqttConfiguration.server);
        preferences.putUShort(CUSTOM_MQTT_PORT_KEY, _customMqttConfiguration.port);
        preferences.putString(CUSTOM_MQTT_CLIENT_ID_KEY, _customMqttConfiguration.clientid);
        preferences.putString(CUSTOM_MQTT_TOPIC_KEY, _customMqttConfiguration.topic);
        preferences.putUInt(CUSTOM_MQTT_FREQUENCY_KEY, _customMqttConfiguration.frequencySeconds);
        preferences.putBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, _customMqttConfiguration.useCredentials);
        preferences.putString(CUSTOM_MQTT_USERNAME_KEY, _customMqttConfiguration.username);
        preferences.putString(CUSTOM_MQTT_PASSWORD_KEY, _customMqttConfiguration.password);

        preferences.end();

        LOG_DEBUG("Successfully saved Custom MQTT configuration to Preferences");
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
            if (jsonDocument["enabled"].is<bool>()) return true;        
            if (jsonDocument["server"].is<const char*>()) return true;        
            if (jsonDocument["port"].is<uint16_t>()) return true;        
            if (jsonDocument["clientid"].is<const char*>()) return true;        
            if (jsonDocument["topic"].is<const char*>()) return true;        
            if (jsonDocument["frequency"].is<uint32_t>()) return true;        
            if (jsonDocument["useCredentials"].is<bool>()) return true;        
            if (jsonDocument["username"].is<const char*>()) return true;        
            if (jsonDocument["password"].is<const char*>()) return true;

            LOG_WARNING("No valid fields found in JSON document");
            return false;
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["enabled"].is<bool>()) { LOG_WARNING("enabled field is not a boolean"); return false; }
            if (!jsonDocument["server"].is<const char*>()) { LOG_WARNING("server field is not a string"); return false; }
            if (!jsonDocument["port"].is<uint16_t>()) { LOG_WARNING("port field is not an integer"); return false; }
            if (!jsonDocument["clientid"].is<const char*>()) { LOG_WARNING("clientid field is not a string"); return false; }
            if (!jsonDocument["topic"].is<const char*>()) { LOG_WARNING("topic field is not a string"); return false; }
            if (!jsonDocument["frequency"].is<uint32_t>()) { LOG_WARNING("frequency field is not an integer"); return false; }
            if (!jsonDocument["useCredentials"].is<bool>()) { LOG_WARNING("useCredentials field is not a boolean"); return false; }
            if (!jsonDocument["username"].is<const char*>()) { LOG_WARNING("username field is not a string"); return false; }
            if (!jsonDocument["password"].is<const char*>()) { LOG_WARNING("password field is not a string"); return false; }

            return true;
        }
    }

    static void _disable() 
    {
        LOG_DEBUG("Disabling Custom MQTT due to persistent failures...");

        _customMqttConfiguration.enabled = false;
        setConfiguration(_customMqttConfiguration); // This will save the configuration and stop the task

        snprintf(_status, sizeof(_status), "Disabled due to persistent failures");
        _statusTimestampUnix = CustomTime::getUnixTime();

        LOG_DEBUG("Custom MQTT disabled");
    }

    static bool _connectMqtt()
    {
        LOG_DEBUG("Attempt to connect to Custom MQTT (attempt %d)...", _currentMqttConnectionAttempt + 1);

        // Ensure clean connection state
        if (_customClientMqtt.connected()) {
            _customClientMqtt.disconnect();
        }

        // If the clientid is empty, use the default one
        char clientId[NAME_BUFFER_SIZE];
        snprintf(clientId, sizeof(clientId), "%s", _customMqttConfiguration.clientid);
        if (strlen(_customMqttConfiguration.clientid) == 0) {
            LOG_WARNING("Client ID is empty, using device client ID");
            snprintf(clientId, sizeof(clientId), "%s", DEVICE_ID);
        }
        
        bool res;
        if (_customMqttConfiguration.useCredentials) {
            res = _customClientMqtt.connect(
                clientId,
                _customMqttConfiguration.username,
                _customMqttConfiguration.password);
        } else {
            res = _customClientMqtt.connect(clientId);
        }

        if (res) {
            LOG_INFO("Connected to Custom MQTT | Server: %s, Port: %u, Client ID: %s, Topic: %s",
                        _customMqttConfiguration.server,
                        _customMqttConfiguration.port,
                        clientId,
                        _customMqttConfiguration.topic);

            _currentMqttConnectionAttempt = 0; // Reset attempt counter on successful connection
            snprintf(_status, sizeof(_status), "Connected");
            _statusTimestampUnix = CustomTime::getUnixTime();

            return true;
        } else {
            _currentMqttConnectionAttempt++;
            int32_t currentState = _customClientMqtt.state();
            const char* _reason = _getMqttStateReason(currentState);

            // Check for specific errors that warrant disabling Custom MQTT
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                 LOG_ERROR("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling Custom MQTT.",
                    currentState);
                _disable();
                return false;
            }

            if (_currentMqttConnectionAttempt >= MQTT_CUSTOM_MAX_RECONNECT_ATTEMPTS) {
                LOG_ERROR("Custom MQTT connection failed after %lu attempts. Disabling Custom MQTT.", _currentMqttConnectionAttempt);
                _disable();
                return false;
            }

            // Calculate next attempt time using exponential backoff
            uint64_t _backoffDelay = calculateExponentialBackoff(_currentMqttConnectionAttempt, MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL, MQTT_CUSTOM_MAX_RECONNECT_INTERVAL, MQTT_CUSTOM_RECONNECT_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;

            snprintf(_status, sizeof(_status), "Connection failed: %s (Attempt %lu). Retrying in %llu ms", _reason, _currentMqttConnectionAttempt, _backoffDelay);
            _statusTimestampUnix = CustomTime::getUnixTime();

            LOG_WARNING("Failed to connect to Custom MQTT (attempt %lu). Reason: %s (%ld). Retrying in %llu ms", 
                           _currentMqttConnectionAttempt,
                           _reason,
                           currentState,
                           _nextMqttConnectionAttemptMillis - millis64());

            return false;
        }
    }

    static void _publishMeter()
    {
        JsonDocument jsonDocument;
        Ade7953::fullMeterValuesToJson(jsonDocument);

        // Validate that we have actual data before serializing (since the JSON serialization allows for empty objects)
        if (jsonDocument.isNull() || jsonDocument.size() == 0) {
            LOG_DEBUG("No meter data available for publishing to Custom MQTT");
            return;
        }

        // Measure JSON size first for timing comparison
        size_t jsonSize = measureJson(jsonDocument);
        LOG_DEBUG("JSON document size: %zu bytes", jsonSize);

        uint64_t startTime = millis64();
        
        bool publishSuccess = _publishMeterStreaming(jsonDocument);
        
        uint64_t endTime = millis64();
        uint64_t duration = endTime - startTime;
        
        LOG_DEBUG("Streaming publish completed in %llu ms for %zu bytes (%.2f bytes/ms)", 
                  duration, jsonSize, jsonSize > 0 ? (float)jsonSize / (float)duration : 0.0f);
        
        if (publishSuccess) LOG_DEBUG("Meter data published to Custom MQTT via streaming");
        else LOG_WARNING("Failed to publish meter data to Custom MQTT via streaming");
    }

    static bool _publishMeterStreaming(JsonDocument &jsonDocument)
    {
        statistics.customMqttMessagesPublishedError++; // Pre-increment, will decrement on success
        _currentFailedMessagePublishAttempt++;

        if (_currentFailedMessagePublishAttempt > MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS) {
            LOG_ERROR("Too many failed message publish attempts (%d). Disabling Custom MQTT.", _currentFailedMessagePublishAttempt);
            _disable();
            return false;
        }

        if (!CustomWifi::isFullyConnected()) {
            LOG_WARNING("Custom MQTT not connected to WiFi. Skipping streaming publish");
            return false;
        }

        if (!_customClientMqtt.connected()) {
            LOG_WARNING("Custom MQTT client not connected. State: %s. Skipping streaming publish", 
                       _getMqttStateReason(_customClientMqtt.state()));
            return false;
        }

        const char* topic = _customMqttConfiguration.topic;
        if (strlen(topic) == 0) {
            LOG_WARNING("Empty topic. Skipping streaming publish");
            return false;
        }

        // Calculate the JSON payload size
        size_t payloadLength = measureJson(jsonDocument);
        if (payloadLength == 0) {
            LOG_WARNING("Empty JSON payload. Skipping streaming publish");
            return false;
        }

        LOG_DEBUG("Starting streaming publish to topic '%s' with payload size %zu bytes", topic, payloadLength);

        // Begin publish with the calculated payload size
        if (!_customClientMqtt.beginPublish(topic, payloadLength, false)) {
            LOG_WARNING("Failed to begin streaming publish. MQTT client state: %s", 
                       _getMqttStateReason(_customClientMqtt.state()));
            return false;
        }

        // Stream JSON directly to the MQTT client
        BufferingPrint bufferedCustomMqttClient(_customClientMqtt, STREAM_UTILS_PACKET_SIZE);
        size_t bytesWritten = serializeJson(jsonDocument, bufferedCustomMqttClient);
        bufferedCustomMqttClient.flush();
        
        // End the publish
        if (_customClientMqtt.endPublish() != 1) {
            LOG_WARNING("Failed to end streaming publish. MQTT client state: %s", 
                       _getMqttStateReason(_customClientMqtt.state()));
            return false;
        }

        // Verify that we wrote the expected number of bytes
        if (bytesWritten != payloadLength) {
            LOG_WARNING("Streaming publish size mismatch: expected %zu bytes, wrote %zu bytes", 
                       payloadLength, bytesWritten);
            return false;
        }

        // Success - reset counters and update statistics
        statistics.customMqttMessagesPublishedError--; // Decrement the pre-incremented error count
        _currentFailedMessagePublishAttempt = 0;
        statistics.customMqttMessagesPublished++;
        
        LOG_DEBUG("Streaming publish successful: %zu bytes written to topic '%s'", bytesWritten, topic);
        return true;
    }

    static bool _publishMessage(const char *topic, const char *message)
    {

        statistics.customMqttMessagesPublishedError++; // I know it is weird (but we avoid repetition), but if later we reach the end, we decrement it
        _currentFailedMessagePublishAttempt++;

        if (_currentFailedMessagePublishAttempt > MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS) {
            LOG_ERROR("Too many failed message publish attempts (%d). Disabling Custom MQTT.", _currentFailedMessagePublishAttempt);
            _disable();
            return false;
        }

        if (topic == nullptr || message == nullptr)
        {
            LOG_WARNING("Null pointer or message passed, meaning Custom MQTT not initialized yet");
            return false;
        }

        if (!CustomWifi::isFullyConnected())
        {
            LOG_WARNING("Custom MQTT not connected to WiFi. Skipping publishing on %s", topic);
            return false;
        }

        if (!_customClientMqtt.connected())
        {
            LOG_WARNING("Custom MQTT client not connected. State: %s. Skipping publishing on %s", _getMqttStateReason(_customClientMqtt.state()), topic);
            return false;
        }

        if (strlen(topic) == 0 || strlen(message) == 0)
        {
            LOG_WARNING("Empty topic or message. Skipping publishing.");
            return false;
        }

        if (!_customClientMqtt.publish(topic, message))
        {
            LOG_WARNING("Failed to publish message. Custom MQTT client state: %s", _getMqttStateReason(_customClientMqtt.state()));
            return false;
        }
        
        statistics.customMqttMessagesPublishedError--; // Because we increased before
        _currentFailedMessagePublishAttempt = 0; // Reset failed message publish attempt counter on successful publish
        statistics.customMqttMessagesPublished++;
        LOG_DEBUG("Message published to %s (%d bytes)", topic, strlen(message));
        return true;
    }

    const char* _getMqttStateReason(int32_t state)
    {

        // Full description of the MQTT state codes
        // -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
        // -3 : MQTT_CONNECTION_LOST - the network connection was broken
        // -2 : MQTT_CONNECT_FAILED - the network connection failed
        // -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
        // 0 : MQTT_CONNECTED - the client is connected
        // 1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
        // 2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
        // 3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
        // 4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
        // 5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect

        switch (state)
        {
            case -4: return "MQTT_CONNECTION_TIMEOUT";
            case -3: return "MQTT_CONNECTION_LOST";
            case -2: return "MQTT_CONNECT_FAILED";
            case -1: return "MQTT_DISCONNECTED";
            case 0: return "MQTT_CONNECTED";
            case 1: return "MQTT_CONNECT_BAD_PROTOCOL";
            case 2: return "MQTT_CONNECT_BAD_CLIENT_ID";
            case 3: return "MQTT_CONNECT_UNAVAILABLE";
            case 4: return "MQTT_CONNECT_BAD_CREDENTIALS";
            case 5: return "MQTT_CONNECT_UNAUTHORIZED";
            default: return "Unknown MQTT state";
        }
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_customMqttTaskHandle, CUSTOM_MQTT_TASK_STACK_SIZE);
    }
}