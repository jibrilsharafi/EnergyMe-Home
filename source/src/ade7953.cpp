#include "ade7953.h"

static const char *TAG = "ade7953";

// Define the static instance pointer
Ade7953* Ade7953::_instance = nullptr;

Ade7953::Ade7953(
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
    CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter) : 
                            _ssPin(ssPin),
                            _sckPin(sckPin),
                            _misoPin(misoPin),
                            _mosiPin(mosiPin),
                            _resetPin(resetPin),
                            _interruptPin(interruptPin),                            
                            _logger(logger),
                            _mainFlags(mainFlags),
                            _customTime(customTime),
                            _led(led),
                            _multiplexer(multiplexer),
                            _payloadMeter(payloadMeter) {}

bool Ade7953::begin() {
    _logger.debug("Initializing Ade7953", TAG);

    TRACE();
    if (_instance != nullptr) {
        _logger.error("Ade7953 instance already exists", TAG);
        return false;
    }

    TRACE();
    _logger.debug("Creating Ade7953 instance", TAG);
    _initializeSpiMutexes();
    _logger.debug("Successfully created Ade7953 instance", TAG);

    TRACE();
    _logger.debug("Setting up hardware pins...", TAG);
    _setHardwarePins();
    _logger.debug("Successfully set up hardware pins", TAG);

    TRACE();
    _logger.debug("Verifying communication with Ade7953...", TAG);
    if (!_verifyCommunication()) {
        _logger.error("Failed to communicate with Ade7953", TAG);
        return false;
    }
    _logger.debug("Successfully initialized Ade7953", TAG);
    
    TRACE();
    _logger.debug("Setting optimum settings...", TAG);
    _setOptimumSettings();
    _logger.debug("Successfully set optimum settings", TAG);

    TRACE();
    _logger.debug("Setting default parameters...", TAG);
    _setDefaultParameters();
    _logger.debug("Successfully set default parameters", TAG);

    TRACE();
    _logger.debug("Setting configuration from SPIFFS...", TAG);
    _setConfigurationFromSpiffs();
    _logger.debug("Done setting configuration from SPIFFS", TAG);

    TRACE();
    _logger.debug("Reading channel data from SPIFFS...", TAG);
    _setChannelDataFromSpiffs();
    _logger.debug("Done reading channel data from SPIFFS", TAG);

    TRACE();
    _logger.debug("Reading calibration values from SPIFFS...", TAG);
    _setCalibrationValuesFromSpiffs();
    _logger.debug("Done reading calibration values from SPIFFS", TAG);

    TRACE();
    _logger.debug("Reading energy from SPIFFS...", TAG);
    _setEnergyFromSpiffs();
    _logger.debug("Done reading energy from SPIFFS", TAG);    
    
    // Set it up only at the end to avoid premature interrupts
    TRACE();
    _logger.debug("Setting up interrupts...", TAG);
    _setupInterrupts();
    _logger.debug("Successfully set up interrupts", TAG);

    TRACE();
    _logger.debug("Starting meter reading task...", TAG);
    _startMeterReadingTask();
    _logger.debug("Meter reading task started", TAG);

    return true;
}

void Ade7953::_initializeSpiMutexes()
{
    TRACE();
    _spiMutex = xSemaphoreCreateMutex();
    if (_spiMutex == NULL)
    {
        _logger.error("Failed to create SPI mutex", TAG);
        return;
    }
    _logger.debug("SPI mutex created successfully", TAG);

    TRACE();
    _spiOperationMutex = xSemaphoreCreateMutex();
    if (_spiOperationMutex == NULL)
    {
        _logger.error("Failed to create SPI operation mutex", TAG);
        vSemaphoreDelete(_spiMutex);
        _spiMutex = NULL;
        return;
    }
    _logger.debug("SPI operation mutex created successfully", TAG);

    TRACE();
    _payloadMeterMutex = xSemaphoreCreateMutex();
    if (_payloadMeterMutex == NULL)
    {
        _logger.error("Failed to create payload meter mutex", TAG);
        vSemaphoreDelete(_spiMutex);
        vSemaphoreDelete(_spiOperationMutex);
        _spiMutex = NULL;
        _spiOperationMutex = NULL;
        return;
    }
    _logger.debug("Payload meter mutex created successfully", TAG);
}

void Ade7953::_setHardwarePins() {
    _logger.debug("Setting hardware pins...", TAG);

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

    _logger.debug("Successfully set hardware pins", TAG);
}

void Ade7953::_setDefaultParameters()
{
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, VOLTAGE);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, CURRENT);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_B, CURRENT);

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
void Ade7953::_setOptimumSettings()
{
    writeRegister(UNLOCK_OPTIMUM_REGISTER, 8, UNLOCK_OPTIMUM_REGISTER_VALUE);
    writeRegister(Reserved_16, 16, DEFAULT_OPTIMUM_REGISTER);
}

void Ade7953::loop() {
    if (
        millis() - _lastMillisSaveEnergy > SAVE_ENERGY_INTERVAL || 
        restartConfiguration.isRequired
    ) {
        _lastMillisSaveEnergy = millis();
        saveEnergy();
    }

    // If restart required, clean up
    if (restartConfiguration.isRequired) {
        _logger.info("Restart required. Cleaning up Ade7953 resources", TAG);
        cleanup();
    }
}

void Ade7953::cleanup() {
    _logger.debug("Cleaning up Ade7953 resources", TAG);
    
    // Stop meter reading task and interrupt handling first
    _stopMeterReadingTask();
    
    // Clean up SPI mutex
    if (_spiMutex != NULL) {
        vSemaphoreDelete(_spiMutex);
        _spiMutex = NULL;
        _logger.debug("SPI mutex deleted", TAG);
    }

    if (_spiOperationMutex != NULL) {
        vSemaphoreDelete(_spiOperationMutex);
        _spiOperationMutex = NULL;
        _logger.debug("SPI operation mutex deleted", TAG);
    }

    if (_payloadMeterMutex != NULL) {
        vSemaphoreDelete(_payloadMeterMutex);
        _payloadMeterMutex = NULL;
        _logger.debug("Payload meter mutex deleted", TAG);
    }
}

void Ade7953::_reset() {
    _logger.debug("Resetting Ade7953", TAG);
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
bool Ade7953::_verifyCommunication() {
    _logger.debug("Verifying communication with Ade7953...", TAG);
    
    int _attempt = 0;
    bool _success = false;
    unsigned long _lastMillisAttempt = 0;

    unsigned int _loops = 0;
    while (_attempt < ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS && !_success && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;
        if (millis() - _lastMillisAttempt < ADE7953_VERIFY_COMMUNICATION_INTERVAL) {
            continue;
        }

        _logger.debug("Attempt (%d/%d) to communicate with ADE7953", TAG, _attempt+1, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        
        _reset();
        _attempt++;
        _lastMillisAttempt = millis();

        if ((readRegister(AP_NOLOAD_32, 32, false)) == DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
            _logger.debug("Communication successful with ADE7953", TAG);
            return true;
        } else {
            _logger.warning("Failed to communicate with ADE7953 on _attempt (%d/%d). Retrying in %d ms", TAG, _attempt, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS, ADE7953_VERIFY_COMMUNICATION_INTERVAL);
        }
    }

    _logger.error("Failed to communicate with ADE7953 after %d attempts", TAG, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
    return false;
}

// Configuration
// --------------------

void Ade7953::_setConfigurationFromSpiffs() {
    _logger.debug("Setting configuration from SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    if (!setConfiguration(_jsonDocument)) {
        _logger.error("Failed to set configuration from SPIFFS. Keeping default one", TAG);
        setDefaultConfiguration();
        return;
    }

    _logger.debug("Successfully set configuration from SPIFFS", TAG);
}

bool Ade7953::setConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Setting configuration...", TAG);

    if (!_validateConfigurationJson(jsonDocument)) {
        _logger.warning("Invalid configuration JSON. Keeping previous configuration", TAG);
        return false;
    }
    
    if (!serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, jsonDocument)) {
        _logger.error("Failed to save configuration to SPIFFS. Keeping previous configuration", TAG);
        return false;
    } else {
        _applyConfiguration(jsonDocument);
        _logger.debug("Successfully saved configuration to SPIFFS", TAG);
        return true;
    }
}

void Ade7953::setDefaultConfiguration() {
    _logger.debug("Setting default configuration...", TAG);

    createDefaultAde7953ConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    setConfiguration(_jsonDocument);

    _logger.debug("Default configuration set", TAG);
}

void Ade7953::_applyConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Applying configuration...", TAG);

    _sampleTime = jsonDocument["sampleTime"].as<unsigned long>();
    _updateSampleTime();
    
    _setGain(jsonDocument["aVGain"].as<long>(), CHANNEL_A, VOLTAGE);
    // Channel B voltage gain should not be set as by datasheet

    _setGain(jsonDocument["aIGain"].as<long>(), CHANNEL_A, CURRENT);
    _setGain(jsonDocument["bIGain"].as<long>(), CHANNEL_B, CURRENT);

    _setOffset(jsonDocument["aIRmsOs"].as<long>(), CHANNEL_A, CURRENT);
    _setOffset(jsonDocument["bIRmsOs"].as<long>(), CHANNEL_B, CURRENT);

    _setGain(jsonDocument["aWGain"].as<long>(), CHANNEL_A, ACTIVE_POWER);
    _setGain(jsonDocument["bWGain"].as<long>(), CHANNEL_B, ACTIVE_POWER);

    _setOffset(jsonDocument["aWattOs"].as<long>(), CHANNEL_A, ACTIVE_POWER);
    _setOffset(jsonDocument["bWattOs"].as<long>(), CHANNEL_B, ACTIVE_POWER);

    _setGain(jsonDocument["aVarGain"].as<long>(), CHANNEL_A, REACTIVE_POWER);
    _setGain(jsonDocument["bVarGain"].as<long>(), CHANNEL_B, REACTIVE_POWER);

    _setOffset(jsonDocument["aVarOs"].as<long>(), CHANNEL_A, REACTIVE_POWER);
    _setOffset(jsonDocument["bVarOs"].as<long>(), CHANNEL_B, ACTIVE_POWER);

    _setGain(jsonDocument["aVaGain"].as<long>(), CHANNEL_A, APPARENT_POWER);
    _setGain(jsonDocument["bVaGain"].as<long>(), CHANNEL_B, APPARENT_POWER);

    _setOffset(jsonDocument["aVaOs"].as<long>(), CHANNEL_A, APPARENT_POWER);
    _setOffset(jsonDocument["bVaOs"].as<long>(), CHANNEL_B, APPARENT_POWER);

    _setPhaseCalibration(jsonDocument["phCalA"].as<long>(), CHANNEL_A);
    _setPhaseCalibration(jsonDocument["phCalB"].as<long>(), CHANNEL_B);

    _logger.debug("Successfully applied configuration", TAG);
}

