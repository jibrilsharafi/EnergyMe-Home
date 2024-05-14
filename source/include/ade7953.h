#ifndef ADE7953_H
#define ADE7953_H

#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <Ticker.h>

#include "ade7953registers.h"
#include "constants.h"
#include "utils.h"
#include "structs.h"
#include "global.h"

extern AdvancedLogger logger;
extern CustomTime customTime;

class Ade7953 {
public:
    Ade7953(
        int ssPin,
        int sckPin,
        int misoPin,
        int mosiPin,
        int resetPin
    );

    bool begin();
    void loop();

    void setDefaultConfiguration();
    void setConfiguration(Ade7953Configuration newConfiguration);
    bool saveConfigurationToSpiffs();
    JsonDocument configurationToJson();
    Ade7953Configuration parseJsonConfiguration(JsonDocument jsonDocument);

    void setDefaultCalibrationValues();
    void setCalibrationValues(std::vector<CalibrationValues> newCalibrationValues);
    bool saveCalibrationValuesToSpiffs();
    JsonDocument calibrationValuesToJson();
    std::vector<CalibrationValues> parseJsonCalibrationValues(JsonDocument jsonDocument);

    CalibrationValues findCalibrationValue(String label);

    void setDefaultChannelData();
    void setChannelData(ChannelData* newChannelData);
    bool saveChannelDataToSpiffs();
    JsonDocument channelDataToJson();
    void parseJsonChannelData(JsonDocument jsonDocument, ChannelData* channelData);
    void updateDataChannel();
    int findNextActiveChannel(int currentChannel);
    int getActiveChannelCount();

    void readMeterValues(int channel);
    JsonDocument singleMeterValuesToJson(int index);
    JsonDocument meterValuesToJson();
    void setEnergyFromSpiffs();
    void resetEnergyValues();
    void saveEnergyToSpiffs();
    void saveDailyEnergyToSpiffs();

    void setLinecyc(long linecyc);
    void setPgaGain(long pgaGain, int channel, int measurementType);
    void setPhaseCalibration(long phaseCalibration, int channel);
    void setGain(long gain, int channel, int measurementType);
    void setOffset(long offset, int channel, int measurementType);

    long readApparentPowerInstantaneous(int channel);
    long readActivePowerInstantaneous(int channel);
    long readReactivePowerInstantaneous(int channel);
    long readCurrentInstantaneous(int channel);
    long readVoltageInstantaneous();
    long readCurrentRms(int channel);
    long readVoltageRms();
    long readActiveEnergy(int channel);
    long readReactiveEnergy(int channel);
    long readApparentEnergy(int channel);
    long readPowerFactor(int channel);

    Ade7953Configuration configuration;
    std::vector<CalibrationValues> calibrationValues;
    MeterValues meterValues[MULTIPLEXER_CHANNEL_COUNT+1];
    ChannelData channelData[MULTIPLEXER_CHANNEL_COUNT+1];

    bool isLinecycFinished();

    long readRegister(long registerAddress, int nBits, bool signedData);
    void writeRegister(long registerAddress, int nBits, long data);

    static bool saveEnergyFlag;
    static bool saveDailyEnergyFlag;

private:
    bool _verifyCommunication();
    void _setOptimumSettings();
    void _reset();

    void _setDefaultLycmode();
    void _setDefaultNoLoadFeature();
    void _setDefaultPgaGain();
    void _setDefaultConfigRegister();
    void _applyConfiguration();
    void _updateSampleTime();

    void _setConfigurationFromSpiffs();
    
    void _setCalibrationValuesFromSpiffs();
    
    void _initializeMeterValues();
    
    void _setChannelDataFromSpiffs();

    int _ssPin;
    int _sckPin;
    int _misoPin;
    int _mosiPin;
    int _resetPin;
};

#endif