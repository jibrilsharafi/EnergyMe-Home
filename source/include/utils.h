#ifndef UTILS_H
#define UTILS_H

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <TimeLib.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include <esp_system.h>
#include <rom/rtc.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "global.h"
#include "led.h"
#include "structs.h"

void getJsonProjectInfo(JsonDocument& jsonDocument);
void getJsonDeviceInfo(JsonDocument& jsonDocument);

void setRestartEsp32(const char* functionName, const char* reason);
void checkIfRestartEsp32Required();
void restartEsp32();

void printMeterValues(MeterValues meterValues, const char* channelLabel);
void printDeviceStatus();

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);

void formatAndCreateDefaultFiles();
void createDefaultGeneralConfigurationFile();
void createDefaultEnergyFile();
void createDefaultDailyEnergyFile();
void createDefaultFirmwareUpdateInfoFile();
void createDefaultFirmwareUpdateStatusFile();
void createDefaultFirmwareRollbackFile();
void createDefaultCrashCounterFile();
void createFirstSetupFile();

bool checkAllFiles();
bool checkIfFirstSetup();

void setDefaultGeneralConfiguration();
bool setGeneralConfiguration(JsonDocument& jsonDocument);
bool setGeneralConfigurationFromSpiffs();
bool saveGeneralConfigurationToSpiffs();
void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument);
bool validateGeneralConfigurationJson(JsonDocument& jsonDocument);

void applyGeneralConfiguration();

void getPublicLocation(PublicLocation* publicLocation);
void getPublicTimezone(int* gmtOffset, int* dstOffset);
void updateTimezone();

void factoryReset();

bool isLatestFirmwareInstalled();

String getDeviceId();

const char* getMqttStateReason(int state);

void incrementCrashCounter();
void handleCrashCounter();
void crashCounterLoop();
void handleFirmwareTesting();
void firmwareTestingLoop();

String decryptData(String encryptedData, String key);
String readEncryptedFile(const char* path);

#endif