#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <CircularBuffer.hpp>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "structs.h"
#include "utils.h"

extern DebugFlagsRtc debugFlagsRtc;
class Mqtt
{
public:
    Mqtt(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        PubSubClient &clientMqtt,
        WiFiClientSecure &net,
        PublishMqtt &publishMqtt,
        CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &_payloadMeter,
        RestartConfiguration &restartConfiguration
    );

    void begin();
    void loop();

private:
    bool _connectMqtt();
    void _setCertificates();
    void _claimProcess();
    void _setupMqttWithCertificates();

    void _checkIfPublishMeterNeeded();
    void _checkIfPublishStatusNeeded();
    void _checkIfPublishMonitorNeeded();
    void _checkIfPublishStatisticsNeeded();

    void _checkPublishMqtt();

    void _publishConnectivity(bool isOnline = true);
    void _publishMeter();
    void _publishStatus();
    void _publishMetadata();
    void _publishChannel();
    void _publishCrash();
    void _publishMonitor();
    void _publishGeneralConfiguration();
    void _publishStatistics();
    bool _publishProvisioningRequest();

    void _setupTopics();
    void _setTopicConnectivity();
    void _setTopicMeter();
    void _setTopicStatus();
    void _setTopicMetadata();
    void _setTopicChannel();
    void _setTopicCrash();
    void _setTopicMonitor();
    void _setTopicGeneralConfiguration();
    void _setTopicStatistics();

    bool _publishMessage(const char *topic, const char *message, bool retain = false);

    void _subscribeUpdateFirmware();
    void _subscribeRestart();
    void _subscribeEraseCertificates();
    void _subscribeProvisioningResponse();
    void _subscribeSetGeneralConfiguration();
    void _subscribeEnableDebugLogging(); 
    void _subscribeToTopics();

    void _constructMqttTopicWithRule(const char *ruleName, const char *finalTopic, char *topic, size_t topicBufferSize);
    void _constructMqttTopic(const char *finalTopic, char *topic, size_t topicBufferSize);

    void _circularBufferToJson(JsonDocument *jsonDocument, CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &buffer);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    PubSubClient &_clientMqtt;
    WiFiClientSecure &_net;
    PublishMqtt &_publishMqtt;
    CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &_payloadMeter;
    RestartConfiguration &_restartConfiguration;

    char _mqttTopicConnectivity[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicMeter[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicStatus[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicMetadata[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicChannel[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicCrash[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicMonitor[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicGeneralConfiguration[MQTT_MAX_TOPIC_LENGTH];
    char _mqttTopicStatistics[MQTT_MAX_TOPIC_LENGTH];

    unsigned long _lastMillisMqttLoop = 0;
    unsigned long _lastMillisMeterPublished = MINIMUM_TIME_BEFORE_VALID_METER; // Do not publish immediately after setup
    unsigned long _lastMillisStatusPublished = 0;
    unsigned long _lastMillisMonitorPublished = 0;
    unsigned long _lastMillisStatisticsPublished = 0;
    unsigned long _lastMillisMqttFailed = 0;
    unsigned long _mqttConnectionAttempt = 0;
    unsigned long _nextMqttConnectionAttemptMillis = 0;

    bool _isSetupDone = false;
    bool _isClaimInProgress = false;

    char _awsIotCoreCert[AWS_IOT_CORE_CERT_BUFFER_SIZE];
    char _awsIotCorePrivateKey[AWS_IOT_CORE_PRIVATE_KEY_BUFFER_SIZE];
};