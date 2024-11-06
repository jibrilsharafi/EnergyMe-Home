#pragma once

#include <Arduino.h>

struct MainFlags
{
  bool isFirmwareUpdate;
  bool isCrashCounterReset;
  bool isfirstLinecyc;
  bool blockLoop;
  int currentChannel;

  MainFlags() : isFirmwareUpdate(false), isCrashCounterReset(false), isfirstLinecyc(true), blockLoop(false), currentChannel(-1) {}
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

enum Measurement : int {
    VOLTAGE,
    CURRENT,
    ACTIVE_POWER,
    REACTIVE_POWER,
    APPARENT_POWER,
    POWER_FACTOR,
};

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
  long lastMillis;

  MeterValues()
    : voltage(230.0), current(0.0), activePower(0.0), reactivePower(0.0), apparentPower(0.0), powerFactor(0.0),
      activeEnergyImported(0.0), activeEnergyExported(0.0), reactiveEnergyImported(0.0), 
      reactiveEnergyExported(0.0), apparentEnergy(0.0), lastMillis(0) {}
};

struct PayloadMeter
{
  int channel;
  unsigned long long unixTime;
  float activePower;
  float powerFactor;

  PayloadMeter() : channel(0), unixTime(0), activePower(0.0), powerFactor(0.0) {}

  PayloadMeter(int channel, unsigned long long unixTime, float activePower, float powerFactor)
      : channel(channel), unixTime(unixTime), activePower(activePower), powerFactor(powerFactor) {}
};

struct CalibrationValues
{
  String label;
  float vLsb;
  float aLsb;
  float wLsb;
  float varLsb;
  float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CalibrationValues()
    : label(String("Calibration")), vLsb(1.0), aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {}
};


struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  String label;
  Phase phase;
  CalibrationValues calibrationValues;

  ChannelData()
    : index(0), active(false), reverse(false), label(String("Channel")), phase(PHASE_1), calibrationValues(CalibrationValues()) {}
};

struct GeneralConfiguration
{
  bool isCloudServicesEnabled;
  int gmtOffset;
  int dstOffset;
  int ledBrightness;

  GeneralConfiguration() : isCloudServicesEnabled(false), gmtOffset(0), dstOffset(0), ledBrightness(127) {}
};

struct PublicLocation
{
  String country;
  String city;
  String latitude;
  String longitude;

  PublicLocation() : country(String("Unknown")), city(String("Unknown")), latitude(String("45.0")), longitude(String("9.0")) {}
};

struct RestartConfiguration
{
  bool isRequired;
  unsigned long requiredAt;
  String functionName;
  String reason;

  RestartConfiguration() : isRequired(false), functionName(String("Unknown")), reason(String("Unknown")) {}
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

  PublishMqtt() : connectivity(true), meter(true), status(true), metadata(true), channel(true), crash(false), monitor(true), generalConfiguration(true) {} // Set default to true to publish everything on first connection
};

struct CustomMqttConfiguration {
    bool enabled;
    String server;
    int port;
    String clientid;
    String topic;
    int frequency;
    bool useCredentials;
    String username;
    String password;

    CustomMqttConfiguration() 
        : enabled(DEFAULT_IS_CUSTOM_MQTT_ENABLED), 
          server(String(MQTT_CUSTOM_SERVER_DEFAULT)), 
          port(MQTT_CUSTOM_PORT_DEFAULT),
          clientid(String(MQTT_CUSTOM_CLIENTID_DEFAULT)),
          topic(String(MQTT_CUSTOM_TOPIC_DEFAULT)),
          frequency(MQTT_CUSTOM_FREQUENCY_DEFAULT),
          useCredentials(MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT),
          username(String(MQTT_CUSTOM_USERNAME_DEFAULT)),
          password(String(MQTT_CUSTOM_PASSWORD_DEFAULT)) {}
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
};

// Log callback struct
// --------------------
// Define maximum lengths for each field
const size_t TIMESTAMP_LEN = 20;
const size_t LEVEL_LEN     = 10;
const size_t FUNCTION_LEN  = 50;
const size_t MESSAGE_LEN   = 256;

struct LogJson {
    char timestamp[TIMESTAMP_LEN];
    unsigned long millisEsp;
    char level[LEVEL_LEN];
    unsigned int coreId;
    char function[FUNCTION_LEN];
    char message[MESSAGE_LEN];

    LogJson()
        : millisEsp(0), coreId(0) {
        timestamp[0] = '\0';
        level[0] = '\0';
        function[0] = '\0';
        message[0] = '\0';
    }

    LogJson(const char* timestampIn, unsigned long millisEspIn, const char* levelIn, unsigned int coreIdIn, const char* functionIn, const char* messageIn)
        : millisEsp(millisEspIn), coreId(coreIdIn) {
        strncpy(timestamp, timestampIn, TIMESTAMP_LEN - 1);
        timestamp[TIMESTAMP_LEN - 1] = '\0';

        strncpy(level, levelIn, LEVEL_LEN - 1);
        level[LEVEL_LEN - 1] = '\0';

        strncpy(function, functionIn, FUNCTION_LEN - 1);
        function[FUNCTION_LEN - 1] = '\0';

        strncpy(message, messageIn, MESSAGE_LEN - 1);
        message[MESSAGE_LEN - 1] = '\0';
    }
};