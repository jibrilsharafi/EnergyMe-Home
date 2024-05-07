#include "mqtt.h"

#include "secrets.h"

#include "ade7953.h"

extern Ade7953 ade7953;

char *mqttTopicMeter;
char *mqttTopicStatus;
char *mqttTopicMetadata;
char *mqttTopicChannel;
char *mqttTopicGeneralConfiguration;

long lastMillisPublished = millis();
long lastMillisMqttFailed = millis();
int mqttConnectionAttempt = 0;

Ticker statusTicker;

bool setupMqtt() {
    logger.log("Connecting to MQTT...", "mqtt::setupMqtt", CUSTOM_LOG_LEVEL_DEBUG);
    
    net.setCACert(MQTT_CERT_CA);
    net.setCertificate(MQTT_CERT_CRT);
    net.setPrivateKey(MQTT_CERT_PRIVATE);

    clientMqtt.setServer(MQTT_ENDPOINT, MQTT_PORT);

    clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
    clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

    if (!connectMqtt()) {
        return false;
    } 
    
    statusTicker.attach(MQTT_STATUS_PUBLISH_INTERVAL, publishStatus);

    return true;
}

void setupTopics() {
    setTopicMeter();
    setTopicStatus();
    setTopicMetadata();
    setTopicChannel();
    setTopicGeneralConfiguration();
}

void mqttLoop() {
    if (!clientMqtt.loop()) {
        if ((millis() - lastMillisMqttFailed) < MQTT_MIN_CONNECTION_INTERVAL) {
            logger.log("MQTT connection failed recently. Skipping...", "mqtt::connectMqtt", CUSTOM_LOG_LEVEL_DEBUG);
            return;
        }

        logger.log("MQTT connection lost. Reconnecting...", "mqtt::mqttLoop", CUSTOM_LOG_LEVEL_WARNING);
        if (!connectMqtt()) {
            if (mqttConnectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPT) {
                restartEsp32("mqtt::mqttLoop", "Failed to connect to MQTT and hit maximum connection attempt.");
            }
            return;
        }
    }

    if (payloadMeter.isFull() || (millis() - lastMillisPublished) > MAX_INTERVAL_PAYLOAD) {
        logger.log("Payload meter buffer is full. Publishing...", "mqtt::mqttLoop", CUSTOM_LOG_LEVEL_DEBUG);
        
        publishMeter();

        lastMillisPublished = millis();
    }
}

bool connectMqtt() {
    logger.log("MQTT client configured. Starting attempt to connect...", "mqtt::connectMqtt", CUSTOM_LOG_LEVEL_DEBUG);
    
    String _clientId = WiFi.macAddress();
    _clientId.replace(":", "");

    if (clientMqtt.connect(_clientId.c_str())) {
        logger.log("Connected to MQTT", "mqtt::connectMqtt", CUSTOM_LOG_LEVEL_INFO);

        mqttConnectionAttempt = 0;
        
        publishMetadata();
        publishChannel();
        publishStatus();
        
        return true;
    } else {
        logger.log(
            ("Failed to connect to MQTT (" + String(mqttConnectionAttempt+1) + "/" + String(MQTT_MAX_CONNECTION_ATTEMPT) + ")").c_str(), 
            "mqtt::connectMqtt", 
            CUSTOM_LOG_LEVEL_WARNING
        );
        lastMillisMqttFailed = millis();
        mqttConnectionAttempt++;
        return false;
    }
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
        WiFi.macAddress().c_str(),
        topic
    );
    return mqttTopic;
}

void setTopicMeter() {
    mqttTopicMeter = constructMqttTopic(MQTT_RULE_NAME_METER, MQTT_TOPIC_METER);
    logger.log(mqttTopicMeter, "mqtt::setTopicMeter", CUSTOM_LOG_LEVEL_DEBUG);
}

void setTopicStatus() {
    mqttTopicStatus = constructMqttTopic(MQTT_RULE_NAME_STATUS, MQTT_TOPIC_STATUS);
    logger.log(mqttTopicStatus, "mqtt::setTopicStatus", CUSTOM_LOG_LEVEL_DEBUG);
}

