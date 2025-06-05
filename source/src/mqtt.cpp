#include "mqtt.h"
#include "structs.h" // Ensure structs.h is included for DebugFlagsRtc and DEBUG_FLAGS_RTC_SIGNATURE

static const char *TAG = "mqtt";

void subscribeCallback(const char* topic, byte *payload, unsigned int length) {
    TRACE
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE)) {
        TRACE
        JsonDocument _jsonDocument;
        if (deserializeJson(_jsonDocument, message)) {
            return;
        }
        serializeJsonToSpiffs(FW_UPDATE_INFO_JSON_PATH, _jsonDocument);
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_RESTART)) {
        TRACE
        setRestartEsp32("subscribeCallback", "Restart requested from MQTT");
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) {
        TRACE
        JsonDocument _jsonDocument;
        if (deserializeJson(_jsonDocument, message)) {
            return;
        }

        if (_jsonDocument["status"] == "success") {
            String _encryptedCertPem = _jsonDocument["encryptedCertificatePem"];
            String _encryptedPrivateKey = _jsonDocument["encryptedPrivateKey"];
            
            writeEncryptedPreferences(PREFS_KEY_CERTIFICATE, _encryptedCertPem.c_str());
            writeEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, _encryptedPrivateKey.c_str());

            // Restart MQTT connection
            setRestartEsp32("subscribeCallback", "Restarting after successful certificates provisioning");
        }
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES)) {
        TRACE
        clearCertificates();
        setRestartEsp32("subscribeCallback", "Certificates erase requested from MQTT");
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION)) {
        TRACE
        JsonDocument _jsonDocument;
        if (deserializeJson(_jsonDocument, message)) {
            return;
        }
        setGeneralConfiguration(_jsonDocument);        
    } else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING)) { 
        TRACE
        JsonDocument _jsonDocument;
        if (deserializeJson(_jsonDocument, message)) {
            return;
        }

        bool enable = false;
        if (_jsonDocument["enable"].is<bool>()) {
            enable = _jsonDocument["enable"].as<bool>();
        }

        if (enable) {
            int durationMinutes = MQTT_DEBUG_LOGGING_DEFAULT_DURATION / (60 * 1000);
            if (_jsonDocument["duration_minutes"].is<int>()) {
                durationMinutes = _jsonDocument["duration_minutes"].as<int>();
            }
            
            int durationMs = durationMinutes * 60 * 1000;            
            if (durationMs <= 0 || durationMs > MQTT_DEBUG_LOGGING_MAX_DURATION) {
                durationMs = MQTT_DEBUG_LOGGING_DEFAULT_DURATION;
            }
            
            debugFlagsRtc.enableMqttDebugLogging = true;
            debugFlagsRtc.mqttDebugLoggingDurationMillis = durationMs;
            debugFlagsRtc.mqttDebugLoggingEndTimeMillis = millis() + durationMs;
            debugFlagsRtc.signature = DEBUG_FLAGS_RTC_SIGNATURE;
        } else {
            debugFlagsRtc.enableMqttDebugLogging = false;
            debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
            debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
            debugFlagsRtc.signature = 0;
        }
    }
}

Mqtt::Mqtt(
    Ade7953 &ade7953,
    AdvancedLogger &logger,
    PubSubClient &clientMqtt,
    WiFiClientSecure &net,
    PublishMqtt &publishMqtt,
    CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter
    ) : _ade7953(ade7953), _logger(logger), _clientMqtt(clientMqtt), _net(net), _publishMqtt(publishMqtt), _payloadMeter(payloadMeter) {}

void Mqtt::begin() {
#if HAS_SECRETS
    _logger.info("Setting up MQTT...", TAG);

    _deviceId = getDeviceId();

    TRACE
    if (!checkCertificatesExist()) {
        _claimProcess();
        return;
    }
    
    TRACE
    _setupTopics();

    TRACE
    _clientMqtt.setCallback(subscribeCallback);

    TRACE
    _setCertificates();

    TRACE
    _net.setCACert(aws_iot_core_cert_ca);
    _net.setCertificate(_awsIotCoreCert.c_str());
    _net.setPrivateKey(_awsIotCorePrivateKey.c_str());

    TRACE
    _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

    TRACE
    _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    _logger.info("MQTT setup complete", TAG);

    TRACE
    _nextMqttConnectionAttemptMillis = millis(); // Try connecting immediately
    _mqttConnectionAttempt = 0;
    _connectMqtt();

    _isSetupDone = true;
#else
    _logger.info("MQTT setup skipped - no secrets available", TAG);
    // Skip MQTT setup when secrets are not available
    _isSetupDone = false;
#endif
}

