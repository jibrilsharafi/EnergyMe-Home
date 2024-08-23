#include "ade7953.h"

Ade7953::Ade7953(
    int ssPin,
    int sckPin,
    int misoPin,
    int mosiPin,
    int resetPin,
    AdvancedLogger &logger,
    CustomTime &customTime) : 
        _ssPin(ssPin),
        _sckPin(sckPin),
        _misoPin(misoPin),
        _mosiPin(mosiPin),
        _resetPin(resetPin),
        _logger(logger),
        _customTime(customTime) {

    Ade7953Configuration _configuration;
    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];
}

bool Ade7953::begin() {
    _logger.debug("Initializing Ade7953", "ade7953::begin");

    _logger.debug("Setting up pins", "ade7953::begin");
    pinMode(_ssPin, OUTPUT);
    pinMode(_sckPin, OUTPUT);
    pinMode(_misoPin, INPUT);
    pinMode(_mosiPin, OUTPUT);
    pinMode(_resetPin, OUTPUT);

    _logger.debug("Setting up SPI", "ade7953::begin");
    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);
    SPI.setClockDivider(SPI_CLOCK_DIV64); // 64div -> 250kHz on 16MHz clock, but on 80MHz clock it's 1.25MHz. Max Ade7953 clock is 2MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    digitalWrite(_ssPin, HIGH);

    _logger.debug("Verifying communication with Ade7953...", "ade7953::begin");
    if (!_verifyCommunication()) {
        _logger.error("Failed to communicate with Ade7953", "ade7953::begin");
        return false;
    }
    _logger.debug("Successfully initialized Ade7953", "ade7953::begin");
    
    _logger.debug("Setting optimum settings...", "ade7953::begin");
    _setOptimumSettings();
    _logger.debug("Successfully set optimum settings", "ade7953::begin");

    _logger.debug("Setting default configuration...", "ade7953::begin");
    _setDefaultLycmode();
    _logger.debug("Successfully set default configuration", "ade7953::begin");    

    _logger.debug("Setting default no-load feature...", "ade7953::begin");
    _setDefaultNoLoadFeature();
    _logger.debug("Successfully set no-load feature", "ade7953::begin");
    
    _logger.debug("Setting default config register...", "ade7953::begin");
    _setDefaultConfigRegister();
    _logger.debug("Successfully set config register", "ade7953::begin");

    _logger.debug("Setting PGA gains...", "ade7953::begin");
    _setDefaultPgaGain();
    _logger.debug("Successfully set PGA gains", "ade7953::begin");

    _logger.debug("Setting configuration from spiffs...", "ade7953::begin");
    _setConfigurationFromSpiffs();
    _logger.debug("Done setting configuration from spiffs", "ade7953::begin");
 
    _logger.debug("Reading calibration values from SPIFFS...", "ade7953::begin");
    _setCalibrationValuesFromSpiffs();
    _logger.debug("Done reading calibration values from SPIFFS", "ade7953::begin");

    _logger.debug("Reading energy from SPIFFS...", "ade7953::begin");
    _setEnergyFromSpiffs();
    _logger.debug("Done reading energy from SPIFFS", "ade7953::begin");

    _logger.debug("Reading channel data from SPIFFS...", "ade7953::begin");
    _setChannelDataFromSpiffs();
    _logger.debug("Done reading channel data from SPIFFS", "ade7953::begin");

    return true;
}

void Ade7953::_setDefaultPgaGain()
{
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, VOLTAGE_MEASUREMENT);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, CURRENT_MEASUREMENT);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_B, CURRENT_MEASUREMENT);
}

void Ade7953::_setDefaultNoLoadFeature()
{
    writeRegister(DISNOLOAD_8, 8, DEFAULT_DISNOLOAD_REGISTER);

    writeRegister(AP_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);
    writeRegister(VAR_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);
    writeRegister(VA_NOLOAD_32, 32, DEFAULT_X_NOLOAD_REGISTER);

}

void Ade7953::_setDefaultLycmode()
{
    writeRegister(LCYCMODE_8, 8, DEFAULT_LCYCMODE_REGISTER);
}