bool Ade7953::_validateConfigurationJson(JsonDocument& jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", TAG); return false;}

    if (!jsonDocument["sampleTime"].is<unsigned long>()) {_logger.warning("sampleTime is not unsigned long", TAG); return false;}
    // Ensure sampleTime is not below MINIMUM_SAMPLE_TIME
    if (jsonDocument["sampleTime"].as<unsigned long>() < MINIMUM_SAMPLE_TIME) {
        _logger.warning("sampleTime %lu is below the minimum value of %lu", TAG, 
            jsonDocument["sampleTime"].as<unsigned long>(), MINIMUM_SAMPLE_TIME);
        return false;
    }
    if (!jsonDocument["aVGain"].is<long>()) {_logger.warning("aVGain is not long", TAG); return false;}
    if (!jsonDocument["aIGain"].is<long>()) {_logger.warning("aIGain is not long", TAG); return false;}
    if (!jsonDocument["bIGain"].is<long>()) {_logger.warning("bIGain is not long", TAG); return false;}
    if (!jsonDocument["aIRmsOs"].is<long>()) {_logger.warning("aIRmsOs is not long", TAG); return false;}
    if (!jsonDocument["bIRmsOs"].is<long>()) {_logger.warning("bIRmsOs is not long", TAG); return false;}
    if (!jsonDocument["aWGain"].is<long>()) {_logger.warning("aWGain is not long", TAG); return false;}
    if (!jsonDocument["bWGain"].is<long>()) {_logger.warning("bWGain is not long", TAG); return false;}
    if (!jsonDocument["aWattOs"].is<long>()) {_logger.warning("aWattOs is not long", TAG); return false;}
    if (!jsonDocument["bWattOs"].is<long>()) {_logger.warning("bWattOs is not long", TAG); return false;} 
    if (!jsonDocument["aVarGain"].is<long>()) {_logger.warning("aVarGain is not long", TAG); return false;}
    if (!jsonDocument["bVarGain"].is<long>()) {_logger.warning("bVarGain is not long", TAG); return false;}
    if (!jsonDocument["aVarOs"].is<long>()) {_logger.warning("aVarOs is not long", TAG); return false;}
    if (!jsonDocument["bVarOs"].is<long>()) {_logger.warning("bVarOs is not long", TAG); return false;}
    if (!jsonDocument["aVaGain"].is<long>()) {_logger.warning("aVaGain is not long", TAG); return false;}
    if (!jsonDocument["bVaGain"].is<long>()) {_logger.warning("bVaGain is not long", TAG); return false;}
    if (!jsonDocument["aVaOs"].is<long>()) {_logger.warning("aVaOs is not long", TAG); return false;}
    if (!jsonDocument["bVaOs"].is<long>()) {_logger.warning("bVaOs is not long", TAG); return false;}
    if (!jsonDocument["phCalA"].is<long>()) {_logger.warning("phCalA is not long", TAG); return false;}
    if (!jsonDocument["phCalB"].is<long>()) {_logger.warning("phCalB is not long", TAG); return false;}

    return true;
}

// Calibration values
// --------------------

void Ade7953::_setCalibrationValuesFromSpiffs() {
    _logger.debug("Setting calibration values from SPIFFS", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (!setCalibrationValues(_jsonDocument)) {
        _logger.error("Failed to set calibration values from SPIFFS. Keeping default ones", TAG);
        setDefaultCalibrationValues();
        return;
    }

    _logger.debug("Successfully set calibration values from SPIFFS", TAG);
}

bool Ade7953::setCalibrationValues(JsonDocument &jsonDocument) {
    _logger.debug("Setting new calibration values...", TAG);

    if (!_validateCalibrationValuesJson(jsonDocument)) {
        _logger.warning("Invalid calibration JSON. Keeping previous calibration values", TAG);
        return false;
    }

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

    _updateChannelData();

    _logger.debug("Successfully set new calibration values", TAG);

    return true;
}

void Ade7953::setDefaultCalibrationValues() {
    _logger.debug("Setting default calibration values", TAG);

    createDefaultCalibrationFile();
    
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    setCalibrationValues(_jsonDocument);

    _logger.debug("Successfully set default calibration values", TAG);
}

void Ade7953::_jsonToCalibrationValues(JsonObject &jsonObject, CalibrationValues &calibrationValues) {
    _logger.verbose("Parsing JSON calibration values for label %s", TAG, calibrationValues.label.c_str());

    // The label is not parsed as it is already set in the channel data
    calibrationValues.vLsb = jsonObject["vLsb"].as<float>();
    calibrationValues.aLsb = jsonObject["aLsb"].as<float>();
    calibrationValues.wLsb = jsonObject["wLsb"].as<float>();
    calibrationValues.varLsb = jsonObject["varLsb"].as<float>();
    calibrationValues.vaLsb = jsonObject["vaLsb"].as<float>();
    calibrationValues.whLsb = jsonObject["whLsb"].as<float>();
    calibrationValues.varhLsb = jsonObject["varhLsb"].as<float>();
    calibrationValues.vahLsb = jsonObject["vahLsb"].as<float>();
}

bool Ade7953::_validateCalibrationValuesJson(JsonDocument& jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", TAG); return false;}

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {_logger.warning("JSON pair value is not an object", TAG); return false;}

        JsonObject calibrationObject = kv.value().as<JsonObject>();

        if (!calibrationObject["vLsb"].is<float>() && !calibrationObject["vLsb"].is<int>()) {_logger.warning("vLsb is not float or int", TAG); return false;}
        if (!calibrationObject["aLsb"].is<float>() && !calibrationObject["aLsb"].is<int>()) {_logger.warning("aLsb is not float or int", TAG); return false;}
        if (!calibrationObject["wLsb"].is<float>() && !calibrationObject["wLsb"].is<int>()) {_logger.warning("wLsb is not float or int", TAG); return false;}
        if (!calibrationObject["varLsb"].is<float>() && !calibrationObject["varLsb"].is<int>()) {_logger.warning("varLsb is not float or int", TAG); return false;}
        if (!calibrationObject["vaLsb"].is<float>() && !calibrationObject["vaLsb"].is<int>()) {_logger.warning("vaLsb is not float or int", TAG); return false;}
        if (!calibrationObject["whLsb"].is<float>() && !calibrationObject["whLsb"].is<int>()) {_logger.warning("whLsb is not float or int", TAG); return false;}
        if (!calibrationObject["varhLsb"].is<float>() && !calibrationObject["varhLsb"].is<int>()) {_logger.warning("varhLsb is not float or int", TAG); return false;}
        if (!calibrationObject["vahLsb"].is<float>() && !calibrationObject["vahLsb"].is<int>()) {_logger.warning("vahLsb is or not float or int", TAG); return false;}
    }

    return true;
}

// Data channel
// --------------------

