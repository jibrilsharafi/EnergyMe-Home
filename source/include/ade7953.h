#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Ticker.h>
#include <vector>

#include "ade7953registers.h"
#include "customtime.h"
#include "binaries.h"
#include "constants.h"
#include "structs.h"
#include "utils.h"

class Ade7953
{
public:
    Ade7953(
        int ssPin,
        int sckPin,
        int misoPin,
        int mosiPin,
        int resetPin,
        AdvancedLogger &logger,
        CustomTime &customTime,
        MainFlags &mainFlags
    );

    bool begin();
    void loop();

    void readMeterValues(int channel);

    bool isLinecycFinished();

    long readRegister(long registerAddress, int nBits, bool signedData);
    void writeRegister(long registerAddress, int nBits, long data);

    float getAggregatedActivePower(bool includeChannel0 = true);
    float getAggregatedReactivePower(bool includeChannel0 = true);
    float getAggregatedApparentPower(bool includeChannel0 = true);
    float getAggregatedPowerFactor(bool includeChannel0 = true);
    
    void resetEnergyValues();
    void saveEnergy();

    void setDefaultConfiguration();
    bool setConfiguration(JsonDocument &jsonDocument);

    void setDefaultCalibrationValues();
    bool setCalibrationValues(JsonDocument &jsonDocument);

    void setDefaultChannelData();
    bool setChannelData(JsonDocument &jsonDocument);
    void channelDataToJson(JsonDocument &jsonDocument);
    
    int findNextActiveChannel(int currentChannel);

    JsonDocument singleMeterValuesToJson(int index);
    void meterValuesToJson(JsonDocument &jsonDocument);

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];

private:
    void _setHardwarePins();
    void _setOptimumSettings();

    void _reset();
    bool _verifyCommunication();

    void _setDefaultParameters();

    void _setConfigurationFromSpiffs();
    void _applyConfiguration(JsonDocument &jsonDocument);
    bool _validateConfigurationJson(JsonDocument &jsonDocument);
    
    void _setCalibrationValuesFromSpiffs();
    void _jsonToCalibrationValues(JsonObject &jsonObject, CalibrationValues &calibrationValues);
    bool _validateCalibrationValuesJson(JsonDocument &jsonDocument);

    bool _saveChannelDataToSpiffs();
    void _setChannelDataFromSpiffs();
    void _updateChannelData();
    bool _validateChannelDataJson(JsonDocument &jsonDocument);

    void _updateSampleTime();

    void _setEnergyFromSpiffs();
    void _saveEnergyToSpiffs();
    void _saveDailyEnergyToSpiffs();
    
    long _readApparentPowerInstantaneous(int channel);
    long _readActivePowerInstantaneous(int channel);
    long _readReactivePowerInstantaneous(int channel);
    long _readCurrentInstantaneous(int channel);
    long _readVoltageInstantaneous();
    long _readCurrentRms(int channel);
    long _readVoltageRms();
    long _readActiveEnergy(int channel);
    long _readReactiveEnergy(int channel);
    long _readApparentEnergy(int channel);
    long _readPowerFactor(int channel);
    long _readAngle(int channel);

    float _validateValue(float oldValue, float newValue, float min, float max);
    float _validateVoltage(float oldValue, float newValue);
    float _validateCurrent(float oldValue, float newValue);
    float _validatePower(float oldValue, float newValue);
    float _validatePowerFactor(float oldValue, float newValue);

    void _setLinecyc(unsigned int linecyc);
    void _setPgaGain(long pgaGain, int channel, int measurementType);
    void _setPhaseCalibration(long phaseCalibration, int channel);
    void _setGain(long gain, int channel, int measurementType);
    void _setOffset(long offset, int channel, int measurementType);

    int _ssPin;
    int _sckPin;
    int _misoPin;
    int _mosiPin;
    int _resetPin;

    unsigned int _sampleTime;

    AdvancedLogger &_logger;
    CustomTime &_customTime;
    MainFlags &_mainFlags;

    unsigned long _lastMillisSaveEnergy = 0;
};