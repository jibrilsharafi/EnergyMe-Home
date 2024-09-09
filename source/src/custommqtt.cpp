#include "custommqtt.h"

CustomMqtt::CustomMqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger) : _ade7953(ade7953), _logger(logger) {}

void CustomMqtt::setup()
{
    _logger.debug("Setting up custom MQTT...", "custommqtt::setup");
    
    if (isFirstSetup) {
        _setDefaultConfiguration();
    } else {
        _setConfigurationFromSpiffs();
    }

    customClientMqtt.setBufferSize(MQTT_CUSTOM_PAYLOAD_LIMIT);
    
    customClientMqtt.setServer(customMqttConfiguration.server.c_str(), customMqttConfiguration.port);

    _logger.debug("MQTT setup complete", "custommqtt::setup");

    if (customMqttConfiguration.enabled) _isSetupDone = true;
}

void CustomMqtt::loop()
{
    if (!customMqttConfiguration.enabled)
    {
        if (_isSetupDone)
        {
            _logger.info("Disconnecting custom MQTT", "custommqtt::loop");
            customClientMqtt.disconnect();
            _isSetupDone = false;
        }
        else
        {
            _logger.verbose("Custom MQTT not enabled. Skipping...", "custommqtt::loop");
        }

        return;
    }

    if ((millis() - _lastMillisMqttLoop) > MQTT_CUSTOM_LOOP_INTERVAL)
    {
        _lastMillisMqttLoop = millis();
        if (!customClientMqtt.connected())
        {
            if ((millis() - _lastMillisMqttFailed) < MQTT_CUSTOM_MIN_CONNECTION_INTERVAL)
            {
                _logger.verbose("MQTT connection failed recently. Skipping", "custommqtt::_connectMqtt");
                return;
            }

            if (!_connectMqtt())
                return;
        }

        customClientMqtt.loop();

        if ((millis() - _lastMillisMeterPublish) > customMqttConfiguration.frequency * 1000)
        {
            _lastMillisMeterPublish = millis();
            _publishMeter();
        }
    }
}

void CustomMqtt::_setDefaultConfiguration()
{
    _logger.debug("Setting default custom MQTT configuration...", "custommqtt::setDefaultConfiguration");

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_custom_mqtt_json);

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

    customMqttConfiguration.enabled = jsonDocument["enabled"].as<bool>();
    customMqttConfiguration.server = jsonDocument["server"].as<String>();
    customMqttConfiguration.port = jsonDocument["port"].as<int>();
    customMqttConfiguration.clientid = jsonDocument["clientid"].as<String>();
    customMqttConfiguration.topic = jsonDocument["topic"].as<String>();
    customMqttConfiguration.frequency = jsonDocument["frequency"].as<int>();
    customMqttConfiguration.useCredentials = jsonDocument["useCredentials"].as<bool>();
    customMqttConfiguration.username = jsonDocument["username"].as<String>();
    customMqttConfiguration.password = jsonDocument["password"].as<String>();

    _saveConfigurationToSpiffs();
    
    customClientMqtt.disconnect();
    customClientMqtt.setServer(customMqttConfiguration.server.c_str(), customMqttConfiguration.port);

    _mqttConnectionAttempt = 0;

    _logger.debug("Custom MQTT configuration set", "custommqtt::setConfiguration");

    return true;
}

void CustomMqtt::_setConfigurationFromSpiffs()
{
    _logger.debug("Setting custom MQTT configuration from SPIFFS...", "custommqtt::setConfigurationFromSpiffs");

    File _file = SPIFFS.open(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, "r");
    if (!_file)
    {
        _logger.error("Failed to open custom MQTT configuration file. Using default one", "custommqtt::setConfigurationFromSpiffs");
        _setDefaultConfiguration();
        return;
    }

    JsonDocument _jsonDocument;
    DeserializationError _error = deserializeJson(_jsonDocument, _file);

    if (_error)
    {
        _logger.error("Failed to parse custom MQTT configuration JSON. Using default one", "custommqtt::setConfigurationFromSpiffs");
        _file.close();
        _setDefaultConfiguration();
        return;
    }

    setConfiguration(_jsonDocument);

    _file.close();

    _logger.debug("Successfully set custom MQTT configuration from SPIFFS", "custommqtt::setConfigurationFromSpiffs");
}