void Mqtt::loop() {
#if !HAS_SECRETS
    return;
#else
    if ((millis() - _lastMillisMqttLoop) < MQTT_LOOP_INTERVAL) return;
    _lastMillisMqttLoop = millis();

    if (!WiFi.isConnected()) return;

    TRACE
    if (_isClaimInProgress) { // Only wait for certificates to be claimed
        _clientMqtt.loop();
        return;
    }

    TRACE
    if (!generalConfiguration.isCloudServicesEnabled || restartConfiguration.isRequired) {
        if (_isSetupDone && _clientMqtt.connected()) {
            _logger.info("Disconnecting MQTT", TAG);

            // Send last messages before disconnecting
            TRACE
            _clientMqtt.loop();
            _publishConnectivity(false); // Send offline connectivity as the last will message is not sent with graceful disconnect
            _publishMeter();
            _publishStatus();
            _publishMetadata();
            _publishChannel();
            _publishGeneralConfiguration();

            _clientMqtt.disconnect();

            if (!restartConfiguration.isRequired) { // Meaning that the user decided to disable cloud services
                clearCertificates();
            }

            _isSetupDone = false;
        }

        return;
    }

    if (!_isSetupDone) {begin(); return;}

    if (!_clientMqtt.connected()) {
        // Use exponential backoff timing
        if (millis() >= _nextMqttConnectionAttemptMillis) {
            _logger.info("MQTT client not connected. Attempting to reconnect...", TAG);
            _connectMqtt(); // _connectMqtt now handles setting the next attempt time
        }
    } else {
        // If connected, reset connection attempts counter for backoff calculation
        if (_mqttConnectionAttempt > 0) {
            _logger.debug("MQTT reconnected successfully after %d attempts.", TAG, _mqttConnectionAttempt);
            _mqttConnectionAttempt = 0;
        }
    }

    // Only loop if connected
    if (_clientMqtt.connected()) {
        TRACE
        _clientMqtt.loop();

        _checkIfPublishMeterNeeded();
        _checkIfPublishStatusNeeded();
        _checkIfPublishMonitorNeeded();

        TRACE
        _checkPublishMqtt();
    }
#endif
}

bool Mqtt::_connectMqtt()
{
    _logger.debug("Attempting to connect to MQTT (attempt %d)...", TAG, _mqttConnectionAttempt + 1);

    TRACE
    if (
        _clientMqtt.connect(
            _deviceId.c_str(),
            _mqttTopicConnectivity,
            MQTT_WILL_QOS,
            MQTT_WILL_RETAIN,
            MQTT_WILL_MESSAGE))
    {
        _logger.info("Connected to MQTT", TAG);

        _mqttConnectionAttempt = 0; // Reset attempt counter on success

        _subscribeToTopics();

        // The meter flag should only be set based on buffer status or time from last publish
        _publishMqtt.connectivity = true;
        _publishMqtt.status = true;
        _publishMqtt.metadata = true;
        _publishMqtt.channel = true;
        _publishMqtt.generalConfiguration = true;

        return true;
    }
    else
    {
        int _currentState = _clientMqtt.state();
        _logger.warning(
            "Failed to connect to MQTT (attempt %d). Reason: %s (%d). Retrying...",
            TAG,
            _mqttConnectionAttempt + 1,
            getMqttStateReason(_currentState),
            _currentState
        );

        _lastMillisMqttFailed = millis();
        _mqttConnectionAttempt++;

        // Check for specific errors that warrant clearing certificates
        if (_currentState == MQTT_CONNECT_BAD_CREDENTIALS || _currentState == MQTT_CONNECT_UNAUTHORIZED) {
            _logger.error("MQTT connection failed due to authorization/credentials error (%d). Erasing certificates and restarting...",
                TAG,
                _currentState);
            clearCertificates();
            setRestartEsp32(TAG, "MQTT Authentication/Authorization Error");
            _nextMqttConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts before restart
            return false; // Prevent further processing in this cycle
        }

        // Calculate next attempt time using exponential backoff
        unsigned long _backoffDelay = MQTT_INITIAL_RECONNECT_INTERVAL;
        for (int i = 0; i < _mqttConnectionAttempt - 1 && _backoffDelay < MQTT_MAX_RECONNECT_INTERVAL; ++i) {
            _backoffDelay *= MQTT_RECONNECT_MULTIPLIER;
        }
        _backoffDelay = min(_backoffDelay, (unsigned long)MQTT_MAX_RECONNECT_INTERVAL);

        _nextMqttConnectionAttemptMillis = millis() + _backoffDelay;

        _logger.info("Next MQTT connection attempt in %lu ms", TAG, _backoffDelay);

        return false;
    }
}

