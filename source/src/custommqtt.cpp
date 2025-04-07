#include "custommqtt.h"

CustomMqtt::CustomMqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    PubSubClient &customClientMqtt,
    CustomMqttConfiguration &customMqttConfiguration,
    CustomTime &customTime,
    MainFlags &mainFlags) : _ade7953(ade7953),
                            _logger(logger),
                            _customClientMqtt(customClientMqtt),
                            _customMqttConfiguration(customMqttConfiguration),
                            _customTime(customTime),
                            _mainFlags(mainFlags) {}


void CustomMqtt::begin()
{
    _logger.debug("Setting up custom MQTT...", "custommqtt::begin");
    
    _setConfigurationFromSpiffs();

    _customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
    _customClientMqtt.setServer(_customMqttConfiguration.server.c_str(), _customMqttConfiguration.port);

    _logger.debug("MQTT setup complete", "custommqtt::begin");

    if (_customMqttConfiguration.enabled) {
        _isSetupDone = true;
        _nextMqttConnectionAttemptMillis = millis(); // Try connecting immediately
        _mqttConnectionAttempt = 0;
        _connectMqtt(); // Initial connection attempt
    }
}

void CustomMqtt::loop()
{
    if ((millis() - _lastMillisMqttLoop) < MQTT_CUSTOM_LOOP_INTERVAL) return;
    _lastMillisMqttLoop = millis();

    if (!WiFi.isConnected()) return;

    if (!_customMqttConfiguration.enabled)
    {
        if (_isSetupDone)
        {
            _logger.info("Disconnecting custom MQTT", "custommqtt::loop");
            _customClientMqtt.disconnect();
            _isSetupDone = false;
        }
        return;
    }

    if (!_isSetupDone) { begin(); return; }

    if (!_customClientMqtt.connected())
    {
        // Use exponential backoff timing
        if (millis() >= _nextMqttConnectionAttemptMillis) {
            _logger.info("Custom MQTT client not connected. Attempting to reconnect...", "custommqtt::loop");
            _connectMqtt(); // _connectMqtt now handles setting the next attempt time
        }
    } else {
         // If connected, reset connection attempts counter for backoff calculation
        if (_mqttConnectionAttempt > 0) {
             _logger.debug("Custom MQTT reconnected successfully after %d attempts.", "custommqtt::loop", _mqttConnectionAttempt);
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
    _logger.debug("Setting default custom MQTT configuration...", "custommqtt::setDefaultConfiguration");

    createDefaultCustomMqttConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    setConfiguration(_jsonDocument);

    _logger.debug("Default custom MQTT configuration set", "custommqtt::setDefaultConfiguration");
}

bool CustomMqtt::setConfiguration(JsonDocument &jsonDocument)
{
    _logger.debug("Setting custom MQTT configuration...", "custommqtt::setConfiguration");

    if (!_validateJsonConfiguration(jsonDocument))
    {
        _logger.error("Invalid custom MQTT configuration", "custommqtt::setConfiguration");
        return false;
    }

    _customMqttConfiguration.enabled = jsonDocument["enabled"].as<bool>();
    _customMqttConfiguration.server = jsonDocument["server"].as<String>();
    _customMqttConfiguration.port = jsonDocument["port"].as<int>();
    _customMqttConfiguration.clientid = jsonDocument["clientid"].as<String>();
    _customMqttConfiguration.topic = jsonDocument["topic"].as<String>();
    _customMqttConfiguration.frequency = jsonDocument["frequency"].as<int>();
    _customMqttConfiguration.useCredentials = jsonDocument["useCredentials"].as<bool>();
    _customMqttConfiguration.username = jsonDocument["username"].as<String>();
    _customMqttConfiguration.password = jsonDocument["password"].as<String>();

    _saveConfigurationToSpiffs();
    
    _customClientMqtt.disconnect();
    _customClientMqtt.setServer(_customMqttConfiguration.server.c_str(), _customMqttConfiguration.port);

    _mqttConnectionAttempt = 0;
    _nextMqttConnectionAttemptMillis = millis(); // Try connecting immediately
    _customMqttConfiguration.lastConnectionStatus = "Disconnected";
    _customMqttConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();

    _logger.debug("Custom MQTT configuration set", "custommqtt::setConfiguration");

    return true;
}

void CustomMqtt::_setConfigurationFromSpiffs()
{
    _logger.debug("Setting custom MQTT configuration from SPIFFS...", "custommqtt::setConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (!setConfiguration(_jsonDocument))
    {
        _logger.error("Failed to set custom MQTT configuration from SPIFFS. Using default one", "custommqtt::setConfigurationFromSpiffs");
        _setDefaultConfiguration();
        return;
    }

    _logger.debug("Successfully set custom MQTT configuration from SPIFFS", "custommqtt::setConfigurationFromSpiffs");
}

void CustomMqtt::_saveConfigurationToSpiffs()
{
    _logger.debug("Saving custom MQTT configuration to SPIFFS...", "custommqtt::_saveConfigurationToSpiffs");

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

    _logger.debug("Successfully saved custom MQTT configuration to SPIFFS", "custommqtt::_saveConfigurationToSpiffs");
}

bool CustomMqtt::_validateJsonConfiguration(JsonDocument &jsonDocument)
{
    if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
    {
        _logger.warning("Invalid JSON document", "custommqtt::_validateJsonConfiguration");
        return false;
        }

        if (!jsonDocument["enabled"].is<bool>()) { _logger.warning("enabled field is not a boolean", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["server"].is<String>()) { _logger.warning("server field is not a string", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["port"].is<int>()) { _logger.warning("port field is not an integer", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["clientid"].is<String>()) { _logger.warning("clientid field is not a string", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["topic"].is<String>()) { _logger.warning("topic field is not a string", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["frequency"].is<int>()) { _logger.warning("frequency field is not an integer", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["useCredentials"].is<bool>()) { _logger.warning("useCredentials field is not a boolean", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["username"].is<String>()) { _logger.warning("username field is not a string", "custommqtt::_validateJsonConfiguration"); return false; }
        if (!jsonDocument["password"].is<String>()) { _logger.warning("password field is not a string", "custommqtt::_validateJsonConfiguration"); return false; }

    return true;
}

void CustomMqtt::_disable() {
    _logger.debug("Disabling custom MQTT...", "custommqtt::_disable");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    _jsonDocument["enabled"] = false;

    setConfiguration(_jsonDocument);
    
    _isSetupDone = false;
    _mqttConnectionAttempt = 0;

    _logger.debug("Custom MQTT disabled", "custommqtt::_disable");
}

bool CustomMqtt::_connectMqtt()
{
    _logger.debug("Attempt to connect to custom MQTT (attempt %d)...", "custommqtt::_connectMqtt", _mqttConnectionAttempt + 1);

    bool res;

    if (_customMqttConfiguration.useCredentials)
    {
        res = _customClientMqtt.connect(
            _customMqttConfiguration.clientid.c_str(),
            _customMqttConfiguration.username.c_str(),
            _customMqttConfiguration.password.c_str());
    }
    else
    {
        res = _customClientMqtt.connect(_customMqttConfiguration.clientid.c_str());
    }

    if (res)
    {
        _logger.info("Connected to custom MQTT", "custommqtt::_connectMqtt");
        _mqttConnectionAttempt = 0; // Reset attempt counter on success
        _customMqttConfiguration.lastConnectionStatus = "Connected";
        _customMqttConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
        _saveConfigurationToSpiffs();
        return true;
    }
    else
    {
        int _currentState = _customClientMqtt.state();
        const char* _reason = getMqttStateReason(_currentState);
        _logger.warning(
            "Failed to connect to custom MQTT (attempt %d). Reason: %s (%d). Retrying...",
            "custommqtt::_connectMqtt",
            _mqttConnectionAttempt + 1,
            _reason,
            _currentState);

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;

        _customMqttConfiguration.lastConnectionStatus = String(_reason) + " (Attempt " + String(_mqttConnectionAttempt) + ")";
        _customMqttConfiguration.lastConnectionAttemptTimestamp = _customTime.getTimestamp();
        _saveConfigurationToSpiffs();

        // Check for specific errors that warrant disabling custom MQTT
        if (_currentState == MQTT_CONNECT_BAD_CREDENTIALS || _currentState == MQTT_CONNECT_UNAUTHORIZED) {
             _logger.error("Custom MQTT connection failed due to authorization/credentials error (%d). Disabling custom MQTT.",
                "custommqtt::_connectMqtt",
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

        _logger.info("Next custom MQTT connection attempt in %lu ms", "custommqtt::_connectMqtt", _backoffDelay);

        return false;
    }
}

void CustomMqtt::_publishMeter()
{
    _logger.debug("Publishing meter data to custom MQTT...", "custommqtt::_publishMeter");

    JsonDocument _jsonDocument;
    _ade7953.meterValuesToJson(_jsonDocument);
    
    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    if (_publishMessage(_customMqttConfiguration.topic.c_str(), _meterMessage.c_str())) _logger.debug("Meter data published to custom MQTT", "custommqtt::_publishMeter");
}

bool CustomMqtt::_publishMessage(const char *topic, const char *message)
{
    _logger.debug(
        "Publishing message to topic %s | %s",
        "custommqtt::_publishMessage",
        topic,
        message);

    if (topic == nullptr || message == nullptr)
    {
        _logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", "custommqtt::_publishMessage");
        return false;
    }

    if (!_customClientMqtt.connected())
    {
        _logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", "custommqtt::_publishMessage", getMqttStateReason(_customClientMqtt.state()), topic);
        return false;
    }

    if (strlen(topic) == 0 || strlen(message) == 0)
    {
        _logger.warning("Empty topic or message. Skipping publishing.", "custommqtt::_publishMessage");
        return false;
    }

    if (!_customClientMqtt.publish(topic, message))
    {
        _logger.warning("Failed to publish message. MQTT client state: %s", "custommqtt::_publishMessage", getMqttStateReason(_customClientMqtt.state()));
        return false;
    }

    _logger.debug("Message published: %s", "custommqtt::_publishMessage", message);
    return true;
}