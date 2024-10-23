#include "mqtt.h"

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
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_RESTART)) {
        setRestartEsp32("subscribeCallback", "Restart requested from MQTT");
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) {
        JsonDocument _jsonDocument;
        deserializeJson(_jsonDocument, message);

        if (_jsonDocument["status"] == "success") {
            String _encryptedCertPem = _jsonDocument["encryptedCertificatePem"];
            String _encryptedPrivateKey = _jsonDocument["encryptedPrivateKey"];

            File _certFile = SPIFFS.open(CERTIFICATE_PATH, FILE_WRITE);
            if (_certFile) {
                _certFile.print(_encryptedCertPem);
                _certFile.close();
            }

            File _keyFile = SPIFFS.open(PRIVATE_KEY_PATH, FILE_WRITE);
            if (_keyFile) {
                _keyFile.print(_encryptedPrivateKey);
                _keyFile.close();
            }

            // Restart MQTT connection
            setRestartEsp32("subscribeCallback", "Restarting after successful certificates provisioning");
        }
    }
}

Mqtt::Mqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    CustomTime &customTime
) : _ade7953(ade7953), _logger(logger), _customTime(customTime) {}

void Mqtt::begin() {
    _logger.debug("Setting up MQTT...", "mqtt::begin");

    _deviceId = getDeviceId();

    if (!_checkCertificates()) {
        _claimProcess();
        return;
    }
    
    _setupTopics();

    clientMqtt.setCallback(subscribeCallback);

    _setCertificates();

    net.setCACert(aws_iot_core_cert_ca);
    net.setCertificate(_awsIotCoreCert.c_str());
    net.setPrivateKey(_awsIotCorePrivateKey.c_str());

    clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

    clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    _logger.debug("MQTT setup complete", "mqtt::begin");

    _connectMqtt();

    _isSetupDone = true;
}

void Mqtt::loop() {
    if (_isClaimInProgress) { // Only wait for certificates to be claimed
        clientMqtt.loop();
        return;
    }

    if (!generalConfiguration.isCloudServicesEnabled || restartConfiguration.isRequired) {
        if (_isSetupDone && clientMqtt.connected()) {
            _logger.info("Disconnecting MQTT", "mqtt::mqttLoop");

            // Send last messages before disconnecting
            _publishConnectivity(false); // Send offline connectivity as the last will message is not sent with graceful disconnect
            _publishMeter();
            _publishStatus();
            _publishMetadata();
            _publishChannel();
            _publishGeneralConfiguration();

            clientMqtt.disconnect();

            _isSetupDone = false;
        } else {
            _logger.verbose("Cloud services not enabled or device not connected. Skipping...", "mqtt::mqttLoop");
        }

        return;
    }

    if (!_isSetupDone) {
        begin();
    }

    if ((millis() - _lastMillisMqttLoop) > MQTT_LOOP_INTERVAL) {
        _lastMillisMqttLoop = millis();

        if (_forceDisableMqtt) {
            if ((millis() - _mqttConnectionFailedAt) < MQTT_TEMPORARY_DISABLE_INTERVAL) {
                _logger.verbose("Forced disabling MQTT. Skipping...", "mqtt::mqttLoop");
                return;
            } else {
                _forceDisableMqtt = false;
                _logger.info("Retrying MQTT connection after temporary disable", "mqtt::mqttLoop");
            }
        }

        if (!clientMqtt.connected()) {
            if ((millis() - _lastMillisMqttFailed) < MQTT_MIN_CONNECTION_INTERVAL) {
                _logger.verbose("MQTT connection failed recently. Skipping", "mqtt::_connectMqtt");
                return;
            }
            _logger.info("MQTT client not connected. Attempting to reconnect...", "mqtt::mqttLoop");

            if (!_connectMqtt()) return;
        }

        clientMqtt.loop();

        _checkIfPublishMeterNeeded();
        _checkIfPublishStatusNeeded();

        _checkPublishMqtt();
    }
}

