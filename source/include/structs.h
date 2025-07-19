#pragma once

#include <Arduino.h>

#include "constants.h"

// Enumeration for different types of ADE7953 interrupts
enum class Ade7953InterruptType {
  NONE,           // No interrupt or unknown
  CYCEND,         // Line cycle end - normal meter reading
  RESET,          // Device reset detected
  CRC_CHANGE,     // CRC register change detected
  OTHER           // Other interrupts (SAG, etc.)
};

struct MainFlags {
  bool isFirmwareUpdate;
  bool isCrashCounterReset;
  bool blockLoop;
  
  MainFlags() : isFirmwareUpdate(false), isCrashCounterReset(false), blockLoop(false) {}
};

struct Statistics {
  unsigned long ade7953TotalInterrupts;
  unsigned long ade7953TotalHandledInterrupts;
  unsigned long ade7953ReadingCount;
  unsigned long ade7953ReadingCountFailure;

  unsigned long mqttMessagesPublished;
  unsigned long mqttMessagesPublishedError;
  
  unsigned long customMqttMessagesPublished;
  unsigned long customMqttMessagesPublishedError;
  
  unsigned long modbusRequests;
  unsigned long modbusRequestsError;
  
  unsigned long influxdbUploadCount;
  unsigned long influxdbUploadCountError;

  unsigned long wifiConnection;
  unsigned long wifiConnectionError;

  Statistics() 
    : ade7953TotalInterrupts(0), ade7953TotalHandledInterrupts(0), ade7953ReadingCount(0), ade7953ReadingCountFailure(0), 
    mqttMessagesPublished(0), customMqttMessagesPublished(0), modbusRequests(0), modbusRequestsError(0), 
    influxdbUploadCount(0), influxdbUploadCountError(0), wifiConnection(0), wifiConnectionError(0) {}
};

struct DebugFlagsRtc {
    bool enableMqttDebugLogging;
    unsigned long mqttDebugLoggingDurationMillis;
    unsigned long mqttDebugLoggingEndTimeMillis;
    unsigned int signature;
    // Since this struct will be used in an RTC_NOINIT_ATTR, we cannot initialize it in the constructor
};

enum Phase : int {
    PHASE_1 = 1,
    PHASE_2 = 2,
    PHASE_3 = 3,
};

enum Channel : int {
    CHANNEL_A,
    CHANNEL_B,
};

enum ChannelNumber : int {
  CHANNEL_INVALID = -1,
  CHANNEL_0 = 0,
  CHANNEL_1 = 1,
  CHANNEL_2 = 2,
  CHANNEL_3 = 3,
  CHANNEL_4 = 4,
  CHANNEL_5 = 5,
  CHANNEL_6 = 6,
  CHANNEL_7 = 7,
  CHANNEL_8 = 8,
  CHANNEL_9 = 9,
  CHANNEL_10 = 10,
  CHANNEL_11 = 11,
  CHANNEL_12 = 12,
  CHANNEL_13 = 13,
  CHANNEL_14 = 14,
  CHANNEL_15 = 15,
  CHANNEL_16 = 16,
  CHANNEL_COUNT = 17
};

enum Measurement : int {
    VOLTAGE,
    CURRENT,
    ACTIVE_POWER,
    REACTIVE_POWER,
    APPARENT_POWER,
    POWER_FACTOR,
};

/*
 * Struct to hold the real-time meter values for a specific channel
  * Contains:
  * - voltage: Voltage in Volts
  * - current: Current in Amperes
  * - activePower: Active power in Watts
  * - reactivePower: Reactive power in VAR
  * - apparentPower: Apparent power in VA
  * - powerFactor: Power factor (-1 to 1, where negative values indicate capacitive load while positive values indicate inductive load)
  * - activeEnergyImported: Active energy imported in Wh
  * - activeEnergyExported: Active energy exported in Wh
  * - reactiveEnergyImported: Reactive energy imported in VArh
  * - reactiveEnergyExported: Reactive energy exported in VArh
  * - apparentEnergy: Apparent energy in VAh (only absolute value)
  * - lastUnixTimeMilliseconds: Last time the values were updated in milliseconds since epoch. Useful for absolute time tracking
 */
struct MeterValues
{
  float voltage;
  float current;
  float activePower;
  float reactivePower;
  float apparentPower;
  float powerFactor;
  float activeEnergyImported;
  float activeEnergyExported;
  float reactiveEnergyImported;
  float reactiveEnergyExported;
  float apparentEnergy;
  unsigned long long lastUnixTimeMilliseconds;
  unsigned long lastMillis;

  MeterValues()
    : voltage(230.0), current(0.0f), activePower(0.0f), reactivePower(0.0f), apparentPower(0.0f), powerFactor(0.0f),
      activeEnergyImported(0.0f), activeEnergyExported(0.0f), reactiveEnergyImported(0.0f), 
      reactiveEnergyExported(0.0f), apparentEnergy(0.0f), lastUnixTimeMilliseconds(0), lastMillis(0) {}
};

