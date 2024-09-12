#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

struct MeterValues
{
  float voltage;
  float current;
  float activePower;
  float reactivePower;
  float apparentPower;
  float powerFactor;
  float activeEnergy;
  float reactiveEnergy;
  float apparentEnergy;
  long lastMillis;

  // Default constructor
  MeterValues()
    : voltage(0.0), current(0.0), activePower(0.0), reactivePower(0.0), apparentPower(0.0), powerFactor(0.0),
      activeEnergy(0.0), reactiveEnergy(0.0), apparentEnergy(0.0), lastMillis(0) {}
};

struct PayloadMeter
{
  int channel;
  long unixTime;
  float activePower;
  float powerFactor;

  PayloadMeter() : channel(0), unixTime(0), activePower(0.0), powerFactor(0.0) {}

  PayloadMeter(int channel, long unixTime, float activePower, float powerFactor)
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

  // Default constructor
  CalibrationValues()
    : label(String("Calibration")), vLsb(1.0), aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {}
};


struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  String label;
  CalibrationValues calibrationValues;

  // Default constructor
  ChannelData()
    : index(0), active(false), reverse(false), label(String("Channel")), calibrationValues() {}
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
  bool generalConfiguration;

  PublishMqtt() : connectivity(true), meter(true), status(true), metadata(true), channel(true), generalConfiguration(true) {} // Set default to true to publish everything on first connection
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
  

#endif