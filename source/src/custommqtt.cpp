#include "custommqtt.h"

static const char *TAG = "custommqtt";

CustomMqtt::CustomMqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    PubSubClient &customClientMqtt,
    CustomMqttConfiguration &customMqttConfiguration
    ) : _ade7953(ade7953),
        _logger(logger),
        _customClientMqtt(customClientMqtt),
        _customMqttConfiguration(customMqttConfiguration) {}


void CustomMqtt::begin()
{
    _logger.debug("Setting up custom MQTT...", TAG);
    
    _setConfigurationFromSpiffs();

    _customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
    _customClientMqtt.setServer(_customMqttConfiguration.server, _customMqttConfiguration.port);

    _logger.debug("MQTT setup complete", TAG);

    // Mark setup as done regardless of enabled state - configuration has been loaded
    _isSetupDone = true;
}

void CustomMqtt::loop()
{
    if ((millis() - _lastMillisMqttLoop) < MQTT_CUSTOM_LOOP_INTERVAL) return;
    _lastMillisMqttLoop = millis();

    if (!CustomWifi::isFullyConnected()) return;

    // Ensure configuration is loaded from SPIFFS on first run
    
    if (!_isSetupDone) { begin(); }
    
    
    if (!_customMqttConfiguration.enabled)
    {
        // If MQTT was previously connected but is now disabled, disconnect
        if (_customClientMqtt.state() >= MQTT_CONNECTED) // Anything above 0 is connected
        {
            
            _logger.info("Custom MQTT is disabled. Disconnecting...", TAG);
            _customClientMqtt.disconnect();
        }
        return;
    }

    
    if (!_customClientMqtt.connected())
    {
        // Use exponential backoff timing
        if (millis() >= _nextMqttConnectionAttemptMillis) {
            _logger.debug("Custom MQTT client not connected. Attempting to reconnect...", TAG);
            _connectMqtt(); // _connectMqtt now handles setting the next attempt time
        }
    } else {
         // If connected, reset connection attempts counter for backoff calculation
        if (_mqttConnectionAttempt > 0) {
             _logger.debug("Custom MQTT reconnected successfully after %d attempts.", TAG, _mqttConnectionAttempt);
            _mqttConnectionAttempt = 0;
        }
    }

    
    // Only loop and publish if connected
    if (_customClientMqtt.connected()) {
        
        _customClientMqtt.loop();

        if ((millis() - _lastMillisMeterPublish) > _customMqttConfiguration.frequency * 1000)
        {
            
            _lastMillisMeterPublish = millis();
            _publishMeter();
        }
    }
}

void CustomMqtt::_setDefaultConfiguration()
{
    _logger.debug("Setting default custom MQTT configuration...", TAG);

    createDefaultCustomMqttConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    setConfiguration(_jsonDocument);

    _logger.debug("Default custom MQTT configuration set", TAG);
}

bool CustomMqtt::setConfiguration(JsonDocument &jsonDocument)
{
    _logger.debug("Setting custom MQTT configuration...", TAG);

    if (!_validateJsonConfiguration(jsonDocument))
    {
        _logger.error("Invalid custom MQTT configuration", TAG);
        return false;
    }

    _customMqttConfiguration.enabled = jsonDocument["enabled"].as<bool>();
    snprintf(_customMqttConfiguration.server, sizeof(_customMqttConfiguration.server), "%s", jsonDocument["server"].as<const char*>());
    _customMqttConfiguration.port = jsonDocument["port"].as<int>();
    snprintf(_customMqttConfiguration.clientid, sizeof(_customMqttConfiguration.clientid), "%s", jsonDocument["clientid"].as<const char*>());
    snprintf(_customMqttConfiguration.topic, sizeof(_customMqttConfiguration.topic), "%s", jsonDocument["topic"].as<const char*>());
    _customMqttConfiguration.frequency = jsonDocument["frequency"].as<int>();
    _customMqttConfiguration.useCredentials = jsonDocument["useCredentials"].as<bool>();
    snprintf(_customMqttConfiguration.username, sizeof(_customMqttConfiguration.username), "%s", jsonDocument["username"].as<const char*>());
    snprintf(_customMqttConfiguration.password, sizeof(_customMqttConfiguration.password), "%s", jsonDocument["password"].as<const char*>());    
    snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), "Disconnected");
    char _timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(_timestampBuffer);
    snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", _timestampBuffer);

    _nextMqttConnectionAttemptMillis = millis(); // Try connecting immediately
    _mqttConnectionAttempt = 0;

    _customClientMqtt.disconnect();
    _customClientMqtt.setServer(_customMqttConfiguration.server, _customMqttConfiguration.port);
    
    _saveConfigurationToSpiffs();

    _logger.debug("Custom MQTT configuration set", TAG);

    return true;
}