void Ade7953::_setDefaultConfigRegister()
{
    writeRegister(CONFIG_16, 16, DEFAULT_CONFIG_REGISTER);
}

/**
 * According to the datasheet, setting these registers is mandatory for optimal operation
 */
void Ade7953::_setOptimumSettings()
{
    writeRegister(0x00FE, 8, 0xAD);
    writeRegister(0x0120, 16, 0x0030);
}

void Ade7953::loop() {
    if (millis() - _lastMillisSaveEnergy > SAVE_ENERGY_INTERVAL) {
        _lastMillisSaveEnergy = millis();
        saveEnergy();
    }
}

void Ade7953::_reset() {
    _logger.debug("Resetting Ade7953", "ade7953::_reset");
    digitalWrite(_resetPin, LOW);
    delay(200);
    digitalWrite(_resetPin, HIGH);
    delay(200);
}

/**
 * Verifies the communication with the Ade7953 device.
 * This function reads a specific register from the device and checks if it matches the default value.
 * 
 * @return true if the communication with the Ade7953 is successful, false otherwise.
 */
bool Ade7953::_verifyCommunication() {
    _logger.debug("Verifying communication with Ade7953...", "ade7953::begin");
    int _attempt = 0;
    bool _success = false;

    while (_attempt < MAX_VERIFY_COMMUNICATION_ATTEMPTS && !_success) {
        _logger.debug("Attempt (%d/%d) to communicate with Ade7953", "ade7953::begin", _attempt, MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        
        _reset();
        _attempt++;

        if ((readRegister(AP_NOLOAD_32, 32, false)) == DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
            _logger.debug("Communication successful with Ade7953", "ade7953::begin");
            return true;
        } else {
            _logger.warning("Failed to communicate with Ade7953 on _attempt (%d/%d)", "ade7953::begin", _attempt, MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        }
    }

    _logger.error("Failed to communicate with Ade7953 after %d attempts", "ade7953::begin", MAX_VERIFY_COMMUNICATION_ATTEMPTS);
    return false;
}

// Configuration
// --------------------

void Ade7953::setDefaultConfiguration() {
    _logger.debug("Setting default configuration...", "ade7953::setDefaultConfiguration");

    // Fetch JSON from flashed binary
    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_ade7953_json);

    serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    _applyConfiguration(_jsonDocument);

    _logger.debug("Default configuration set", "ade7953::setDefaultConfiguration");
}

void Ade7953::_setConfigurationFromSpiffs() {
    _logger.debug("Setting configuration from SPIFFS...", "ade7953::_setConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read configuration from SPIFFS. Keeping default one", "ade7953::_setConfigurationFromSpiffs");
        setDefaultConfiguration();
    } else {
        setConfiguration(_jsonDocument);
        _logger.debug("Successfully read and set configuration from SPIFFS", "ade7953::_setConfigurationFromSpiffs");
    }
}

void Ade7953::setConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Setting configuration...", "ade7953::setConfiguration");
    
    if (!serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, jsonDocument)) {
        _logger.error("Failed to save configuration to SPIFFS. Keeping previous configuration", "ade7953::setConfiguration");
    } else {
        _applyConfiguration(jsonDocument);
        _logger.debug("Successfully saved configuration to SPIFFS", "ade7953::setConfiguration");
    }
}

