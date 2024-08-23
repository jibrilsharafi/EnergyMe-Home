#ifndef ADE7953_H
#define ADE7953_H

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
#include "global.h"
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
        CustomTime &customTime); 

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
    void setConfiguration(JsonDocument &jsonDocument);

    void setDefaultCalibrationValues();
    void setCalibrationValues(JsonDocument &jsonDocument);

    void setDefaultChannelData();
    void setChannelData(ChannelData *newChannelData);
    bool saveChannelDataToSpiffs();
    JsonDocument channelDataToJson();
    ChannelData *parseJsonChannelData(JsonDocument jsonDocument);
    void _updateDataChannel();
    
    int findNextActiveChannel(int currentChannel);

    JsonDocument singleMeterValuesToJson(int index);
    JsonDocument meterValuesToJson();

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];

private:
    bool _verifyCommunication();
    void _setOptimumSettings();
    void _reset();

    void _setDefaultLycmode();
    void _setDefaultNoLoadFeature();
    void _setDefaultPgaGain();
    void _setDefaultConfigRegister();
    void _updateSampleTime();


    void _setConfigurationFromSpiffs();
    void _applyConfiguration(JsonDocument &jsonDocument);
    
    void _setCalibrationValuesFromSpiffs();
    void _jsonToCalibrationValues(JsonDocument &jsonDocument, CalibrationValues &calibrationValues);

    void _setChannelDataFromSpiffs();

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

    float _validateValue(float oldValue, float newValue, float min, float max);
    float _validateVoltage(float oldValue, float newValue);
    float _validateCurrent(float oldValue, float newValue);
    float _validatePower(float oldValue, float newValue);
    float _validatePowerFactor(float oldValue, float newValue);

    void _setLinecyc(long linecyc);
    void _setPgaGain(long pgaGain, int channel, int measurementType);
    void _setPhaseCalibration(long phaseCalibration, int channel);
    void _setGain(long gain, int channel, int measurementType);
    void _setOffset(long offset, int channel, int measurementType);

    int _getActiveChannelCount();

    CalibrationValues _calibrationValuesFromSpiffs(String label);

    int _ssPin;
    int _sckPin;
    int _misoPin;
    int _mosiPin;
    int _resetPin;

    AdvancedLogger &_logger;
    CustomTime &_customTime;

    unsigned long _lastMillisSaveEnergy = 0;

    struct Ade7953Configuration
    {
        long _aIGain;
        long _bIGain;
        long _aWGain;
        long _bWGain;
        long _aWattOs;
        long _bWattOs;
        long _aVarGain;
        long _bVarGain;
        long _aVarOs;
        long _bVarOs;
        long _aVaGain;
        long _bVaGain;
        long _aVaOs;
        long _bVaOs;
        long _aIRmsOs;
        long _bIRmsOs;
        long _phCalA;
        long _phCalB;

        Ade7953Configuration()
            : _aIGain(4194304), _bIGain(4194304), _aWGain(4194304), _bWGain(4194304), _aWattOs(0), _bWattOs(0),
              _aVarGain(4194304), _bVarGain(4194304), _aVarOs(0), _bVarOs(0), _aVaGain(4194304), _bVaGain(4194304),
              _aVaOs(0), _bVaOs(0), _aIRmsOs(0), _bIRmsOs(0), _phCalA(0), _phCalB(0) {}
    };

    Ade7953Configuration _configuration;
};

#endif