bool Mqtt::_connectMqtt()
{
    _logger.debug("Attempt to connect to MQTT (%d/%d)...", "mqtt::_connectMqtt", _mqttConnectionAttempt + 1, MQTT_MAX_CONNECTION_ATTEMPT);
    if (_mqttConnectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPT) {
        _logger.warning("Failed to connect to MQTT after %d attempts. Temporarely disabling cloud services", "mqtt::_connectMqtt", MQTT_MAX_CONNECTION_ATTEMPT);
    
        _temporaryDisable();

        return false;
    }

    if (
        clientMqtt.connect(
            _deviceId.c_str(),
            _mqttTopicConnectivity,
            MQTT_WILL_QOS,
            MQTT_WILL_RETAIN,
            MQTT_WILL_MESSAGE))
    {
        _logger.info("Connected to MQTT", "mqtt::_connectMqtt");

        _mqttConnectionAttempt = 0;

        _subscribeToTopics();

        publishMqtt.connectivity = true;
        publishMqtt.meter = true;
        publishMqtt.status = true;
        publishMqtt.metadata = true;
        publishMqtt.channel = true;
        publishMqtt.generalConfiguration = true;

        return true;
    }
    else
    {
        _logger.warning(
            "Failed to connect to MQTT (%d/%d). Reason: %s. Retrying...",
            "mqtt::_connectMqtt",
            _mqttConnectionAttempt + 1,
            MQTT_MAX_CONNECTION_ATTEMPT,
            getMqttStateReason(clientMqtt.state())
        );

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;

        return false;
    }
}

void Mqtt::_temporaryDisable() {
    _logger.debug("Temporarely disabling MQTT...", "mqtt::_temporaryDisable");

    _forceDisableMqtt = true;
    _mqttConnectionFailedAt = millis();
    _mqttConnectionAttempt = 0;

    _logger.debug("MQTT temporarely disabled. Retrying connection in %d milliseconds", "mqtt::_temporaryDisable", MQTT_TEMPORARY_DISABLE_INTERVAL);
}

void Mqtt::_setCertificates() {
    _logger.debug("Setting certificates...", "mqtt::_setCertificates");

    crashMonitor.leaveBreadcrumb(CustomModule::MQTT, 0);
    _awsIotCoreCert = readEncryptedFile(CERTIFICATE_PATH);
    crashMonitor.leaveBreadcrumb(CustomModule::MQTT, 1);
    _awsIotCorePrivateKey = readEncryptedFile(PRIVATE_KEY_PATH);

    _logger.debug("Certificates set", "mqtt::_setCertificates");
}

bool Mqtt::_checkCertificates() {
    if (SPIFFS.exists(CERTIFICATE_PATH) && SPIFFS.exists(PRIVATE_KEY_PATH)) {
        _logger.debug("Certificates found", "mqtt::_checkCertificates");
        return true;
    }
    _logger.debug("Certificates not found", "mqtt::_checkCertificates");
    return false;
}

void Mqtt::_claimProcess() {
    _logger.debug("Claiming certificates...", "mqtt::_claimProcess");
    _isClaimInProgress = true;

    clientMqtt.setCallback(subscribeCallback);
    net.setCACert(aws_iot_core_cert_ca);
    net.setCertificate(aws_iot_core_cert_certclaim);
    net.setPrivateKey(aws_iot_core_cert_privateclaim);

    clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

    clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    _logger.debug("MQTT setup for claiming certificates complete", "mqtt::_claimProcess");

    int _connectionAttempt = 0;
    while (_connectionAttempt < MQTT_MAX_CONNECTION_ATTEMPT) {
        _logger.debug("Attempting to connect to MQTT for claiming certificates (%d/%d)...", "mqtt::_claimProcess", _connectionAttempt + 1, MQTT_MAX_CONNECTION_ATTEMPT);

        if (clientMqtt.connect(_deviceId.c_str())) {
            _logger.debug("Connected to MQTT for claiming certificates", "mqtt::_claimProcess");
            break;
        }

        _logger.warning(
            "Failed to connect to MQTT for claiming certificates (%d/%d). Reason: %s. Retrying...",
            "mqtt::_claimProcess",
            _connectionAttempt + 1,
            MQTT_MAX_CONNECTION_ATTEMPT,
            getMqttStateReason(clientMqtt.state())
        );

        _connectionAttempt++;
    }

    if (_connectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPT) {
        _logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", "mqtt::_claimProcess", MQTT_MAX_CONNECTION_ATTEMPT);
        return;
    }

    _subscribeProvisioningResponse();
    
    int _publishAttempt = 0;
    while (_publishAttempt < MQTT_MAX_CONNECTION_ATTEMPT) {
        _logger.debug("Attempting to publish provisioning request (%d/%d)...", "mqtt::_claimProcess", _publishAttempt + 1, MQTT_MAX_CONNECTION_ATTEMPT);

        if (_publishProvisioningRequest()) {
            _logger.debug("Provisioning request published", "mqtt::_claimProcess");
            break;
        }

        _logger.warning(
            "Failed to publish provisioning request (%d/%d). Retrying...",
            "mqtt::begin",
            _publishAttempt + 1,
            MQTT_MAX_CONNECTION_ATTEMPT
        );

        _publishAttempt++;
    }
}

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

    _setTopicConnectivity();
    _setTopicMeter();
    _setTopicStatus();
    _setTopicMetadata();
    _setTopicChannel();
    _setTopicGeneralConfiguration();

    _logger.debug("MQTT topics setup complete", "mqtt::_setupTopics");
}