void Ade7953::_applyConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Applying configuration...", "ade7953::_applyConfiguration");
    
    _setGain(jsonDocument["aVGain"].as<long>(), CHANNEL_A, VOLTAGE_MEASUREMENT);
    // Channel B voltage gain should not be set as by datasheet

    _setGain(jsonDocument["aIGain"].as<long>(), CHANNEL_A, CURRENT_MEASUREMENT);
    _setGain(jsonDocument["bIGain"].as<long>(), CHANNEL_B, CURRENT_MEASUREMENT);

    _setOffset(jsonDocument["aIRmsOs"].as<long>(), CHANNEL_A, CURRENT_MEASUREMENT);
    _setOffset(jsonDocument["bIRmsOs"].as<long>(), CHANNEL_B, CURRENT_MEASUREMENT);

    _setGain(jsonDocument["aWGain"].as<long>(), CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    _setGain(jsonDocument["bWGain"].as<long>(), CHANNEL_B, ACTIVE_POWER_MEASUREMENT);

    _setOffset(jsonDocument["aWattOs"].as<long>(), CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    _setOffset(jsonDocument["bWattOs"].as<long>(), CHANNEL_B, ACTIVE_POWER_MEASUREMENT);

    _setGain(jsonDocument["aVarGain"].as<long>(), CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    _setGain(jsonDocument["bVarGain"].as<long>(), CHANNEL_B, REACTIVE_POWER_MEASUREMENT);

    _setOffset(jsonDocument["aVarOs"].as<long>(), CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    _setOffset(jsonDocument["bVarOs"].as<long>(), CHANNEL_B, REACTIVE_POWER_MEASUREMENT);

    _setGain(jsonDocument["aVaGain"].as<long>(), CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    _setGain(jsonDocument["bVaGain"].as<long>(), CHANNEL_B, APPARENT_POWER_MEASUREMENT);

    _setOffset(jsonDocument["aVaOs"].as<long>(), CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    _setOffset(jsonDocument["bVaOs"].as<long>(), CHANNEL_B, APPARENT_POWER_MEASUREMENT);

    _setPhaseCalibration(jsonDocument["phCalA"].as<long>(), CHANNEL_A);
    _setPhaseCalibration(jsonDocument["phCalB"].as<long>(), CHANNEL_B);

    _logger.debug("Successfully applied configuration", "ade7953::_applyConfiguration");
}

// Calibration values
// --------------------

void Ade7953::setDefaultCalibrationValues() {
    _logger.debug("Setting default calibration values", "ade7953::setDefaultCalibrationValues");
    
    // Fetch JSON from flashed binary
    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_calibration_json);

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    setCalibrationValues(_jsonDocument);

    _logger.debug("Successfully set default calibration values", "ade7953::setDefaultCalibrationValues");
}

void Ade7953::setCalibrationValues(JsonDocument &jsonDocument) {
    _logger.debug("Setting new calibration values", "ade7953::setCalibrationValues");

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

    _updateDataChannel();

    _logger.debug("Successfully set new calibration values", "ade7953::setCalibrationValues");
}

void Ade7953::_setCalibrationValuesFromSpiffs() {
    _logger.debug("Setting calibration values from SPIFFS", "ade7953::_setCalibrationValuesFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read calibration values from SPIFFS. Setting default ones...", "ade7953::_setCalibrationValuesFromSpiffs");
        setDefaultCalibrationValues();
    } else {
        _logger.debug("Successfully read calibration values from SPIFFS. Setting values...", "ade7953::_setCalibrationValuesFromSpiffs");
        setCalibrationValues(_jsonDocument);
    }
}

void Ade7953::_jsonToCalibrationValues(JsonDocument &jsonDocument, CalibrationValues &calibrationValues) {
    _logger.debug("Parsing JSON calibration values...", "ade7953::_jsonToCalibrationValues");

    calibrationValues.label = jsonDocument["label"].as<String>();
    calibrationValues.vLsb = jsonDocument["vLsb"].as<float>();
    calibrationValues.aLsb = jsonDocument["aLsb"].as<float>();
    calibrationValues.wLsb = jsonDocument["wLsb"].as<float>();
    calibrationValues.varLsb = jsonDocument["varLsb"].as<float>();
    calibrationValues.vaLsb = jsonDocument["vaLsb"].as<float>();
    calibrationValues.whLsb = jsonDocument["whLsb"].as<float>();
    calibrationValues.varhLsb = jsonDocument["varhLsb"].as<float>();
    calibrationValues.vahLsb = jsonDocument["vahLsb"].as<float>();

    _logger.debug("Successfully parsed JSON calibration values", "ade7953::_jsonToCalibrationValues");
}

// Data channel
// --------------------

void Ade7953::setDefaultChannelData() {
    _logger.debug("Setting default data channel %s", "ade7953::setDefaultChannelData", default_config_channel_json);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    setChannelData(parseJsonChannelData(_jsonDocument));    

    _logger.debug("Successfully initialized data channel", "ade7953::setDefaultChannelData");
}

void Ade7953::_setChannelDataFromSpiffs() {
    _logger.debug("Setting data channel from SPIFFS...", "ade7953::_setChannelDataFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read data channel from SPIFFS. Setting default one", "ade7953::_setChannelDataFromSpiffs");
        setDefaultChannelData();
    } else {
        _logger.debug("Successfully read data channel from SPIFFS. Setting values...", "ade7953::_setChannelDataFromSpiffs");
        setChannelData(parseJsonChannelData(_jsonDocument));
    }

    _updateDataChannel();

    _logger.debug("Successfully set data channel from SPIFFS", "ade7953::_setChannelDataFromSpiffs");
}

void Ade7953::setChannelData(ChannelData* newChannelData) {
    _logger.debug("Setting data channel", "ade7953::setChannelData");
    
    for(int i = 0; i < CHANNEL_COUNT; i++) {
        channelData[i] = newChannelData[i];
    }
    saveChannelDataToSpiffs();

    _logger.debug("Successfully set data channel", "ade7953::setChannelData");
}

bool Ade7953::saveChannelDataToSpiffs() {
    _logger.debug("Saving data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs");

    JsonDocument _jsonDocument = channelDataToJson();

    if (serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument)) {
        _logger.debug("Successfully saved data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs");
        return true;
    } else {
        _logger.error("Failed to save data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs");
        return false;
    }
}

JsonDocument Ade7953::channelDataToJson() {
    _logger.debug("Converting data channel to JSON", "ade7953::channelDataToJson");

    JsonDocument _jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["active"] = channelData[i].active;
        _jsonDocument[String(i)]["reverse"] = channelData[i].reverse;
        _jsonDocument[String(i)]["label"] = channelData[i].label;
        _jsonDocument[String(i)]["calibrationLabel"] = channelData[i].calibrationValues.label;
    }

    _logger.debug("Successfully converted data channel to JSON", "ade7953::channelDataToJson");
    return _jsonDocument;
}

