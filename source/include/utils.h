#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <TimeLib.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Ticker.h>

#include "structs.h"
#include "constants.h"
#include "led.h"
#include "global.h"
#include "customtime.h"
#include "logger.h"

JsonDocument getDeviceStatus();
void restartEsp32(const char* functionName, const char* reason);
void printMeterValues(MeterValues meterValues, const char* channelLabel);
void printDeviceStatus();

JsonDocument deserializeJsonFromSpiffs(const char* path);
bool serializeJsonToSpiffs(const char* path, JsonDocument jsonDocument);

bool checkIfFirstSetup();
void logFirstSetupComplete();

void setDefaultGeneralConfiguration();
void setGeneralConfiguration(GeneralConfiguration generalConfiguration);
bool setGeneralConfigurationFromSpiffs();
bool saveGeneralConfigurationToSpiffs();
JsonDocument generalConfigurationToJson(GeneralConfiguration generalConfiguration);
GeneralConfiguration jsonToGeneralConfiguration(JsonDocument jsonDocument);

void clearMemory();

#endif