#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Ticker.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ade7953registers.h"
#include "led.h"
#include "multiplexer.h"
#include "customtime.h"
#include "mqtt.h"
#include "binaries.h"
#include "constants.h"
#include "structs.h"
#include "utils.h"

// Saving date
#define SAVE_ENERGY_INTERVAL (6 * 60 * 1000) // Time between each energy save to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100 // Timeout for acquiring SPI mutex to prevent deadlocks

// Task
#define ADE7953_METER_READING_TASK_NAME "ade7953_task" // The name of the ADE7953 task
#define ADE7953_METER_READING_TASK_STACK_SIZE (8 * 1024) // The stack size for the ADE7953 task
#define ADE7953_METER_READING_TASK_PRIORITY 2 // The priority for the ADE7953 task

// Interrupt handling
#define ADE7953_INTERRUPT_TIMEOUT_MS 1000 // Timeout for waiting on interrupt semaphore (in ms)

// Setup
#define ADE7953_RESET_LOW_DURATION 200 // The duration for the reset pin to be low
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500 

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 
#define DEFAULT_X_NOLOAD_REGISTER 0x00E419 // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 10000:1, meaning that the no-load threshold is 0.01% of the full-scale value
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (enable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0b01111111 // 0xFF 0b01111111 (enable accumulation mode for all channels, disable read with reset)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000100001100 // Enable bit 2, bit 3 (line accumulation for PF), 8 (CRC is enabled), and 15 (keep HPF enabled, keep COMM_LOCK disabled)
#define DEFAULT_GAIN 4194304 // 0x400000 (default gain for the ADE7953)
#define DEFAULT_OFFSET 0 // 0x000000 (default offset for the ADE7953)
#define DEFAULT_PHCAL 10 // 0.02°/LSB, indicating a phase calibration of 0.2° which is minimum needed for CTs
#define DEFAULT_IRQENA_REGISTER 0b001101000000000000000000 // Enable CYCEND interrupt (bit 18) and Reset (bit 20, mandatory) for line cycle end detection
#define MINIMUM_SAMPLE_TIME 200

// IRQSTATA / RSTIRQSTATA Register Bit Positions (Table 23, ADE7953 Datasheet)
#define IRQSTATA_AEHFA_BIT         0  // Active energy register half full (Current Channel A)
#define IRQSTATA_VAREHFA_BIT       1  // Reactive energy register half full (Current Channel A)
#define IRQSTATA_VAEHFA_BIT        2  // Apparent energy register half full (Current Channel A)
#define IRQSTATA_AEOFA_BIT         3  // Active energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAREOFA_BIT       4  // Reactive energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAEOFA_BIT        5  // Apparent energy register overflow/underflow (Current Channel A)
#define IRQSTATA_AP_NOLOADA_BIT    6  // Active power no-load detected (Current Channel A)
#define IRQSTATA_VAR_NOLOADA_BIT   7  // Reactive power no-load detected (Current Channel A)
#define IRQSTATA_VA_NOLOADA_BIT    8  // Apparent power no-load detected (Current Channel A)
#define IRQSTATA_APSIGN_A_BIT      9  // Sign of active energy changed (Current Channel A)
#define IRQSTATA_VARSIGN_A_BIT     10 // Sign of reactive energy changed (Current Channel A)
#define IRQSTATA_ZXTO_IA_BIT       11 // Zero crossing missing on Current Channel A
#define IRQSTATA_ZXIA_BIT          12 // Current Channel A zero crossing detected
#define IRQSTATA_OIA_BIT           13 // Current Channel A overcurrent threshold exceeded
#define IRQSTATA_ZXTO_BIT          14 // Zero crossing missing on voltage channel
#define IRQSTATA_ZXV_BIT           15 // Voltage channel zero crossing detected
#define IRQSTATA_OV_BIT            16 // Voltage peak overvoltage threshold exceeded
#define IRQSTATA_WSMP_BIT          17 // New waveform data acquired
#define IRQSTATA_CYCEND_BIT        18 // End of line cycle accumulation period
#define IRQSTATA_SAG_BIT           19 // Sag event occurred
#define IRQSTATA_RESET_BIT         20 // End of software or hardware reset
#define IRQSTATA_CRC_BIT           21 // Checksum has changed

// Fixed conversion values
#define POWER_FACTOR_CONVERSION_FACTOR 1.0f / 32768.0f // PF/LSB
#define ANGLE_CONVERSION_FACTOR 360.0f * 50.0f / 223000.0f // 0.0807 °/LSB
#define GRID_FREQUENCY_CONVERSION_FACTOR 223750.0f // Clock of the period measurement, in Hz. To be multiplied by the register value of 0x10E

// Validate values
#define VALIDATE_VOLTAGE_MIN 50.0f // Any voltage below this value is discarded
#define VALIDATE_VOLTAGE_MAX 300.0f  // Any voltage above this value is discarded
#define VALIDATE_CURRENT_MIN -300.0f // Any current below this value is discarded
#define VALIDATE_CURRENT_MAX 300.0f // Any current above this value is discarded
#define VALIDATE_POWER_MIN -100000.0f // Any power below this value is discarded
#define VALIDATE_POWER_MAX 100000.0f // Any power above this value is discarded
#define VALIDATE_POWER_FACTOR_MIN -1.0f // Any power factor below this value is discarded
#define VALIDATE_POWER_FACTOR_MAX 1.0f // Any power factor above this value is discarded
#define VALIDATE_GRID_FREQUENCY_MIN 45.0f // Minimum grid frequency in Hz
#define VALIDATE_GRID_FREQUENCY_MAX 65.0f // Maximum grid frequency in Hz

// Guardrails and thresholds
#define MAXIMUM_POWER_FACTOR_CLAMP 1.05f // Values above 1 but below this are still accepted
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.01f // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used
#define MINIMUM_POWER_FACTOR 0.05f // Measuring such low power factors is virtually impossible with such CTs
#define MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE 100 // Threshold to transition to a legitimate zero state for channel 0
#define ADE7953_MIN_LINECYC 10U // The minimum line cycle settable for the ADE7953. Must be unsigned
#define ADE7953_MAX_LINECYC 1000U // The maximum line cycle settable for the ADE7953. Must be unsigned

#define INVALID_SPI_READ_WRITE 0xDEADDEAD // Custom, used to indicate an invalid SPI read/write operation

// ADE7953 Smart Failure Detection
#define ADE7953_MAX_FAILURES_BEFORE_RESTART 100
#define ADE7953_FAILURE_RESET_TIMEOUT_MS (1 * 60 * 1000)

// Check for incorrect readings
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE 100.0f // Absolute difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE 0.20f // Relative difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded

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
        AdvancedLogger &logger
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