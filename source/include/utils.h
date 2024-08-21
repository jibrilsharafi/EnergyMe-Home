#ifndef UTILS_H
#define UTILS_H

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <TimeLib.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "global.h"
#include "led.h"
#include "structs.h"

void getJsonProjectInfo(JsonDocument& jsonDocument);
void getJsonDeviceInfo(JsonDocument& jsonDocument);

void setRestartEsp32(const char* functionName, const char* reason);
void restartEsp32();

void checkPublishMqtt();

void printMeterValues(MeterValues meterValues, const char* channelLabel);
void printDeviceStatus();

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);

void formatAndCreateDefaultFiles();
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