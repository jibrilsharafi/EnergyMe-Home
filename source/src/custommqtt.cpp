#include "custommqtt.h"

extern Ade7953 ade7953; // TODO: remove this once ade7953 becomes static

static const char *TAG = "custommqtt";

namespace CustomMqtt
{
    // Static variables
    static WiFiClient _net;
    static PubSubClient _customClientMqtt(_net);
    static CustomMqttConfiguration _customMqttConfiguration;

    // State variables
    static bool _isSetupDone = false;
    static unsigned long _mqttConnectionAttempt = 0;
    static unsigned long _nextMqttConnectionAttemptMillis = 0;
    
    // Task variables
    static TaskHandle_t _customMqttTaskHandle = nullptr;
    static bool _taskShouldStop = false;

    // Private function declarations
    static void _setDefaultConfiguration();
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    static void _disable();
    static bool _connectMqtt();
    static void _publishMeter();
    static bool _publishMessage(const char *topic, const char *message);
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument);
    static void _customMqttTask(void* parameter);
    static void _startTask();
    static void _stopTask();

    void begin()
    {
        logger.debug("Setting up custom MQTT...", TAG);
        
        _setConfigurationFromPreferences();

        _customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
        _customClientMqtt.setServer(_customMqttConfiguration.server, _customMqttConfiguration.port);

        // Start the task if configuration is enabled
        if (_customMqttConfiguration.enabled) {
            _startTask();
        } else {
            logger.debug("Custom MQTT not enabled", TAG);
        }

        logger.debug("Custom MQTT setup complete", TAG);

        // Mark setup as done regardless of enabled state - configuration has been loaded
        _isSetupDone = true;
    }

    void stop()
    {
        logger.debug("Stopping custom MQTT...", TAG);
        _stopTask();
        _customClientMqtt.disconnect();
        _isSetupDone = false;
        logger.debug("Custom MQTT stopped", TAG);
    }

    static void _setDefaultConfiguration()
    {
        logger.debug("Setting default custom MQTT configuration...", TAG);

        // Create default configuration struct
        CustomMqttConfiguration defaultConfig;
        // Constructor already sets the defaults

        setConfiguration(defaultConfig);

        logger.debug("Default custom MQTT configuration set", TAG);
    }

    bool setConfiguration(CustomMqttConfiguration &config)
    {
        logger.debug("Setting custom MQTT configuration...", TAG);

        // Stop current task if running
        _stopTask();
        _customClientMqtt.disconnect();

        // Copy the configuration
        _customMqttConfiguration = config;
        
        // Update status and timestamp
        snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), "Configuration updated");
        char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
        snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

        _nextMqttConnectionAttemptMillis = millis64(); // Try connecting immediately
        _mqttConnectionAttempt = 0;

        _customClientMqtt.setServer(_customMqttConfiguration.server, _customMqttConfiguration.port);
        
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

        if (!_validateJsonConfiguration(jsonDocument))
        {
            logger.error("Invalid custom MQTT configuration JSON", TAG);
            return false;
        }

        CustomMqttConfiguration config;
        if (!configurationFromJson(jsonDocument, config))
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
        config.enabled = preferences.getBool(CUSTOM_MQTT_ENABLED_KEY, false);
        snprintf(config.server, sizeof(config.server), "%s", preferences.getString(CUSTOM_MQTT_SERVER_KEY, "").c_str());
        config.port = preferences.getUInt(CUSTOM_MQTT_PORT_KEY, 1883);
        snprintf(config.clientid, sizeof(config.clientid), "%s", preferences.getString(CUSTOM_MQTT_CLIENT_ID_KEY, "energyme").c_str());
        snprintf(config.topic, sizeof(config.topic), "%s", preferences.getString(CUSTOM_MQTT_TOPIC_KEY, "energyme/meter").c_str());
        config.frequency = preferences.getUInt(CUSTOM_MQTT_FREQUENCY_KEY, 10);
        config.useCredentials = preferences.getBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, false);
        snprintf(config.username, sizeof(config.username), "%s", preferences.getString(CUSTOM_MQTT_USERNAME_KEY, "").c_str());
        snprintf(config.password, sizeof(config.password), "%s", preferences.getString(CUSTOM_MQTT_PASSWORD_KEY, "").c_str());
        snprintf(config.lastConnectionStatus, sizeof(config.lastConnectionStatus), "Loaded from Preferences");
        
        char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
        snprintf(config.lastConnectionAttemptTimestamp, sizeof(config.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

        preferences.end();

        // Use the main setConfiguration function - but don't restart task during initial setup
        bool wasSetupDone = _isSetupDone;
        _customMqttConfiguration = config;
        if (wasSetupDone) {
            // Only call full setConfiguration if already initialized (to restart task)
            setConfiguration(config);
        }

        logger.debug("Successfully set custom MQTT configuration from Preferences", TAG);
    }

    static void _saveConfigurationToPreferences()
    {
        logger.debug("Saving custom MQTT configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) { // false = read-write
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
        if (!jsonDocument["port"].is<int>()) { logger.warning("port field is not an integer", TAG); return false; }
        if (!jsonDocument["clientid"].is<const char*>()) { logger.warning("clientid field is not a string", TAG); return false; }
        if (!jsonDocument["topic"].is<const char*>()) { logger.warning("topic field is not a string", TAG); return false; }
        if (!jsonDocument["frequency"].is<int>()) { logger.warning("frequency field is not an integer", TAG); return false; }
        if (!jsonDocument["useCredentials"].is<bool>()) { logger.warning("useCredentials field is not a boolean", TAG); return false; }
        if (!jsonDocument["username"].is<const char*>()) { logger.warning("username field is not a string", TAG); return false; }
        if (!jsonDocument["password"].is<const char*>()) { logger.warning("password field is not a string", TAG); return false; }

        return true;
    }

    static void _disable() {
        logger.debug("Disabling custom MQTT...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) { // false = read-write
            logger.error("Failed to open Preferences namespace for custom MQTT", TAG);
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, false);
        preferences.end();

        // Update the configuration object
        _customMqttConfiguration.enabled = false;
        
        // Don't reset _isSetupDone - configuration is still loaded, just disabled
        _mqttConnectionAttempt = 0;

        logger.debug("Custom MQTT disabled", TAG);
    }

    static bool _connectMqtt()
    {
        logger.debug("Attempt to connect to custom MQTT (attempt %d)...", TAG, _mqttConnectionAttempt + 1);

        bool res;

        if (_customMqttConfiguration.useCredentials) {
            res = _customClientMqtt.connect(
                _customMqttConfiguration.clientid,
                _customMqttConfiguration.username,
                _customMqttConfiguration.password);
        } else {
            res = _customClientMqtt.connect(_customMqttConfiguration.clientid);
        }

        if (res) {
            logger.info("Connected to custom MQTT", TAG);

            _mqttConnectionAttempt = 0; // Reset attempt counter on success
            snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), "Connected");

            char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
            CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
            snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

            _saveConfigurationToPreferences();

            return true;
        } else {
            int currentState = _customClientMqtt.state();
            const char* _reason = getMqttStateReason(currentState);

            logger.warning(
                "Failed to connect to custom MQTT (attempt %d). Reason: %s (%d). Retrying...",
                TAG,
                _mqttConnectionAttempt + 1,
                _reason,
                currentState);

            _mqttConnectionAttempt++;

            snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), 
                     "%s (Attempt %d)", _reason, _mqttConnectionAttempt);
            
            char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
            CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
            snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

            _saveConfigurationToPreferences();

            // Check for specific errors that warrant disabling custom MQTT
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                 logger.error("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling custom MQTT.",
                    TAG,
                    currentState);
                _disable(); // Disable custom MQTT on auth errors
                _nextMqttConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts until re-enabled
                return false; // Prevent further processing in this cycle
            }

            // Calculate next attempt time using exponential backoff
            unsigned long _backoffDelay = MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL;
            for (int i = 0; i < _mqttConnectionAttempt -1 && _backoffDelay < MQTT_CUSTOM_MAX_RECONNECT_INTERVAL; ++i) {
                 _backoffDelay *= MQTT_CUSTOM_RECONNECT_MULTIPLIER;
            }
            _backoffDelay = min(_backoffDelay, (unsigned long)MQTT_CUSTOM_MAX_RECONNECT_INTERVAL);

            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;

            logger.info("Next custom MQTT connection attempt in %lu ms", TAG, _backoffDelay);

            return false;
        }
    }

    static void _publishMeter()
    {
        logger.debug("Publishing meter data to custom MQTT...", TAG);

        JsonDocument jsonDocument;
        ade7953.fullMeterValuesToJson(jsonDocument);
        
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
        logger.debug("Message published: %s", TAG, message);
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
        config.port = jsonDocument["port"].as<int>();
        snprintf(config.clientid, sizeof(config.clientid), "%s", jsonDocument["clientid"].as<const char*>());
        snprintf(config.topic, sizeof(config.topic), "%s", jsonDocument["topic"].as<const char*>());
        config.frequency = jsonDocument["frequency"].as<int>();
        config.useCredentials = jsonDocument["useCredentials"].as<bool>();
        snprintf(config.username, sizeof(config.username), "%s", jsonDocument["username"].as<const char*>());
        snprintf(config.password, sizeof(config.password), "%s", jsonDocument["password"].as<const char*>());
        snprintf(config.lastConnectionStatus, sizeof(config.lastConnectionStatus), "Configuration updated");
        
        char timestampBuffer[TIMESTAMP_STRING_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
        snprintf(config.lastConnectionAttemptTimestamp, sizeof(config.lastConnectionAttemptTimestamp), "%s", timestampBuffer);

        logger.debug("Successfully converted JSON to configuration", TAG);
        return true;
    }

    static void _customMqttTask(void* parameter)
    {
        logger.debug("Custom MQTT task started", TAG);
        
        unsigned long lastPublishTime = 0;
        TickType_t publishInterval = pdMS_TO_TICKS(_customMqttConfiguration.frequency * 1000);

        while (!_taskShouldStop) {
            // Check if WiFi is connected
            if (!CustomWifi::isFullyConnected()) {
                vTaskDelay(pdMS_TO_TICKS(CUSTOM_MQTT_TASK_CHECK_INTERVAL));
                continue;
            }

            // Check if MQTT is enabled
            if (!_customMqttConfiguration.enabled) {
                if (_customClientMqtt.connected()) {
                    logger.info("Custom MQTT disabled, disconnecting...", TAG);
                    _customClientMqtt.disconnect();
                }
                vTaskDelay(pdMS_TO_TICKS(CUSTOM_MQTT_TASK_CHECK_INTERVAL));
                continue;
            }

            // Handle connection
            if (!_customClientMqtt.connected()) {
                if (millis64() >= _nextMqttConnectionAttemptMillis) {
                    logger.debug("Custom MQTT client not connected. Attempting to reconnect...", TAG);
                    _connectMqtt();
                }
            } else {
                // Reset connection attempts counter on successful connection
                if (_mqttConnectionAttempt > 0) {
                    logger.debug("Custom MQTT reconnected successfully after %d attempts.", TAG, _mqttConnectionAttempt);
                    _mqttConnectionAttempt = 0;
                }

                // Handle MQTT loop
                _customClientMqtt.loop();

                // Handle publishing
                unsigned long currentTime = millis64();
                if ((currentTime - lastPublishTime) >= (_customMqttConfiguration.frequency * 1000)) {
                    _publishMeter();
                    lastPublishTime = currentTime;
                }
            }

            // Update publish interval if configuration changed
            TickType_t newPublishInterval = pdMS_TO_TICKS(_customMqttConfiguration.frequency * 1000);
            if (newPublishInterval != publishInterval) {
                publishInterval = newPublishInterval;
                logger.debug("Updated publish interval to %d seconds", TAG, _customMqttConfiguration.frequency);
            }

            vTaskDelay(pdMS_TO_TICKS(CUSTOM_MQTT_TASK_CHECK_INTERVAL));
        }

        logger.debug("Custom MQTT task ending", TAG);
        _customMqttTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static void _startTask()
    {
        if (_customMqttTaskHandle != nullptr) {
            logger.warning("Custom MQTT task already running", TAG);
            return;
        }

        _taskShouldStop = false;
        
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
        } else {
            logger.debug("Custom MQTT task started successfully", TAG);
        }
    }

    static void _stopTask()
    {
        if (_customMqttTaskHandle == nullptr) {
            return;
        }

        logger.debug("Stopping custom MQTT task...", TAG);
        _taskShouldStop = true;

        // Wait for task to finish (with timeout)
        for (int i = 0; i < 50 && _customMqttTaskHandle != nullptr; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Force delete if still running
        if (_customMqttTaskHandle != nullptr) {
            logger.warning("Force deleting custom MQTT task", TAG);
            vTaskDelete(_customMqttTaskHandle);
            _customMqttTaskHandle = nullptr;
        }

        _taskShouldStop = false;
        logger.debug("Custom MQTT task stopped", TAG);
    }
}