void setTopicMetadata() {
    mqttTopicMetadata = constructMqttTopic(MQTT_RULE_NAME_METADATA, MQTT_TOPIC_METADATA);
    logger.log(mqttTopicMetadata, "mqtt::setTopicMetadata", CUSTOM_LOG_LEVEL_DEBUG);
}

void setTopicChannel() {
    mqttTopicChannel = constructMqttTopic(MQTT_RULE_NAME_CHANNEL, MQTT_TOPIC_CHANNEL);
    logger.log(mqttTopicChannel, "mqtt::setTopicChannel", CUSTOM_LOG_LEVEL_DEBUG);
}

void setTopicGeneralConfiguration() {
    mqttTopicGeneralConfiguration = constructMqttTopic(MQTT_RULE_NAME_GENERAL_CONFIGURATION, MQTT_TOPIC_GENERAL_CONFIGURATION);
    logger.log(mqttTopicGeneralConfiguration, "mqtt::setTopicGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

JsonDocument circularBufferToJson(CircularBuffer<data::PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> &payloadMeter) {
    logger.log("Converting circular buffer to JSON", "mqtt::circularBufferToJson", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument;
    JsonArray _jsonArray = _jsonDocument.to<JsonArray>();
    
    while (!payloadMeter.isEmpty()) {
        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        data::PayloadMeter _oldestPayloadMeter = payloadMeter.shift();

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

    logger.log("Circular buffer converted to JSON. Payload:", "mqtt::circularBufferToJson", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(_jsonDocument, Serial);
    Serial.println();
    return _jsonDocument;
}

void publishMeter() {
    JsonDocument _jsonDocument = circularBufferToJson(payloadMeter);

    String _meterMessage;
    serializeJson(_jsonDocument, _meterMessage);

    publishMessage(mqttTopicMeter, _meterMessage.c_str());
}

void publishStatus() {
    JsonDocument _jsonDocument;

    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["rssi"] = WiFi.RSSI();
    _jsonObject["uptime"] = millis();
    _jsonObject["freeHeap"] = ESP.getFreeHeap();

    String _statusMessage;
    serializeJson(_jsonDocument, _statusMessage);

    publishMessage(mqttTopicStatus, _statusMessage.c_str());
}

void publishMetadata() {
    JsonDocument _jsonDocument;

    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    String _publicIp = getPublicIp();
    _jsonObject["publicIp"] = _publicIp.c_str();

    String _metadataMessage;
    serializeJson(_jsonDocument, _metadataMessage);

    publishMessage(mqttTopicMetadata, _metadataMessage.c_str());
}

void publishChannel() {
    JsonDocument _jsonDocumentChannel = ade7953.channelDataToJson();
    
    JsonDocument _jsonDocument;
    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["data"] = _jsonDocumentChannel;

    String _channelMessage;
    serializeJson(_jsonDocument, _channelMessage);
 
    publishMessage(mqttTopicChannel, _channelMessage.c_str());
}

void publishGeneralConfiguration() {
    JsonDocument _jsonDocument;

    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();
    _jsonObject["unixTime"] = customTime.getUnixTime();
    _jsonObject["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;

    String _generalConfigurationMessage;
    serializeJson(_jsonDocument, _generalConfigurationMessage);

    publishMessage(mqttTopicGeneralConfiguration, _generalConfigurationMessage.c_str());
}

void publishMessage(const char* topic, const char* message) {
    logger.log(
        ("Publishing message to topic " + String(topic)).c_str(),
        "mqtt::publishMessage",
        CUSTOM_LOG_LEVEL_DEBUG
    );

    if (!generalConfiguration.isCloudServicesEnabled) {
        logger.log("Cloud services not enabled", "mqtt::publishMessage", CUSTOM_LOG_LEVEL_INFO);
        return;
    }

    if (!clientMqtt.connected()) {
        setupMqtt();
    }

    if (!clientMqtt.publish(topic, message)) {
        logger.log("Failed to publish message", "mqtt::publishMessage", CUSTOM_LOG_LEVEL_ERROR);
    }
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
        logger.log("Error on HTTP request", "getPublicIp", CUSTOM_LOG_LEVEL_ERROR);
    }

    http.end();
    return "";
}