ChannelData* Ade7953::parseJsonChannelData(JsonDocument jsonDocument) {
    _logger.debug("Parsing JSON data channel", "ade7953::parseJsonChannelData");

    ChannelData* _channelData = new ChannelData[CHANNEL_COUNT];

    JsonObject _jsonObject = jsonDocument.as<JsonObject>();

    for (JsonPair _kv : _jsonObject) {
        int _index = atoi(_kv.key().c_str());

        // Check if _index is within bounds
        if (_index < 0 || _index >= CHANNEL_COUNT) {
            _logger.error("Index out of bounds: %d", "ade7953::parseJsonChannelData", _index);
            continue;
        }

        _logger.debug("Parsing data channel %d", "ade7953::parseJsonChannelData", _index);

        _channelData[_index].index = _index;
        _channelData[_index].active = _kv.value()["active"].as<bool>();
        _channelData[_index].reverse = _kv.value()["reverse"].as<bool>();
        _channelData[_index].label = _kv.value()["label"].as<String>();
        _channelData[_index].calibrationValues = _calibrationValuesFromSpiffs(_kv.value()["calibrationLabel"].as<String>());
    }

    _logger.debug("Successfully parsed JSON data channel", "ade7953::parseJsonChannelData");
    return _channelData;
}

void Ade7953::_updateDataChannel() {
    _logger.debug("Updating data channel...", "ade7953::_updateDataChannel");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read calibration values from SPIFFS. Keeping previous values", "ade7953::_updateDataChannel");
        return;
    }
    
    String _calibrationValuesLabel;
    JsonDocument _jsonDocumentPreviousLabel;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _calibrationValuesLabel = channelData[i].calibrationValues.label;
        
        if (_jsonDocument.containsKey(_calibrationValuesLabel)) {
            _jsonDocumentPreviousLabel = _jsonDocument[_calibrationValuesLabel];
            _jsonToCalibrationValues(_jsonDocumentPreviousLabel, channelData[i].calibrationValues);
        } else {
            _logger.error("Calibration label %s not found in calibration JSON", "ade7953::_updateDataChannel", _calibrationValuesLabel);
        }
    }
    
    _updateSampleTime();

    _logger.debug("Successfully updated data channel", "ade7953::_updateDataChannel");
}

