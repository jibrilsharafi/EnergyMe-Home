#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduino.h>

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <CircularBuffer.h>

#include "structs.h"

// Global variables are stored here

extern int currentChannel;
extern int previousChannel;

extern bool isFirstSetup;

extern GeneralConfiguration generalConfiguration;

extern PubSubClient clientMqtt; // These must be global to ensure proper working of MQTT
extern WiFiClientSecure net;

extern CircularBuffer<data::PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> payloadMeter;

#endif