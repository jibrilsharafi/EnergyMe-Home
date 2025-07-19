#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Ticker.h>
#include <vector>
#include <CircularBuffer.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ade7953registers.h"
#include "led.h"
#include "multiplexer.h"
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
        int interruptPin,        
        AdvancedLogger &logger,
        MainFlags &mainFlags,
        CustomTime &customTime,
        Led &led,
        Multiplexer &multiplexer,
        CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter
    );

    bool begin();
    void cleanup();
    void loop();
        
    unsigned int getSampleTime() const { return _sampleTime; }

    long readRegister(long registerAddress, int nBits, bool signedData, bool isVerificationRequired = true);
    void writeRegister(long registerAddress, int nBits, long data, bool isVerificationRequired = true);

    float getAggregatedActivePower(bool includeChannel0 = true);
    float getAggregatedReactivePower(bool includeChannel0 = true);
    float getAggregatedApparentPower(bool includeChannel0 = true);
    float getAggregatedPowerFactor(bool includeChannel0 = true);

    float getGridFrequency() const { return _gridFrequency; }
    
    void resetEnergyValues();
    bool setEnergyValues(JsonDocument &jsonDocument);
    void saveEnergy();

    void setDefaultConfiguration();
    bool setConfiguration(JsonDocument &jsonDocument);

    void setDefaultCalibrationValues();
    bool setCalibrationValues(JsonDocument &jsonDocument);

    void setDefaultChannelData();
    bool setChannelData(JsonDocument &jsonDocument);
    void channelDataToJson(JsonDocument &jsonDocument);
    
    void singleMeterValuesToJson(JsonDocument &jsonDocument, ChannelNumber channel);
    void fullMeterValuesToJson(JsonDocument &jsonDocument);

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];    
    
    void pauseMeterReadingTask();
    void resumeMeterReadingTask();
    
    void takePayloadMeterMutex(TickType_t waitTicks = portMAX_DELAY) {
        if (_payloadMeterMutex != NULL) xSemaphoreTake(_payloadMeterMutex, waitTicks); 
    }
    void givePayloadMeterMutex() {
        if (_payloadMeterMutex != NULL) xSemaphoreGive(_payloadMeterMutex);
    }

private:

    void _initializeSpiMutexes();
    Ade7953InterruptType _handleInterrupt();
    void _reinitializeAfterInterrupt();
    static void IRAM_ATTR _isrHandler();
    static void _meterReadingTask(void* parameter);
    void _stopMeterReadingTask();
    void _attachInterruptHandler();
    void _detachInterruptHandler();
    
    static void _irqstataBitName(int bit, char *buffer, size_t bufferSize);
    
    void _setHardwarePins();
    void _setOptimumSettings();
    
    void _reset();
    bool _verifyCommunication();
    
    void _setDefaultParameters();
    
    void _setupInterrupts();
    void _startMeterReadingTask();
    
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

    Phase _getLaggingPhase(Phase phase);
    Phase _getLeadingPhase(Phase phase);
    
    bool _readMeterValues(int channel, unsigned long long linecycUnixTime);
    
    ChannelNumber _findNextActiveChannel(ChannelNumber currentChannel);

    void _updateSampleTime();
    
    void _setEnergyFromSpiffs();
    void _saveEnergyToSpiffs();
    void _saveDailyEnergyToSpiffs();
    
    bool _verifyLastCommunication(long expectedAddress, int expectedBits, long expectedData, bool signedData, bool wasWrite);
    
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
    long _readPeriod();
    
    bool _validateValue(float newValue, float min, float max);
    bool _validateVoltage(float newValue);
    bool _validateCurrent(float newValue);
    bool _validatePower(float newValue);
    bool _validatePowerFactor(float newValue);
    bool _validateGridFrequency(float newValue);

    void _setLinecyc(unsigned int linecyc);
    void _setPgaGain(long pgaGain, int channel, int measurementType);
    void _setPhaseCalibration(long phaseCalibration, int channel);
    void _setGain(long gain, int channel, int measurementType);
    void _setOffset(long offset, int channel, int measurementType);

    void _recordFailure();
    void _checkForTooManyFailures();

    int _ssPin;
    int _sckPin;
    int _misoPin;
    int _mosiPin;
    int _resetPin;
    int _interruptPin;

    unsigned int _sampleTime; // in milliseconds, time between linecycles readings
    
    ChannelNumber _currentChannel = CHANNEL_0;

    AdvancedLogger &_logger;
    MainFlags &_mainFlags;
    CustomTime &_customTime;
    Led &_led;
    Multiplexer &_multiplexer;
    CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &_payloadMeter;
    
    ChannelState _channelStates[CHANNEL_COUNT];

    float _gridFrequency = 50.0f;

    int _failureCount = 0;
    unsigned long _firstFailureTime = 0;
    unsigned long _lastMillisSaveEnergy = 0;
    
    static Ade7953 *_instance;

    SemaphoreHandle_t _spiMutex = NULL;
    SemaphoreHandle_t _spiOperationMutex = NULL;
    SemaphoreHandle_t _payloadMeterMutex = NULL;
    
    TaskHandle_t _meterReadingTaskHandle = NULL;
    SemaphoreHandle_t _ade7953InterruptSemaphore = NULL;
    volatile unsigned long _lastInterruptTime = 0;

    void _checkInterruptTiming();
    bool _processChannelReading(int channel, unsigned long long linecycUnix);
    void _addMeterDataToPayload(int channel, unsigned long long linecycUnix);
    void _processCycendInterrupt(unsigned long long linecycUnix);
    void _handleCrcChangeInterrupt();
};