void CustomMqtt::_saveConfigurationToSpiffs()
{
    _logger.debug("Saving custom MQTT configuration to SPIFFS...", "custommqtt::_saveConfigurationToSpiffs");

    JsonDocument _jsonDocument;
    _jsonDocument["enabled"] = customMqttConfiguration.enabled;
    _jsonDocument["server"] = customMqttConfiguration.server;
    _jsonDocument["port"] = customMqttConfiguration.port;
    _jsonDocument["clientid"] = customMqttConfiguration.clientid;
    _jsonDocument["topic"] = customMqttConfiguration.topic;
    _jsonDocument["frequency"] = customMqttConfiguration.frequency;
    _jsonDocument["useCredentials"] = customMqttConfiguration.useCredentials;
    _jsonDocument["username"] = customMqttConfiguration.username;
    _jsonDocument["password"] = customMqttConfiguration.password;

    serializeJsonToSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    _logger.debug("Successfully saved custom MQTT configuration to SPIFFS", "custommqtt::_saveConfigurationToSpiffs");
}

bool CustomMqtt::_validateJsonConfiguration(JsonDocument &jsonDocument)
{
    if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>())
    {
        return false;
    }

    if (!jsonDocument.containsKey("enabled") || !jsonDocument["enabled"].is<bool>()) return false;
    if (!jsonDocument.containsKey("server") || !jsonDocument["server"].is<String>()) return false;
    if (!jsonDocument.containsKey("port") || !jsonDocument["port"].is<int>()) return false;
    if (!jsonDocument.containsKey("clientid") || !jsonDocument["clientid"].is<String>()) return false;
    if (!jsonDocument.containsKey("topic") || !jsonDocument["topic"].is<String>()) return false;
    if (!jsonDocument.containsKey("frequency") || !jsonDocument["frequency"].is<int>()) return false;
    if (!jsonDocument.containsKey("useCredentials") || !jsonDocument["useCredentials"].is<bool>()) return false;
    if (!jsonDocument.containsKey("username") || !jsonDocument["username"].is<String>()) return false;
    if (!jsonDocument.containsKey("password") || !jsonDocument["password"].is<String>()) return false;

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
    _logger.debug("Attempt to connect to custom MQTT (%d/%d)...", "custommqtt::_connectMqtt", _mqttConnectionAttempt + 1, MQTT_MAX_CONNECTION_ATTEMPT);
    if (_mqttConnectionAttempt >= MQTT_CUSTOM_MAX_CONNECTION_ATTEMPT)
    {
        _logger.error("Failed to connect to custom MQTT after %d attempts. Disabling custom MQTT", "custommqtt::_connectMqtt", MQTT_MAX_CONNECTION_ATTEMPT);
        _disable();
        return false;
    }

    bool res;

    if (customMqttConfiguration.useCredentials)
    {
        res = customClientMqtt.connect(
            customMqttConfiguration.clientid.c_str(),
            customMqttConfiguration.username.c_str(),
            customMqttConfiguration.password.c_str());
    }
    else
    {
        res = customClientMqtt.connect(customMqttConfiguration.clientid.c_str());
    }

    if (res)
    {
        _logger.info("Connected to custom MQTT", "custommqtt::_connectMqtt");

        _mqttConnectionAttempt = 0;

        return true;
    }
    else
    {
        _logger.warning(
            "Failed to connect to custom MQTT (%d/%d). Reason: %s. Retrying...",
            "custommqtt::_connectMqtt",
            _mqttConnectionAttempt + 1,
            MQTT_MAX_CONNECTION_ATTEMPT,
            getMqttStateReason(customClientMqtt.state()));

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;

        return false;
    }
}

void CustomMqtt::_publishMeter()
{
    _logger.debug("Publishing meter data to custom MQTT...", "custommqtt::_publishMeter");

    JsonDocument _jsonDocument = _ade7953.meterValuesToJson();
    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    if (_publishMessage(customMqttConfiguration.topic.c_str(), _meterMessage.c_str())) _logger.debug("Meter data published to custom MQTT", "custommqtt::_publishMeter");
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

    if (!customClientMqtt.connected())
    {
        _logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", "custommqtt::_publishMessage", getMqttStateReason(customClientMqtt.state()), topic);
        return false;
    }

    // Additional debugging information
    _logger.debug("MQTT client is connected. Attempting to publish message.", "custommqtt::_publishMessage");

    // Check if the topic and message are valid
    if (strlen(topic) == 0 || strlen(message) == 0)
    {
        _logger.warning("Empty topic or message. Skipping publishing.", "custommqtt::_publishMessage");
        return false;
    }

    // Attempt to publish the message
    bool result = customClientMqtt.publish(topic, message);
    if (!result)
    {
        _logger.warning("Failed to publish message. MQTT client state: %s", "custommqtt::_publishMessage", getMqttStateReason(customClientMqtt.state()));
        return false;
    }

    _logger.debug("Message published: %s", "custommqtt::_publishMessage", message);
    return true;
}