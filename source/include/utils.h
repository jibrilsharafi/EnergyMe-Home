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
#include <esp_mac.h> // For the MAC address
#include <ESPmDNS.h>
#include <WiFi.h>

#include "binaries.h"
#include "constants.h"
#include "customtime.h"
#include "crashmonitor.h"
#include "led.h"
#include "structs.h"

// Only place where the extern is used a lot
extern AdvancedLogger logger;
extern GeneralConfiguration generalConfiguration;
extern RestartConfiguration restartConfiguration;
extern MainFlags mainFlags;
extern PublishMqtt publishMqtt;

void getJsonProductInfo(JsonDocument& jsonDocument);
void getJsonDeviceInfo(JsonDocument& jsonDocument);

void setRestartEsp32(const char* functionName, const char* reason);
void checkIfRestartEsp32Required();
void restartEsp32();

void printMeterValues(MeterValues* meterValues, ChannelData* channelData);
void printDeviceStatus();
void updateStatistics();
void printStatistics();

bool safeSerializeJson(JsonDocument& jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);
bool deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
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

void factoryReset();
void clearAllPreferences();

bool isLatestFirmwareInstalled();
void updateJsonFirmwareStatus(const char *status, const char *reason);

void getDeviceId(char* deviceId, size_t maxLength);

void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument);

const char* getMqttStateReason(int state);

void decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize);
void readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* value, size_t valueSize);
void writeEncryptedPreferences(const char* preference_key, const char* value);
void clearCertificates();
bool checkCertificatesExist();

// Authentication functions
void initializeAuthentication();
bool validatePassword(const char* password);
bool setAuthPassword(const char* newPassword);
bool isUsingDefaultPassword();
void generateAuthToken(char* token, size_t tokenSize);
bool validateAuthToken(const char* token);
void clearAuthToken(const char* token);
void clearAllAuthTokens();
void hashPassword(const char* password, char* hashedPassword, size_t hashedPasswordSize);
int getActiveTokenCount();
bool canAcceptMoreTokens();

// Rate limiting functions for DoS protection
void initializeRateLimiting();
bool isIpBlocked(const char* clientIp);
void recordFailedLogin(const char* clientIp);
void recordSuccessfulLogin(const char* clientIp);
void cleanupOldRateLimitEntries();

bool setupMdns();

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds = true);