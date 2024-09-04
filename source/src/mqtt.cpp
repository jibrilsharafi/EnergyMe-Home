#include "mqtt.h"

Mqtt* globalMqttInstance = nullptr;

void subscribeCallback(const char* topic, byte *payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE)) {
        File _file = SPIFFS.open(FW_UPDATE_INFO_JSON_PATH, FILE_WRITE);
        if (!_file) {
            return;
        }

        _file.print(message);
        _file.close();
    }
}

Mqtt::Mqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    CustomTime &customTime
) : _ade7953(ade7953), _logger(logger), _customTime(customTime) {
    globalMqttInstance = this;
}

#ifdef ENERGYME_HOME_SECRETS_H

bool Mqtt::begin(String deviceId) {
    _logger.debug("Setting up MQTT...", "mqtt::begin");

    if (!generalConfiguration.isCloudServicesEnabled) {
        _logger.warning("Cloud services not enabled. Skipping...", "mqtt::begin");
        return false;
    }

    _deviceId = deviceId;
    
    _setupTopics();

    clientMqtt.setCallback(subscribeCallback); //FIXME: not working, crashing the subscribe as well if policy not * (maybe it is the subscribe part, not here)

    net.setCACert(aws_iot_core_cert_ca);
    net.setCertificate(aws_iot_core_cert_crt);
    net.setPrivateKey(aws_iot_core_cert_private);

    clientMqtt.setServer(AWS_IOT_CORE_MQTT_ENDPOINT, AWS_IOT_CORE_PORT);

    clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    if (!_connectMqtt()) {
        _logger.error("Failed to setup MQTT", "mqtt::begin");
        return false;
    }

    _logger.debug("MQTT setup complete", "mqtt::begin");

    _isSetupDone = true;

    return true;
}

#else

bool Mqtt::begin() {
    _logger.warning("Secrets not available. MQTT setup failed.", "mqtt::setupMqtt");
    return false;
}

#endif

#ifdef ENERGYME_HOME_SECRETS_H

void Mqtt::loop() {
    if (!generalConfiguration.isCloudServicesEnabled) {
        if (_isSetupDone) {
            _logger.info("Cloud services disabled. Disconnecting MQTT...", "mqtt::mqttLoop");
            clientMqtt.disconnect();
            _isSetupDone = false;
        }

        _logger.verbose("Cloud services not enabled. Skipping...", "mqtt::mqttLoop");
        return;
    }

    if (!_isSetupDone) {
        if (!begin(getDeviceId())) {
            setRestartEsp32("mqtt::mqttLoop", "Failed to setup MQTT");
        }
    }

    if ((millis() - _lastMillisMqttLoop) > MQTT_LOOP_INTERVAL) {
        _lastMillisMqttLoop = millis();

        if (!clientMqtt.connected()) {
            if ((millis() - _lastMillisMqttFailed) < MQTT_MIN_CONNECTION_INTERVAL) {
                _logger.verbose("MQTT connection failed recently. Skipping...", "mqtt::_connectMqtt");
                return;
            }

            _logger.warning("MQTT connection lost. Reconnecting...", "mqtt::mqttLoop");
            if (!_connectMqtt()) {
                if (_mqttConnectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPT) {
                    setRestartEsp32("mqtt::mqttLoop", "Failed to connect to MQTT and hit maximum connection attempt");
                }
                return;
            }
        }

        _checkIfPublishMeterNeeded();
        _checkIfPublishStatusNeeded();

        _checkPublishMqtt();
    }
}

#else

void Mqtt::mqttLoop() {
    _logger.verbose("Secrets not available. MQTT loop failed", "mqtt::mqttLoop");
}

#endif

