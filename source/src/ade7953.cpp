// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "ade7953.h"

namespace Ade7953
{
    // Static variables
    // =========================================================
    // =========================================================
    
    // Hardware pins
    static uint8_t _ssPin;
    static uint8_t _sckPin;
    static uint8_t _misoPin;
    static uint8_t _mosiPin;
    static uint8_t _resetPin;
    static uint8_t _interruptPin;

    // Timing and measurement variables
    static uint64_t _sampleTime; // in milliseconds, time between linecycles readings
    static uint8_t _currentChannel = INVALID_CHANNEL; // By default, no channel is selected (except for channel 0 which is always active)
    static float _gridFrequency = 50.0f;

    // Failure tracking
    static uint32_t _failureCount = 0;
    static uint64_t _firstFailureTime = 0;
    
    // Critical failure tracking (for missed interrupts)
    static uint32_t _criticalFailureCount = 0;
    static uint64_t _firstCriticalFailureTime = 0;

    // Operational flags
    static bool _hasConfigurationChanged = false; // Flag to track if configuration has changed (needed since we will get an interrupt for CRC change)
    static bool _hasToSkipReading = true; // Flag to skip every other reading on Channel B so we purge the ADE7953 data when switching multiplexer channel
    
    // Interrupt management
    // We set this flag to false in the ISR and to true immediately after reading the energy registers (with reset)
    // This ensures that we only process the interrupt once and avoid reading 0 energy values when reading twice in a row
    // This happens when the CPU is starved (e.g. during OTA) and thus we fall late, starting the reading just before an
    // interrupt is triggered, meaning we read twice in a row in the same window.
    // TL;DR: This flag helps us avoid reading zero energy data during OTA.
    static bool volatile _interruptHandledChannelA = false;
    static bool volatile _interruptHandledChannelB = false;

    // Synchronization primitives
    static SemaphoreHandle_t _spiMutex = NULL; // To handle single SPI operations
    static SemaphoreHandle_t _spiOperationMutex = NULL; // To handle full SPI operations (like read with verification, which is 2 SPI operations)
    static SemaphoreHandle_t _ade7953InterruptSemaphore = NULL;
    static SemaphoreHandle_t _configMutex = NULL;
    static SemaphoreHandle_t _meterValuesMutex = NULL;
    static SemaphoreHandle_t _channelDataMutex = NULL;

    // FreeRTOS task handles and control flags
    static TaskHandle_t _meterReadingTaskHandle = NULL;
    static bool _meterReadingTaskShouldRun = false;
    
    static TaskHandle_t _energySaveTaskHandle = NULL;
    static bool _energySaveTaskShouldRun = false;
    
    static TaskHandle_t _hourlyCsvSaveTaskHandle = NULL;
    static bool _hourlyCsvSaveTaskShouldRun = false;

    // Waveform capture state and buffers
    static CaptureState _captureState = CaptureState::IDLE;
    static uint8_t _captureRequestedChannel = INVALID_CHANNEL;
    static uint8_t _captureChannel = INVALID_CHANNEL;  // The channel being actively captured
    static uint16_t _captureSampleCount = 0;
    static uint64_t _captureStartUnixMillis = 0;
    static uint64_t _captureStartMicros = 0;  // Microseconds timestamp at capture start
    static int32_t* _voltageWaveformBuffer = nullptr;
    static int32_t* _currentWaveformBuffer = nullptr;
    static uint64_t* _microsWaveformBuffer = nullptr;  // Microseconds delta from start for each sample

    // Configuration and data arrays
    static Ade7953Configuration _configuration;
    MeterValues _meterValues[CHANNEL_COUNT];
    EnergyValues _energyValues[CHANNEL_COUNT]; // Store previous energy values for energy comparisons (optimize saving to flash)
    ChannelData _channelData[CHANNEL_COUNT];

    // Private function declarations
    // =========================================================
    // =========================================================

    // Hardware initialization and control
    static void _setHardwarePins(
        uint8_t ssPin,
        uint8_t sckPin,
        uint8_t misoPin,
        uint8_t mosiPin,
        uint8_t resetPin,
        uint8_t interruptPin
    );
    static void _reset();
    // According to the datasheet, setting these registers is mandatory for optimal operation
    static void _setOptimumSettings();
    static void _setDefaultParameters();

    // System management
    static void _initializeMutexes();
    static void _cleanup();
    static bool _verifyCommunication();

    // Interrupt handling
    static void _setupInterrupts();
    static Ade7953InterruptType _handleInterrupt();
    static void _attachInterruptHandler();
    static void _detachInterruptHandler();
    static void IRAM_ATTR _isrHandler();
    static void _handleCycendInterrupt(uint64_t linecycUnix);
    static void _pollWaveformSamples();
    static void _handleCrcChangeInterrupt();
    static void _handleResetInterrupt();
    static void _handleOtherInterrupt();
    
    // Task management
    static void _startMeterReadingTask();
    static void _stopMeterReadingTask();
    static void _meterReadingTask(void* parameter);

    static void _startEnergySaveTask();
    static void _stopEnergySaveTask();
    static void _energySaveTask(void* parameter);

    static void _startHourlyCsvSaveTask();
    static void _stopHourlyCsvSaveTask();
    static void _hourlyCsvSaveTask(void* parameter);

    // Configuration management
    static void _setConfigurationFromPreferences();
    static void _saveConfigurationToPreferences();
    static void _applyConfiguration(const Ade7953Configuration &config); // Apply all the single register values from the configuration
    static bool _validateJsonConfiguration(const JsonDocument &jsonDocument, bool partial = false);

    // Channel data management
    static void _setChannelDataFromPreferences(uint8_t channelIndex);
    static bool _saveChannelDataToPreferences(uint8_t channelIndex);
    static void _updateChannelData(uint8_t channelIndex);
    static bool _validateChannelDataJson(const JsonDocument &jsonDocument, bool partial = false);
    static void _calculateLsbValues(CtSpecification &ctSpec);

    // Energy data management
    static void _setEnergyFromPreferences(uint8_t channelIndex);
    static void _saveEnergyToPreferences(uint8_t channelIndex, bool forceSave = false); // Needed for saving data anyway on first setup (energy is 0 and not saved otherwise)
    static void _saveHourlyEnergyToCsv(); // Not per channel so that we open the file only once
    static void _saveEnergyComplete();

    // Meter reading and processing
    static bool _readMeterValues(uint8_t channelIndex, uint64_t linecycUnixTime);
    static void _purgeEnergyRegisters(Ade7953Channel ade7953Channel);
    static bool _processChannelReading(uint8_t channelIndex, uint64_t linecycUnix);
    static void _addMeterDataToPayload(uint8_t channelIndex);

