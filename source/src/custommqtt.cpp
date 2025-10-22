// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "custommqtt.h"

namespace CustomMqtt
{
    // Static variables
    // ==========================================================
    
    // MQTT client objects
    static WiFiClient _net;
    static PubSubClient _mqttClient(_net);
    
    // State variables
    static bool _isSetupDone = false;
    static uint32_t _currentMqttConnectionAttempt = 0;
    static uint64_t _nextMqttConnectionAttemptMillis = 0;
    static uint32_t _currentFailedMessagePublishAttempt = 0;

    // Custom MQTT configuration
    static CustomMqttConfiguration _configuration;
    
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
    static void _saveConfigurationToPreferences(const CustomMqttConfiguration &config);
    
    // MQTT operations
    static bool _connectMqtt(const CustomMqttConfiguration &config);
    static void _publishMeter(const char* topic);
    static bool _publishMeterStreaming(JsonDocument &jsonDocument, const char* topic);
    
    // JSON validation
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);
    
    // Task management
    static void _customMqttTask(void* parameter);
    static void _startTask();
    static void _stopTask();
    static void _disable(const char* reason); // Needed for halting the functionality if we have too many failure

    // Utils
    const char* _getMqttStateReason(int32_t state);

    // Public API functions
    // =========================================================

    void begin()
    {
        LOG_DEBUG("Setting up Custom MQTT client...");
        
        if (!createMutexIfNeeded(&_configMutex)) return;
        
        _isSetupDone = true; // Must set before since we have checks on the setup later        
        
        _mqttClient.setBufferSize(STREAM_UTILS_MQTT_PACKET_SIZE); // Called only here since it won't be modified never later
        _setConfigurationFromPreferences(); // Task will be started from setConfiguration called here

        LOG_DEBUG("Custom MQTT client setup complete");
    }

    void stop()
    {
        LOG_DEBUG("Stopping Custom MQTT client...");
        _stopTask();
        
        deleteMutex(&_configMutex);
        
        _isSetupDone = false;
        LOG_INFO("Custom MQTT client stopped");
    }

    bool getConfiguration(CustomMqttConfiguration &config)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return false;
        }
        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for getConfiguration");
            return false;
        }

        config = _configuration;
        
        releaseMutex(&_configMutex);
        return true;
    }

    bool setConfiguration(const CustomMqttConfiguration &config)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return false;
        }

        _stopTask();

        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for setConfiguration");
            return false;
        }
        _configuration = config; // Copy to the static configuration variable
        releaseMutex(&_configMutex);

        snprintf(_status, sizeof(_status), "Configuration updated");
        _statusTimestampUnix = CustomTime::getUnixTime();

        // Reset counter to start fresh
        _nextMqttConnectionAttemptMillis = 0; // Immediately attempt to connect
        _currentMqttConnectionAttempt = 0;
        _currentFailedMessagePublishAttempt = 0;

        _saveConfigurationToPreferences(config);

        _startTask();

        LOG_DEBUG("Custom MQTT configuration set");
        return true;
    }

    bool resetConfiguration() 
    {
        LOG_DEBUG("Resetting Custom MQTT configuration to default");

        CustomMqttConfiguration defaultConfig;
        if (!setConfiguration(defaultConfig)) {
            LOG_ERROR("Failed to reset Custom MQTT configuration");
            return false;
        }

        LOG_INFO("Custom MQTT configuration reset to default");
        return true;
    }

    bool getConfigurationAsJson(JsonDocument &jsonDocument) 
    {
        CustomMqttConfiguration config;
        if (!getConfiguration(config)) return false;
        
        configurationToJson(config, jsonDocument);
        return true;
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        CustomMqttConfiguration config;
        getConfiguration(config);

        if (!configurationFromJson(jsonDocument, config, partial)) {
            LOG_ERROR("Failed to set configuration from JSON");
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument)
    {
        jsonDocument["enabled"] = config.enabled;
        jsonDocument["server"] = JsonString(config.server); // Ensure it is not a dangling pointer
        jsonDocument["port"] = config.port;
        jsonDocument["clientid"] = JsonString(config.clientid); // Ensure it is not a dangling pointer
        jsonDocument["topic"] = JsonString(config.topic); // Ensure it is not a dangling pointer
        jsonDocument["frequency"] = config.frequencySeconds;
        jsonDocument["useCredentials"] = config.useCredentials;
        jsonDocument["username"] = JsonString(config.username); // Ensure it is not a dangling pointer
        jsonDocument["password"] = JsonString(config.password); // Ensure it is not a dangling pointer

        LOG_DEBUG("Successfully converted configuration to JSON");
    }

    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config, bool partial)
    {
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

    bool getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize)
    {
        if (!_isSetupDone) {
            LOG_WARNING("CustomMQTT client is not set up yet, returning early");
            return false;
        }

        if (statusBuffer && statusSize > 0) snprintf(statusBuffer, statusSize, "%s", _status);
        if (timestampBuffer && timestampSize > 0) CustomTime::timestampIsoFromUnix(_statusTimestampUnix, timestampBuffer, timestampSize);

        return true;
    }

    // Private function implementations
    // =========================================================

    static void _startTask()
    {
        if (_customMqttTaskHandle != nullptr) {
            LOG_DEBUG("Custom MQTT task is already running");
            return;
        }

        LOG_DEBUG("Starting Custom MQTT task with %d bytes stack in internal RAM (uses NVS)", CUSTOM_MQTT_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _customMqttTask,
            CUSTOM_MQTT_TASK_NAME,
            CUSTOM_MQTT_TASK_STACK_SIZE,
            nullptr,
            CUSTOM_MQTT_TASK_PRIORITY,
            &_customMqttTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create Custom MQTT task");
            _customMqttTaskHandle = nullptr;
        } else {
            LOG_DEBUG("Custom MQTT task created");
        }
    }

    static void _stopTask() { 
        stopTaskGracefully(&_customMqttTaskHandle, "Custom MQTT task"); 
    }

    static void _customMqttTask(void* parameter)
    {
        LOG_DEBUG("Custom MQTT task started");
        
        _taskShouldRun = true;
        uint64_t lastPublishTime = 0;
        CustomMqttConfiguration config;

        while (_taskShouldRun) {
            getConfiguration(config);
            if (config.enabled) { // We have the custom MQTT enabled (atomic operation, no race condition)
                if (CustomWifi::isFullyConnected()) { // We are connected (no need to check if time is synched)
                    if (_mqttClient.connected()) { // We are connected to MQTT
                        if (_currentMqttConnectionAttempt > 0) { // If we were having problems, reset the attempt counter since we are now connected
                            LOG_DEBUG("Custom MQTT reconnected successfully after %d attempts.", _currentMqttConnectionAttempt);
                            _currentMqttConnectionAttempt = 0;
                        }

                        _mqttClient.loop(); // Required by the MQTT library to process incoming messages and maintain connection

                        uint64_t currentTime = millis64();
                        // Check if enough time has passed since last publish
                        if ((currentTime - lastPublishTime) >= (config.frequencySeconds * 1000)) { // (atomic operation, no race condition)
                            _publishMeter(config.topic);
                            lastPublishTime = currentTime;
                        }
                    } else { // We are not connected to MQTT
                        if (millis64() >= _nextMqttConnectionAttemptMillis) { // Enough time has passed since last attempt (in case of failures) to retry connection
                            LOG_DEBUG("Custom MQTT client not connected. Attempting to connect...");
                            _connectMqtt(config);
                        }
                    }
                } else { // We are not connected to WiFi
                    LOG_DEBUG("Device is not connected to WiFi. Waiting for automatic connection...");
                }
            }

            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CUSTOM_MQTT_TASK_CHECK_INTERVAL)) > 0) {
                _taskShouldRun = false;
                break;
            }
        }
        
        _mqttClient.disconnect();
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

    static void _saveConfigurationToPreferences(const CustomMqttConfiguration &config)
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false)) {
            LOG_ERROR("Failed to open Preferences namespace for Custom MQTT");
            return;
        }

        preferences.putBool(CUSTOM_MQTT_ENABLED_KEY, config.enabled);
        preferences.putString(CUSTOM_MQTT_SERVER_KEY, config.server);
        preferences.putUShort(CUSTOM_MQTT_PORT_KEY, config.port);
        preferences.putString(CUSTOM_MQTT_CLIENT_ID_KEY, config.clientid);
        preferences.putString(CUSTOM_MQTT_TOPIC_KEY, config.topic);
        preferences.putUInt(CUSTOM_MQTT_FREQUENCY_KEY, config.frequencySeconds);
        preferences.putBool(CUSTOM_MQTT_USE_CREDENTIALS_KEY, config.useCredentials);
        preferences.putString(CUSTOM_MQTT_USERNAME_KEY, config.username);
        preferences.putString(CUSTOM_MQTT_PASSWORD_KEY, config.password);

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
            if (jsonDocument["enabled"].is<bool>())         return true;        
            if (jsonDocument["server"].is<const char*>())   return true;        
            if (jsonDocument["port"].is<uint16_t>())        return true;        
            if (jsonDocument["clientid"].is<const char*>()) return true;        
            if (jsonDocument["topic"].is<const char*>())    return true;        
            if (jsonDocument["frequency"].is<uint32_t>())   return true;        
            if (jsonDocument["useCredentials"].is<bool>())  return true;        
            if (jsonDocument["username"].is<const char*>()) return true;        
            if (jsonDocument["password"].is<const char*>()) return true;

            LOG_WARNING("No valid fields found in JSON document");
            return false;
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["enabled"].is<bool>())        { LOG_WARNING("enabled field is not a boolean"); return false; }
            if (!jsonDocument["server"].is<const char*>())  { LOG_WARNING("server field is not a string"); return false; }
            if (!jsonDocument["port"].is<uint16_t>())       { LOG_WARNING("port field is not an integer"); return false; }
            if (!jsonDocument["clientid"].is<const char*>()){ LOG_WARNING("clientid field is not a string"); return false; }
            if (!jsonDocument["topic"].is<const char*>())   { LOG_WARNING("topic field is not a string"); return false; }
            if (!jsonDocument["frequency"].is<uint32_t>())  { LOG_WARNING("frequency field is not an integer"); return false; }
            if (!jsonDocument["useCredentials"].is<bool>()) { LOG_WARNING("useCredentials field is not a boolean"); return false; }
            if (!jsonDocument["username"].is<const char*>()){ LOG_WARNING("username field is not a string"); return false; }
            if (!jsonDocument["password"].is<const char*>()){ LOG_WARNING("password field is not a string"); return false; }

            return true;
        }
    }

    static void _disable(const char* reason) {
        if (!acquireMutex(&_configMutex)) return;
        _configuration.enabled = false;
        _saveConfigurationToPreferences(_configuration);
        releaseMutex(&_configMutex);

        snprintf(_status, sizeof(_status), "Disabled due to: %s", reason);
        _statusTimestampUnix = CustomTime::getUnixTime();

        // Not calling _stopTask() here to avoid deadlock

        LOG_WARNING("Custom MQTT disabled due to: %s", reason);
    }

    static bool _connectMqtt(const CustomMqttConfiguration &config)
    {
        LOG_DEBUG("Attempt to connect to Custom MQTT (attempt %d)...", _currentMqttConnectionAttempt + 1);

        // Ensure clean connection state
        if (_mqttClient.connected()) _mqttClient.disconnect();

        // If the clientid is empty, use the default one
        char clientId[NAME_BUFFER_SIZE];
        snprintf(clientId, sizeof(clientId), "%s", config.clientid);
        if (strlen(config.clientid) == 0) {
            LOG_WARNING("Client ID is empty, using device client ID");
            snprintf(clientId, sizeof(clientId), "%s", DEVICE_ID);
        }
        
        _mqttClient.setServer(config.server, config.port);

        bool res;
        if (config.useCredentials) {
            res = _mqttClient.connect(
                clientId,
                config.username,
                config.password);
        } else {
            res = _mqttClient.connect(clientId);
        }

        if (res) {
            LOG_INFO("Connected to Custom MQTT | Server: %s, Port: %u, Client ID: %s, Topic: %s",
                        config.server,
                        config.port,
                        clientId,
                        config.topic);

            _currentMqttConnectionAttempt = 0; // Reset attempt counter on successful connection
            snprintf(_status, sizeof(_status), "Connected");
            _statusTimestampUnix = CustomTime::getUnixTime();

            return true;
        } else {
            _currentMqttConnectionAttempt++;
            int32_t currentState = _mqttClient.state();
            const char* _reason = _getMqttStateReason(currentState);

            // Check for specific errors that warrant disabling Custom MQTT
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                 LOG_ERROR("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling Custom MQTT.",
                    currentState);
                _disable("Authorization/credentials error");
                return false;
            }

            if (_currentMqttConnectionAttempt >= MQTT_CUSTOM_MAX_RECONNECT_ATTEMPTS) {
                LOG_ERROR("Custom MQTT connection failed after %lu attempts. Disabling Custom MQTT.", _currentMqttConnectionAttempt);
                _disable("Max reconnect attempts reached");
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

    static void _publishMeter(const char* topic)
    {
        JsonDocument jsonDocument;
        Ade7953::fullMeterValuesToJson(jsonDocument);

        // Validate that we have actual data before serializing (since the JSON serialization allows for empty objects)
        if (jsonDocument.isNull() || jsonDocument.size() == 0) {
            LOG_DEBUG("No meter data available for publishing to Custom MQTT");
            return;
        }

        if (_publishMeterStreaming(jsonDocument, topic)) LOG_DEBUG("Meter data published to Custom MQTT via streaming");
        else LOG_WARNING("Failed to publish meter data to Custom MQTT via streaming");
    }

    static bool _publishMeterStreaming(JsonDocument &jsonDocument, const char* topic)
    {
        if (strlen(topic) == 0) {
            LOG_WARNING("Empty topic. Skipping streaming publish");
            return false;
        }

        size_t payloadLength = measureJson(jsonDocument);
        if (payloadLength == 0) {
            LOG_WARNING("Empty JSON payload. Skipping streaming publish");
            return false;
        }

        LOG_DEBUG("Starting streaming publish to topic '%s' with payload size %zu bytes", topic, payloadLength);

        if (!_mqttClient.beginPublish(topic, payloadLength, false)) {
            LOG_WARNING("Failed to begin streaming publish. MQTT client state: %s", _getMqttStateReason(_mqttClient.state()));
            statistics.customMqttMessagesPublishedError++;
            _currentFailedMessagePublishAttempt++;
            if (_currentFailedMessagePublishAttempt > MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS) {
                _disable("Max failed message publish attempts reached");
            }
            return false;
        }

        BufferingPrint bufferedCustomMqttClient(_mqttClient, STREAM_UTILS_MQTT_PACKET_SIZE);
        size_t bytesWritten = serializeJson(jsonDocument, bufferedCustomMqttClient);
        bufferedCustomMqttClient.flush();
        _mqttClient.endPublish();

        if (bytesWritten != payloadLength) {
            LOG_WARNING("Streaming publish size mismatch: expected %zu bytes, wrote %zu bytes", payloadLength, bytesWritten);
            statistics.customMqttMessagesPublishedError++;
            _currentFailedMessagePublishAttempt++;
            if (_currentFailedMessagePublishAttempt > MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS) {
                _disable("Max failed message publish attempts reached");
            }
            return false;
        }

        // success
        _currentFailedMessagePublishAttempt = 0;
        statistics.customMqttMessagesPublished++;
        LOG_DEBUG("Streaming publish successful: %zu bytes written to topic '%s'", bytesWritten, topic);
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