void Ade7953::_updateSampleTime() {
    _logger.debug("Updating sample time", "ade7953::updateSampleTime");

    int _activeChannelCount = _getActiveChannelCount();

    if (_activeChannelCount > 0) {
        long _linecyc = long(DEFAULT_SAMPLE_CYCLES / _activeChannelCount);
        
        _setLinecyc(_linecyc);

        _logger.debug("Successfully set sample to %d line cycles", "ade7953::updateSampleTime", _linecyc); 
    } else {
        _logger.warning("No active channels found, sample time not updated", "ade7953::updateSampleTime");
    }
}

int Ade7953::findNextActiveChannel(int currentChannel) {
    for (int i = currentChannel + 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            return i;
        }
    }
    for (int i = 0; i < currentChannel; i++) {
        if (channelData[i].active) {
            return i;
        }
    }
    _logger.verbose("No active channel found, returning current channel", "ade7953::findNextActiveChannel");
    return currentChannel;
}

int Ade7953::_getActiveChannelCount() {
    int _activeChannelCount = 0;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            _activeChannelCount++;
        }
    }

    _logger.debug("Found %d active channels", "ade7953::_getActiveChannelCount", _activeChannelCount);
    return _activeChannelCount;
}


// Meter values
// --------------------

void Ade7953::readMeterValues(int channel) {
    long _currentMillis = millis();
    long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
    meterValues[channel].lastMillis = _currentMillis;

    int _ade7953Channel = (channel == 0) ? CHANNEL_A : CHANNEL_B;

    float _voltage = _readVoltageRms() * channelData[channel].calibrationValues.vLsb;
    float _current = _readCurrentRms(_ade7953Channel) * channelData[channel].calibrationValues.aLsb;
    float _activePower = _readActivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.wLsb * (channelData[channel].reverse ? -1 : 1);
    float _reactivePower = _readReactivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.varLsb * (channelData[channel].reverse ? -1 : 1);
    float _apparentPower = _readApparentPowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.vaLsb;
    float _powerFactor = _readPowerFactor(_ade7953Channel) * POWER_FACTOR_CONVERSION_FACTOR * (channelData[channel].reverse ? -1 : 1);

    meterValues[channel].voltage = _validateVoltage(meterValues[channel].voltage, _voltage);
    meterValues[channel].current = _validateCurrent(meterValues[channel].current, _current);
    meterValues[channel].activePower = _validatePower(meterValues[channel].activePower, _activePower);
    meterValues[channel].reactivePower = _validatePower(meterValues[channel].reactivePower, _reactivePower);
    meterValues[channel].apparentPower = _validatePower(meterValues[channel].apparentPower, _apparentPower);
    meterValues[channel].powerFactor = _validatePowerFactor(meterValues[channel].powerFactor, _powerFactor);
    
    float _activeEnergy = _readActiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.whLsb;
    float _reactiveEnergy = _readReactiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.varhLsb;
    float _apparentEnergy = _readApparentEnergy(_ade7953Channel) * channelData[channel].calibrationValues.vahLsb;

    if (_activeEnergy != 0.0) {
        meterValues[channel].activeEnergy += meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0; // W * ms * s / 1000 ms * h / 3600 s = Wh
    } else {
        meterValues[channel].activePower = 0.0;
        meterValues[channel].powerFactor = 0.0;
    }

    if (_reactiveEnergy != 0.0) {
        meterValues[channel].reactiveEnergy += meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0; // var * ms * s / 1000 ms * h / 3600 s = VArh
    } else {
        meterValues[channel].reactivePower = 0.0;
    }

    if (_apparentEnergy != 0.0) {
        meterValues[channel].apparentEnergy += meterValues[channel].apparentPower * _deltaMillis / 1000.0 / 3600.0; // VA * ms * s / 1000 ms * h / 3600 s = VAh
    } else {
        meterValues[channel].current = 0.0;
        meterValues[channel].apparentPower = 0.0;
    }
}

