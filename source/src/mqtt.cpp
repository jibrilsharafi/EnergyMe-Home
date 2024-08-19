#include "mqtt.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "[EnergyMe-Home] [mqtt.cpp] secrets.h not found. MQTT will not work. See secrets-sample.h"
#endif

#include "ade7953.h"

extern Ade7953 ade7953;

char *mqttTopicMeter = nullptr;
char *mqttTopicStatus = nullptr;
char *mqttTopicMetadata = nullptr;
char *mqttTopicChannel = nullptr;
char *mqttTopicGeneralConfiguration = nullptr;

unsigned long lastMillisPublished = millis();
unsigned long lastMillisMqttFailed = millis();
unsigned long lastMillisMqttLoop = millis();
unsigned long mqttConnectionAttempt = 0;

Ticker publishStatusTicker;
bool publishStatusFlag = false;

#ifdef ENERGYME_HOME_SECRETS_H

bool setupMqtt() {
    logger.debug("Connecting to MQTT...", "mqtt::setupMqtt");
    
    setupTopics();
    clientMqtt.setCallback(subscribeCallback);

    net.setCACert(AWS_IOT_CORE_CERT_CA);
    net.setCertificate(AWS_IOT_CORE_CERT_CRT);
    net.setPrivateKey(AWS_IOT_CORE_CERT_PRIVATE);

    clientMqtt.setServer(AWS_IOT_CORE_MQTT_ENDPOINT, AWS_IOT_CORE_PORT);

    clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    if (!connectMqtt()) {
        return false;
    }
    
    publishStatusTicker.attach(MQTT_STATUS_PUBLISH_INTERVAL, []() { publishStatusFlag = true; }); // Inside ticker callbacks, only flags should be set

    return true;
}
#else

bool setupMqtt() {
    logger.debug("Secrets not available. MQTT setup failed.", "mqtt::setupMqtt");
    return false;
}

#endif

#ifdef ENERGYME_HOME_SECRETS_H

void mqttLoop() {
    if (!generalConfiguration.isCloudServicesEnabled) {
        logger.verbose("Cloud services not enabled. Skipping...", "mqtt::mqttLoop");
        return;
    }

    if ((millis() - lastMillisMqttLoop) > MQTT_LOOP_INTERVAL) {
        lastMillisMqttLoop = millis();
        
        if (!clientMqtt.loop()) {
            if ((millis() - lastMillisMqttFailed) < MQTT_MIN_CONNECTION_INTERVAL) {
                logger.verbose("MQTT connection failed recently. Skipping...", "mqtt::connectMqtt");
                return;
            }

            logger.warning("MQTT connection lost. Reconnecting...", "mqtt::mqttLoop");
            if (!connectMqtt()) {
                if (mqttConnectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPT) {
                    restartEsp32("mqtt::mqttLoop", "Failed to connect to MQTT and hit maximum connection attempt");
                }
                return;
            }
        }

        if (payloadMeter.isFull() || (millis() - lastMillisPublished) > MAX_INTERVAL_PAYLOAD) {
            logger.debug("Payload meter buffer is full or enough time has passed. Publishing...", "mqtt::mqttLoop");
            
            publishMeter();

            lastMillisPublished = millis();
        }

        if (publishStatusFlag) {
            publishStatus();
            publishStatusFlag = false;
        }
    }
}

#else

void mqttLoop() {
    logger.debug("Secrets not available. MQTT loop failed", "mqtt::mqttLoop");
}

#endif

bool connectMqtt() { //TODO: Handle continuous connection/reconnection
    logger.debug("MQTT client configured. Starting attempt to connect...", "mqtt::connectMqtt");
    
    if (clientMqtt.connect(getDeviceId().c_str())) {
        logger.info("Connected to MQTT", "mqtt::connectMqtt");

        mqttConnectionAttempt = 0;
        
        publishMetadata();
        publishChannel();
        publishStatus();

        subscribeToTopics();
        
        return true;
    } else {
        logger.warning(
            "Failed to connect to MQTT (%d/%d)",
            "mqtt::connectMqtt",
            mqttConnectionAttempt+1,
            MQTT_MAX_CONNECTION_ATTEMPT
        );
        lastMillisMqttFailed = millis();
        mqttConnectionAttempt++;
        return false;
    }
}

#ifdef ENERGYME_HOME_SECRETS_H

void setupTopics() {
    setTopicMeter();
    setTopicStatus();
    setTopicMetadata();
    setTopicChannel();
    setTopicGeneralConfiguration();
}