void Mqtt::_setCertificates() {
    _logger.debug("Setting certificates...", TAG);

    TRACE
    _awsIotCoreCert = readEncryptedPreferences(PREFS_KEY_CERTIFICATE);
    _awsIotCorePrivateKey = readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY);

    _logger.debug("Certificates set", TAG);
}

void Mqtt::_claimProcess() {
    _logger.debug("Claiming certificates...", TAG);
    _isClaimInProgress = true;

    TRACE
    _clientMqtt.setCallback(subscribeCallback);
    _net.setCACert(aws_iot_core_cert_ca);
    _net.setCertificate(aws_iot_core_cert_certclaim);
    _net.setPrivateKey(aws_iot_core_cert_privateclaim);

    _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

    _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    _logger.debug("MQTT setup for claiming certificates complete", TAG);

    int _connectionAttempt = 0;
    while (_connectionAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT) {
        _logger.debug("Attempting to connect to MQTT for claiming certificates (%d/%d)...", TAG, _connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

        TRACE
        if (_clientMqtt.connect(_deviceId.c_str())) {
            _logger.debug("Connected to MQTT for claiming certificates", TAG);
            break;
        }

        _logger.warning(
            "Failed to connect to MQTT for claiming certificates (%d/%d). Reason: %s. Retrying...",
            TAG,
            _connectionAttempt + 1,
            MQTT_CLAIM_MAX_CONNECTION_ATTEMPT,
            getMqttStateReason(_clientMqtt.state())
        );

        _connectionAttempt++;
    }

    if (_connectionAttempt >= MQTT_CLAIM_MAX_CONNECTION_ATTEMPT) {
        _logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", TAG, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
        setRestartEsp32(TAG, "Failed to claim certificates");
        return;
    }

    TRACE
    _subscribeProvisioningResponse();
    
    int _publishAttempt = 0;
    while (_publishAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT) {
        _logger.debug("Attempting to publish provisioning request (%d/%d)...", TAG, _publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

        TRACE
        if (_publishProvisioningRequest()) {
            _logger.debug("Provisioning request published", TAG);
            break;
        }

        _logger.warning(
            "Failed to publish provisioning request (%d/%d). Retrying...",
            TAG,
            _publishAttempt + 1,
            MQTT_CLAIM_MAX_CONNECTION_ATTEMPT
        );

        _publishAttempt++;
    }
}

void Mqtt::_constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topic) {
    TRACE
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

    _logger.debug("Constructing MQTT topic with rule for %s | %s", TAG, finalTopic, topic);
}

void Mqtt::_constructMqttTopic(const char* finalTopic, char* topic) {
    TRACE
    snprintf(
        topic,
        MQTT_MAX_TOPIC_LENGTH,
        "%s/%s/%s/%s",
        MQTT_TOPIC_1,
        MQTT_TOPIC_2,
        _deviceId,
        finalTopic
    );

    _logger.debug("Constructing MQTT topic for %s | %s", TAG, finalTopic, topic);
}

void Mqtt::_setupTopics() {
    _logger.debug("Setting up MQTT topics...", TAG);

    TRACE
    _setTopicConnectivity();
    _setTopicMeter();
    _setTopicStatus();
    _setTopicMetadata();
    _setTopicChannel();
    _setTopicCrash();
    _setTopicMonitor();
    _setTopicGeneralConfiguration();

    _logger.debug("MQTT topics setup complete", TAG);
}

void Mqtt::_setTopicConnectivity() { _constructMqttTopic(MQTT_TOPIC_CONNECTIVITY, _mqttTopicConnectivity); }
void Mqtt::_setTopicMeter() { _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter); }
void Mqtt::_setTopicStatus() { _constructMqttTopic(MQTT_TOPIC_STATUS, _mqttTopicStatus); }
void Mqtt::_setTopicMetadata() { _constructMqttTopic(MQTT_TOPIC_METADATA, _mqttTopicMetadata); }
void Mqtt::_setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel); }
void Mqtt::_setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash); }
void Mqtt::_setTopicMonitor() { _constructMqttTopic(MQTT_TOPIC_MONITOR, _mqttTopicMonitor); }
void Mqtt::_setTopicGeneralConfiguration() { _constructMqttTopic(MQTT_TOPIC_GENERAL_CONFIGURATION, _mqttTopicGeneralConfiguration); }