float Ade7953::_validateValue(float oldValue, float newValue, float min, float max) {
    if (newValue < min || newValue > max) {
        _logger.warning("Value %f out of range (minimum: %f, maximum: %f). Keeping old value %f", "ade7953::_validateValue", newValue, min, max, oldValue);
        return oldValue;
    }
    return newValue;
}

float Ade7953::_validateVoltage(float oldValue, float newValue) {
    return _validateValue(oldValue, newValue, VALIDATE_VOLTAGE_MIN, VALIDATE_VOLTAGE_MAX);
}

float Ade7953::_validateCurrent(float oldValue, float newValue) {
    return _validateValue(oldValue, newValue, VALIDATE_CURRENT_MIN, VALIDATE_CURRENT_MAX);
}

float Ade7953::_validatePower(float oldValue, float newValue) {
    return _validateValue(oldValue, newValue, VALIDATE_POWER_MIN, VALIDATE_POWER_MAX);
}

float Ade7953::_validatePowerFactor(float oldValue, float newValue) {
    return _validateValue(oldValue, newValue, VALIDATE_POWER_FACTOR_MIN, VALIDATE_POWER_FACTOR_MAX);
}

JsonDocument Ade7953::singleMeterValuesToJson(int index) {
    JsonDocument _jsonDocument;

    JsonObject _jsonValues = _jsonDocument.to<JsonObject>();

    _jsonValues["voltage"] = meterValues[index].voltage;
    _jsonValues["current"] = meterValues[index].current;
    _jsonValues["activePower"] = meterValues[index].activePower;
    _jsonValues["apparentPower"] = meterValues[index].apparentPower;
    _jsonValues["reactivePower"] = meterValues[index].reactivePower;
    _jsonValues["powerFactor"] = meterValues[index].powerFactor;
    _jsonValues["activeEnergy"] = meterValues[index].activeEnergy;
    _jsonValues["reactiveEnergy"] = meterValues[index].reactiveEnergy;
    _jsonValues["apparentEnergy"] = meterValues[index].apparentEnergy;

    return _jsonDocument;
}


JsonDocument Ade7953::meterValuesToJson() {
    JsonDocument _jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            JsonObject _jsonChannel = _jsonDocument.add<JsonObject>();
            _jsonChannel["index"] = channelData[i].index;
            _jsonChannel["label"] = channelData[i].label;
            _jsonChannel["data"] = singleMeterValuesToJson(i);
        }
    }

    return _jsonDocument;
}

// Energy
// --------------------

void Ade7953::_setEnergyFromSpiffs() {
    _logger.debug("Reading energy from SPIFFS", "ade7953::readEnergyFromSpiffs");
    
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read energy from SPIFFS", "ade7953::readEnergyFromSpiffs");
    } else {
        _logger.debug("Successfully read energy from SPIFFS", "ade7953::readEnergyFromSpiffs");

        for (int i = 0; i < CHANNEL_COUNT; i++) {
            meterValues[i].activeEnergy = _jsonDocument[String(i)]["activeEnergy"].as<float>();
            meterValues[i].reactiveEnergy = _jsonDocument[String(i)]["reactiveEnergy"].as<float>();
            meterValues[i].apparentEnergy = _jsonDocument[String(i)]["apparentEnergy"].as<float>();
        }
    }
}

void Ade7953::saveEnergy() {
    _logger.debug("Saving energy...", "ade7953::saveEnergy");

    _saveEnergyToSpiffs();
    _saveDailyEnergyToSpiffs();

    _logger.debug("Successfully saved energy", "ade7953::saveEnergy");
}