void Mqtt::_setTopicConnectivity() {
    _constructMqttTopic(MQTT_TOPIC_CONNECTIVITY, _mqttTopicConnectivity);
    _logger.debug(_mqttTopicConnectivity, "mqtt::_setTopicConnectivity");
}

void Mqtt::_setTopicMeter() {
    _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter);
    _logger.debug(_mqttTopicMeter, "mqtt::_setTopicMeter");
}

void Mqtt::_setTopicStatus() {
    _constructMqttTopic(MQTT_TOPIC_STATUS, _mqttTopicStatus);
    _logger.debug(_mqttTopicStatus, "mqtt::_setTopicStatus");
}

void Mqtt::_setTopicMetadata() {
    _constructMqttTopic(MQTT_TOPIC_METADATA, _mqttTopicMetadata);
    _logger.debug(_mqttTopicMetadata, "mqtt::_setTopicMetadata");
}

void Mqtt::_setTopicChannel() {
    _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel);
    _logger.debug(_mqttTopicChannel, "mqtt::_setTopicChannel");
}

void Mqtt::_setTopicGeneralConfiguration() {
    _constructMqttTopic(MQTT_TOPIC_GENERAL_CONFIGURATION, _mqttTopicGeneralConfiguration);
    _logger.debug(_mqttTopicGeneralConfiguration, "mqtt::_setTopicGeneralConfiguration");
}