void Ade7953::_setChannelDataFromSpiffs() {
    _logger.debug("Setting data channel from SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    if (!setChannelData(_jsonDocument)) {
        _logger.error("Failed to set data channel from SPIFFS. Keeping default data channel", TAG);
        setDefaultChannelData();
        return;
    }

    _logger.debug("Successfully set data channel from SPIFFS", TAG);
}

bool Ade7953::setChannelData(JsonDocument &jsonDocument) {
    _logger.debug("Setting channel data...", TAG);

    if (!_validateChannelDataJson(jsonDocument)) {
        _logger.warning("Invalid JSON data channel. Keeping previous data channel", TAG);
        return false;
    }

    for (JsonPair _kv : jsonDocument.as<JsonObject>()) {
        _logger.verbose(
            "Parsing JSON data channel %s | Active: %d | Reverse: %d | Label: %s | Phase: %d | Calibration Label: %s", 
            TAG, 
            _kv.key().c_str(), 
            _kv.value()["active"].as<bool>(), 
            _kv.value()["reverse"].as<bool>(), 
            _kv.value()["label"].as<String>(), 
            _kv.value()["phase"].as<Phase>(),
            _kv.value()["calibrationLabel"].as<String>()
        );

        int _index = atoi(_kv.key().c_str());

        // Check if _index is within bounds
        if (_index < CHANNEL_0 || _index >= CHANNEL_COUNT) {
            _logger.error("Index out of bounds: %d", TAG, _index);
            continue;
        }

        // Protect channel 0 from being disabled - it's the main voltage channel and must remain active
        if (_index == CHANNEL_0 && !_kv.value()["active"].as<bool>()) {
            _logger.warning("Attempt to disable channel 0 blocked - channel 0 must remain active", TAG);
            // Force channel 0 to stay active
            channelData[_index].active = true;
        } else {
            channelData[_index].active = _kv.value()["active"].as<bool>();
        }

        channelData[_index].index = _index;
        channelData[_index].reverse = _kv.value()["reverse"].as<bool>();
        channelData[_index].label = _kv.value()["label"].as<String>();
        channelData[_index].phase = _kv.value()["phase"].as<Phase>();
        channelData[_index].calibrationValues.label = _kv.value()["calibrationLabel"].as<String>();
    }
    _logger.debug("Successfully set data channel properties", TAG);

    // Add the calibration values to the channel data
    _updateChannelData();

    _saveChannelDataToSpiffs();

    publishMqtt.channel = true;

    _logger.debug("Successfully parsed JSON data channel", TAG);

    return true;
}

void Ade7953::setDefaultChannelData() {
    _logger.debug("Setting default data channel...", TAG);

    createDefaultChannelDataFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    setChannelData(_jsonDocument);

    _logger.debug("Successfully initialized data channel", TAG);
}

bool Ade7953::_saveChannelDataToSpiffs() {
    _logger.debug("Saving data channel to SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    channelDataToJson(_jsonDocument);

    if (serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument)) {
        _logger.debug("Successfully saved data channel to SPIFFS", TAG);
        return true;
    } else {
        _logger.error("Failed to save data channel to SPIFFS", TAG);
        return false;
    }
}

void Ade7953::channelDataToJson(JsonDocument &jsonDocument) {
    _logger.debug("Converting data channel to JSON...", TAG);

    for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {
        jsonDocument[String(i)]["active"] = channelData[i].active;
        jsonDocument[String(i)]["reverse"] = channelData[i].reverse;
        jsonDocument[String(i)]["label"] = channelData[i].label;
        jsonDocument[String(i)]["phase"] = channelData[i].phase;
        jsonDocument[String(i)]["calibrationLabel"] = channelData[i].calibrationValues.label;
    }

    _logger.debug("Successfully converted data channel to JSON", TAG);
}

bool Ade7953::_validateChannelDataJson(JsonDocument &jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", TAG); return false;}

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {_logger.warning("JSON pair value is not an object", TAG); return false;}

        int _index = atoi(kv.key().c_str());
        if (_index < CHANNEL_0 || _index >= CHANNEL_COUNT) {_logger.warning("Index out of bounds: %d", TAG, _index); return false;}

        JsonObject channelObject = kv.value().as<JsonObject>();

        if (!channelObject["active"].is<bool>()) {_logger.warning("active is not bool", TAG); return false;}
        if (!channelObject["reverse"].is<bool>()) {_logger.warning("reverse is not bool", TAG); return false;}
        if (!channelObject["label"].is<String>()) {_logger.warning("label is not string", TAG); return false;}
        if (!channelObject["phase"].is<int>()) {_logger.warning("phase is not int", TAG); return false;}
        if (kv.value()["phase"].as<int>() != PHASE_1 && kv.value()["phase"].as<int>() != PHASE_2 && kv.value()["phase"].as<int>() != PHASE_3) {
            _logger.warning("phase is not between 1 and 3", TAG); return false;
        }
        if (!channelObject["calibrationLabel"].is<String>()) {_logger.warning("calibrationLabel is not string", TAG); return false;}
    }

    return true;
}

void Ade7953::_updateChannelData() {
    _logger.debug("Updating data channel...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read calibration values from SPIFFS. Keeping previous values", TAG);
        return;
    }
    
    for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {        
        if (_jsonDocument[channelData[i].calibrationValues.label]) {
            // Extract the corresponding calibration values from the JSON
            JsonObject _jsonCalibrationValues = _jsonDocument[channelData[i].calibrationValues.label].as<JsonObject>();

            // Set the calibration values for the channel
            _jsonToCalibrationValues(_jsonCalibrationValues, channelData[i].calibrationValues);
        } else {
            _logger.warning(
                "Calibration label %s for channel %d not found in calibration JSON", 
                TAG, 
                channelData[i].calibrationValues.label.c_str(), 
                i
            );
        }
    }

    _logger.debug("Successfully updated data channel", TAG);
}

void Ade7953::_updateSampleTime() {
    _logger.debug("Updating sample time", TAG);

    unsigned int _linecyc = _sampleTime * 50 * 2 / 1000; // 1 channel at 1000 ms: 1000 ms / 1000 * 50 * 2 = 100 linecyc, as linecyc is half of the cycle
    _setLinecyc(_linecyc);

    _logger.debug("Successfully updated sample time", TAG);
}

