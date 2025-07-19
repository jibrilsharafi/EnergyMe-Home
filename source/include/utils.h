#pragma once

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
#include <vector>
#include <ESPmDNS.h>

#include "binaries.h"
#include "constants.h"
#include "customtime.h"
#include "crashmonitor.h"
#include "led.h"
#include "structs.h"

// Only place where the extern is used a lot
extern AdvancedLogger logger;
extern GeneralConfiguration generalConfiguration;
extern CustomTime customTime;
extern Led led;
extern RestartConfiguration restartConfiguration;
extern MainFlags mainFlags;
extern PublishMqtt publishMqtt;
extern Statistics statistics;

void getJsonProductInfo(JsonDocument& jsonDocument);
void getJsonDeviceInfo(JsonDocument& jsonDocument);

void setRestartEsp32(const char* functionName, const char* reason);
void checkIfRestartEsp32Required();
void restartEsp32();
void cleanupInterruptHandling();

void printMeterValues(MeterValues* meterValues, ChannelData* channelData);
void printDeviceStatus();
void printStatistics();

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);
void createEmptyJsonFile(const char* path);

std::vector<const char*> checkMissingFiles();
void createDefaultFilesForMissingFiles(const std::vector<const char*>& missingFiles);
bool checkAllFiles();
void createDefaultCustomMqttConfigurationFile();
void createDefaultInfluxDbConfigurationFile();
void createDefaultChannelDataFile();
void createDefaultCalibrationFile();
void createDefaultAde7953ConfigurationFile();
void createDefaultGeneralConfigurationFile();
void createDefaultEnergyFile();
void createDefaultDailyEnergyFile();
void createDefaultFirmwareUpdateInfoFile();
void createDefaultFirmwareUpdateStatusFile();

void setDefaultGeneralConfiguration();
bool setGeneralConfiguration(JsonDocument& jsonDocument);
bool setGeneralConfigurationFromSpiffs();
void saveGeneralConfigurationToSpiffs();
void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument);
bool validateGeneralConfigurationJson(JsonDocument& jsonDocument);
void applyGeneralConfiguration();

void getPublicLocation(PublicLocation* publicLocation);
void getPublicTimezone(int* gmtOffset, int* dstOffset);
void updateTimezone();

void factoryReset();
void clearAllPreferences();

bool isLatestFirmwareInstalled();
void updateJsonFirmwareStatus(const char *status, const char *reason);

void getDeviceId(char* deviceId, size_t maxLength);

void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument);

const char* getMqttStateReason(int state);

String decryptData(String encryptedData, String key);
String readEncryptedPreferences(const char* preference_key);
void writeEncryptedPreferences(const char* preference_key, const char* value);
void clearCertificates();
bool checkCertificatesExist();

// Authentication functions
void initializeAuthentication();
bool validatePassword(const String& password);
bool setAuthPassword(const String& newPassword);
bool isUsingDefaultPassword();
String generateAuthToken();
bool validateAuthToken(const String& token);
void clearAuthToken(const String& token);
void clearAllAuthTokens();
String hashPassword(const String& password);
int getActiveTokenCount();
bool canAcceptMoreTokens();

// Rate limiting functions for DoS protection
void initializeRateLimiting();
bool isIpBlocked(const String& clientIp);
void recordFailedLogin(const String& clientIp);
void recordSuccessfulLogin(const String& clientIp);
void cleanupOldRateLimitEntries();

bool setupMdns();

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds = true);