bool Mqtt::_connectMqtt() { //TODO: Handle continuous connection/reconnection
    _logger.debug("Attempt to connect to MQTT...", "mqtt::_connectMqtt");
    
    if (clientMqtt.connect(_deviceId.c_str())) {
        _logger.info("Connected to MQTT", "mqtt::_connectMqtt");

        _mqttConnectionAttempt = 0;
        
        publishMetadata();
        publishChannel();
        publishStatus();
        publishGeneralConfiguration();

        _subscribeToTopics();
        
        return true;
    } else {
        _logger.warning(
            "Failed to connect to MQTT (%d/%d)",
            "mqtt::_connectMqtt",
            _mqttConnectionAttempt+1,
            MQTT_MAX_CONNECTION_ATTEMPT
        );

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;
        
        return false;
    }
}

#ifdef ENERGYME_HOME_SECRETS_H

void Mqtt::_constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topic) {
    _logger.debug("Constructing MQTT topic with rule for %s | %s", "mqtt::_constructMqttTopicWithRule", ruleName, finalTopic);

    snprintf(
        topic,
        MQTT_MAX_TOPIC_LENGTH,
        "%s/%s/%s/%s/%s/%s",
        MQTT_BASIC_INGEST,
        ruleName,
        MQTT_TOPIC_1,
        MQTT_TOPIC_2,
        _deviceId,
        finalTopic
    );
}

void Mqtt::_constructMqttTopic(const char* finalTopic, char* topic) {
    _logger.debug("Constructing MQTT topic for %s", "mqtt::_constructMqttTopic", finalTopic);

    snprintf(
        topic,
        MQTT_MAX_TOPIC_LENGTH,
        "%s/%s/%s/%s",
        MQTT_TOPIC_1,
        MQTT_TOPIC_2,
        _deviceId,
        finalTopic
    );
}

void Mqtt::_setupTopics() {
    _logger.debug("Setting up MQTT topics...", "mqtt::_setupTopics");

    _setTopicMeter();
    _setTopicStatus();
    _setTopicMetadata();
    _setTopicChannel();
    _setTopicGeneralConfiguration();

    _logger.debug("MQTT topics setup complete", "mqtt::_setupTopics");
}

void Mqtt::_setTopicMeter() {
#ifdef USE_RULE_FOR_TOPIC_CONSTRUCTION
    _constructMqttTopicWithRule(MQTT_RULE_NAME_METER, MQTT_TOPIC_METER, _mqttTopicMeter);
#else
    _constructMqttTopic(MQTT_TOPIC_METER, _mqttTopicMeter);
#endif
    _logger.debug(_mqttTopicMeter, "mqtt::_setTopicMeter");
}

void Mqtt::_setTopicStatus() {
#ifdef USE_RULE_FOR_TOPIC_CONSTRUCTION
    _constructMqttTopicWithRule(MQTT_RULE_NAME_STATUS, MQTT_TOPIC_STATUS, _mqttTopicStatus);
#else
    _constructMqttTopic(MQTT_TOPIC_STATUS, _mqttTopicStatus);
#endif
    _logger.debug(_mqttTopicStatus, "mqtt::_setTopicStatus");
}

void Mqtt::_setTopicMetadata() {
#ifdef USE_RULE_FOR_TOPIC_CONSTRUCTION
    _constructMqttTopicWithRule(MQTT_RULE_NAME_METADATA, MQTT_TOPIC_METADATA, _mqttTopicMetadata);
#else
    _constructMqttTopic(MQTT_TOPIC_METADATA, _mqttTopicMetadata);
#endif
    _logger.debug(_mqttTopicMetadata, "mqtt::_setTopicMetadata");
}

void Mqtt::_setTopicChannel() {
#ifdef USE_RULE_FOR_TOPIC_CONSTRUCTION
    _constructMqttTopicWithRule(MQTT_RULE_NAME_CHANNEL, MQTT_TOPIC_CHANNEL, _mqttTopicChannel);
#else
    _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel);
#endif
    _logger.debug(_mqttTopicChannel, "mqtt::_setTopicChannel");
}