// This returns the next channel (except CHANNEL_0) that is active
ChannelNumber Ade7953::_findNextActiveChannel(ChannelNumber currentChannel) {
    for (ChannelNumber i = static_cast<ChannelNumber>(currentChannel + 1); i < CHANNEL_COUNT; i = static_cast<ChannelNumber>(i + 1)) {
        if (channelData[i].active && i != CHANNEL_0) {
            return i;
        }
    }
    for (ChannelNumber i = static_cast<ChannelNumber>(1); i < currentChannel; i = static_cast<ChannelNumber>(i + 1)) {
        if (channelData[i].active && i != CHANNEL_0) {
            return i;
        }
    }

    return CHANNEL_INVALID;
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
bool Ade7953::_readMeterValues(int channel, unsigned long long linecycUnixTimeMillis) {
    unsigned long _millisRead = millis();
    unsigned long _deltaMillis = _millisRead - meterValues[channel].lastMillis;

    // // Ensure the reading is not being called too early if a previous valid reading exists
    // if (previousLastUnixTimeMilliseconds != 0) {
    //     unsigned long long timeSinceLastRead = linecycUnixTimeMillis - previousLastUnixTimeMilliseconds;
        
    //     // Not useful anymore since the measurement timing (_sampleTime) is handled indipendently
    //     // by the linecyc of the ADE7953. The actual reading of the data may be not coordinated,
    //     // and that is ok
    //     // // Ensure the reading is not being called too early (should not happen anyway)
    //     // // This was introduced as in channel 0 it was noticed that sometimes two meter values
    //     // // were sent with 1 ms difference, where the second one had 0 active power (since most
    //     // // probably the next line cycle was not yet finished)
    //     // // We use the time multiplied by 0.8 to keep some headroom
    //     // if (timeSinceLastRead < _sampleTime * 0.8) {
    //     //     _logger.warning("Reading too early for channel %d: %llu ms since last read, expected at least %0.0f ms", TAG, channel, timeSinceLastRead, _sampleTime * 0.8);
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
    if (meterValues[channel].lastMillis != 0 && _deltaMillis == 0) {
        _logger.warning("%s (%d): delta millis (%llu) is invalid. Discarding reading", TAG, channelData[channel].label.c_str(), channel, _deltaMillis);
        // _recordFailure();
        return false;
    }

    int _ade7953Channel = (channel == CHANNEL_0) ? CHANNEL_A : CHANNEL_B;

    float _voltage = 0.0f;
    float _current = 0.0f;
    float _activePower = 0.0f;
    float _reactivePower = 0.0f;
    float _apparentPower = 0.0f;
    float _powerFactor = 0.0f;
    float _activeEnergy = 0.0f;
    float _reactiveEnergy = 0.0f;
    float _apparentEnergy = 0.0f;

    Phase _basePhase = channelData[CHANNEL_0].phase;

    if (channelData[channel].phase == _basePhase) { // The phase is not necessarily PHASE_A, so use as reference the one of channel A
        TRACE();
        // These are the three most important values to read
        _activeEnergy = _readActiveEnergy(_ade7953Channel) / channelData[channel].calibrationValues.whLsb * (channelData[channel].reverse ? -1 : 1);
        _reactiveEnergy = _readReactiveEnergy(_ade7953Channel) / channelData[channel].calibrationValues.varhLsb * (channelData[channel].reverse ? -1 : 1);
        _apparentEnergy = _readApparentEnergy(_ade7953Channel) / channelData[channel].calibrationValues.vahLsb;        
        
        // Since the voltage measurement is only one in any case, it makes sense to just re-use the same value
        // as channel 0 (sampled 100s of milliseconds before only)
        if (channel == CHANNEL_0) {
            _voltage = _readVoltageRms() / channelData[channel].calibrationValues.vLsb;
            
            // Update grid frequency during channel 0 reading
            float _newGridFrequency = GRID_FREQUENCY_CONVERSION_FACTOR / _readPeriod();
            if (_validateGridFrequency(_newGridFrequency)) _gridFrequency = _newGridFrequency;
        } else {
            _voltage = meterValues[CHANNEL_0].voltage;
        }
        
        // We use sample time instead of _deltaMillis because the energy readings are over whole line cycles (defined by the sample time)
        // Thus, extracting the power from energy divided by linecycle is more stable (does not care about ESP32 slowing down) and accurate
        _activePower = _activeEnergy / (_sampleTime / 1000.0 / 3600.0); // W
        _reactivePower = _reactiveEnergy / (_sampleTime / 1000.0 / 3600.0); // var
        _apparentPower = _apparentEnergy / (_sampleTime / 1000.0 / 3600.0); // VA
        
        // It is faster and more consistent to compute the values rather than reading them from the ADE7953
        if (_apparentPower == 0) _powerFactor = 0.0f; // Avoid division by zero
        else _powerFactor = _activePower / _apparentPower * (_reactivePower >= 0 ? 1 : -1); // Apply sign as by datasheet (page 38)

        _current = _apparentPower / _voltage; // VA = V * A => A = VA / V | Always positive as apparent power is always positive
    } else { 
        // TODO: understand if this can be improved using the energy registers
        // Assume everything is the same as channel 0 except the current
        // Important: here the reverse channel is not taken into account as the calculations would (probably) be wrong
        // It is easier just to ensure during installation that the CTs are installed correctly

        TRACE();
        // Assume from channel 0
        _voltage = meterValues[CHANNEL_0].voltage; // Assume the voltage is the same for all channels (medium assumption as difference usually is in the order of few volts, so less than 1%)
        
        // Read wrong power factor due to the phase shift
        float _powerFactorPhaseOne = _readPowerFactor(_ade7953Channel)  * POWER_FACTOR_CONVERSION_FACTOR;

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

        if (channelData[channel].phase == _getLaggingPhase(_basePhase)) {
            _powerFactor = cos(acos(_powerFactorPhaseOne) - (2 * PI / 3));
        } else if (channelData[channel].phase == _getLeadingPhase(_basePhase)) {
            // I cannot prove why, but I am SURE the minus is needed if the phase is leading
            _powerFactor = - cos(acos(_powerFactorPhaseOne) + (2 * PI / 3));
        } else {
            _logger.error("Invalid phase %d for channel %d", TAG, channelData[channel].phase, channel);
            _recordFailure();
            return false;
        }

        // Read the current
        _current = _readCurrentRms(_ade7953Channel) / channelData[channel].calibrationValues.aLsb;
        
        // Compute power values
        _activePower = _current * _voltage * abs(_powerFactor);
        _apparentPower = _current * _voltage;
        _reactivePower = sqrt(pow(_apparentPower, 2) - pow(_activePower, 2)); // Small approximation leaving out distorted power
    }

    _apparentPower = abs(_apparentPower); // Apparent power must be positive

    // If the power factor is below a certain threshold, assume everything is 0 to avoid weird readings
    if (abs(_powerFactor) < MINIMUM_POWER_FACTOR) {
        _current = 0.0f;
        _activePower = 0.0f;
        _reactivePower = 0.0f;
        _apparentPower = 0.0f;
        _powerFactor = 0.0f;
        _activeEnergy = 0.0f;
        _reactiveEnergy = 0.0f;
        _apparentEnergy = 0.0f;
    }

    // Sometimes the power factor is very close to 1 but above 1. If so, clamp it to 1 as the measure is still valid
    if (abs(_powerFactor) > VALIDATE_POWER_FACTOR_MAX && abs(_powerFactor) < MAXIMUM_POWER_FACTOR_CLAMP) {
        _logger.debug(
            "%s (%d): Power factor %.3f is above %.3f, clamping it", 
            TAG,
            channelData[channel].label.c_str(), 
            channel, 
            _powerFactor,
            MAXIMUM_POWER_FACTOR_CLAMP
        );
        _powerFactor = (_powerFactor > 0) ? VALIDATE_POWER_FACTOR_MAX : VALIDATE_POWER_FACTOR_MIN; // Keep the sign of the power factor
        _activePower = _apparentPower; // Recompute active power based on the clamped power factor
        _reactivePower = 0.0f; // Small approximation leaving out distorted power
    }    
    
    TRACE();
    // For channel 0, discard readings where active or apparent energy is exactly 0,
    // unless there have been at least 100 consecutive zero readings
    if (channel == CHANNEL_0 && (_activeEnergy == 0.0f || _apparentEnergy == 0.0f)) {
        _channelStates[channel].consecutiveZeroCount++;
        
        if (_channelStates[channel].consecutiveZeroCount < MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE) {
            TRACE();
            _logger.debug("%s (%d): Zero energy reading on channel 0 discarded (count: %lu/%d)", 
                TAG, channelData[channel].label.c_str(),channel, _channelStates[channel].consecutiveZeroCount, MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE);
            _recordFailure();
            return false;
        }
    } else {
        // Reset counter for non-zero readings
        _channelStates[channel].consecutiveZeroCount = 0;
    }

    TRACE();
    if (
        !_validateVoltage(_voltage) || 
        !_validateCurrent(_current) || 
        !_validatePower(_activePower) || 
        !_validatePower(_reactivePower) || 
        !_validatePower(_apparentPower) || 
        !_validatePowerFactor(_powerFactor)
    ) {
        TRACE();
        logger.warning("%s (%d): Invalid reading (%.1fW, %.3fA, %.1fVAr, %.1fVA, %.3f)", 
            TAG, channelData[channel].label.c_str(), channel, _activePower, _current, _reactivePower, _apparentPower, _powerFactor);
        _recordFailure();
        return false;
    }

    // This part makes no sense to use anymore as the current is computed from the apparent power and voltage
    // // Ensure the current * voltage is not too different from the apparent power (both in absolute and relative terms)
    // // Skip this check if apparent power is below 1 to avoid issues with low power readings
    // // Channel 0 does not have this problem since it has a dedicated ADC on the ADE7953, while the other channels
    // // use a multiplexer and some weird behavior can happen sometimes
    // TRACE();
    // if (_apparentPower >= 1.0 && _apparentEnergy != 0 && channel != CHANNEL_0 &&
    //     (abs(_current * _voltage - _apparentPower) > MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE || 
    //      abs(_current * _voltage - _apparentPower) / _apparentPower > MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE)) 
    // {
    //     _logger.warning(
    //         "%s (%D): Current (%.3f * Voltage %.1f = %.1f) is too different from measured Apparent Power (%.1f). Discarding data point", 
    //         TAG,
    //         channelData[channel].label.c_str(),
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
    meterValues[channel].voltage = _voltage;
    meterValues[channel].current = _current;
    meterValues[channel].activePower = _activePower;
    meterValues[channel].reactivePower = _reactivePower;
    meterValues[channel].apparentPower = _apparentPower;
    meterValues[channel].powerFactor = _powerFactor;

    // If the phase is not the phase of the main channel, set the energy not to 0 if the current
    // is above the threshold since we cannot use the ADE7593 no-load feature in this approximation
    if (channelData[channel].phase != _basePhase && _current > MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD) {
        _activeEnergy = 1;
        _reactiveEnergy = 1;
        _apparentEnergy = 1;
    }

    TRACE();
    // Leverage the no-load feature of the ADE7953 to discard the noise
    // As such, when the energy read by the ADE7953 in the given linecycle is below
    // a certain threshold (set during setup), the read value is 0
    if (_activeEnergy > 0) {
        meterValues[channel].activeEnergyImported += abs(meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0); // W * ms * s / 1000 ms * h / 3600 s = Wh
    } else if (_activeEnergy < 0) {
        meterValues[channel].activeEnergyExported += abs(meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0); // W * ms * s / 1000 ms * h / 3600 s = Wh
    } else {
        _logger.debug(
            "%s (%d): No load active energy reading. Setting active power and power factor to 0", 
            TAG,
            channelData[channel].label.c_str(),
            channel
        );
        meterValues[channel].activePower = 0.0f;
        meterValues[channel].powerFactor = 0.0f;
    }

    if (_reactiveEnergy > 0) {
        meterValues[channel].reactiveEnergyImported += abs(meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0); // var * ms * s / 1000 ms * h / 3600 s = VArh
    } else if (_reactiveEnergy < 0) {
        meterValues[channel].reactiveEnergyExported += abs(meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0); // var * ms * s / 1000 ms * h / 3600 s = VArh
    } else {
        _logger.debug(
            "%s (%d): No load reactive energy reading. Setting reactive power to 0", 
            TAG,
            channelData[channel].label.c_str(),
            channel
        );
        meterValues[channel].reactivePower = 0.0f;
    }

    if (_apparentEnergy != 0) {
        meterValues[channel].apparentEnergy += meterValues[channel].apparentPower * _deltaMillis / 1000.0 / 3600.0; // VA * ms * s / 1000 ms * h / 3600 s = VAh
    } else {
        _logger.debug(
            "%s (%d): No load apparent energy reading. Setting apparent power and current to 0", 
            TAG,
            channelData[channel].label.c_str(),
            channel
        );
        meterValues[channel].current = 0.0f;
        meterValues[channel].apparentPower = 0.0f;
    }

    // We actually set the timestamp of the channel (used for the energy calculations)
    // only if we actually reached the end. Otherwise it would mean the point had to be
    // discarded
    statistics.ade7953ReadingCount++;
    meterValues[channel].lastMillis = _millisRead;
    meterValues[channel].lastUnixTimeMilliseconds = linecycUnixTimeMillis;
    return true;
}


// Poor man's phase shift (doing with modulus didn't work properly,
// and in any case the phases will always be at most 3)
Phase Ade7953::_getLaggingPhase(Phase phase) {
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

Phase Ade7953::_getLeadingPhase(Phase phase) {
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

bool Ade7953::_validateValue(float newValue, float min, float max) {
    if (newValue < min || newValue > max) {
        _logger.warning("Value %f out of range (minimum: %f, maximum: %f)", TAG, newValue, min, max);
        return false;
    }
    return true;
}

bool Ade7953::_validateVoltage(float newValue) {
    return _validateValue(newValue, VALIDATE_VOLTAGE_MIN, VALIDATE_VOLTAGE_MAX);
}

bool Ade7953::_validateCurrent(float newValue) {
    return _validateValue(newValue, VALIDATE_CURRENT_MIN, VALIDATE_CURRENT_MAX);
}

bool Ade7953::_validatePower(float newValue) {
    return _validateValue(newValue, VALIDATE_POWER_MIN, VALIDATE_POWER_MAX);
}

bool Ade7953::_validatePowerFactor(float newValue) {
    return _validateValue(newValue, VALIDATE_POWER_FACTOR_MIN, VALIDATE_POWER_FACTOR_MAX);
}

bool Ade7953::_validateGridFrequency(float newValue) {
    return _validateValue(newValue, VALIDATE_GRID_FREQUENCY_MIN, VALIDATE_GRID_FREQUENCY_MAX);
}

void Ade7953::singleMeterValuesToJson(JsonDocument &jsonDocument, ChannelNumber channel) {
    JsonObject _jsonValues = jsonDocument.to<JsonObject>();

    // TODO: make this names with defines to ensure consistency
    _jsonValues["voltage"] = meterValues[channel].voltage;
    _jsonValues["current"] = meterValues[channel].current;
    _jsonValues["activePower"] = meterValues[channel].activePower;
    _jsonValues["apparentPower"] = meterValues[channel].apparentPower;
    _jsonValues["reactivePower"] = meterValues[channel].reactivePower;
    _jsonValues["powerFactor"] = meterValues[channel].powerFactor;
    _jsonValues["activeEnergyImported"] = meterValues[channel].activeEnergyImported;
    _jsonValues["activeEnergyExported"] = meterValues[channel].activeEnergyExported;
    _jsonValues["reactiveEnergyImported"] = meterValues[channel].reactiveEnergyImported;
    _jsonValues["reactiveEnergyExported"] = meterValues[channel].reactiveEnergyExported;
    _jsonValues["apparentEnergy"] = meterValues[channel].apparentEnergy;
}


void Ade7953::fullMeterValuesToJson(JsonDocument &jsonDocument) {
    for (ChannelNumber i = CHANNEL_0; i < CHANNEL_COUNT; i = static_cast<ChannelNumber>(i + 1)) {
        if (channelData[i].active) {
            JsonObject _jsonChannel = jsonDocument.add<JsonObject>();
            _jsonChannel["index"] = i;
            _jsonChannel["label"] = channelData[i].label;
            // TODO: add the phase here as well

            JsonDocument _jsonData;
            singleMeterValuesToJson(_jsonData, i);
            _jsonChannel["data"] = _jsonData.as<JsonObject>();
        }
    }
}

// Energy
// --------------------

void Ade7953::_setEnergyFromSpiffs() {
    _logger.debug("Reading energy from SPIFFS", TAG);
    
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        _logger.error("Failed to read energy from SPIFFS", TAG);
        return;
    } else {
        for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {
            meterValues[i].activeEnergyImported = _jsonDocument[String(i)]["activeEnergyImported"].as<float>();
            meterValues[i].activeEnergyExported = _jsonDocument[String(i)]["activeEnergyExported"].as<float>();
            meterValues[i].reactiveEnergyImported = _jsonDocument[String(i)]["reactiveEnergyImported"].as<float>();
            meterValues[i].reactiveEnergyExported = _jsonDocument[String(i)]["reactiveEnergyExported"].as<float>();
            meterValues[i].apparentEnergy = _jsonDocument[String(i)]["apparentEnergy"].as<float>();
        }
    }
        
    _logger.debug("Successfully read energy from SPIFFS", TAG);
}

void Ade7953::saveEnergy() {
    _logger.debug("Saving energy...", TAG);

    TRACE();
    _saveEnergyToSpiffs();
    _saveDailyEnergyToSpiffs();

    _logger.debug("Successfully saved energy", TAG);
}

void Ade7953::_saveEnergyToSpiffs() {
    _logger.debug("Saving energy to SPIFFS...", TAG);

    TRACE();
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["activeEnergyImported"] = meterValues[i].activeEnergyImported;
        _jsonDocument[String(i)]["activeEnergyExported"] = meterValues[i].activeEnergyExported;
        _jsonDocument[String(i)]["reactiveEnergyImported"] = meterValues[i].reactiveEnergyImported;
        _jsonDocument[String(i)]["reactiveEnergyExported"] = meterValues[i].reactiveEnergyExported;
        _jsonDocument[String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) _logger.debug("Successfully saved energy to SPIFFS", TAG);
}

void Ade7953::_saveDailyEnergyToSpiffs() {
    _logger.debug("Saving daily energy to SPIFFS...", TAG);

    TRACE();
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument);
    
    time_t now = time(nullptr);
    if (!validateUnixTime(now, false)) {
        _logger.warning("Saving daily energy even if time is invalid: %ld", TAG, now);
    }
    struct tm *timeinfo = localtime(&now);
    char _currentDate[11];
    strftime(_currentDate, sizeof(_currentDate), "%Y-%m-%d", timeinfo);

    for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            if (meterValues[i].activeEnergyImported > 1) _jsonDocument[_currentDate][String(i)]["activeEnergyImported"] = meterValues[i].activeEnergyImported;
            if (meterValues[i].activeEnergyExported > 1) _jsonDocument[_currentDate][String(i)]["activeEnergyExported"] = meterValues[i].activeEnergyExported;
            if (meterValues[i].reactiveEnergyImported > 1) _jsonDocument[_currentDate][String(i)]["reactiveEnergyImported"] = meterValues[i].reactiveEnergyImported;
            if (meterValues[i].reactiveEnergyExported > 1) _jsonDocument[_currentDate][String(i)]["reactiveEnergyExported"] = meterValues[i].reactiveEnergyExported;
            if (meterValues[i].apparentEnergy > 1) _jsonDocument[_currentDate][String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
        }
    }

    if (serializeJsonToSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument)) _logger.debug("Successfully saved daily energy to SPIFFS", TAG);
}