void Mqtt::_circularBufferToJson(JsonDocument* jsonDocument, CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &_payloadMeter) {
    _logger.debug("Converting circular buffer to JSON...", TAG);

    TRACE
    JsonArray _jsonArray = jsonDocument->to<JsonArray>();
    
    unsigned int _loops = 0;
    while (!_payloadMeter.isEmpty() && _loops < MAX_LOOP_ITERATIONS && generalConfiguration.sendPowerData) {
        _loops++;
        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        PayloadMeter _oldestPayloadMeter = _payloadMeter.shift();

        if (_oldestPayloadMeter.unixTime == 0) {
            _logger.debug("Payload meter has zero unixTime, skipping...", TAG);
            continue;
        }

        if (!validateUnixTime(_oldestPayloadMeter.unixTime)) {
            _logger.warning("Invalid unixTime in payload meter: %llu", TAG, _oldestPayloadMeter.unixTime);
            continue;
        }

        _jsonObject["unixTime"] = _oldestPayloadMeter.unixTime;
        _jsonObject["channel"] = _oldestPayloadMeter.channel;
        _jsonObject["activePower"] = _oldestPayloadMeter.activePower;
        _jsonObject["powerFactor"] = _oldestPayloadMeter.powerFactor;
    }

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
        if (_ade7953.channelData[i].active) {
            JsonObject _jsonObject = _jsonArray.add<JsonObject>();

            if (_ade7953.meterValues[i].lastUnixTimeMilliseconds == 0) {
                _logger.debug("Meter values for channel %d have zero unixTime, skipping...", TAG, i);
                continue;
            }

            if (!validateUnixTime(_ade7953.meterValues[i].lastUnixTimeMilliseconds)) {
                _logger.warning("Invalid unixTime in meter values for channel %d: %llu", TAG, i, _ade7953.meterValues[i].lastUnixTimeMilliseconds);
                continue;
            }

            _jsonObject["unixTime"] = _ade7953.meterValues[i].lastUnixTimeMilliseconds;
            _jsonObject["channel"] = i;
            _jsonObject["activeEnergyImported"] = _ade7953.meterValues[i].activeEnergyImported;
            _jsonObject["activeEnergyExported"] = _ade7953.meterValues[i].activeEnergyExported;
            _jsonObject["reactiveEnergyImported"] = _ade7953.meterValues[i].reactiveEnergyImported;
            _jsonObject["reactiveEnergyExported"] = _ade7953.meterValues[i].reactiveEnergyExported;
            _jsonObject["apparentEnergy"] = _ade7953.meterValues[i].apparentEnergy;
        }
    }

    JsonObject _jsonObject = _jsonArray.add<JsonObject>();

    if (_ade7953.meterValues[0].lastUnixTimeMilliseconds == 0) {
        _logger.debug("Meter values have zero unixTime, skipping...", TAG);
        return;
    }

    if (!validateUnixTime(_ade7953.meterValues[0].lastUnixTimeMilliseconds)) {
        _logger.warning("Invalid unixTime in meter values: %llu", TAG, _ade7953.meterValues[0].lastUnixTimeMilliseconds);
        return;
    }

    _jsonObject["unixTime"] = _ade7953.meterValues[0].lastUnixTimeMilliseconds;
    _jsonObject["voltage"] = _ade7953.meterValues[0].voltage;

    _logger.debug("Circular buffer converted to JSON", TAG);
}

void Mqtt::_publishConnectivity(bool isOnline) {
    _logger.debug("Publishing connectivity to MQTT...", TAG);

    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["connectivity"] = isOnline ? "online" : "offline";

    String _connectivityMessage;
    serializeJson(_jsonDocument, _connectivityMessage);

    if (_publishMessage(_mqttTopicConnectivity, _connectivityMessage.c_str(), true)) {_publishMqtt.connectivity = false;} // Publish with retain

    _logger.debug("Connectivity published to MQTT", TAG);
}

