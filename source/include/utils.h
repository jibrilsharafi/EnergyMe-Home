#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <TimeLib.h>
#include <Preferences.h>
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
#include "globals.h"

// New system info functions
void populateSystemStaticInfo(SystemStaticInfo& info);
void populateSystemDynamicInfo(SystemDynamicInfo& info);
void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument& doc);
void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument& doc);

// Convenience functions for JSON API endpoints
void getJsonDeviceStaticInfo(JsonDocument& doc);
void getJsonDeviceDynamicInfo(JsonDocument& doc);

// Legacy functions (for backward compatibility)
void populateSystemInfo(SystemInfo& systemInfo);
void systemInfoToJson(JsonDocument& jsonDocument);

// Preferences utilities for configuration storage
namespace PreferencesConfig {    
    // ADE7953 configuration
    bool setSampleTime(uint32_t sampleTime);
    uint32_t getSampleTime();
    bool setVoltageGain(uint32_t gain);
    uint32_t getVoltageGain();
    bool setCurrentGainA(uint32_t gain);
    uint32_t getCurrentGainA();
    bool setCurrentGainB(uint32_t gain);
    uint32_t getCurrentGainB();
    
    // Channel configuration
    bool setChannelActive(uint8_t channel, bool active);
    bool getChannelActive(uint8_t channel);
    bool setChannelLabel(uint8_t channel, const char* label);
    bool getChannelLabel(uint8_t channel, char* buffer, size_t bufferSize);
    bool setChannelPhase(uint8_t channel, uint8_t phase);
    uint8_t getChannelPhase(uint8_t channel);
    
    // Custom MQTT configuration // TODO: make custom
    bool setMqttEnabled(bool enabled);
    bool getMqttEnabled();
    bool setMqttServer(const char* server);
    bool getMqttServer(char* buffer, size_t bufferSize);
    bool setMqttPort(uint16_t port);
    uint16_t getMqttPort();
    bool setMqttUsername(const char* username);
    bool getMqttUsername(char* buffer, size_t bufferSize);
    bool setMqttPassword(const char* password);
    bool getMqttPassword(char* buffer, size_t bufferSize);
    
    // Authentication functions
    bool setWebPassword(const char* password);
    bool getWebPassword(char* buffer, size_t bufferSize);
    bool resetWebPassword();
    bool validatePasswordStrength(const char* password);
    
    // Utility functions
    bool hasConfiguration(const char* prefsNamespace);

    // Firmware
    bool setFirmwareUpdatesVersion(const char* version);
    bool getFirmwareUpdatesVersion(char* buffer, size_t bufferSize);
    bool setFirmwareUpdatesUrl(const char* url);
    bool getFirmwareUpdatesUrl(char* buffer, size_t bufferSize);

    // MQTT
    bool setCloudServicesEnabled(bool enabled);
    bool getCloudServicesEnabled();
    bool setSendPowerData(bool enabled);
    bool getSendPowerData();
}

void setRestartEsp32(const char* functionName, const char* reason);
void checkIfRestartEsp32Required();
void restartEsp32();

void printMeterValues(MeterValues* meterValues, ChannelData* channelData);
void printDeviceStatus();
void printDeviceStatusStatic();
void printDeviceStatusDynamic();
void updateStatistics();
void printStatistics();

bool safeSerializeJson(JsonDocument& jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError = false);
bool loadConfigFromPreferences(const char* configType, JsonDocument& jsonDocument);
bool saveConfigToPreferences(const char* configType, JsonDocument& jsonDocument);
// Legacy SPIFFS functions for backward compatibility during transition
bool deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument);
bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument);
void createEmptyJsonFile(const char* path);

std::vector<const char*> checkMissingFiles();
void createDefaultFilesForMissingFiles(const std::vector<const char*>& missingFiles);
bool checkAllFiles();
void createDefaultCustomMqttConfigurationFile();
void createDefaultChannelDataFile();
void createDefaultCalibrationFile();
void createDefaultAde7953ConfigurationFile();
void createDefaultEnergyFile();
void createDefaultDailyEnergyFile();

bool getPublicLocation(PublicLocation* publicLocation);
bool getPublicTimezone(int* gmtOffset, int* dstOffset);

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

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds = true);