struct PayloadMeter
{
  int channel;
  unsigned long long unixTime;
  float activePower;
  float powerFactor;

  PayloadMeter() : channel(0), unixTime(0), activePower(0.0f), powerFactor(0.0f) {}

  PayloadMeter(int channel, unsigned long long unixTime, float activePower, float powerFactor)
      : channel(channel), unixTime(unixTime), activePower(activePower), powerFactor(powerFactor) {}
};

struct CalibrationValues
{
  char label[CALIBRATION_LABEL_BUFFER_SIZE];
  float vLsb;
  float aLsb;
  float wLsb;
  float varLsb;
  float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CalibrationValues()
    : vLsb(1.0), aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {
      snprintf(label, sizeof(label), "Calibration");
    }
};


struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  char label[CHANNEL_LABEL_BUFFER_SIZE];
  Phase phase;
  CalibrationValues calibrationValues;

  ChannelData()
    : index(0), active(false), reverse(false), phase(PHASE_1), calibrationValues(CalibrationValues()) {
      snprintf(label, sizeof(label), "Channel");
    }
};

// Used to track consecutive zero energy readings for channel 0
struct ChannelState {
    unsigned long consecutiveZeroCount = 0;
};

struct GeneralConfiguration
{
  bool isCloudServicesEnabled;
  int gmtOffset;
  int dstOffset;
  int ledBrightness;
  bool sendPowerData; // Flag to control sending of power data. This can only be modified via MQTT

  GeneralConfiguration() : isCloudServicesEnabled(DEFAULT_IS_CLOUD_SERVICES_ENABLED), gmtOffset(DEFAULT_GMT_OFFSET), dstOffset(DEFAULT_DST_OFFSET), ledBrightness(DEFAULT_LED_BRIGHTNESS), sendPowerData(DEFAULT_SEND_POWER_DATA) {} // Updated constructor
};

struct PublicLocation
{
  char country[COUNTRY_BUFFER_SIZE];
  char city[CITY_BUFFER_SIZE];
  char latitude[LATITUDE_BUFFER_SIZE];
  char longitude[LONGITUDE_BUFFER_SIZE];

  PublicLocation() : country("Unknown"), city("Unknown"), latitude("45.0"), longitude("9.0") {} // Default to Milan coordinates
};

struct RestartConfiguration
{
  bool isRequired;
  unsigned long requiredAt;
  char functionName[FUNCTION_NAME_BUFFER_SIZE];
  char reason[REASON_BUFFER_SIZE];

  RestartConfiguration() : isRequired(false), requiredAt(0xFFFFFFFF) {
    snprintf(functionName, sizeof(functionName), "Unknown");
    snprintf(reason, sizeof(reason), "Unknown");
  }
};

struct PublishMqtt
{
  bool connectivity;
  bool meter;
  bool status;
  bool metadata;
  bool channel;
  bool crash;
  bool monitor;
  bool generalConfiguration;
  bool statistics;

  // Set default to true to publish everything on first connection (except meter which needs to gather data first and crash which is needed only in case of crash)
  PublishMqtt() : connectivity(true), meter(false), status(true), metadata(true), channel(true), crash(false), monitor(true), generalConfiguration(true), statistics(true) {}
};

struct CustomMqttConfiguration {
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    char clientid[CLIENT_ID_BUFFER_SIZE];
    char topic[TOPIC_BUFFER_SIZE];
    int frequency;
    bool useCredentials;
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    char lastConnectionStatus[STATUS_BUFFER_SIZE];
    char lastConnectionAttemptTimestamp[TIMESTAMP_STRING_BUFFER_SIZE];

    CustomMqttConfiguration() 
        : enabled(DEFAULT_IS_CUSTOM_MQTT_ENABLED), 
          port(MQTT_CUSTOM_PORT_DEFAULT),
          frequency(MQTT_CUSTOM_FREQUENCY_DEFAULT),
          useCredentials(MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT) {
      snprintf(server, sizeof(server), "%s", MQTT_CUSTOM_SERVER_DEFAULT);
      snprintf(clientid, sizeof(clientid), "%s", MQTT_CUSTOM_CLIENTID_DEFAULT);
      snprintf(topic, sizeof(topic), "%s", MQTT_CUSTOM_TOPIC_DEFAULT);
      snprintf(username, sizeof(username), "%s", MQTT_CUSTOM_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", MQTT_CUSTOM_PASSWORD_DEFAULT);
      snprintf(lastConnectionStatus, sizeof(lastConnectionStatus), "Never attempted");
      lastConnectionAttemptTimestamp[0] = '\0';
    }
};

struct InfluxDbConfiguration {
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    int version;  // 1 or 2
    
