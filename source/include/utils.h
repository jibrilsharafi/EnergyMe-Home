#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <TimeLib.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <AdvancedLogger.h>

#include "structs.h"
#include "constants.h"
#include "led.h"
#include "global.h"
#include "customtime.h"
#include "ade7953.h"
#include "mqtt.h"

void getJsonProjectInfo(JsonDocument& jsonDocument);
void getJsonDeviceInfo(JsonDocument& jsonDocument);

void restartEsp32(const char* functionName, const char* reason);

void printMeterValues(MeterValues meterValues, const char* channelLabel);
void printDeviceStatus();

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);

void createDefaultFiles();
void createDefaultConfigurationAde7953File();
void createDefaultGeneralConfigurationFile();
void createDefaultEnergyFile();
void createDefaultDailyEnergyFile();
void createDefaultFirmwareUpdateInfoFile();
void createDefaultFirmwareUpdateStatusFile();
void createFirstSetupFile();

bool checkAllFiles();
bool checkIfFirstSetup();

void setDefaultGeneralConfiguration();
void setGeneralConfiguration(GeneralConfiguration& generalConfiguration);
bool setGeneralConfigurationFromSpiffs();
bool saveGeneralConfigurationToSpiffs();
void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument);
void jsonToGeneralConfiguration(JsonDocument& jsonDocument, GeneralConfiguration& generalConfiguration);

void applyGeneralConfiguration();
bool checkIfRebootRequiredGeneralConfiguration(GeneralConfiguration& previousConfiguration, GeneralConfiguration& newConfiguration);

void getPublicLocation();
void getPublicTimezone();
void updateTimezone();

void factoryReset();

bool isLatestFirmwareInstalled();

String getDeviceId();

#endif