void CustomMqtt::_setConfigurationFromSpiffs()
{
    _logger.debug("Setting custom MQTT configuration from SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (!setConfiguration(_jsonDocument))
    {
        _logger.error("Failed to set custom MQTT configuration from SPIFFS. Using default one", TAG);
        _setDefaultConfiguration();
        return;
    }

    _logger.debug("Successfully set custom MQTT configuration from SPIFFS", TAG);
}

void CustomMqtt::_saveConfigurationToSpiffs()
{
    _logger.debug("Saving custom MQTT configuration to SPIFFS...", TAG);

    JsonDocument _jsonDocument;

    _jsonDocument["enabled"] = _customMqttConfiguration.enabled;
    _jsonDocument["server"] = _customMqttConfiguration.server;
    _jsonDocument["port"] = _customMqttConfiguration.port;
    _jsonDocument["clientid"] = _customMqttConfiguration.clientid;
    _jsonDocument["topic"] = _customMqttConfiguration.topic;
    _jsonDocument["frequency"] = _customMqttConfiguration.frequency;
    _jsonDocument["useCredentials"] = _customMqttConfiguration.useCredentials;
    _jsonDocument["username"] = _customMqttConfiguration.username;
    _jsonDocument["password"] = _customMqttConfiguration.password;
    _jsonDocument["lastConnectionStatus"] = _customMqttConfiguration.lastConnectionStatus;
    _jsonDocument["lastConnectionAttemptTimestamp"] = _customMqttConfiguration.lastConnectionAttemptTimestamp;

    serializeJsonToSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    _logger.debug("Successfully saved custom MQTT configuration to SPIFFS", TAG);
}

bool CustomMqtt::_validateJsonConfiguration(JsonDocument &jsonDocument)
{
    if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
    {
        _logger.warning("Invalid JSON document", TAG);
        return false;
    }

    if (!jsonDocument["enabled"].is<bool>()) { _logger.warning("enabled field is not a boolean", TAG); return false; }
    if (!jsonDocument["server"].is<const char*>()) { _logger.warning("server field is not a string", TAG); return false; }
    if (!jsonDocument["port"].is<int>()) { _logger.warning("port field is not an integer", TAG); return false; }
    if (!jsonDocument["clientid"].is<const char*>()) { _logger.warning("clientid field is not a string", TAG); return false; }
    if (!jsonDocument["topic"].is<const char*>()) { _logger.warning("topic field is not a string", TAG); return false; }
    if (!jsonDocument["frequency"].is<int>()) { _logger.warning("frequency field is not an integer", TAG); return false; }
    if (!jsonDocument["useCredentials"].is<bool>()) { _logger.warning("useCredentials field is not a boolean", TAG); return false; }
    if (!jsonDocument["username"].is<const char*>()) { _logger.warning("username field is not a string", TAG); return false; }
    if (!jsonDocument["password"].is<const char*>()) { _logger.warning("password field is not a string", TAG); return false; }

    return true;
}

void CustomMqtt::_disable() {
    _logger.debug("Disabling custom MQTT...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    _jsonDocument["enabled"] = false;

    setConfiguration(_jsonDocument);
    
    // Don't reset _isSetupDone - configuration is still loaded, just disabled
    _mqttConnectionAttempt = 0;

    _logger.debug("Custom MQTT disabled", TAG);
}

bool CustomMqtt::_connectMqtt()
{
    _logger.debug("Attempt to connect to custom MQTT (attempt %d)...", TAG, _mqttConnectionAttempt + 1);

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
        _logger.info("Connected to custom MQTT", TAG);

        _mqttConnectionAttempt = 0; // Reset attempt counter on success
        snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), "Connected");

        char _timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(_timestampBuffer);
        snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", _timestampBuffer);

        _saveConfigurationToSpiffs();

        return true;
    } else {
        int _currentState = _customClientMqtt.state();
        const char* _reason = getMqttStateReason(_currentState);

        _logger.warning(
            "Failed to connect to custom MQTT (attempt %d). Reason: %s (%d). Retrying...",
            TAG,
            _mqttConnectionAttempt + 1,
            _reason,
            _currentState);

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;

        snprintf(_customMqttConfiguration.lastConnectionStatus, sizeof(_customMqttConfiguration.lastConnectionStatus), 
                 "%s (Attempt %d)", _reason, _mqttConnectionAttempt);
        
        char _timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(_timestampBuffer);
        snprintf(_customMqttConfiguration.lastConnectionAttemptTimestamp, sizeof(_customMqttConfiguration.lastConnectionAttemptTimestamp), "%s", _timestampBuffer);

        _saveConfigurationToSpiffs();

        // Check for specific errors that warrant disabling custom MQTT
        if (_currentState == MQTT_CONNECT_BAD_CREDENTIALS || _currentState == MQTT_CONNECT_UNAUTHORIZED) {
             _logger.error("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling custom MQTT.",
                TAG,
                _currentState);
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

        _nextMqttConnectionAttemptMillis = millis() + _backoffDelay;

        _logger.info("Next custom MQTT connection attempt in %lu ms", TAG, _backoffDelay);

        return false;
    }
}