    // v1 fields
    char database[DATABASE_NAME_BUFFER_SIZE];
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    
    // v2 fields
    char organization[ORGANIZATION_BUFFER_SIZE];
    char bucket[BUCKET_NAME_BUFFER_SIZE];
    char token[TOKEN_BUFFER_SIZE];
    
    char measurement[MEASUREMENT_BUFFER_SIZE];
    int frequency;
    bool useSSL;
    char lastConnectionStatus[STATUS_BUFFER_SIZE];
    char lastConnectionAttemptTimestamp[TIMESTAMP_STRING_BUFFER_SIZE];

    InfluxDbConfiguration() 
        : enabled(DEFAULT_IS_INFLUXDB_ENABLED), 
          port(INFLUXDB_PORT_DEFAULT),
          version(INFLUXDB_VERSION_DEFAULT),  // Default to v2
          frequency(INFLUXDB_FREQUENCY_DEFAULT),
          useSSL(INFLUXDB_USE_SSL_DEFAULT) {
      snprintf(server, sizeof(server), "%s", INFLUXDB_SERVER_DEFAULT);
      snprintf(database, sizeof(database), "%s", INFLUXDB_DATABASE_DEFAULT);
      snprintf(username, sizeof(username), "%s", INFLUXDB_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", INFLUXDB_PASSWORD_DEFAULT);
      snprintf(organization, sizeof(organization), "%s", INFLUXDB_ORGANIZATION_DEFAULT);
      snprintf(bucket, sizeof(bucket), "%s", INFLUXDB_BUCKET_DEFAULT);
      snprintf(token, sizeof(token), "%s", INFLUXDB_TOKEN_DEFAULT);
      snprintf(measurement, sizeof(measurement), "%s", INFLUXDB_MEASUREMENT_DEFAULT);
      snprintf(lastConnectionStatus, sizeof(lastConnectionStatus), "Never attempted");
      lastConnectionAttemptTimestamp[0] = '\0';
    }
};

enum FirmwareState : int {
    STABLE,
    NEW_TO_TEST,
    TESTING,
    ROLLBACK
};

struct Breadcrumb {
    const char* file;
    const char* function;
    unsigned int line;
    unsigned long long micros;
    unsigned int freeHeap;
    unsigned int coreId;
};

struct CrashData {
    Breadcrumb breadcrumbs[MAX_BREADCRUMBS];      // Circular buffer of breadcrumbs
    unsigned int currentIndex;            // Current position in circular buffer
    unsigned int crashCount;             // Number of crashes detected
    unsigned int lastResetReason;        // Last reset reason from ESP32
    unsigned int resetCount;             // Number of resets
    unsigned long lastUnixTime;          // Last unix time before crash
    unsigned int signature;              // To verify RTC data validity
    // Since this struct will be used in an RTC_NOINIT_ATTR, we cannot initialize it in the constructor
};

// Log callback struct
// --------------------
// Define maximum lengths for each field

struct LogJson {
    char timestamp[LOG_CALLBACK_TIMESTAMP_SIZE];
    unsigned long millisEsp;
    char level[LOG_CALLBACK_LEVEL_SIZE];
    unsigned int coreId;
    char function[LOG_CALLBACK_FUNCTION_SIZE];
    char message[LOG_CALLBACK_MESSAGE_SIZE];

    LogJson()
        : millisEsp(0), coreId(0) {
        timestamp[0] = '\0';
        level[0] = '\0';
        function[0] = '\0';
        message[0] = '\0';
    }

    LogJson(const char* timestampIn, unsigned long millisEspIn, const char* levelIn, unsigned int coreIdIn, const char* functionIn, const char* messageIn)
        : millisEsp(millisEspIn), coreId(coreIdIn) {
        snprintf(timestamp, sizeof(timestamp), "%s", timestampIn ? timestampIn : "");
        snprintf(level, sizeof(level), "%s", levelIn ? levelIn : "");
        snprintf(function, sizeof(function), "%s", functionIn ? functionIn : "");
        snprintf(message, sizeof(message), "%s", messageIn ? messageIn : "");
    }
};

// Rate limiting structure for DoS protection
// --------------------
struct RateLimitEntry {
    char ipAddress[IP_ADDRESS_BUFFER_SIZE];
    int failedAttempts;
    unsigned long lastFailedAttempt;
    unsigned long blockedUntil;
    
    RateLimitEntry() : failedAttempts(0), lastFailedAttempt(0), blockedUntil(0) {
        ipAddress[0] = '\0';
    }
    RateLimitEntry(const char* ip) : failedAttempts(0), lastFailedAttempt(0), blockedUntil(0) {
        if (ip && strlen(ip) < IP_ADDRESS_BUFFER_SIZE) {
            snprintf(ipAddress, sizeof(ipAddress), "%s", ip);
        } else {
            ipAddress[0] = '\0';
        }
    }
};