void Mqtt::_setTopicGeneralConfiguration() {
#ifdef USE_RULE_FOR_TOPIC_CONSTRUCTION
    _constructMqttTopicWithRule(MQTT_RULE_NAME_GENERAL_CONFIGURATION, MQTT_TOPIC_GENERAL_CONFIGURATION, _mqttTopicGeneralConfiguration);
#else
    _constructMqttTopic(MQTT_TOPIC_GENERAL_CONFIGURATION, _mqttTopicGeneralConfiguration);
#endif
    _logger.debug(_mqttTopicGeneralConfiguration, "mqtt::_setTopicGeneralConfiguration");
}

#endif

void Mqtt::_circularBufferToJson(JsonDocument* jsonDocument, CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter) {
    _logger.debug("Converting circular buffer to JSON", "mqtt::_circularBufferToJson");

    JsonArray _jsonArray = jsonDocument->to<JsonArray>();
    
    while (!payloadMeter.isEmpty()) {
        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        PayloadMeter _oldestPayloadMeter = payloadMeter.shift();

        _jsonObject["channel"] = _oldestPayloadMeter.channel;
        _jsonObject["unixTime"] = _oldestPayloadMeter.unixTime;
        _jsonObject["activePower"] = _oldestPayloadMeter.activePower;
        _jsonObject["powerFactor"] = _oldestPayloadMeter.powerFactor;
    }

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
        if (_ade7953.channelData[i].active) {
            JsonObject _jsonObject = _jsonArray.add<JsonObject>();

            _jsonObject["channel"] = i;
            _jsonObject["unixTime"] = _customTime.getUnixTime();
            _jsonObject["activeEnergy"] = _ade7953.meterValues[i].activeEnergy;
            _jsonObject["reactiveEnergy"] = _ade7953.meterValues[i].reactiveEnergy;
            _jsonObject["apparentEnergy"] = _ade7953.meterValues[i].apparentEnergy;
        }
    }

    JsonObject _jsonObject = _jsonArray.add<JsonObject>();
    _jsonObject["unixTime"] = _customTime.getUnixTime();
    _jsonObject["voltage"] = _ade7953.meterValues[0].voltage;

    _logger.debug("Circular buffer converted to JSON", "mqtt::_circularBufferToJson");
}

void Mqtt::publishMeter() {
    _logger.debug("Publishing meter data to MQTT...", "mqtt::publishMeter");

    JsonDocument _jsonDocument;
    _circularBufferToJson(&_jsonDocument, payloadMeter);

    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    if (_publishMessage(_mqttTopicMeter, _meterMessage.c_str())) {publishMqtt.meter = false;}

    _logger.debug("Meter data published to MQTT", "mqtt::publishMeter");
}