char* constructMqttTopic(const char* ruleName, const char* topic) {
    char* mqttTopic = new char[MAX_MQTT_TOPIC_LENGTH];
    snprintf(
        mqttTopic,
        MAX_MQTT_TOPIC_LENGTH,
        "%s/%s/%s/%s/%s/%s",
        MQTT_BASIC_INGEST,
        ruleName,
        MQTT_TOPIC_1,
        MQTT_TOPIC_2,
        getDeviceId().c_str(),
        topic
    );
    return mqttTopic;
}

void setTopicMeter() {
    mqttTopicMeter = constructMqttTopic(MQTT_RULE_NAME_METER, MQTT_TOPIC_METER);
    logger.debug(mqttTopicMeter, "mqtt::setTopicMeter");
}

void setTopicStatus() {
    mqttTopicStatus = constructMqttTopic(MQTT_RULE_NAME_STATUS, MQTT_TOPIC_STATUS);
    logger.debug(mqttTopicStatus, "mqtt::setTopicStatus");
}

void setTopicMetadata() {
    mqttTopicMetadata = constructMqttTopic(MQTT_RULE_NAME_METADATA, MQTT_TOPIC_METADATA);
    logger.debug(mqttTopicMetadata, "mqtt::setTopicMetadata");
}

void setTopicChannel() {
    mqttTopicChannel = constructMqttTopic(MQTT_RULE_NAME_CHANNEL, MQTT_TOPIC_CHANNEL);
    logger.debug(mqttTopicChannel, "mqtt::setTopicChannel");
}

void setTopicGeneralConfiguration() {
    mqttTopicGeneralConfiguration = constructMqttTopic(MQTT_RULE_NAME_GENERAL_CONFIGURATION, MQTT_TOPIC_GENERAL_CONFIGURATION);
    logger.debug(mqttTopicGeneralConfiguration, "mqtt::setTopicGeneralConfiguration");
}

#endif

JsonDocument circularBufferToJson(CircularBuffer<PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> &payloadMeter) {
    logger.debug("Converting circular buffer to JSON", "mqtt::circularBufferToJson");

    JsonDocument _jsonDocument;
    JsonArray _jsonArray = _jsonDocument.to<JsonArray>();
    
    while (!payloadMeter.isEmpty()) {
        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        PayloadMeter _oldestPayloadMeter = payloadMeter.shift();

        _jsonObject["channel"] = _oldestPayloadMeter.channel;
        _jsonObject["unixTime"] = _oldestPayloadMeter.unixTime;
        _jsonObject["activePower"] = _oldestPayloadMeter.activePower;
        _jsonObject["powerFactor"] = _oldestPayloadMeter.powerFactor;
    }

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
        if (ade7953.channelData[i].active) {
            JsonObject _jsonObject = _jsonArray.add<JsonObject>();

            _jsonObject["channel"] = i;
            _jsonObject["unixTime"] = customTime.getUnixTime();
            _jsonObject["activeEnergy"] = ade7953.meterValues[i].activeEnergy;
            _jsonObject["reactiveEnergy"] = ade7953.meterValues[i].reactiveEnergy;
            _jsonObject["apparentEnergy"] = ade7953.meterValues[i].apparentEnergy;
        }
    }

    JsonObject _jsonObject = _jsonArray.add<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["voltage"] = ade7953.meterValues[0].voltage;

    logger.debug("Circular buffer converted to JSON", "mqtt::circularBufferToJson");
    return _jsonDocument;
}

void publishMeter() {
    logger.debug("Publishing meter data to MQTT", "mqtt::publishMeter");

    JsonDocument _jsonDocument = circularBufferToJson(payloadMeter);

    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    publishMessage(mqttTopicMeter, _meterMessage.c_str());
    logger.debug("Meter data published to MQTT", "mqtt::publishMeter");
}