void CustomMqtt::_publishMeter()
{
    _logger.debug("Publishing meter data to custom MQTT...", TAG);

    JsonDocument _jsonDocument;
    _ade7953.fullMeterValuesToJson(_jsonDocument);
    
    char _meterMessage[MQTT_CUSTOM_PAYLOAD_LIMIT];
    if (!safeSerializeJson(_jsonDocument, _meterMessage, sizeof(_meterMessage))) {
        _logger.warning("Failed to serialize meter data to JSON", TAG);
        return;
    }

    if (_publishMessage(_customMqttConfiguration.topic, _meterMessage)) _logger.debug("Meter data published to custom MQTT", TAG);
}

bool CustomMqtt::_publishMessage(const char *topic, const char *message)
{
    _logger.debug(
        "Publishing message to topic %s | %s",
        TAG,
        topic,
        message);

    if (topic == nullptr || message == nullptr)
    {
        _logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", TAG);
        statistics.customMqttMessagesPublishedError++;
        return false;
    }

    if (!_customClientMqtt.connected())
    {
        _logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", TAG, getMqttStateReason(_customClientMqtt.state()), topic);
        statistics.customMqttMessagesPublishedError++;
        return false;
    }

    if (strlen(topic) == 0 || strlen(message) == 0)
    {
        _logger.warning("Empty topic or message. Skipping publishing.", TAG);
        statistics.customMqttMessagesPublishedError++;
        return false;
    }

    if (!_customClientMqtt.publish(topic, message))
    {
        _logger.warning("Failed to publish message. MQTT client state: %s", TAG, getMqttStateReason(_customClientMqtt.state()));
        statistics.customMqttMessagesPublishedError++;
        return false;
    }

    statistics.customMqttMessagesPublished++;
    _logger.debug("Message published: %s", TAG, message);
    return true;
}