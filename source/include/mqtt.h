#ifndef MQTT_H
#define MQTT_H

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "global.h"
#include "structs.h"
#include "utils.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "[EnergyMe-Home] [mqtt.cpp] secrets.h not found. MQTT will not work. See secrets-sample.h"
#endif

class Mqtt
{
public:
    Mqtt(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        CustomTime &customTime);

    bool begin(String deviceId);
    void loop();

    void publishMeter();
    void publishStatus();
    void publishMetadata();
    void publishChannel();
    void publishGeneralConfiguration();

private:
    bool _connectMqtt();

    void _checkPublishMeter();
    void _checkPublishStatus();

    void _checkPublishMqtt();

    void _setupTopics();
    void _setTopicMeter();
    void _setTopicStatus();
    void _setTopicMetadata();
    void _setTopicChannel();
    void _setTopicGeneralConfiguration();

    bool _publishMessage(const char *topic, const char *message);

    void _subscribeCallback(char *topic, byte *payload, unsigned int length);
    void _subscribeUpdateFirmware();
    void _subscribeToTopics();

    void _constructMqttTopicWithRule(const char *ruleName, const char *finalTopic, char *topic);
    void _constructMqttTopic(const char *finalTopic, char *topic);

    void _circularBufferToJson(JsonDocument *jsonDocument, CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> &buffer);

    WiFiClientSecure _net;
    PubSubClient _clientMqtt;

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    CustomTime &_customTime;

    CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> _payloadMeter;

    String _deviceId;

    char _mqttTopicMeter[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicStatus[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicMetadata[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicChannel[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicGeneralConfiguration[MQTT_MAX_TOPIC_LENGTH];

    unsigned long _lastMillisMqttLoop = 0;
    unsigned long _lastMillisMeterPublished = 0;
    unsigned long _lastMillisStatusPublished = 0;
    unsigned long _lastMillisMqttFailed = 0;
    unsigned long _mqttConnectionAttempt = 0;
};

#endif