void publishStatus() {
    logger.debug("Publishing status to MQTT", "mqtt::publishStatus");

    JsonDocument _jsonDocument;

    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["rssi"] = WiFi.RSSI();
    _jsonObject["uptime"] = millis();
    _jsonObject["freeHeap"] = ESP.getFreeHeap();
    _jsonObject["freeSpiffs"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();

    String _statusMessage;
    serializeJson(_jsonDocument, _statusMessage);

    publishMessage(mqttTopicStatus, _statusMessage.c_str());
    logger.debug("Status published to MQTT", "mqtt::publishStatus");
}

void publishMetadata() {
    logger.debug("Publishing metadata to MQTT", "mqtt::publishMetadata");

    JsonDocument _jsonDocument;

    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    String _publicIp = getPublicIp();
    _jsonObject["publicIp"] = _publicIp.c_str();
    _jsonObject["firmwareVersion"] = FIRMWARE_BUILD_VERSION;

    String _metadataMessage;
    serializeJson(_jsonDocument, _metadataMessage);

    publishMessage(mqttTopicMetadata, _metadataMessage.c_str());
    logger.debug("Metadata published to MQTT", "mqtt::publishMetadata");
}

void publishChannel() {
    logger.debug("Publishing channel data to MQTT", "mqtt::publishChannel");

    JsonDocument _jsonDocumentChannel = ade7953.channelDataToJson();
    
    JsonDocument _jsonDocument;
    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["data"] = _jsonDocumentChannel;

    String _channelMessage;
    serializeJson(_jsonDocument, _channelMessage);
 
    publishMessage(mqttTopicChannel, _channelMessage.c_str());
    logger.debug("Channel data published to MQTT", "mqtt::publishChannel");
}

void publishGeneralConfiguration() {
    logger.debug("Publishing general configuration to MQTT", "mqtt::publishGeneralConfiguration");

    JsonDocument _jsonDocument;

    _jsonDocument["unixTime"] = customTime.getUnixTime();
    _jsonDocument["generalConfiguration"] = generalConfigurationToJson(generalConfiguration);

    String _generalConfigurationMessage;
    serializeJson(_jsonDocument, _generalConfigurationMessage);

    publishMessage(mqttTopicGeneralConfiguration, _generalConfigurationMessage.c_str());
    logger.debug("General configuration published to MQTT", "mqtt::publishGeneralConfiguration");
}

void publishMessage(const char* topic, const char* message) {
    if (topic == nullptr || message == nullptr) {
        logger.debug("Null pointer passed, meaning the CloudServices are likely not enabled", "mqtt::publishMessage");
        return;
    }

    logger.debug(
        "Publishing message to topic %s",
        "mqtt::publishMessage",
        topic
    );

    if (!generalConfiguration.isCloudServicesEnabled) {
        logger.verbose("Cloud services not enabled. Skipping...", "mqtt::publishMessage");
        return;
    }

    if (!clientMqtt.connected()) {
        logger.warning("MQTT client not connected. Skipping...", "mqtt::publishMessage");
    }

    if (!clientMqtt.publish(topic, message)) {
        logger.error("Failed to publish message", "mqtt::publishMessage");
    }
}

void subscribeCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    logger.debug("Message arrived: %s", "mqtt::subscribeCallback", message.c_str());

    if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE)) {
        logger.info("Firmware update received", "mqtt::subscribeCallback");

        File _file = SPIFFS.open(FIRMWARE_UPDATE_INFO_PATH, FILE_WRITE);
        if (!_file) {
            logger.error("Failed to open file for writing: %s", "mqtt::subscribeCallback", FIRMWARE_UPDATE_INFO_PATH);
            return;
        }

        _file.print(message);
        _file.close();
    } else {
        logger.info("Unknown topic message received: %s | %s", "mqtt::subscribeCallback", topic, message.c_str());
        return;
    }
}

void subscribeToTopics() {
    logger.debug("Subscribing to topics...", "mqtt::subscribeToTopics");

    subscribeUpdateFirmware();

    logger.debug("Subscribed to topics", "mqtt::subscribeToTopics");
}

void subscribeUpdateFirmware() {
    if (!clientMqtt.subscribe(getSpecificDeviceIdTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE))) {
        logger.error("Failed to subscribe to firmware update topic", "mqtt::subscribeUpdateFirmware");
    }
}

const char* getSpecificDeviceIdTopic(const char* lastTopic) {
    static char topic[MAX_MQTT_TOPIC_LENGTH];
    snprintf(
        topic,
        MAX_MQTT_TOPIC_LENGTH,
        "%s/%s/%s/%s",
        MQTT_TOPIC_1,
        MQTT_TOPIC_2,
        getDeviceId().c_str(),
        lastTopic
    );

    logger.debug("Topic generated: %s", "mqtt::getSpecificDeviceIdTopic", topic);

    return topic;
}

String getPublicIp() {
    HTTPClient http;

    http.begin(PUBLIC_IP_ENDPOINT);
    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            payload.trim();
            return payload;
        }
    } else {
        logger.error("Error on HTTP request", "mqtt::getPublicIp");
    }

    http.end();
    return "";
}