#include "custommqtt.h"

static const char *TAG = "custommqtt";

namespace CustomMqtt
{
    // Static variables
    static WiFiClient _net;
    static PubSubClient _customClientMqtt(_net);
    static CustomMqttConfiguration _customMqttConfiguration;

    // State variables
    static bool _isSetupDone = false;
    static uint64_t _mqttConnectionAttempt = 0;
    static uint64_t _nextMqttConnectionAttemptMillis = 0;
    
    // Runtime connection status - kept in memory only, not saved to preferences
    static char _status[STATUS_BUFFER_SIZE];
    static uint64_t _statusTimestampUnix;

    // Task variables
    static TaskHandle_t _customMqttTaskHandle = nullptr;
    static bool _taskShouldRun = false;

    // Private function declarations
    static void _setDefaultConfiguration();
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    
    static void _disable();
    
    static bool _connectMqtt();
    
    static void _publishMeter();
    static bool _publishMessage(const char *topic, const char *message);
    
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument);
    static bool _validateJsonConfigurationPartial(JsonDocument &jsonDocument);

    static void _customMqttTask(void* parameter);
    static void _startTask();
    static void _stopTask();

    void begin()
    {
        logger.debug("Setting up custom MQTT...", TAG);
        
        _setConfigurationFromPreferences();
        _customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
        
        if (_customMqttConfiguration.enabled) {
            _startTask();
        } else {
            logger.debug("Custom MQTT not enabled", TAG);
        }
        
        _isSetupDone = true;
        logger.debug("Custom MQTT setup complete", TAG);
    }

    void stop()
    {
        logger.debug("Stopping custom MQTT...", TAG);
        _stopTask();
        _isSetupDone = false;
        logger.info("Custom MQTT stopped", TAG);
    }

    static void _setDefaultConfiguration()
    {
        logger.debug("Setting default custom MQTT configuration...", TAG);

        // Create default configuration struct
        CustomMqttConfiguration defaultConfig;

        setConfiguration(defaultConfig);

        logger.debug("Default custom MQTT configuration set", TAG);
    }

    bool setConfiguration(CustomMqttConfiguration &config)
    {
        logger.debug("Setting custom MQTT configuration...", TAG);

        // Stop current task if running
        _stopTask();

        // Copy the configuration
        _customMqttConfiguration = config;
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        _nextMqttConnectionAttemptMillis = millis64();
        _mqttConnectionAttempt = 0;

        _saveConfigurationToPreferences();

        // Start task if enabled
        if (_customMqttConfiguration.enabled) {
            _startTask();
        }

        logger.debug("Custom MQTT configuration set", TAG);

        return true;
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument)
    {
        logger.debug("Setting custom MQTT configuration from JSON...", TAG);

        if (!_validateJsonConfigurationPartial(jsonDocument))
        {
            logger.error("Invalid custom MQTT configuration JSON", TAG);
            return false;
        }

        // Get current configuration as base
        CustomMqttConfiguration config = _customMqttConfiguration;
        
        if (!configurationFromJsonPartial(jsonDocument, config))
        {
            logger.error("Failed to convert JSON to configuration struct", TAG);
            return false;
        }

        return setConfiguration(config);
    }

    static void _setConfigurationFromPreferences()
    {
        logger.debug("Setting custom MQTT configuration from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, true)) {
            logger.error("Failed to open Preferences namespace for custom MQTT. Using default configuration", TAG);
            _setDefaultConfiguration();
            return;
        }

        CustomMqttConfiguration config;
        config.enabled = preferences.getBool(CUSTOM_MQTT_ENABLED_KEY, DEFAULT_IS_CUSTOM_MQTT_ENABLED);
        snprintf(config.server, sizeof(config.server), "%s", preferences.getString(CUSTOM_MQTT_SERVER_KEY, MQTT_CUSTOM_SERVER_DEFAULT).c_str());
        config.port = preferences.getUInt(CUSTOM_MQTT_PORT_KEY, MQTT_CUSTOM_PORT_DEFAULT);
        snprintf(config.clientid, sizeof(config.clientid), "%s", preferences.getString(CUSTOM_MQTT_CLIENT_ID_KEY, MQTT_CUSTOM_CLIENTID_DEFAULT).c_str());
        snprintf(config.topic, sizeof(config.topic), "%s", preferences.getString(CUSTOM_MQTT_TOPIC_KEY, MQTT_CUSTOM_TOPIC_DEFAULT).c_str());
        config.frequency = preferences.getUInt(CUSTOM_MQTT_FREQUENCY_KEY, MQTT_CUSTOM_FREQUENCY_DEFAULT);
        config.useCredentials = preferences.getBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT);
        snprintf(config.username, sizeof(config.username), "%s", preferences.getString(CUSTOM_MQTT_USERNAME_KEY, MQTT_CUSTOM_USERNAME_DEFAULT).c_str());
        snprintf(config.password, sizeof(config.password), "%s", preferences.getString(CUSTOM_MQTT_PASSWORD_KEY, MQTT_CUSTOM_PASSWORD_DEFAULT).c_str());
        
        snprintf(_status, sizeof(_status), "Configuration loaded from Preferences");
        _statusTimestampUnix = CustomTime::getUnixTime();

        preferences.end();

        _customMqttConfiguration = config;

        logger.debug("Successfully set custom MQTT configuration from Preferences", TAG);
    }

    static void _saveConfigurationToPreferences()
    {
        logger.debug("Saving custom MQTT configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) {
            logger.error("Failed to open Preferences namespace for custom MQTT", TAG);
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, _customMqttConfiguration.enabled);
        preferences.putString(CUSTOM_MQTT_SERVER_KEY, _customMqttConfiguration.server);
        preferences.putUInt(CUSTOM_MQTT_PORT_KEY, _customMqttConfiguration.port);
        preferences.putString(CUSTOM_MQTT_CLIENT_ID_KEY, _customMqttConfiguration.clientid);
        preferences.putString(CUSTOM_MQTT_TOPIC_KEY, _customMqttConfiguration.topic);
        preferences.putUInt(CUSTOM_MQTT_FREQUENCY_KEY, _customMqttConfiguration.frequency);
        preferences.putBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, _customMqttConfiguration.useCredentials);
        preferences.putString(CUSTOM_MQTT_USERNAME_KEY, _customMqttConfiguration.username);
        preferences.putString(CUSTOM_MQTT_PASSWORD_KEY, _customMqttConfiguration.password);

        preferences.end();

        logger.debug("Successfully saved custom MQTT configuration to Preferences", TAG);
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
        if (!jsonDocument["port"].is<uint32_t>()) { logger.warning("port field is not an integer", TAG); return false; }
        if (!jsonDocument["clientid"].is<const char*>()) { logger.warning("clientid field is not a string", TAG); return false; }
        if (!jsonDocument["topic"].is<const char*>()) { logger.warning("topic field is not a string", TAG); return false; }
        if (!jsonDocument["frequency"].is<uint32_t>()) { logger.warning("frequency field is not an integer", TAG); return false; }
        if (!jsonDocument["useCredentials"].is<bool>()) { logger.warning("useCredentials field is not a boolean", TAG); return false; }
        if (!jsonDocument["username"].is<const char*>()) { logger.warning("username field is not a string", TAG); return false; }
        if (!jsonDocument["password"].is<const char*>()) { logger.warning("password field is not a string", TAG); return false; }

        return true;
    }

    static bool _validateJsonConfigurationPartial(JsonDocument &jsonDocument)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
        {
            logger.warning("Invalid or empty JSON document", TAG);
            return false;
        }

        // If even only one field is present, we return true
        if (jsonDocument["enabled"].is<bool>()) {return true;}        
        if (jsonDocument["server"].is<const char*>()) {return true;}        
        if (jsonDocument["port"].is<uint32_t>()) {return true;}        
        if (jsonDocument["clientid"].is<const char*>()) {return true;}        
        if (jsonDocument["topic"].is<const char*>()) {return true;}        
        if (jsonDocument["frequency"].is<uint32_t>()) {return true;}        
        if (jsonDocument["useCredentials"].is<bool>()) {return true;}        
        if (jsonDocument["username"].is<const char*>()) {return true;}        
        if (jsonDocument["password"].is<const char*>()) {return true;}

        // If we did not return true by now, it means no valid fields were found
        logger.warning("No valid fields found in JSON document", TAG);
        return false;
    }

    static void _disable() {
        logger.debug("Disabling custom MQTT...", TAG);

        // Stop the task since it can't reconnect anyway
        _stopTask();

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) {
            logger.error("Failed to open Preferences namespace for custom MQTT", TAG);
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, false);
        preferences.end();

        // Update the configuration object
        _customMqttConfiguration.enabled = false;
        
        // Reset connection state but keep module initialized
        _mqttConnectionAttempt = 0;
        _nextMqttConnectionAttemptMillis = 0;

        logger.debug("Custom MQTT disabled", TAG);
    }

    static bool _connectMqtt()
    {
        logger.debug("Attempt to connect to custom MQTT (attempt %d)...", TAG, _mqttConnectionAttempt + 1);

        bool res;

        // If the clientid is empty, use the default one
        char clientId[NAME_BUFFER_SIZE];
        snprintf(clientId, sizeof(clientId), "%s", _customMqttConfiguration.clientid);
        if (strlen(_customMqttConfiguration.clientid) == 0) {
            logger.warning("Client ID is empty, using device client ID", TAG);
            snprintf(clientId, sizeof(clientId), "%s", DEVICE_ID);
        }

        if (_customMqttConfiguration.useCredentials) {
            res = _customClientMqtt.connect(
                clientId,
                _customMqttConfiguration.username,
                _customMqttConfiguration.password);
        } else {
            res = _customClientMqtt.connect(clientId);
        }

        if (res) {
            logger.info("Connected to custom MQTT | Server: %s, Port: %d, Client ID: %s, Topic: %s", TAG,
                        _customMqttConfiguration.server,
                        _customMqttConfiguration.port,
                        clientId,
                        _customMqttConfiguration.topic);

            _mqttConnectionAttempt = 0;
            snprintf(_status, sizeof(_status), "Connected");
            _statusTimestampUnix = CustomTime::getUnixTime();

            return true;
        } else {
            int32_t currentState = _customClientMqtt.state();
            const char* _reason = getMqttStateReason(currentState);

            logger.warning(
                "Failed to connect to custom MQTT (attempt %d). Reason: %s (%d). Retrying...",
                TAG,
                _mqttConnectionAttempt + 1,
                _reason,
                currentState);

            _mqttConnectionAttempt++;

            snprintf(_status, sizeof(_status), 
                     "%s (Attempt %d)", _reason, _mqttConnectionAttempt);
            
            _statusTimestampUnix = CustomTime::getUnixTime();

            // Check for specific errors that warrant disabling custom MQTT
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                 logger.error("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling custom MQTT.",
                    TAG,
                    currentState);
                _disable();
                _nextMqttConnectionAttemptMillis = UINT32_MAX;
                return false;
            }

            // Calculate next attempt time using exponential backoff
            uint64_t _backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL, MQTT_CUSTOM_MAX_RECONNECT_INTERVAL, MQTT_CUSTOM_RECONNECT_MULTIPLIER);

            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;

            logger.info("Next custom MQTT connection attempt in %llu ms", TAG, _backoffDelay);

            return false;
        }
    }

    static void _publishMeter()
    {
        logger.debug("Publishing meter data to custom MQTT...", TAG);

        JsonDocument jsonDocument;
        Ade7953::fullMeterValuesToJson(jsonDocument);

        // Validate that we have actual data before serializing (since the JSON serialization allows for empty objects)
        if (jsonDocument.isNull() || jsonDocument.size() == 0) {
            logger.debug("No meter data available for publishing to custom MQTT", TAG);
            return;
        }
        
        char meterMessage[MQTT_CUSTOM_PAYLOAD_LIMIT];
        if (!safeSerializeJson(jsonDocument, meterMessage, sizeof(meterMessage))) {
            logger.warning("Failed to serialize meter data to JSON", TAG);
            return;
        }

        if (_publishMessage(_customMqttConfiguration.topic, meterMessage)) logger.debug("Meter data published to custom MQTT", TAG);
    }

    static bool _publishMessage(const char *topic, const char *message)
    {
        logger.verbose(
            "Publishing message to topic %s | %s",
            TAG,
            topic,
            message);

        if (topic == nullptr || message == nullptr)
        {
            logger.warning("Null pointer or message passed, meaning custom MQTT not initialized yet", TAG);
            statistics.customMqttMessagesPublishedError++;
            return false;
        }

        if (!_customClientMqtt.connected())
        {
            logger.warning("Custom MQTT client not connected. State: %s. Skipping publishing on %s", TAG, getMqttStateReason(_customClientMqtt.state()), topic);
            statistics.customMqttMessagesPublishedError++;
            return false;
        }

        if (strlen(topic) == 0 || strlen(message) == 0)
        {
            logger.warning("Empty topic or message. Skipping publishing.", TAG);
            statistics.customMqttMessagesPublishedError++;
            return false;
        }

        if (!_customClientMqtt.publish(topic, message))
        {
            logger.warning("Failed to publish message. Custom MQTT client state: %s", TAG, getMqttStateReason(_customClientMqtt.state()));
            statistics.customMqttMessagesPublishedError++;
            return false;
        }

        statistics.customMqttMessagesPublished++;
        logger.debug("Message published (%d bytes)", TAG, strlen(message));
        return true;
    }

    void getConfiguration(CustomMqttConfiguration &config)
    {
        logger.debug("Getting custom MQTT configuration...", TAG);

        // Ensure configuration is loaded
        if (!_isSetupDone)
        {
            begin();
        }

        // Copy the current configuration
        config = _customMqttConfiguration;
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

    bool configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument)
    {
        logger.debug("Converting custom MQTT configuration to JSON...", TAG);

        jsonDocument["enabled"] = config.enabled;
        jsonDocument["server"] = config.server;
        jsonDocument["port"] = config.port;
        jsonDocument["clientid"] = config.clientid;
        jsonDocument["topic"] = config.topic;
        jsonDocument["frequency"] = config.frequency;
        jsonDocument["useCredentials"] = config.useCredentials;
        jsonDocument["username"] = config.username;
        jsonDocument["password"] = config.password;

        logger.debug("Successfully converted configuration to JSON", TAG);
        return true;
    }

    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config)
    {
        logger.debug("Converting JSON to custom MQTT configuration...", TAG);

        if (!_validateJsonConfiguration(jsonDocument))
        {
            logger.error("Invalid JSON configuration", TAG);
            return false;
        }

        config.enabled = jsonDocument["enabled"].as<bool>();
        snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
        config.port = jsonDocument["port"].as<uint32_t>();
        snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
        snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
        config.frequency = jsonDocument["frequency"].as<uint32_t>();
        config.useCredentials = jsonDocument["useCredentials"].as<bool>();
        snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
        snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Successfully converted JSON to configuration", TAG);
        return true;
    }

    bool configurationFromJsonPartial(JsonDocument &jsonDocument, CustomMqttConfiguration &config)
    {
        logger.debug("Converting JSON to custom MQTT configuration (partial update)...", TAG);

        if (!_validateJsonConfigurationPartial(jsonDocument))
        {
            logger.error("Invalid JSON configuration", TAG);
            return false;
        }

        // Update only fields that are present in JSON
        if (jsonDocument["enabled"].is<bool>()) {
            config.enabled = jsonDocument["enabled"].as<bool>();
        }
        if (jsonDocument["server"].is<const char*>()) {
            snprintf(config.server, sizeof(config.server), "%s", jsonDocument["server"].as<const char*>());
        }
        if (jsonDocument["port"].is<uint32_t>()) {
            config.port = jsonDocument["port"].as<uint32_t>();
        }
        if (jsonDocument["clientid"].is<const char*>()) {
            snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
        }
        if (jsonDocument["topic"].is<const char*>()) {
            snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
        }
        if (jsonDocument["frequency"].is<uint32_t>()) {
            config.frequency = jsonDocument["frequency"].as<uint32_t>();
        }
        if (jsonDocument["useCredentials"].is<bool>()) {
            config.useCredentials = jsonDocument["useCredentials"].as<bool>();
        }
        if (jsonDocument["username"].is<const char*>()) {
            snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
        }
        if (jsonDocument["password"].is<const char*>()) {
            snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        }
        
        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        logger.debug("Successfully converted JSON to configuration", TAG);
        return true;
    }

    static void _customMqttTask(void* parameter)
    {
        logger.debug("Custom MQTT task started", TAG);
        
        _taskShouldRun = true;
        uint64_t lastPublishTime = 0;

        while (_taskShouldRun) {
            if (CustomWifi::isFullyConnected()) { // We are connected
                if (_customMqttConfiguration.enabled) { // We have the custom MQTT enabled
                    if (_customClientMqtt.connected()) { // We are connected to MQTT
                        if (_mqttConnectionAttempt > 0) { // If we were having problems, reset the attempt counter
                            logger.debug("Custom MQTT reconnected successfully after %d attempts.", TAG, _mqttConnectionAttempt);
                            _mqttConnectionAttempt = 0;
                        }

                        _customClientMqtt.loop();

                        uint64_t currentTime = millis64();
                        if ((currentTime - lastPublishTime) >= (_customMqttConfiguration.frequency * 1000)) {
                            _publishMeter();
                            lastPublishTime = currentTime;
                        }
                    } else {
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

        logger.debug("Custom MQTT task stopping", TAG);
        _customMqttTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

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
            logger.error("Failed to create custom MQTT task", TAG);
            _customMqttTaskHandle = nullptr;
        }
    }

    static void _stopTask() { 
        stopTaskGracefully(&_customMqttTaskHandle, "Custom MQTT task");
        _customClientMqtt.disconnect();
    }
}