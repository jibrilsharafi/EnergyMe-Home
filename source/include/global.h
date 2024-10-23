#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduino.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <CircularBuffer.hpp>

#include "crashmonitor.h"
#include "structs.h"

// Global variables are stored here

extern RestartConfiguration restartConfiguration;
extern PublishMqtt publishMqtt;

extern bool isFirstSetup;
extern bool isFirmwareUpdate;
extern bool isCrashCounterReset;

extern int currentChannel;
extern int previousChannel;

extern GeneralConfiguration generalConfiguration;
extern CustomMqttConfiguration customMqttConfiguration;
extern CrashMonitor crashMonitor;

extern WiFiClientSecure net;
extern PubSubClient clientMqtt; // These must be global to ensure proper working of MQTT

extern WiFiClient customNet;
extern PubSubClient customClientMqtt;

extern AsyncWebServer server;

extern CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;

#endif