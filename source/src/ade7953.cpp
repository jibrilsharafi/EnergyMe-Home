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

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];
}

bool Ade7953::begin() {
    _logger.debug("Initializing Ade7953", "ade7953::begin");

    _logger.debug("Setting up hardware pins...", "ade7953::begin");
    _setHardwarePins();
    _logger.debug("Successfully set up hardware pins", "ade7953::begin");

    _logger.debug("Verifying communication with Ade7953...", "ade7953::begin");
    if (!_verifyCommunication()) {
        _logger.error("Failed to communicate with Ade7953", "ade7953::begin");
        return false;
    }
    _logger.debug("Successfully initialized Ade7953", "ade7953::begin");
    
    _logger.debug("Setting optimum settings...", "ade7953::begin");
    _setOptimumSettings();
    _logger.debug("Successfully set optimum settings", "ade7953::begin");

    _logger.debug("Setting default parameters...", "ade7953::begin");
    _setDefaultParameters();
    _logger.debug("Successfully set default parameters", "ade7953::begin");

    if (isFirstSetup) {
        _logger.info("First setup detected. Setting everything default...", "ade7953::begin");

        _logger.debug("Setting default configuration...", "ade7953::begin");
        setDefaultConfiguration();
        _logger.debug("Done setting default configuration", "ade7953::begin");
        
        _logger.debug("Setting default calibration values...", "ade7953::begin");
        _setDefaultCalibrationValuesOnly();
        _logger.debug("Done setting default calibration values", "ade7953::begin");

        _logger.debug("Setting default data channel...", "ade7953::begin");
        setDefaultChannelData();
        _logger.debug("Done setting default data channel", "ade7953::begin");

        // No need to set the default energy as it is initialized to 0.0

        // Save energy so to have the daily energy to 0.0 from the beginning
        _logger.debug("Saving energy...", "ade7953::begin");
        saveEnergy();
        _logger.debug("Done saving energy", "ade7953::begin");

        _logger.info("First setup done", "ade7953::begin");
    } else {
        _logger.debug("Setting configuration from SPIFFS...", "ade7953::begin");
        _setConfigurationFromSpiffs();
        _logger.debug("Done setting configuration from SPIFFS", "ade7953::begin");
    
        _logger.debug("Reading channel data from SPIFFS...", "ade7953::begin");
        _setChannelDataFromSpiffs();
        _logger.debug("Done reading channel data from SPIFFS", "ade7953::begin");

        _logger.debug("Reading calibration values from SPIFFS...", "ade7953::begin");
        _setCalibrationValuesFromSpiffs();
        _logger.debug("Done reading calibration values from SPIFFS", "ade7953::begin");

        _logger.debug("Reading energy from SPIFFS...", "ade7953::begin");
        _setEnergyFromSpiffs();
        _logger.debug("Done reading energy from SPIFFS", "ade7953::begin");
    }

    return true;
}

void Ade7953::_setHardwarePins() {
    _logger.debug("Setting hardware pins...", "ade7953::_setHardwarePins");

    pinMode(_ssPin, OUTPUT);
    pinMode(_sckPin, OUTPUT);
    pinMode(_misoPin, INPUT);
    pinMode(_mosiPin, OUTPUT);
    pinMode(_resetPin, OUTPUT);

    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);
    SPI.setClockDivider(SPI_CLOCK_DIV64); // 64div -> 250kHz on 16MHz clock, but on 80MHz clock it's 1.25MHz. Max Ade7953 clock is 2MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    digitalWrite(_ssPin, HIGH);

    _logger.debug("Successfully set hardware pins", "ade7953::_setHardwarePins");
}