void Mqtt::_publishMeter() {
    _logger.debug("Publishing meter data to MQTT...", TAG);

    JsonDocument _jsonDocument;
    _circularBufferToJson(&_jsonDocument, _payloadMeter);

    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    if (_publishMessage(_mqttTopicMeter, _meterMessage.c_str())) {_publishMqtt.meter = false;}

    _logger.debug("Meter data published to MQTT", TAG);
}

void Mqtt::_publishStatus() {
    _logger.debug("Publishing status to MQTT...", TAG);

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["rssi"] = WiFi.RSSI();
    _jsonDocument["uptime"] = millis();
    _jsonDocument["freeHeap"] = ESP.getFreeHeap();
    _jsonDocument["freeSpiffs"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();

    String _statusMessage;
    serializeJson(_jsonDocument, _statusMessage);

    if (_publishMessage(_mqttTopicStatus, _statusMessage.c_str())) {_publishMqtt.status = false;}

    _logger.debug("Status published to MQTT", TAG);
}

void Mqtt::_publishMetadata() {
    _logger.debug("Publishing metadata to MQTT...", TAG);

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["firmwareBuildVersion"] = FIRMWARE_BUILD_VERSION;
    _jsonDocument["firmwareBuildDate"] = FIRMWARE_BUILD_DATE;

    String _metadataMessage;
    serializeJson(_jsonDocument, _metadataMessage);

    if (_publishMessage(_mqttTopicMetadata, _metadataMessage.c_str())) {_publishMqtt.metadata = false;}
    
    _logger.debug("Metadata published to MQTT", TAG);
}

void Mqtt::_publishChannel() {
    _logger.debug("Publishing channel data to MQTT", TAG);

    JsonDocument _jsonChannelData;
    _ade7953.channelDataToJson(_jsonChannelData);
    
    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["data"] = _jsonChannelData;

    String _channelMessage;
    serializeJson(_jsonDocument, _channelMessage);
 
    if (_publishMessage(_mqttTopicChannel, _channelMessage.c_str())) {_publishMqtt.channel = false;}

    _logger.debug("Channel data published to MQTT", TAG);
}

void Mqtt::_publishCrash() {
    _logger.debug("Publishing crash data to MQTT", TAG);

    TRACE
    CrashData _crashData;
    if (!CrashMonitor::getSavedCrashData(_crashData)) {
        _logger.error("Error getting crash data", TAG);
        return;
    }

    TRACE
    JsonDocument _jsonDocumentCrash;
    if (!crashMonitor.getJsonReport(_jsonDocumentCrash, _crashData)) {
        _logger.error("Error creating JSON report", TAG);
        return;
    }

    TRACE
    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["crashData"] = _jsonDocumentCrash;

    TRACE
    String _crashMessage;
    serializeJson(_jsonDocument, _crashMessage);

    TRACE
    if (_publishMessage(_mqttTopicCrash, _crashMessage.c_str())) {_publishMqtt.crash = false;}

    _logger.debug("Crash data published to MQTT", TAG);
}


void Mqtt::_publishMonitor() {
    _logger.debug("Publishing monitor data to MQTT", TAG);

    TRACE
    JsonDocument _jsonDocumentMonitor;
    crashMonitor.getJsonReport(_jsonDocumentMonitor, crashData);

    TRACE
    JsonDocument _jsonDocument;
    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
    _jsonDocument["monitorData"] = _jsonDocumentMonitor;

    TRACE
    String _crashMonitorMessage;
    serializeJson(_jsonDocument, _crashMonitorMessage);

    TRACE
    if (_publishMessage(_mqttTopicMonitor, _crashMonitorMessage.c_str())) {_publishMqtt.monitor = false;}

    _logger.debug("Monitor data published to MQTT", TAG);
}

void Mqtt::_publishGeneralConfiguration() {
    _logger.debug("Publishing general configuration to MQTT", TAG);

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

    JsonDocument _jsonDocumentConfiguration;
    generalConfigurationToJson(generalConfiguration, _jsonDocumentConfiguration);
    _jsonDocument["generalConfiguration"] = _jsonDocumentConfiguration;

    String _generalConfigurationMessage;
    serializeJson(_jsonDocument, _generalConfigurationMessage);

    if (_publishMessage(_mqttTopicGeneralConfiguration, _generalConfigurationMessage.c_str())) {_publishMqtt.generalConfiguration = false;}

    _logger.debug("General configuration published to MQTT", TAG);
}

bool Mqtt::_publishProvisioningRequest() {
    _logger.debug("Publishing provisioning request to MQTT", TAG);

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
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
        TAG,
        topic
    );

    if (topic == nullptr || message == nullptr) {
        _logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", TAG);
        return false;
    }

    if (!_clientMqtt.connected()) {
        _logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", TAG, getMqttStateReason(_clientMqtt.state()), topic);
        return false;
    }

    // Ensure time has been synced
    if (!customTime.isTimeSynched()) {
        _logger.warning("Time not synced. Skipping publishing on %s", TAG, topic);
        return false;
    }

    TRACE
    if (!_clientMqtt.publish(topic, message, retain)) {
        _logger.error("Failed to publish message on %s. MQTT client state: %s", TAG, topic, getMqttStateReason(_clientMqtt.state()));
        return false;
    }

    _logger.debug("Message published: %s", TAG, message);
    return true;
}

