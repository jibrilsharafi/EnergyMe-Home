#ifndef CUSTOMWIFI_H
#define CUSTOMWIFI_H

#include <Arduino.h>

#include <WiFiManager.h>
#include <ESPmDNS.h>

#include "constants.h"
#include "led.h"
#include "utils.h"

extern AdvancedLogger logger;
extern Led led;

bool setupWifi();
void wifiLoop();
void resetWifi();

bool setupMdns();

void getWifiStatus(JsonDocument& jsonDocument);
void printWifiStatus();

#endif