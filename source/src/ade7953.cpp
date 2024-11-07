#include "ade7953.h"

Ade7953::Ade7953(
    int ssPin,
    int sckPin,
    int misoPin,
    int mosiPin,
    int resetPin,
    AdvancedLogger &logger,
    CustomTime &customTime,
    MainFlags &mainFlags) : _ssPin(ssPin),
                            _sckPin(sckPin),
                            _misoPin(misoPin),
                            _mosiPin(mosiPin),
                            _resetPin(resetPin),
                            _logger(logger),
                            _customTime(customTime),
                            _mainFlags(mainFlags)
{

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];
}

bool Ade7953::begin() {
    _logger.debug("Initializing Ade7953", "ade7953::begin");

    TRACE
    _logger.debug("Setting up hardware pins...", "ade7953::begin");
    _setHardwarePins();
    _logger.debug("Successfully set up hardware pins", "ade7953::begin");

    TRACE
    _logger.debug("Verifying communication with Ade7953...", "ade7953::begin");
    if (!_verifyCommunication()) {
        _logger.error("Failed to communicate with Ade7953", "ade7953::begin");
        return false;
    }
    _logger.debug("Successfully initialized Ade7953", "ade7953::begin");
    
    TRACE
    _logger.debug("Setting optimum settings...", "ade7953::begin");
    _setOptimumSettings();
    _logger.debug("Successfully set optimum settings", "ade7953::begin");

    TRACE
    _logger.debug("Setting default parameters...", "ade7953::begin");
    _setDefaultParameters();
    _logger.debug("Successfully set default parameters", "ade7953::begin");

    TRACE
    _logger.debug("Setting configuration from SPIFFS...", "ade7953::begin");
    _setConfigurationFromSpiffs();
    _logger.debug("Done setting configuration from SPIFFS", "ade7953::begin");

    TRACE
    _logger.debug("Reading channel data from SPIFFS...", "ade7953::begin");
    _setChannelDataFromSpiffs();
    _logger.debug("Done reading channel data from SPIFFS", "ade7953::begin");

    TRACE
    _logger.debug("Reading calibration values from SPIFFS...", "ade7953::begin");
    _setCalibrationValuesFromSpiffs();
    _logger.debug("Done reading calibration values from SPIFFS", "ade7953::begin");

    TRACE
    _logger.debug("Reading energy from SPIFFS...", "ade7953::begin");
    _setEnergyFromSpiffs();
    _logger.debug("Done reading energy from SPIFFS", "ade7953::begin");

    TRACE
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
        (
            restartConfiguration.isRequired && 
            restartConfiguration.functionName != "utils::factoryReset"
        )
    ) {
        _lastMillisSaveEnergy = millis();
        saveEnergy();
    }
}

