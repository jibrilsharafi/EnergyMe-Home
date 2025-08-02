#include "custommqtt.h"

static const char *TAG = "custommqtt";

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
    static bool _publishMessage(const char *topic, const char *message);
    
    // JSON validation
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);
    
    // Task management
    static void _customMqttTask(void* parameter);
    static void _startTask();
    static void _stopTask();
    static void _disable(); // Needed for halting the functionality if we have too many failure

    // Public API functions
    // =========================================================

    void begin()
    {
        logger.debug("Setting up Custom MQTT client...", TAG);
        
        // Create configuration mutex
        if (_configMutex == nullptr) {
            _configMutex = xSemaphoreCreateMutex();
            if (_configMutex == nullptr) {
                logger.error("Failed to create configuration mutex", TAG);
                return;
            }
        }
        
        _isSetupDone = true; // Must set before since we have checks on the setup later
        _customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
        _setConfigurationFromPreferences(); // Here we load and set the config. The setConfig will handle starting the task if enabled
        logger.debug("Custom MQTT client setup complete", TAG);
    }

    void stop()
    {
        logger.debug("Stopping Custom MQTT client...", TAG);
        _stopTask();
        
        // Clean up mutex
        if (_configMutex != nullptr) {
            vSemaphoreDelete(_configMutex);
            _configMutex = nullptr;
        }
        
        _isSetupDone = false;
        logger.info("Custom MQTT client stopped", TAG);
    }

    void getConfiguration(CustomMqttConfiguration &config)
    {
        if (!_isSetupDone) begin();
        config = _customMqttConfiguration;
    }

    bool setConfiguration(CustomMqttConfiguration &config)
    {
        logger.debug("Setting Custom MQTT configuration...", TAG);
        
        if (!_isSetupDone) begin();

        // Acquire mutex with timeout
        if (_configMutex == nullptr || xSemaphoreTake(_configMutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            logger.error("Failed to acquire configuration mutex for setConfiguration", TAG);
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
        xSemaphoreGive(_configMutex);

        logger.debug("Custom MQTT configuration set", TAG);
        return true;
    }

    void resetConfiguration() 
    {
        logger.debug("Resetting Custom MQTT configuration to default", TAG);
        
        if (!_isSetupDone) begin();

        CustomMqttConfiguration defaultConfig;
        setConfiguration(defaultConfig);

        logger.info("Custom MQTT configuration reset to default", TAG);
    }

    void getConfigurationAsJson(JsonDocument &jsonDocument) 
    {
        if (!_isSetupDone) begin();
        configurationToJson(_customMqttConfiguration, jsonDocument);
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        if (!_isSetupDone) begin();

        CustomMqttConfiguration config;
        config = _customMqttConfiguration; // Start with current configuration (yeah I know it's cumbersome)
        if (!configurationFromJson(jsonDocument, config, partial)) {
            logger.error("Failed to set configuration from JSON", TAG);
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument)
    {
        if (!_isSetupDone) begin();

        jsonDocument["enabled"] = config.enabled;
        jsonDocument["server"] = config.server;
        jsonDocument["port"] = config.port;
        jsonDocument["clientid"] = config.clientid;
        jsonDocument["topic"] = config.topic;
        jsonDocument["frequency"] = config.frequencySeconds;
        jsonDocument["useCredentials"] = config.useCredentials;
        jsonDocument["username"] = config.username;
        jsonDocument["password"] = config.password;

        logger.debug("Successfully converted configuration to JSON", TAG);
    }

    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config, bool partial)
    {
        if (!_isSetupDone) begin();

        if (!_validateJsonConfiguration(jsonDocument, partial))
        {
            logger.error("Invalid JSON configuration", TAG);
            return false;
        }

        if (partial) {
            // Update only fields that are present in JSON
            if (jsonDocument["enabled"].is<bool>())             config.enabled = jsonDocument["enabled"].as<bool>();
            if (jsonDocument["server"].is<const char*>())       snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
            if (jsonDocument["port"].is<uint32_t>())            config.port = jsonDocument["port"].as<uint32_t>();
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
            config.port = jsonDocument["port"].as<uint32_t>();
            snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
            snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
            config.frequencySeconds = jsonDocument["frequency"].as<uint32_t>();
            config.useCredentials = jsonDocument["useCredentials"].as<bool>();
            snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
            snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        }
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Successfully converted JSON to configuration%s", TAG, partial ? " (partial)" : "");
        return true;
    }

    void getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize)
    {
        if (!_isSetupDone) begin();

        if (statusBuffer && statusSize > 0) snprintf(statusBuffer, statusSize, "%s", _status);
        if (timestampBuffer && timestampSize > 0) CustomTime::timestampIsoFromUnix(_statusTimestampUnix, timestampBuffer, timestampSize);
    }

    // Private function implementations
    // =========================================================

    static void _startTask()
    {
        if (_customMqttTaskHandle != nullptr) {
            logger.debug("Custom MQTT task is already running", TAG);
            return;
        }

        logger.debug("Starting Custom MQTT task", TAG);

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
            logger.error("Failed to create Custom MQTT task", TAG);
            _customMqttTaskHandle = nullptr;
        }
    }

    static void _stopTask() { stopTaskGracefully(&_customMqttTaskHandle, "Custom MQTT task"); }

    static void _customMqttTask(void* parameter)
    {
        logger.debug("Custom MQTT task started", TAG);
        
        _taskShouldRun = true;
        uint64_t lastPublishTime = 0;

        while (_taskShouldRun) {
            if (CustomWifi::isFullyConnected()) { // We are connected
                if (_customMqttConfiguration.enabled) { // We have the custom MQTT enabled
                    if (_customClientMqtt.connected()) { // We are connected to MQTT
                        if (_currentMqttConnectionAttempt > 0) { // If we were having problems, reset the attempt counter since we are now connected
                            logger.debug("Custom MQTT reconnected successfully after %d attempts.", TAG, _currentMqttConnectionAttempt);
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
                            logger.debug("Custom MQTT client not connected. Attempting to connect...", TAG);
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
        logger.debug("Custom MQTT task stopping", TAG);
        _customMqttTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static void _setConfigurationFromPreferences()
    {
        logger.debug("Setting Custom MQTT configuration from Preferences...", TAG);

        CustomMqttConfiguration config; // Start with default configuration

        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, true)) {
            config.enabled = preferences.getBool(CUSTOM_MQTT_ENABLED_KEY, DEFAULT_IS_CUSTOM_MQTT_ENABLED);
            snprintf(config.server, sizeof(config.server), "%s", preferences.getString(CUSTOM_MQTT_SERVER_KEY, MQTT_CUSTOM_SERVER_DEFAULT).c_str());
            config.port = preferences.getUInt(CUSTOM_MQTT_PORT_KEY, MQTT_CUSTOM_PORT_DEFAULT);
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
            logger.error("Failed to open Preferences namespace for Custom MQTT. Using default configuration", TAG);
        }

        setConfiguration(config);

        logger.debug("Successfully set Custom MQTT configuration from Preferences", TAG);
    }

    static void _saveConfigurationToPreferences()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) {
            logger.error("Failed to open Preferences namespace for Custom MQTT", TAG);
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, _customMqttConfiguration.enabled);
        preferences.putString(CUSTOM_MQTT_SERVER_KEY, _customMqttConfiguration.server);
        preferences.putUInt(CUSTOM_MQTT_PORT_KEY, _customMqttConfiguration.port);
        preferences.putString(CUSTOM_MQTT_CLIENT_ID_KEY, _customMqttConfiguration.clientid);
        preferences.putString(CUSTOM_MQTT_TOPIC_KEY, _customMqttConfiguration.topic);
        preferences.putUInt(CUSTOM_MQTT_FREQUENCY_KEY, _customMqttConfiguration.frequencySeconds);
        preferences.putBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, _customMqttConfiguration.useCredentials);
        preferences.putString(CUSTOM_MQTT_USERNAME_KEY, _customMqttConfiguration.username);
        preferences.putString(CUSTOM_MQTT_PASSWORD_KEY, _customMqttConfiguration.password);

        preferences.end();

        logger.debug("Successfully saved Custom MQTT configuration to Preferences", TAG);
    }

    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            logger.warning("Invalid JSON document", TAG);
            return false;
        }

        if (partial) {
            // Partial validation - at least one valid field must be present
            if (jsonDocument["enabled"].is<bool>()) return true;        
            if (jsonDocument["server"].is<const char*>()) return true;        
            if (jsonDocument["port"].is<uint32_t>()) return true;        
            if (jsonDocument["clientid"].is<const char*>()) return true;        
            if (jsonDocument["topic"].is<const char*>()) return true;        
            if (jsonDocument["frequency"].is<uint32_t>()) return true;        
            if (jsonDocument["useCredentials"].is<bool>()) return true;        
            if (jsonDocument["username"].is<const char*>()) return true;        
            if (jsonDocument["password"].is<const char*>()) return true;

            logger.warning("No valid fields found in JSON document", TAG);
            return false;
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["enabled"].is<bool>()) { logger.warning("enabled field is not a boolean", TAG); return false; }
            if (!jsonDocument["server"].is<const char*>()) { logger.warning("server field is not a string", TAG); return false; }
            if (!jsonDocument["port"].is<uint32_t>()) { logger.warning("port field is not an integer", TAG); return false; }
            if (!jsonDocument["clientid"].is<const char*>()) { logger.warning("clientid field is not a string", TAG); return false; }
            if (!jsonDocument["topic"].is<const char*>()) { logger.warning("topic field is not a string", TAG); return false; }
            if (!jsonDocument["frequency"].is<uint32_t>()) { logger.warning("frequency field is not an integer", TAG); return false; }
            if (!jsonDocument["useCredentials"].is<bool>()) { logger.warning("useCredentials field is not a boolean", TAG); return false; }
            if (!jsonDocument["username"].is<const char*>()) { logger.warning("username field is not a string", TAG); return false; }
            if (!jsonDocument["password"].is<const char*>()) { logger.warning("password field is not a string", TAG); return false; }

            return true;
        }
    }

    static void _disable() 
    {
        logger.debug("Disabling Custom MQTT due to persistent failures...", TAG);

        _customMqttConfiguration.enabled = false;
        setConfiguration(_customMqttConfiguration); // This will save the configuration and stop the task

        snprintf(_status, sizeof(_status), "Disabled due to persistent failures");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Custom MQTT disabled", TAG);
    }

    static bool _connectMqtt()
    {
        logger.debug("Attempt to connect to Custom MQTT (attempt %d)...", TAG, _currentMqttConnectionAttempt + 1);

        // If the clientid is empty, use the default one
        char clientId[NAME_BUFFER_SIZE];
        snprintf(clientId, sizeof(clientId), "%s", _customMqttConfiguration.clientid);
        if (strlen(_customMqttConfiguration.clientid) == 0) {
            logger.warning("Client ID is empty, using device client ID", TAG);
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
            logger.info("Connected to Custom MQTT | Server: %s, Port: %lu, Client ID: %s, Topic: %s", TAG,
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
            const char* _reason = getMqttStateReason(currentState);

            // Check for specific errors that warrant disabling Custom MQTT
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                 logger.error("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling Custom MQTT.",
                    TAG,
                    currentState);
                _disable();
                return false;
            }

            if (_currentMqttConnectionAttempt >= MQTT_CUSTOM_MAX_RECONNECT_ATTEMPTS) {
                logger.error("Custom MQTT connection failed after %lu attempts. Disabling Custom MQTT.", TAG, _currentMqttConnectionAttempt);
                _disable();
                return false;
            }

            // Calculate next attempt time using exponential backoff
            uint64_t _backoffDelay = calculateExponentialBackoff(_currentMqttConnectionAttempt, MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL, MQTT_CUSTOM_MAX_RECONNECT_INTERVAL, MQTT_CUSTOM_RECONNECT_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;

            snprintf(_status, sizeof(_status), "Connection failed: %s (Attempt %lu). Retrying in %llu ms", _reason, _currentMqttConnectionAttempt, _backoffDelay);
            _statusTimestampUnix = CustomTime::getUnixTime();

            logger.warning("Failed to connect to Custom MQTT (attempt %lu). Reason: %s (%ld). Retrying in %llu ms", TAG, 
                           _currentMqttConnectionAttempt,
                           _reason,
                           currentState,
                           _nextMqttConnectionAttemptMillis - millis64());

            return false;
        }
    }

    static void _publishMeter()
    {
        logger.debug("Publishing meter data to Custom MQTT...", TAG);

        JsonDocument jsonDocument;
        Ade7953::fullMeterValuesToJson(jsonDocument);

        // Validate that we have actual data before serializing (since the JSON serialization allows for empty objects)
        if (jsonDocument.isNull() || jsonDocument.size() == 0) {
            logger.debug("No meter data available for publishing to Custom MQTT", TAG);
            return;
        }
        
        char meterMessage[MQTT_CUSTOM_PAYLOAD_LIMIT];
        if (!safeSerializeJson(jsonDocument, meterMessage, sizeof(meterMessage))) {
            logger.warning("Failed to serialize meter data to JSON", TAG);
            return;
        }

        if (_publishMessage(_customMqttConfiguration.topic, meterMessage)) logger.debug("Meter data published to Custom MQTT", TAG);
        else logger.warning("Failed to publish meter data to Custom MQTT", TAG);
    }

    static bool _publishMessage(const char *topic, const char *message)
    {

        statistics.customMqttMessagesPublishedError++; // I know it is weird (but we avoid repetition), but if later we reach the end, we decrement it
        _currentFailedMessagePublishAttempt++;

        if (_currentFailedMessagePublishAttempt > MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS) {
            logger.error("Too many failed message publish attempts (%d). Disabling Custom MQTT.", TAG, _currentFailedMessagePublishAttempt);
            _disable();
            return false;
        }

        if (topic == nullptr || message == nullptr)
        {
            logger.warning("Null pointer or message passed, meaning Custom MQTT not initialized yet", TAG);
            return false;
        }

        if (!CustomWifi::isFullyConnected())
        {
            logger.warning("Custom MQTT not connected to WiFi. Skipping publishing on %s", TAG, topic);
            return false;
        }

        if (!_customClientMqtt.connected())
        {
            logger.warning("Custom MQTT client not connected. State: %s. Skipping publishing on %s", TAG, getMqttStateReason(_customClientMqtt.state()), topic);
            return false;
        }

        if (strlen(topic) == 0 || strlen(message) == 0)
        {
            logger.warning("Empty topic or message. Skipping publishing.", TAG);
            return false;
        }

        if (!_customClientMqtt.publish(topic, message))
        {
            logger.warning("Failed to publish message. Custom MQTT client state: %s", TAG, getMqttStateReason(_customClientMqtt.state()));
            return false;
        }
        
        statistics.customMqttMessagesPublishedError--; // Because we increased before
        _currentFailedMessagePublishAttempt = 0; // Reset failed message publish attempt counter on successful publish
        statistics.customMqttMessagesPublished++;
        logger.debug("Message published to %s (%d bytes)", TAG, topic, strlen(message));
        return true;
    }
}