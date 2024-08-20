#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

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
    : label("Calibration"), vLsb(0.0), aLsb(0.0), wLsb(0.0), varLsb(0.0), vaLsb(0.0), whLsb(0.0), varhLsb(0.0), vahLsb(0.0) {}
};

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

struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  String label;
  CalibrationValues calibrationValues;

  // Default constructor
  ChannelData()
    : index(0), active(false), reverse(false), label("Channel"), calibrationValues() {}
};

struct Ade7953Configuration
{
  long expectedApNoLoad;
  long xNoLoad;
  long disNoLoad;
  long lcycMode;
  long linecyc;
  long pga;
  long config;
  long aWGain;
  long aWattOs;
  long aVarGain;
  long aVarOs;
  long aVaGain;
  long aVaOs;
  long aIGain;
  long aIRmsOs;
  long bIGain;
  long bIRmsOs;
  long phCalA;
  long phCalB;

  Ade7953Configuration()
    : expectedApNoLoad(0), xNoLoad(0), disNoLoad(0), lcycMode(0), linecyc(0), pga(0), config(0),
      aWGain(0), aWattOs(0), aVarGain(0), aVarOs(0), aVaGain(0), aVaOs(0), aIGain(0), aIRmsOs(0),
      bIGain(0), bIRmsOs(0), phCalA(0), phCalB(0) {}
};

struct GeneralConfiguration
{
  int sampleCycles;
  bool isCloudServicesEnabled;
  int gmtOffset;
  int dstOffset;
  int ledBrightness;

  GeneralConfiguration() : sampleCycles(100), isCloudServicesEnabled(false), gmtOffset(0), dstOffset(0), ledBrightness(127) {}
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

struct PublicLocation
{
  String country;
  String city;
  String latitude;
  String longitude;

  PublicLocation() : country("Unknown"), city("Unknown"), latitude("45.0"), longitude("9.0") {}
};

#endif