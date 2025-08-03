#include "ade7953.h"

static const char *TAG = "ade7953";

namespace Ade7953
{
    // Static variables
    // =========================================================
    // =========================================================
    
    // Hardware pins
    static uint32_t _ssPin;
    static uint32_t _sckPin;
    static uint32_t _misoPin;
    static uint32_t _mosiPin;
    static uint32_t _resetPin;
    static uint32_t _interruptPin;

    // Timing and measurement variables
    static uint32_t _sampleTime; // in milliseconds, time between linecycles readings
    static uint32_t _currentChannel = 0;
    static float _gridFrequency = 50.0f;

    // Failure tracking
    static uint32_t _failureCount = 0;
    static uint64_t _firstFailureTime = 0;

    // Energy saving timestamps
    static uint64_t _lastMillisSaveEnergy = 0;

    // Synchronization primitives
    static SemaphoreHandle_t _spiMutex = NULL; // To handle single SPI operations
    static SemaphoreHandle_t _spiOperationMutex = NULL; // To handle full SPI operations (like read with verification, which is 2 SPI operations)
    static SemaphoreHandle_t _ade7953InterruptSemaphore = NULL;
    static SemaphoreHandle_t _configMutex = NULL; // For thread-safe configuration access

    // FreeRTOS task handles and control flags
    static TaskHandle_t _meterReadingTaskHandle = NULL;
    static bool _meterReadingTaskShouldRun = false;
    static TaskHandle_t _energySaveTaskHandle = NULL;
    static bool _energySaveTaskShouldRun = false;
    static TaskHandle_t _hourlyCsvSaveTaskHandle = NULL;
    static bool _hourlyCsvSaveTaskShouldRun = false;

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
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin
    );
    static void _reset();
    // According to the datasheet, setting these registers is mandatory for optimal operation
    static void _setOptimumSettings();
    static void _setDefaultParameters();

    // System management
    static void _initializeSpiMutexes();
    static void _cleanup();
    static bool _verifyCommunication();

    // Interrupt handling
    static void _setupInterrupts();
    static Ade7953InterruptType _handleInterrupt();
    static void _attachInterruptHandler();
    static void _detachInterruptHandler();
    static void IRAM_ATTR _isrHandler();
    static void _handleCycendInterrupt(uint64_t linecycUnix);
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
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);

    // Channel data management
    static void _setChannelDataFromPreferences(uint32_t channelIndex);
    static bool _saveChannelDataToPreferences(uint32_t channelIndex);
    static void _updateChannelData(uint32_t channelIndex);
    static bool _validateChannelDataJson(JsonDocument &jsonDocument, bool partial = false);
    static void _calculateLsbValues(CtSpecification &ctSpec);

    // Energy data management
    static void _setEnergyFromPreferences(uint32_t channelIndex);
    static void _saveEnergyToPreferences(uint32_t channelIndex);
    static void _saveHourlyEnergyToCsv(); // Not per channel so that we open the file only once
    static void _saveEnergyComplete();

    // Meter reading and processing
    static bool _readMeterValues(int32_t channel, uint64_t linecycUnixTime);
    static bool _processChannelReading(int32_t channel, uint64_t linecycUnix);
    static void _addMeterDataToPayload(int32_t channel);

    // ADE7953 register writing functions
    static void _setLinecyc(uint32_t linecyc);
    static void _setPgaGain(int32_t pgaGain, Ade7953Channel channel, MeasurementType measurementType);
    static void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel channel);
    static void _setGain(int32_t gain, Ade7953Channel channel, MeasurementType measurementType);
    static void _setOffset(int32_t offset, Ade7953Channel channel, MeasurementType measurementType);

    // Sample time management
    static void _updateSampleTime();
    static void _setSampleTimeFromPreferences();
    static void _saveSampleTimeToPreferences();

    // ADE7953 register reading functions

    static int32_t _readApparentPowerInstantaneous(Ade7953Channel channel);
    /*
    Reads the "instantaneous" active power.

    "Instantaneous" because the active power is only defined as the dc component
    of the instantaneous power signal, which is V_RMS * I_RMS - V_RMS * I_RMS * cos(2*omega*t). 
    It is updated at 6.99 kHz.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The active power in LSB.
    */
    static int32_t _readActivePowerInstantaneous(Ade7953Channel channel);
    static int32_t _readReactivePowerInstantaneous(Ade7953Channel channel);
    /*
    Reads the actual instantaneous current. 

    This allows you see the actual sinusoidal waveform, so both positive and 
    negative values. At full scale (so 500 mV), the value returned is 9032007d.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The actual instantaneous current in LSB.
    */
    static int32_t _readCurrentInstantaneous(Ade7953Channel channel);
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
    static int32_t _readCurrentRms(Ade7953Channel channel);
    /*
    Reads the voltage in RMS.

    This measurement is updated at 6.99 kHz and has a settling
    time of 200 ms. The value is in LSB.

    @return The voltage in RMS in LSB.
    */
    static int32_t _readVoltageRms();
    static int32_t _readActiveEnergy(Ade7953Channel channel);
    static int32_t _readReactiveEnergy(Ade7953Channel channel);
    static int32_t _readApparentEnergy(Ade7953Channel channel);
    static int32_t _readPowerFactor(Ade7953Channel channel);
    static int32_t _readAngle(Ade7953Channel channel);
    static int32_t _readPeriod();

    // Verification and validation functions
    static void _recordFailure();
    static void _checkForTooManyFailures();
    static bool _verifyLastSpiCommunication(int32_t expectedAddress, int32_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite);
    static bool _validateValue(float newValue, float min, float max);
    static bool _validateVoltage(float newValue);
    static bool _validateCurrent(float newValue);
    static bool _validatePower(float newValue);
    static bool _validatePowerFactor(float newValue);
    static bool _validateGridFrequency(float newValue);

    // Utility functions
    static uint32_t _findNextActiveChannel(uint32_t currentChannel);
    static Phase _getLaggingPhase(Phase phase);
    static Phase _getLeadingPhase(Phase phase);
    // Returns the string name of the IRQSTATA bit, or UNKNOWN if the bit is not recognized.
    static void _irqstataBitName(int32_t bit, char *buffer, size_t bufferSize);
    void _printMeterValues(MeterValues* meterValues, ChannelData* channelData);


    // Public API functions
    // =========================================================
    // =========================================================

    // Core lifecycle management
    // =========================

    bool begin(
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin
    ) {
        logger.debug("Initializing Ade7953", TAG);

        _initializeSpiMutexes();
        logger.debug("Initialized SPI mutexes", TAG);
      
        _setHardwarePins(ssPin, sckPin, misoPin, mosiPin, resetPin, interruptPin);
        logger.debug("Successfully set up hardware pins", TAG);
     
        if (!_verifyCommunication()) {
            logger.error("Failed to communicate with ADE7953", TAG);
            return false;
        }
        logger.debug("Communication with ADE7953 verified", TAG);
                
        _setOptimumSettings();
        logger.debug("Set optimum settings", TAG);
        
        _setDefaultParameters();
        logger.debug("Set default parameters", TAG);

        _setConfigurationFromPreferences();
        logger.debug("Done setting configuration from Preferences", TAG);

        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            _setChannelDataFromPreferences(i);
        }
        logger.debug("Done setting channel data from Preferences", TAG);

        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            _setEnergyFromPreferences(i);
        }
        logger.debug("Done setting energy from Preferences", TAG);

        _setupInterrupts();
        logger.debug("Set up interrupts", TAG);

        _startMeterReadingTask();
        logger.debug("Meter reading task started", TAG);

        _startEnergySaveTask();
        logger.debug("Energy save task started", TAG);

        _startHourlyCsvSaveTask();
        logger.debug("Hourly CSV save task started", TAG);

        return true;
    }

    void stop() {
        logger.debug("Stopping ADE7953...", TAG);
        
        // Clean up resources (where the data will also be saved)
        _cleanup();
        
        logger.info("ADE7953 stopped successfully", TAG);
    }

    // Register operations
    // ===================

    int32_t readRegister(int32_t registerAddress, int32_t nBits, bool signedData, bool isVerificationRequired) {

        if (isVerificationRequired) {
            if (_spiOperationMutex == NULL || xSemaphoreTake(_spiOperationMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
                logger.error("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
                return INVALID_SPI_READ_WRITE;
            }
        }

        // Acquire SPI mutex with timeout to prevent deadlocks
        if (_spiMutex == NULL || xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            logger.error("Failed to acquire SPI mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
            if (isVerificationRequired) xSemaphoreGive(_spiOperationMutex);
            return INVALID_SPI_READ_WRITE;
        }

        digitalWrite(_ssPin, LOW);

        SPI.transfer(registerAddress >> 8);
        SPI.transfer(registerAddress & 0xFF);
        SPI.transfer(READ_TRANSFER);

        byte _response[nBits / 8];
        for (int32_t i = 0; i < nBits / 8; i++) {
            _response[i] = SPI.transfer(READ_TRANSFER);
        }

        digitalWrite(_ssPin, HIGH);

        xSemaphoreGive(_spiMutex); // Leave as soon as possible since no more SPI operations are needed

        int32_t _long_response = 0;
        for (int32_t i = 0; i < nBits / 8; i++) {
            _long_response = (_long_response << 8) | _response[i];
        }
        if (signedData) {
            if (_long_response & (1 << (nBits - 1))) {
                _long_response -= (1 << nBits);
            }
        }
        logger.verbose(
            "Read %ld from register %ld with %d bits",
            TAG,
            _long_response,
            registerAddress,
            nBits
        );

        if (isVerificationRequired) {
            if (!_verifyLastSpiCommunication(registerAddress, nBits, _long_response, signedData, false)) {
                logger.debug("Failed to verify last read communication for register %ld (0x%04lX). Value was %ld (0x%04lX)", TAG, registerAddress, registerAddress, _long_response, _long_response);
                _recordFailure();
                _long_response = INVALID_SPI_READ_WRITE; // Return an invalid value if verification fails
            }
            xSemaphoreGive(_spiOperationMutex);
        }

        return _long_response;
    }

    void writeRegister(int32_t registerAddress, int32_t nBits, int32_t data, bool isVerificationRequired) {
        logger.debug(
            "Writing %ld (0x%04lX) to register %ld (0x%04lX) with %d bits",
            TAG,
            data, data,
            registerAddress, registerAddress,
            nBits
        );

        if (isVerificationRequired) {
            if (_spiOperationMutex == NULL || xSemaphoreTake(_spiOperationMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
                logger.error("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
                return;
            }
        }

        // Acquire SPI mutex with timeout to prevent deadlocks
        if (_spiMutex == NULL || xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            logger.error("Failed to acquire SPI mutex for write operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
            if (isVerificationRequired) xSemaphoreGive(_spiOperationMutex);
            return;
        }

        digitalWrite(_ssPin, LOW);

        SPI.transfer(registerAddress >> 8);
        SPI.transfer(registerAddress & 0xFF);
        SPI.transfer(WRITE_TRANSFER);

        if (nBits / 8 == 4) {
            SPI.transfer((data >> 24) &  0xFF);
            SPI.transfer((data >> 16) & 0xFF);
            SPI.transfer((data >> 8) & 0xFF);
            SPI.transfer(data & 0xFF);
        } else if (nBits / 8 == 3) {
            SPI.transfer((data >> 16) & 0xFF);
            SPI.transfer((data >> 8) & 0xFF);
            SPI.transfer(data & 0xFF);
        } else if (nBits / 8 == 2) {
            SPI.transfer((data >> 8) & 0xFF);
            SPI.transfer(data & 0xFF);
        } else if (nBits / 8 == 1) {
            SPI.transfer(data & 0xFF);
        }

        digitalWrite(_ssPin, HIGH);

        xSemaphoreGive(_spiMutex);

        if (isVerificationRequired) {
            if (!_verifyLastSpiCommunication(registerAddress, nBits, data, false, true)) {
                logger.warning("Failed to verify last write communication for register %ld", TAG, registerAddress);
                _recordFailure();
            }
            xSemaphoreGive(_spiOperationMutex);
        }
    }

    // Task control
    // ============

    void pauseTasks() {
        logger.debug("Pausing ADE7953 tasks...", TAG);

        _detachInterruptHandler();
        if (_meterReadingTaskHandle != NULL) vTaskSuspend(_meterReadingTaskHandle);
        if (_energySaveTaskHandle != NULL) vTaskSuspend(_energySaveTaskHandle);
        if (_hourlyCsvSaveTaskHandle != NULL) vTaskSuspend(_hourlyCsvSaveTaskHandle);

        logger.info("ADE7953 tasks suspended", TAG);
    }

    void resumeTasks() {
        logger.debug("Resuming ADE7953 tasks...", TAG);

        if (_meterReadingTaskHandle != NULL) vTaskResume(_meterReadingTaskHandle);
        if (_energySaveTaskHandle != NULL) vTaskResume(_energySaveTaskHandle);
        if (_hourlyCsvSaveTaskHandle != NULL) vTaskResume(_hourlyCsvSaveTaskHandle);
        _attachInterruptHandler();

        logger.info("ADE7953 tasks resumed", TAG);
    }

    // Configuration management
    // ========================

    void getConfiguration(Ade7953Configuration &config) {
        config = _configuration;
    }

    bool setConfiguration(const Ade7953Configuration &config) {
        if (xSemaphoreTake(_configMutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            logger.error("Failed to acquire config mutex for setConfiguration", TAG);
            return false;
        }

        _configuration = config;
        _applyConfiguration(_configuration);
        
        _saveConfigurationToPreferences();
        
        xSemaphoreGive(_configMutex);

        logger.debug("Configuration set successfully", TAG);
        return true;
    }

    void resetConfiguration() {
        logger.debug("Resetting ADE7953 configuration to defaults...", TAG);

        Ade7953Configuration defaultConfig;
        setConfiguration(defaultConfig);
    }

    // Configuration management - JSON operations
    // ==========================================

    void getConfigurationAsJson(JsonDocument &jsonDocument) {
        configurationToJson(_configuration, jsonDocument);
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        Ade7953Configuration config;
        config = _configuration; // Start with current configuration
        if (!configurationFromJson(jsonDocument, config, partial)) {
            logger.error("Failed to set configuration from JSON", TAG);
            return false;
        }

        return setConfiguration(config);
    }

    void configurationToJson(Ade7953Configuration &config, JsonDocument &jsonDocument)
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

        logger.debug("Successfully converted configuration to JSON", TAG);
    }

    bool configurationFromJson(JsonDocument &jsonDocument, Ade7953Configuration &config, bool partial)
    {
        if (!_validateJsonConfiguration(jsonDocument, partial))
        {
            logger.warning("Invalid JSON configuration", TAG);
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

        logger.debug("Successfully converted JSON to configuration%s", TAG, partial ? " (partial)" : "");
        return true;
    }

    // Sample time management
    // ======================

    uint32_t getSampleTime() { 
        return _sampleTime; 
    }

    bool setSampleTime(uint32_t sampleTime) {
        if (sampleTime < MINIMUM_SAMPLE_TIME) {
            logger.warning("Sample time %lu is below minimum %lu", TAG, sampleTime, MINIMUM_SAMPLE_TIME);
            return false;
        }

        _sampleTime = sampleTime;
        _updateSampleTime();
        _saveSampleTimeToPreferences();

        return true;
    }

    // Channel data management
    // =======================

    bool isChannelActive(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return false;
        }

        return _channelData[channelIndex].active;
    }

    bool hasChannelValidMeasurements(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return false;
        }

        return _meterValues[channelIndex].lastUnixTimeMilliseconds != 0; // If it is 0, we did not add any data point yet
    }

    void getChannelLabel(uint32_t channelIndex, char* buffer, size_t bufferSize) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        snprintf(buffer, bufferSize, "%s", _channelData[channelIndex].label);
    }

    void getChannelData(ChannelData &channelData, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        channelData = _channelData[channelIndex];
    }

    void setChannelData(const ChannelData &channelData, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        // Protect channel 0 from being disabled
        if (channelIndex == 0 && !channelData.active) {
            logger.warning("Attempt to disable channel 0 blocked - channel 0 must remain active", TAG);
            _channelData[channelIndex].active = true;
        } else {
            _channelData[channelIndex] = channelData;
        }

        _updateChannelData(channelIndex);
        _saveChannelDataToPreferences(channelIndex);
        Mqtt::requestChannelPublish();

        logger.debug("Successfully set channel data for channel %lu", TAG, channelIndex);
    }

    void resetChannelData(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        ChannelData channelData;
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            setChannelData(channelData, i);
        }

        logger.debug("Successfully reset channel data for channel %lu", TAG, channelIndex);
    }

    // Channel data management - JSON operations
    // =========================================

    void getChannelDataAsJson(JsonDocument &jsonDocument, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }
        
        channelDataToJson(_channelData[channelIndex], jsonDocument);
    }

    bool setChannelDataFromJson(JsonDocument &jsonDocument, bool partial) {
        if (!_validateChannelDataJson(jsonDocument, partial)) {
            logger.error("Invalid channel data JSON", TAG);
            return false;
        }

        uint32_t channelIndex = jsonDocument["index"].as<uint32_t>();
        
        if (!isChannelValid(channelIndex)) {
            logger.error("Invalid channel index: %lu", TAG, channelIndex);
            return false;
        }

        ChannelData channelData;
        channelData = _channelData[channelIndex];
        
        if (!channelDataFromJson(jsonDocument, channelData, partial)) {
            logger.error("Failed to convert JSON to channel data", TAG);
            return false;
        }
        
        setChannelData(channelData, channelIndex);

        return true;
    }

    void channelDataToJson(ChannelData &channelData, JsonDocument &jsonDocument) {
        jsonDocument["index"] = channelData.index;
        jsonDocument["active"] = channelData.active;
        jsonDocument["reverse"] = channelData.reverse;
        jsonDocument["label"] = channelData.label;
        jsonDocument["phase"] = channelData.phase;

        jsonDocument["ctSpecification"]["currentRating"] = channelData.ctSpecification.currentRating;
        jsonDocument["ctSpecification"]["voltageOutput"] = channelData.ctSpecification.voltageOutput;
        jsonDocument["ctSpecification"]["scalingFraction"] = channelData.ctSpecification.scalingFraction;

        logger.debug("Successfully converted channel data to JSON for channel %lu", TAG, channelData.index);
    }

    bool channelDataFromJson(JsonDocument &jsonDocument, ChannelData &channelData, bool partial) {
        if (partial) {
            // Update only fields that are present in JSON
            if (jsonDocument["index"].is<uint32_t>()) channelData.index = jsonDocument["index"].as<uint32_t>();
            if (jsonDocument["active"].is<bool>()) channelData.active = jsonDocument["active"].as<bool>();
            if (jsonDocument["reverse"].is<bool>()) channelData.reverse = jsonDocument["reverse"].as<bool>();
            if (jsonDocument["label"].is<const char*>()) {
                snprintf(channelData.label, sizeof(channelData.label), "%s", jsonDocument["label"].as<const char*>());
            }
            if (jsonDocument["phase"].is<uint8_t>()) channelData.phase = static_cast<Phase>(jsonDocument["phase"].as<uint8_t>());
            
            // CT Specification fields
            if (jsonDocument["ctSpecification"]["currentRating"].is<uint32_t>()) {
                channelData.ctSpecification.currentRating = jsonDocument["ctSpecification"]["currentRating"].as<uint32_t>();
            }
            if (jsonDocument["ctSpecification"]["voltageOutput"].is<uint32_t>()) {
                channelData.ctSpecification.voltageOutput = jsonDocument["ctSpecification"]["voltageOutput"].as<uint32_t>();
            }
            if (jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) {
                channelData.ctSpecification.scalingFraction = jsonDocument["ctSpecification"]["scalingFraction"].as<float>();
            }
        } else {
            // Full update - set all fields
            channelData.index = jsonDocument["index"].as<uint32_t>();
            channelData.active = jsonDocument["active"].as<bool>();
            channelData.reverse = jsonDocument["reverse"].as<bool>();
            snprintf(channelData.label, sizeof(channelData.label), "%s", jsonDocument["label"].as<const char*>());
            channelData.phase = static_cast<Phase>(jsonDocument["phase"].as<uint8_t>());
            
            channelData.ctSpecification.currentRating = jsonDocument["ctSpecification"]["currentRating"].as<uint32_t>();
            channelData.ctSpecification.voltageOutput = jsonDocument["ctSpecification"]["voltageOutput"].as<uint32_t>();
            channelData.ctSpecification.scalingFraction = jsonDocument["ctSpecification"]["scalingFraction"].as<float>();
        }

        logger.debug("Successfully converted JSON to channel data for channel %lu%s", TAG, channelData.index, partial ? " (partial)" : "");
        return true;
    }

    // Energy data management
    // ======================

    void resetEnergyValues() {
        logger.warning("Resetting energy values to 0", TAG);

        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            _meterValues[i].activeEnergyImported = 0.0f;
            _meterValues[i].activeEnergyExported = 0.0f;
            _meterValues[i].reactiveEnergyImported = 0.0f;
            _meterValues[i].reactiveEnergyExported = 0.0f;
            _meterValues[i].apparentEnergy = 0.0f;
        }

        // Clear energy preferences
        Preferences preferences;
        preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false);
        preferences.clear();
        preferences.end();

        // Remove all CSV energy files
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String fileName = file.name();
            if (fileName.startsWith("/energy_") && fileName.endsWith(".csv")) {
                SPIFFS.remove(fileName.c_str());
                logger.debug("Removed energy CSV file: %s", TAG, fileName.c_str());
            }
            file = root.openNextFile();
        }
        root.close();

        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) _saveEnergyToPreferences(i);

        logger.info("Successfully reset energy values to 0", TAG);
    }

    bool setEnergyValues(
        uint32_t channelIndex,
        float activeEnergyImported,
        float activeEnergyExported,
        float reactiveEnergyImported,
        float reactiveEnergyExported,
        float apparentEnergy
    ) {
        logger.warning("Setting energy values for channel %d", TAG, channelIndex);

        if (channelIndex < 0 || channelIndex >= CHANNEL_COUNT) {
            logger.error("Invalid channel index %d", TAG, channelIndex);
            return false;
        }

        if (activeEnergyImported < 0 || activeEnergyExported < 0 || 
            reactiveEnergyImported < 0 || reactiveEnergyExported < 0 || 
            apparentEnergy < 0) {
            logger.error("Energy values must be non-negative", TAG);
            return false;
        }

        _meterValues[channelIndex].activeEnergyImported = activeEnergyImported;
        _meterValues[channelIndex].activeEnergyExported = activeEnergyExported;
        _meterValues[channelIndex].reactiveEnergyImported = reactiveEnergyImported;
        _meterValues[channelIndex].reactiveEnergyExported = reactiveEnergyExported;
        _meterValues[channelIndex].apparentEnergy = apparentEnergy;

        _saveEnergyToPreferences(channelIndex);
        
        logger.info("Successfully set energy values for channel %d", TAG, channelIndex);
        return true;
    }

    // Data output
    // ===========

    void singleMeterValuesToJson(JsonDocument &jsonDocument, uint32_t channel) {
        if (!isChannelValid(channel)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channel);
            return;
        }

        jsonDocument["voltage"] = _meterValues[channel].voltage;
        jsonDocument["current"] = _meterValues[channel].current;
        jsonDocument["activePower"] = _meterValues[channel].activePower;
        jsonDocument["apparentPower"] = _meterValues[channel].apparentPower;
        jsonDocument["reactivePower"] = _meterValues[channel].reactivePower;
        jsonDocument["powerFactor"] = _meterValues[channel].powerFactor;
        jsonDocument["activeEnergyImported"] = _meterValues[channel].activeEnergyImported;
        jsonDocument["activeEnergyExported"] = _meterValues[channel].activeEnergyExported;
        jsonDocument["reactiveEnergyImported"] = _meterValues[channel].reactiveEnergyImported;
        jsonDocument["reactiveEnergyExported"] = _meterValues[channel].reactiveEnergyExported;
        jsonDocument["apparentEnergy"] = _meterValues[channel].apparentEnergy;
    }


    void fullMeterValuesToJson(JsonDocument &jsonDocument) {
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            // Here we also ensure the channel has valid measurements since we have the "duty" to pass all the correct data
            if (isChannelActive(i) && hasChannelValidMeasurements(i)) {
                JsonObject _jsonChannel = jsonDocument.add<JsonObject>();
                _jsonChannel["index"] = i;
                _jsonChannel["label"] = _channelData[i].label;
                _jsonChannel["phase"] = _channelData[i].phase;

                JsonDocument _jsonData;
                singleMeterValuesToJson(_jsonData, i);
                _jsonChannel["data"] = _jsonData.as<JsonObject>();
            }
        }
    }

    void getMeterValues(MeterValues &meterValues, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        meterValues = _meterValues[channelIndex];
    }

    // Aggregated power calculations 
    // =============================

    float getAggregatedActivePower(bool includeChannel0) {
        float sum = 0.0f;
        int32_t activeChannelCount = 0;

        for (int32_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].activePower;
                activeChannelCount++;
            }
        }
        return activeChannelCount > 0 ? sum : 0.0f;
    }

    float getAggregatedReactivePower(bool includeChannel0) {
        float sum = 0.0f;
        int32_t activeChannelCount = 0;

        for (int32_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].reactivePower;
                activeChannelCount++;
            }
        }
        return activeChannelCount > 0 ? sum : 0.0f;
    }

    float getAggregatedApparentPower(bool includeChannel0) {
        float sum = 0.0f;
        int32_t activeChannelCount = 0;

        for (int32_t i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                sum += _meterValues[i].apparentPower;
                activeChannelCount++;
            }
        }
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
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin
    ) {
        logger.debug("Setting hardware pins...", TAG);

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

        logger.debug("Successfully set hardware pins", TAG);
    }

    void _reset() {
        digitalWrite(_resetPin, LOW);
        delay(ADE7953_RESET_LOW_DURATION);
        digitalWrite(_resetPin, HIGH);
        delay(ADE7953_RESET_LOW_DURATION);

        logger.debug("Reset ADE7953", TAG);
    }

    void _setOptimumSettings()
    {
        writeRegister(UNLOCK_OPTIMUM_REGISTER, BIT_8, UNLOCK_OPTIMUM_REGISTER_VALUE);
        writeRegister(Reserved_16, BIT_16, DEFAULT_OPTIMUM_REGISTER);

        logger.debug("Optimum settings applied", TAG);
    }

    void _setDefaultParameters()
    {
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::VOLTAGE);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::CURRENT);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::B, MeasurementType::CURRENT);

        writeRegister(DISNOLOAD_8, BIT_8, DEFAULT_DISNOLOAD_REGISTER);

        writeRegister(AP_NOLOAD_32, BIT_32, DEFAULT_X_NOLOAD_REGISTER);
        writeRegister(VAR_NOLOAD_32, BIT_32, DEFAULT_X_NOLOAD_REGISTER);
        writeRegister(VA_NOLOAD_32, BIT_32, DEFAULT_X_NOLOAD_REGISTER);

        writeRegister(LCYCMODE_8, BIT_8, DEFAULT_LCYCMODE_REGISTER);

        writeRegister(CONFIG_16, BIT_16, DEFAULT_CONFIG_REGISTER);

        logger.debug("Default parameters set", TAG);
    }

    // System management
    // =================

    void _initializeSpiMutexes()
    {
        _spiMutex = xSemaphoreCreateMutex();
        if (_spiMutex == NULL)
        {
            logger.error("Failed to create SPI mutex", TAG);
            return;
        }
        logger.debug("SPI mutex created successfully", TAG);

        
        _spiOperationMutex = xSemaphoreCreateMutex();
        if (_spiOperationMutex == NULL)
        {
            logger.error("Failed to create SPI operation mutex", TAG);
            vSemaphoreDelete(_spiMutex);
            _spiMutex = NULL;
            return;
        }
        logger.debug("SPI operation mutex created successfully", TAG);

        _configMutex = xSemaphoreCreateMutex();
        if (_configMutex == NULL)
        {
            logger.error("Failed to create config mutex", TAG);
            vSemaphoreDelete(_spiMutex);
            vSemaphoreDelete(_spiOperationMutex);
            _spiMutex = NULL;
            _spiOperationMutex = NULL;
            return;
        }
        
        logger.debug("Config mutex created successfully", TAG);
    }

    void _cleanup() {
        logger.debug("Cleaning up ADE7953 resources", TAG);
        
        // Stop all tasks first
        _stopMeterReadingTask();
        _stopEnergySaveTask();
        _stopHourlyCsvSaveTask();
        
        // Save final energy data if not already saved
        logger.debug("Saving final energy data during cleanup", TAG);
        _saveEnergyComplete();
        
        // Clean up SPI mutex
        if (_spiMutex != NULL) {
            vSemaphoreDelete(_spiMutex);
            _spiMutex = NULL;
            logger.debug("SPI mutex deleted", TAG);
        }

        if (_spiOperationMutex != NULL) {
            vSemaphoreDelete(_spiOperationMutex);
            _spiOperationMutex = NULL;
            logger.debug("SPI operation mutex deleted", TAG);
        }

        if (_configMutex != NULL) {
            vSemaphoreDelete(_configMutex);
            _configMutex = NULL;
            logger.debug("Config mutex deleted", TAG);
        }
    }

    /**
     * Verifies the communication with the ADE7953 device.
     * This function reads a specific register from the device and checks if it matches the default value.
     * 
     * @return true if the communication with the ADE7953 is successful, false otherwise.
     */
    bool _verifyCommunication() {
        logger.debug("Verifying communication with Ade7953...", TAG);
        
        uint32_t attempt = 0;
        bool success = false;
        uint64_t lastMillisAttempt = 0;

        uint32_t loops = 0;
        while (attempt < ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS && !success && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            if (millis64() - lastMillisAttempt < ADE7953_VERIFY_COMMUNICATION_INTERVAL) {
                continue;
            }

            logger.debug("Attempt (%lu/%lu) to communicate with ADE7953", TAG, attempt+1, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
            
            _reset();
            attempt++;
            lastMillisAttempt = millis64();

            if ((readRegister(AP_NOLOAD_32, 32, false)) == DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
                logger.debug("Communication successful with ADE7953", TAG);
                return true;
            }
        }

        logger.warning("Failed to communicate with ADE7953 after %lu attempts", TAG, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        return false;
    }

    // Interrupt handling
    // ==================

    void _setupInterrupts() {
        logger.debug("Setting up ADE7953 interrupts...", TAG);
        
        // Enable only CYCEND interrupt for line cycle end detection (bit 18)
        writeRegister(IRQENA_32, 32, DEFAULT_IRQENA_REGISTER);
        
        // Clear any existing interrupt status
        readRegister(RSTIRQSTATA_32, 32, false);
        readRegister(RSTIRQSTATB_32, 32, false);

        logger.debug("ADE7953 interrupts enabled: CYCEND, RESET", TAG);
    }

    Ade7953InterruptType _handleInterrupt() 
    {    
        int32_t statusA = readRegister(RSTIRQSTATA_32, 32, false);
        // No need to read for channel B (only channel A has the relevant information for use)

        if (statusA & (1 << IRQSTATA_RESET_BIT)) { 
            return Ade7953InterruptType::RESET;
        } else if (statusA & (1 << IRQSTATA_CRC_BIT)) {
            return Ade7953InterruptType::CRC_CHANGE;
        } else if (statusA & (1 << IRQSTATA_CYCEND_BIT)) {
            return Ade7953InterruptType::CYCEND;
        } else {
            // Just log the unhandled status
            logger.warning("Unhandled ADE7953 interrupt status: 0x%08lX", TAG, statusA);
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

        // Signal the task to handle the interrupt - let the task determine the cause
        if (_ade7953InterruptSemaphore != NULL) {
            xSemaphoreGiveFromISR(_ade7953InterruptSemaphore, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
        }
    }

    void _handleCycendInterrupt(uint64_t linecycUnix) {
        logger.verbose("Line cycle end detected on Channel A", TAG);
        statistics.ade7953TotalHandledInterrupts++;
        
        // Only for CYCEND interrupts, switch to next channel and set multiplexer
        // This is because thanks to the linecyc approach, the data in the ADE7953 is "frozen"
        // until the next linecyc interrupt is received, which is plenty of time to read the data
        _currentChannel = _findNextActiveChannel(_currentChannel);

        // Weird way to ensure we don't go below 0 and we set the multiplexer to the channel minus 
        // 1 (since channel 0 does not pass through the multiplexer)
        Multiplexer::setChannel(max(_currentChannel - 1UL, 0UL));
        
        // Process current channel (if active)
        if (_currentChannel != INVALID_CHANNEL) _processChannelReading(_currentChannel, linecycUnix);
        
        // Always process channel 0 as it is on a separate ADE7953 channel
        _processChannelReading(0, linecycUnix);
    }

    void _handleCrcChangeInterrupt() {
        // This indicates the configuration changed. We should probably re-initialize the device
        logger.warning("TO BE IMPLEMENTED: CRC change detected - this may indicate configuration changes", TAG);
    }

    void _handleResetInterrupt() {
        // This also indicates 
        logger.warning("TO BE IMPLEMENTED: ADE7953 reset interrupt detected - reinitializing device", TAG);
    }

    void _handleOtherInterrupt() {
        logger.warning("TO BE IMPLEMENTED: unknown ADE7953 interrupt - this may indicate an unexpected condition", TAG);
    }

    // Tasks
    // =====

    void _startMeterReadingTask() {
        if (_meterReadingTaskHandle != NULL) {
            logger.debug("ADE7953 meter reading task is already running", TAG);
            return;
        }

        // Create interrupt semaphore if not exists
        if (_ade7953InterruptSemaphore == NULL) {
            _ade7953InterruptSemaphore = xSemaphoreCreateBinary();
            if (_ade7953InterruptSemaphore == NULL) {
                logger.error("Failed to create ADE7953 interrupt semaphore", TAG);
                return;
            }
        }

        // Attach interrupt handler
        _attachInterruptHandler();
        
        logger.debug("Starting ADE7953 meter reading task", TAG);
        BaseType_t result = xTaskCreate(
            _meterReadingTask, 
            ADE7953_METER_READING_TASK_NAME, 
            ADE7953_METER_READING_TASK_STACK_SIZE, 
            nullptr, 
            ADE7953_METER_READING_TASK_PRIORITY, 
            &_meterReadingTaskHandle
        );
        
        if (result != pdPASS) {
            logger.error("Failed to create ADE7953 meter reading task", TAG);
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
        logger.debug("ADE7953 meter reading task started", TAG);
        
        _meterReadingTaskShouldRun = true;
        // Wait for the interrupt (every sample time plus some headroom)
        const TickType_t timeoutTicks = pdMS_TO_TICKS(ADE7953_INTERRUPT_TIMEOUT_MS + _sampleTime);
        
        while (_meterReadingTaskShouldRun)
        {
            // Wait for interrupt signal with timeout
            if (_ade7953InterruptSemaphore != NULL &&
                xSemaphoreTake(_ade7953InterruptSemaphore, timeoutTicks) == pdTRUE)
            {
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
            }
            
            // Check for stop notification (non-blocking) - this gives immediate shutdown response
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, 0);
            if (notificationValue > 0) {
                _meterReadingTaskShouldRun = false;
                break;
            }
        }

        logger.debug("ADE7953 meter reading task stopping", TAG);
        _meterReadingTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void _startEnergySaveTask() {
        if (_energySaveTaskHandle != NULL) {
            logger.debug("ADE7953 energy save task is already running", TAG);
            return;
        }

        logger.debug("Starting ADE7953 energy save task", TAG);
        BaseType_t result = xTaskCreate(
            _energySaveTask,
            ADE7953_ENERGY_SAVE_TASK_NAME,
            ADE7953_ENERGY_SAVE_TASK_STACK_SIZE,
            nullptr,
            ADE7953_ENERGY_SAVE_TASK_PRIORITY,
            &_energySaveTaskHandle
        );

        if (result != pdPASS) {
            logger.error("Failed to create ADE7953 energy save task", TAG);
            _energySaveTaskHandle = NULL;
        }
    }

    void _stopEnergySaveTask() {
        stopTaskGracefully(&_energySaveTaskHandle, "ADE7953 energy save task");
    }

    void _energySaveTask(void* parameter) {
        logger.debug("ADE7953 energy save task started", TAG);

        _energySaveTaskShouldRun = true;
        while (_energySaveTaskShouldRun) {
            for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
                if (isChannelActive(i)) _saveEnergyToPreferences(i);
            }

            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SAVE_ENERGY_INTERVAL));
            if (notificationValue > 0) {
                _energySaveTaskShouldRun = false;
                break;
            }
        }

        logger.debug("ADE7953 energy save task stopping", TAG);
        _energySaveTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void _startHourlyCsvSaveTask() {
        if (_hourlyCsvSaveTaskHandle != NULL) {
            logger.debug("ADE7953 hourly CSV save task is already running", TAG);
            return;
        }

        logger.debug("Starting ADE7953 hourly CSV save task", TAG);
        BaseType_t result = xTaskCreate(
            _hourlyCsvSaveTask,
            ADE7953_HOURLY_CSV_SAVE_TASK_NAME,
            ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE,
            nullptr,
            ADE7953_HOURLY_CSV_SAVE_TASK_PRIORITY,
            &_hourlyCsvSaveTaskHandle
        );

        if (result != pdPASS) {
            logger.error("Failed to create ADE7953 hourly CSV save task", TAG);
            _hourlyCsvSaveTaskHandle = NULL;
        }
    }

    void _stopHourlyCsvSaveTask() {
        stopTaskGracefully(&_hourlyCsvSaveTaskHandle, "ADE7953 hourly CSV save task");
    }

    void _hourlyCsvSaveTask(void* parameter) {
        logger.debug("ADE7953 hourly CSV save task started", TAG);

        _hourlyCsvSaveTaskShouldRun = true;
        while (_hourlyCsvSaveTaskShouldRun) {
            // Calculate milliseconds until next hour using CustomTime
            uint64_t msUntilNextHour = CustomTime::getMillisecondsUntilNextHour();
            
            // Convert to ticks for FreeRTOS (max value is portMAX_DELAY for very int32_t waits)
            TickType_t ticksToWait = (msUntilNextHour > portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(msUntilNextHour);
            
            // Wait for the calculated time or stop notification
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, ticksToWait);
            if (notificationValue > 0) {
                _hourlyCsvSaveTaskShouldRun = false;
                break;
            }
            
            // Only save if we're still running and within tolerance of hourly save time
            if (_hourlyCsvSaveTaskShouldRun){
                if (
                    CustomTime::isTimeSynched() &&
                    CustomTime::isNowCloseToHour() // Only save on the hour for clean data
                ) {
                    _saveHourlyEnergyToCsv();
                }
            }
        }

        logger.debug("ADE7953 hourly CSV save task stopping", TAG);
        _hourlyCsvSaveTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    // Configuration management functions
    // ==================================

    void _setConfigurationFromPreferences() {
        logger.debug("Setting configuration from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, true)) { // true = read-only
            logger.error("Failed to open Preferences for ADE7953 configuration", TAG);
            // Set default configuration
            _configuration = Ade7953Configuration();
            _sampleTime = DEFAULT_SAMPLE_TIME;
            _updateSampleTime();
            return;
        }

        // Load configuration values (use defaults if not found)
        _sampleTime = preferences.getULong(CONFIG_SAMPLE_TIME_KEY, DEFAULT_SAMPLE_TIME);
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

        // Validate sample time
        if (_sampleTime < MINIMUM_SAMPLE_TIME) {
            logger.warning("Sample time %lu is below minimum %lu, using default", TAG, _sampleTime, MINIMUM_SAMPLE_TIME);
            _sampleTime = DEFAULT_SAMPLE_TIME;
        }

        // Apply the configuration
        _applyConfiguration(_configuration);
        _updateSampleTime();

        logger.debug("Successfully set configuration from Preferences", TAG);
    }

    void _saveConfigurationToPreferences() {
        logger.debug("Saving configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open Preferences for saving ADE7953 configuration", TAG);
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

        logger.debug("Successfully saved configuration to Preferences", TAG);
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

        logger.debug("Successfully applied configuration", TAG);
    }

    bool _validateJsonConfiguration(JsonDocument& jsonDocument, bool partial) {
        if (!jsonDocument.is<JsonObject>()) {
            logger.warning("JSON is not an object", TAG);
            return false;
        }

        // For partial updates, we don't require all fields to be present
        if (!partial) {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["aVGain"].is<int32_t>()) { logger.warning("aVGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aIGain"].is<int32_t>()) { logger.warning("aIGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bIGain"].is<int32_t>()) { logger.warning("bIGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aIRmsOs"].is<int32_t>()) { logger.warning("aIRmsOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bIRmsOs"].is<int32_t>()) { logger.warning("bIRmsOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aWGain"].is<int32_t>()) { logger.warning("aWGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bWGain"].is<int32_t>()) { logger.warning("bWGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aWattOs"].is<int32_t>()) { logger.warning("aWattOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bWattOs"].is<int32_t>()) { logger.warning("bWattOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aVarGain"].is<int32_t>()) { logger.warning("aVarGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bVarGain"].is<int32_t>()) { logger.warning("bVarGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aVarOs"].is<int32_t>()) { logger.warning("aVarOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bVarOs"].is<int32_t>()) { logger.warning("bVarOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aVaGain"].is<int32_t>()) { logger.warning("aVaGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bVaGain"].is<int32_t>()) { logger.warning("bVaGain is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["aVaOs"].is<int32_t>()) { logger.warning("aVaOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["bVaOs"].is<int32_t>()) { logger.warning("bVaOs is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["phCalA"].is<int32_t>()) { logger.warning("phCalA is missing or not int32_t", TAG); return false; }
            if (!jsonDocument["phCalB"].is<int32_t>()) { logger.warning("phCalB is missing or not int32_t", TAG); return false; }
        } else {
            // Partial validation - only validate fields that are present
            if (!jsonDocument["aVGain"].is<int32_t>()) { logger.warning("aVGain is not int32_t", TAG); return false; }
            if (!jsonDocument["aIGain"].is<int32_t>()) { logger.warning("aIGain is not int32_t", TAG); return false; }
            if (!jsonDocument["bIGain"].is<int32_t>()) { logger.warning("bIGain is not int32_t", TAG); return false; }
            if (!jsonDocument["aIRmsOs"].is<int32_t>()) { logger.warning("aIRmsOs is not int32_t", TAG); return false; }
            if (!jsonDocument["bIRmsOs"].is<int32_t>()) { logger.warning("bIRmsOs is not int32_t", TAG); return false; }
            if (!jsonDocument["aWGain"].is<int32_t>()) { logger.warning("aWGain is not int32_t", TAG); return false; }
            if (!jsonDocument["bWGain"].is<int32_t>()) { logger.warning("bWGain is not int32_t", TAG); return false; }
            if (!jsonDocument["aWattOs"].is<int32_t>()) { logger.warning("aWattOs is not int32_t", TAG); return false; }
            if (!jsonDocument["bWattOs"].is<int32_t>()) { logger.warning("bWattOs is not int32_t", TAG); return false; }
            if (!jsonDocument["aVarGain"].is<int32_t>()) { logger.warning("aVarGain is not int32_t", TAG); return false; }
            if (!jsonDocument["bVarGain"].is<int32_t>()) { logger.warning("bVarGain is not int32_t", TAG); return false; }
            if (!jsonDocument["aVarOs"].is<int32_t>()) { logger.warning("aVarOs is not int32_t", TAG); return false; }
            if (!jsonDocument["bVarOs"].is<int32_t>()) { logger.warning("bVarOs is not int32_t", TAG); return false; }
            if (!jsonDocument["aVaGain"].is<int32_t>()) { logger.warning("aVaGain is not int32_t", TAG); return false; }
            if (!jsonDocument["bVaGain"].is<int32_t>()) { logger.warning("bVaGain is not int32_t", TAG); return false; }
            if (!jsonDocument["aVaOs"].is<int32_t>()) { logger.warning("aVaOs is not int32_t", TAG); return false; }
            if (!jsonDocument["bVaOs"].is<int32_t>()) { logger.warning("bVaOs is not int32_t", TAG); return false; }
            if (!jsonDocument["phCalA"].is<int32_t>()) { logger.warning("phCalA is not int32_t", TAG); return false; }
            if (!jsonDocument["phCalB"].is<int32_t>()) { logger.warning("phCalB is not int32_t", TAG); return false; }
        }

        return true;
    }

    // Channel data management functions
    // =================================

    void _setChannelDataFromPreferences(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Invalid channel index: %lu", TAG, channelIndex);
            return;
        }

        logger.debug("Setting channel data from preferences for channel %lu", TAG, channelIndex);

        ChannelData channelData;
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) { // true = read-only
            logger.error("Failed to open Preferences for channel data", TAG);
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
        channelData.phase = static_cast<Phase>(preferences.getUChar(key, static_cast<uint8_t>(DEFAULT_CHANNEL_PHASE)));

        // CT Specification
        snprintf(key, sizeof(key), CHANNEL_CT_CURRENT_RATING_KEY, channelIndex);
        channelData.ctSpecification.currentRating = preferences.getFloat(key, DEFAULT_CT_CURRENT_RATING);

        snprintf(key, sizeof(key), CHANNEL_CT_VOLTAGE_OUTPUT_KEY, channelIndex);
        channelData.ctSpecification.voltageOutput = preferences.getFloat(key, DEFAULT_CT_VOLTAGE_OUTPUT);

        snprintf(key, sizeof(key), CHANNEL_CT_SCALING_FRACTION_KEY, channelIndex);
        channelData.ctSpecification.scalingFraction = preferences.getFloat(key, DEFAULT_CT_SCALING_FRACTION);

        preferences.end();

        setChannelData(channelData, channelIndex);

        logger.debug("Successfully set channel data from Preferences for channel %lu", TAG, channelIndex);
    }

    bool _saveChannelDataToPreferences(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.warning("Invalid channel index: %lu", TAG, channelIndex);
            return false;
        }

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) { // false = read-write
            logger.error("Failed to open Preferences for saving channel data", TAG);
            return false;
        }

        char key[PREFERENCES_KEY_BUFFER_SIZE];

        // Save channel data
        snprintf(key, sizeof(key), CHANNEL_ACTIVE_KEY, channelIndex);
        preferences.putBool(key, _channelData[channelIndex].active);

        snprintf(key, sizeof(key), CHANNEL_REVERSE_KEY, channelIndex);
        preferences.putBool(key, _channelData[channelIndex].reverse);

        snprintf(key, sizeof(key), CHANNEL_LABEL_KEY, channelIndex);
        preferences.putString(key, _channelData[channelIndex].label);

        snprintf(key, sizeof(key), CHANNEL_PHASE_KEY, channelIndex);
        preferences.putUChar(key, static_cast<uint8_t>(_channelData[channelIndex].phase));

        // CT Specification
        snprintf(key, sizeof(key), CHANNEL_CT_CURRENT_RATING_KEY, channelIndex);
        preferences.putFloat(key, _channelData[channelIndex].ctSpecification.currentRating);

        snprintf(key, sizeof(key), CHANNEL_CT_VOLTAGE_OUTPUT_KEY, channelIndex);
        preferences.putFloat(key, _channelData[channelIndex].ctSpecification.voltageOutput);

        snprintf(key, sizeof(key), CHANNEL_CT_SCALING_FRACTION_KEY, channelIndex);
        preferences.putFloat(key, _channelData[channelIndex].ctSpecification.scalingFraction);

        preferences.end();

        logger.debug("Successfully saved channel data to Preferences for channel %lu", TAG, channelIndex);
        return true;
    }

    bool _validateChannelDataJson(JsonDocument &jsonDocument, bool partial) {
        if (!jsonDocument.is<JsonObject>()) {
            logger.warning("JSON is not an object", TAG);
            return false;
        }

        // Index is always required for channel operations
        if (!jsonDocument["index"].is<uint32_t>()) {
            logger.warning("index is missing or not uint32_t", TAG);
            return false;
        }

        if (!isChannelValid(jsonDocument["index"].as<uint32_t>())) {
            logger.warning("Invalid channel index: %lu", TAG, jsonDocument["index"].as<uint32_t>());
            return false;
        }

        if (partial) {
            if (jsonDocument["active"].is<bool>()) return true;
            if (jsonDocument["reverse"].is<bool>()) return true;
            if (jsonDocument["label"].is<const char*>()) return true;
            if (jsonDocument["phase"].is<uint8_t>()) return true;

            // CT Specification validation for partial updates
            if (!jsonDocument["ctSpecification"].is<JsonObject>()) {
                if (jsonDocument["ctSpecification"]["currentRating"].is<uint32_t>()) return true;
                if (jsonDocument["ctSpecification"]["voltageOutput"].is<uint32_t>()) return true;
                if (jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) return true;   
            }

            logger.warning("No valid fields found for partial update", TAG);
            return false; // No valid fields found for partial update
        } else {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["active"].is<bool>()) { logger.warning("active is missing or not bool", TAG); return false; }
            if (!jsonDocument["reverse"].is<bool>()) { logger.warning("reverse is missing or not bool", TAG); return false; }
            if (!jsonDocument["label"].is<const char*>()) { logger.warning("label is missing or not string", TAG); return false; }
            if (!jsonDocument["phase"].is<uint8_t>()) { logger.warning("phase is missing or not uint8_t", TAG); return false; }

            // CT Specification validation
            if (!jsonDocument["ctSpecification"].is<JsonObject>()) { logger.warning("ctSpecification is missing or not object", TAG); return false; }
            if (!jsonDocument["ctSpecification"]["currentRating"].is<uint32_t>()) { logger.warning("ctSpecification.currentRating is missing or not uint32_t", TAG); return false; }
            if (!jsonDocument["ctSpecification"]["voltageOutput"].is<uint32_t>()) { logger.warning("ctSpecification.voltageOutput is missing or not uint32_t", TAG); return false; }
            if (!jsonDocument["ctSpecification"]["scalingFraction"].is<float>()) { logger.warning("ctSpecification.scalingFraction is missing or not float", TAG); return false; }

            return true; // All fields validated successfully
        }
    }

    void _updateChannelData(uint32_t channelIndex) {
        _calculateLsbValues(_channelData[channelIndex].ctSpecification);

        logger.debug("Successfully updated channel data for channel %lu", TAG, channelIndex);
    }

    void _calculateLsbValues(CtSpecification &ctSpec) {
        // TODO: Implement the actual LSB calculation math based on:
        // - ctSpec.currentRating (e.g., 30.0 for 30A)
        // - ctSpec.voltageOutput (e.g., 0.333 for 333mV)  
        // - ctSpec.scalingFraction (e.g., 0.05 for +5% adjustment)
        // - ADE7953 maximum values and voltage divider ratios
        
        // For now, set default values to prevent compilation errors
        ctSpec.aLsb = 1.0f;
        ctSpec.wLsb = 1.0f;
        ctSpec.varLsb = 1.0f;
        ctSpec.vaLsb = 1.0f;
        ctSpec.whLsb = 1.0f;
        ctSpec.varhLsb = 1.0f;
        ctSpec.vahLsb = 1.0f;

        logger.debug(
            "LSB values calculated for CT with current rating %.1f, voltage output %.3f and scaling fraction %.3f", 
            TAG, 
            ctSpec.currentRating, 
            ctSpec.voltageOutput, 
            ctSpec.scalingFraction
        );
    }

    // Energy
    // ======

    void _setEnergyFromPreferences(uint32_t channelIndex) {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ENERGY, true)) {
            logger.error("Failed to open preferences for reading", TAG);
            return;
        }

        char key[PREFERENCES_KEY_BUFFER_SIZE];
        
        // Only place in which we read the energy from preferences, and set the _energyValues initially

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

        preferences.end();
        logger.debug("Successfully read energy from preferences for channel %lu", TAG, channelIndex);
    }

    void _saveEnergyToPreferences(uint32_t channelIndex) {
        Preferences preferences;
        preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false);

        char key[PREFERENCES_KEY_BUFFER_SIZE];

        // Hereafter we optimize the flash writes by only saving if the value has changed significantly
        // Meter values are the real-time values, while energy values are the last saved values
        if (_meterValues[channelIndex].activeEnergyImported - _energyValues[channelIndex].activeEnergyImported > ENERGY_SAVE_THRESHOLD) {
            snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, channelIndex);
            preferences.putFloat(key, _meterValues[channelIndex].activeEnergyImported);
            _energyValues[channelIndex].activeEnergyImported = _meterValues[channelIndex].activeEnergyImported;
        }

        if (_meterValues[channelIndex].activeEnergyExported - _energyValues[channelIndex].activeEnergyExported > ENERGY_SAVE_THRESHOLD) {
            snprintf(key, sizeof(key), ENERGY_ACTIVE_EXP_KEY, channelIndex);
            preferences.putFloat(key, _meterValues[channelIndex].activeEnergyExported);
            _energyValues[channelIndex].activeEnergyExported = _meterValues[channelIndex].activeEnergyExported;
        }
        
        if (_meterValues[channelIndex].reactiveEnergyImported - _energyValues[channelIndex].reactiveEnergyImported > ENERGY_SAVE_THRESHOLD) {
            snprintf(key, sizeof(key), ENERGY_REACTIVE_IMP_KEY, channelIndex);
            preferences.putFloat(key, _meterValues[channelIndex].reactiveEnergyImported);
            _energyValues[channelIndex].reactiveEnergyImported = _meterValues[channelIndex].reactiveEnergyImported;
        }

        if (_meterValues[channelIndex].reactiveEnergyExported - _energyValues[channelIndex].reactiveEnergyExported > ENERGY_SAVE_THRESHOLD) {
            snprintf(key, sizeof(key), ENERGY_REACTIVE_EXP_KEY, channelIndex);
            preferences.putFloat(key, _meterValues[channelIndex].reactiveEnergyExported);
            _energyValues[channelIndex].reactiveEnergyExported = _meterValues[channelIndex].reactiveEnergyExported;
        }

        if (_meterValues[channelIndex].apparentEnergy - _energyValues[channelIndex].apparentEnergy > ENERGY_SAVE_THRESHOLD) {
            snprintf(key, sizeof(key), ENERGY_APPARENT_KEY, channelIndex);
            preferences.putFloat(key, _meterValues[channelIndex].apparentEnergy);
            _energyValues[channelIndex].apparentEnergy = _meterValues[channelIndex].apparentEnergy;
        }

        preferences.end();
        // Verbose because it is called for each channel
        logger.verbose("Successfully saved energy to preferences for channel %lu", TAG, channelIndex);
    }

    void _saveHourlyEnergyToCsv() {
        logger.debug("Saving hourly energy to CSV...", TAG);

        // Ensure time is synchronized before saving
        if (!CustomTime::isTimeSynched()) {
            logger.warning("Time not synchronized, skipping energy CSV save", TAG);
            return;
        }
        
        // Create UTC timestamp in ISO format (rounded to the hour)
        char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
        CustomTime::getTimestampIsoRoundedToHour(timestamp, sizeof(timestamp));
        
        // Create filename for today's CSV file (UTC date)
        char filename[NAME_BUFFER_SIZE];
        CustomTime::getDateIso(filename, sizeof(filename));

        // We must start the path with "/...."
        char filepath[NAME_BUFFER_SIZE + 2];
        snprintf(filepath, sizeof(filepath), "/%s", filename);
        
        // Check if file exists to determine if we need to write header
        bool fileExists = SPIFFS.exists(filepath);
        
        // Open file in append mode
        File file = SPIFFS.open(filepath, FILE_APPEND);
        if (!file) {
            logger.error("Failed to open CSV file %s for writing", TAG, filepath);
            return;
        }
        
        // Write header if this is a new file
        if (!fileExists) {
            file.println(DAILY_ENERGY_CSV_HEADER);
            logger.debug("Created new CSV file %s with header", TAG, filename);
        }
        
        // Write data for each active channel
        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
                logger.verbose("Saving hourly energy data for channel %d: %s", TAG, i, _channelData[i].label);

                // Only save data if energy values are above threshold
                if (_meterValues[i].activeEnergyImported > ENERGY_SAVE_THRESHOLD || 
                    _meterValues[i].activeEnergyExported > ENERGY_SAVE_THRESHOLD || 
                    _meterValues[i].reactiveEnergyImported > ENERGY_SAVE_THRESHOLD || 
                    _meterValues[i].reactiveEnergyExported > ENERGY_SAVE_THRESHOLD || 
                    _meterValues[i].apparentEnergy > ENERGY_SAVE_THRESHOLD) {
                    
                    file.print(timestamp);
                    file.print(",");
                    file.print(i);
                    file.print(",");
                    file.print(_channelData[i].label);
                    file.print(",");
                    file.print(_channelData[i].phase);
                    file.print(",");
                    file.print(_meterValues[i].activeEnergyImported, DAILY_ENERGY_CSV_DIGITS);
                    file.print(",");
                    file.print(_meterValues[i].activeEnergyExported, DAILY_ENERGY_CSV_DIGITS);
                    file.print(",");
                    file.print(_meterValues[i].reactiveEnergyImported, DAILY_ENERGY_CSV_DIGITS);
                    file.print(",");
                    file.print(_meterValues[i].reactiveEnergyExported, DAILY_ENERGY_CSV_DIGITS);
                    file.print(",");
                    file.println(_meterValues[i].apparentEnergy, DAILY_ENERGY_CSV_DIGITS);
                }
            }
        }
        
        file.close();
        logger.debug("Successfully saved hourly energy data to %s", TAG, filename);
    }

    void _saveEnergyComplete() {
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) _saveEnergyToPreferences(i);
        }
        _saveHourlyEnergyToCsv();

        logger.debug("Successfully saved complete energy data", TAG);
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
    bool _readMeterValues(int32_t channel, uint64_t linecycUnixTimeMillis) {
        uint64_t _millisRead = millis64();
        uint64_t _deltaMillis = _millisRead - _meterValues[channel].lastMillis;

        // // Ensure the reading is not being called too early if a previous valid reading exists
        // if (previousLastUnixTimeMilliseconds != 0) {
        //     uint64_t timeSinceLastRead = linecycUnixTimeMillis - previousLastUnixTimeMilliseconds;
            
        //     // Not useful anymore since the measurement timing (_sampleTime) is handled indipendently
        //     // by the linecyc of the ADE7953. The actual reading of the data may be not coordinated,
        //     // and that is ok
        //     // // Ensure the reading is not being called too early (should not happen anyway)
        //     // // This was introduced as in channel 0 it was noticed that sometimes two meter values
        //     // // were sent with 1 ms difference, where the second one had 0 active power (since most
        //     // // probably the next line cycle was not yet finished)
        //     // // We use the time multiplied by 0.8 to keep some headroom
        //     // if (timeSinceLastRead < _sampleTime * 0.8) {
        //     //     logger.warning("Reading too early for channel %d: %llu ms since last read, expected at least %0.0f ms", TAG, channel, timeSinceLastRead, _sampleTime * 0.8);
        //     //     _recordFailure();
        //     //     return false;
        //     // }
        //     _deltaMillis = timeSinceLastRead;
        // } else {
        //     // This is the first attempt to get a valid reading for this channel,
        //     // or previous attempts failed before setting the timestamp.
        //     // For energy accumulation of the first valid point, the period is _sampleTime.
        //     _deltaMillis = _sampleTime; 
        // }

        // We cannot put an higher limit here because if the channel happened to be disabled, then
        // enabled again, this would result in an infinite error.
        if (_meterValues[channel].lastMillis != 0 && _deltaMillis == 0) {
            logger.warning("%s (%lu): delta millis (%llu) is invalid. Discarding reading", TAG, _channelData[channel].label, channel, _deltaMillis);
            // _recordFailure();
            return false;
        }

        Ade7953Channel ade7953Channel = (channel == 0) ? Ade7953Channel::A : Ade7953Channel::B;

        float voltage = 0.0f;
        float current = 0.0f;
        float activePower = 0.0f;
        float reactivePower = 0.0f;
        float apparentPower = 0.0f;
        float powerFactor = 0.0f;
        float activeEnergy = 0.0f;
        float reactiveEnergy = 0.0f;
        float apparentEnergy = 0.0f;

        Phase basePhase = _channelData[0].phase;

        if (_channelData[channel].phase == basePhase) { // The phase is not necessarily PHASE_A, so use as reference the one of channel A
            
            // These are the three most important values to read
            activeEnergy = (_channelData[channel].ctSpecification.whLsb != 0) ?
                _readActiveEnergy(ade7953Channel) / _channelData[channel].ctSpecification.whLsb * (_channelData[channel].reverse ? -1 : 1) : 0.0f;
            reactiveEnergy = (_channelData[channel].ctSpecification.varhLsb != 0) ?
                _readReactiveEnergy(ade7953Channel) / _channelData[channel].ctSpecification.varhLsb * (_channelData[channel].reverse ? -1 : 1) : 0.0f;
            apparentEnergy = (_channelData[channel].ctSpecification.vahLsb != 0) ?
                _readApparentEnergy(ade7953Channel) / _channelData[channel].ctSpecification.vahLsb : 0.0f;
            
            // Since the voltage measurement is only one in any case, it makes sense to just re-use the same value
            // as channel 0 (sampled just before)
            if (channel == 0) {
                voltage = _readVoltageRms() * VOLT_PER_LSB;
                
                // Update grid frequency during channel 0 reading
                int32_t period = _readPeriod();
                float newGridFrequency = period > 0 ? GRID_FREQUENCY_CONVERSION_FACTOR / period : 0.0f;
                if (_validateGridFrequency(newGridFrequency)) _gridFrequency = newGridFrequency;
            } else {
                voltage = _meterValues[0].voltage;
            }
            
            // We use sample time instead of _deltaMillis because the energy readings are over whole line cycles (defined by the sample time)
            // Thus, extracting the power from energy divided by linecycle is more stable (does not care about ESP32 slowing down) and accurate
            activePower = _sampleTime > 0 ? activeEnergy / (_sampleTime / 1000.0 / 3600.0) : 0.0f; // W
            reactivePower = _sampleTime > 0 ? reactiveEnergy / (_sampleTime / 1000.0 / 3600.0) : 0.0f; // var
            apparentPower = _sampleTime > 0 ? apparentEnergy / (_sampleTime / 1000.0 / 3600.0) : 0.0f; // VA

            // It is faster and more consistent to compute the values rather than reading them from the ADE7953
            if (apparentPower == 0) powerFactor = 0.0f; // Avoid division by zero
            else powerFactor = activePower / apparentPower * (reactivePower >= 0 ? 1 : -1); // Apply sign as by datasheet (page 38)

            current = voltage > 0 ? apparentPower / voltage : 0.0f; // VA = V * A => A = VA / V | Always positive as apparent power is always positive
        } else {
            // TODO: understand if this can be improved using the energy registers
            // Assume everything is the same as channel 0 except the current
            // Important: here the reverse channel is not taken into account as the calculations would (probably) be wrong
            // It is easier just to ensure during installation that the CTs are installed correctly

            
            // Assume from channel 0
            voltage = _meterValues[0].voltage; // Assume the voltage is the same for all channels (medium assumption as difference usually is in the order of few volts, so less than 1%)
            
            // Read wrong power factor due to the phase shift
            float _powerFactorPhaseOne = _readPowerFactor(ade7953Channel)  * POWER_FACTOR_CONVERSION_FACTOR;

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

            if (_channelData[channel].phase == _getLaggingPhase(basePhase)) {
                powerFactor = cos(acos(_powerFactorPhaseOne) - (2 * PI / 3));
            } else if (_channelData[channel].phase == _getLeadingPhase(basePhase)) {
                // I cannot prove why, but I am SURE the minus is needed if the phase is leading
                powerFactor = - cos(acos(_powerFactorPhaseOne) + (2 * PI / 3));
            } else {
                logger.error("Invalid phase %d for channel %d", TAG, _channelData[channel].phase, channel);
                _recordFailure();
                return false;
            }

            // Read the current
            current = _channelData[channel].ctSpecification.aLsb != 0 ? _readCurrentRms(ade7953Channel) / _channelData[channel].ctSpecification.aLsb : 0.0f;

            // Compute power values
            activePower = current * voltage * abs(powerFactor);
            apparentPower = current * voltage;
            reactivePower = sqrt(pow(apparentPower, 2) - pow(activePower, 2)); // Small approximation leaving out distorted power
        }

        apparentPower = abs(apparentPower); // Apparent power must be positive

        // If the power factor is below a certain threshold, assume everything is 0 to avoid weird readings
        if (abs(powerFactor) < MINIMUM_POWER_FACTOR) {
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
            logger.debug(
                "%s (%d): Power factor %.3f is above %.3f, clamping it", 
                TAG,
                _channelData[channel].label, 
                channel, 
                powerFactor,
                MAXIMUM_POWER_FACTOR_CLAMP
            );
            powerFactor = (powerFactor > 0) ? VALIDATE_POWER_FACTOR_MAX : VALIDATE_POWER_FACTOR_MIN; // Keep the sign of the power factor
            activePower = apparentPower; // Recompute active power based on the clamped power factor
            reactivePower = 0.0f; // Small approximation leaving out distorted power
        }    
        
        
        // For channel 0, discard readings where active or apparent energy is exactly 0,
        // unless there have been at least 100 consecutive zero readings
        // TODO: understand if this method was actually required or not to filter invalid readings
        // if (channel == 0 && (activeEnergy == 0.0f || apparentEnergy == 0.0f)) {
        //     consecutiveZeroCount++;
            
        //     if (consecutiveZeroCount < MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE) {
                
        //         logger.debug("%s (%d): Zero energy reading on channel 0 discarded (count: %lu/%d)", 
        //             TAG, _channelData[channel].label,channel, consecutiveZeroCount, MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE);
        //         _recordFailure();
        //         return false;
        //     }
        // } else {
        //     // Reset counter for non-zero readings
        //     consecutiveZeroCount = 0;
        // }

        
        if (
            !_validateVoltage(voltage) || 
            !_validateCurrent(current) || 
            !_validatePower(activePower) || 
            !_validatePower(reactivePower) || 
            !_validatePower(apparentPower) || 
            !_validatePowerFactor(powerFactor)
        ) {
            
            logger.warning("%s (%d): Invalid reading (%.1fW, %.3fA, %.1fVAr, %.1fVA, %.3f)", 
                TAG, _channelData[channel].label, channel, activePower, current, reactivePower, apparentPower, powerFactor);
            _recordFailure();
            return false;
        }

        // This part makes no sense to use anymore as the current is computed from the apparent power and voltage
        // // Ensure the current * voltage is not too different from the apparent power (both in absolute and relative terms)
        // // Skip this check if apparent power is below 1 to avoid issues with low power readings
        // // Channel 0 does not have this problem since it has a dedicated ADC on the ADE7953, while the other channels
        // // use a multiplexer and some weird behavior can happen sometimes
        // 
        // if (_apparentPower >= 1.0 && _apparentEnergy != 0 && channel != 0 &&
        //     (abs(_current * _voltage - _apparentPower) > MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE || 
        //      abs(_current * _voltage - _apparentPower) / _apparentPower > MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE)) 
        // {
        //     logger.warning(
        //         "%s (%D): Current (%.3f * Voltage %.1f = %.1f) is too different from measured Apparent Power (%.1f). Discarding data point", 
        //         TAG,
        //         channelData[channel].label,
        //         channel, 
        //         _current, 
        //         _voltage,
        //         _current * _voltage, 
        //         _apparentPower
        //     );
        //     _recordFailure();
        //     return false;
        // }
        
        // Enough checks, now we can set the values
        _meterValues[channel].voltage = voltage;
        _meterValues[channel].current = current;
        _meterValues[channel].activePower = activePower;
        _meterValues[channel].reactivePower = reactivePower;
        _meterValues[channel].apparentPower = apparentPower;
        _meterValues[channel].powerFactor = powerFactor;

        // If the phase is not the phase of the main channel, set the energy not to 0 if the current
        // is above the threshold since we cannot use the ADE7593 no-load feature in this approximation
        if (_channelData[channel].phase != basePhase && current > MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD) {
            activeEnergy = 1;
            reactiveEnergy = 1;
            apparentEnergy = 1;
        }

        
        // Leverage the no-load feature of the ADE7953 to discard the noise
        // As such, when the energy read by the ADE7953 in the given linecycle is below
        // a certain threshold (set during setup), the read value is 0
        if (activeEnergy > 0) {
            _meterValues[channel].activeEnergyImported += abs(_meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0); // W * ms * s / 1000 ms * h / 3600 s = Wh
        } else if (activeEnergy < 0) {
            _meterValues[channel].activeEnergyExported += abs(_meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0); // W * ms * s / 1000 ms * h / 3600 s = Wh
        } else {
            logger.debug(
                "%s (%d): No load active energy reading. Setting active power and power factor to 0", 
                TAG,
                _channelData[channel].label,
                channel
            );
            _meterValues[channel].activePower = 0.0f;
            _meterValues[channel].powerFactor = 0.0f;
        }

        if (reactiveEnergy > 0) {
            _meterValues[channel].reactiveEnergyImported += abs(_meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0); // var * ms * s / 1000 ms * h / 3600 s = VArh
        } else if (reactiveEnergy < 0) {
            _meterValues[channel].reactiveEnergyExported += abs(_meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0); // var * ms * s / 1000 ms * h / 3600 s = VArh
        } else {
            logger.debug(
                "%s (%d): No load reactive energy reading. Setting reactive power to 0", 
                TAG,
                _channelData[channel].label,
                channel
            );
            _meterValues[channel].reactivePower = 0.0f;
        }

        if (apparentEnergy != 0) {
            _meterValues[channel].apparentEnergy += _meterValues[channel].apparentPower * _deltaMillis / 1000.0 / 3600.0; // VA * ms * s / 1000 ms * h / 3600 s = VAh
        } else {
            logger.debug(
                "%s (%d): No load apparent energy reading. Setting apparent power and current to 0", 
                TAG,
                _channelData[channel].label,
                channel
            );
            _meterValues[channel].current = 0.0f;
            _meterValues[channel].apparentPower = 0.0f;
        }

        // We actually set the timestamp of the channel (used for the energy calculations)
        // only if we actually reached the end. Otherwise it would mean the point had to be
        // discarded
        statistics.ade7953ReadingCount++;
        _meterValues[channel].lastMillis = _millisRead;
        _meterValues[channel].lastUnixTimeMilliseconds = linecycUnixTimeMillis;
        return true;
    }

    bool _processChannelReading(int32_t channel, uint64_t linecycUnix) {
        if (!_readMeterValues(channel, linecycUnix)) return false;

        _addMeterDataToPayload(channel);
        _printMeterValues(&_meterValues[channel], &_channelData[channel]);
        return true;
    }

    void _addMeterDataToPayload(int32_t channel) {
        if (!CustomTime::isTimeSynched()) return;
        if (!isChannelActive(channel) || !hasChannelValidMeasurements(channel)) {
            logger.warning("Channel %d is not active or has no valid measurements", TAG, channel);
            return;
        }

        Mqtt::pushMeter(
            PayloadMeter(
                channel,
                _meterValues[channel].lastUnixTimeMilliseconds,
                _meterValues[channel].activePower,
                _meterValues[channel].powerFactor
            )
        );
    }

    // ADE7953 register writing functions
    // ==================================

    void _setLinecyc(uint32_t linecyc) {
        uint32_t constrainedLinecyc = min(max(linecyc, ADE7953_MIN_LINECYC), ADE7953_MAX_LINECYC);
        writeRegister(LINECYC_16, BIT_16, constrainedLinecyc);
        logger.debug("Linecyc set to %d", TAG, constrainedLinecyc);
    }

    void _setPgaGain(int32_t pgaGain, Ade7953Channel channel, MeasurementType measurementType) {
        logger.debug("Setting PGA gain to %d on channel %d for measurement type %d", TAG, pgaGain, channel, measurementType);

        if (channel == Ade7953Channel::A) {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, BIT_8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IA_8, BIT_8, pgaGain);
                    break;
            }
        } else {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, BIT_8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IB_8, BIT_8, pgaGain);
                    break;
            }
        }
    }

    void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) writeRegister(PHCALA_16, BIT_16, phaseCalibration);
        else writeRegister(PHCALB_16, BIT_16, phaseCalibration);
        logger.debug("Phase calibration set to %d on channel %d", TAG, phaseCalibration, channel);
    }

    void _setGain(int32_t gain, Ade7953Channel channel, MeasurementType measurementType) {
        logger.debug("Setting gain to %ld on channel %d for measurement type %d", TAG, gain, channel, measurementType);

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
            }
        }
    }

    void _setOffset(int32_t offset, Ade7953Channel channel, MeasurementType measurementType) {
        logger.debug("Setting offset to %ld on channel %d for measurement type %d", TAG, offset, channel, measurementType);

        if (channel == Ade7953Channel::A) {
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
            }
        }
    }

    // Sample time management
    // ======================

    void _setSampleTimeFromPreferences() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            logger.error("Failed to open Preferences for loading sample time", TAG);
            return;
        }
        uint32_t sampleTime = preferences.getULong(CONFIG_SAMPLE_TIME_KEY, DEFAULT_SAMPLE_TIME);
        preferences.end();
        
        setSampleTime(sampleTime);

        logger.debug("Loaded sample time %d ms from preferences", TAG, sampleTime);
    }

    void _saveSampleTimeToPreferences() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open Preferences for saving sample time", TAG);
            return;
        }
        preferences.putULong(CONFIG_SAMPLE_TIME_KEY, _sampleTime);
        preferences.end();

        logger.debug("Saved sample time %d ms to preferences", TAG, _sampleTime);
    }

    void _updateSampleTime() {
        // Example: sample time at 1000 ms -> 1000 ms / 1000 * 50 * 2 = 100 linecyc, as linecyc is half of the cycle
        uint32_t linecyc = _sampleTime * CYCLES_PER_SECOND * 2 / 1000;
        _setLinecyc(linecyc);

        logger.info("Successfully updated sample time to %d ms (%d line cycles)", TAG, _sampleTime, linecyc);
    }

    // ADE7953 register reading functions
    // ==================================

    int32_t _readApparentPowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(AVA_32, BIT_32, true);
        else return readRegister(BVA_32, BIT_32, true);
    }

    int32_t _readActivePowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(AWATT_32, BIT_32, true);
        else return readRegister(BWATT_32, BIT_32, true);
    }

    int32_t _readReactivePowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(AVAR_32, BIT_32, true);
        else return readRegister(BVAR_32, BIT_32, true);
    }

    int32_t _readCurrentInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(IA_32, BIT_32, true);
        else return readRegister(IB_32, BIT_32, true);
    }

    int32_t _readVoltageInstantaneous() {
        return readRegister(V_32, BIT_32, true);
    }

    int32_t _readCurrentRms(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(IRMSA_32, BIT_32, false);
        else return readRegister(IRMSB_32, BIT_32, false);
    }

    int32_t _readVoltageRms() {
        return readRegister(VRMS_32, BIT_32, false);
    }

    int32_t _readActiveEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(AENERGYA_32, BIT_32, true);
        else return readRegister(AENERGYB_32, BIT_32, true);
    }

    int32_t _readReactiveEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(RENERGYA_32, BIT_32, true);
        else return readRegister(RENERGYB_32, BIT_32, true);
    }

    int32_t _readApparentEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(APENERGYA_32, BIT_32, true);
        else return readRegister(APENERGYB_32, BIT_32, true);
    }

    int32_t _readPowerFactor(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(PFA_16, BIT_16, true);
        else return readRegister(PFB_16, BIT_16, true);
    }

    int32_t _readAngle(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) return readRegister(ANGLE_A_16, BIT_16, true);
        else return readRegister(ANGLE_B_16, BIT_16, true);
    }

    int32_t _readPeriod() {
        return readRegister(PERIOD_16, BIT_16, false);
    }

    // Verification and validation functions
    // =====================================

    void _recordFailure() {
        
        logger.debug("Recording failure for ADE7953 communication", TAG);

        if (_failureCount == 0) {
            _firstFailureTime = millis64();
        }

        _failureCount++;
        statistics.ade7953ReadingCountFailure++;
        _checkForTooManyFailures();
    }

    void _checkForTooManyFailures() {
        
        if (millis64() - _firstFailureTime > ADE7953_FAILURE_RESET_TIMEOUT_MS && _failureCount > 0) {
            logger.debug("Failure timeout exceeded (%lu ms). Resetting failure count (reached %d)", TAG, millis64() - _firstFailureTime, _failureCount);
            
            _failureCount = 0;
            _firstFailureTime = 0;
            
            return;
        }

        if (_failureCount >= ADE7953_MAX_FAILURES_BEFORE_RESTART) {
            
            logger.fatal("Too many failures (%d) in ADE7953 communication or readings. Resetting device...", TAG, _failureCount);
            setRestartSystem(TAG, "Too many failures in ADE7953 communication or readings");

            // Reset the failure count and first failure time to avoid infinite loop of setting the restart
            _failureCount = 0;
            _firstFailureTime = 0;
        }
    }

    bool _verifyLastSpiCommunication(int32_t expectedAddress, int32_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite) 
    {        
        int32_t lastAddress = readRegister(LAST_ADD_16, 16, false, false);
        if (lastAddress != expectedAddress) {
            logger.warning(
                "Last address %ld (0x%04lX) (write: %d) does not match expected %ld (0x%04lX). Expected data %ld (0x%04lX)", 
                TAG, 
                lastAddress, lastAddress, 
                wasWrite, 
                expectedAddress, expectedAddress, 
                expectedData, expectedData);
            return false;
        }
        
        int32_t lastOp = readRegister(LAST_OP_8, 8, false, false);
        if (wasWrite && lastOp != LAST_OP_WRITE_VALUE) {
            logger.warning("Last operation was not a write (expected %d, got %ld)", TAG, LAST_OP_WRITE_VALUE, lastOp);
            return false;
        } else if (!wasWrite && lastOp != LAST_OP_READ_VALUE) {
            logger.warning("Last operation was not a read (expected %d, got %ld)", TAG, LAST_OP_READ_VALUE, lastOp);
            return false;
        }    
        
        // Select the appropriate LAST_RWDATA register based on the bit size
        int32_t dataRegister;
        int32_t dataRegisterBits;
        
        if (expectedBits == 8) {
            dataRegister = LAST_RWDATA_8;
            dataRegisterBits = 8;
        } else if (expectedBits == 16) {
            dataRegister = LAST_RWDATA_16;
            dataRegisterBits = 16;
        } else if (expectedBits == 24) {
            dataRegister = LAST_RWDATA_24;
            dataRegisterBits = 24;
        } else { // 32 bits or any other value defaults to 32-bit register
            dataRegister = LAST_RWDATA_32;
            dataRegisterBits = 32;
        }
        
        int32_t lastData = readRegister(dataRegister, dataRegisterBits, signedData, false);
        if (lastData != expectedData) {
            logger.warning("Last data %ld does not match expected %ld", TAG, lastData, expectedData);
            return false;
        }

        logger.verbose("Last communication verified successfully", TAG);
        return true;
    }

    bool _validateValue(float newValue, float min, float max) {
        if (newValue < min || newValue > max) {
            logger.warning("Value %f out of range (minimum: %f, maximum: %f)", TAG, newValue, min, max);
            return false;
        }
        return true;
    }

    bool _validateVoltage(float newValue) {
        return _validateValue(newValue, VALIDATE_VOLTAGE_MIN, VALIDATE_VOLTAGE_MAX);
    }

    bool _validateCurrent(float newValue) {
        return _validateValue(newValue, VALIDATE_CURRENT_MIN, VALIDATE_CURRENT_MAX);
    }

    bool _validatePower(float newValue) {
        return _validateValue(newValue, VALIDATE_POWER_MIN, VALIDATE_POWER_MAX);
    }

    bool _validatePowerFactor(float newValue) {
        return _validateValue(newValue, VALIDATE_POWER_FACTOR_MIN, VALIDATE_POWER_FACTOR_MAX);
    }

    bool _validateGridFrequency(float newValue) {
        return _validateValue(newValue, VALIDATE_GRID_FREQUENCY_MIN, VALIDATE_GRID_FREQUENCY_MAX);
    }

    // Utility functions
    // =================

    uint32_t _findNextActiveChannel(uint32_t currentChannel) {
        // This returns the next channel (except 0, which has to be always active) that is active
        for (uint32_t i = currentChannel + 1; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i) && i != 0) {
                return i;
            }
        }
        for (uint32_t i = 1; i < currentChannel; i++) {
            if (isChannelActive(i) && i != 0) {
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

    void _irqstataBitName(int32_t bit, char *buffer, size_t bufferSize) {
        switch (bit) {
            case IRQSTATA_AEHFA_BIT:       snprintf(buffer, bufferSize, "AEHFA");
            case IRQSTATA_VAREHFA_BIT:     snprintf(buffer, bufferSize, "VAREHFA");
            case IRQSTATA_VAEHFA_BIT:      snprintf(buffer, bufferSize, "VAEHFA");
            case IRQSTATA_AEOFA_BIT:       snprintf(buffer, bufferSize, "AEOFA");
            case IRQSTATA_VAREOFA_BIT:     snprintf(buffer, bufferSize, "VAREOFA");
            case IRQSTATA_VAEOFA_BIT:      snprintf(buffer, bufferSize, "VAEOFA");
            case IRQSTATA_AP_NOLOADA_BIT:  snprintf(buffer, bufferSize, "AP_NOLOADA");
            case IRQSTATA_VAR_NOLOADA_BIT: snprintf(buffer, bufferSize, "VAR_NOLOADA");
            case IRQSTATA_VA_NOLOADA_BIT:  snprintf(buffer, bufferSize, "VA_NOLOADA");
            case IRQSTATA_APSIGN_A_BIT:    snprintf(buffer, bufferSize, "APSIGN_A");
            case IRQSTATA_VARSIGN_A_BIT:   snprintf(buffer, bufferSize, "VARSIGN_A");
            case IRQSTATA_ZXTO_IA_BIT:     snprintf(buffer, bufferSize, "ZXTO_IA");
            case IRQSTATA_ZXIA_BIT:        snprintf(buffer, bufferSize, "ZXIA");
            case IRQSTATA_OIA_BIT:         snprintf(buffer, bufferSize, "OIA");
            case IRQSTATA_ZXTO_BIT:        snprintf(buffer, bufferSize, "ZXTO");
            case IRQSTATA_ZXV_BIT:         snprintf(buffer, bufferSize, "ZXV");
            case IRQSTATA_OV_BIT:          snprintf(buffer, bufferSize, "OV");
            case IRQSTATA_WSMP_BIT:        snprintf(buffer, bufferSize, "WSMP");
            case IRQSTATA_CYCEND_BIT:      snprintf(buffer, bufferSize, "CYCEND");
            case IRQSTATA_SAG_BIT:         snprintf(buffer, bufferSize, "SAG");
            case IRQSTATA_RESET_BIT:       snprintf(buffer, bufferSize, "RESET");
            case IRQSTATA_CRC_BIT:         snprintf(buffer, bufferSize, "CRC");
            default:                       snprintf(buffer, bufferSize, "UNKNOWN");
        }
    }

    void _printMeterValues(MeterValues* meterValues, ChannelData* channelData) {
        logger.debug(
            "%s (%D): %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh <- | %.3f Wh -> | %.3f VARh <- | %.3f VARh -> | %.3f VAh", 
            TAG, 
            channelData->label,
            channelData->index,
            meterValues->voltage, 
            meterValues->current, 
            meterValues->activePower, 
            meterValues->reactivePower, 
            meterValues->apparentPower, 
            meterValues->powerFactor, 
            meterValues->activeEnergyImported,
            meterValues->activeEnergyExported,
            meterValues->reactiveEnergyImported, 
            meterValues->reactiveEnergyExported, 
            meterValues->apparentEnergy
        );
    }
}