    // ADE7953 register writing functions
    static void _setLinecyc(uint32_t linecyc);
    static void _setPgaGain(int32_t pgaGain, Ade7953Channel ade7953Channel, MeasurementType measurementType);
    static void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel ade7953Channel);
    static void _setGain(int32_t gain, Ade7953Channel ade7953Channel, MeasurementType measurementType);
    static void _setOffset(int32_t offset, Ade7953Channel ade7953Channel, MeasurementType measurementType);

    // Sample time management
    static void _updateSampleTime();
    static void _setSampleTimeFromPreferences();
    static void _saveSampleTimeToPreferences();

    // ADE7953 register reading functions

    static int32_t _readApparentPowerInstantaneous(Ade7953Channel ade7953Channel);
    /*
    Reads the "instantaneous" active power.

    "Instantaneous" because the active power is only defined as the dc component
    of the instantaneous power signal, which is V_RMS * I_RMS - V_RMS * I_RMS * cos(2*omega*t). 
    It is updated at 6.99 kHz.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The active power in LSB.
    */
    static int32_t _readActivePowerInstantaneous(Ade7953Channel ade7953Channel);
    static int32_t _readReactivePowerInstantaneous(Ade7953Channel ade7953Channel);
    /*
    Reads the actual instantaneous current. 

    This allows you see the actual sinusoidal waveform, so both positive and 
    negative values. At full scale (so 500 mV), the value returned is 9032007d.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The actual instantaneous current in LSB.
    */
    static int32_t _readCurrentInstantaneous(Ade7953Channel ade7953Channel);
    /*
    Reads the actual instantaneous voltage. 

    This allows you 
    to see the actual sinusoidal waveform, so both positive
    and negative values. At full scale (so 500 mV), the value
    returned is 9032007d.

    @return The actual instantaneous voltage in LSB.
    */
    static int32_t _readVoltageInstantaneous();
    /*
    Reads the current in RMS.

    This measurement is updated at 6.99 kHz and has a settling
    time of 200 ms. The value is in LSB.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The current in RMS in LSB.
    */
    static int32_t _readCurrentRms(Ade7953Channel ade7953Channel);
    /*
    Reads the voltage in RMS.

    This measurement is updated at 6.99 kHz and has a settling
    time of 200 ms. The value is in LSB.

    @return The voltage in RMS in LSB.
    */
    static int32_t _readVoltageRms();
    static int32_t _readActiveEnergy(Ade7953Channel ade7953Channel);
    static int32_t _readReactiveEnergy(Ade7953Channel ade7953Channel);
    static int32_t _readApparentEnergy(Ade7953Channel ade7953Channel);
    static int32_t _readPowerFactor(Ade7953Channel ade7953Channel);
    static int32_t _readAngle(Ade7953Channel ade7953Channel);
    static int32_t _readPeriod();

    // Verification and validation functions
    static void _recordFailure();
    static void _checkForTooManyFailures();
    static void _recordCriticalFailure();
    static void _checkForTooManyCriticalFailures();
    static bool _verifyLastSpiCommunication(uint16_t expectedAddress, uint8_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite);
    static bool _validateValue(float newValue, float min, float max);
    static bool _validateVoltage(float newValue);
    static bool _validateCurrent(float newValue);
    static bool _validatePower(float newValue);
    static bool _validatePowerFactor(float newValue);
    static bool _validateGridFrequency(float newValue);

    // Utility functions
    static uint8_t _findNextActiveChannel(uint8_t currentChannel);
    static Phase _getLaggingPhase(Phase phase);
    static Phase _getLeadingPhase(Phase phase);
    // Returns the string name of the IRQSTATA bit, or UNKNOWN if the bit is not recognized.
    static const char *_irqstataBitName(uint32_t bit);
    void _printMeterValues(uint8_t channelIndex);


    // Public API functions
    // =========================================================
    // =========================================================

    // Core lifecycle management
    // =========================

    bool begin(
        uint8_t ssPin,
        uint8_t sckPin,
        uint8_t misoPin,
        uint8_t mosiPin,
        uint8_t resetPin,
        uint8_t interruptPin
    ) {
        LOG_DEBUG("Initializing Ade7953");

        _initializeMutexes();
        LOG_DEBUG("Initialized SPI mutexes");

        // Allocate PSRAM buffers for waveform capture
        _voltageWaveformBuffer = (int32_t*)ps_malloc(WAVEFORM_BUFFER_SIZE * sizeof(int32_t));
        _currentWaveformBuffer = (int32_t*)ps_malloc(WAVEFORM_BUFFER_SIZE * sizeof(int32_t));
        _microsWaveformBuffer = (uint64_t*)ps_malloc(WAVEFORM_BUFFER_SIZE * sizeof(uint64_t));

        if (!_voltageWaveformBuffer || !_currentWaveformBuffer || !_microsWaveformBuffer) {
            LOG_ERROR("Failed to allocate waveform buffers from PSRAM");
            _captureState = CaptureState::ERROR;
            // Continue initialization - waveform capture just won't be available
        } else {
            LOG_DEBUG("Allocated waveform buffers in PSRAM (%u samples)", WAVEFORM_BUFFER_SIZE);
        }
      
        _setHardwarePins(ssPin, sckPin, misoPin, mosiPin, resetPin, interruptPin);
        LOG_DEBUG("Successfully set up hardware pins");
     
        if (!_verifyCommunication()) {
            LOG_ERROR("Failed to communicate with ADE7953");
            return false;
        }
        LOG_DEBUG("Communication with ADE7953 verified");
                
        _setOptimumSettings();
        LOG_DEBUG("Set optimum settings");
        
        _setDefaultParameters();
        LOG_DEBUG("Set default parameters");

        _setConfigurationFromPreferences();
        LOG_DEBUG("Done setting configuration from Preferences");

        _setSampleTimeFromPreferences();
        LOG_DEBUG("Done setting sample time from Preferences");

        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            _setChannelDataFromPreferences(i);
        }
        LOG_DEBUG("Done setting channel data from Preferences");

        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            _setEnergyFromPreferences(i);
        }
        LOG_DEBUG("Done setting energy from Preferences");

        _setupInterrupts();
        LOG_DEBUG("Set up interrupts");

        _startMeterReadingTask();
        LOG_DEBUG("Meter reading task started");

        _startEnergySaveTask();
        LOG_DEBUG("Energy save task started");

        _startHourlyCsvSaveTask();
        LOG_DEBUG("Hourly CSV save task started");

        return true;
    }

    void stop() {
        LOG_DEBUG("Stopping ADE7953...");
        
        // Clean up resources (where the data will also be saved)
        _cleanup();

        deleteMutex(&_spiMutex);
        deleteMutex(&_spiOperationMutex);
        deleteMutex(&_configMutex);
        deleteMutex(&_meterValuesMutex);
        deleteMutex(&_channelDataMutex);
        
        LOG_DEBUG("ADE7953 stopped successfully");
    }

    // Register operations
    // ===================

    int32_t readRegister(uint16_t registerAddress, uint8_t nBits, bool signedData, bool isVerificationRequired) {
        // Ensure the bits are valid (must be 8, 16, 24, or 32 bits)
        if (nBits != BIT_8 && nBits != BIT_16 && nBits != BIT_24 && nBits != BIT_32) {
            LOG_ERROR(
                "Invalid number of bits (%u) for register read operation on register %ld (0x%04lX)",
                nBits, registerAddress, registerAddress
            );
            return INVALID_SPI_READ_WRITE; // Return an invalid value
        }

        // If we need to verify, ensure we are able to take the full SPI operation mutex
        if (isVerificationRequired) {
            if (!acquireMutex(&_spiOperationMutex, ADE7953_SPI_OPERATION_MUTEX_TIMEOUT_MS)) {
                LOG_ERROR("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", registerAddress, registerAddress);
                return INVALID_SPI_READ_WRITE;
            }
        }

        // Acquire direct SPI mutex
        if (!acquireMutex(&_spiMutex, ADE7953_SPI_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire SPI mutex for read operation on register %ld (0x%04lX)", registerAddress, registerAddress);
            if (isVerificationRequired) releaseMutex(&_spiOperationMutex);
            return INVALID_SPI_READ_WRITE;
        }

        // Signal the start of the SPI operation
        digitalWrite(_ssPin, LOW);

        // Send the register address (two 8-bit transfers) and read command
        // Note: The register address is sent in big-endian format (MSB first)
        SPI.transfer((uint8_t)(registerAddress >> 8));
        SPI.transfer((uint8_t)(registerAddress & 0xFF));
        SPI.transfer((uint8_t)(READ_TRANSFER));

        // Create the array of bytes to read (one element per each 8 bits)
        uint8_t response[nBits / 8];
        for (uint8_t i = 0; i < nBits / 8; i++) {
            response[i] = SPI.transfer((uint8_t)(READ_TRANSFER)); // Read the data byte by byte
        }

        // Signal the end of the SPI operation
        digitalWrite(_ssPin, HIGH);

        releaseMutex(&_spiMutex); // Leave as soon as possible since no more direct SPI operations are needed

        // Compose the long response from the byte array
        int32_t longResponse = 0;
        for (uint8_t i = 0; i < nBits / 8; i++) {
            longResponse = (longResponse << 8) | response[i];
        }

        // If it is signed data, we need to check the sign bit
        // and eventually convert it to a negative value
        if (signedData) {
            if (longResponse & (1 << (nBits - 1))) { // Check if the sign bit (the highest bit) is set
                longResponse -= (1 << nBits);
            }
        }

        // Verify the data if required by reading the dedicated ADE7953 register
        if (isVerificationRequired) {
            if (!_verifyLastSpiCommunication(registerAddress, nBits, longResponse, signedData, false)) {
                LOG_DEBUG("Failed to verify last read communication for register %lu (0x%04lX). Value was %ld (0x%04lX)", registerAddress, registerAddress, longResponse, longResponse);
                _recordFailure();
                longResponse = INVALID_SPI_READ_WRITE; // Return an invalid value if verification fails
            }
            releaseMutex(&_spiOperationMutex);
        }

        LOG_VERBOSE(
            "Read successfully %ld from register %lu with %u bits",
            longResponse,
            registerAddress,
            nBits
        );
        return longResponse;
    }

    void writeRegister(uint16_t registerAddress, uint8_t nBits, int32_t data, bool isVerificationRequired) {
        // Ensure the bits are valid (must be 8, 16, 24, or 32 bits)
        if (nBits != BIT_8 && nBits != BIT_16 && nBits != BIT_24 && nBits != BIT_32) {
            LOG_ERROR(
                "Invalid number of bits (%u) for register write operation on register %ld (0x%04lX)",
                nBits, registerAddress, registerAddress
            );
            return; // Return an invalid value
        }

        // If we need to verify, ensure we are able to take the full SPI operation mutex
        if (isVerificationRequired) {
            if (!acquireMutex(&_spiOperationMutex, ADE7953_SPI_OPERATION_MUTEX_TIMEOUT_MS)) {
                LOG_ERROR("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", registerAddress, registerAddress);
                return;
            }
        }

        // Acquire direct SPI mutex
        if (!acquireMutex(&_spiMutex, ADE7953_SPI_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire SPI mutex for write operation on register %ld (0x%04lX)", registerAddress, registerAddress);
            if (isVerificationRequired) releaseMutex(&_spiOperationMutex);
            return;
        }

        // Signal the start of the SPI operation
        digitalWrite(_ssPin, LOW);

        // Send the register address (two 8-bit transfers) and write command
        // Note: The register address is sent in big-endian format (MSB first)
        SPI.transfer((uint8_t)(registerAddress >> 8));
        SPI.transfer((uint8_t)(registerAddress & 0xFF));
        SPI.transfer((uint8_t)(WRITE_TRANSFER));

        // Send the data to write, depending on the number of bits
        // Note: The data is sent in big-endian format (MSB first)
        if (nBits == BIT_32) {
            SPI.transfer((uint8_t)((data >> 24) &  0xFF));
            SPI.transfer((uint8_t)((data >> 16) & 0xFF));
            SPI.transfer((uint8_t)((data >> 8) & 0xFF));
            SPI.transfer((uint8_t)(data & 0xFF));
        } else if (nBits == BIT_24) {
            SPI.transfer((uint8_t)((data >> 16) & 0xFF));
            SPI.transfer((uint8_t)((data >> 8) & 0xFF));
            SPI.transfer((uint8_t)(data & 0xFF));
        } else if (nBits == BIT_16) {
            SPI.transfer((uint8_t)((data >> 8) & 0xFF));
            SPI.transfer((uint8_t)(data & 0xFF));
        } else if (nBits == BIT_8) {
            SPI.transfer((uint8_t)(data & 0xFF));
        } else {
            LOG_ERROR("Invalid number of bits (%u) for register write operation on register %ld (0x%04lX)", nBits, registerAddress, registerAddress);
            digitalWrite(_ssPin, HIGH); // Ensure we release the SS pin
            releaseMutex(&_spiMutex);
            if (isVerificationRequired) releaseMutex(&_spiOperationMutex);
            return; // Return without writing
        }

        // Signal the end of the SPI operation
        digitalWrite(_ssPin, HIGH);

        releaseMutex(&_spiMutex);

        // Verify the data if required by reading the dedicated ADE7953 register
        if (isVerificationRequired) {
            if (!_verifyLastSpiCommunication(registerAddress, nBits, data, false, true)) {
                LOG_WARNING("Failed to verify last write communication for register %ld", registerAddress);
                _recordFailure();
            }
            releaseMutex(&_spiOperationMutex);
        }

        // When writing a register, we inherently change the configuration and thus trigger a CRC change interrupt
        _hasConfigurationChanged = true;

        LOG_DEBUG(
            "Written successfully %ld (0x%04lX) to register %lu (0x%04lX) with %u bits",
            data, data,
            registerAddress, registerAddress,
            nBits
        );
    }

    // Task control
    // ============

    void pauseTasks() {
        LOG_DEBUG("Pausing ADE7953 tasks...");

        _detachInterruptHandler();
        if (_meterReadingTaskHandle != NULL) vTaskSuspend(_meterReadingTaskHandle);
        if (_energySaveTaskHandle != NULL) vTaskSuspend(_energySaveTaskHandle);
        if (_hourlyCsvSaveTaskHandle != NULL) vTaskSuspend(_hourlyCsvSaveTaskHandle);

        LOG_INFO("ADE7953 tasks suspended");
    }

    void resumeTasks() {
        LOG_DEBUG("Resuming ADE7953 tasks...");

        if (_meterReadingTaskHandle != NULL) vTaskResume(_meterReadingTaskHandle);
        if (_energySaveTaskHandle != NULL) vTaskResume(_energySaveTaskHandle);
        if (_hourlyCsvSaveTaskHandle != NULL) vTaskResume(_hourlyCsvSaveTaskHandle);
        _attachInterruptHandler();

        LOG_INFO("ADE7953 tasks resumed");
    }

    // Configuration management
    // ========================

    void getConfiguration(Ade7953Configuration &config) {
        config = _configuration;
    }

    bool setConfiguration(const Ade7953Configuration &config) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire config mutex for setConfiguration");
            return false;
        }

        _configuration = config;
        _applyConfiguration(_configuration);
        
        _saveConfigurationToPreferences();
        
        releaseMutex(&_configMutex);

        LOG_DEBUG("Configuration set successfully");
        return true;
    }

    void resetConfiguration() {
        LOG_DEBUG("Resetting ADE7953 configuration to defaults...");

        Ade7953Configuration defaultConfig;
        setConfiguration(defaultConfig);
    }

    // Configuration management - JSON operations
    // ==========================================

    void getConfigurationAsJson(JsonDocument &jsonDocument) {
        configurationToJson(_configuration, jsonDocument);
    }

    bool setConfigurationFromJson(const JsonDocument &jsonDocument, bool partial)
    {
        Ade7953Configuration config;
        config = _configuration; // Start with current configuration
        if (!configurationFromJson(jsonDocument, config, partial)) {
            LOG_ERROR("Failed to set configuration from JSON");
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(const Ade7953Configuration &config, JsonDocument &jsonDocument)
    {
        jsonDocument["aVGain"] = config.aVGain;
        jsonDocument["aIGain"] = config.aIGain;
        jsonDocument["bIGain"] = config.bIGain;
        jsonDocument["aIRmsOs"] = config.aIRmsOs;
        jsonDocument["bIRmsOs"] = config.bIRmsOs;
        jsonDocument["aWGain"] = config.aWGain;
        jsonDocument["bWGain"] = config.bWGain;
        jsonDocument["aWattOs"] = config.aWattOs;
        jsonDocument["bWattOs"] = config.bWattOs;
        jsonDocument["aVarGain"] = config.aVarGain;
        jsonDocument["bVarGain"] = config.bVarGain;
        jsonDocument["aVarOs"] = config.aVarOs;
        jsonDocument["bVarOs"] = config.bVarOs;
        jsonDocument["aVaGain"] = config.aVaGain;
        jsonDocument["bVaGain"] = config.bVaGain;
        jsonDocument["aVaOs"] = config.aVaOs;
        jsonDocument["bVaOs"] = config.bVaOs;
        jsonDocument["phCalA"] = config.phCalA;
        jsonDocument["phCalB"] = config.phCalB;

        LOG_DEBUG("Successfully converted configuration to JSON");
    }

    bool configurationFromJson(const JsonDocument &jsonDocument, Ade7953Configuration &config, bool partial)
    {
        if (!_validateJsonConfiguration(jsonDocument, partial))
        {
            LOG_WARNING("Invalid JSON configuration");
            return false;
        }

        if (partial) {
            // Update only fields that are present in JSON
            if (jsonDocument["aVGain"].is<int32_t>()) config.aVGain = jsonDocument["aVGain"].as<int32_t>();
            if (jsonDocument["aIGain"].is<int32_t>()) config.aIGain = jsonDocument["aIGain"].as<int32_t>();
            if (jsonDocument["bIGain"].is<int32_t>()) config.bIGain = jsonDocument["bIGain"].as<int32_t>();
            if (jsonDocument["aIRmsOs"].is<int32_t>()) config.aIRmsOs = jsonDocument["aIRmsOs"].as<int32_t>();
            if (jsonDocument["bIRmsOs"].is<int32_t>()) config.bIRmsOs = jsonDocument["bIRmsOs"].as<int32_t>();
            if (jsonDocument["aWGain"].is<int32_t>()) config.aWGain = jsonDocument["aWGain"].as<int32_t>();
            if (jsonDocument["bWGain"].is<int32_t>()) config.bWGain = jsonDocument["bWGain"].as<int32_t>();
            if (jsonDocument["aWattOs"].is<int32_t>()) config.aWattOs = jsonDocument["aWattOs"].as<int32_t>();
            if (jsonDocument["bWattOs"].is<int32_t>()) config.bWattOs = jsonDocument["bWattOs"].as<int32_t>();
            if (jsonDocument["aVarGain"].is<int32_t>()) config.aVarGain = jsonDocument["aVarGain"].as<int32_t>();
            if (jsonDocument["bVarGain"].is<int32_t>()) config.bVarGain = jsonDocument["bVarGain"].as<int32_t>();
            if (jsonDocument["aVarOs"].is<int32_t>()) config.aVarOs = jsonDocument["aVarOs"].as<int32_t>();
            if (jsonDocument["bVarOs"].is<int32_t>()) config.bVarOs = jsonDocument["bVarOs"].as<int32_t>();
            if (jsonDocument["aVaGain"].is<int32_t>()) config.aVaGain = jsonDocument["aVaGain"].as<int32_t>();
            if (jsonDocument["bVaGain"].is<int32_t>()) config.bVaGain = jsonDocument["bVaGain"].as<int32_t>();
            if (jsonDocument["aVaOs"].is<int32_t>()) config.aVaOs = jsonDocument["aVaOs"].as<int32_t>();
            if (jsonDocument["bVaOs"].is<int32_t>()) config.bVaOs = jsonDocument["bVaOs"].as<int32_t>();
            if (jsonDocument["phCalA"].is<int32_t>()) config.phCalA = jsonDocument["phCalA"].as<int32_t>();
            if (jsonDocument["phCalB"].is<int32_t>()) config.phCalB = jsonDocument["phCalB"].as<int32_t>();
        } else {
            // Full update - set all fields
            config.aVGain = jsonDocument["aVGain"].as<int32_t>();
            config.aIGain = jsonDocument["aIGain"].as<int32_t>();
            config.bIGain = jsonDocument["bIGain"].as<int32_t>();
            config.aIRmsOs = jsonDocument["aIRmsOs"].as<int32_t>();
            config.bIRmsOs = jsonDocument["bIRmsOs"].as<int32_t>();
            config.aWGain = jsonDocument["aWGain"].as<int32_t>();
            config.bWGain = jsonDocument["bWGain"].as<int32_t>();
            config.aWattOs = jsonDocument["aWattOs"].as<int32_t>();
            config.bWattOs = jsonDocument["bWattOs"].as<int32_t>();
            config.aVarGain = jsonDocument["aVarGain"].as<int32_t>();
            config.bVarGain = jsonDocument["bVarGain"].as<int32_t>();
            config.aVarOs = jsonDocument["aVarOs"].as<int32_t>();
            config.bVarOs = jsonDocument["bVarOs"].as<int32_t>();
            config.aVaGain = jsonDocument["aVaGain"].as<int32_t>();
            config.bVaGain = jsonDocument["bVaGain"].as<int32_t>();
            config.aVaOs = jsonDocument["aVaOs"].as<int32_t>();
            config.bVaOs = jsonDocument["bVaOs"].as<int32_t>();
            config.phCalA = jsonDocument["phCalA"].as<int32_t>();
            config.phCalB = jsonDocument["phCalB"].as<int32_t>();
        }

        LOG_DEBUG("Successfully converted JSON to configuration%s", partial ? " (partial)" : "");
        return true;
    }

    // Sample time management
    // ======================

    uint64_t getSampleTime() { 
        return _sampleTime; 
    }

    bool setSampleTime(uint64_t sampleTime) {
        if (sampleTime < MINIMUM_SAMPLE_TIME) {
            LOG_WARNING("Sample time %lu is below minimum %lu", sampleTime, MINIMUM_SAMPLE_TIME);
            return false;
        }

        _sampleTime = sampleTime;
        _updateSampleTime();
        _saveSampleTimeToPreferences();

        return true;
    }

    // Channel data management
    // =======================

    bool isChannelActive(uint8_t channelIndex) {
        if (channelIndex == INVALID_CHANNEL) return false; // Invalid (and expected to be) channel, thus no logs

        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        return _channelData[channelIndex].active;
    }

    bool hasChannelValidMeasurements(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        return CustomTime::isUnixTimeValid(_meterValues[channelIndex].lastUnixTimeMilliseconds);
    }

    void getChannelLabel(uint8_t channelIndex, char* buffer, size_t bufferSize) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return;
        }

        ChannelData channelData;
        getChannelData(channelData, channelIndex);

        snprintf(buffer, bufferSize, "%s", channelData.label); // Fallback is the default constructor value if getChannelData failed
    }

    bool getChannelData(ChannelData &channelData, uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        if (!acquireMutex(&_channelDataMutex)) {
            LOG_ERROR("Failed to acquire mutex for channel data");
            return false;
        }

        channelData = _channelData[channelIndex];
        releaseMutex(&_channelDataMutex);
        return true;
    }

    bool setChannelData(const ChannelData &channelData, uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        if (!acquireMutex(&_channelDataMutex)) {
            LOG_ERROR("Failed to acquire mutex for channel data");
            return false;
        }

        // Protect channel 0 from being disabled
        if (channelIndex == 0 && !channelData.active) {
            LOG_WARNING("Attempt to disable channel 0 blocked - channel 0 must remain active");
            _channelData[channelIndex].active = true;
        } else {
            _channelData[channelIndex] = channelData;
        }

        releaseMutex(&_channelDataMutex);

        _updateChannelData(channelIndex);
        _saveChannelDataToPreferences(channelIndex);
        #ifdef HAS_SECRETS
        Mqtt::requestChannelPublish();
        #endif

        LOG_DEBUG("Successfully set channel data for channel %lu", channelIndex);
        return true;
    }

    void resetChannelData(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return;
        }

        ChannelData channelData; // Default constructor initializes to default values
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            setChannelData(channelData, i);
        }

        LOG_DEBUG("Successfully reset channel data for channel %lu", channelIndex);
    }

    // Channel data management - JSON operations
    // =========================================

    bool getChannelDataAsJson(JsonDocument &jsonDocument, uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        ChannelData channelData;
        if (!getChannelData(channelData, channelIndex)) {
            LOG_WARNING("Failed to get channel data as JSON for channel %lu", channelIndex);
            return false;
        }
        channelDataToJson(channelData, jsonDocument);
        return true;
    }

    bool getAllChannelDataAsJson(JsonDocument &jsonDocument) {
        for (uint8_t channelIndex = 0; channelIndex < CHANNEL_COUNT; channelIndex++) {
            SpiRamAllocator allocator;
            JsonDocument channelDoc(&allocator);
            if (!getChannelDataAsJson(channelDoc, channelIndex)) {
                LOG_WARNING("Failed to get channel data as JSON for channel %lu", channelIndex);
                continue;
            }
            jsonDocument[channelIndex] = channelDoc;
        }

        if (!jsonDocument.isNull() && jsonDocument.size() > 0) return true;
        else return false;
    }

    bool setChannelDataFromJson(const JsonDocument &jsonDocument, bool partial) {
        if (!_validateChannelDataJson(jsonDocument, partial)) {
            LOG_WARNING("Invalid channel data JSON. Skipping setting data");
            return false;
        }

        uint8_t channelIndex = jsonDocument["index"].as<uint8_t>();
        
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Invalid channel index: %u. Skipping setting data", channelIndex);
            return false;
        }

        ChannelData channelData;
        if (!getChannelData(channelData, channelIndex)) {
            LOG_WARNING("Failed to get channel data from JSON for channel %u", channelIndex);
            return false;
        }

        channelDataFromJson(jsonDocument, channelData, partial);

        if (!setChannelData(channelData, channelIndex)) {
            LOG_WARNING("Failed to set channel data from JSON for channel %u", channelIndex);
            return false;
        }

        return true;
    }

    void channelDataToJson(const ChannelData &channelData, JsonDocument &jsonDocument) {
        jsonDocument["index"] = channelData.index;
        jsonDocument["active"] = channelData.active;
        jsonDocument["reverse"] = channelData.reverse;
        jsonDocument["label"] = JsonString(channelData.label); // Ensure it is not a dangling pointer
        jsonDocument["phase"] = channelData.phase;

        jsonDocument["ctSpecification"]["currentRating"] = channelData.ctSpecification.currentRating;
        jsonDocument["ctSpecification"]["voltageOutput"] = channelData.ctSpecification.voltageOutput;
        jsonDocument["ctSpecification"]["scalingFraction"] = channelData.ctSpecification.scalingFraction;

        LOG_VERBOSE("Successfully converted channel data to JSON for channel %u", channelData.index);
    }

    void channelDataFromJson(const JsonDocument &jsonDocument, ChannelData &channelData, bool partial) {
        if (partial) {
            // Update only fields that are present in JSON
            if (jsonDocument["index"].is<uint8_t>()) channelData.index = jsonDocument["index"].as<uint8_t>();
            if (jsonDocument["active"].is<bool>()) channelData.active = jsonDocument["active"].as<bool>();
            if (jsonDocument["reverse"].is<bool>()) channelData.reverse = jsonDocument["reverse"].as<bool>();
            if (jsonDocument["label"].is<const char*>()) {
                snprintf(channelData.label, sizeof(channelData.label), "%s", jsonDocument["label"].as<const char*>());
            }
            if (jsonDocument["phase"].is<uint8_t>()) channelData.phase = static_cast<Phase>(jsonDocument["phase"].as<uint8_t>());
            
            // CT Specification fields
            if (jsonDocument["ctSpecification"]["currentRating"].is<float>()) {
                channelData.ctSpecification.currentRating = jsonDocument["ctSpecification"]["currentRating"].as<float>();
            }
            if (jsonDocument["ctSpecification"]["voltageOutput"].is<float>()) {
                channelData.ctSpecification.voltageOutput = jsonDocument["ctSpecification"]["voltageOutput"].as<float>();
            }
            if (jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) {
                channelData.ctSpecification.scalingFraction = jsonDocument["ctSpecification"]["scalingFraction"].as<float>();
            }
        } else {
            // Full update - set all fields
            channelData.index = jsonDocument["index"].as<uint8_t>();
            channelData.active = jsonDocument["active"].as<bool>();
            channelData.reverse = jsonDocument["reverse"].as<bool>();
            snprintf(channelData.label, sizeof(channelData.label), "%s", jsonDocument["label"].as<const char*>());
            channelData.phase = static_cast<Phase>(jsonDocument["phase"].as<uint8_t>());
            
            channelData.ctSpecification.currentRating = jsonDocument["ctSpecification"]["currentRating"].as<float>();
            channelData.ctSpecification.voltageOutput = jsonDocument["ctSpecification"]["voltageOutput"].as<float>();
            channelData.ctSpecification.scalingFraction = jsonDocument["ctSpecification"]["scalingFraction"].as<float>();
        }
    }

    // Energy data management
    // ======================

    void resetEnergyValues() {
        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            return;
        }

        // Set all energy values to 0 (safe since we acquired the mutex)
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            _meterValues[i].activeEnergyImported = 0.0f;
            _meterValues[i].activeEnergyExported = 0.0f;
            _meterValues[i].reactiveEnergyImported = 0.0f;
            _meterValues[i].reactiveEnergyExported = 0.0f;
            _meterValues[i].apparentEnergy = 0.0f;
        }

        releaseMutex(&_meterValuesMutex);

        // Clear energy preferences
        Preferences preferences;
        preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false);
        preferences.clear();
        preferences.end();

        // Remove all CSV energy files
        File root = LittleFS.open(ENERGY_CSV_PREFIX "/"); // We already know all the historical energy files will be inside the ENERGY_CSV_PREFIX folder
        File file = root.openNextFile();
        while (file) {
            if (strstr(file.name(), ".csv.gz") || strstr(file.name(), ".csv")) { // Remove the CSV files (current day is not compressed, thus only .csv)
                char fullPathFile[NAME_BUFFER_SIZE]; // Since the file name would only be the name inside the ENERGY_CSV_PREFIX folder, we need to prepend the folder path
                snprintf(fullPathFile, sizeof(fullPathFile), "%s/%s", ENERGY_CSV_PREFIX, file.name());
                
                // We now need to close the file before removing it, otherwise we get esp_littlefs: Failed to unlink path "/energy/2025-09-23.csv". Has open FD.
                file.close();
                LittleFS.remove(fullPathFile);
                LOG_DEBUG("Removed energy CSV file: %s", fullPathFile);
            }
            file = root.openNextFile();
        }
        root.close();

        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) _saveEnergyToPreferences(i);

        LOG_INFO("Successfully reset energy values to 0");
    }

    bool setEnergyValues(
        uint8_t channelIndex,
        float activeEnergyImported,
        float activeEnergyExported,
        float reactiveEnergyImported,
        float reactiveEnergyExported,
        float apparentEnergy
    ) {
        if (!isChannelValid(channelIndex)) {
            LOG_ERROR("Invalid channel index %d", channelIndex);
            return false;
        }

        if (activeEnergyImported < 0 || activeEnergyExported < 0 || 
            reactiveEnergyImported < 0 || reactiveEnergyExported < 0 || 
            apparentEnergy < 0) {
            LOG_ERROR("Energy values must be non-negative");
            return false;
        }

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            return false;
        }

        _meterValues[channelIndex].activeEnergyImported = activeEnergyImported;
        _meterValues[channelIndex].activeEnergyExported = activeEnergyExported;
        _meterValues[channelIndex].reactiveEnergyImported = reactiveEnergyImported;
        _meterValues[channelIndex].reactiveEnergyExported = reactiveEnergyExported;
        _meterValues[channelIndex].apparentEnergy = apparentEnergy;

        releaseMutex(&_meterValuesMutex);

        _saveEnergyToPreferences(channelIndex);
        
        LOG_INFO("Successfully set energy values for channel %d", channelIndex);
        return true;
    }

    // Data output
    // ===========

    bool singleMeterValuesToJson(JsonDocument &jsonDocument, uint8_t channelIndex) { // Around 250 bytes per channel of meter values
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %u", channelIndex);
            return false;
        }

        MeterValues meterValues;
        if (!getMeterValues(meterValues, channelIndex)) { // We use it here to ensure non-concurrent access
            LOG_WARNING("Failed to get meter values for channel %d", channelIndex);
            return false;
        }

        // Reduce the decimals since we don't have or need too much precision, and we save on space
        jsonDocument["voltage"] = roundToDecimals(meterValues.voltage, VOLTAGE_DECIMALS);
        jsonDocument["current"] = roundToDecimals(meterValues.current, CURRENT_DECIMALS);
        jsonDocument["activePower"] = roundToDecimals(meterValues.activePower, POWER_DECIMALS);
        jsonDocument["apparentPower"] = roundToDecimals(meterValues.apparentPower, POWER_DECIMALS);
        jsonDocument["reactivePower"] = roundToDecimals(meterValues.reactivePower, POWER_DECIMALS);
        jsonDocument["powerFactor"] = roundToDecimals(meterValues.powerFactor, POWER_FACTOR_DECIMALS);
        jsonDocument["activeEnergyImported"] = roundToDecimals(meterValues.activeEnergyImported, ENERGY_DECIMALS);
        jsonDocument["activeEnergyExported"] = roundToDecimals(meterValues.activeEnergyExported, ENERGY_DECIMALS);
        jsonDocument["reactiveEnergyImported"] = roundToDecimals(meterValues.reactiveEnergyImported, ENERGY_DECIMALS);
        jsonDocument["reactiveEnergyExported"] = roundToDecimals(meterValues.reactiveEnergyExported, ENERGY_DECIMALS);
        jsonDocument["apparentEnergy"] = roundToDecimals(meterValues.apparentEnergy, ENERGY_DECIMALS);

        return true;
    }


    bool fullMeterValuesToJson(JsonDocument &jsonDocument) {
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            // Here we also ensure the channel has valid measurements since we have the "duty" to pass all the correct data
            if (isChannelActive(i) && hasChannelValidMeasurements(i)) {
                ChannelData channelData;
                if (!getChannelData(channelData, i)) {
                    LOG_WARNING("Failed to get channel data for channel %u", i);
                    continue;
                }

                JsonObject _jsonChannel = jsonDocument.add<JsonObject>();
                _jsonChannel["index"] = i;
                _jsonChannel["label"] = JsonString(channelData.label); // Ensure the string is not a dangling pointer
                _jsonChannel["phase"] = channelData.phase;

                SpiRamAllocator allocator;
                JsonDocument jsonData(&allocator);
                if (!singleMeterValuesToJson(jsonData, i)) {
                    LOG_WARNING("Failed to convert single meter values to JSON for channel %d", i);
                    continue;
                }
                _jsonChannel["data"] = jsonData.as<JsonObject>();
            }
        }

        if (!jsonDocument.isNull() && jsonDocument.size() > 0) return true;
        else return false;
    }

    bool getMeterValues(MeterValues &meterValues, uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Channel index out of bounds: %lu", channelIndex);
            return false;
        }

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire meter values mutex");
            return false;
        }
        meterValues = _meterValues[channelIndex];
        releaseMutex(&_meterValuesMutex);
        return true;
    }

    // Aggregated power calculations 
    // =============================

    float getAggregatedActivePower(bool includeChannel0) {
        float sum = 0.0f;
        uint8_t activeChannelCount = 0;

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            return 0.0f; // Return 0 if mutex acquisition fails
        }
        for (uint8_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].activePower;
                activeChannelCount++;
            }
        }
        releaseMutex(&_meterValuesMutex);
        return activeChannelCount > 0 ? sum : 0.0f;
    }

    float getAggregatedReactivePower(bool includeChannel0) {
        float sum = 0.0f;
        uint8_t activeChannelCount = 0;

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            return 0.0f; // Return 0 if mutex acquisition fails
        }
        for (uint8_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].reactivePower;
                activeChannelCount++;
            }
        }
        releaseMutex(&_meterValuesMutex);
        return activeChannelCount > 0 ? sum : 0.0f;
    }

    float getAggregatedApparentPower(bool includeChannel0) {
        float sum = 0.0f;
        uint8_t activeChannelCount = 0;

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            return 0.0f; // Return 0 if mutex acquisition fails
        }
        for (uint8_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].apparentPower;
                activeChannelCount++;
            }
        }
        releaseMutex(&_meterValuesMutex);
        return activeChannelCount > 0 ? sum : 0.0f;
    }

    float getAggregatedPowerFactor(bool includeChannel0) {
        float _aggregatedActivePower = getAggregatedActivePower(includeChannel0);
        float _aggregatedApparentPower = getAggregatedApparentPower(includeChannel0);

        return _aggregatedApparentPower > 0 ? _aggregatedActivePower / _aggregatedApparentPower : 0.0f;
    }

    // Grid frequency
    // ==============

    float getGridFrequency() { 
        return _gridFrequency; 
    }

    // Private function implementations
    // =========================================================
    // =========================================================

    // Hardware initialization and control
    // ===================================

    static void _setHardwarePins(
        uint8_t ssPin,
        uint8_t sckPin,
        uint8_t misoPin,
        uint8_t mosiPin,
        uint8_t resetPin,
        uint8_t interruptPin
    ) {
        _ssPin = ssPin;
        _sckPin = sckPin;
        _misoPin = misoPin;
        _mosiPin = mosiPin;
        _resetPin = resetPin;
        _interruptPin = interruptPin;
        
        pinMode(_ssPin, OUTPUT);
        pinMode(_sckPin, OUTPUT);
        pinMode(_misoPin, INPUT);
        pinMode(_mosiPin, OUTPUT);
        pinMode(_resetPin, OUTPUT);
        pinMode(_interruptPin, INPUT);

        SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);
        SPI.setFrequency(ADE7953_SPI_FREQUENCY); //  Max ADE7953 clock is 2MHz
        SPI.setDataMode(SPI_MODE0);
        SPI.setBitOrder(MSBFIRST);
        digitalWrite(_ssPin, HIGH);

        LOG_DEBUG("Successfully set hardware pins");
    }

    void _reset() {
        digitalWrite(_resetPin, LOW);
        delay(ADE7953_RESET_LOW_DURATION);
        digitalWrite(_resetPin, HIGH);
        delay(ADE7953_RESET_LOW_DURATION);

        LOG_DEBUG("Reset ADE7953");
    }

    void _setOptimumSettings()
    {
        writeRegister(UNLOCK_OPTIMUM_REGISTER, BIT_8, UNLOCK_OPTIMUM_REGISTER_VALUE);
        writeRegister(Reserved_16, BIT_16, DEFAULT_OPTIMUM_REGISTER);

        LOG_DEBUG("Optimum settings applied");
    }

    void _setDefaultParameters()
    {
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::VOLTAGE);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::CURRENT);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::B, MeasurementType::CURRENT);

        writeRegister(DISNOLOAD_8, BIT_8, DEFAULT_DISNOLOAD_REGISTER);

        // To compute the no load register, we use X_NOLOAD = 65536 - DYNAMIC_RANGE / 1.4 (as per datasheet)
        // The higher the dynamic range, the lower the no load value (thus we are able to pick up smaller currents)
        int32_t xNoLoad = 65536UL - (int32_t)(DEFAULT_NOLOAD_DYNAMIC_RANGE / 1.4);

        writeRegister(AP_NOLOAD_32, BIT_32, xNoLoad);
        writeRegister(VAR_NOLOAD_32, BIT_32, xNoLoad);
        writeRegister(VA_NOLOAD_32, BIT_32, xNoLoad);

        writeRegister(LCYCMODE_8, BIT_8, DEFAULT_LCYCMODE_REGISTER);

        writeRegister(CONFIG_16, BIT_16, DEFAULT_CONFIG_REGISTER);

        LOG_DEBUG("Default parameters set");
    }

    // System management
    // =================

    void _initializeMutexes()
    {
        if (!createMutexIfNeeded(&_spiMutex)) {
            LOG_ERROR("Failed to create SPI mutex");
            return;
        }
        LOG_DEBUG("SPI mutex created successfully");

        if (!createMutexIfNeeded(&_spiOperationMutex)) {
            LOG_ERROR("Failed to create SPI operation mutex");
            deleteMutex(&_spiMutex);
            return;
        }
        LOG_DEBUG("SPI operation mutex created successfully");

        if (!createMutexIfNeeded(&_configMutex)) {
            LOG_ERROR("Failed to create config mutex");
            deleteMutex(&_spiMutex);
            deleteMutex(&_spiOperationMutex);
            return;
        }
        LOG_DEBUG("Config mutex created successfully");

        if (!createMutexIfNeeded(&_meterValuesMutex)) {
            LOG_ERROR("Failed to create meter values mutex");
            deleteMutex(&_spiMutex);
            deleteMutex(&_spiOperationMutex);
            deleteMutex(&_meterValuesMutex);
        }
        LOG_DEBUG("Meter values mutex created successfully");

        if (!createMutexIfNeeded(&_channelDataMutex)) {
            LOG_ERROR("Failed to create channel data mutex");
            deleteMutex(&_spiMutex);
            deleteMutex(&_spiOperationMutex);
            deleteMutex(&_meterValuesMutex);
            deleteMutex(&_channelDataMutex);
            return;
        }
        LOG_DEBUG("Channel data mutex created successfully");

        LOG_DEBUG("All mutexes initialized successfully");
    }

    void _cleanup() {
        LOG_DEBUG("Cleaning up ADE7953 resources...");
        
        // Stop all tasks first
        _stopMeterReadingTask();
        _stopEnergySaveTask();
        _stopHourlyCsvSaveTask();
        
        // Reset failure counters during cleanup
        _failureCount = 0;
        _firstFailureTime = 0;
        _criticalFailureCount = 0;
        _firstCriticalFailureTime = 0;
        
        // Save final energy data if not already saved
        LOG_DEBUG("Saving final energy data during cleanup");
        _saveEnergyComplete();

        // Free waveform capture buffers
        if (_voltageWaveformBuffer) {
            free(_voltageWaveformBuffer);
            _voltageWaveformBuffer = nullptr;
        }
        if (_currentWaveformBuffer) {
            free(_currentWaveformBuffer);
            _currentWaveformBuffer = nullptr;
        }
        if (_microsWaveformBuffer) {
            free(_microsWaveformBuffer);
            _microsWaveformBuffer = nullptr;
        }
        LOG_DEBUG("Cleaned up waveform capture buffers");

        LOG_DEBUG("Cleaned up tasks and energy saved");
    }

    /**
     * Verifies the communication with the ADE7953 device.
     * This function reads a specific register from the device and checks if it matches the default value.
     * 
     * @return true if the communication with the ADE7953 is successful, false otherwise.
     */
    bool _verifyCommunication() {
        LOG_DEBUG("Verifying communication with Ade7953...");
        
        uint32_t attempt = 0;
        bool success = false;
        uint64_t lastMillisAttempt = 0;

        uint32_t loops = 0;
        while (attempt < ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS && !success && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            if (millis64() - lastMillisAttempt < ADE7953_VERIFY_COMMUNICATION_INTERVAL) {
                continue;
            }

            LOG_DEBUG("Attempt (%lu/%lu) to communicate with ADE7953", attempt+1, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
            
            _reset();
            attempt++;
            lastMillisAttempt = millis64();

            if ((readRegister(AP_NOLOAD_32, BIT_32, false)) == DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
                LOG_DEBUG("Communication successful with ADE7953");
                return true;
            }
        }

        LOG_WARNING("Failed to communicate with ADE7953 after %lu attempts", ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        return false;
    }

    // Interrupt handling
    // ==================

    void _setupInterrupts() {
        // Enable only CYCEND interrupt for line cycle end detection (bit 18)
        writeRegister(IRQENA_32, BIT_32, DEFAULT_IRQENA_REGISTER);
        
        // Clear any existing interrupt status
        readRegister(RSTIRQSTATA_32, BIT_32, false);
        readRegister(RSTIRQSTATB_32, BIT_32, false);

        LOG_DEBUG("ADE7953 interrupts enabled: CYCEND, RESET");
    }

    Ade7953InterruptType _handleInterrupt() 
    {    
        int32_t statusA = readRegister(RSTIRQSTATA_32, BIT_32, false);
        // No need to read for channel B (only channel A has the relevant information for use)

        // Check in order of priority, but ONLY check interrupts that are actually enabled
        
        // CYCEND is always enabled - check it first as it's the primary interrupt
        if (statusA & (1 << IRQSTATA_CYCEND_BIT)) {
            return Ade7953InterruptType::CYCEND;
        } else if (statusA & (1 << IRQSTATA_RESET_BIT)) { 
            return Ade7953InterruptType::RESET;
        } else if (statusA & (1 << IRQSTATA_CRC_BIT)) {
            return Ade7953InterruptType::CRC_CHANGE;
        } else {
            // Just log the unhandled status
            LOG_WARNING("Unhandled ADE7953 interrupt status: 0x%08lX | %s", statusA, _irqstataBitName(statusA));
            return Ade7953InterruptType::OTHER;
        }
    }

    void _attachInterruptHandler() {
        // Detach only if already attached (avoid priting error)
        if (_meterReadingTaskHandle != NULL) _detachInterruptHandler();
        attachInterrupt(digitalPinToInterrupt(_interruptPin), _isrHandler, FALLING);
    }

    void _detachInterruptHandler() {
        detachInterrupt(digitalPinToInterrupt(_interruptPin));
    }

    void _isrHandler()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        statistics.ade7953TotalInterrupts++;
        _interruptHandledChannelA = false;
        _interruptHandledChannelB = false;

        // Signal the task to handle the interrupt - let the task determine the cause
        if (_ade7953InterruptSemaphore != NULL) {
            xSemaphoreGiveFromISR(_ade7953InterruptSemaphore, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
        }
    }

    void _handleCycendInterrupt(uint64_t linecycUnix) {
        LOG_VERBOSE("Line cycle end detected on Channel A");
        statistics.ade7953TotalHandledInterrupts++;
        
        if (_hasToSkipReading) {
            LOG_VERBOSE("Purging energy registers for Channel B (channel %lu)", _currentChannel);
            _purgeEnergyRegisters(Ade7953Channel::B);
            _hasToSkipReading = false;
        } else {
            // Next linecyc we skip since we changed channel
            _hasToSkipReading = true;

            // Since the data is frozen anyway, we can first retrieve the waveform, then the channel reading
            if (_captureState == CaptureState::ARMED && _currentChannel == _captureRequestedChannel) {
                LOG_DEBUG("Matched requested channel %u. Starting waveform capture via polling", _currentChannel);
                
                _captureChannel = _currentChannel;
                _captureSampleCount = 0;
                _captureStartUnixMillis = CustomTime::getUnixTimeMilliseconds();
                _captureStartMicros = micros64();
                _captureState = CaptureState::CAPTURING;
                
                // Immediately start polling for waveform samples
                _pollWaveformSamples();
            }
            
            // Process current channel (if active)
            if (_currentChannel != INVALID_CHANNEL) _processChannelReading(_currentChannel, linecycUnix);

            // Thanks to the linecyc approach, the data in the ADE7953 is "frozen"
            // until the next linecyc interrupt is received, which allows us to switch to the
            // next channel after we've completely read what we need to read (and in any case, the
            // next reading will be purged)
            _currentChannel = _findNextActiveChannel(_currentChannel);

            // Weird way to ensure we don't go below 0 and we set the multiplexer to the channel minus 
            // 1 (since channel 0 does not pass through the multiplexer)
            Multiplexer::setChannel((uint8_t)(max(static_cast<int>(_currentChannel) - 1, 0)));
        }
        
        // Check for channel 0 waveform capture separately (channel 0 doesn't go through multiplexer rotation)
        // Channel 0 is always active and processed on every CYCEND, so we check for armed state here
        if (_captureState == CaptureState::ARMED && _captureRequestedChannel == 0) {
            LOG_DEBUG("Starting waveform capture for channel 0 via polling");
            
            _captureChannel = 0;
            _captureSampleCount = 0;
            _captureStartUnixMillis = CustomTime::getUnixTimeMilliseconds();
            _captureStartMicros = micros64();
            _captureState = CaptureState::CAPTURING;
            
            // Immediately start polling for waveform samples
            _pollWaveformSamples();
        }
        
        // Always process channel 0 as it is on a separate ADE7953 channel
        _processChannelReading(0, linecycUnix);
    }

    void _pollWaveformSamples() {
        // This function performs tight polling of instantaneous waveform registers
        // Called from CYCEND interrupt context - we are sure we are in a "clean" state (no multiplexer switching)
        // Poll as fast as possible with no artificial rate limiting - let SPI run at maximum speed
        
        Ade7953Channel ade7953Channel = (_captureChannel == 0) ? Ade7953Channel::A : Ade7953Channel::B;
        
        uint32_t loops = 0;
        while (_captureSampleCount < WAVEFORM_BUFFER_SIZE && loops < MAX_LOOP_ITERATIONS) {
            uint64_t currentMicros = micros64();
            loops++;
            
            // Stop if we've captured for max duration
            if (currentMicros - _captureStartMicros >= WAVEFORM_CAPTURE_MAX_DURATION_MICROS) break;
            
            // Read instantaneous values directly from registers (no rate limiting)
            _voltageWaveformBuffer[_captureSampleCount] = _readVoltageInstantaneous();
            _currentWaveformBuffer[_captureSampleCount] = _readCurrentInstantaneous(ade7953Channel);
            _microsWaveformBuffer[_captureSampleCount] = currentMicros - _captureStartMicros;
            
            _captureSampleCount++;
        }
        
        uint64_t totalDurationMs = (micros64() - _captureStartMicros) / 1000;
        float effectiveSampleRate = _captureSampleCount > 0 ? (_captureSampleCount * 1000.0f) / totalDurationMs : 0;
        
        _captureState = CaptureState::COMPLETE;
        LOG_INFO("Waveform capture complete for channel %u: %u samples in %llu ms (%.0f Hz)", 
            _captureChannel, _captureSampleCount, totalDurationMs, effectiveSampleRate);
    }

    void _handleCrcChangeInterrupt() {
        if (_hasConfigurationChanged) { // We were expecting this change, thus no need to log a warning
            _hasConfigurationChanged = false; // Reset the flag
            LOG_DEBUG("Expected configuration change detected");
        } else {
            LOG_WARNING("Unexpected configuration change detected - this may indicate a device issue");
        }
    }

    void _handleResetInterrupt() {
        // This should never happen unless a powerful power drop occurs (which would likely reset also the ESP32) 
        LOG_WARNING("TO BE IMPLEMENTED: ADE7953 reset interrupt detected - reinitializing device");
    }

    void _handleOtherInterrupt() {
        LOG_WARNING("TO BE IMPLEMENTED: unknown ADE7953 interrupt - this may indicate an unexpected condition");
    }

    // Tasks
    // =====

    void _startMeterReadingTask() {
        if (_meterReadingTaskHandle != NULL) {
            LOG_DEBUG("ADE7953 meter reading task is already running");
            return;
        }

        // Create interrupt semaphore if not exists
        if (_ade7953InterruptSemaphore == NULL) {
            _ade7953InterruptSemaphore = xSemaphoreCreateBinary();
            if (_ade7953InterruptSemaphore == NULL) {
                LOG_ERROR("Failed to create ADE7953 interrupt semaphore");
                return;
            }
        }

        // Attach interrupt handler
        _attachInterruptHandler();
        
        LOG_DEBUG("Starting ADE7953 meter reading task with %d bytes stack in internal RAM", ADE7953_METER_READING_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _meterReadingTask, 
            ADE7953_METER_READING_TASK_NAME, 
            ADE7953_METER_READING_TASK_STACK_SIZE, 
            nullptr, 
            ADE7953_METER_READING_TASK_PRIORITY, 
            &_meterReadingTaskHandle);
        
        if (result != pdPASS) {
            LOG_ERROR("Failed to create ADE7953 meter reading task");
            _meterReadingTaskHandle = NULL;
        }
    }

    void _stopMeterReadingTask() {
        // Detach interrupt handler
        _detachInterruptHandler();
        
        // Stop task gracefully using utils function
        stopTaskGracefully(&_meterReadingTaskHandle, "ADE7953 meter reading task");
        
        // Clean up semaphore
        if (_ade7953InterruptSemaphore != NULL) {
            vSemaphoreDelete(_ade7953InterruptSemaphore);
            _ade7953InterruptSemaphore = NULL;
        }
    }

    void _meterReadingTask(void *parameter)
    {
        LOG_DEBUG("ADE7953 meter reading task started");
        
        _meterReadingTaskShouldRun = true;
        
        while (_meterReadingTaskShouldRun)
        {
            // Wait for interrupt signal with timeout
            if (
                _ade7953InterruptSemaphore != NULL &&
                xSemaphoreTake(_ade7953InterruptSemaphore, pdMS_TO_TICKS(ADE7953_INTERRUPT_TIMEOUT_MS + _sampleTime)) == pdTRUE
            ) {
                // Grab as quickly as possible the current unix time in milliseconds
                // that refers to the "true" time at which the data is temporarily frozen in the ADE7953
                uint64_t linecycUnix = CustomTime::getUnixTimeMilliseconds();

                // Handle the interrupt and determine its type (need to read it from ADE7953 since 
                // we only got an interrupt, but we don't know the reason)
                Ade7953InterruptType interruptType = _handleInterrupt();

                // Process based on interrupt type
                switch (interruptType)
                {
                    case Ade7953InterruptType::CYCEND:
                        _handleCycendInterrupt(linecycUnix);
                        break;

                    case Ade7953InterruptType::RESET:
                        _handleResetInterrupt();
                        break;
                        
                    case Ade7953InterruptType::CRC_CHANGE:
                        _handleCrcChangeInterrupt();
                        break;

                    case Ade7953InterruptType::OTHER:
                        _handleOtherInterrupt();
                        break;
                    default:
                        // Already logged in _handleInterrupt(), just continue
                        break;
                }
            } else {
                _recordCriticalFailure();
                #ifdef ENV_DEV
                LOG_DEBUG("No ADE7953 interrupt received within timeout, checking for stop notification");
                #else
                LOG_WARNING("No ADE7953 interrupt received within time expected, this indicates some problems.");
                #endif

                // Clear any interrupt flag to ensure we don't remain stuck
                readRegister(RSTIRQSTATA_32, BIT_32, false);
            }
            
            // Check for stop notification (non-blocking) - this gives immediate shutdown response
            if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
                _meterReadingTaskShouldRun = false;
                break;
            }
        }

        LOG_DEBUG("ADE7953 meter reading task stopping");
        _meterReadingTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void _startEnergySaveTask() {
        if (_energySaveTaskHandle != NULL) {
            LOG_DEBUG("ADE7953 energy save task is already running");
            return;
        }

        LOG_DEBUG("Starting ADE7953 energy save task with %d bytes stack in internal RAM (uses NVS)", ADE7953_ENERGY_SAVE_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _energySaveTask,
            ADE7953_ENERGY_SAVE_TASK_NAME,
            ADE7953_ENERGY_SAVE_TASK_STACK_SIZE,
            nullptr,
            ADE7953_ENERGY_SAVE_TASK_PRIORITY,
            &_energySaveTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create ADE7953 energy save task");
            _energySaveTaskHandle = NULL;
        }
    }

    void _stopEnergySaveTask() {
        stopTaskGracefully(&_energySaveTaskHandle, "ADE7953 energy save task");
    }

    void _energySaveTask(void* parameter) {
        LOG_DEBUG("ADE7953 energy save task started");

        _energySaveTaskShouldRun = true;
        while (_energySaveTaskShouldRun) {
            for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
                if (isChannelActive(i)) _saveEnergyToPreferences(i);
            }

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAVE_ENERGY_INTERVAL)) > 0) {
                _energySaveTaskShouldRun = false;
                break;
            }
        }

        LOG_DEBUG("ADE7953 energy save task stopping");
        _energySaveTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void _startHourlyCsvSaveTask() {
        if (_hourlyCsvSaveTaskHandle != NULL) {
            LOG_DEBUG("ADE7953 hourly CSV save task is already running");
            return;
        }

        LOG_DEBUG("Starting ADE7953 hourly CSV save task with %d bytes stack in internal RAM (uses LittleFS)", ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _hourlyCsvSaveTask,
            ADE7953_HOURLY_CSV_SAVE_TASK_NAME,
            ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE,
            nullptr,
            ADE7953_HOURLY_CSV_SAVE_TASK_PRIORITY,
            &_hourlyCsvSaveTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create ADE7953 hourly CSV save task");
            _hourlyCsvSaveTaskHandle = NULL;
        }
    }

    void _stopHourlyCsvSaveTask() {
        stopTaskGracefully(&_hourlyCsvSaveTaskHandle, "ADE7953 hourly CSV save task");
    }

    void _hourlyCsvSaveTask(void* parameter) {
        LOG_DEBUG("ADE7953 hourly CSV save task started");

        // Wait INDEFINITELY for time sync before attempting migration as saving daily data does not 
        // make sense without correct timestamps
        while (!CustomTime::isTimeSynched()) {
            LOG_DEBUG("Waiting for time sync to continue with CSV migration on startup");
            delay(10000);
        }

        // Check again just to be sure..
        if (!CustomTime::isTimeSynched()) {
            LOG_WARNING("Time not synchronized after timeout, skipping CSV migration on startup");
        } else {
            // Avoid compressing current day CSV (still need to append today's data)
            char dateIso[TIMESTAMP_BUFFER_SIZE];
            CustomTime::getCurrentDateIso(dateIso, sizeof(dateIso));
            char excludeFilepath[NAME_BUFFER_SIZE + sizeof(ENERGY_CSV_PREFIX) + 1]; // Added space for prefix plus "/"
            snprintf(excludeFilepath, sizeof(excludeFilepath), "%s/%s", ENERGY_CSV_PREFIX, dateIso);
            
            LOG_DEBUG("Migrating the CSV files, excluding prefix of %s", excludeFilepath);
            migrateCsvToGzip(ENERGY_CSV_PREFIX, excludeFilepath);
        }

        _hourlyCsvSaveTaskShouldRun = true;
        while (_hourlyCsvSaveTaskShouldRun) {
            // Calculate milliseconds until next hour using CustomTime
            uint64_t msUntilNextHour = CustomTime::getMillisecondsUntilNextHour();
            LOG_DEBUG("Waiting for %llu ms until next hour to save the hourly energy", msUntilNextHour);

            // Wait for the calculated time or stop notification
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((uint32_t)msUntilNextHour)) > 0) { // Needs to be casted to uint32_t otherwise it will crash
                _hourlyCsvSaveTaskShouldRun = false;
                break;
            }
            
            // Only save if we're still running and within tolerance of hourly save time
            if (_hourlyCsvSaveTaskShouldRun)
            {
                if (CustomTime::isTimeSynched())
                {
                    if (CustomTime::isNowCloseToHour())
                    {
                        LOG_DEBUG("Time is close to the hour, saving hourly energy data");
                        _saveHourlyEnergyToCsv();

                        // If this save corresponds to the first hour of the new day (HH == 00),
                        // compress the previous day's CSV (yesterday).
                        if (CustomTime::isNowHourZero()) {
                            LOG_DEBUG("Timestamp is at hour 00: trigger compression of yesterday's CSV");

                            // Create filename for yesterday's CSV file (UTC date)
                            char dateIso[TIMESTAMP_BUFFER_SIZE];
                            CustomTime::getDateIsoOffset(dateIso, sizeof(dateIso), -1);
                            char filepath[NAME_BUFFER_SIZE + sizeof(ENERGY_CSV_PREFIX) + 4]; // Added space for prefix plus "/.csv"
                            snprintf(filepath, sizeof(filepath), "%s/%s.csv", ENERGY_CSV_PREFIX, dateIso);

                            if (compressFile(filepath)) {
                                LOG_DEBUG("Successfully compressed yesterday's CSV: %s", filepath);
                            } else {
                                LOG_ERROR("Failed to compress yesterday's CSV: %s", filepath);
                            }
                        }
                    }
                    else
                    {
                        LOG_DEBUG("Not close to the hour, skipping hourly energy save");
                    }
                }
            }
        }

        LOG_DEBUG("ADE7953 hourly CSV save task stopping");
        _hourlyCsvSaveTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    // Configuration management functions
    // ==================================

    void _setConfigurationFromPreferences() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, true)) { // true = read-only
            LOG_ERROR("Failed to open Preferences for ADE7953 configuration");
            // Default configuration already set in constructor, so no need to do anything
            return;
        }

        // Load configuration values (use defaults if not found)
        _configuration.aVGain = preferences.getLong(CONFIG_AV_GAIN_KEY, DEFAULT_CONFIG_AV_GAIN);
        _configuration.aIGain = preferences.getLong(CONFIG_AI_GAIN_KEY, DEFAULT_CONFIG_AI_GAIN);
        _configuration.bIGain = preferences.getLong(CONFIG_BI_GAIN_KEY, DEFAULT_CONFIG_BI_GAIN);
        _configuration.aIRmsOs = preferences.getLong(CONFIG_AIRMS_OS_KEY, DEFAULT_CONFIG_AIRMS_OS);
        _configuration.bIRmsOs = preferences.getLong(CONFIG_BIRMS_OS_KEY, DEFAULT_CONFIG_BIRMS_OS);
        _configuration.aWGain = preferences.getLong(CONFIG_AW_GAIN_KEY, DEFAULT_CONFIG_AW_GAIN);
        _configuration.bWGain = preferences.getLong(CONFIG_BW_GAIN_KEY, DEFAULT_CONFIG_BW_GAIN);
        _configuration.aWattOs = preferences.getLong(CONFIG_AWATT_OS_KEY, DEFAULT_CONFIG_AWATT_OS);
        _configuration.bWattOs = preferences.getLong(CONFIG_BWATT_OS_KEY, DEFAULT_CONFIG_BWATT_OS);
        _configuration.aVarGain = preferences.getLong(CONFIG_AVAR_GAIN_KEY, DEFAULT_CONFIG_AVAR_GAIN);
        _configuration.bVarGain = preferences.getLong(CONFIG_BVAR_GAIN_KEY, DEFAULT_CONFIG_BVAR_GAIN);
        _configuration.aVarOs = preferences.getLong(CONFIG_AVAR_OS_KEY, DEFAULT_CONFIG_AVAR_OS);
        _configuration.bVarOs = preferences.getLong(CONFIG_BVAR_OS_KEY, DEFAULT_CONFIG_BVAR_OS);
        _configuration.aVaGain = preferences.getLong(CONFIG_AVA_GAIN_KEY, DEFAULT_CONFIG_AVA_GAIN);
        _configuration.bVaGain = preferences.getLong(CONFIG_BVA_GAIN_KEY, DEFAULT_CONFIG_BVA_GAIN);
        _configuration.aVaOs = preferences.getLong(CONFIG_AVA_OS_KEY, DEFAULT_CONFIG_AVA_OS);
        _configuration.bVaOs = preferences.getLong(CONFIG_BVA_OS_KEY, DEFAULT_CONFIG_BVA_OS);
        _configuration.phCalA = preferences.getLong(CONFIG_PHCAL_A_KEY, DEFAULT_CONFIG_PHCAL_A);
        _configuration.phCalB = preferences.getLong(CONFIG_PHCAL_B_KEY, DEFAULT_CONFIG_PHCAL_B);

        preferences.end();

        // Apply the configuration
        _applyConfiguration(_configuration);

        LOG_DEBUG("Successfully set configuration from Preferences");
    }

    void _saveConfigurationToPreferences() {
        LOG_DEBUG("Saving configuration to Preferences...");

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            LOG_ERROR("Failed to open Preferences for saving ADE7953 configuration");
            return;
        }

        preferences.putLong(CONFIG_AV_GAIN_KEY, _configuration.aVGain);
        preferences.putLong(CONFIG_AI_GAIN_KEY, _configuration.aIGain);
        preferences.putLong(CONFIG_BI_GAIN_KEY, _configuration.bIGain);
        preferences.putLong(CONFIG_AIRMS_OS_KEY, _configuration.aIRmsOs);
        preferences.putLong(CONFIG_BIRMS_OS_KEY, _configuration.bIRmsOs);
        preferences.putLong(CONFIG_AW_GAIN_KEY, _configuration.aWGain);
        preferences.putLong(CONFIG_BW_GAIN_KEY, _configuration.bWGain);
        preferences.putLong(CONFIG_AWATT_OS_KEY, _configuration.aWattOs);
        preferences.putLong(CONFIG_BWATT_OS_KEY, _configuration.bWattOs);
        preferences.putLong(CONFIG_AVAR_GAIN_KEY, _configuration.aVarGain);
        preferences.putLong(CONFIG_BVAR_GAIN_KEY, _configuration.bVarGain);
        preferences.putLong(CONFIG_AVAR_OS_KEY, _configuration.aVarOs);
        preferences.putLong(CONFIG_BVAR_OS_KEY, _configuration.bVarOs);
        preferences.putLong(CONFIG_AVA_GAIN_KEY, _configuration.aVaGain);
        preferences.putLong(CONFIG_BVA_GAIN_KEY, _configuration.bVaGain);
        preferences.putLong(CONFIG_AVA_OS_KEY, _configuration.aVaOs);
        preferences.putLong(CONFIG_BVA_OS_KEY, _configuration.bVaOs);
        preferences.putLong(CONFIG_PHCAL_A_KEY, _configuration.phCalA);
        preferences.putLong(CONFIG_PHCAL_B_KEY, _configuration.phCalB);

        preferences.end();

        LOG_DEBUG("Successfully saved configuration to Preferences");
    }

    void _applyConfiguration(const Ade7953Configuration &config) {
        _setGain(config.aVGain, Ade7953Channel::A, MeasurementType::VOLTAGE);
        // Channel B voltage gain should not be set as by datasheet

        _setGain(config.aIGain, Ade7953Channel::A, MeasurementType::CURRENT);
        _setGain(config.bIGain, Ade7953Channel::B, MeasurementType::CURRENT);

        _setOffset(config.aIRmsOs, Ade7953Channel::A, MeasurementType::CURRENT);
        _setOffset(config.bIRmsOs, Ade7953Channel::B, MeasurementType::CURRENT);

        _setGain(config.aWGain, Ade7953Channel::A, MeasurementType::ACTIVE_POWER);
        _setGain(config.bWGain, Ade7953Channel::B, MeasurementType::ACTIVE_POWER);

        _setOffset(config.aWattOs, Ade7953Channel::A, MeasurementType::ACTIVE_POWER);
        _setOffset(config.bWattOs, Ade7953Channel::B, MeasurementType::ACTIVE_POWER);

        _setGain(config.aVarGain, Ade7953Channel::A, MeasurementType::REACTIVE_POWER);
        _setGain(config.bVarGain, Ade7953Channel::B, MeasurementType::REACTIVE_POWER);

        _setOffset(config.aVarOs, Ade7953Channel::A, MeasurementType::REACTIVE_POWER);
        _setOffset(config.bVarOs, Ade7953Channel::B, MeasurementType::ACTIVE_POWER);

        _setGain(config.aVaGain, Ade7953Channel::A, MeasurementType::APPARENT_POWER);
        _setGain(config.bVaGain, Ade7953Channel::B, MeasurementType::APPARENT_POWER);

        _setOffset(config.aVaOs, Ade7953Channel::A, MeasurementType::APPARENT_POWER);
        _setOffset(config.bVaOs, Ade7953Channel::B, MeasurementType::APPARENT_POWER);

        _setPhaseCalibration(config.phCalA, Ade7953Channel::A);
        _setPhaseCalibration(config.phCalB, Ade7953Channel::B);

        LOG_DEBUG("Successfully applied configuration");
    }

    bool _validateJsonConfiguration(const JsonDocument &jsonDocument, bool partial) {
        if (!jsonDocument.is<JsonObjectConst>()) {
            LOG_WARNING("JSON is not an object");
            return false;
        }

        // For partial updates, we don't require all fields to be present
        if (partial) {
            // Partial validation - only validate fields that are present
            if (jsonDocument["aVGain"].is<int32_t>()) return true;
            if (jsonDocument["aIGain"].is<int32_t>()) return true;
            if (jsonDocument["bIGain"].is<int32_t>()) return true;
            if (jsonDocument["aIRmsOs"].is<int32_t>()) return true;
            if (jsonDocument["bIRmsOs"].is<int32_t>()) return true;
            if (jsonDocument["aWGain"].is<int32_t>()) return true;
            if (jsonDocument["bWGain"].is<int32_t>()) return true;
            if (jsonDocument["aWattOs"].is<int32_t>()) return true;
            if (jsonDocument["bWattOs"].is<int32_t>()) return true;
            if (jsonDocument["aVarGain"].is<int32_t>()) return true;
            if (jsonDocument["bVarGain"].is<int32_t>()) return true;
            if (jsonDocument["aVarOs"].is<int32_t>()) return true;
            if (jsonDocument["bVarOs"].is<int32_t>()) return true;
            if (jsonDocument["aVaGain"].is<int32_t>()) return true;
            if (jsonDocument["bVaGain"].is<int32_t>()) return true;
            if (jsonDocument["aVaOs"].is<int32_t>()) return true;
            if (jsonDocument["bVaOs"].is<int32_t>()) return true;
            if (jsonDocument["phCalA"].is<int32_t>()) return true;
            if (jsonDocument["phCalB"].is<int32_t>()) return true;

            return false;
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["aVGain"].is<int32_t>()) { LOG_WARNING("aVGain is missing or not int32_t"); return false; }
            if (!jsonDocument["aIGain"].is<int32_t>()) { LOG_WARNING("aIGain is missing or not int32_t"); return false; }
            if (!jsonDocument["bIGain"].is<int32_t>()) { LOG_WARNING("bIGain is missing or not int32_t"); return false; }
            if (!jsonDocument["aIRmsOs"].is<int32_t>()) { LOG_WARNING("aIRmsOs is missing or not int32_t"); return false; }
            if (!jsonDocument["bIRmsOs"].is<int32_t>()) { LOG_WARNING("bIRmsOs is missing or not int32_t"); return false; }
            if (!jsonDocument["aWGain"].is<int32_t>()) { LOG_WARNING("aWGain is missing or not int32_t"); return false; }
            if (!jsonDocument["bWGain"].is<int32_t>()) { LOG_WARNING("bWGain is missing or not int32_t"); return false; }
            if (!jsonDocument["aWattOs"].is<int32_t>()) { LOG_WARNING("aWattOs is missing or not int32_t"); return false; }
            if (!jsonDocument["bWattOs"].is<int32_t>()) { LOG_WARNING("bWattOs is missing or not int32_t"); return false; }
            if (!jsonDocument["aVarGain"].is<int32_t>()) { LOG_WARNING("aVarGain is missing or not int32_t"); return false; }
            if (!jsonDocument["bVarGain"].is<int32_t>()) { LOG_WARNING("bVarGain is missing or not int32_t"); return false; }
            if (!jsonDocument["aVarOs"].is<int32_t>()) { LOG_WARNING("aVarOs is missing or not int32_t"); return false; }
            if (!jsonDocument["bVarOs"].is<int32_t>()) { LOG_WARNING("bVarOs is missing or not int32_t"); return false; }
            if (!jsonDocument["aVaGain"].is<int32_t>()) { LOG_WARNING("aVaGain is missing or not int32_t"); return false; }
            if (!jsonDocument["bVaGain"].is<int32_t>()) { LOG_WARNING("bVaGain is missing or not int32_t"); return false; }
            if (!jsonDocument["aVaOs"].is<int32_t>()) { LOG_WARNING("aVaOs is missing or not int32_t"); return false; }
            if (!jsonDocument["bVaOs"].is<int32_t>()) { LOG_WARNING("bVaOs is missing or not int32_t"); return false; }
            if (!jsonDocument["phCalA"].is<int32_t>()) { LOG_WARNING("phCalA is missing or not int32_t"); return false; }
            if (!jsonDocument["phCalB"].is<int32_t>()) { LOG_WARNING("phCalB is missing or not int32_t"); return false; }

            return true;
        }
    }

    // Channel data management functions
    // =================================

    void _setChannelDataFromPreferences(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Invalid channel index: %lu", channelIndex);
            return;
        }

        ChannelData channelData;
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) { // true = read-only
            LOG_ERROR("Failed to open Preferences for channel data");
            // Set default channel data
            setChannelData(channelData, channelIndex);
            return;
        }

        char key[PREFERENCES_KEY_BUFFER_SIZE];
        
        // Load channel data (use defaults if not found)
        channelData.index = channelIndex;

        snprintf(key, sizeof(key), CHANNEL_ACTIVE_KEY, channelIndex);
        channelData.active = preferences.getBool(key, channelIndex == 0); // Channel 0 active by default

        snprintf(key, sizeof(key), CHANNEL_REVERSE_KEY, channelIndex);
        channelData.reverse = preferences.getBool(key, DEFAULT_CHANNEL_REVERSE);

        snprintf(key, sizeof(key), CHANNEL_LABEL_KEY, channelIndex);
        char defaultLabel[NAME_BUFFER_SIZE];
        snprintf(defaultLabel, sizeof(defaultLabel), DEFAULT_CHANNEL_LABEL_FORMAT, channelIndex);
        preferences.getString(key, channelData.label, sizeof(channelData.label));
        if (strlen(channelData.label) == 0) {
            snprintf(channelData.label, sizeof(channelData.label), "%s", defaultLabel);
        }

        snprintf(key, sizeof(key), CHANNEL_PHASE_KEY, channelIndex);
        channelData.phase = static_cast<Phase>(preferences.getUChar(key, (uint8_t)(DEFAULT_CHANNEL_PHASE)));

        // CT Specification
        snprintf(key, sizeof(key), CHANNEL_CT_CURRENT_RATING_KEY, channelIndex);
        channelData.ctSpecification.currentRating = preferences.getFloat(key, channelIndex == 0 ? DEFAULT_CT_CURRENT_RATING_CHANNEL_0 : DEFAULT_CT_CURRENT_RATING);

        snprintf(key, sizeof(key), CHANNEL_CT_VOLTAGE_OUTPUT_KEY, channelIndex);
        channelData.ctSpecification.voltageOutput = preferences.getFloat(key, DEFAULT_CT_VOLTAGE_OUTPUT);

        snprintf(key, sizeof(key), CHANNEL_CT_SCALING_FRACTION_KEY, channelIndex);
        channelData.ctSpecification.scalingFraction = preferences.getFloat(key, DEFAULT_CT_SCALING_FRACTION);

        preferences.end();

        setChannelData(channelData, channelIndex);

        LOG_DEBUG("Successfully set channel data from Preferences for channel %lu", channelIndex);
    }

    bool _saveChannelDataToPreferences(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Invalid channel index: %lu", channelIndex);
            return false;
        }

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) { // false = read-write
            LOG_ERROR("Failed to open Preferences for saving channel data");
            return false;
        }

        char key[PREFERENCES_KEY_BUFFER_SIZE];

        ChannelData channelData;
        if (!getChannelData(channelData, channelIndex)) {
            LOG_WARNING("Failed to get channel data for channel %u", channelIndex);
            return false;
        }

        // Save channel data
        snprintf(key, sizeof(key), CHANNEL_ACTIVE_KEY, channelIndex);
        preferences.putBool(key, channelData.active);

        snprintf(key, sizeof(key), CHANNEL_REVERSE_KEY, channelIndex);
        preferences.putBool(key, channelData.reverse);

        snprintf(key, sizeof(key), CHANNEL_LABEL_KEY, channelIndex);
        preferences.putString(key, channelData.label);

        snprintf(key, sizeof(key), CHANNEL_PHASE_KEY, channelIndex);
        preferences.putUChar(key, (uint8_t)(channelData.phase));

        // CT Specification
        snprintf(key, sizeof(key), CHANNEL_CT_CURRENT_RATING_KEY, channelIndex);
        preferences.putFloat(key, channelData.ctSpecification.currentRating);

        snprintf(key, sizeof(key), CHANNEL_CT_VOLTAGE_OUTPUT_KEY, channelIndex);
        preferences.putFloat(key, channelData.ctSpecification.voltageOutput);

        snprintf(key, sizeof(key), CHANNEL_CT_SCALING_FRACTION_KEY, channelIndex);
        preferences.putFloat(key, channelData.ctSpecification.scalingFraction);

        preferences.end();

        LOG_DEBUG("Successfully saved channel data to Preferences for channel %lu", channelIndex);
        return true;
    }

    bool _validateChannelDataJson(const JsonDocument &jsonDocument, bool partial) {
        if (!jsonDocument.is<JsonObjectConst>()) {
            LOG_WARNING("JSON is not an object");
            return false;
        }

        // Index is always required for channel operations
        if (!jsonDocument["index"].is<uint8_t>()) {
            LOG_WARNING("index is missing or not uint8_t");
            return false;
        }

        if (!isChannelValid(jsonDocument["index"].as<uint8_t>())) {
            LOG_WARNING("Invalid channel index: %lu", jsonDocument["index"].as<uint8_t>());
            return false;
        }

        if (partial) {
            if (jsonDocument["active"].is<bool>()) return true;
            if (jsonDocument["reverse"].is<bool>()) return true;
            if (jsonDocument["label"].is<const char*>()) return true;
            if (jsonDocument["phase"].is<uint8_t>()) return true;

            // CT Specification validation for partial updates
            if (jsonDocument["ctSpecification"].is<JsonObjectConst>()) {
                if (jsonDocument["ctSpecification"]["currentRating"].is<float>()) return true;
                if (jsonDocument["ctSpecification"]["voltageOutput"].is<float>()) return true;
                if (jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) return true;   
            }

            LOG_WARNING("No valid fields found for partial update");
            return false; // No valid fields found for partial update
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["active"].is<bool>()) { LOG_WARNING("active is missing or not bool"); return false; }
            if (!jsonDocument["reverse"].is<bool>()) { LOG_WARNING("reverse is missing or not bool"); return false; }
            if (!jsonDocument["label"].is<const char*>()) { LOG_WARNING("label is missing or not string"); return false; }
            if (!jsonDocument["phase"].is<uint8_t>()) { LOG_WARNING("phase is missing or not uint8_t"); return false; }

            // CT Specification validation
            if (!jsonDocument["ctSpecification"].is<JsonObjectConst>()) { LOG_WARNING("ctSpecification is missing or not object"); return false; }
            if (!jsonDocument["ctSpecification"]["currentRating"].is<float>()) { LOG_WARNING("ctSpecification.currentRating is missing or not float"); return false; }
            if (!jsonDocument["ctSpecification"]["voltageOutput"].is<float>()) { LOG_WARNING("ctSpecification.voltageOutput is missing or not float"); return false; }
            if (!jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) { LOG_WARNING("ctSpecification.scalingFraction is missing or not float"); return false; }

            return true; // All fields validated successfully
        }
    }

    void _updateChannelData(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Invalid channel index: %lu", channelIndex);
            return;
        }

        if (!acquireMutex(&_channelDataMutex)) { // We need to explicitly acquire the mutex since the _calculateLsbValues function modifies the pointer passed to it
            LOG_ERROR("Failed to acquire mutex for channel data");
            return;
        }
        _calculateLsbValues(_channelData[channelIndex].ctSpecification);
        releaseMutex(&_channelDataMutex);

        LOG_DEBUG("Successfully updated channel data for channel %lu", channelIndex);
    }

    void _calculateLsbValues(CtSpecification &ctSpec) {        
        // General helping values
        // The datasheet provides the absolute maximum input voltage for the ADE7953, 
        // but for convenience we will do all the calculations based on the RMS values
        float maximumAdcChannelInputRms = MAXIMUM_ADC_CHANNEL_INPUT / sqrt(2.0f);

        // Current constant calculation
        // ----------------------------
        // We know that at full scale inputs (thus feeding MAXIMUM_ADC_CHANNEL_INPUT to the current channel),
        // the ADE7953 will output a full scale value of FULL_SCALE_LSB_FOR_RMS_VALUES.
        // The usable voltage for the current is related to the CT voltage output, which is typically 333mV for a 30A CT.
        // This value can be exceeded with caution (for instance, using 100A 1V output CTs, given a load that never exceeds
        // 30A).
        float usableAdcChannelInputRms = ctSpec.voltageOutput / maximumAdcChannelInputRms;
        // We need the usable voltage to calculate the usable LSB because if the voltage output is lower than the maximum ADC input,
        // we will have less LSB to work with, and thus we need to scale everything.
        float usableLsbRms = FULL_SCALE_LSB_FOR_RMS_VALUES / usableAdcChannelInputRms;
        // Finally, we can calculate the LSB for the current channel by dividing the current rating (in A RMS) by the usable LSB,
        // remembering to include the scaling fraction.
        ctSpec.aLsb = ctSpec.currentRating / usableLsbRms * (1 + ctSpec.scalingFraction);

        // ctSpec.wLsb = 1.0f / 462.59f;
        // ctSpec.varLsb = 1.0f / 462.59f;
        // ctSpec.vaLsb = 1.0f / 462.59f;

        // Energy constant calculation
        // ---------------------------
        // This is more tricky since we need to consider the full scale current and voltage ratings
        // First, we compute the full scale current RMS, which is simply the CT current rating.
        float fullScaleCurrentRms = ctSpec.currentRating;
        // Then we compute the full scale voltage RMS, which is the maximum ADC input RMS scaled by the voltage divider ratio.
        float voltageDivideRatio = 1 / (VOLTAGE_DIVIDER_R2 / (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2));
        float fullScaleVoltageRms = maximumAdcChannelInputRms * voltageDivideRatio;
        // The full scale power is simply the product of the full scale current and voltage RMS values (assuming cos phi = 1 for simplicity).
        float fullScalePower = fullScaleCurrentRms * fullScaleVoltageRms;
        // The amount of time it passes in one tick is needed to compute the (miniscule) energy that is accumulated in one tick.
        float deltaHoursOneTick = (1.0f / ENERGY_ACCUMULATION_FREQUENCY) / 3600.0f;
        // Finally, we can compute the LSB for energy in watt-hours by multiplying the full scale power (in W) by the time delta (in hours)
        float wattHourPerLsb = fullScalePower * deltaHoursOneTick;

        // The scaling is the same for all channels, remembering to add the scaling fraction.
        ctSpec.whLsb = wattHourPerLsb * (1 + ctSpec.scalingFraction);
        ctSpec.varhLsb = wattHourPerLsb * (1 + ctSpec.scalingFraction);
        ctSpec.vahLsb = wattHourPerLsb * (1 + ctSpec.scalingFraction);

        LOG_VERBOSE(
            "LSB values for %.1f A, %.3f V, scaling %.3f | A per LSB: %.10f, Wh per LSB: %.10f",
            ctSpec.currentRating,
            ctSpec.voltageOutput,
            ctSpec.scalingFraction,
            ctSpec.aLsb,
            ctSpec.whLsb
        );
    }

    // Energy
    // ======

    void _setEnergyFromPreferences(uint8_t channelIndex) {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ENERGY, true)) {
            LOG_ERROR("Failed to open preferences for reading");
            return;
        }

        char key[PREFERENCES_KEY_BUFFER_SIZE];
        
        // Only place in which we read the energy from preferences, and set the _energyValues initially

        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for energy values");
            return;
        }

        snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, channelIndex);
        _meterValues[channelIndex].activeEnergyImported = preferences.getFloat(key, 0.0f);
        _energyValues[channelIndex].activeEnergyImported = _meterValues[channelIndex].activeEnergyImported;

        snprintf(key, sizeof(key), ENERGY_ACTIVE_EXP_KEY, channelIndex);
        _meterValues[channelIndex].activeEnergyExported = preferences.getFloat(key, 0.0f);
        _energyValues[channelIndex].activeEnergyExported = _meterValues[channelIndex].activeEnergyExported;

        snprintf(key, sizeof(key), ENERGY_REACTIVE_IMP_KEY, channelIndex);
        _meterValues[channelIndex].reactiveEnergyImported = preferences.getFloat(key, 0.0f);
        _energyValues[channelIndex].reactiveEnergyImported = _meterValues[channelIndex].reactiveEnergyImported;

        snprintf(key, sizeof(key), ENERGY_REACTIVE_EXP_KEY, channelIndex);
        _meterValues[channelIndex].reactiveEnergyExported = preferences.getFloat(key, 0.0f);
        _energyValues[channelIndex].reactiveEnergyExported = _meterValues[channelIndex].reactiveEnergyExported;

        snprintf(key, sizeof(key), ENERGY_APPARENT_KEY, channelIndex);
        _meterValues[channelIndex].apparentEnergy = preferences.getFloat(key, 0.0f);
        _energyValues[channelIndex].apparentEnergy = _meterValues[channelIndex].apparentEnergy;

        releaseMutex(&_meterValuesMutex);

        preferences.end();

        _saveEnergyToPreferences(channelIndex, true); // Ensure we have the initial values saved always

        LOG_DEBUG("Successfully read energy from preferences for channel %lu", channelIndex);
    }

    void _saveEnergyToPreferences(uint8_t channelIndex, bool forceSave) {
        Preferences preferences;
        preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false);

        char key[PREFERENCES_KEY_BUFFER_SIZE];

        MeterValues meterValues;
        if (!getMeterValues(meterValues, channelIndex)) {
            LOG_WARNING("Failed to get meter values for channel %d. Skipping energy save", channelIndex);
            return;
        }

        // Hereafter we optimize the flash writes by only saving if the value has changed significantly
        // Meter values are the real-time values, while energy values are the last saved values
        if ((meterValues.activeEnergyImported - _energyValues[channelIndex].activeEnergyImported > ENERGY_SAVE_THRESHOLD) || forceSave) {
            snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, channelIndex);
            preferences.putFloat(key, meterValues.activeEnergyImported);
            _energyValues[channelIndex].activeEnergyImported = meterValues.activeEnergyImported;
        }

        if ((meterValues.activeEnergyExported - _energyValues[channelIndex].activeEnergyExported > ENERGY_SAVE_THRESHOLD) || forceSave) {
            snprintf(key, sizeof(key), ENERGY_ACTIVE_EXP_KEY, channelIndex);
            preferences.putFloat(key, meterValues.activeEnergyExported);
            _energyValues[channelIndex].activeEnergyExported = meterValues.activeEnergyExported;
        }

        if ((meterValues.reactiveEnergyImported - _energyValues[channelIndex].reactiveEnergyImported > ENERGY_SAVE_THRESHOLD) || forceSave) {
            snprintf(key, sizeof(key), ENERGY_REACTIVE_IMP_KEY, channelIndex);
            preferences.putFloat(key, meterValues.reactiveEnergyImported);
            _energyValues[channelIndex].reactiveEnergyImported = meterValues.reactiveEnergyImported;
        }

        if ((meterValues.reactiveEnergyExported - _energyValues[channelIndex].reactiveEnergyExported > ENERGY_SAVE_THRESHOLD) || forceSave) {
            snprintf(key, sizeof(key), ENERGY_REACTIVE_EXP_KEY, channelIndex);
            preferences.putFloat(key, meterValues.reactiveEnergyExported);
            _energyValues[channelIndex].reactiveEnergyExported = meterValues.reactiveEnergyExported;
        }

        if ((meterValues.apparentEnergy - _energyValues[channelIndex].apparentEnergy > ENERGY_SAVE_THRESHOLD) || forceSave) {
            snprintf(key, sizeof(key), ENERGY_APPARENT_KEY, channelIndex);
            preferences.putFloat(key, meterValues.apparentEnergy);
            _energyValues[channelIndex].apparentEnergy = meterValues.apparentEnergy;
        }

        preferences.end();
        LOG_DEBUG("Successfully saved energy to preferences for channel %lu", channelIndex);
    }

    void _saveHourlyEnergyToCsv() {
        LOG_DEBUG("Saving hourly energy to CSV...");

        // Ensure time is synchronized before saving
        if (!CustomTime::isTimeSynched()) {
            LOG_INFO("Time not synchronized, skipping energy CSV save");
            return;
        }
        
        // Create UTC timestamp in ISO format (rounded to the hour)
        char timestampRoundedHour[TIMESTAMP_ISO_BUFFER_SIZE];
        CustomTime::getTimestampIsoRoundedToHour(timestampRoundedHour, sizeof(timestampRoundedHour));
        
        // Create filename for today's CSV file (UTC date)
        char filename[NAME_BUFFER_SIZE];
        CustomTime::getCurrentDateIso(filename, sizeof(filename));

        char filepath[NAME_BUFFER_SIZE + sizeof(ENERGY_CSV_PREFIX) + 4]; // Added space for prefix plus "/.csv"
        snprintf(filepath, sizeof(filepath), "%s/%s.csv", ENERGY_CSV_PREFIX, filename);
        
        // Ensure the energy directory exists (LittleFS requires explicit directory creation)
        if (!LittleFS.exists(ENERGY_CSV_PREFIX)) {
            if (!LittleFS.mkdir(ENERGY_CSV_PREFIX)) {
                LOG_ERROR("Failed to create energy directory %s", ENERGY_CSV_PREFIX);
                return;
            }
            LOG_DEBUG("Created energy directory %s", ENERGY_CSV_PREFIX);
        }
        
        // Check if file exists to determine if we need to write header
        bool fileExists = LittleFS.exists(filepath);

        // Open file in appropriate mode
        File file = LittleFS.open(filepath, FILE_APPEND);
        if (!file) {
            LOG_ERROR("Failed to open CSV file %s for writing", filepath);
            return;
        }        
        
        // Write header if this is a new file
        if (!fileExists) {
            file.println(DAILY_ENERGY_CSV_HEADER);
            LOG_DEBUG("Created new CSV file %s with header", filename);
        }
        
        // Write data for each active channel
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                LOG_VERBOSE("Saving hourly energy data for channel %d: %s", i, _channelData[i].label);

                MeterValues meterValues;
                if (!getMeterValues(meterValues, i)) {
                    LOG_WARNING("Failed to get meter values for channel %d. Skipping hourly energy save", i);
                    continue;
                }

                // Only save data if (absolute) energy values are above threshold
                if (
                    meterValues.activeEnergyImported > ENERGY_SAVE_THRESHOLD ||
                    meterValues.activeEnergyExported > ENERGY_SAVE_THRESHOLD
                ) {
                    file.print(timestampRoundedHour);
                    file.print(",");
                    file.print(i);
                    file.print(",");
                    file.print(meterValues.activeEnergyImported, DAILY_ENERGY_CSV_DIGITS);
                    file.print(",");
                    file.print(meterValues.activeEnergyExported, DAILY_ENERGY_CSV_DIGITS);
                    file.println();
                } else {
                    LOG_DEBUG("Skipping saving hourly energy data for channel %d: %s (values below threshold)", i, _channelData[i].label);
                }
            }
        }
        
        file.close();
        LOG_DEBUG("Successfully saved hourly energy data to %s", filename);
    }

    void _saveEnergyComplete() {
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) _saveEnergyToPreferences(i, true); // Force save to ensure all values are saved
        }
        if (CustomTime::isNowCloseToHour()) _saveHourlyEnergyToCsv(); // If we are not close to the hour, we avoid saving since we will save at the hour anyway on the next reboot

        LOG_DEBUG("Successfully saved complete energy data");
    }


    // Meter reading and processing
    // ============================

    /*
    There is no better way to read the values from the ADE7953 
    than this. Since we use a multiplexer, we cannot read the data
    more often than 200 ms as that is the settling time for the
    RMS current. 

    Moreover, as we switch the multiplexer just after the line cycle has
    ended, we need to make 1 line cycle go empty before we can actually
    read the data. This is because we want the power factor reading
    to be as accurate as possible.

    In the end we can have a channel 0 reading every 200 ms, while the
    other will need 400 ms per channel.

    The read values from which everything is computed afterwards are:
    - Voltage RMS
    - Current RMS (needs 200 ms to settle, and is computed by the ADE7953 at every zero crossing)
    - Sign of the active power (as we use RMS values, the signedData will indicate the direction of the power)
    - Power factor (computed by the ADE7953 by averaging on the whole line cycle, thus why we need to read only every other line cycle)
    - Active energy, reactive energy, apparent energy (only to make use of the no-load feature)

    For the three phase, we assume that the phase shift is 120 degrees. 

    It the energies are 0, all the previously computed values are set to 0 as the no-load feature is enabled.

    All the values are validated to be within the limits of the hardware/system used.

    There could be a way to improve the measurements by directly using the energy registers and computing the average
    power during the last line cycle. This proved to be a bit unstable and complex to calibrate with respect to the 
    direct voltage, current and power factor readings. The time limitation of 200 ms would still be present. The only
    real advantage would be an accurate value of reactive power, which now is only an approximation.

    */

    /*
    Read all the meter values from the ADE7953.
    A detailed explanation of the inner workings and assumptions
    can be found in the function itself.

    @param channel The channel to read the values from. Returns
    false if the data reading is not ready yet or valid.
    */
    bool _readMeterValues(uint8_t channelIndex, uint64_t linecycUnixTimeMillis) { // TODO: add waveform data
        uint64_t millisRead = millis64();
        uint64_t deltaMillis = millisRead - _meterValues[channelIndex].lastMillis;

        ChannelData channelData;
        if (!getChannelData(channelData, channelIndex)) {
            LOG_WARNING("Failed to get channel data for channel %u", channelIndex);
            _recordFailure();
            return false;
        }

        // We cannot put an higher limit here because if the channel happened to be disabled, then
        // enabled again, this would result in an infinite error.
        if (_meterValues[channelIndex].lastMillis != 0 && deltaMillis == 0) {
            LOG_WARNING(
                "%s (%lu): delta millis (%llu) is invalid. Discarding reading", 
                channelData.label, channelIndex, deltaMillis
            );
            _recordFailure();
            return false;
        }

        Ade7953Channel ade7953Channel = (channelIndex == 0) ? Ade7953Channel::A : Ade7953Channel::B;

        float voltage = 0.0f;
        float current = 0.0f;
        float activePower = 0.0f;
        float reactivePower = 0.0f;
        float apparentPower = 0.0f;
        float powerFactor = 0.0f;
        float activeEnergy = 0.0f;
        float reactiveEnergy = 0.0f;
        float apparentEnergy = 0.0f;

        Phase basePhase = _channelData[0].phase; // Not using channelData since it is only accessed here and it is an integer so very low probability of race condition

        if (channelData.phase == basePhase) { // The phase is not necessarily PHASE_A, so use as reference the one of channel A
            
            // These are the three most important (and only) values to read. All of the rest will be computed from these.
            // These are the most reliable since they are computed on the whole line cycle, thus they incorporate any harmonic.
            // Using directly power or RMS values would require instead constant sampling and averaging. Let's avoid that and leave
            // the ADE7953 do the hard work for us.
            // Use multiplication instead of division as it is faster in embedded systems
                    
            // Handle interrupt flag depending on channel
            if (ade7953Channel == Ade7953Channel::A) {
                if (_interruptHandledChannelA) {
                    LOG_DEBUG("Tried to handle CYCEND interrupt for channel A, but it was already handled");
                    _recordFailure();
                    return false; // Already handled, cannot read again
                }
            } else {
                if (_interruptHandledChannelB) {
                    LOG_DEBUG("Tried to handle CYCEND interrupt for channel B, but it was already handled");
                    _recordFailure();
                    return false; // Already handled, cannot read again
                }
            }

            activeEnergy = float(_readActiveEnergy(ade7953Channel)) * channelData.ctSpecification.whLsb * (channelData.reverse ? -1 : 1);
            reactiveEnergy = float(_readReactiveEnergy(ade7953Channel)) * channelData.ctSpecification.varhLsb * (channelData.reverse ? -1 : 1);
            apparentEnergy = float(_readApparentEnergy(ade7953Channel)) * channelData.ctSpecification.vahLsb;

            // Set the handling just after reading the energy values, to ensure 100% consistency
            if (ade7953Channel == Ade7953Channel::A) _interruptHandledChannelA = true;
            else _interruptHandledChannelB = true;

            // Since the voltage measurement is only one in any case, it makes sense to just re-use the same value
            // as channel 0 (sampled just before) instead of reading it again. It will be at worst _sampleTime old.
            if (channelIndex == 0) {
                voltage = float(_readVoltageRms()) * VOLT_PER_LSB;

                // Update grid frequency during channel 0 reading
                int32_t period = _readPeriod();
                float newGridFrequency = period > 0 ? GRID_FREQUENCY_CONVERSION_FACTOR / float(period) : 0.0f;
                if (_validateGridFrequency(newGridFrequency)) _gridFrequency = newGridFrequency;
            } else {
                voltage = _meterValues[0].voltage;
            }
            
            // We use sample time instead of _deltaMillis because the energy readings are over whole line cycles (defined by the sample time)
            // Thus, extracting the power from energy divided by linecycle is more stable (does not care about ESP32 slowing down) and accurate
            // Use multiplication instead of division as it is faster in embedded systems
            float deltaHoursSampleTime = float(_sampleTime) / 1000.0f / 3600.0f; // Convert milliseconds to hours | ENSURE THEY ARE FLOAT: YOU LOST A LOT OF TIME DEBUGGING THIS!!!
            activePower = deltaHoursSampleTime > 0.0f ? activeEnergy / deltaHoursSampleTime : 0.0f; // W
            reactivePower = deltaHoursSampleTime > 0.0f ? reactiveEnergy / deltaHoursSampleTime : 0.0f; // VAR
            apparentPower = deltaHoursSampleTime > 0.0f ? apparentEnergy / deltaHoursSampleTime : 0.0f; // VA

            // It is faster and more consistent to compute the values rather than reading them from the ADE7953
            if (apparentPower == 0.0f) powerFactor = 0.0f; // Avoid division by zero
            else powerFactor = activePower / apparentPower * (reactivePower >= 0.0f ? 1.0f : -1.0f); // Apply sign as by datasheet (page 38)

            current = voltage > 0.0f ? apparentPower / voltage : 0.0f; // VA = V * A => A = VA / V | Always positive as apparent power is always positive
        } else {
            // TODO: understand if this can be improved using the energy registers
            // Assume everything is the same as channel 0 except the current
            // Important: here the reverse channel is not taken into account as the calculations would (probably) be wrong
            // It is easier just to ensure during installation that the CTs are installed correctly
            
            // Assume from channel 0
            voltage = _meterValues[0].voltage; // Assume the voltage is the same for all channels (medium assumption as difference usually is in the order of few volts, so less than 1%)
            
            // Read wrong power factor due to the phase shift
            float _powerFactorPhaseOne = float(_readPowerFactor(ade7953Channel)) * POWER_FACTOR_CONVERSION_FACTOR;

            // Compute the correct power factor assuming 120 degrees phase shift in voltage (weak assumption as this is normally true)
            // The idea is to:
            // 1. Compute the angle between the voltage and the current with the arc cosine of the just read power factor
            // 2. Add or subtract 120 degrees to the angle depending on the phase (phase is lagging 120 degrees, phase 3 is leading 120 degrees)
            // 3. Compute the cosine of the new corrected angle to get the corrected power factor
            // 4. Multiply by -1 if the channel is reversed (as normal)

            // Note that the direction of the current (and consequently the power) cannot be determined (or at least, I couldn't manage to do it reliably). 
            // This is because the only reliable reading is the power factor, while the angle only gives the angle difference of the current 
            // reading instead of the one of the whole line cycle. As such, the power factor is the only reliable reading and it cannot 
            // provide information about the direction of the power.

            if (channelData.phase == _getLaggingPhase(basePhase)) {
                powerFactor = cos(acos(_powerFactorPhaseOne) - (2.0f * (float)PI / 3.0f));
            } else if (channelData.phase == _getLeadingPhase(basePhase)) {
                // I cannot prove why, but I am SURE the minus is needed if the phase is leading
                powerFactor = - cos(acos(_powerFactorPhaseOne) + (2.0f * (float)PI / 3.0f));
            } else {
                LOG_ERROR("Invalid phase %d for channel %d", channelData.phase, channelIndex);
                _recordFailure();
                return false;
            }

            // Read the current (RMS is absolute so no reverse is needed)
            current = float(_readCurrentRms(ade7953Channel)) * channelData.ctSpecification.aLsb;

            // Compute power values
            activePower = current * voltage * abs(powerFactor);
            apparentPower = current * voltage;
            reactivePower = float(sqrt(pow(apparentPower, 2) - pow(activePower, 2))); // Small approximation leaving out distorted power
        }

        apparentPower = abs(apparentPower); // Apparent power must be positive

        // If the power factor is below a certain threshold, assume everything is 0 to avoid weird readings sinc
        // in any case reading reliable values at such low power factor is not possible with CTs.
        if (abs(powerFactor) < MINIMUM_POWER_FACTOR) {
            LOG_VERBOSE(
                "%s (%d): Power factor %.3f is below %.3f, setting all values to 0",
                channelData.label, 
                channelIndex, 
                powerFactor,
                MINIMUM_POWER_FACTOR
            );
            current = 0.0f;
            activePower = 0.0f;
            reactivePower = 0.0f;
            apparentPower = 0.0f;
            powerFactor = 0.0f;
            activeEnergy = 0.0f;
            reactiveEnergy = 0.0f;
            apparentEnergy = 0.0f;
        }

        // Sometimes the power factor is very close to 1 but above 1. If so, clamp it to 1 as the measure is still valid
        if (abs(powerFactor) > VALIDATE_POWER_FACTOR_MAX && abs(powerFactor) < MAXIMUM_POWER_FACTOR_CLAMP) {
            LOG_VERBOSE(
                "%s (%d): Power factor %.3f is above %.3f, clamping it", 
                channelData.label, 
                channelIndex, 
                powerFactor,
                MAXIMUM_POWER_FACTOR_CLAMP
            );
            powerFactor = (powerFactor > 0) ? VALIDATE_POWER_FACTOR_MAX : VALIDATE_POWER_FACTOR_MIN; // Keep the sign of the power factor
            activePower = apparentPower; // Recompute active power based on the clamped power factor
            reactivePower = 0.0f; // Small approximation leaving out distorted power
        }    
        
        if (
            !_validateVoltage(voltage) || 
            !_validateCurrent(current) || 
            !_validatePower(activePower) || 
            !_validatePower(reactivePower) || 
            !_validatePower(apparentPower) || 
            !_validatePowerFactor(powerFactor)
        ) {
            
            LOG_WARNING("%s (%d): Invalid reading (%.1fW, %.3fA, %.1fVAr, %.1fVA, %.3f)", channelData.label, channelIndex, activePower, current, reactivePower, apparentPower, powerFactor);
            _recordFailure();
            return false;
        }
        
        // Enough checks, now we can set the values
        if (!acquireMutex(&_meterValuesMutex)) {
            LOG_ERROR("Failed to acquire mutex for meter values");
            _recordFailure();
            return false;
        }

        _meterValues[channelIndex].voltage = voltage;
        _meterValues[channelIndex].current = current;
        _meterValues[channelIndex].activePower = activePower;
        _meterValues[channelIndex].reactivePower = reactivePower;
        _meterValues[channelIndex].apparentPower = apparentPower;
        _meterValues[channelIndex].powerFactor = powerFactor;

        // If the phase is not the phase of the main channel, set the energy not to 0 if the current
        // is above the threshold since we cannot use the ADE7593 no-load feature in this approximation
        if (channelData.phase != basePhase && current > MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD) {
            activeEnergy = 1;
            reactiveEnergy = 1;
            apparentEnergy = 1;
        }

        
        // Leverage the no-load feature of the ADE7953 to discard the noise
        // As such, when the energy read by the ADE7953 in the given linecycle is below
        // a certain threshold (set during setup), the read value is 0
        // In this way, we can avoid worrying about setting up thresholds (which are always specific to one case)
        // and instead use this feature to really discard zero-power readings.
        float deltaHoursFromLastEnergyIncrement = float(deltaMillis) / 1000.0f / 3600.0f; // Convert milliseconds to hours
        if (activeEnergy > 0) { // Increment imported
            _meterValues[channelIndex].activeEnergyImported += abs(_meterValues[channelIndex].activePower * deltaHoursFromLastEnergyIncrement); // W * h = Wh
        } else if (activeEnergy < 0) { // Increment exported
            _meterValues[channelIndex].activeEnergyExported += abs(_meterValues[channelIndex].activePower * deltaHoursFromLastEnergyIncrement); // W * h = Wh
        } else { // No load active energy detected
            LOG_VERBOSE(
                "%s (%d): No load active energy reading. Setting active power and power factor to 0",
                channelData.label,
                channelIndex
            );
            _meterValues[channelIndex].activePower = 0.0f;
            _meterValues[channelIndex].powerFactor = 0.0f;
        }

        if (reactiveEnergy > 0) { // Increment imported reactive energy
            _meterValues[channelIndex].reactiveEnergyImported += abs(_meterValues[channelIndex].reactivePower * deltaHoursFromLastEnergyIncrement); // var * h = VArh
        } else if (reactiveEnergy < 0) { // Increment exported reactive energy
            _meterValues[channelIndex].reactiveEnergyExported += abs(_meterValues[channelIndex].reactivePower * deltaHoursFromLastEnergyIncrement); // var * h = VArh
        } else { // No load reactive energy detected
            LOG_VERBOSE(
                "%s (%d): No load reactive energy reading. Setting reactive power to 0",
                channelData.label,
                channelIndex
            );
            _meterValues[channelIndex].reactivePower = 0.0f;
        }

        if (apparentEnergy != 0) {
            _meterValues[channelIndex].apparentEnergy += _meterValues[channelIndex].apparentPower * deltaHoursFromLastEnergyIncrement; // VA * h = VAh
        } else {
            LOG_VERBOSE(
                "%s (%d): No load apparent energy reading. Setting apparent power and current to 0",
                channelData.label,
                channelIndex
            );
            _meterValues[channelIndex].current = 0.0f;
            _meterValues[channelIndex].apparentPower = 0.0f;
        }

        // We actually set the timestamp of the channel (used for the energy calculations)
        // only if we actually reached the end. Otherwise it would mean the point had to be
        // discarded
        statistics.ade7953ReadingCount++;
        _meterValues[channelIndex].lastMillis = millisRead;
        _meterValues[channelIndex].lastUnixTimeMilliseconds = linecycUnixTimeMillis;
        releaseMutex(&_meterValuesMutex);
        return true;
    }

    static void _purgeEnergyRegisters(Ade7953Channel ade7953Channel) {
        // Purge the energy registers to ensure the next linecyc reading is clean
        // To do so, we just need to read the energy registers (which are read with reset)
        LOG_VERBOSE("Purging energy registers for channel %s", ADE7953_CHANNEL_TO_STRING(ade7953Channel));
        if (ade7953Channel == Ade7953Channel::A) {
            _readActiveEnergy(Ade7953Channel::A);
            _readReactiveEnergy(Ade7953Channel::A);
            _readApparentEnergy(Ade7953Channel::A);
        } else {
            _readActiveEnergy(Ade7953Channel::B);
            _readReactiveEnergy(Ade7953Channel::B);
            _readApparentEnergy(Ade7953Channel::B);
        }
    }

    bool _processChannelReading(uint8_t channelIndex, uint64_t linecycUnix) {
        if (!_readMeterValues(channelIndex, linecycUnix)) return false;
        _addMeterDataToPayload(channelIndex);
        _printMeterValues(channelIndex);

        return true;
    }

    void _addMeterDataToPayload(uint8_t channelIndex) {
        #ifdef HAS_SECRETS

        LOG_VERBOSE("Adding meter data to payload for channel %u", channelIndex);

        MeterValues meterValues;
        if (!getMeterValues(meterValues, channelIndex)) {
            LOG_WARNING("Failed to get meter values for channel %d. Skipping payload addition", channelIndex);
            return;
        }

        if (!CustomTime::isUnixTimeValid(meterValues.lastUnixTimeMilliseconds)) {
            LOG_VERBOSE("Channel %d has invalid Unix time. Skipping payload addition", channelIndex);
            return;
        }

        Mqtt::pushMeter(
            PayloadMeter(
                channelIndex,
                meterValues.lastUnixTimeMilliseconds,
                meterValues.activePower,
                meterValues.powerFactor
            )
        );
        #endif
    }

    // ADE7953 register writing functions
    // ==================================

    void _setLinecyc(uint32_t linecyc) {
        int32_t constrainedLinecyc = min(max(linecyc, ADE7953_MIN_LINECYC), ADE7953_MAX_LINECYC);
        writeRegister(LINECYC_16, BIT_16, constrainedLinecyc);
        LOG_DEBUG("Linecyc set to %d", constrainedLinecyc);
    }

    void _setPgaGain(int32_t pgaGain, Ade7953Channel ade7953Channel, MeasurementType measurementType) {
        if (ade7953Channel == Ade7953Channel::A) {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, BIT_8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IA_8, BIT_8, pgaGain);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel A", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        } else {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, BIT_8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IB_8, BIT_8, pgaGain);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel B", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        }

        LOG_DEBUG("Set PGA gain to %ld on channel %s for measurement type %s", pgaGain, ADE7953_CHANNEL_TO_STRING(ade7953Channel), MEASUREMENT_TYPE_TO_STRING(measurementType));
    }

    void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) writeRegister(PHCALA_16, BIT_16, phaseCalibration);
        else writeRegister(PHCALB_16, BIT_16, phaseCalibration);
        LOG_DEBUG("Phase calibration set to %ld on channel %s", phaseCalibration, ADE7953_CHANNEL_TO_STRING(channel));
    }

    void _setGain(int32_t gain, Ade7953Channel channel, MeasurementType measurementType) {
        if (channel == Ade7953Channel::A) {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(AVGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(AIGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::ACTIVE_POWER:
                    writeRegister(AWGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::REACTIVE_POWER:
                    writeRegister(AVARGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::APPARENT_POWER:
                    writeRegister(AVAGAIN_32, BIT_32, gain);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel A", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        } else {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(AVGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(BIGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::ACTIVE_POWER:
                    writeRegister(BWGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::REACTIVE_POWER:
                    writeRegister(BVARGAIN_32, BIT_32, gain);
                    break;
                case MeasurementType::APPARENT_POWER:
                    writeRegister(BVAGAIN_32, BIT_32, gain);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel B", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        }

        LOG_DEBUG("Set gain to %ld on channel %s for measurement type %s", gain, ADE7953_CHANNEL_TO_STRING(channel), MEASUREMENT_TYPE_TO_STRING(measurementType));
    }

    void _setOffset(int32_t offset, Ade7953Channel ade7953Channel, MeasurementType measurementType) {
        if (ade7953Channel == Ade7953Channel::A) {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(VRMSOS_32, BIT_32, offset);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(AIRMSOS_32, BIT_32, offset);
                    break;
                case MeasurementType::ACTIVE_POWER:
                    writeRegister(AWATTOS_32, BIT_32, offset);
                    break;
                case MeasurementType::REACTIVE_POWER:
                    writeRegister(AVAROS_32, BIT_32, offset);
                    break;
                case MeasurementType::APPARENT_POWER:
                    writeRegister(AVAOS_32, BIT_32, offset);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel A", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        } else {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(VRMSOS_32, BIT_32, offset);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(BIRMSOS_32, BIT_32, offset);
                    break;
                case MeasurementType::ACTIVE_POWER:
                    writeRegister(BWATTOS_32, BIT_32, offset);
                    break;
                case MeasurementType::REACTIVE_POWER:
                    writeRegister(BVAROS_32, BIT_32, offset);
                    break;
                case MeasurementType::APPARENT_POWER:
                    writeRegister(BVAOS_32, BIT_32, offset);
                    break;
                default:
                    LOG_ERROR("Invalid measurement type %s for channel B", MEASUREMENT_TYPE_TO_STRING(measurementType));
                    return;
            }
        }

        LOG_DEBUG("Set offset to %ld on channel %s for measurement type %s", offset, ADE7953_CHANNEL_TO_STRING(ade7953Channel), MEASUREMENT_TYPE_TO_STRING(measurementType));
    }

    // Sample time management
    // ======================

    void _setSampleTimeFromPreferences() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            LOG_ERROR("Failed to open Preferences for loading sample time");
            return;
        }
        uint64_t sampleTime = preferences.getULong64(CONFIG_SAMPLE_TIME_KEY, DEFAULT_SAMPLE_TIME);
        preferences.end();
        
        setSampleTime(sampleTime);

        LOG_DEBUG("Loaded sample time %llu ms from preferences", sampleTime);
    }

    void _saveSampleTimeToPreferences() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            LOG_ERROR("Failed to open Preferences for saving sample time");
            return;
        }
        preferences.putULong64(CONFIG_SAMPLE_TIME_KEY, _sampleTime);
        preferences.end();

        LOG_DEBUG("Saved sample time %llu ms to preferences", _sampleTime);
    }

    void _updateSampleTime() {
        int32_t period = _readPeriod();
        float gridFrequency = period > 0 ? GRID_FREQUENCY_CONVERSION_FACTOR / float(period) : 0.0f;
        uint64_t gridFrequencyInt = DEFAULT_FALLBACK_FREQUENCY;

        if (
            _validateGridFrequency(gridFrequency) &&
            fabs(gridFrequency - 60.0f) < 2.0f
        ) gridFrequencyInt = 60; // If valid frequency and close to 60 Hz, set to 60 Hz
        else gridFrequencyInt = DEFAULT_FALLBACK_FREQUENCY; // Otherwise, set to 50 Hz as most of the world uses this

        uint64_t calculatedLinecyc = _sampleTime * gridFrequencyInt * 2 / 1000;
        _setLinecyc((uint32_t)calculatedLinecyc);

        LOG_DEBUG("Successfully updated sample time to %llu ms (%llu line cycles) with grid frequency %llu Hz", _sampleTime, calculatedLinecyc, gridFrequencyInt);
    }

    // ADE7953 register reading functions
    // ==================================

    int32_t _readCurrentInstantaneous(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(IA_32, BIT_32, true, false); // Since these instantaneous values need to be quick, we don't verify the reading
        else return readRegister(IB_32, BIT_32, true, false); // Since these instantaneous values need to be quick, we don't verify the reading
    }

    int32_t _readVoltageInstantaneous() {
        return readRegister(V_32, BIT_32, true, false); // Since these instantaneous values need to be quick, we don't verify the reading
    }

    int32_t _readCurrentRms(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(IRMSA_32, BIT_32, false);
        else return readRegister(IRMSB_32, BIT_32, false);
    }

    int32_t _readVoltageRms() {
        return readRegister(VRMS_32, BIT_32, false);
    }

    int32_t _readActiveEnergy(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(AENERGYA_32, BIT_32, true);
        else return readRegister(AENERGYB_32, BIT_32, true);
    }

    int32_t _readReactiveEnergy(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(RENERGYA_32, BIT_32, true);
        else return readRegister(RENERGYB_32, BIT_32, true);
    }

    int32_t _readApparentEnergy(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(APENERGYA_32, BIT_32, true);
        else return readRegister(APENERGYB_32, BIT_32, true);
    }

    int32_t _readPowerFactor(Ade7953Channel ade7953Channel) {
        if (ade7953Channel == Ade7953Channel::A) return readRegister(PFA_16, BIT_16, true);
        else return readRegister(PFB_16, BIT_16, true);
    }

    int32_t _readPeriod() {
        return readRegister(PERIOD_16, BIT_16, false);
    }

    // Verification and validation functions
    // =====================================

    void _recordFailure() { // Record failure per channel and disable channel
        
        LOG_DEBUG("Recording failure for ADE7953 communication");

        if (_failureCount == 0) _firstFailureTime = millis64();

        _failureCount++;
        statistics.ade7953ReadingCountFailure++;
        _checkForTooManyFailures();
    }

    void _checkForTooManyFailures() {
        
        if (millis64() - _firstFailureTime > ADE7953_FAILURE_RESET_TIMEOUT_MS && _failureCount > 0) {
            LOG_DEBUG("Failure timeout exceeded (%lu ms). Resetting failure count (reached %d)", millis64() - _firstFailureTime, _failureCount);
            
            _failureCount = 0;
            _firstFailureTime = 0;
            
            return;
        }

        if (_failureCount >= ADE7953_MAX_FAILURES_BEFORE_RESTART) {
            
            LOG_FATAL("Too many failures (%d) in ADE7953 communication or readings. Resetting device", _failureCount);
            setRestartSystem("Too many failures in ADE7953 communication or readings");

            // Reset the failure count and first failure time to avoid infinite loop of setting the restart
            _failureCount = 0;
            _firstFailureTime = 0;
        }
    }

    void _recordCriticalFailure() {
        LOG_DEBUG("Recording critical failure for ADE7953 missed interrupt. Current RMS values: %ld voltage, %ld current (ch A) - %ld current (ch B)", 
            _readVoltageRms(), 
            _readCurrentRms(Ade7953Channel::A),
            _readCurrentRms(Ade7953Channel::B)
        );

        if (_criticalFailureCount == 0) _firstCriticalFailureTime = millis64();

        _criticalFailureCount++;
        _checkForTooManyCriticalFailures();
    }

    void _checkForTooManyCriticalFailures() {
        if (millis64() - _firstCriticalFailureTime > ADE7953_CRITICAL_FAILURE_RESET_TIMEOUT_MS && _criticalFailureCount > 0) {
            LOG_DEBUG("Critical failure timeout exceeded (%llu ms). Resetting critical failure count (reached %lu)", 
                millis64() - _firstCriticalFailureTime, _criticalFailureCount);
            
            _criticalFailureCount = 0;
            _firstCriticalFailureTime = 0;
            
            return;
        }

        // Progressive warning system
        const uint32_t warningThreshold = ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT / 2;
        const uint32_t almostThreshold = ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT - 5;
        if (_criticalFailureCount == warningThreshold) { // Not >= since we want to log this only once
            LOG_WARNING("Critical failures reaching concerning level (%lu/%lu) - missed ADE7953 interrupts", 
                _criticalFailureCount, ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT);
        } else if (_criticalFailureCount == almostThreshold) { // Not >= since we want to log this only once
            LOG_WARNING("Critical failures approaching reboot threshold (%lu/%lu) - system stability at risk", 
                _criticalFailureCount, ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT);
        }

        if (_criticalFailureCount >= ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT) {
            LOG_FATAL("Too many critical failures (%lu) - missed ADE7953 interrupts. Rebooting device for recovery", _criticalFailureCount);
            setRestartSystem("Too many missed ADE7953 interrupts - system recovery required");

            // Reset the critical failure count and first failure time to avoid infinite loop of setting the restart
            _criticalFailureCount = 0;
            _firstCriticalFailureTime = 0;
        }
    }

    bool _verifyLastSpiCommunication(uint16_t expectedAddress, uint8_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite) 
    {
        // To verify
        int32_t lastAddress = readRegister(LAST_ADD_16, BIT_16, false, false);
        if (lastAddress != expectedAddress) {
            LOG_WARNING(
                "Last address %ld (0x%04lX) (write: %d) does not match expected %ld (0x%04lX). Expected data %ld (0x%04lX)", 
                lastAddress, lastAddress, 
                wasWrite, 
                expectedAddress, expectedAddress, 
                expectedData, expectedData);
            return false;
        }
        
        int32_t lastOp = readRegister(LAST_OP_8, BIT_8, false, false);
        if (wasWrite && lastOp != LAST_OP_WRITE_VALUE) {
            LOG_WARNING("Last operation was not a write (expected %d, got %ld)", LAST_OP_WRITE_VALUE, lastOp);
            return false;
        } else if (!wasWrite && lastOp != LAST_OP_READ_VALUE) {
            LOG_WARNING("Last operation was not a read (expected %d, got %ld)", LAST_OP_READ_VALUE, lastOp);
            return false;
        }    
        
        // Select the appropriate LAST_RWDATA register based on the bit size
        uint16_t dataRegister;
        uint8_t dataRegisterBits;
        
        if (expectedBits == BIT_8) {
            dataRegister = LAST_RWDATA_8;
            dataRegisterBits = BIT_8;
        } else if (expectedBits == BIT_16) {
            dataRegister = LAST_RWDATA_16;
            dataRegisterBits = BIT_16;
        } else if (expectedBits == BIT_24) {
            dataRegister = LAST_RWDATA_24;
            dataRegisterBits = BIT_24;
        } else { // 32 bits or any other value defaults to 32-bit register
            dataRegister = LAST_RWDATA_32;
            dataRegisterBits = BIT_32;
        }
        
        int32_t lastData = readRegister(dataRegister, dataRegisterBits, signedData, false);
        if (lastData != expectedData) {
            LOG_WARNING("Last data %ld does not match expected %ld", lastData, expectedData);
            return false;
        }

        LOG_VERBOSE("Last communication verified successfully (register: 0x%04lX)", dataRegister);
        return true;
    }

    bool _validateValue(float newValue, float min, float max) {
        if (newValue < min || newValue > max) {
            LOG_WARNING("Value %f out of range (minimum: %f, maximum: %f)", newValue, min, max);
            return false;
        }
        return true;
    }

    bool _validateVoltage(float newValue) { return _validateValue(newValue, VALIDATE_VOLTAGE_MIN, VALIDATE_VOLTAGE_MAX); }
    bool _validateCurrent(float newValue) { return _validateValue(newValue, VALIDATE_CURRENT_MIN, VALIDATE_CURRENT_MAX); }
    bool _validatePower(float newValue) { return _validateValue(newValue, VALIDATE_POWER_MIN, VALIDATE_POWER_MAX); }
    bool _validatePowerFactor(float newValue) { return _validateValue(newValue, VALIDATE_POWER_FACTOR_MIN, VALIDATE_POWER_FACTOR_MAX); }
    bool _validateGridFrequency(float newValue) { return _validateValue(newValue, VALIDATE_GRID_FREQUENCY_MIN, VALIDATE_GRID_FREQUENCY_MAX); }

    // Utility functions
    // =================

    uint8_t _findNextActiveChannel(uint8_t currentChannel) {
        // Since the current channel is initialized with the invalid one (which has a very high value),
        // we need to convert it to a valid channel index (0 is always active) and move on
        uint8_t realCurrentChannel = currentChannel == INVALID_CHANNEL ? 0 : currentChannel;

        // This returns the next channel (except 0, which has to be always active) that is active
        // For i that starts from currentChannel + 1, it will return the first active channel found
        // up to the maximum channel count
        for (uint8_t i = realCurrentChannel + 1; i < CHANNEL_COUNT; i++) {
            if (i != 0 && isChannelActive(i)) {
                return i;
            }
        }

        // If no active channel is found after the current one, it will start from 1 and go up to currentChannel
        // simulating us starting from the beginning
        for (uint8_t i = 1; i < realCurrentChannel; i++) {
            if (i != 0 && isChannelActive(i)) {
                return i;
            }
        }

        return INVALID_CHANNEL; // Invalid channel, no active channels found
    }

    Phase _getLaggingPhase(Phase phase) {
        // Poor man's phase shift (doing with modulus didn't work properly,
        // and in any case the phases will always be at most 3)
        switch (phase) {
            case PHASE_1:
                return PHASE_2;
            case PHASE_2:
                return PHASE_3;
            case PHASE_3:
                return PHASE_1;
            default:
                return PHASE_1;
        }
    }

    Phase _getLeadingPhase(Phase phase) {
        switch (phase) {
            case PHASE_1:
                return PHASE_3;
            case PHASE_2:
                return PHASE_1;
            case PHASE_3:
                return PHASE_2;
            default:
                return PHASE_1;
        }
    }

    const char* _irqstataBitName(uint32_t bit) {
        switch (bit) {
            case IRQSTATA_AEHFA_BIT:       return "AEHFA";
            case IRQSTATA_VAREHFA_BIT:     return "VAREHFA";
            case IRQSTATA_VAEHFA_BIT:      return "VAEHFA";
            case IRQSTATA_AEOFA_BIT:       return "AEOFA";
            case IRQSTATA_VAREOFA_BIT:     return "VAREOFA";
            case IRQSTATA_VAEOFA_BIT:      return "VAEOFA";
            case IRQSTATA_AP_NOLOADA_BIT:  return "AP_NOLOADA";
            case IRQSTATA_VAR_NOLOADA_BIT: return "VAR_NOLOADA";
            case IRQSTATA_VA_NOLOADA_BIT:  return "VA_NOLOADA";
            case IRQSTATA_APSIGN_A_BIT:    return "APSIGN_A";
            case IRQSTATA_VARSIGN_A_BIT:   return "VARSIGN_A";
            case IRQSTATA_ZXTO_IA_BIT:     return "ZXTO_IA";
            case IRQSTATA_ZXIA_BIT:        return "ZXIA";
            case IRQSTATA_OIA_BIT:         return "OIA";
            case IRQSTATA_ZXTO_BIT:        return "ZXTO";
            case IRQSTATA_ZXV_BIT:         return "ZXV";
            case IRQSTATA_OV_BIT:          return "OV";
            case IRQSTATA_WSMP_BIT:        return "WSMP";
            case IRQSTATA_CYCEND_BIT:      return "CYCEND";
            case IRQSTATA_SAG_BIT:         return "SAG";
            case IRQSTATA_RESET_BIT:       return "RESET";
            case IRQSTATA_CRC_BIT:         return "CRC";
            default:                       return "UNKNOWN";
        }
    }

    void _printMeterValues(uint8_t channelIndex) {
        MeterValues meterValues;
        ChannelData channelData;
        
        if (!getMeterValues(meterValues, channelIndex) ||
            !getChannelData(channelData, channelIndex)) 
        {
            return;
        }

        LOG_DEBUG(
            "%s (%u): %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.1f%% || %.3f Wh <- | %.3f Wh -> | %.3f VARh <- | %.3f VARh -> | %.3f VAh", 
            channelData.label,
            channelData.index,
            meterValues.voltage,
            meterValues.current,
            meterValues.activePower,
            meterValues.reactivePower,
            meterValues.apparentPower,
            meterValues.powerFactor * 100.0f,
            meterValues.activeEnergyImported,
            meterValues.activeEnergyExported,
            meterValues.reactiveEnergyImported,
            meterValues.reactiveEnergyExported,
            meterValues.apparentEnergy
        );
    }

    TaskInfo getMeterReadingTaskInfo()
    {
        if (_meterReadingTaskHandle != NULL) {
            return TaskInfo(ADE7953_METER_READING_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(_meterReadingTaskHandle));
        } else {
            return TaskInfo(); // Return empty/default TaskInfo if task is not running
        }
    }

    TaskInfo getEnergySaveTaskInfo()
    {
        if (_energySaveTaskHandle != NULL) {
            return TaskInfo(ADE7953_ENERGY_SAVE_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(_energySaveTaskHandle));
        } else {
            return TaskInfo(); // Return empty/default TaskInfo if task is not running
        }
    }

    TaskInfo getHourlyCsvTaskInfo()
    {
        if (_hourlyCsvSaveTaskHandle != NULL) {
            return TaskInfo(ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(_hourlyCsvSaveTaskHandle));
        } else {
            return TaskInfo(); // Return empty/default TaskInfo if task is not running
        }
    }

    // Waveform capture API
    // ====================

    bool startWaveformCapture(uint8_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            LOG_WARNING("Invalid channel index for waveform capture: %u", channelIndex);
            return false;
        }

        // Check if buffers were allocated successfully at startup
        if (_captureState == CaptureState::ERROR) {
            LOG_ERROR("Cannot start capture, buffer allocation failed at startup");
            return false;
        }

        // Check if another capture is already in progress or armed
        if (_captureState != CaptureState::IDLE && _captureState != CaptureState::COMPLETE) {
            LOG_WARNING("Cannot start capture, another capture is already in progress (state: %u)", static_cast<uint8_t>(_captureState));
            return false;
        }

        _captureRequestedChannel = channelIndex;
        _captureState = CaptureState::ARMED;
        LOG_INFO("Waveform capture armed for channel %u", channelIndex);
        return true;
    }

    uint8_t getWaveformCaptureChannel() {
        return _captureChannel;
    }

    CaptureState getWaveformCaptureStatus() {
        return _captureState;
    }

    uint16_t getWaveformCaptureData(int32_t* vBuffer, int32_t* iBuffer, uint64_t* microsBuffer, uint16_t bufferSize) {
        if (_captureState != CaptureState::COMPLETE) {
            return 0; // No data ready
        }

        if (!vBuffer || !iBuffer || !microsBuffer) {
            LOG_ERROR("Invalid buffer pointers provided to getWaveformCaptureData");
            return 0;
        }

        uint16_t samplesToCopy = (bufferSize < _captureSampleCount) ? bufferSize : _captureSampleCount;
        memcpy(vBuffer, _voltageWaveformBuffer, samplesToCopy * sizeof(int32_t));
        memcpy(iBuffer, _currentWaveformBuffer, samplesToCopy * sizeof(int32_t));
        memcpy(microsBuffer, _microsWaveformBuffer, samplesToCopy * sizeof(uint64_t));

        // Reset state to allow for a new capture
        _captureState = CaptureState::IDLE;
        _captureRequestedChannel = INVALID_CHANNEL;
        _captureChannel = INVALID_CHANNEL;

        LOG_DEBUG("Retrieved %u waveform samples", samplesToCopy);
        return samplesToCopy;
    }

    bool getWaveformCaptureAsJson(JsonDocument& jsonDocument) {
        if (_captureState != CaptureState::COMPLETE) {
            LOG_DEBUG("No waveform data ready for JSON serialization (state: %u)", static_cast<uint8_t>(_captureState));
            return false;
        }

        // Add metadata to the root object
        jsonDocument["channelIndex"] = _captureChannel;
        jsonDocument["sampleCount"] = _captureSampleCount;
        jsonDocument["captureStartUnixMillis"] = _captureStartUnixMillis;
        jsonDocument["captureStartMicros"] = _captureStartMicros;

        // Create JSON arrays for voltage, current, and microseconds (only scaled values)
        JsonArray voltageArray = jsonDocument["voltage"].to<JsonArray>();
        JsonArray currentArray = jsonDocument["current"].to<JsonArray>();
        JsonArray microsArray = jsonDocument["microsDelta"].to<JsonArray>();
        
        // Populate the arrays with scaled values only (leaner JSON)
        for (uint16_t i = 0; i < _captureSampleCount; i++) {
            voltageArray.add(roundToDecimals(float(_voltageWaveformBuffer[i]) * VOLT_PER_LSB_INSTANTANEOUS, VOLTAGE_DECIMALS));
            // HACK: computing the actual value needed for the real current instantaneous values is long. Times 2 is close enough
            currentArray.add(roundToDecimals(float(_currentWaveformBuffer[i]) * _channelData[_captureChannel].ctSpecification.aLsb * 2, CURRENT_DECIMALS));
            microsArray.add(_microsWaveformBuffer[i]);
        }
        
        LOG_INFO("Serialized %u waveform samples to JSON for channel %u and cleared data", _captureSampleCount, _captureChannel);
        
        // Data has been "consumed" by serializing it. Reset for the next capture.
        _captureState = CaptureState::IDLE;
        _captureRequestedChannel = INVALID_CHANNEL;
        _captureChannel = INVALID_CHANNEL;

        return true;
    }
}