void Ade7953::resetEnergyValues() {
    _logger.warning("Resetting energy values to 0", TAG);

    for (int i = CHANNEL_0; i < CHANNEL_COUNT; i++) {
        meterValues[i].activeEnergyImported = 0.0f;
        meterValues[i].activeEnergyExported = 0.0f;
        meterValues[i].reactiveEnergyImported = 0.0f;
        meterValues[i].reactiveEnergyExported = 0.0f;
        meterValues[i].apparentEnergy = 0.0f;
    }

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);
    saveEnergy();

    _logger.info("Successfully reset energy values to 0", TAG);
}

bool Ade7953::setEnergyValues(JsonDocument &jsonDocument) {
    _logger.warning("Setting selective energy values", TAG);

    if (!jsonDocument.is<JsonObject>()) {
        _logger.error("Invalid JSON format for energy values", TAG);
        return false;
    }

    bool hasChanges = false;
    bool clearDailyEnergy = false;

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        String channelStr = kv.key().c_str();
        int channel = channelStr.toInt();
        
        if (channel < CHANNEL_0 || channel >= CHANNEL_COUNT) {
            _logger.warning("Invalid channel index %d", TAG, channel);
            continue;
        }

        JsonObject energyData = kv.value().as<JsonObject>();
        
        if (energyData["activeEnergyImported"].is<float>()) {
            meterValues[channel].activeEnergyImported = energyData["activeEnergyImported"];
            hasChanges = true;
            clearDailyEnergy = true;
        }
        
        if (energyData["activeEnergyExported"].is<float>()) {
            meterValues[channel].activeEnergyExported = energyData["activeEnergyExported"];
            hasChanges = true;
            clearDailyEnergy = true;
        }
        
        if (energyData["reactiveEnergyImported"].is<float>()) {
            meterValues[channel].reactiveEnergyImported = energyData["reactiveEnergyImported"];
            hasChanges = true;
            clearDailyEnergy = true;
        }
        
        if (energyData["reactiveEnergyExported"].is<float>()) {
            meterValues[channel].reactiveEnergyExported = energyData["reactiveEnergyExported"];
            hasChanges = true;
            clearDailyEnergy = true;
        }
        
        if (energyData["apparentEnergy"].is<float>()) {
            meterValues[channel].apparentEnergy = energyData["apparentEnergy"];
            hasChanges = true;
            clearDailyEnergy = true;
        }
    }

    if (hasChanges) {
        if (clearDailyEnergy) {
            _logger.warning("Clearing daily energy data due to energy value changes", TAG);
            createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);
        }
        
        saveEnergy();
        _logger.info("Successfully set selective energy values", TAG);
        return true;
    }

    _logger.warning("No valid energy values found to set", TAG);
    return false;
}

