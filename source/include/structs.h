#ifndef STRUCTS_H
#define STRUCTS_H

#include <Arduino.h>

struct CalibrationValues{
    String label;
    float vLsb;
    float aLsb;
    float wLsb;
    float varLsb;
    float vaLsb;
    float whLsb;
    float varhLsb;
    float vahLsb;
};

struct MeterValues{
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
};

struct ChannelData {
  int index;
  bool active;
  bool reverse;
  String label;
  CalibrationValues calibrationValues;
};

struct Ade7953Configuration {
  long linecyc;
  struct Calibration {
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
  } calibration;
};

struct GeneralConfiguration {
  bool isCloudServicesEnabled;
  int gmtOffset;
  int dstOffset;
};

namespace data {
  struct PayloadMeter {
    int channel;
    long unixTime;
    float activePower;
    float powerFactor;

    // Default constructor
    PayloadMeter() : channel(0), unixTime(0), activePower(0.0), powerFactor(0.0) {}

    // Constructor
    PayloadMeter(int channel, long unixTime, float activePower, float powerFactor)
      : channel(channel), unixTime(unixTime), activePower(activePower), powerFactor(powerFactor) {}
  };
}

#endif