void Mqtt::_circularBufferToJson(JsonDocument* jsonDocument, CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter) {
    _logger.debug("Converting circular buffer to JSON", "mqtt::_circularBufferToJson");

    JsonArray _jsonArray = jsonDocument->to<JsonArray>();
    
    while (!payloadMeter.isEmpty()) {
        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        PayloadMeter _oldestPayloadMeter = payloadMeter.shift();

        _jsonObject["unixTime"] = _oldestPayloadMeter.unixTime;
        _jsonObject["channel"] = _oldestPayloadMeter.channel;
        _jsonObject["activePower"] = _oldestPayloadMeter.activePower;
        _jsonObject["powerFactor"] = _oldestPayloadMeter.powerFactor;
    }

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
        if (_ade7953.channelData[i].active) {
            JsonObject _jsonObject = _jsonArray.add<JsonObject>();

            _jsonObject["unixTime"] = _customTime.getUnixTime();
            _jsonObject["channel"] = i;
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

void Mqtt::_publishConnectivity(bool isOnline) {
    _logger.debug("Publishing connectivity to MQTT...", "mqtt::_publishConnectivity");

    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["connectivity"] = isOnline ? "online" : "offline";

    String _connectivityMessage;
    serializeJson(_jsonDocument, _connectivityMessage);

    if (_publishMessage(_mqttTopicConnectivity, _connectivityMessage.c_str(), true)) {publishMqtt.connectivity = false;} // Publish with retain

    _logger.debug("Connectivity published to MQTT", "mqtt::_publishConnectivity");
}

void Mqtt::_publishMeter() {
    _logger.debug("Publishing meter data to MQTT...", "mqtt::_publishMeter");

    JsonDocument _jsonDocument;
    _circularBufferToJson(&_jsonDocument, payloadMeter);

    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    if (_publishMessage(_mqttTopicMeter, _meterMessage.c_str())) {publishMqtt.meter = false;}

    _logger.debug("Meter data published to MQTT", "mqtt::_publishMeter");
}

void Mqtt::_publishStatus() {
    _logger.debug("Publishing status to MQTT...", "mqtt::_publishStatus");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["rssi"] = WiFi.RSSI();
    _jsonDocument["uptime"] = millis();
    _jsonDocument["freeHeap"] = ESP.getFreeHeap();
    _jsonDocument["freeSpiffs"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();

    String _statusMessage;
    serializeJson(_jsonDocument, _statusMessage);

    if (_publishMessage(_mqttTopicStatus, _statusMessage.c_str())) {publishMqtt.status = false;}

    _logger.debug("Status published to MQTT", "mqtt::_publishStatus");
}

void Mqtt::_publishMetadata() {
    _logger.debug("Publishing metadata to MQTT...", "mqtt::_publishMetadata");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["firmwareBuildVersion"] = FIRMWARE_BUILD_VERSION;
    _jsonDocument["firmwareBuildDate"] = FIRMWARE_BUILD_DATE;

    String _metadataMessage;
    serializeJson(_jsonDocument, _metadataMessage);

    if (_publishMessage(_mqttTopicMetadata, _metadataMessage.c_str())) {publishMqtt.metadata = false;}
    
    _logger.debug("Metadata published to MQTT", "mqtt::_publishMetadata");
}

void Mqtt::_publishChannel() {
    _logger.debug("Publishing channel data to MQTT", "mqtt::_publishChannel");

    JsonDocument _jsonChannelData;
    _ade7953.channelDataToJson(_jsonChannelData);
    
    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["data"] = _jsonChannelData;

    String _channelMessage;
    serializeJson(_jsonDocument, _channelMessage);
 
    if (_publishMessage(_mqttTopicChannel, _channelMessage.c_str())) {publishMqtt.channel = false;}

    _logger.debug("Channel data published to MQTT", "mqtt::_publishChannel");
}

void Mqtt::_publishGeneralConfiguration() {
    _logger.debug("Publishing general configuration to MQTT", "mqtt::_publishGeneralConfiguration");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();

    JsonDocument _jsonDocumentConfiguration;
    generalConfigurationToJson(generalConfiguration, _jsonDocumentConfiguration);
    _jsonDocument["generalConfiguration"] = _jsonDocumentConfiguration;

    String _generalConfigurationMessage;
    serializeJson(_jsonDocument, _generalConfigurationMessage);

    if (_publishMessage(_mqttTopicGeneralConfiguration, _generalConfigurationMessage.c_str())) {publishMqtt.generalConfiguration = false;}

    _logger.debug("General configuration published to MQTT", "mqtt::_publishGeneralConfiguration");
}

bool Mqtt::_publishProvisioningRequest() {
    _logger.debug("Publishing provisioning request to MQTT", "mqtt::_publishProvisioningRequest");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = _customTime.getUnixTime();
    _jsonDocument["firmwareVersion"] = FIRMWARE_BUILD_VERSION;

    String _provisioningRequestMessage;
    serializeJson(_jsonDocument, _provisioningRequestMessage);

    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, _topic);

    return _publishMessage(_topic, _provisioningRequestMessage.c_str());
}

bool Mqtt::_publishMessage(const char* topic, const char* message, bool retain) {
    _logger.debug(
        "Publishing message to topic %s",
        "mqtt::_publishMessage",
        topic
    );

    if (topic == nullptr || message == nullptr) {
        _logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", "mqtt::_publishMessage");
        return false;
    }

    if (!clientMqtt.connected()) {
        _logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", "mqtt::_publishMessage", getMqttStateReason(clientMqtt.state()), topic);
        return false;
    }

    if (!clientMqtt.publish(topic, message, retain)) {
        _logger.error("Failed to publish message", "mqtt::_publishMessage");
        return false;
    }

    _logger.debug("Message published: %s", "mqtt::_publishMessage", message);
    return true;
}

void Mqtt::_checkIfPublishMeterNeeded() {
    if (payloadMeter.isFull() || (millis() - _lastMillisMeterPublished) > MAX_INTERVAL_METER_PUBLISH) { // Either buffer is full or time has passed
        _logger.debug("Setting flag to publish %d meter data points", "mqtt::_checkIfPublishMeterNeeded", payloadMeter.size());

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
    if (publishMqtt.connectivity) {_publishConnectivity();}
    if (publishMqtt.meter) {_publishMeter();}
    if (publishMqtt.status) {_publishStatus();}
    if (publishMqtt.metadata) {_publishMetadata();}
    if (publishMqtt.channel) {_publishChannel();}
    if (publishMqtt.generalConfiguration) {_publishGeneralConfiguration();}
}

void Mqtt::_subscribeToTopics() {
    _logger.debug("Subscribing to topics...", "mqtt::_subscribeToTopics");

    _subscribeUpdateFirmware();
    _subscribeRestart();

    _logger.debug("Subscribed to topics", "mqtt::_subscribeToTopics");
}

void Mqtt::_subscribeUpdateFirmware() {
    _logger.debug("Subscribing to firmware update topic: %s", "mqtt::_subscribeUpdateFirmware", MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE, _topic);
    
    if (!clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to firmware update topic", "mqtt::_subscribeUpdateFirmware");
    }
}

void Mqtt::_subscribeRestart() {
    _logger.debug("Subscribing to restart topic: %s", "mqtt::_subscribeRestart", MQTT_TOPIC_SUBSCRIBE_RESTART);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, _topic);
    
    if (!clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to restart topic", "mqtt::_subscribeRestart");
    }
}

void Mqtt::_subscribeProvisioningResponse() {
    _logger.debug("Subscribing to provisioning response topic: %s", "mqtt::_subscribeProvisioningResponse", MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, _topic);
    
    if (!clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to provisioning response topic", "mqtt::_subscribeProvisioningResponse");
    }
}