// Others
// --------------------

void Ade7953::_setLinecyc(unsigned int linecyc) {
    linecyc = min(max(linecyc, ADE7953_MIN_LINECYC), ADE7953_MAX_LINECYC);

    _logger.debug(
        "Setting linecyc to %d",
        TAG,
        linecyc
    );

    writeRegister(LINECYC_16, 16, long(linecyc));
}

void Ade7953::_setPhaseCalibration(long phaseCalibration, int channel) {
    _logger.debug(
        "Setting phase calibration to %d on channel %d",
        TAG,
        phaseCalibration,
        channel
    );
    
    if (channel == CHANNEL_A) {
        writeRegister(PHCALA_16, 16, phaseCalibration);
    } else {
        writeRegister(PHCALB_16, 16, phaseCalibration);
    }
}

void Ade7953::_setPgaGain(long pgaGain, int channel, int measurementType) {
    _logger.debug(
        "Setting PGA gain to %d on channel %d for measurement type %d",
        TAG,
        pgaGain,
        channel,
        measurementType
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(PGA_V_8, 8, pgaGain);
                break;
            case CURRENT:
                writeRegister(PGA_IA_8, 8, pgaGain);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(PGA_V_8, 8, pgaGain);
                break;
            case CURRENT:
                writeRegister(PGA_IB_8, 8, pgaGain);
                break;
        }
    }
}

void Ade7953::_setGain(long gain, int channel, int measurementType) {
    _logger.debug(
        "Setting gain to %ld on channel %d for measurement type %d",
        TAG,
        gain,
        channel,
        measurementType
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(AVGAIN_32, 32, gain);
                break;
            case CURRENT:
                writeRegister(AIGAIN_32, 32, gain);
                break;
            case ACTIVE_POWER:
                writeRegister(AWGAIN_32, 32, gain);
                break;
            case REACTIVE_POWER:
                writeRegister(AVARGAIN_32, 32, gain);
                break;
            case APPARENT_POWER:
                writeRegister(AVAGAIN_32, 32, gain);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(AVGAIN_32, 32, gain);
                break;
            case CURRENT:
                writeRegister(BIGAIN_32, 32, gain);
                break;
            case ACTIVE_POWER:
                writeRegister(BWGAIN_32, 32, gain);
                break;
            case REACTIVE_POWER:
                writeRegister(BVARGAIN_32, 32, gain);
                break;
            case APPARENT_POWER:
                writeRegister(BVAGAIN_32, 32, gain);
                break;
        }
    }
}

void Ade7953::_setOffset(long offset, int channel, int measurementType) {
    _logger.debug(
        "Setting offset to %ld on channel %d for measurement type %d",
        TAG,
        offset,
        channel,
        measurementType
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(VRMSOS_32, 32, offset);
                break;
            case CURRENT:
                writeRegister(AIRMSOS_32, 32, offset);
                break;
            case ACTIVE_POWER:
                writeRegister(AWATTOS_32, 32, offset);
                break;
            case REACTIVE_POWER:
                writeRegister(AVAROS_32, 32, offset);
                break;
            case APPARENT_POWER:
                writeRegister(AVAOS_32, 32, offset);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE:
                writeRegister(VRMSOS_32, 32, offset);
                break;
            case CURRENT:
                writeRegister(BIRMSOS_32, 32, offset);
                break;
            case ACTIVE_POWER:
                writeRegister(BWATTOS_32, 32, offset);
                break;
            case REACTIVE_POWER:
                writeRegister(BVAROS_32, 32, offset);
                break;
            case APPARENT_POWER:
                writeRegister(BVAOS_32, 32, offset);
                break;
        }
    }
}

long Ade7953::_readApparentPowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVA_32, 32, true);} 
    else {return readRegister(BVA_32, 32, true);}
}

/*
Reads the "instantaneous" active power.

"Instantaneous" because the active power is only defined as the dc component
of the instantaneous power signal, which is V_RMS * I_RMS - V_RMS * I_RMS * cos(2*omega*t). 
It is updated at 6.99 kHz.

@param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
@return The active power in LSB.
*/
long Ade7953::_readActivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AWATT_32, 32, true);} 
    else {return readRegister(BWATT_32, 32, true);}
}

long Ade7953::_readReactivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVAR_32, 32, true);} 
    else {return readRegister(BVAR_32, 32, true);}
}


/*
Reads the actual instantaneous current. 

This allows you see the actual sinusoidal waveform, so both positive and 
negative values. At full scale (so 500 mV), the value returned is 9032007d.

@param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
@return The actual instantaneous current in LSB.
*/
long Ade7953::_readCurrentInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IA_32, 32, true);} 
    else {return readRegister(IB_32, 32, true);}
}

/*
Reads the actual instantaneous voltage. 

This allows you 
to see the actual sinusoidal waveform, so both positive
and negative values. At full scale (so 500 mV), the value
returned is 9032007d.

@return The actual instantaneous voltage in LSB.
*/
long Ade7953::_readVoltageInstantaneous() {
    return readRegister(V_32, 32, true);
}

/*
Reads the current in RMS.

This measurement is updated at 6.99 kHz and has a settling
time of 200 ms. The value is in LSB.

@param channel The channel to read from. Either CHANNEL_A or CHANNEL_B.
@return The current in RMS in LSB.
*/
long Ade7953::_readCurrentRms(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IRMSA_32, 32, false);} 
    else {return readRegister(IRMSB_32, 32, false);}
}

/*
Reads the voltage in RMS.

This measurement is updated at 6.99 kHz and has a settling
time of 200 ms. The value is in LSB.

@return The voltage in RMS in LSB.
*/
long Ade7953::_readVoltageRms() {
    return readRegister(VRMS_32, 32, false);
}

long Ade7953::_readActiveEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AENERGYA_32, 32, true);} 
    else {return readRegister(AENERGYB_32, 32, true);}
}

long Ade7953::_readReactiveEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(RENERGYA_32, 32, true);} 
    else {return readRegister(RENERGYB_32, 32, true);}
}

long Ade7953::_readApparentEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(APENERGYA_32, 32, true);} 
    else {return readRegister(APENERGYB_32, 32, true);}
}

long Ade7953::_readPowerFactor(int channel) {
    if (channel == CHANNEL_A) {return readRegister(PFA_16, 16, true);} 
    else {return readRegister(PFB_16, 16, true);}
}