void Mqtt::_checkIfPublishMeterNeeded() {
    if (
        (_payloadMeter.isFull() && generalConfiguration.sendPowerData) || 
        (millis() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH
    ) { // Either buffer is full (and we are sending power data) or time has passed
        _logger.debug("Setting flag to publish %d meter data points", TAG, _payloadMeter.size());

        _publishMqtt.meter = true;
        
        _lastMillisMeterPublished = millis();
    }
}

void Mqtt::_checkIfPublishStatusNeeded() {
    if ((millis() - _lastMillisStatusPublished) > MQTT_MAX_INTERVAL_STATUS_PUBLISH) {
        _logger.debug("Setting flag to publish status", TAG);
        
        _publishMqtt.status = true;
        
        _lastMillisStatusPublished = millis();
    }
}

void Mqtt::_checkIfPublishMonitorNeeded() {
    if ((millis() - _lastMillisMonitorPublished) > MQTT_MAX_INTERVAL_CRASH_MONITOR_PUBLISH) {
        _logger.debug("Setting flag to publish crash monitor", TAG);
        
        _publishMqtt.monitor = true;
        
        _lastMillisMonitorPublished = millis();
    }
}

void Mqtt::_checkPublishMqtt() {
    if (_publishMqtt.connectivity) {_publishConnectivity();}
    if (_publishMqtt.meter) {_publishMeter();}
    if (_publishMqtt.status) {_publishStatus();}
    if (_publishMqtt.metadata) {_publishMetadata();}
    if (_publishMqtt.channel) {_publishChannel();}
    if (_publishMqtt.crash) {_publishCrash();}
    if (_publishMqtt.monitor) {_publishMonitor();}
    if (_publishMqtt.generalConfiguration) {_publishGeneralConfiguration();}
}

void Mqtt::_subscribeToTopics() {
    _logger.debug("Subscribing to topics...", TAG);

    TRACE
    _subscribeUpdateFirmware();
    _subscribeRestart();
    _subscribeEraseCertificates();
    _subscribeSetGeneralConfiguration();
    _subscribeEnableDebugLogging(); 

    _logger.debug("Subscribed to topics", TAG);
}

void Mqtt::_subscribeUpdateFirmware() {
    _logger.debug("Subscribing to firmware update topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to firmware update topic", TAG);
    }
}

void Mqtt::_subscribeRestart() {
    _logger.debug("Subscribing to restart topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_RESTART);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to restart topic", TAG);
    }
}

void Mqtt::_subscribeEraseCertificates() {
    _logger.debug("Subscribing to erase certificates topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to erase certificates topic", TAG);
    }
}

void Mqtt::_subscribeSetGeneralConfiguration() {
    _logger.debug("Subscribing to set general configuration topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to set general configuration topic", TAG);
    }
}

void Mqtt::_subscribeEnableDebugLogging() { 
    _logger.debug("Subscribing to enable debug logging topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to enable debug logging topic", TAG);
    }
}

void Mqtt::_subscribeProvisioningResponse() {
    _logger.debug("Subscribing to provisioning response topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE);
    char _topic[MQTT_MAX_TOPIC_LENGTH];
    _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, _topic);
    
    if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
        _logger.warning("Failed to subscribe to provisioning response topic", TAG);
    }
}