#include "ade7953.h"

static const char *TAG = "ade7953";

namespace Ade7953
{
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
    static volatile uint64_t _lastInterruptTime = 0;

    // Failure tracking
    static uint32_t _failureCount = 0;
    static uint64_t _firstFailureTime = 0;

    // Energy saving timestamps
    static uint64_t _lastMillisSaveEnergy = 0;
    static uint64_t _lastHourCsvSave = 0; // Track the last (integer) hour (from unix) when CSV was saved

    // Synchronization primitives
    static SemaphoreHandle_t _spiMutex = NULL; // To handle single SPI operations
    static SemaphoreHandle_t _spiOperationMutex = NULL; // To handle full SPI operations (like read with verification, which is 2 SPI operations)
    static SemaphoreHandle_t _ade7953InterruptSemaphore = NULL; // TODO: maybe notification is better here?
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

    // Hardware initialization and control
    static void _setHardwarePins(
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin
    );
    static void _initializeSpiMutexes();
    static void _reset();
    static bool _verifyCommunication();
    static void _setOptimumSettings();
    static void _setDefaultParameters();

    // System management
    void _cleanup();

    // Interrupt handling
    static void IRAM_ATTR _isrHandler();
    static Ade7953InterruptType _handleInterrupt();
    static void _reinitializeAfterInterrupt();
    static void _attachInterruptHandler();
    static void _detachInterruptHandler();
    static void _setupInterrupts();
    static void _checkInterruptTiming();
    static void _processCycendInterrupt(uint64_t linecycUnix);
    static void _handleCrcChangeInterrupt();

    // Task management
    static void _meterReadingTask(void* parameter);
    static void _startMeterReadingTask();
    static void _stopMeterReadingTask();

    static void _energySaveTask(void* parameter);
    static void _startEnergySaveTask();
    static void _stopEnergySaveTask();

    static void _hourlyCsvSaveTask(void* parameter);
    static void _startHourlyCsvSaveTask();
    static void _stopHourlyCsvSaveTask();

