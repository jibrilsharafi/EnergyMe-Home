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

class Mqtt
{
public:
    Mqtt(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        CustomTime &customTime);

    void begin();
    void loop();

private:
    bool _connectMqtt();
    void _setCertificates();
    bool _checkCertificates();
    void _claimProcess();

    void _checkIfPublishMeterNeeded();
    void _checkIfPublishStatusNeeded();

    void _checkPublishMqtt();

    void _publishConnectivity(bool isOnline = true);
    void _publishMeter();
    void _publishStatus();
    void _publishMetadata();
    void _publishChannel();
    void _publishGeneralConfiguration();
    bool _publishProvisioningRequest();

    void _setupTopics();
    void _setTopicConnectivity();
    void _setTopicMeter();
    void _setTopicStatus();
    void _setTopicMetadata();
    void _setTopicChannel();
    void _setTopicGeneralConfiguration();

    bool _publishMessage(const char *topic, const char *message, bool retain = false);

    void _subscribeUpdateFirmware();
    void _subscribeRestart();
    void _subscribeProvisioningResponse();
    void _subscribeToTopics();

    void _constructMqttTopicWithRule(const char *ruleName, const char *finalTopic, char *topic);
    void _constructMqttTopic(const char *finalTopic, char *topic);

    void _circularBufferToJson(JsonDocument *jsonDocument, CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> &buffer);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    CustomTime &_customTime;

    CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> _payloadMeter;

    String _deviceId;

    char _mqttTopicConnectivity[MQTT_MAX_TOPIC_LENGTH];
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

    bool _isSetupDone = false;
    bool _isClaimInProgress = false;

    void _temporaryDisable();
    bool _forceDisableMqtt = false;
    unsigned long _mqttConnectionFailedAt = 0;

    String _awsIotCoreCert;
    String _awsIotCorePrivateKey;
};

#endif