void Ade7953::_saveEnergyToSpiffs() {
    _logger.debug("Saving energy to SPIFFS...", "ade7953::saveEnergyToSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["activeEnergy"] = meterValues[i].activeEnergy;
        _jsonDocument[String(i)]["reactiveEnergy"] = meterValues[i].reactiveEnergy;
        _jsonDocument[String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) {
        _logger.debug("Successfully saved energy to SPIFFS", "ade7953::saveEnergyToSpiffs");
    } else {
        _logger.error("Failed to save energy to SPIFFS", "ade7953::saveEnergyToSpiffs");
    }
}

void Ade7953::_saveDailyEnergyToSpiffs() {
    _logger.debug("Saving daily energy to SPIFFS...", "ade7953::saveDailyEnergyToSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument);
    
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    char _currentDate[11];
    strftime(_currentDate, sizeof(_currentDate), "%Y-%m-%d", timeinfo);

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            _jsonDocument[_currentDate][String(i)]["activeEnergy"] = meterValues[i].activeEnergy;
            _jsonDocument[_currentDate][String(i)]["reactiveEnergy"] = meterValues[i].reactiveEnergy;
            _jsonDocument[_currentDate][String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
        }
    }

    if (serializeJsonToSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument)) {
        _logger.debug("Successfully saved daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs");
    } else {
        _logger.error("Failed to save daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs");
    }
}

void Ade7953::resetEnergyValues() {
    _logger.warning("Resetting energy values to 0", "ade7953::resetEnergyValues");

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        meterValues[i].activeEnergy = 0.0;
        meterValues[i].reactiveEnergy = 0.0;
        meterValues[i].apparentEnergy = 0.0;
    }

    saveEnergy();
}


// Others
// --------------------

void Ade7953::_setLinecyc(long linecyc) {
    linecyc = min(max(linecyc, 10L), 1000L); // Linecyc must be between reasonable values, 10 and 1000

    _logger.debug(
        "Setting linecyc to %d",
        "ade7953::_setLinecyc",
        linecyc
    );

    writeRegister(LINECYC_16, 16, linecyc);
}

void Ade7953::_setPhaseCalibration(long phaseCalibration, int channel) {
    _logger.debug(
        "Setting phase calibration to %d on channel %d",
        "ade7953::_setPhaseCalibration",
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
        "ade7953::_setPgaGain",
        pgaGain,
        channel
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(PGA_V_8, 8, pgaGain);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(PGA_IA_8, 8, pgaGain);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(PGA_V_8, 8, pgaGain);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(PGA_IB_8, 8, pgaGain);
                break;
        }
    }
}

void Ade7953::_setGain(long gain, int channel, int measurementType) {
    _logger.debug(
        "Setting gain to %d on channel %d for measurement type %d",
        "ade7953::_setGain",
        gain,
        channel,
        measurementType
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(AVGAIN_32, 32, gain);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(AIGAIN_32, 32, gain);
                break;
            case ACTIVE_POWER_MEASUREMENT:
                writeRegister(AWGAIN_32, 32, gain);
                break;
            case REACTIVE_POWER_MEASUREMENT:
                writeRegister(AVARGAIN_32, 32, gain);
                break;
            case APPARENT_POWER_MEASUREMENT:
                writeRegister(AVAGAIN_32, 32, gain);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(AVGAIN_32, 32, gain);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(BIGAIN_32, 32, gain);
                break;
            case ACTIVE_POWER_MEASUREMENT:
                writeRegister(BWGAIN_32, 32, gain);
                break;
            case REACTIVE_POWER_MEASUREMENT:
                writeRegister(BVARGAIN_32, 32, gain);
                break;
            case APPARENT_POWER_MEASUREMENT:
                writeRegister(BVAGAIN_32, 32, gain);
                break;
        }
    }
}