    // Configuration management
    static void _setConfigurationFromPreferences();
    static bool _saveConfigurationToPreferences();
    static void _applyConfiguration(const Ade7953Configuration &config);
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false);

    // Calibration management
    static void _setCalibrationValuesFromSpiffs();
    static void _jsonToCalibrationValues(JsonObject &jsonObject, CalibrationValues &calibrationValues);
    static bool _validateCalibrationValuesJson(JsonDocument &jsonDocument);

    // Channel data management
    static void _setChannelDataFromPreferences();
    static void _updateChannelData();
    static bool _saveChannelDataToPreferences();
    static bool _saveChannelToPreferences(uint32_t channelIndex);
    static bool _validateChannelDataJson(JsonDocument &jsonDocument);

    // Energy data management
    static void _setEnergyFromPreferences();
    static void _saveEnergyToPreferences();
    static void _saveDailyEnergyToSpiffs();
    static void _saveHourlyEnergyToCsv();
    static void _saveEnergyComplete();

    // Meter reading and processing
    static bool _readMeterValues(int32_t channel, uint64_t linecycUnixTime);
    static bool _processChannelReading(int32_t channel, uint64_t linecycUnix);
    static void _addMeterDataToPayload(int32_t channel, uint64_t linecycUnix);
    static uint32_t _findNextActiveChannel(uint32_t currentChannel);

    // ADE7953 register reading functions
    static int32_t _readApparentPowerInstantaneous(Ade7953Channel channel);
    static int32_t _readActivePowerInstantaneous(Ade7953Channel channel);
    static int32_t _readReactivePowerInstantaneous(Ade7953Channel channel);
    static int32_t _readCurrentInstantaneous(Ade7953Channel channel);
    static int32_t _readVoltageInstantaneous();
    static int32_t _readCurrentRms(Ade7953Channel channel);
    static int32_t _readVoltageRms();
    static int32_t _readActiveEnergy(Ade7953Channel channel);
    static int32_t _readReactiveEnergy(Ade7953Channel channel);
    static int32_t _readApparentEnergy(Ade7953Channel channel);
    static int32_t _readPowerFactor(Ade7953Channel channel);
    static int32_t _readAngle(Ade7953Channel channel);
    static int32_t _readPeriod();

    // ADE7953 register writing functions
    static void _setLinecyc(uint32_t linecyc);
    static void _setPgaGain(int32_t pgaGain, Ade7953Channel channel, MeasurementType measurementType);
    static void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel channel);
    static void _setGain(int32_t gain, Ade7953Channel channel, MeasurementType measurementType);
    static void _setOffset(int32_t offset, Ade7953Channel channel, MeasurementType measurementType);

    // Validation functions
    static bool _validateValue(float newValue, float min, float max);
    static bool _validateVoltage(float newValue);
    static bool _validateCurrent(float newValue);
    static bool _validatePower(float newValue);
    static bool _validatePowerFactor(float newValue);
    static bool _validateGridFrequency(float newValue);

    // Print functions
    void _printMeterValues(MeterValues* meterValues, ChannelData* channelData);

    // Utility functions
    static Phase _getLaggingPhase(Phase phase);
    static Phase _getLeadingPhase(Phase phase);
    static void _updateSampleTime();
    static bool _verifyLastCommunication(int32_t expectedAddress, int32_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite);
    static void _recordFailure();
    static void _checkForTooManyFailures();
    static void _irqstataBitName(int32_t bit, char *buffer, size_t bufferSize);

    bool begin(
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin
    ) {
        logger.debug("Initializing Ade7953", TAG);

        logger.debug("Creating ADE7953 instance", TAG);
        _initializeSpiMutexes();
        logger.debug("Successfully created ADE7953 instance", TAG);
      
        logger.debug("Setting up hardware pins...", TAG);
        _setHardwarePins(
            ssPin, sckPin, misoPin, mosiPin, resetPin, interruptPin
        );
        logger.debug("Successfully set up hardware pins", TAG);
     
        logger.debug("Verifying communication with ADE7953...", TAG);
        if (!_verifyCommunication()) {
            logger.error("Failed to communicate with ADE7953", TAG);
            return false;
        }
        logger.debug("Successfully initialized ADE7953", TAG);
                
        logger.debug("Setting optimum settings...", TAG);
        _setOptimumSettings();
        logger.debug("Successfully set optimum settings", TAG);
        
        logger.debug("Setting default parameters...", TAG);
        _setDefaultParameters();
        logger.debug("Successfully set default parameters", TAG);
        
        logger.debug("Setting configuration from Preferences...", TAG);
        _setConfigurationFromPreferences();
        logger.debug("Done setting configuration from Preferences", TAG);
  
        logger.debug("Reading channel data from Preferences...", TAG);
        _setChannelDataFromPreferences();
        logger.debug("Done reading channel data from Preferences", TAG);
        
        logger.debug("Reading calibration values from SPIFFS...", TAG);
        _setCalibrationValuesFromSpiffs();
        logger.debug("Done reading calibration values from SPIFFS", TAG);
        
        logger.debug("Reading energy from preferences...", TAG);
        _setEnergyFromPreferences();
        logger.debug("Done reading energy from preferences", TAG);    
        
        // Set it up only at the end to avoid premature interrupts
        
        logger.debug("Setting up interrupts...", TAG);
        _setupInterrupts();
        logger.debug("Successfully set up interrupts", TAG);

        logger.debug("Starting meter reading task...", TAG);
        _startMeterReadingTask();
        logger.debug("Meter reading task started", TAG);

        logger.debug("Starting energy save task...", TAG);
        _startEnergySaveTask();
        logger.debug("Energy save task started", TAG);

        logger.debug("Starting hourly CSV save task...", TAG);
        _startHourlyCsvSaveTask();
        logger.debug("Hourly CSV save task started", TAG);

        return true;
    }

    void stop() {
        logger.info("Stopping ADE7953...", TAG);
        
        // Clean up resources (where the data will also be saved)
        _cleanup();
        
        logger.info("ADE7953 stopped successfully", TAG);
    }

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

    void _setDefaultParameters()
    {
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::VOLTAGE);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::A, MeasurementType::CURRENT);
        _setPgaGain(DEFAULT_PGA_REGISTER, Ade7953Channel::B, MeasurementType::CURRENT);

        writeRegister(DISNOLOAD_8, 8, DEFAULT_DISNOLOAD_REGISTER);

        writeRegister(AP_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);
        writeRegister(VAR_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);
        writeRegister(VA_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);

        writeRegister(LCYCMODE_8, 8, DEFAULT_LCYCMODE_REGISTER);

        writeRegister(CONFIG_16, 16, DEFAULT_CONFIG_REGISTER);
    }

    /*
    * According to the datasheet, setting these registers is mandatory for optimal operation
    */
    void _setOptimumSettings()
    {
        writeRegister(UNLOCK_OPTIMUM_REGISTER, 8, UNLOCK_OPTIMUM_REGISTER_VALUE);
        writeRegister(Reserved_16, 16, DEFAULT_OPTIMUM_REGISTER);
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

    void _reset() {
        logger.debug("Resetting Ade7953", TAG);
        digitalWrite(_resetPin, LOW);
        delay(ADE7953_RESET_LOW_DURATION);
        digitalWrite(_resetPin, HIGH);
        delay(ADE7953_RESET_LOW_DURATION);
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
            } else {
                logger.warning("Failed to communicate with ADE7953 on attempt (%lu/%lu). Retrying in %lu ms", TAG, attempt, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS, ADE7953_VERIFY_COMMUNICATION_INTERVAL);
            }
        }

        logger.error("Failed to communicate with ADE7953 after %lu attempts", TAG, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        return false;
    }

    // Configuration
    // --------------------

    void _setConfigurationFromPreferences() {
        logger.debug("Setting configuration from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, true)) { // true = read-only
            logger.error("Failed to open Preferences for ADE7953 configuration", TAG);
            // Set default configuration
            _configuration = Ade7953Configuration();
            _sampleTime = DEFAULT_CONFIG_SAMPLE_TIME;
            _updateSampleTime();
            return;
        }

        // Load configuration values (use defaults if not found)
        _sampleTime = preferences.getULong(CONFIG_SAMPLE_TIME_KEY, DEFAULT_CONFIG_SAMPLE_TIME);
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
            _sampleTime = DEFAULT_CONFIG_SAMPLE_TIME;
        }

        // Apply the configuration
        _applyConfiguration(_configuration);
        _updateSampleTime();

        logger.debug("Successfully set configuration from Preferences", TAG);
    }

    // Thread-safe configuration management using standardized pattern
    void getConfiguration(Ade7953Configuration &config) {
        if (xSemaphoreTake(_configMutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            config = _configuration;
            xSemaphoreGive(_configMutex);
        } else {
            logger.error("Failed to acquire config mutex for getConfiguration", TAG);
            config = Ade7953Configuration(); // Return default if mutex acquisition fails
        } // TODO: fix this, better approach
    }

    bool setConfiguration(const Ade7953Configuration &config) {
        if (xSemaphoreTake(_configMutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            logger.error("Failed to acquire config mutex for setConfiguration", TAG);
            return false;
        }

        _configuration = config;
        _applyConfiguration(_configuration);
        
        bool result = _saveConfigurationToPreferences();
        if (!result) {
            logger.error("Failed to save configuration to Preferences", TAG);
        }
        
        xSemaphoreGive(_configMutex);
        return result;
    }

    bool configurationToJson(const Ade7953Configuration &config, JsonDocument &jsonDocument) {
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
        return true;
    }

    bool configurationFromJson(JsonDocument &jsonDocument, Ade7953Configuration &config, bool partial) {
        if (!_validateJsonConfiguration(jsonDocument, partial)) {
            return false;
        }

        if (partial) {
            // For partial updates, start with current config
            getConfiguration(config);
        } else {
            // For full updates, start with default
            config = Ade7953Configuration();
        }

        if (jsonDocument["aVGain"].is<int32_t>()) config.aVGain = jsonDocument["aVGain"].as<int32_t>();
        if (jsonDocument["aIGain"].is<int32_t>()) config.aIGain = jsonDocument["aIGain"].as<int32_t>();
        if (jsonDocument["bIGain"].is<int32_t>()) config.bIGain = jsonDocument["bIGain"].as<int32_t>();
        if (jsonDocument["aIRmsOs"].is<int32_t>()) config.aIRmsOs = jsonDocument["aIRmsOs"].as<int32_t>();
        if (jsonDocument["bIRmsOs"].is<int32_t>()) config.bIRmsOs = jsonDocument["bIRmsOs"].as<int32_t>();
        if (jsonDocument["aWGain"].is<int32_t>()) config.aWGain = jsonDocument["aWGain"].as<int32_t>();
        if (jsonDocument["bWGain"].is<int32_t>()) config.bWGain = jsonDocument["bWGain"].as<int32_t>();
        if (jsonDocument["bWGain"].is<int32_t>()) config.bWGain = jsonDocument["bWGain"].as<int32_t>();
        if (jsonDocument["aWattOs"].is<int32_t>()) config.aWattOs = jsonDocument["aWattOs"].as<int32_t>();
        if (jsonDocument["bWattOs"].is<int32_t>()) config.bWattOs = jsonDocument["bWattOs"].as<int32_t>();
        if (jsonDocument["aVarGain"].is<int32_t>()) config.aVarGain = jsonDocument["aVarGain"].as<int32_t>();
        if (jsonDocument["bVarGain"].is<int32_t>()) config.bVarGain = jsonDocument["bVarGain"].as<int32_t>();
        if (jsonDocument["aVarOs"].is<int32_t>()) config.aVarOs = jsonDocument["aVarOs"].as<int32_t>();
        if (jsonDocument["bVarOs"].is<int32_t>()) config.bVarOs = jsonDocument["bVarOs"].as<int32_t>();
        if (jsonDocument["aVarOs"].is<int32_t>()) config.aVarOs = jsonDocument["aVarOs"].as<int32_t>();
        if (jsonDocument["bVarOs"].is<int32_t>()) config.bVarOs = jsonDocument["bVarOs"].as<int32_t>();
        if (jsonDocument["aVaGain"].is<int32_t>()) config.aVaGain = jsonDocument["aVaGain"].as<int32_t>();
        if (jsonDocument["bVaGain"].is<int32_t>()) config.bVaGain = jsonDocument["bVaGain"].as<int32_t>();
        if (jsonDocument["aVaOs"].is<int32_t>()) config.aVaOs = jsonDocument["aVaOs"].as<int32_t>();
        if (jsonDocument["bVaOs"].is<int32_t>()) config.bVaOs = jsonDocument["bVaOs"].as<int32_t>();
        if (jsonDocument["phCalA"].is<int32_t>()) config.phCalA = jsonDocument["phCalA"].as<int32_t>();
        if (jsonDocument["phCalB"].is<int32_t>()) config.phCalB = jsonDocument["phCalB"].as<int32_t>();

        return true;
    }

    // Simple setSampleTime and getSampleTime functions
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

        // Save to preferences
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            preferences.putULong(CONFIG_SAMPLE_TIME_KEY, _sampleTime);
            preferences.end();
        }

        return true;
        return true;
    }

    void _applyConfiguration(const Ade7953Configuration &config) {
        logger.debug("Applying configuration...", TAG);

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

    bool _saveConfigurationToPreferences() {
        logger.debug("Saving configuration to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open Preferences for saving ADE7953 configuration", TAG);
            return false;
        }

        preferences.putULong(CONFIG_SAMPLE_TIME_KEY, _sampleTime);
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
        return true;
    }

    bool _validateJsonConfiguration(JsonDocument& jsonDocument, bool partial) {
        if (!jsonDocument.is<JsonObject>()) {
            logger.warning("JSON is not an object", TAG);
            return false;
        }

        // For partial updates, we don't require all fields to be present
        if (!partial) {
            // Full validation - all fields must be present and valid
            if (!jsonDocument["aVGain"].is<int32_t>()) {
                logger.warning("aVGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aIGain"].is<int32_t>()) {
                logger.warning("aIGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bIGain"].is<int32_t>()) {
                logger.warning("bIGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aIRmsOs"].is<int32_t>()) {
                logger.warning("aIRmsOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bIRmsOs"].is<int32_t>()) {
                logger.warning("bIRmsOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aWGain"].is<int32_t>()) {
                logger.warning("aWGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bWGain"].is<int32_t>()) {
                logger.warning("bWGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aWattOs"].is<int32_t>()) {
                logger.warning("aWattOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bWattOs"].is<int32_t>()) {
                logger.warning("bWattOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVarGain"].is<int32_t>()) {
                logger.warning("aVarGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVarGain"].is<int32_t>()) {
                logger.warning("bVarGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVarOs"].is<int32_t>()) {
                logger.warning("aVarOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVarOs"].is<int32_t>()) {
                logger.warning("bVarOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVaGain"].is<int32_t>()) {
                logger.warning("aVaGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVaGain"].is<int32_t>()) {
                logger.warning("bVaGain is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVaOs"].is<int32_t>()) {
                logger.warning("aVaOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVaOs"].is<int32_t>()) {
                logger.warning("bVaOs is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["phCalA"].is<int32_t>()) {
                logger.warning("phCalA is missing or not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["phCalB"].is<int32_t>()) {
                logger.warning("phCalB is missing or not int32_t", TAG);
                return false;
            }
        } else {
            // Partial validation - only validate fields that are present
            if (!jsonDocument["aVGain"].is<int32_t>()) {
                logger.warning("aVGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aIGain"].is<int32_t>()) {
                logger.warning("aIGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bIGain"].is<int32_t>()) {
                logger.warning("bIGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aIRmsOs"].is<int32_t>()) {
                logger.warning("aIRmsOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bIRmsOs"].is<int32_t>()) {
                logger.warning("bIRmsOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aWGain"].is<int32_t>()) {
                logger.warning("aWGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bWGain"].is<int32_t>()) {
                logger.warning("bWGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aWattOs"].is<int32_t>()) {
                logger.warning("aWattOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bWattOs"].is<int32_t>()) {
                logger.warning("bWattOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVarGain"].is<int32_t>()) {
                logger.warning("aVarGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVarGain"].is<int32_t>()) {
                logger.warning("bVarGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVarOs"].is<int32_t>()) {
                logger.warning("aVarOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVarOs"].is<int32_t>()) {
                logger.warning("bVarOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVaGain"].is<int32_t>()) {
                logger.warning("aVaGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVaGain"].is<int32_t>()) {
                logger.warning("bVaGain is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["aVaOs"].is<int32_t>()) {
                logger.warning("aVaOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["bVaOs"].is<int32_t>()) {
                logger.warning("bVaOs is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["phCalA"].is<int32_t>()) {
                logger.warning("phCalA is not int32_t", TAG);
                return false;
            }
            if (!jsonDocument["phCalB"].is<int32_t>()) {
                logger.warning("phCalB is not int32_t", TAG);
                return false;
            }
        }

        return true;
    }

    // Calibration values
    // --------------------

    void _setCalibrationValuesFromSpiffs() {
        logger.debug("Setting calibration values from SPIFFS", TAG);

        JsonDocument jsonDocument;
        deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

        if (!setCalibrationValues(jsonDocument)) {
            logger.error("Failed to set calibration values from SPIFFS. Keeping default ones", TAG);
            setDefaultCalibrationValues();
            return;
        }

        logger.debug("Successfully set calibration values from SPIFFS", TAG);
    }

    bool setCalibrationValues(JsonDocument &jsonDocument) {
        logger.debug("Setting new calibration values...", TAG);

        if (!_validateCalibrationValuesJson(jsonDocument)) {
            logger.warning("Invalid calibration JSON. Keeping previous calibration values", TAG);
            return false;
        }

        serializeJsonToSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

        _updateChannelData();

        logger.debug("Successfully set new calibration values", TAG);

        return true;
    }

    void setDefaultCalibrationValues() {
        logger.debug("Setting default calibration values", TAG);

        createDefaultCalibrationFile();
        
        JsonDocument jsonDocument;
        deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

        setCalibrationValues(jsonDocument);

        logger.debug("Successfully set default calibration values", TAG);
    }

    void _jsonToCalibrationValues(JsonObject &jsonObject, CalibrationValues &calibrationValues) {
        logger.verbose("Parsing JSON calibration values for label %s", TAG, calibrationValues.label);

        // The label is not parsed as it is already set in the channel data
        calibrationValues.aLsb = jsonObject["aLsb"].as<float>();
        calibrationValues.wLsb = jsonObject["wLsb"].as<float>();
        calibrationValues.varLsb = jsonObject["varLsb"].as<float>();
        calibrationValues.vaLsb = jsonObject["vaLsb"].as<float>();
        calibrationValues.whLsb = jsonObject["whLsb"].as<float>();
        calibrationValues.varhLsb = jsonObject["varhLsb"].as<float>();
        calibrationValues.vahLsb = jsonObject["vahLsb"].as<float>();
    }

    bool _validateCalibrationValuesJson(JsonDocument& jsonDocument) {
        if (!jsonDocument.is<JsonObject>()) {logger.warning("JSON is not an object", TAG); return false;}

        for (JsonPair kv : jsonDocument.as<JsonObject>()) {
            if (!kv.value().is<JsonObject>()) {logger.warning("JSON pair value is not an object", TAG); return false;}

            JsonObject calibrationObject = kv.value().as<JsonObject>();

            if (!calibrationObject["aLsb"].is<float>() && !calibrationObject["aLsb"].is<int32_t>()) {logger.warning("aLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["wLsb"].is<float>() && !calibrationObject["wLsb"].is<int32_t>()) {logger.warning("wLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["varLsb"].is<float>() && !calibrationObject["varLsb"].is<int32_t>()) {logger.warning("varLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["vaLsb"].is<float>() && !calibrationObject["vaLsb"].is<int32_t>()) {logger.warning("vaLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["whLsb"].is<float>() && !calibrationObject["whLsb"].is<int32_t>()) {logger.warning("whLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["varhLsb"].is<float>() && !calibrationObject["varhLsb"].is<int32_t>()) {logger.warning("varhLsb is not float or int32_t", TAG); return false;}
            if (!calibrationObject["vahLsb"].is<float>() && !calibrationObject["vahLsb"].is<int32_t>()) {logger.warning("vahLsb is or not float or int32_t", TAG); return false;}
        }

        return true;
    }

    // Data channel
    // --------------------

    bool isChannelActive(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.error("Channel index out of bounds: %lu", TAG, channelIndex);
            return false;
        }

        return _channelData[channelIndex].active;
    }

    void getChannelData(ChannelData &channelData, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.error("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        channelData = _channelData[channelIndex];
    }

    void getMeterValues(MeterValues &meterValues, uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.error("Channel index out of bounds: %lu", TAG, channelIndex);
            return;
        }

        meterValues = _meterValues[channelIndex];
    }

    void _setChannelDataFromPreferences() {
        logger.debug("Setting channel data from Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) { // true = read-only
            logger.error("Failed to open Preferences for channel data", TAG);
            setDefaultChannelData();
            return;
        }

        // Load data for each channel
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            char activeKey[32]; // TODO: use constants for buffer size
            char reverseKey[32];
            char labelKey[32];
            char phaseKey[32];
            char calibrationLabelKey[32];

            snprintf(activeKey, sizeof(activeKey), CHANNEL_ACTIVE_KEY, i);
            snprintf(reverseKey, sizeof(reverseKey), CHANNEL_REVERSE_KEY, i);
            snprintf(labelKey, sizeof(labelKey), CHANNEL_LABEL_KEY, i);
            snprintf(phaseKey, sizeof(phaseKey), CHANNEL_PHASE_KEY, i);
            snprintf(calibrationLabelKey, sizeof(calibrationLabelKey), CHANNEL_CALIBRATION_LABEL_KEY, i);

            // Set defaults
            _channelData[i].index = i;
            _channelData[i].active = (i == 0) ? DEFAULT_CHANNEL_0_ACTIVE : DEFAULT_CHANNEL_ACTIVE;
            _channelData[i].reverse = DEFAULT_CHANNEL_REVERSE;
            _channelData[i].phase = DEFAULT_CHANNEL_PHASE;

            // Set default labels
            if (i == 0) {
                snprintf(_channelData[i].label, sizeof(_channelData[i].label), DEFAULT_CHANNEL_0_LABEL);
                snprintf(_channelData[i].calibrationValues.label, sizeof(_channelData[i].calibrationValues.label), DEFAULT_CHANNEL_0_CALIBRATION_LABEL);
            } else {
                snprintf(_channelData[i].label, sizeof(_channelData[i].label), DEFAULT_CHANNEL_LABEL_FORMAT, i);
                snprintf(_channelData[i].calibrationValues.label, sizeof(_channelData[i].calibrationValues.label), DEFAULT_CHANNEL_CALIBRATION_LABEL);
            }

            // Load from preferences (use defaults if not found)
            if (preferences.isKey(activeKey)) {
                bool active = preferences.getBool(activeKey, _channelData[i].active);
                // Protect channel 0 from being disabled
                if (i == 0 && !active) {
                    logger.warning("Attempt to disable channel 0 blocked - channel 0 must remain active", TAG);
                    _channelData[i].active = true;
                } else {
                    _channelData[i].active = active;
                }
            }

            if (preferences.isKey(reverseKey)) {
                _channelData[i].reverse = preferences.getBool(reverseKey, _channelData[i].reverse);
            }

            if (preferences.isKey(phaseKey)) {
                _channelData[i].phase = static_cast<Phase>(preferences.getUChar(phaseKey, _channelData[i].phase));
            }

            if (preferences.isKey(labelKey)) {
                preferences.getString(labelKey, _channelData[i].label, sizeof(_channelData[i].label));
            }

            if (preferences.isKey(calibrationLabelKey)) {
                preferences.getString(calibrationLabelKey, _channelData[i].calibrationValues.label, sizeof(_channelData[i].calibrationValues.label));
            }
        }

        preferences.end();

        // Update calibration values
        _updateChannelData();

        logger.debug("Successfully set channel data from Preferences", TAG);
    }

    bool setChannelData(JsonDocument &jsonDocument) {
        logger.debug("Setting channel data from JSON...", TAG);

        if (!_validateChannelDataJson(jsonDocument)) {
            logger.warning("Invalid JSON channel data. Keeping previous channel data", TAG);
            return false;
        }

        // Parse JSON and update internal data
        for (JsonPair kv : jsonDocument.as<JsonObject>()) {
            logger.verbose(
                "Parsing JSON channel data %s | Active: %d | Reverse: %d | Label: %s | Phase: %d | Calibration Label: %s", 
                TAG, 
                kv.key().c_str(),
                kv.value()["active"].as<bool>(), 
                kv.value()["reverse"].as<bool>(), 
                kv.value()["label"].as<const char*>(), 
                kv.value()["phase"].as<Phase>(),
                kv.value()["calibrationLabel"].as<const char*>()
            );

            int32_t index = atoi(kv.key().c_str());

            // Check if index is within bounds
            if (!isChannelValid(index)) {
                logger.error("Index out of bounds: %lu", TAG, index);
                continue;
            }

            // Protect channel 0 from being disabled - it's the main voltage channel and must remain active
            if (index == 0 && !kv.value()["active"].as<bool>()) {
                logger.warning("Attempt to disable channel 0 blocked - channel 0 must remain active", TAG);
                // Force channel 0 to stay active
                _channelData[index].active = true;
            } else {
                _channelData[index].active = kv.value()["active"].as<bool>();
            }

            _channelData[index].index = index;
            _channelData[index].reverse = kv.value()["reverse"].as<bool>();
            snprintf(_channelData[index].label, sizeof(_channelData[index].label), "%s", kv.value()["label"].as<const char*>());
            _channelData[index].phase = kv.value()["phase"].as<Phase>();
            snprintf(_channelData[index].calibrationValues.label, sizeof(_channelData[index].calibrationValues.label), "%s", kv.value()["calibrationLabel"].as<const char*>());
        }
        logger.debug("Successfully parsed JSON channel data", TAG);

        // Add the calibration values to the channel data
        _updateChannelData();

        // Save to Preferences instead of SPIFFS
        _saveChannelDataToPreferences();

        Mqtt::requestChannelPublish();

        logger.debug("Successfully applied channel data changes", TAG);

        return true;
    }

    void setDefaultChannelData() {
        logger.debug("Setting default channel data...", TAG);

        // Initialize all channels with default values
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            _channelData[i].index = i;
            _channelData[i].active = (i == 0) ? DEFAULT_CHANNEL_0_ACTIVE : DEFAULT_CHANNEL_ACTIVE;
            _channelData[i].reverse = DEFAULT_CHANNEL_REVERSE;
            _channelData[i].phase = DEFAULT_CHANNEL_PHASE;

            // Set default labels
            if (i == 0) {
                snprintf(_channelData[i].label, sizeof(_channelData[i].label), DEFAULT_CHANNEL_0_LABEL);
                snprintf(_channelData[i].calibrationValues.label, sizeof(_channelData[i].calibrationValues.label), DEFAULT_CHANNEL_0_CALIBRATION_LABEL);
            } else {
                snprintf(_channelData[i].label, sizeof(_channelData[i].label), DEFAULT_CHANNEL_LABEL_FORMAT, i);
                snprintf(_channelData[i].calibrationValues.label, sizeof(_channelData[i].calibrationValues.label), DEFAULT_CHANNEL_CALIBRATION_LABEL);
            }
        }

        // Update calibration values
        _updateChannelData();

        // Save to Preferences
        _saveChannelDataToPreferences();

        logger.debug("Successfully initialized default channel data", TAG);
    }

    bool _saveChannelDataToPreferences() {
        logger.debug("Saving channel data to Preferences...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) {
            logger.error("Failed to open Preferences for saving channel data", TAG);
            return false;
        }

        // Save data for each channel
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            char activeKey[32]; // TODO: make constants the buffers
            char reverseKey[32];
            char labelKey[32];
            char phaseKey[32];
            char calibrationLabelKey[32];

            snprintf(activeKey, sizeof(activeKey), CHANNEL_ACTIVE_KEY, i);
            snprintf(reverseKey, sizeof(reverseKey), CHANNEL_REVERSE_KEY, i);
            snprintf(labelKey, sizeof(labelKey), CHANNEL_LABEL_KEY, i);
            snprintf(phaseKey, sizeof(phaseKey), CHANNEL_PHASE_KEY, i);
            snprintf(calibrationLabelKey, sizeof(calibrationLabelKey), CHANNEL_CALIBRATION_LABEL_KEY, i);

            preferences.putBool(activeKey, _channelData[i].active);
            preferences.putBool(reverseKey, _channelData[i].reverse);
            preferences.putUChar(phaseKey, static_cast<uint8_t>(_channelData[i].phase));
            preferences.putString(labelKey, _channelData[i].label);
            preferences.putString(calibrationLabelKey, _channelData[i].calibrationValues.label);
        }

        preferences.end();

        logger.debug("Successfully saved channel data to Preferences", TAG);
        return true;
    }

    bool _saveChannelToPreferences(uint32_t channelIndex) {
        if (!isChannelValid(channelIndex)) {
            logger.error("Channel index out of bounds: %u", TAG, channelIndex);
            return false;
        }

        logger.debug("Saving channel %u data to Preferences...", TAG, channelIndex);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) {
            logger.error("Failed to open Preferences for saving channel %u data", TAG, channelIndex);
            return false;
        }

        char activeKey[32];
        char reverseKey[32];
        char labelKey[32];
        char phaseKey[32];
        char calibrationLabelKey[32];

        snprintf(activeKey, sizeof(activeKey), CHANNEL_ACTIVE_KEY, channelIndex);
        snprintf(reverseKey, sizeof(reverseKey), CHANNEL_REVERSE_KEY, channelIndex);
        snprintf(labelKey, sizeof(labelKey), CHANNEL_LABEL_KEY, channelIndex);
        snprintf(phaseKey, sizeof(phaseKey), CHANNEL_PHASE_KEY, channelIndex);
        snprintf(calibrationLabelKey, sizeof(calibrationLabelKey), CHANNEL_CALIBRATION_LABEL_KEY, channelIndex);

        preferences.putBool(activeKey, _channelData[channelIndex].active);
        preferences.putBool(reverseKey, _channelData[channelIndex].reverse);
        preferences.putUChar(phaseKey, static_cast<uint8_t>(_channelData[channelIndex].phase));
        preferences.putString(labelKey, _channelData[channelIndex].label);
        preferences.putString(calibrationLabelKey, _channelData[channelIndex].calibrationValues.label);

        preferences.end();

        logger.debug("Successfully saved channel %u data to Preferences", TAG, channelIndex);
        return true;
    }

    void channelDataToJson(JsonDocument &jsonDocument) {
        logger.debug("Converting channel data to JSON...", TAG);

        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            jsonDocument[i]["active"] = _channelData[i].active;
            jsonDocument[i]["reverse"] = _channelData[i].reverse;
            jsonDocument[i]["label"] = _channelData[i].label;
            jsonDocument[i]["phase"] = _channelData[i].phase;
            jsonDocument[i]["calibrationLabel"] = _channelData[i].calibrationValues.label;
        }

        logger.debug("Successfully converted channel data to JSON", TAG);
    }

    bool setSingleChannelData(uint32_t channelIndex, bool active, bool reverse, const char* label, Phase phase, const char* calibrationLabel) {
        if (!isChannelValid(channelIndex)) {
            logger.error("Channel index out of bounds: %u", TAG, channelIndex);
            return false;
        }

        if (!label || !calibrationLabel) {
            logger.error("Label or calibration label is null for channel %u", TAG, channelIndex);
            return false;
        }

        logger.debug("Setting single channel data for channel %u", TAG, channelIndex);

        // Protect channel 0 from being disabled
        if (channelIndex == 0 && !active) {
            logger.warning("Attempt to disable channel 0 blocked - channel 0 must remain active", TAG);
            active = true;
        }

        // Validate phase
        if (phase != PHASE_1 && phase != PHASE_2 && phase != PHASE_3) {
            logger.error("Invalid phase %d for channel %u", TAG, phase, channelIndex);
            return false;
        }

        // Update internal data
        _channelData[channelIndex].active = active;
        _channelData[channelIndex].reverse = reverse;
        _channelData[channelIndex].phase = phase;
        snprintf(_channelData[channelIndex].label, sizeof(_channelData[channelIndex].label), "%s", label);
        snprintf(_channelData[channelIndex].calibrationValues.label, sizeof(_channelData[channelIndex].calibrationValues.label), "%s", calibrationLabel);

        // Update calibration values for this channel
        _updateChannelData();

        // Save to Preferences
        if (!_saveChannelToPreferences(channelIndex)) {
            logger.error("Failed to save channel %u data to Preferences", TAG, channelIndex);
            return false;
        }

        Mqtt::requestChannelPublish();

        logger.debug("Successfully set single channel data for channel %u", TAG, channelIndex);
        return true;
    }

    bool _validateChannelDataJson(JsonDocument &jsonDocument) {
        if (!jsonDocument.is<JsonObject>()) {logger.warning("JSON is not an object", TAG); return false;}

        for (JsonPair kv : jsonDocument.as<JsonObject>()) {
            if (!kv.value().is<JsonObject>()) {logger.warning("JSON pair value is not an object", TAG); return false;}

            int32_t index = atoi(kv.key().c_str());
            if (!isChannelValid(index)) {logger.warning("Index out of bounds: %d", TAG, index); return false;}

            JsonObject channelObject = kv.value().as<JsonObject>();

            if (!channelObject["active"].is<bool>()) {logger.warning("active is not bool", TAG); return false;}
            if (!channelObject["reverse"].is<bool>()) {logger.warning("reverse is not bool", TAG); return false;}
            if (!channelObject["label"].is<const char*>()) {logger.warning("label is not string", TAG); return false;}
            if (!channelObject["phase"].is<int32_t>()) {logger.warning("phase is not int32_t", TAG); return false;}
            if (kv.value()["phase"].as<int32_t>() != PHASE_1 && kv.value()["phase"].as<int32_t>() != PHASE_2 && kv.value()["phase"].as<int32_t>() != PHASE_3) {
                logger.warning("phase is not between 1 and 3", TAG); return false;
            }
            if (!channelObject["calibrationLabel"].is<const char*>()) {logger.warning("calibrationLabel is not string", TAG); return false;}
        }

        return true;
    }

    void _updateChannelData() {
        logger.debug("Updating data channel...", TAG);

        JsonDocument jsonDocument;
        deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

        if (jsonDocument.isNull()) {
            logger.error("Failed to read calibration values from SPIFFS. Keeping previous values", TAG);
            return;
        }
        
        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {        
            if (jsonDocument[_channelData[i].calibrationValues.label]) {
                // Extract the corresponding calibration values from the JSON
                JsonObject _jsonCalibrationValues = jsonDocument[_channelData[i].calibrationValues.label].as<JsonObject>();

                // Set the calibration values for the channel
                _jsonToCalibrationValues(_jsonCalibrationValues, _channelData[i].calibrationValues);
            } else {
                logger.warning(
                    "Calibration label %s for channel %d not found in calibration JSON", 
                    TAG, 
                    _channelData[i].calibrationValues.label, 
                    i
                );
            }
        }

        logger.debug("Successfully updated data channel", TAG);
    }

    void _updateSampleTime() {
        logger.debug("Updating sample time", TAG);

        uint32_t linecyc = _sampleTime * 50 * 2 / 1000; // 1 channel at 1000 ms: 1000 ms / 1000 * 50 * 2 = 100 linecyc, as linecyc is half of the cycle
        _setLinecyc(linecyc);

        logger.debug("Successfully updated sample time", TAG);
    }

    // This returns the next channel (except 0, which has to be always active) that is active
    uint32_t _findNextActiveChannel(uint32_t currentChannel) {
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


    // Meter values
    // --------------------

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
            activeEnergy = _readActiveEnergy(ade7953Channel) / _channelData[channel].calibrationValues.whLsb * (_channelData[channel].reverse ? -1 : 1);
            reactiveEnergy = _readReactiveEnergy(ade7953Channel) / _channelData[channel].calibrationValues.varhLsb * (_channelData[channel].reverse ? -1 : 1);
            apparentEnergy = _readApparentEnergy(ade7953Channel) / _channelData[channel].calibrationValues.vahLsb;        
            
            // Since the voltage measurement is only one in any case, it makes sense to just re-use the same value
            // as channel 0 (sampled just before)
            if (channel == 0) {
                voltage = _readVoltageRms() / VOLT_PER_LSB;
                
                // Update grid frequency during channel 0 reading
                float _newGridFrequency = GRID_FREQUENCY_CONVERSION_FACTOR / _readPeriod();
                if (_validateGridFrequency(_newGridFrequency)) _gridFrequency = _newGridFrequency;
            } else {
                voltage = _meterValues[0].voltage;
            }
            
            // We use sample time instead of _deltaMillis because the energy readings are over whole line cycles (defined by the sample time)
            // Thus, extracting the power from energy divided by linecycle is more stable (does not care about ESP32 slowing down) and accurate
            activePower = activeEnergy / (_sampleTime / 1000.0 / 3600.0); // W
            reactivePower = reactiveEnergy / (_sampleTime / 1000.0 / 3600.0); // var
            apparentPower = apparentEnergy / (_sampleTime / 1000.0 / 3600.0); // VA
            
            // It is faster and more consistent to compute the values rather than reading them from the ADE7953
            if (apparentPower == 0) powerFactor = 0.0f; // Avoid division by zero
            else powerFactor = activePower / apparentPower * (reactivePower >= 0 ? 1 : -1); // Apply sign as by datasheet (page 38)

            current = apparentPower / voltage; // VA = V * A => A = VA / V | Always positive as apparent power is always positive
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
            current = _readCurrentRms(ade7953Channel) / _channelData[channel].calibrationValues.aLsb;
            
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


    // Poor man's phase shift (doing with modulus didn't work properly,
    // and in any case the phases will always be at most 3)
    Phase _getLaggingPhase(Phase phase) {
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

    void singleMeterValuesToJson(JsonDocument &jsonDocument, uint32_t  channel) {
        JsonObject _jsonValues = jsonDocument.to<JsonObject>();

        // TODO: make this names with defines to ensure consistency
        _jsonValues["voltage"] = _meterValues[channel].voltage;
        _jsonValues["current"] = _meterValues[channel].current;
        _jsonValues["activePower"] = _meterValues[channel].activePower;
        _jsonValues["apparentPower"] = _meterValues[channel].apparentPower;
        _jsonValues["reactivePower"] = _meterValues[channel].reactivePower;
        _jsonValues["powerFactor"] = _meterValues[channel].powerFactor;
        _jsonValues["activeEnergyImported"] = _meterValues[channel].activeEnergyImported;
        _jsonValues["activeEnergyExported"] = _meterValues[channel].activeEnergyExported;
        _jsonValues["reactiveEnergyImported"] = _meterValues[channel].reactiveEnergyImported;
        _jsonValues["reactiveEnergyExported"] = _meterValues[channel].reactiveEnergyExported;
        _jsonValues["apparentEnergy"] = _meterValues[channel].apparentEnergy;
    }


    void fullMeterValuesToJson(JsonDocument &jsonDocument) {
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            if (isChannelActive(i)) {
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

    // Energy
    // --------------------

    void _setEnergyFromPreferences() { // TODO: fix with standard structure, such that if it fails, the default values get written to preferences
        logger.debug("Reading energy from preferences", TAG);
        
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ENERGY, true)) {
            logger.error("Failed to open preferences for reading", TAG);
            return;
        }

        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            char key[PREFERENCES_KEY_BUFFER_SIZE];
            
            // Only place in which we read the energy from preferences, and set the _energyValues initially

            snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, i);
            _meterValues[i].activeEnergyImported = preferences.getFloat(key, 0.0f);
            _energyValues[i].activeEnergyImported = _meterValues[i].activeEnergyImported;

            snprintf(key, sizeof(key), ENERGY_ACTIVE_EXP_KEY, i);
            _meterValues[i].activeEnergyExported = preferences.getFloat(key, 0.0f);
            _energyValues[i].activeEnergyExported = _meterValues[i].activeEnergyExported;

            snprintf(key, sizeof(key), ENERGY_REACTIVE_IMP_KEY, i);
            _meterValues[i].reactiveEnergyImported = preferences.getFloat(key, 0.0f);
            _energyValues[i].reactiveEnergyImported = _meterValues[i].reactiveEnergyImported;

            snprintf(key, sizeof(key), ENERGY_REACTIVE_EXP_KEY, i);
            _meterValues[i].reactiveEnergyExported = preferences.getFloat(key, 0.0f);
            _energyValues[i].reactiveEnergyExported = _meterValues[i].reactiveEnergyExported;

            snprintf(key, sizeof(key), ENERGY_APPARENT_KEY, i);
            _meterValues[i].apparentEnergy = preferences.getFloat(key, 0.0f);
            _energyValues[i].apparentEnergy = _meterValues[i].apparentEnergy;
        }

        preferences.end();
        logger.debug("Successfully read energy from preferences", TAG);
    }

    void _saveHourlyEnergyToCsv() {
        logger.debug("Saving hourly energy to CSV...", TAG);
        
        // Update the last hour save tracking
        if (CustomTime::isTimeSynched()) {
            uint64_t currentUnixTime = CustomTime::getUnixTime();
            _lastHourCsvSave = currentUnixTime / 3600; // Hour since epoch
        }
        
        _saveDailyEnergyToSpiffs();
        logger.debug("Successfully saved hourly energy to CSV", TAG);
    }

    void _saveEnergyComplete() {
        logger.debug("Saving complete energy data (preferences + CSV)...", TAG);
        _saveEnergyToPreferences();
        _saveDailyEnergyToSpiffs();
        logger.debug("Successfully saved complete energy data", TAG);
    }

    void _saveEnergyToPreferences() {
        logger.debug("Saving energy to preferences...", TAG);

        Preferences preferences;
        preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false);

        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            char key[PREFERENCES_KEY_BUFFER_SIZE];

            // Hereafter we optimize the flash writes by only saving if the value has changed significantly
            // Meter values are the real-time values, while energy values are the last saved values

            if (_meterValues[i].activeEnergyImported - _energyValues[i].activeEnergyImported > ENERGY_SAVE_THRESHOLD) {
                snprintf(key, sizeof(key), ENERGY_ACTIVE_IMP_KEY, i);
                preferences.putFloat(key, _meterValues[i].activeEnergyImported);
                _energyValues[i].activeEnergyImported = _meterValues[i].activeEnergyImported;
            }

            if (_meterValues[i].activeEnergyExported - _energyValues[i].activeEnergyExported > ENERGY_SAVE_THRESHOLD) {
                snprintf(key, sizeof(key), ENERGY_ACTIVE_EXP_KEY, i);
                preferences.putFloat(key, _meterValues[i].activeEnergyExported);
                _energyValues[i].activeEnergyExported = _meterValues[i].activeEnergyExported;
            }
            
            if (_meterValues[i].reactiveEnergyImported - _energyValues[i].reactiveEnergyImported > ENERGY_SAVE_THRESHOLD) {
                snprintf(key, sizeof(key), ENERGY_REACTIVE_IMP_KEY, i);
                preferences.putFloat(key, _meterValues[i].reactiveEnergyImported);
                _energyValues[i].reactiveEnergyImported = _meterValues[i].reactiveEnergyImported;
            }

            if (_meterValues[i].reactiveEnergyExported - _energyValues[i].reactiveEnergyExported > ENERGY_SAVE_THRESHOLD) {
                snprintf(key, sizeof(key), ENERGY_REACTIVE_EXP_KEY, i);
                preferences.putFloat(key, _meterValues[i].reactiveEnergyExported);
                _energyValues[i].reactiveEnergyExported = _meterValues[i].reactiveEnergyExported;
            }

            if (_meterValues[i].apparentEnergy - _energyValues[i].apparentEnergy > ENERGY_SAVE_THRESHOLD) {
                snprintf(key, sizeof(key), ENERGY_APPARENT_KEY, i);
                preferences.putFloat(key, _meterValues[i].apparentEnergy);
                _energyValues[i].apparentEnergy = _meterValues[i].apparentEnergy;
            }
        }

        preferences.end();
        logger.debug("Successfully saved energy to preferences", TAG);
    }

    void _saveDailyEnergyToSpiffs() { // TODO: duplicate?
        logger.debug("Saving hourly energy to CSV...", TAG);

        // Ensure time is synchronized before saving
        if (!CustomTime::isTimeSynched()) {
            logger.warning("Time not synchronized, skipping energy CSV save", TAG);
            return;
        }
        
        // Create UTC timestamp in ISO format (rounded to the hour)
        char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
        CustomTime::getTimestampIso(timestamp, sizeof(timestamp));
        
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

        _saveEnergyToPreferences();

        logger.info("Successfully reset energy values to 0", TAG);
    }

    bool setEnergyValues(
        uint32_t channel,
        float activeEnergyImported,
        float activeEnergyExported,
        float reactiveEnergyImported,
        float reactiveEnergyExported,
        float apparentEnergy
    ) {
        logger.warning("Setting energy values for channel %d", TAG, channel);

        if (channel < 0 || channel >= CHANNEL_COUNT) {
            logger.error("Invalid channel index %d", TAG, channel);
            return false;
        }

        if (activeEnergyImported < 0 || activeEnergyExported < 0 || 
            reactiveEnergyImported < 0 || reactiveEnergyExported < 0 || 
            apparentEnergy < 0) {
            logger.error("Energy values must be non-negative", TAG);
            return false;
        }

        _meterValues[channel].activeEnergyImported = activeEnergyImported;
        _meterValues[channel].activeEnergyExported = activeEnergyExported;
        _meterValues[channel].reactiveEnergyImported = reactiveEnergyImported;
        _meterValues[channel].reactiveEnergyExported = reactiveEnergyExported;
        _meterValues[channel].apparentEnergy = apparentEnergy;

        _saveEnergyToPreferences();
        logger.info("Successfully set energy values for channel %d", TAG, channel);
        return true;
    }

    bool setEnergyValues(JsonDocument &jsonDocument) {
        logger.warning("Setting selective energy values", TAG);

        if (!jsonDocument.is<JsonObject>()) {
            logger.error("Invalid JSON format for energy values", TAG);
            return false;
        }

        bool hasChanges = false;
        bool clearDailyEnergy = false;

        for (JsonPair kv : jsonDocument.as<JsonObject>()) {
            int32_t channel = atoi(kv.key().c_str());

            if (!isChannelValid(channel)) {
                logger.warning("Invalid channel index %d", TAG, channel);
                continue;
            }

            JsonObject energyData = kv.value().as<JsonObject>();
            
            if (energyData["activeEnergyImported"].is<float>()) {
                _meterValues[channel].activeEnergyImported = energyData["activeEnergyImported"];
                hasChanges = true;
                clearDailyEnergy = true;
            }
            
            if (energyData["activeEnergyExported"].is<float>()) {
                _meterValues[channel].activeEnergyExported = energyData["activeEnergyExported"];
                hasChanges = true;
                clearDailyEnergy = true;
            }
            
            if (energyData["reactiveEnergyImported"].is<float>()) {
                _meterValues[channel].reactiveEnergyImported = energyData["reactiveEnergyImported"];
                hasChanges = true;
                clearDailyEnergy = true;
            }
            
            if (energyData["reactiveEnergyExported"].is<float>()) {
                _meterValues[channel].reactiveEnergyExported = energyData["reactiveEnergyExported"];
                hasChanges = true;
                clearDailyEnergy = true;
            }
            
            if (energyData["apparentEnergy"].is<float>()) {
                _meterValues[channel].apparentEnergy = energyData["apparentEnergy"];
                hasChanges = true;
                clearDailyEnergy = true;
            }
        }

        if (hasChanges) {
            if (clearDailyEnergy) {
                logger.warning("Clearing daily energy CSV files due to energy value changes", TAG);
                
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
            }
            
            _saveEnergyToPreferences();
            logger.info("Successfully set selective energy values", TAG);
            return true;
        }

        logger.warning("No valid energy values found to set", TAG);
        return false;
    }

    // Others
    // --------------------

    void _setLinecyc(uint32_t linecyc) {
        linecyc = min(max(linecyc, ADE7953_MIN_LINECYC), ADE7953_MAX_LINECYC);

        logger.debug(
            "Setting linecyc to %d",
            TAG,
            linecyc
        );

        writeRegister(LINECYC_16, 16, int32_t(linecyc));
    }

    void _setPhaseCalibration(int32_t phaseCalibration, Ade7953Channel channel) {
        logger.debug(
            "Setting phase calibration to %d on channel %d",
            TAG,
            phaseCalibration,
            channel
        );

        if (channel == Ade7953Channel::A) {
            writeRegister(PHCALA_16, BIT_16, phaseCalibration);
        } else {
            writeRegister(PHCALB_16, BIT_16, phaseCalibration);
        }
    }

    void _setPgaGain(int32_t pgaGain, Ade7953Channel channel, MeasurementType measurementType) {
        logger.debug(
            "Setting PGA gain to %d on channel %d for measurement type %d",
            TAG,
            pgaGain,
            channel,
            measurementType
        );

        if (channel == Ade7953Channel::A) {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, 8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IA_8, 8, pgaGain);
                    break;
            }
        } else {
            switch (measurementType) {
                case MeasurementType::VOLTAGE:
                    writeRegister(PGA_V_8, 8, pgaGain);
                    break;
                case MeasurementType::CURRENT:
                    writeRegister(PGA_IB_8, 8, pgaGain);
                    break;
            }
        }
    }

    void _setGain(int32_t gain, Ade7953Channel channel, MeasurementType measurementType) {
        logger.debug(
            "Setting gain to %ld on channel %d for measurement type %d",
            TAG,
            gain,
            channel,
            measurementType
        );

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
        logger.debug(
            "Setting offset to %ld on channel %d for measurement type %d",
            TAG,
            offset,
            channel,
            measurementType
        );

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

    int32_t _readApparentPowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(AVA_32, BIT_32, true);} 
        else {return readRegister(BVA_32, BIT_32, true);}
    }

    /*
    Reads the "instantaneous" active power.

    "Instantaneous" because the active power is only defined as the dc component
    of the instantaneous power signal, which is V_RMS * I_RMS - V_RMS * I_RMS * cos(2*omega*t). 
    It is updated at 6.99 kHz.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The active power in LSB.
    */
    int32_t _readActivePowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(AWATT_32, BIT_32, true);} 
        else {return readRegister(BWATT_32, BIT_32, true);}
    }

    int32_t _readReactivePowerInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(AVAR_32, BIT_32, true);} 
        else {return readRegister(BVAR_32, BIT_32, true);}
    }


    /*
    Reads the actual instantaneous current. 

    This allows you see the actual sinusoidal waveform, so both positive and 
    negative values. At full scale (so 500 mV), the value returned is 9032007d.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The actual instantaneous current in LSB.
    */
    int32_t _readCurrentInstantaneous(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(IA_32, BIT_32, true);} 
        else {return readRegister(IB_32, BIT_32, true);}
    }

    /*
    Reads the actual instantaneous voltage. 

    This allows you 
    to see the actual sinusoidal waveform, so both positive
    and negative values. At full scale (so 500 mV), the value
    returned is 9032007d.

    @return The actual instantaneous voltage in LSB.
    */
    int32_t _readVoltageInstantaneous() {
        return readRegister(V_32, BIT_32, true);
    }

    /*
    Reads the current in RMS.

    This measurement is updated at 6.99 kHz and has a settling
    time of 200 ms. The value is in LSB.

    @param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
    @return The current in RMS in LSB.
    */
    int32_t _readCurrentRms(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(IRMSA_32, BIT_32, false);} 
        else {return readRegister(IRMSB_32, BIT_32, false);}
    }

    /*
    Reads the voltage in RMS.

    This measurement is updated at 6.99 kHz and has a settling
    time of 200 ms. The value is in LSB.

    @return The voltage in RMS in LSB.
    */
    int32_t _readVoltageRms() {
        return readRegister(VRMS_32, BIT_32, false);
    }

    int32_t _readActiveEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(AENERGYA_32, BIT_32, true);} 
        else {return readRegister(AENERGYB_32, BIT_32, true);}
    }

    int32_t _readReactiveEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(RENERGYA_32, BIT_32, true);} 
        else {return readRegister(RENERGYB_32, BIT_32, true);}
    }

    int32_t _readApparentEnergy(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(APENERGYA_32, BIT_32, true);} 
        else {return readRegister(APENERGYB_32, BIT_32, true);}
    }

    int32_t _readPowerFactor(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(PFA_16, BIT_16, true);} 
        else {return readRegister(PFB_16, BIT_16, true);}
    }

    int32_t _readAngle(Ade7953Channel channel) {
        if (channel == Ade7953Channel::A) {return readRegister(ANGLE_A_16, BIT_16, true);} 
        else {return readRegister(ANGLE_B_16, BIT_16, true);}
    }

    int32_t _readPeriod() {
        return readRegister(PERIOD_16, BIT_16, false);
    }

    /**
     * Reads the value from a register in the ADE7953 energy meter.
     * 
     * @param registerAddress The address of the register to read from. Expected range: 0 to 65535
     * @param numBits The number of bits to read from the register. Expected values: 8, 16, 24 or 32.
     * @param isSignedData Flag indicating whether the data is signed (true) or unsigned (false).
     * @param isVerificationRequired Flag indicating whether to verify the last communication.
     * @return The value read from the register.
     */
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

        xSemaphoreGive(_spiMutex);

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
            if (!_verifyLastCommunication(registerAddress, nBits, _long_response, signedData, false)) {
                logger.debug("Failed to verify last read communication for register %ld (0x%04lX). Value was %ld (0x%04lX)", TAG, registerAddress, registerAddress, _long_response, _long_response);
                _recordFailure();
                _long_response = INVALID_SPI_READ_WRITE; // Return an invalid value if verification fails
            }
            xSemaphoreGive(_spiOperationMutex);
        }

        return _long_response;
    }

    /**
     * Writes data to a register in the ADE7953 energy meter.
     * 
     * @param registerAddress The address of the register to write to. (16-bit value)
     * @param nBits The number of bits in the register. (8, 16, 24, or 32)
     * @param data The data to write to the register. (nBits-bit value)
     * @param isVerificationRequired Flag indicating whether to verify the last communication.
     */
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
            if (!_verifyLastCommunication(registerAddress, nBits, data, false, true)) {
                logger.warning("Failed to verify last write communication for register %ld", TAG, registerAddress);
                _recordFailure();
            }
            xSemaphoreGive(_spiOperationMutex);
        }
    }

    bool _verifyLastCommunication(int32_t expectedAddress, int32_t expectedBits, int32_t expectedData, bool signedData, bool wasWrite) {    
        
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

    float getGridFrequency() { return _gridFrequency; }


    // Helper functions
    // --------------------

    // Aggregate data

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

    // Interrupt handling methods
    // --------------------

    void _setupInterrupts() {
        logger.debug("Setting up ADE7953 interrupts...", TAG);
        
        // Enable only CYCEND interrupt for line cycle end detection (bit 18)
        writeRegister(IRQENA_32, 32, DEFAULT_IRQENA_REGISTER);
        
        // Clear any existing interrupt status
        readRegister(RSTIRQSTATA_32, 32, false);
        readRegister(RSTIRQSTATB_32, 32, false);

        logger.debug("ADE7953 interrupts enabled: CYCEND, RESET", TAG);
    }

    // Returns the string name of the IRQSTATA bit, or UNKNOWN if the bit is not recognized.
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

    Ade7953InterruptType _handleInterrupt() {
        
        int32_t statusA = readRegister(RSTIRQSTATA_32, 32, false);
        // No need to read for channel B

        // Very important: if we detected a reset or a CRC change in the configurations, 
        // we must reinitialize the device
        if (statusA & (1 << IRQSTATA_RESET_BIT)) {
            
            logger.warning("Reset interrupt detected. Device needs reinitialization", TAG);
            return Ade7953InterruptType::RESET;
        } else if (statusA & (1 << IRQSTATA_CRC_BIT)) {
            // TODO: how to handle this? if we change a setting in the ADE7953, we should expect a CRC change and avoid a loop.
            
            logger.warning("CRC changed detected. Device configuration may have changed", TAG);
            return Ade7953InterruptType::CRC_CHANGE;
            // Check for CYCEND interrupt (bit 18) - Line cycle end
        } else if (statusA & (1 << IRQSTATA_CYCEND_BIT)) {
            statistics.ade7953TotalHandledInterrupts++;
            logger.verbose("Line cycle end detected on Channel A", TAG);
            
            // Only for CYCEND interrupts, switch to next channel and set multiplexer
            _currentChannel = _findNextActiveChannel(_currentChannel);
            Multiplexer::setChannel(max(_currentChannel - (uint32_t)1, (uint32_t)0));

            return Ade7953InterruptType::CYCEND;
        } else {
            // Just log the unhandled status
            logger.warning("Unhandled ADE7953 interrupt status: 0x%08lX", TAG, statusA);
            return Ade7953InterruptType::OTHER;
        }
    }

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

    void pauseMeterReadingTask() {
        
        logger.info("Pausing ADE7953 meter reading task", TAG);

        _detachInterruptHandler();
        if (_meterReadingTaskHandle != NULL) {
            vTaskSuspend(_meterReadingTaskHandle);
        }
    }

    void resumeMeterReadingTask() {
        
        logger.info("Resuming ADE7953 meter reading task", TAG);
        
        if (_meterReadingTaskHandle != NULL) vTaskResume(_meterReadingTaskHandle);
        _attachInterruptHandler();
    }

    void _attachInterruptHandler() {
        // Detach only if already attached (avoid priting error)
        if (_meterReadingTaskHandle != NULL) {
            _detachInterruptHandler();
        }
        ::attachInterrupt(digitalPinToInterrupt(_interruptPin), _isrHandler, FALLING);
    }

    void _detachInterruptHandler() {
        ::detachInterrupt(digitalPinToInterrupt(_interruptPin));
    }

    void _stopEnergySaveTask() {
        stopTaskGracefully(&_energySaveTaskHandle, "ADE7953 energy save task");
    }

    void _stopHourlyCsvSaveTask() {
        stopTaskGracefully(&_hourlyCsvSaveTaskHandle, "ADE7953 hourly CSV save task");
    }

    void _isrHandler()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        statistics.ade7953TotalInterrupts++;
        _lastInterruptTime = millis64();

        // Signal the task to handle the interrupt - let the task determine the cause
        if (_ade7953InterruptSemaphore != NULL) {
            xSemaphoreGiveFromISR(_ade7953InterruptSemaphore, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
        }
    }

    void _checkInterruptTiming() {
        if (millis64() - _lastInterruptTime >= _sampleTime) {
            logger.warning("ADE7953 interrupt not handled within the sample time. We are %llu ms late (sample time is %u ms).", 
                        TAG, millis64() - _lastInterruptTime, _sampleTime);
        }
    }

    bool _processChannelReading(int32_t channel, uint64_t linecycUnix) {
        if (!_readMeterValues(channel, linecycUnix)) {
            return false;
        }
        
        _addMeterDataToPayload(channel, linecycUnix);
        _printMeterValues(&_meterValues[channel], &_channelData[channel]);
        return true;
    }

    void _addMeterDataToPayload(int32_t channel, uint64_t linecycUnix) {
        if (!CustomTime::isTimeSynched()) {
            return;
        }
        
        Mqtt::pushMeter(PayloadMeter(
            channel, 
            linecycUnix, 
            _meterValues[channel].activePower, 
            _meterValues[channel].powerFactor
        ));
    }

    void _processCycendInterrupt(uint64_t linecycUnix) {
        // Process current channel (if active)
        if (_currentChannel != INVALID_CHANNEL) {
            _processChannelReading(_currentChannel, linecycUnix);
        }
        
        // Always process channel 0
        _processChannelReading(0, linecycUnix);
    }

    void _handleCrcChangeInterrupt() {
        logger.warning("CRC change detected - this may indicate configuration changes", TAG);
        
        // For now, we'll just log this. In the future, you might want to:
        // 1. Check if this was an expected change (e.g., during calibration)
        // 2. Verify device configuration integrity
        // 3. Potentially trigger a reinitialization if unexpected
        
        // TODO: Implement smarter CRC change handling to avoid reinitialization loops
        // when making intentional configuration changes
    }

    void _meterReadingTask(void *parameter)
    {
        logger.debug("ADE7953 meter reading task started", TAG);
        
        _meterReadingTaskShouldRun = true;
        const TickType_t timeoutTicks = pdMS_TO_TICKS(ADE7953_INTERRUPT_TIMEOUT_MS + _sampleTime);
        
        while (_meterReadingTaskShouldRun)
        {
            // Wait for interrupt signal with timeout
            if (_ade7953InterruptSemaphore != NULL &&
                xSemaphoreTake(_ade7953InterruptSemaphore, timeoutTicks) == pdTRUE)
            {
                uint64_t linecycUnix = CustomTime::getUnixTimeMilliseconds();
                
                // Check if interrupt timing is within expected bounds
                _checkInterruptTiming();

                // Handle the interrupt and determine its type
                Ade7953InterruptType interruptType = _handleInterrupt();

                // Process based on interrupt type
                switch (interruptType)
                {
                case Ade7953InterruptType::CYCEND:
                    _processCycendInterrupt(linecycUnix);
                    break;

                case Ade7953InterruptType::RESET:
                    _reinitializeAfterInterrupt();
                    break;
                    
                case Ade7953InterruptType::CRC_CHANGE:
                    _handleCrcChangeInterrupt();
                    break;

                case Ade7953InterruptType::OTHER:
                case Ade7953InterruptType::NONE:
                default:
                    // Already logged in _handleInterrupt(), just continue
                    break;
                }
            }
            else
            {
                // Timeout or semaphore error - check for stop notification
                if (_ade7953InterruptSemaphore == NULL) {
                    logger.debug("Interrupt semaphore is NULL, task exiting", TAG);
                    _meterReadingTaskShouldRun = false;
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

    void _energySaveTask(void* parameter) {
        logger.debug("ADE7953 energy save task started", TAG);

        _energySaveTaskShouldRun = true;
        while (_energySaveTaskShouldRun) {
            _saveEnergyToPreferences();
            
            // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
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
            if (_hourlyCsvSaveTaskShouldRun) _saveHourlyEnergyToCsv();
        }

        logger.debug("ADE7953 hourly CSV save task stopping", TAG);
        _hourlyCsvSaveTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    void _reinitializeAfterInterrupt() {
        logger.warning("Reinitializing ADE7953 after reset/CRC change interrupt", TAG);
        
        // Note: We don't pause/resume meter reading task here because this method
        // is called from within the meter reading task itself. Pausing would cause a deadlock.
        
        // Detach interrupt handler temporarily to prevent conflicts
        _detachInterruptHandler();
        
        // Reinitialize the device settings (but not the hardware pins)
        logger.debug("Setting optimum settings...", TAG);
        _setOptimumSettings();
        
        logger.debug("Setting default parameters...", TAG);
        _setDefaultParameters();
        
        logger.debug("Setting configuration from Preferences...", TAG);
        _setConfigurationFromPreferences();
        
        logger.debug("Reading calibration values from SPIFFS...", TAG);
        _setCalibrationValuesFromSpiffs();
        
        logger.debug("Setting up interrupts...", TAG);
        _setupInterrupts();
        
        logger.info("ADE7953 reinitialization completed successfully", TAG);
        
        // Reattach interrupt handler
        _attachInterruptHandler();
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