long Ade7953::_readAngle(int channel) {
    if (channel == CHANNEL_A) {return readRegister(ANGLE_A_16, 16, true);} 
    else {return readRegister(ANGLE_B_16, 16, true);}
}

long Ade7953::_readPeriod() {
    return readRegister(PERIOD_16, 16, false);
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
long Ade7953::readRegister(long registerAddress, int nBits, bool signedData, bool isVerificationRequired) {

    if (isVerificationRequired) {
        if (_spiOperationMutex == NULL || xSemaphoreTake(_spiOperationMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            _logger.error("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
            return INVALID_SPI_READ_WRITE;
        }
    }

    // Acquire SPI mutex with timeout to prevent deadlocks
    if (_spiMutex == NULL || xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        _logger.error("Failed to acquire SPI mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
        if (isVerificationRequired) xSemaphoreGive(_spiOperationMutex);
        return INVALID_SPI_READ_WRITE;
    }

    digitalWrite(_ssPin, LOW);

    SPI.transfer(registerAddress >> 8);
    SPI.transfer(registerAddress & 0xFF);
    SPI.transfer(READ_TRANSFER);

    byte _response[nBits / 8];
    for (int i = 0; i < nBits / 8; i++) {
        _response[i] = SPI.transfer(READ_TRANSFER);
    }

    digitalWrite(_ssPin, HIGH);

    xSemaphoreGive(_spiMutex);

    long _long_response = 0;
    for (int i = 0; i < nBits / 8; i++) {
        _long_response = (_long_response << 8) | _response[i];
    }
    if (signedData) {
        if (_long_response & (1 << (nBits - 1))) {
            _long_response -= (1 << nBits);
        }
    }
    _logger.verbose(
        "Read %ld from register %ld with %d bits",
        TAG,
        _long_response,
        registerAddress,
        nBits
    );

    if (isVerificationRequired) {
        if (!_verifyLastCommunication(registerAddress, nBits, _long_response, signedData, false)) {
            _logger.debug("Failed to verify last read communication for register %ld (0x%04lX). Value was %ld (0x%04lX)", TAG, registerAddress, registerAddress, _long_response, _long_response);
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
void Ade7953::writeRegister(long registerAddress, int nBits, long data, bool isVerificationRequired) {
    _logger.debug(
        "Writing %ld (0x%04lX) to register %ld (0x%04lX) with %d bits",
        TAG,
        data, data,
        registerAddress, registerAddress,
        nBits
    );

    if (isVerificationRequired) {
        if (_spiOperationMutex == NULL || xSemaphoreTake(_spiOperationMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            _logger.error("Failed to acquire SPI operation mutex for read operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
            return;
        }
    }

    // Acquire SPI mutex with timeout to prevent deadlocks
    if (_spiMutex == NULL || xSemaphoreTake(_spiMutex, pdMS_TO_TICKS(ADE7953_SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        _logger.error("Failed to acquire SPI mutex for write operation on register %ld (0x%04lX)", TAG, registerAddress, registerAddress);
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
            _logger.warning("Failed to verify last write communication for register %ld", TAG, registerAddress);
            _recordFailure();
        }
        xSemaphoreGive(_spiOperationMutex);
    }
}

bool Ade7953::_verifyLastCommunication(long expectedAddress, int expectedBits, long expectedData, bool signedData, bool wasWrite) {    
    
    long lastAddress = readRegister(LAST_ADD_16, 16, false, false);
    if (lastAddress != expectedAddress) {
        _logger.warning(
            "Last address %ld (0x%04lX) (write: %d) does not match expected %ld (0x%04lX). Expected data %ld (0x%04lX)", 
            TAG, 
            lastAddress, lastAddress, 
            wasWrite, 
            expectedAddress, expectedAddress, 
            expectedData, expectedData);
        return false;
    }
    
    long lastOp = readRegister(LAST_OP_8, 8, false, false);
    if (wasWrite && lastOp != LAST_OP_WRITE_VALUE) {
        _logger.warning("Last operation was not a write (expected %d, got %ld)", TAG, LAST_OP_WRITE_VALUE, lastOp);
        return false;
    } else if (!wasWrite && lastOp != LAST_OP_READ_VALUE) {
        _logger.warning("Last operation was not a read (expected %d, got %ld)", TAG, LAST_OP_READ_VALUE, lastOp);
        return false;
    }    
    
    // Select the appropriate LAST_RWDATA register based on the bit size
    long dataRegister;
    int dataRegisterBits;
    
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
    
    long lastData = readRegister(dataRegister, dataRegisterBits, signedData, false);
    if (lastData != expectedData) {
        _logger.warning("Last data %ld does not match expected %ld", TAG, lastData, expectedData);
        return false;
    }

    _logger.verbose("Last communication verified successfully", TAG);
    return true;
}

void Ade7953::_recordFailure() {
    TRACE();
    _logger.debug("Recording failure for ADE7953 communication", TAG);

    if (_failureCount == 0) {
        _firstFailureTime = millis();
    }

    _failureCount++;
    statistics.ade7953ReadingCountFailure++;
    _checkForTooManyFailures();
}

void Ade7953::_checkForTooManyFailures() {
    TRACE();
    if (millis() - _firstFailureTime > ADE7953_FAILURE_RESET_TIMEOUT_MS && _failureCount > 0) {
        _logger.debug("Failure timeout exceeded (%lu ms). Resetting failure count (reached %d)", TAG, millis() - _firstFailureTime, _failureCount);
        
        _failureCount = 0;
        _firstFailureTime = 0;
        
        return;
    }

    if (_failureCount >= ADE7953_MAX_FAILURES_BEFORE_RESTART) {
        TRACE();
        _logger.fatal("Too many failures (%d) in ADE7953 communication or readings. Resetting device...", TAG, _failureCount);
        setRestartEsp32(TAG, "Too many failures in ADE7953 communication or readings");

        // Reset the failure count and first failure time to avoid infinite loop of setting the restart
        _failureCount = 0;
        _firstFailureTime = 0;
    }
}

// Helper functions
// --------------------

// Aggregate data

float Ade7953::getAggregatedActivePower(bool includeChannel0) {
    float sum = 0.0f;
    int activeChannelCount = 0;

    for (int i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            sum += meterValues[i].activePower;
            activeChannelCount++;
        }
    }
    return activeChannelCount > 0 ? sum : 0.0f;
}

float Ade7953::getAggregatedReactivePower(bool includeChannel0) {
    float sum = 0.0f;
    int activeChannelCount = 0;

    for (int i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            sum += meterValues[i].reactivePower;
            activeChannelCount++;
        }
    }
    return activeChannelCount > 0 ? sum : 0.0f;
}

float Ade7953::getAggregatedApparentPower(bool includeChannel0) {
    float sum = 0.0f;
    int activeChannelCount = 0;

    for (int i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            sum += meterValues[i].apparentPower;
            activeChannelCount++;
        }
    }
    return activeChannelCount > 0 ? sum : 0.0f;
}

float Ade7953::getAggregatedPowerFactor(bool includeChannel0) {
    float _aggregatedActivePower = getAggregatedActivePower(includeChannel0);
    float _aggregatedApparentPower = getAggregatedApparentPower(includeChannel0);

    return _aggregatedApparentPower > 0 ? _aggregatedActivePower / _aggregatedApparentPower : 0.0f;
}

// Interrupt handling methods
// --------------------

void Ade7953::_setupInterrupts() {
    _logger.debug("Setting up ADE7953 interrupts...", TAG);
    
    // Enable only CYCEND interrupt for line cycle end detection (bit 18)
    writeRegister(IRQENA_32, 32, DEFAULT_IRQENA_REGISTER);
    
    // Clear any existing interrupt status
    readRegister(RSTIRQSTATA_32, 32, false);
    readRegister(RSTIRQSTATB_32, 32, false);

    _logger.debug("ADE7953 interrupts enabled: CYCEND, RESET", TAG);
}



// Returns the string name of the IRQSTATA bit, or nullptr if not found
const char* Ade7953::_irqstataBitName(int bit) {
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
    default:                       return "";
  }
}

String Ade7953::_getBitsString(long value, int nBits) {
    String result = "";
    for (int i = nBits - 1; i >= 0; i--) {
        if ((value >> i) & 1) {
            if (result.length() > 0) result += ",";
            result += _irqstataBitName(1 << i);
        }
    }
    return result.length() > 0 ? result : "none";
}

Ade7953InterruptType Ade7953::_handleInterrupt() {
    
    long statusA = readRegister(RSTIRQSTATA_32, 32, false);
    // No need to read for channel B

    // Very important: if we detected a reset or a CRC change in the configurations, 
    // we must reinitialize the device
    if (statusA & (1 << IRQSTATA_RESET_BIT)) {
        TRACE();
        _logger.warning("Reset interrupt detected. Device needs reinitialization", TAG);
        return Ade7953InterruptType::RESET;
    } else if (statusA & (1 << IRQSTATA_CRC_BIT)) {
        // TODO: how to handle this? if we change a setting in the ADE7953, we should expect a CRC change and avoid a loop.
        TRACE();
        _logger.warning("CRC changed detected. Device configuration may have changed", TAG);
        return Ade7953InterruptType::CRC_CHANGE;
        // Check for CYCEND interrupt (bit 18) - Line cycle end
    } else if (statusA & (1 << IRQSTATA_CYCEND_BIT)) {
        statistics.ade7953TotalHandledInterrupts++;
        _logger.verbose("Line cycle end detected on Channel A", TAG);
        
        // Only for CYCEND interrupts, switch to next channel and set multiplexer
        _currentChannel = _findNextActiveChannel(_currentChannel);
        _multiplexer.setChannel(static_cast<ChannelNumber>(std::max(_currentChannel - 1, 0)));
        
        return Ade7953InterruptType::CYCEND;
    } else {
        // Just log the unhandled status
        _logger.warning("Unhandled ADE7953 interrupt status: %s (0x%08lX)", TAG, 
            _getBitsString(statusA, 32).c_str(), statusA);
        return Ade7953InterruptType::OTHER;
    }
}

void Ade7953::_startMeterReadingTask() {
    TRACE();
    if (_ade7953InterruptSemaphore == NULL) {
        _ade7953InterruptSemaphore = xSemaphoreCreateBinary();
        if (_ade7953InterruptSemaphore == NULL) {
            _logger.error("Failed to create ADE7953 interrupt semaphore", TAG);
            return;
        }
    }

    TRACE();
    _instance = this;
    _attachInterruptHandler();
    if (_meterReadingTaskHandle == NULL) {
        BaseType_t result = xTaskCreate(
            _meterReadingTask, 
            ADE7953_METER_READING_TASK_NAME, 
            ADE7953_METER_READING_TASK_STACK_SIZE, 
            this, 
            ADE7953_METER_READING_TASK_PRIORITY, 
            &_meterReadingTaskHandle
        );
        if (result != pdPASS) {
            _logger.error("Failed to create meter reading task", TAG);
            return;
        }
    }
}

void Ade7953::_stopMeterReadingTask() {
    TRACE();
    _detachInterruptHandler();
    if (_meterReadingTaskHandle != NULL) {
        vTaskDelete(_meterReadingTaskHandle);
        _meterReadingTaskHandle = NULL;
    }
    if (_ade7953InterruptSemaphore != NULL) {
        vSemaphoreDelete(_ade7953InterruptSemaphore);
        _ade7953InterruptSemaphore = NULL;
    }
    _instance = nullptr;
}

void Ade7953::pauseMeterReadingTask() {
    TRACE();
    _logger.info("Pausing ADE7953 meter reading task", TAG);

    _detachInterruptHandler();
    if (_meterReadingTaskHandle != NULL) {
        vTaskSuspend(_meterReadingTaskHandle);
    }
}

void Ade7953::resumeMeterReadingTask() {
    TRACE();
    _logger.info("Resuming ADE7953 meter reading task", TAG);
    
    if (_meterReadingTaskHandle != NULL) vTaskResume(_meterReadingTaskHandle);
    _attachInterruptHandler();
}

void Ade7953::_attachInterruptHandler() {
    _detachInterruptHandler();
    ::attachInterrupt(digitalPinToInterrupt(_interruptPin), _isrHandler, FALLING);
}

void Ade7953::_detachInterruptHandler() {
    ::detachInterrupt(digitalPinToInterrupt(_interruptPin));
}

void IRAM_ATTR Ade7953::_isrHandler()
{
    if (_instance && _instance->_ade7953InterruptSemaphore != NULL)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        statistics.ade7953TotalInterrupts++;
        _instance->_lastInterruptTime = millis();

        // Signal the task to handle the interrupt - let the task determine the cause
        xSemaphoreGiveFromISR(_instance->_ade7953InterruptSemaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
    }
}

void Ade7953::_checkInterruptTiming() {
    if (millis() > MINIMUM_TIME_BEFORE_VALID_METER &&
        millis() - _lastInterruptTime >= getSampleTime()) {
        _logger.warning("ADE7953 interrupt not handled within the sample time. We are %lu ms late (sample time is %lu ms).", 
                       TAG, millis() - _lastInterruptTime, getSampleTime());
    }
}

bool Ade7953::_processChannelReading(int channel, unsigned long long linecycUnix) {
    if (!_readMeterValues(channel, linecycUnix)) {
        return false;
    }
    
    _addMeterDataToPayload(channel, linecycUnix);
    printMeterValues(&meterValues[channel], &channelData[channel]);
    return true;
}

void Ade7953::_addMeterDataToPayload(int channel, unsigned long long linecycUnix) {
    if (!_customTime.isTimeSynched() || millis() <= MINIMUM_TIME_BEFORE_VALID_METER) {
        return;
    }
    
    if (_payloadMeterMutex) xSemaphoreTake(_payloadMeterMutex, pdMS_TO_TICKS(5000));
    
    _payloadMeter.push(PayloadMeter(
        channel, 
        linecycUnix, 
        meterValues[channel].activePower, 
        meterValues[channel].powerFactor
    ));
    
    if (_payloadMeterMutex) xSemaphoreGive(_payloadMeterMutex);
}

void Ade7953::_processCycendInterrupt(unsigned long long linecycUnix) {
    _led.setGreen();
    
    // Process current channel (if active)
    if (_currentChannel != CHANNEL_INVALID) {
        _processChannelReading(_currentChannel, linecycUnix);
    }
    
    // Always process channel 0
    _processChannelReading(CHANNEL_0, linecycUnix);
    
    _led.setOff();
}

void Ade7953::_handleCrcChangeInterrupt() {
    _logger.warning("CRC change detected - this may indicate configuration changes", TAG);
    
    // For now, we'll just log this. In the future, you might want to:
    // 1. Check if this was an expected change (e.g., during calibration)
    // 2. Verify device configuration integrity
    // 3. Potentially trigger a reinitialization if unexpected
    
    // TODO: Implement smarter CRC change handling to avoid reinitialization loops
    // when making intentional configuration changes
}

void Ade7953::_meterReadingTask(void *parameter)
{
    Ade7953 *self = static_cast<Ade7953 *>(parameter);
    if (!self) vTaskDelete(NULL);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(ADE7953_INTERRUPT_TIMEOUT_MS + self->getSampleTime());
    
    while (true)
    {
        // Wait for interrupt signal
        if (self->_ade7953InterruptSemaphore != NULL &&
            xSemaphoreTake(self->_ade7953InterruptSemaphore, timeoutTicks) == pdTRUE)
        {
            unsigned long long linecycUnix = self->_customTime.getUnixTimeMilliseconds();
            
            // Check if interrupt timing is within expected bounds
            self->_checkInterruptTiming();

            // Handle the interrupt and determine its type
            Ade7953InterruptType interruptType = self->_handleInterrupt();

            // Process based on interrupt type
            switch (interruptType)
            {
            case Ade7953InterruptType::CYCEND:
                self->_processCycendInterrupt(linecycUnix);
                break;

            case Ade7953InterruptType::RESET:
                self->_reinitializeAfterInterrupt();
                break;
            case Ade7953InterruptType::CRC_CHANGE:
                self->_handleCrcChangeInterrupt();
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
            // Timeout or semaphore error
            if (self->_ade7953InterruptSemaphore == NULL) {
                self->_logger.debug("Semaphore is NULL, task exiting...", TAG);
                break;
            }
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
        }
    }

    vTaskDelete(NULL);
}

void Ade7953::_reinitializeAfterInterrupt() {
    _logger.warning("Reinitializing ADE7953 after reset/CRC change interrupt", TAG);
    
    // Note: We don't pause/resume meter reading task here because this method
    // is called from within the meter reading task itself. Pausing would cause a deadlock.
    
    // Detach interrupt handler temporarily to prevent conflicts
    _detachInterruptHandler();
    
    try {
        // Reinitialize the device settings (but not the hardware pins)
        _logger.debug("Setting optimum settings...", TAG);
        _setOptimumSettings();
        
        _logger.debug("Setting default parameters...", TAG);
        _setDefaultParameters();
        
        _logger.debug("Setting configuration from SPIFFS...", TAG);
        _setConfigurationFromSpiffs();
        
        _logger.debug("Reading calibration values from SPIFFS...", TAG);
        _setCalibrationValuesFromSpiffs();
        
        _logger.debug("Setting up interrupts...", TAG);
        _setupInterrupts();
        
        _logger.info("ADE7953 reinitialization completed successfully", TAG);
    } catch (...) {
        _logger.error("Error during ADE7953 reinitialization", TAG);
    }
    
    // Reattach interrupt handler
    _attachInterruptHandler();
}