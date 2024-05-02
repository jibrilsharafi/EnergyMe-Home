#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Ticker.h>
#include <HTTPClient.h>

#include "constants.h"
#include "utils.h"
#include "global.h"
#include "structs.h"

extern Logger logger;
extern CustomTime customTime;

bool setupMqtt();
void setupTopics();
void mqttLoop();
bool connectMqtt();

char* constructMqttTopic(const char* ruleName, const char* topic);

void setTopicMeter();
void setTopicStatus();
void setTopicMetadata();
void setTopicChannel();
void setTopicGeneralConfiguration();

void publishMeter();
void publishStatus();
void publishMetadata();
void publishChannel();
void publishGeneralConfiguration();

void publishMessage(const char* topic, const char* message);

JsonDocument circularBufferToJson(CircularBuffer<data::PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> &buffer);

String getPublicIp();

#endif