void Ade7953::_reset() {
    _logger.debug("Resetting Ade7953", "ade7953::_reset");
    digitalWrite(_resetPin, LOW);
    delay(ADE7953_RESET_LOW_DURATION);
    digitalWrite(_resetPin, HIGH);
    delay(ADE7953_RESET_LOW_DURATION);
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

        _logger.debug("Attempt (%d/%d) to communicate with Ade7953", "ade7953::_verifyCommunication", _attempt+1, ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS);
        
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

void Ade7953::_setConfigurationFromSpiffs() {
    _logger.debug("Setting configuration from SPIFFS...", "ade7953::_setConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    if (!setConfiguration(_jsonDocument)) {
        _logger.error("Failed to set configuration from SPIFFS. Keeping default one", "ade7953::_setConfigurationFromSpiffs");
        setDefaultConfiguration();
        return;
    }

    _logger.debug("Successfully set configuration from SPIFFS", "ade7953::_setConfigurationFromSpiffs");
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

void Ade7953::setDefaultConfiguration() {
    _logger.debug("Setting default configuration...", "ade7953::setDefaultConfiguration");

    createDefaultAde7953ConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    setConfiguration(_jsonDocument);

    _logger.debug("Default configuration set", "ade7953::setDefaultConfiguration");
}

void Ade7953::_applyConfiguration(JsonDocument &jsonDocument) {
    _logger.debug("Applying configuration...", "ade7953::_applyConfiguration");

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
    _setOffset(jsonDocument["bVarOs"].as<long>(), CHANNEL_B, REACTIVE_POWER);

    _setGain(jsonDocument["aVaGain"].as<long>(), CHANNEL_A, APPARENT_POWER);
    _setGain(jsonDocument["bVaGain"].as<long>(), CHANNEL_B, APPARENT_POWER);

    _setOffset(jsonDocument["aVaOs"].as<long>(), CHANNEL_A, APPARENT_POWER);
    _setOffset(jsonDocument["bVaOs"].as<long>(), CHANNEL_B, APPARENT_POWER);

    _setPhaseCalibration(jsonDocument["phCalA"].as<long>(), CHANNEL_A);
    _setPhaseCalibration(jsonDocument["phCalB"].as<long>(), CHANNEL_B);

    _logger.debug("Successfully applied configuration", "ade7953::_applyConfiguration");
}

bool Ade7953::_validateConfigurationJson(JsonDocument& jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", "ade7953::_validateConfigurationJson"); return false;}

    if (!jsonDocument["sampleTime"].is<unsigned long>()) {_logger.warning("sampleTime is not unsigned long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aVGain"].is<long>()) {_logger.warning("aVGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aIGain"].is<long>()) {_logger.warning("aIGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bIGain"].is<long>()) {_logger.warning("bIGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aIRmsOs"].is<long>()) {_logger.warning("aIRmsOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bIRmsOs"].is<long>()) {_logger.warning("bIRmsOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aWGain"].is<long>()) {_logger.warning("aWGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bWGain"].is<long>()) {_logger.warning("bWGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aWattOs"].is<long>()) {_logger.warning("aWattOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bWattOs"].is<long>()) {_logger.warning("bWattOs is not long", "ade7953::_validateConfigurationJson"); return false;} 
    if (!jsonDocument["aVarGain"].is<long>()) {_logger.warning("aVarGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bVarGain"].is<long>()) {_logger.warning("bVarGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aVarOs"].is<long>()) {_logger.warning("aVarOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bVarOs"].is<long>()) {_logger.warning("bVarOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aVaGain"].is<long>()) {_logger.warning("aVaGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bVaGain"].is<long>()) {_logger.warning("bVaGain is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["aVaOs"].is<long>()) {_logger.warning("aVaOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["bVaOs"].is<long>()) {_logger.warning("bVaOs is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["phCalA"].is<long>()) {_logger.warning("phCalA is not long", "ade7953::_validateConfigurationJson"); return false;}
    if (!jsonDocument["phCalB"].is<long>()) {_logger.warning("phCalB is not long", "ade7953::_validateConfigurationJson"); return false;}

    return true;
}

// Calibration values
// --------------------

void Ade7953::_setCalibrationValuesFromSpiffs() {
    _logger.debug("Setting calibration values from SPIFFS", "ade7953::_setCalibrationValuesFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    if (!setCalibrationValues(_jsonDocument)) {
        _logger.error("Failed to set calibration values from SPIFFS. Keeping default ones", "ade7953::_setCalibrationValuesFromSpiffs");
        setDefaultCalibrationValues();
        return;
    }

    _logger.debug("Successfully set calibration values from SPIFFS", "ade7953::_setCalibrationValuesFromSpiffs");
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

void Ade7953::setDefaultCalibrationValues() {
    _logger.debug("Setting default calibration values", "ade7953::setDefaultCalibrationValues");

    createDefaultCalibrationFile();
    
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    setCalibrationValues(_jsonDocument);

    _logger.debug("Successfully set default calibration values", "ade7953::setDefaultCalibrationValues");
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
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", "ade7953::_validateCalibrationValuesJson"); return false;}

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {_logger.warning("JSON pair value is not an object", "ade7953::_validateCalibrationValuesJson"); return false;}

        JsonObject calibrationObject = kv.value().as<JsonObject>();

        if (!calibrationObject["vLsb"].is<float>() && !calibrationObject["vLsb"].is<int>()) {_logger.warning("vLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["aLsb"].is<float>() && !calibrationObject["aLsb"].is<int>()) {_logger.warning("aLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["wLsb"].is<float>() && !calibrationObject["wLsb"].is<int>()) {_logger.warning("wLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["varLsb"].is<float>() && !calibrationObject["varLsb"].is<int>()) {_logger.warning("varLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["vaLsb"].is<float>() && !calibrationObject["vaLsb"].is<int>()) {_logger.warning("vaLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["whLsb"].is<float>() && !calibrationObject["whLsb"].is<int>()) {_logger.warning("whLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["varhLsb"].is<float>() && !calibrationObject["varhLsb"].is<int>()) {_logger.warning("varhLsb is not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
        if (!calibrationObject["vahLsb"].is<float>() && !calibrationObject["vahLsb"].is<int>()) {_logger.warning("vahLsb is or not float or int", "ade7953::_validateCalibrationValuesJson"); return false;}
    }

    return true;
}

// Data channel
// --------------------

void Ade7953::_setChannelDataFromSpiffs() {
    _logger.debug("Setting data channel from SPIFFS...", "ade7953::_setChannelDataFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    if (!setChannelData(_jsonDocument)) {
        _logger.error("Failed to set data channel from SPIFFS. Keeping default data channel", "ade7953::_setChannelDataFromSpiffs");
        setDefaultChannelData();
        return;
    }

    _logger.debug("Successfully set data channel from SPIFFS", "ade7953::_setChannelDataFromSpiffs");
}

bool Ade7953::setChannelData(JsonDocument &jsonDocument) {
    _logger.debug("Setting channel data...", "ade7953::setChannelData");

    if (!_validateChannelDataJson(jsonDocument)) {
        _logger.warning("Invalid JSON data channel. Keeping previous data channel", "ade7953::setChannelData");
        return false;
    }

    for (JsonPair _kv : jsonDocument.as<JsonObject>()) {
        _logger.verbose(
            "Parsing JSON data channel %s | Active: %d | Reverse: %d | Label: %s | Phase: %d | Calibration Label: %s", 
            "ade7953::setChannelData", 
            _kv.key().c_str(), 
            _kv.value()["active"].as<bool>(), 
            _kv.value()["reverse"].as<bool>(), 
            _kv.value()["label"].as<String>(), 
            _kv.value()["phase"].as<Phase>(),
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
        channelData[_index].phase = _kv.value()["phase"].as<Phase>();
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

void Ade7953::setDefaultChannelData() {
    _logger.debug("Setting default data channel...", "ade7953::setDefaultChannelData");

    createDefaultChannelDataFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    setChannelData(_jsonDocument);

    _logger.debug("Successfully initialized data channel", "ade7953::setDefaultChannelData");
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
        jsonDocument[String(i)]["phase"] = channelData[i].phase;
        jsonDocument[String(i)]["calibrationLabel"] = channelData[i].calibrationValues.label;
    }

    _logger.debug("Successfully converted data channel to JSON", "ade7953::channelDataToJson");
}

bool Ade7953::_validateChannelDataJson(JsonDocument &jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) {_logger.warning("JSON is not an object", "ade7953::_validateChannelDataJson"); return false;}

    for (JsonPair kv : jsonDocument.as<JsonObject>()) {
        if (!kv.value().is<JsonObject>()) {_logger.warning("JSON pair value is not an object", "ade7953::_validateChannelDataJson"); return false;}

        int _index = atoi(kv.key().c_str());
        if (_index < 0 || _index >= CHANNEL_COUNT) {_logger.warning("Index out of bounds: %d", "ade7953::_validateChannelDataJson", _index); return false;}

        JsonObject channelObject = kv.value().as<JsonObject>();

        if (!channelObject["active"].is<bool>()) {_logger.warning("active is not bool", "ade7953::_validateChannelDataJson"); return false;}
        if (!channelObject["reverse"].is<bool>()) {_logger.warning("reverse is not bool", "ade7953::_validateChannelDataJson"); return false;}
        if (!channelObject["label"].is<String>()) {_logger.warning("label is not string", "ade7953::_validateChannelDataJson"); return false;}
        if (!channelObject["phase"].is<int>()) {_logger.warning("phase is not int", "ade7953::_validateChannelDataJson"); return false;}
        if (kv.value()["phase"].as<int>() < 1 || kv.value()["phase"].as<int>() > 3) {_logger.warning("phase is not between 1 and 3", "ade7953::_validateChannelDataJson"); return false;}
        if (!channelObject["calibrationLabel"].is<String>()) {_logger.warning("calibrationLabel is not string", "ade7953::_validateChannelDataJson"); return false;}
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
        if (_jsonDocument[channelData[i].calibrationValues.label]) {
            // Extract the corresponding calibration values from the JSON
            JsonObject _jsonCalibrationValues = _jsonDocument[channelData[i].calibrationValues.label].as<JsonObject>();

            // Set the calibration values for the channel
            _jsonToCalibrationValues(_jsonCalibrationValues, channelData[i].calibrationValues);
        } else {
            _logger.warning(
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

    unsigned int _linecyc = _sampleTime * 50 * 2 / 1000; // 1 channel at 1000 ms: 1000 ms / 1000 * 50 * 2 = 100 linecyc, as linecyc is half of the cycle
    _setLinecyc(_linecyc);

    _logger.debug("Successfully updated sample time", "ade7953::updateSampleTime");
}

int Ade7953::findNextActiveChannel(int currentChannel) {
    for (int i = currentChannel + 1; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active && i != 0) {
            return i;
        }
    }
    for (int i = 1; i < currentChannel; i++) {
        if (channelData[i].active && i != 0) {
            return i;
        }
    }

    return -1;
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
- Sign of the active power (as we use RMS values, the sign will indicate the direction of the power)
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

void Ade7953::readMeterValues(int channel) {
    long _currentMillis = millis();
    long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
    meterValues[channel].lastMillis = _currentMillis;

    int _ade7953Channel = (channel == 0) ? CHANNEL_A : CHANNEL_B;

    float _voltage = 0.0;
    float _powerFactor = 0.0;
    float _current = 0.0;
    float _activePower = 0.0;
    float _reactivePower = 0.0;
    float _apparentPower = 0.0;
    int _signActivePower = 1;
    int _signReactivePower = 1;

    if (channelData[channel].phase == PHASE_1) {
        TRACE
        _voltage = _readVoltageRms() / channelData[channel].calibrationValues.vLsb;
        _signActivePower = _readActivePowerInstantaneous(_ade7953Channel) < 0 ? -1 : 1;
        _current = _readCurrentRms(_ade7953Channel) / channelData[channel].calibrationValues.aLsb * (channelData[channel].reverse ? -1 : 1) * _signActivePower;
        
        _powerFactor = _readPowerFactor(_ade7953Channel)  * POWER_FACTOR_CONVERSION_FACTOR * (channelData[channel].reverse ? -1 : 1);
        _signReactivePower = _powerFactor < 0 ? -1 : 1;

        _activePower = _current * _voltage * abs(_powerFactor); 
        _apparentPower = _current * _voltage;
        _reactivePower = sqrt(pow(_apparentPower, 2) - pow(_activePower, 2)) * _signReactivePower; // This is incorrect but it is the best we can do
    } else { // Assume everything is the same as channel 0 except the current
        TRACE
        // Assume from channel 0
        _voltage = meterValues[0].voltage; // Assume the voltage is the same for all channels (weak assumption but difference usually is in the order of few volts, so less than 1%)
        
        // Read wrong power factor due to the phase shift
        _powerFactor = _readPowerFactor(_ade7953Channel)  * POWER_FACTOR_CONVERSION_FACTOR * (channelData[channel].reverse ? -1 : 1);

        // Compute the correct power factor assuming 120 degrees phase shift in voltage (solid assumption)
        // The idea is to:
        // 1. Compute the angle between the voltage and the current with the arc cosine of the just read power factor
        // 2. Add or subtract 120 degrees to the angle depending on the phase (phase is is lagging 120 degrees, phase 3 is leading 120 degrees)
        // 3. Compute the cosine of the new corrected angle to get the corrected power factor
        // 4. Multiply by -1 if the channel is reversed (as normal)

        // Note that the direction of the current (and consequently the power) cannot be determined. This is because the only reliable reading
        // is the power factor, while the angle only gives the angle difference of the current reading instead of the one of the whole 
        // line cycle. As such, the power factor is the only reliable reading and it cannot provide information about the direction of the power.

        if (channelData[channel].phase == PHASE_2) {
            _powerFactor = cos(acos(_powerFactor) - (2 * PI / 3)) * (channelData[channel].reverse ? -1 : 1);
        } else if (channelData[channel].phase == PHASE_3) {
            // I cannot prove why, but I am SURE the minus is needed if the phase is leading (phase 3)
            _powerFactor = - cos(acos(_powerFactor) + (2 * PI / 3)) * (channelData[channel].reverse ? -1 : 1);
        } else {
            _logger.error("Invalid phase %d for channel %d", "ade7953::readMeterValues", channelData[channel].phase, channel);
        }

        // Read the current
        _current = _readCurrentRms(_ade7953Channel) / channelData[channel].calibrationValues.aLsb * (channelData[channel].reverse ? -1 : 1);
        
        // Compute power values
        _activePower = _current * _voltage * abs(_powerFactor);
        _apparentPower = _current * _voltage;
        _reactivePower = sqrt(pow(_apparentPower, 2) - pow(_activePower, 2)) * _signReactivePower;
    }

    TRACE
    meterValues[channel].voltage = _validateVoltage(meterValues[channel].voltage, _voltage);
    meterValues[channel].current = _validateCurrent(meterValues[channel].current, _current);
    meterValues[channel].activePower = _validatePower(meterValues[channel].activePower, _activePower);
    meterValues[channel].reactivePower = _validatePower(meterValues[channel].reactivePower, _reactivePower);
    meterValues[channel].apparentPower = _validatePower(meterValues[channel].apparentPower, _apparentPower);
    meterValues[channel].powerFactor = _validatePowerFactor(meterValues[channel].powerFactor, _powerFactor);

    TRACE
    // Even though the energy is not directly used, we can use the ADE7953 register for the no-load feature (enabled during setup)
    float _activeEnergy = _readActiveEnergy(_ade7953Channel) / channelData[channel].calibrationValues.whLsb * _signActivePower;
    float _reactiveEnergy = _readReactiveEnergy(_ade7953Channel) / channelData[channel].calibrationValues.varhLsb * _signReactivePower;
    float _apparentEnergy = _readApparentEnergy(_ade7953Channel) / channelData[channel].calibrationValues.vahLsb;

    // If the phase is not Phase 1, set the energy to 1 if the current is above 0.003 A since we cannot  use the no-load future in this approximation
    if (channelData[channel].phase != PHASE_1 && _current > MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD) {
        _activeEnergy = 1;
        _reactiveEnergy = 1;
        _apparentEnergy = 1;
    }

    // Leverage the no-load feature of the ADE7953 to discard the noise
    // As such, when the energy read by the ADE7953 in the given linecycle is below
    // a certain threshold (set before), the read value is 0
    if (_activeEnergy > 0) {
        meterValues[channel].activeEnergyImported += meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0; // W * ms * s / 1000 ms * h / 3600 s = Wh
    } else if (_activeEnergy < 0) {
        meterValues[channel].activeEnergyExported += - meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0; // W * ms * s / 1000 ms * h / 3600 s = Wh
    } else {
        meterValues[channel].activePower = 0.0;
        meterValues[channel].powerFactor = 0.0;
    }

    if (_reactiveEnergy > 0) {
        meterValues[channel].reactiveEnergyImported += meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0; // var * ms * s / 1000 ms * h / 3600 s = VArh
    } else if (_reactiveEnergy < 0) {
        meterValues[channel].reactiveEnergyExported += - meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0; // var * ms * s / 1000 ms * h / 3600 s = VArh
    } else {
        meterValues[channel].reactivePower = 0.0;
    }

    if (_apparentEnergy != 0) {
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
    _jsonValues["activeEnergyImported"] = meterValues[index].activeEnergyImported;
    _jsonValues["activeEnergyExported"] = meterValues[index].activeEnergyExported;
    _jsonValues["reactiveEnergyImported"] = meterValues[index].reactiveEnergyImported;
    _jsonValues["reactiveEnergyExported"] = meterValues[index].reactiveEnergyExported;
    _jsonValues["apparentEnergy"] = meterValues[index].apparentEnergy;

    return _jsonDocument;
}


void Ade7953::meterValuesToJson(JsonDocument &jsonDocument) {
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            JsonObject _jsonChannel = jsonDocument.add<JsonObject>();
            _jsonChannel["index"] = i;
            _jsonChannel["label"] = channelData[i].label;
            _jsonChannel["data"] = singleMeterValuesToJson(i);
        }
    }
}

// Energy
// --------------------

void Ade7953::_setEnergyFromSpiffs() {
    _logger.debug("Reading energy from SPIFFS", "ade7953::readEnergyFromSpiffs");
    
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        _logger.error("Failed to read energy from SPIFFS", "ade7953::readEnergyFromSpiffs");
        return;
    } else {
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            meterValues[i].activeEnergyImported = _jsonDocument[String(i)]["activeEnergyImported"].as<float>();
            meterValues[i].activeEnergyExported = _jsonDocument[String(i)]["activeEnergyExported"].as<float>();
            meterValues[i].reactiveEnergyImported = _jsonDocument[String(i)]["reactiveEnergyImported"].as<float>();
            meterValues[i].reactiveEnergyExported = _jsonDocument[String(i)]["reactiveEnergyExported"].as<float>();
            meterValues[i].apparentEnergy = _jsonDocument[String(i)]["apparentEnergy"].as<float>();
        }
    }
        
    _logger.debug("Successfully read energy from SPIFFS", "ade7953::readEnergyFromSpiffs");
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
        _jsonDocument[String(i)]["activeEnergyImported"] = meterValues[i].activeEnergyImported;
        _jsonDocument[String(i)]["activeEnergyExported"] = meterValues[i].activeEnergyExported;
        _jsonDocument[String(i)]["reactiveEnergyImported"] = meterValues[i].reactiveEnergyImported;
        _jsonDocument[String(i)]["reactiveEnergyExported"] = meterValues[i].reactiveEnergyExported;
        _jsonDocument[String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) _logger.debug("Successfully saved energy to SPIFFS", "ade7953::saveEnergyToSpiffs");
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
            if (meterValues[i].activeEnergyImported > 1) _jsonDocument[_currentDate][String(i)]["activeEnergyImported"] = meterValues[i].activeEnergyImported;
            if (meterValues[i].activeEnergyExported > 1) _jsonDocument[_currentDate][String(i)]["activeEnergyExported"] = meterValues[i].activeEnergyExported;
            if (meterValues[i].reactiveEnergyImported > 1) _jsonDocument[_currentDate][String(i)]["reactiveEnergyImported"] = meterValues[i].reactiveEnergyImported;
            if (meterValues[i].reactiveEnergyExported > 1) _jsonDocument[_currentDate][String(i)]["reactiveEnergyExported"] = meterValues[i].reactiveEnergyExported;
            if (meterValues[i].apparentEnergy > 1) _jsonDocument[_currentDate][String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
        }
    }

    if (serializeJsonToSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument)) _logger.debug("Successfully saved daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs");
}

void Ade7953::resetEnergyValues() {
    _logger.warning("Resetting energy values to 0", "ade7953::resetEnergyValues");

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        meterValues[i].activeEnergyImported = 0.0;
        meterValues[i].activeEnergyExported = 0.0;
        meterValues[i].reactiveEnergyImported = 0.0;
        meterValues[i].reactiveEnergyExported = 0.0;
        meterValues[i].apparentEnergy = 0.0;
    }

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);
    saveEnergy();

    _logger.info("Successfully reset energy values to 0", "ade7953::resetEnergyValues");
}


// Others
// --------------------

void Ade7953::_setLinecyc(unsigned int linecyc) {
    // Limit between 100 ms and 10 s
    unsigned int _minLinecyc = 10;
    unsigned int _maxLinecyc = 1000;

    linecyc = min(max(linecyc, _minLinecyc), _maxLinecyc);

    _logger.debug(
        "Setting linecyc to %d",
        "ade7953::_setLinecyc",
        linecyc
    );

    writeRegister(LINECYC_16, 16, long(linecyc));
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
        "ade7953::_setGain",
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
        "ade7953::_setOffset",
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

long Ade7953::_readAngle(int channel) {
    if (channel == CHANNEL_A) {return readRegister(ANGLE_A_16, 16, true);} 
    else {return readRegister(ANGLE_B_16, 16, true);}
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
    float _aggregatedActivePower = getAggregatedActivePower(includeChannel0);
    float _aggregatedApparentPower = getAggregatedApparentPower(includeChannel0);

    return _aggregatedApparentPower > 0 ? _aggregatedActivePower / _aggregatedApparentPower : 0.0f;
}