void Ade7953::_setDefaultParameters()
{
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, VOLTAGE_MEASUREMENT);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, CURRENT_MEASUREMENT);
    _setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_B, CURRENT_MEASUREMENT);

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
    _logger.debug("Verifying communication with Ade7953...", "ade7953::_verifyCommunication");
    
    int _attempt = 0;
    bool _success = false;
    unsigned long _lastMillisAttempt = 0;

    while (_attempt < ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS && !_success) {
        if (millis() - _lastMillisAttempt < ADE7953_VERIFY_COMMUNICATION_INTERVAL) {
            continue;
        }

        _logger.debug("Attempt (%d/%d) to communicate with Ade7953", "ade7953::_verifyCommunication", _attempt, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        
        _reset();
        _attempt++;
        _lastMillisAttempt = millis();

        if ((readRegister(AP_NOLOAD_32, 32, false)) == DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
            _logger.debug("Communication successful with Ade7953", "ade7953::_verifyCommunication");
            return true;
        } else {
            _logger.warning("Failed to communicate with Ade7953 on _attempt (%d/%d). Retrying in %d ms", "ade7953::_verifyCommunication", _attempt, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS, ADE7953_VERIFY_COMMUNICATION_INTERVAL);
        }
    }

    _logger.error("Failed to communicate with Ade7953 after %d attempts", "ade7953::_verifyCommunication", ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
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

bool Ade7953::setConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Setting configuration...", "ade7953::setConfiguration");

    if (!_validateConfigurationJson(jsonDocument)) {
        _logger.warning("Invalid configuration JSON. Keeping previous configuration", "ade7953::setConfiguration");
        return false;
    }
    
    if (!serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, jsonDocument)) {
        _logger.error("Failed to save configuration to SPIFFS. Keeping previous configuration", "ade7953::setConfiguration");
        return false;
    } else {
        _applyConfiguration(jsonDocument);
        _logger.debug("Successfully saved configuration to SPIFFS", "ade7953::setConfiguration");
        return true;
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

bool Ade7953::_validateConfigurationJson(JsonDocument& jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {
        return false;
    }

    if (!jsonDocument.containsKey("aVGain") || !jsonDocument["aVGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("aIGain") || !jsonDocument["aIGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("bIGain") || !jsonDocument["bIGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("aIRmsOs") || !jsonDocument["aIRmsOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("bIRmsOs") || !jsonDocument["bIRmsOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("aWGain") || !jsonDocument["aWGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("bWGain") || !jsonDocument["bWGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("aWattOs") || !jsonDocument["aWattOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("bWattOs") || !jsonDocument["bWattOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("aVarGain") || !jsonDocument["aVarGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("bVarGain") || !jsonDocument["bVarGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("aVarOs") || !jsonDocument["aVarOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("bVarOs") || !jsonDocument["bVarOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("aVaGain") || !jsonDocument["aVaGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("bVaGain") || !jsonDocument["bVaGain"].is<long>()) return false;
    if (!jsonDocument.containsKey("aVaOs") || !jsonDocument["aVaOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("bVaOs") || !jsonDocument["bVaOs"].is<long>()) return false;
    if (!jsonDocument.containsKey("phCalA") || !jsonDocument["phCalA"].is<long>()) return false;
    if (!jsonDocument.containsKey("phCalB") || !jsonDocument["phCalB"].is<long>()) return false;

    return true;
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

void Ade7953::_setDefaultCalibrationValuesOnly() {
    _logger.debug("Setting default calibration values", "ade7953::_setDefaultCalibrationValuesOnly");
    
    // Fetch JSON from flashed binary
    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_calibration_json);

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    _logger.debug("Successfully set default calibration values", "ade7953::_setDefaultCalibrationValuesOnly");
}

bool Ade7953::setCalibrationValues(JsonDocument &jsonDocument) {
    _logger.debug("Setting new calibration values...", "ade7953::setCalibrationValues");

    if (!_validateCalibrationValuesJson(jsonDocument)) {
        _logger.warning("Invalid calibration JSON. Keeping previous calibration values", "ade7953::setCalibrationValues");
        return false;
    }

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

    _updateChannelData();

    _logger.debug("Successfully set new calibration values", "ade7953::setCalibrationValues");

    return true;
}

void Ade7953::_setCalibrationValuesFromSpiffs() {
    _logger.debug("Setting calibration values from SPIFFS", "ade7953::_setCalibrationValuesFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read calibration values from SPIFFS. Setting default ones...", "ade7953::_setCalibrationValuesFromSpiffs");
        _setDefaultCalibrationValuesOnly();
    } else {
        _logger.debug("Successfully read calibration values from SPIFFS. Setting values...", "ade7953::_setCalibrationValuesFromSpiffs");
        setCalibrationValues(_jsonDocument);
    }
}

void Ade7953::_jsonToCalibrationValues(JsonObject &jsonObject, CalibrationValues &calibrationValues) {
    _logger.verbose("Parsing JSON calibration values for label %s", "ade7953::_jsonToCalibrationValues", calibrationValues.label.c_str());

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
    if (!jsonDocument.is<JsonObject>()) {
        return false;
    }

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {
            return false;
        }

        JsonObject calibrationObject = kv.value().as<JsonObject>();

        if (!calibrationObject.containsKey("vLsb") || !calibrationObject["vLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("aLsb") || !calibrationObject["aLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("wLsb") || !calibrationObject["wLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("varLsb") || !calibrationObject["varLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("vaLsb") || !calibrationObject["vaLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("whLsb") || !calibrationObject["whLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("varhLsb") || !calibrationObject["varhLsb"].is<float>()) return false;
        if (!calibrationObject.containsKey("vahLsb") || !calibrationObject["vahLsb"].is<float>()) return false;
    }

    return true;
}

// Data channel
// --------------------

void Ade7953::setDefaultChannelData() {
    _logger.debug("Setting default data channel: %s...", "ade7953::setDefaultChannelData", default_config_channel_json);

    // Read JSON from flashed binary
    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    // Save default JSON to SPIFFS
    serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    // Parse JSON and set channel data
    setChannelData(_jsonDocument);

    _logger.debug("Successfully initialized data channel", "ade7953::setDefaultChannelData");
}

void Ade7953::_setChannelDataFromSpiffs() {
    _logger.debug("Setting data channel from SPIFFS...", "ade7953::_setChannelDataFromSpiffs");

    // Read the channel data JSON from SPIFFS
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read data channel from SPIFFS. Setting default one", "ade7953::_setChannelDataFromSpiffs");
        setDefaultChannelData();
    } else {
        _logger.debug("Successfully read data channel from SPIFFS. Setting values...", "ade7953::_setChannelDataFromSpiffs");

        // If the JSON is not empty, parse it and set the channelData
        setChannelData(_jsonDocument);
    }

    _logger.debug("Successfully set data channel from SPIFFS", "ade7953::_setChannelDataFromSpiffs");
}

bool Ade7953::_saveChannelDataToSpiffs() {
    _logger.debug("Saving data channel to SPIFFS...", "ade7953::_saveChannelDataToSpiffs");

    JsonDocument _jsonDocument;
    channelDataToJson(_jsonDocument);

    if (serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument)) {
        _logger.debug("Successfully saved data channel to SPIFFS", "ade7953::_saveChannelDataToSpiffs");
        return true;
    } else {
        _logger.error("Failed to save data channel to SPIFFS", "ade7953::_saveChannelDataToSpiffs");
        return false;
    }
}

void Ade7953::channelDataToJson(JsonDocument &jsonDocument) {
    _logger.debug("Converting data channel to JSON...", "ade7953::channelDataToJson");

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        jsonDocument[String(i)]["active"] = channelData[i].active;
        jsonDocument[String(i)]["reverse"] = channelData[i].reverse;
        jsonDocument[String(i)]["label"] = channelData[i].label;
        jsonDocument[String(i)]["calibrationLabel"] = channelData[i].calibrationValues.label;
    }

    _logger.debug("Successfully converted data channel to JSON", "ade7953::channelDataToJson");
}

bool Ade7953::setChannelData(JsonDocument &jsonDocument) {
    _logger.debug("Setting channel data...", "ade7953::setChannelData");

    if (!_validateChannelDataJson(jsonDocument)) {
        _logger.warning("Invalid JSON data channel. Keeping previous data channel", "ade7953::setChannelData");
        return false;
    }

    for (JsonPair _kv : jsonDocument.as<JsonObject>()) {
        _logger.verbose(
            "Parsing JSON data channel %s | Active: %d | Reverse: %d | Label: %s | Calibration Label: %s", 
            "ade7953::setChannelData", 
            _kv.key().c_str(), 
            _kv.value()["active"].as<bool>(), 
            _kv.value()["reverse"].as<bool>(), 
            _kv.value()["label"].as<String>(), 
            _kv.value()["calibrationLabel"].as<String>()
        );

        int _index = atoi(_kv.key().c_str());

        // Check if _index is within bounds
        if (_index < 0 || _index >= CHANNEL_COUNT) {
            _logger.error("Index out of bounds: %d", "ade7953::setChannelData", _index);
            continue;
        }

        channelData[_index].index = _index;
        channelData[_index].active = _kv.value()["active"].as<bool>();
        channelData[_index].reverse = _kv.value()["reverse"].as<bool>();
        channelData[_index].label = _kv.value()["label"].as<String>();
        channelData[_index].calibrationValues.label = _kv.value()["calibrationLabel"].as<String>();
    }
    _logger.debug("Successfully set data channel properties", "ade7953::setChannelData");

    // Add the calibration values to the channel data
    _updateChannelData();

    _saveChannelDataToSpiffs();

    publishMqtt.channel = true;

    _logger.debug("Successfully parsed JSON data channel", "ade7953::setChannelData");

    return true;
}

bool Ade7953::_validateChannelDataJson(JsonDocument &jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {
        return false;
    }

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {
            return false;
        }

        int _index = atoi(kv.key().c_str());
        if (_index < 0 || _index >= CHANNEL_COUNT) {
            return false;
        }

        JsonObject channelObject = kv.value().as<JsonObject>();

        if (!channelObject.containsKey("active") || !channelObject["active"].is<bool>()) return false;
        if (!channelObject.containsKey("reverse") || !channelObject["reverse"].is<bool>()) return false;
        if (!channelObject.containsKey("label") || !channelObject["label"].is<String>()) return false;
        if (!channelObject.containsKey("calibrationLabel") || !channelObject["calibrationLabel"].is<String>()) return false;
    }

    return true;
}

void Ade7953::_updateChannelData() {
    _logger.debug("Updating data channel...", "ade7953::_updateChannelData");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()) {
        _logger.error("Failed to read calibration values from SPIFFS. Keeping previous values", "ade7953::_updateChannelData");
        return;
    }
    
    for (int i = 0; i < CHANNEL_COUNT; i++) {        
        if (_jsonDocument.containsKey(channelData[i].calibrationValues.label)) {
            // Extract the corresponding calibration values from the JSON
            JsonObject _jsonCalibrationValues = _jsonDocument[channelData[i].calibrationValues.label].as<JsonObject>();

            // Set the calibration values for the channel
            _jsonToCalibrationValues(_jsonCalibrationValues, channelData[i].calibrationValues);
        } else {
            _logger.error(
                "Calibration label %s for channel %d not found in calibration JSON", 
                "ade7953::_updateChannelData", 
                channelData[i].calibrationValues.label.c_str(), 
                i
            );
        }
    }
    
    _updateSampleTime();

    _logger.debug("Successfully updated data channel", "ade7953::_updateChannelData");
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
            _jsonChannel["index"] = i;
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
    if (now < 1000000000) { // Any time less than 2001-09-09 01:46:40
        _logger.warning("Skipping saving daily energy as time is not set yet", "ade7953::saveDailyEnergyToSpiffs");
        return;
    }
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
        channel,
        measurementType
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
        "Setting gain to %ld on channel %d for measurement type %d",
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
        "Writing %ld to register %ld with %d bits",
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