void Ade7953::_setOffset(long offset, int channel, int measurementType) {
    _logger.debug(
        "Setting offset to %ld on channel %d for measurement type %d",
        "ade7953::_setOffset",
        offset,
        channel,
        measurementType
    );

    if (channel == CHANNEL_A) {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(VRMSOS_32, 32, offset);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(AIRMSOS_32, 32, offset);
                break;
            case ACTIVE_POWER_MEASUREMENT:
                writeRegister(AWATTOS_32, 32, offset);
                break;
            case REACTIVE_POWER_MEASUREMENT:
                writeRegister(AVAROS_32, 32, offset);
                break;
            case APPARENT_POWER_MEASUREMENT:
                writeRegister(AVAOS_32, 32, offset);
                break;
        }
    } else {
        switch (measurementType) {
            case VOLTAGE_MEASUREMENT:
                writeRegister(VRMSOS_32, 32, offset);
                break;
            case CURRENT_MEASUREMENT:
                writeRegister(BIRMSOS_32, 32, offset);
                break;
            case ACTIVE_POWER_MEASUREMENT:
                writeRegister(BWATTOS_32, 32, offset);
                break;
            case REACTIVE_POWER_MEASUREMENT:
                writeRegister(BVAROS_32, 32, offset);
                break;
            case APPARENT_POWER_MEASUREMENT:
                writeRegister(BVAOS_32, 32, offset);
                break;
        }
    }
}

long Ade7953::_readApparentPowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVA_32, 32, true);} 
    else {return readRegister(BVA_32, 32, true);}
}

long Ade7953::_readActivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AWATT_32, 32, true);} 
    else {return readRegister(BWATT_32, 32, true);}
}

long Ade7953::_readReactivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVAR_32, 32, true);} 
    else {return readRegister(BVAR_32, 32, true);}
}

long Ade7953::_readCurrentInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IA_32, 32, true);} 
    else {return readRegister(IB_32, 32, true);}
}

long Ade7953::_readVoltageInstantaneous() {
    return readRegister(V_32, 32, true);
}

long Ade7953::_readCurrentRms(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IRMSA_32, 32, false);} 
    else {return readRegister(IRMSB_32, 32, false);}
}

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

/**
 * Checks if the line cycle has finished.
 * 
 * This function reads the RSTIRQSTATA_32 register and checks the 18th bit to determine if the line cycle has finished.
 * 
 * @return true if the line cycle has finished, false otherwise.
 */
bool Ade7953::isLinecycFinished() {
    return (readRegister(RSTIRQSTATA_32, 32, false) & (1 << 18)) != 0;
}

/**
 * Reads the value from a register in the ADE7953 energy meter.
 * 
 * @param registerAddress The address of the register to read from. Expected range: 0 to 65535
 * @param numBits The number of bits to read from the register. Expected values: 8, 16, 24 or 32.
 * @param isSignedData Flag indicating whether the data is signed (true) or unsigned (false).
 * @return The value read from the register.
 */
long Ade7953::readRegister(long registerAddress, int nBits, bool signedData) {
    digitalWrite(_ssPin, LOW);

    SPI.transfer(registerAddress >> 8);
    SPI.transfer(registerAddress & 0xFF);
    SPI.transfer(READ_TRANSFER);

    byte _response[nBits / 8];
    for (int i = 0; i < nBits / 8; i++) {
        _response[i] = SPI.transfer(READ_TRANSFER);
    }

    digitalWrite(_ssPin, HIGH);

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
        "ade7953::readRegister",
        _long_response,
        registerAddress,
        nBits
    );

    return _long_response;
}

/**
 * Writes data to a register in the ADE7953 energy meter.
 * 
 * @param registerAddress The address of the register to write to. (16-bit value)
 * @param nBits The number of bits in the register. (8, 16, 24, or 32)
 * @param data The data to write to the register. (nBits-bit value)
 */
void Ade7953::writeRegister(long registerAddress, int nBits, long data) {
    _logger.debug(
        "Writing %l to register %d with %d bits",
        "ade7953::writeRegister",
        data,
        registerAddress,
        nBits
    );   

    digitalWrite(_ssPin, LOW);

    SPI.transfer(registerAddress >> 8);
    SPI.transfer(registerAddress & 0xFF);
    SPI.transfer(WRITE_TRANSFER);

    if (nBits / 8 == 4) {
        SPI.transfer((data >> 24) & 0xFF);
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
    float sum = 0.0f;
    int activeChannelCount = 0;

    for (int i = includeChannel0 ? 0 : 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            sum += meterValues[i].powerFactor;
            activeChannelCount++;
        }
    }
    return activeChannelCount > 0 ? sum / activeChannelCount : 0.0f;
}