void Mqtt::publishStatus() {
    _logger.debug("Publishing status to MQTT...", "mqtt::publishStatus");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["rssi"] = WiFi.RSSI();
    _jsonDocument["uptime"] = millis();
    _jsonDocument["freeHeap"] = ESP.getFreeHeap();
    _jsonDocument["freeSpiffs"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();

    String _statusMessage;
    serializeJson(_jsonDocument, _statusMessage);

    if (_publishMessage(_mqttTopicStatus, _statusMessage.c_str())) {publishMqtt.status = false;}

    _logger.debug("Status published to MQTT", "mqtt::publishStatus");
}

void Mqtt::publishMetadata() {
    _logger.debug("Publishing metadata to MQTT...", "mqtt::publishMetadata");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["firmwareBuildVersion"] = FIRMWARE_BUILD_VERSION;
    _jsonDocument["firmwareBuildDate"] = FIRMWARE_BUILD_DATE;

    String _metadataMessage;
    serializeJson(_jsonDocument, _metadataMessage);

    if (_publishMessage(_mqttTopicMetadata, _metadataMessage.c_str())) {publishMqtt.metadata = false;}
    
    _logger.debug("Metadata published to MQTT", "mqtt::publishMetadata");
}

void Mqtt::publishChannel() {
    _logger.debug("Publishing channel data to MQTT", "mqtt::publishChannel");

    JsonDocument _jsonChannelData;
    _ade7953.channelDataToJson(_jsonChannelData);
    
    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["data"] = _jsonChannelData;

    String _channelMessage;
    serializeJson(_jsonDocument, _channelMessage);
 
    if (_publishMessage(_mqttTopicChannel, _channelMessage.c_str())) {publishMqtt.channel = false;}

    _logger.debug("Channel data published to MQTT", "mqtt::publishChannel");
}

void Mqtt::publishGeneralConfiguration() {
    _logger.debug("Publishing general configuration to MQTT", "mqtt::publishGeneralConfiguration");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();

    JsonDocument _jsonDocumentConfiguration;
    generalConfigurationToJson(generalConfiguration, _jsonDocumentConfiguration);
    _jsonDocument["generalConfiguration"] = _jsonDocumentConfiguration;

    String _generalConfigurationMessage;
    serializeJson(_jsonDocument, _generalConfigurationMessage);

    if (_publishMessage(_mqttTopicGeneralConfiguration, _generalConfigurationMessage.c_str())) {publishMqtt.generalConfiguration = false;}

    _logger.debug("General configuration published to MQTT", "mqtt::publishGeneralConfiguration");
}

bool Mqtt::_publishMessage(const char* topic, const char* message) {
    if (!generalConfiguration.isCloudServicesEnabled) {
        _logger.verbose("Cloud services not enabled. Skipping...", "mqtt::_publishMessage");
        return false;
    }

    _logger.debug(
        "Publishing message to topic %s",
        "mqtt::_publishMessage",
        topic
    );

    if (topic == nullptr || message == nullptr) {
        _logger.debug("Null pointer or message passed, meaning MQTT not initialized yet", "mqtt::_publishMessage");
        return false;
    }

    if (!clientMqtt.connected()) {
        _logger.warning("MQTT client not connected. Skipping...", "mqtt::_publishMessage");
        return false;
    }

    if (!clientMqtt.publish(topic, message)) {
        _logger.error("Failed to publish message", "mqtt::_publishMessage");
        return false;
    }

    _logger.debug("Message published: %s", "mqtt::_publishMessage", message);
    return true;
}

void Mqtt::_checkIfPublishMeterNeeded() {
    if (payloadMeter.isFull() || (millis() - _lastMillisMeterPublished) > MAX_INTERVAL_METER_PUBLISH) {
        _logger.debug("Setting flag to publish %d meter data", "mqtt::_checkIfPublishMeterNeeded", payloadMeter.size());

        publishMqtt.meter = true;
        
        _lastMillisMeterPublished = millis();
    }
}

void Mqtt::_checkIfPublishStatusNeeded() {
    if ((millis() - _lastMillisStatusPublished) > MAX_INTERVAL_STATUS_PUBLISH) {
        _logger.debug("Setting flag to publish status", "mqtt::_checkIfPublishStatusNeeded");
        
        publishMqtt.status = true;
        
        _lastMillisStatusPublished = millis();
    }
}

void Mqtt::_checkPublishMqtt() {
    if (!generalConfiguration.isCloudServicesEnabled) {
        _logger.verbose("Cloud services not enabled. Skipping...", "mqtt::_checkPublishMqtt");
        return;
    }

    if (publishMqtt.meter) {publishMeter();}
    if (publishMqtt.status) {publishStatus();}
    if (publishMqtt.metadata) {publishMetadata();}
    if (publishMqtt.channel) {publishChannel();}
    if (publishMqtt.generalConfiguration) {publishGeneralConfiguration();}
}

void Mqtt::_subscribeToTopics() {
    _logger.debug("Subscribing to topics...", "mqtt::_subscribeToTopics");

    _subscribeUpdateFirmware();

    _logger.debug("Subscribed to topics", "mqtt::_subscribeToTopics");
}

void Mqtt::_subscribeUpdateFirmware() {
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE, _topic);
    
    if (!clientMqtt.subscribe(_topic)) {
        _logger.error("Failed to subscribe to firmware update topic", "mqtt::_subscribeUpdateFirmware");
    }
}