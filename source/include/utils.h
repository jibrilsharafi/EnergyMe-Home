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

extern const char DEFAULT_CALIBRATION_JSON[] asm("_binary_config_calibration_json_start");
extern const char DEFAULT_CHANNEL_JSON[] asm("_binary_config_channel_json_start");

JsonDocument getProjectInfo();
JsonDocument getDeviceInfo();
void restartEsp32(const char* functionName, const char* reason);
void printMeterValues(MeterValues meterValues, const char* channelLabel);
void printDeviceStatus();

JsonDocument deserializeJsonFromSpiffs(const char* path);
bool serializeJsonToSpiffs(const char* path, JsonDocument jsonDocument);

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
void setGeneralConfiguration(GeneralConfiguration generalConfiguration);
bool setGeneralConfigurationFromSpiffs();
bool saveGeneralConfigurationToSpiffs();
JsonDocument generalConfigurationToJson(GeneralConfiguration generalConfiguration);
GeneralConfiguration jsonToGeneralConfiguration(JsonDocument jsonDocument);

void applyGeneralConfiguration();
bool checkIfRebootRequiredGeneralConfiguration(GeneralConfiguration previousConfiguration, GeneralConfiguration newConfiguration);

JsonDocument getPublicLocation();
std::pair<int, int> getPublicTimezone();
void updateTimezone();

void factoryReset();
bool _duplicateFile(const char* sourcePath, const char* destinationPath);

bool isLatestFirmwareInstalled();

String getDeviceId();

#endif