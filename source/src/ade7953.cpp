#include "ade7953.h"

Ticker energyTicker;

bool Ade7953::saveEnergyFlag = false;
int currentDay = day();

Ade7953::Ade7953(
    int ssPin,
    int sckPin,
    int misoPin,
    int mosiPin,
    int resetPin
): _ssPin(ssPin), 
    _sckPin(sckPin), 
    _misoPin(misoPin), 
    _mosiPin(mosiPin), 
    _resetPin(resetPin) {
        Ade7953Configuration configuration;
        MeterValues meterValues[MULTIPLEXER_CHANNEL_COUNT + 1];
        ChannelData channelData[MULTIPLEXER_CHANNEL_COUNT + 1];

        _initializeMeterValues();

        int _maxDurationMs = MAX_DURATION_AVERAGE_MEASUREMENT;
    }

bool Ade7953::begin() {
    logger.log("Initializing Ade7953", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Setting up pins", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    pinMode(_ssPin, OUTPUT);
    pinMode(_sckPin, OUTPUT);
    pinMode(_misoPin, INPUT);
    pinMode(_mosiPin, OUTPUT);
    pinMode(_resetPin, OUTPUT);

    logger.log("Setting up SPI", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);
    SPI.setClockDivider(SPI_CLOCK_DIV64); // 64div -> 250kHz on 16MHz clock, but on 80MHz clock it's 1.25MHz. Max Ade7953 clock is 2MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    digitalWrite(_ssPin, HIGH);

    _reset();

    logger.log("Verifying communication with Ade7953", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    if (!_verifyCommunication()) {
        logger.log("Failed to communicate with Ade7953", "ade7953::begin", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
    logger.log("Successfully initialized Ade7953", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    
    logger.log("Setting optimum settings", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setOptimumSettings();
    logger.log("Successfully set optimum settings", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Setting default configuration", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setDefaultLycmode();
    logger.log("Successfully set default configuration", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);    

    logger.log("Setting default no-load feature", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setDefaultNoLoadFeature();
    logger.log("Successfully set no-load feature", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    
    logger.log("Setting default config register", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setDefaultConfigRegister();
    logger.log("Successfully set config register", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Setting PGA gains", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setDefaultPgaGain();
    logger.log("Successfully set PGA gains", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Setting configuration from spiffs", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setConfigurationFromSpiffs();
    logger.log("Done setting configuration from spiffs", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
 
    logger.log("Reading calibration values from SPIFFS", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setCalibrationValuesFromSpiffs();
    logger.log("Done reading calibration values from SPIFFS", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Reading energy from SPIFFS", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    setEnergyFromSpiffs();
    logger.log("Done reading energy from SPIFFS", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);

    logger.log("Initializing data channel", "ade7953::begin", CUSTOM_LOG_LEVEL_DEBUG);
    _setChannelDataFromSpiffs();

    energyTicker.attach(ENERGY_SAVE_INTERVAL, [](){ saveEnergyFlag = true; });

    return true;
}

void Ade7953::_setDefaultPgaGain()
{
    setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, VOLTAGE_MEASUREMENT);
    setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_A, CURRENT_MEASUREMENT);
    setPgaGain(DEFAULT_PGA_REGISTER, CHANNEL_B, CURRENT_MEASUREMENT);
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
    if (saveEnergyFlag) {
        saveEnergyToSpiffs();
        saveEnergyFlag = false;
    }

    int newDay = day();
    if (newDay != currentDay) {
        saveDailyEnergyToSpiffs();
        currentDay = newDay;
    }
}

void Ade7953::_reset() {
    logger.log("Resetting Ade7953", "ade7953::_reset", CUSTOM_LOG_LEVEL_DEBUG);
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
    if ((readRegister(AP_NOLOAD_32, 32, false)) != DEFAULT_EXPECTED_AP_NOLOAD_REGISTER) {
        logger.log("Failed to communicate with Ade7953", "ade7953::_verifyCommunication", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
    logger.log("Successfully communicated with Ade7953", "ade7953::_verifyCommunication", CUSTOM_LOG_LEVEL_DEBUG);
    return true;
}

// Configuration
// --------------------

void Ade7953::setDefaultConfiguration() {
    logger.log("Setting default configuration", "ade7953::setDefaultConfiguration", CUSTOM_LOG_LEVEL_DEBUG);

    configuration.linecyc = DEFAULT_LCYCMODE_REGISTER;
    configuration.calibration.aWGain = DEFAULT_AWGAIN;
    configuration.calibration.aWattOs = DEFAULT_AWATTOS;
    configuration.calibration.aVarGain = DEFAULT_AVARGAIN;
    configuration.calibration.aVarOs = DEFAULT_AVAROS;
    configuration.calibration.aVaGain = DEFAULT_AVAGAIN;
    configuration.calibration.aVaOs = DEFAULT_AVAOS;
    configuration.calibration.aIGain = DEFAULT_AIGAIN;
    configuration.calibration.aIRmsOs = DEFAULT_AIRMSOS;
    configuration.calibration.bIGain = DEFAULT_BIGAIN;
    configuration.calibration.bIRmsOs = DEFAULT_BIRMSOS;
    configuration.calibration.phCalA = DEFAULT_PHCALA;
    configuration.calibration.phCalB = DEFAULT_PHCALB;

    _applyConfiguration();

    logger.log("Default configuration set", "ade7953::setDefaultConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

void Ade7953::_setConfigurationFromSpiffs() {
    logger.log("Setting configuration from SPIFFS", "ade7953::_setConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.log("Failed to read configuration from SPIFFS. Keeping default one", "ade7953::_setConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        setDefaultConfiguration();
    } else {
        setConfiguration(parseJsonConfiguration(_jsonDocument));
        logger.log("Successfully read and set configuration from SPIFFS", "ade7953::_setConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    }
}

void Ade7953::setConfiguration(Ade7953Configuration newConfiguration) {
    logger.log("Setting configuration", "ade7953::setConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
    configuration = newConfiguration;

    _applyConfiguration();

    if (!saveConfigurationToSpiffs()) {
        logger.log("Failed to save configuration to SPIFFS", "ade7953::setConfiguration", CUSTOM_LOG_LEVEL_ERROR);
    } else {
        logger.log("Successfully saved configuration to SPIFFS", "ade7953::setConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
    }
}

void Ade7953::_applyConfiguration() {
    logger.log("Applying configuration", "ade7953::applyConfiguration", CUSTOM_LOG_LEVEL_DEBUG);

    setLinecyc(configuration.linecyc);
    setGain(configuration.calibration.aWGain, CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    setOffset(configuration.calibration.aWattOs, CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    setGain(configuration.calibration.aVarGain, CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    setOffset(configuration.calibration.aVarOs, CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    setGain(configuration.calibration.aVaGain, CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    setOffset(configuration.calibration.aVaOs, CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    setGain(configuration.calibration.aIGain, CHANNEL_A, CURRENT_MEASUREMENT);
    setOffset(configuration.calibration.aIRmsOs, CHANNEL_A, CURRENT_MEASUREMENT);
    setGain(configuration.calibration.bIGain, CHANNEL_B, CURRENT_MEASUREMENT);
    setOffset(configuration.calibration.bIRmsOs, CHANNEL_B, CURRENT_MEASUREMENT);
    setPhaseCalibration(configuration.calibration.phCalA, CHANNEL_A);
    setPhaseCalibration(configuration.calibration.phCalB, CHANNEL_B);

    logger.log("Successfully applied configuration", "ade7953::applyConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

bool Ade7953::saveConfigurationToSpiffs() {
    logger.log("Saving configuration to SPIFFS", "ade7953::saveConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    if (serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, configurationToJson())) {
        logger.log("Successfully serialize the JSON configuration to SPIFFS", "ade7953::saveConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    } else {
        logger.log("Failed to serialize the JSON configuration to SPIFFS", "ade7953::saveConfigurationToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
}

JsonDocument Ade7953::configurationToJson() {
    logger.log("Converting configuration to JSON", "ade7953::configurationToJson", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument;
    JsonObject _jsonObject = _jsonDocument.to<JsonObject>();

    _jsonObject["linecyc"] = configuration.linecyc;
    JsonObject _calibrationObject = _jsonObject["calibration"].to<JsonObject>();
    _calibrationObject["aWGain"] = configuration.calibration.aWGain;
    _calibrationObject["aWattOs"] = configuration.calibration.aWattOs;
    _calibrationObject["aVarGain"] = configuration.calibration.aVarGain;
    _calibrationObject["aVarOs"] = configuration.calibration.aVarOs;
    _calibrationObject["aVaGain"] = configuration.calibration.aVaGain;
    _calibrationObject["aVaOs"] = configuration.calibration.aVaOs;
    _calibrationObject["aIGain"] = configuration.calibration.aIGain;
    _calibrationObject["aIRmsOs"] = configuration.calibration.aIRmsOs;
    _calibrationObject["bIGain"] = configuration.calibration.bIGain;
    _calibrationObject["bIRmsOs"] = configuration.calibration.bIRmsOs;
    _calibrationObject["phCalA"] = configuration.calibration.phCalA;
    _calibrationObject["phCalB"] = configuration.calibration.phCalB;

    return _jsonDocument;
}

Ade7953Configuration Ade7953::parseJsonConfiguration(JsonDocument jsonDocument) {
    logger.log("Parsing JSON configuration", "ade7953::parseJsonConfiguration", CUSTOM_LOG_LEVEL_DEBUG);

    Ade7953Configuration newConfiguration;

    newConfiguration.linecyc = jsonDocument["linecyc"].as<long>();
    newConfiguration.calibration.aWGain = jsonDocument["calibration"]["aWGain"].as<long>();
    newConfiguration.calibration.aWattOs = jsonDocument["calibration"]["aWattOs"].as<long>();
    newConfiguration.calibration.aVarGain = jsonDocument["calibration"]["aVarGain"].as<long>();
    newConfiguration.calibration.aVarOs = jsonDocument["calibration"]["aVarOs"].as<long>();
    newConfiguration.calibration.aVaGain = jsonDocument["calibration"]["aVaGain"].as<long>();
    newConfiguration.calibration.aVaOs = jsonDocument["calibration"]["aVaOs"].as<long>();
    newConfiguration.calibration.aIGain = jsonDocument["calibration"]["aIGain"].as<long>();
    newConfiguration.calibration.aIRmsOs = jsonDocument["calibration"]["aIRmsOs"].as<long>();
    newConfiguration.calibration.bIGain = jsonDocument["calibration"]["bIGain"].as<long>();
    newConfiguration.calibration.bIRmsOs = jsonDocument["calibration"]["bIRmsOs"].as<long>();
    newConfiguration.calibration.phCalA = jsonDocument["calibration"]["phCalA"].as<long>();
    newConfiguration.calibration.phCalB = jsonDocument["calibration"]["phCalB"].as<long>();

    return newConfiguration;
}

// Calibration values
// --------------------

void Ade7953::setDefaultCalibrationValues() {
    logger.log("Setting default calibration values", "ade7953::setDefaultCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);
    CalibrationValues defaultValue;

    defaultValue.label = "Unknown";
    defaultValue.vLsb = 1.0;
    defaultValue.aLsb = 1.0;
    defaultValue.wLsb = 1.0;
    defaultValue.varLsb = 1.0;
    defaultValue.vaLsb = 1.0;
    defaultValue.whLsb = 1.0;
    defaultValue.varhLsb = 1.0;
    defaultValue.vahLsb = 1.0;

    calibrationValues.push_back(defaultValue);
}

void Ade7953::_setCalibrationValuesFromSpiffs() {
    logger.log("Setting calibration values from SPIFFS", "ade7953::_setCalibrationValuesFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.log("Failed to read calibration values from SPIFFS. Setting default ones...", "ade7953::_setCalibrationValuesFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        setDefaultCalibrationValues();
    } else {
        logger.log("Successfully read calibration values from SPIFFS. Setting values...", "ade7953::_setCalibrationValuesFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        setCalibrationValues(parseJsonCalibrationValues(_jsonDocument));
    }
}

bool Ade7953::saveCalibrationValuesToSpiffs() {
    logger.log("Saving calibration values to SPIFFS", "ade7953::saveCalibrationValuesToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    if (serializeJsonToSpiffs(CALIBRATION_JSON_PATH, calibrationValuesToJson())) {
        logger.log("Successfully saved calibration values to SPIFFS", "ade7953::saveCalibrationValuesToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    } else {
        logger.log("Failed to save calibration values to SPIFFS", "ade7953::saveCalibrationValuesToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
}

void Ade7953::setCalibrationValues(std::vector<CalibrationValues> newCalibrationValues) {
    logger.log("Setting new calibration values", "ade7953::setCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);

    bool _found;
    for (auto& newCalibrationValue : newCalibrationValues) {
        _found = false;
        for (auto& calibrationValue : calibrationValues) {
            if (calibrationValue.label == newCalibrationValue.label) {
                calibrationValue = newCalibrationValue;
                _found = true;
                break;
            }
        }
        if (!_found) {
            calibrationValues.push_back(newCalibrationValue);
        }
    }

    if (!saveCalibrationValuesToSpiffs()) {
        logger.log("Failed to save calibration values to SPIFFS", "ade7953::setCalibrationValues", CUSTOM_LOG_LEVEL_ERROR);
    } else {
        logger.log("Successfully saved calibration values to SPIFFS", "ade7953::setCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);
    }

    logger.log("Successfully set new calibration values", "ade7953::setCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);
}

JsonDocument Ade7953::calibrationValuesToJson() {
    logger.log("Converting calibration values to JSON", "ade7953::calibrationValuesToJson", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument;
    JsonArray _jsonArray = _jsonDocument.to<JsonArray>();

    for (auto& calibrationValue : calibrationValues) {
        JsonObject _jsonCalibration = _jsonArray.add<JsonObject>();
        _jsonCalibration["label"] = calibrationValue.label;
        
        JsonObject _jsonValues = _jsonCalibration["calibrationValues"].to<JsonObject>();
        
        _jsonValues["vLsb"] = calibrationValue.vLsb;
        _jsonValues["aLsb"] = calibrationValue.aLsb;
        _jsonValues["wLsb"] = calibrationValue.wLsb;
        _jsonValues["varLsb"] = calibrationValue.varLsb;
        _jsonValues["vaLsb"] = calibrationValue.vaLsb;
        _jsonValues["whLsb"] = calibrationValue.whLsb;
        _jsonValues["varhLsb"] = calibrationValue.varhLsb;
        _jsonValues["vahLsb"] = calibrationValue.vahLsb;
    }

    return _jsonDocument;
}

std::vector<CalibrationValues> Ade7953::parseJsonCalibrationValues(JsonDocument jsonDocument) {
    logger.log("Parsing JSON calibration values", "ade7953::parseJsonCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);
    
    JsonArray jsonArray = jsonDocument.as<JsonArray>();

    std::vector<CalibrationValues> calibrationValuesVector;
    for (JsonVariant json : jsonArray) {
        CalibrationValues calibrationValues;
        calibrationValues.label = json["label"].as<String>();
        calibrationValues.vLsb = json["calibrationValues"]["vLsb"].as<float>();
        calibrationValues.aLsb = json["calibrationValues"]["aLsb"].as<float>();
        calibrationValues.wLsb = json["calibrationValues"]["wLsb"].as<float>();
        calibrationValues.varLsb = json["calibrationValues"]["varLsb"].as<float>();
        calibrationValues.vaLsb = json["calibrationValues"]["vaLsb"].as<float>();
        calibrationValues.whLsb = json["calibrationValues"]["whLsb"].as<float>();
        calibrationValues.varhLsb = json["calibrationValues"]["varhLsb"].as<float>();
        calibrationValues.vahLsb = json["calibrationValues"]["vahLsb"].as<float>();
        calibrationValuesVector.push_back(calibrationValues);
    }

    logger.log("Successfully parsed JSON calibration values", "ade7953::parseJsonCalibrationValues", CUSTOM_LOG_LEVEL_DEBUG);
    return calibrationValuesVector;
}

// Data channel
// --------------------

void Ade7953::setDefaultChannelData() {
    logger.log("Initializing data channel", "ade7953::setDefaultChannelData", CUSTOM_LOG_LEVEL_DEBUG);
    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        logger.log(
            ("Initializing channel " + String(i)).c_str(),
            "ade7953::setDefaultChannelData",
            CUSTOM_LOG_LEVEL_DEBUG
        );
        channelData[i].index = i;
        channelData[i].active = false;
        channelData[i].reverse = false;
        channelData[i].label = "Channel " + String(i);
        channelData[i].calibrationValues = calibrationValues[0];
    }
    // By default, the first channel is active
    channelData[0].active = true;
    channelData[0].label = "General";
}

void Ade7953::_setChannelDataFromSpiffs() {
    logger.log("Setting data channel from SPIFFS", "ade7953::_setChannelDataFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.log("Failed to read data channel from SPIFFS. Setting default one", "ade7953::_setChannelDataFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        setDefaultChannelData();
    } else {
        logger.log("Successfully read data channel from SPIFFS. Setting values...", "ade7953::_setChannelDataFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        
        ChannelData newChannelData[MULTIPLEXER_CHANNEL_COUNT + 1];
        parseJsonChannelData(_jsonDocument, newChannelData);
        setChannelData(newChannelData);
    }
    updateDataChannel();
}

void Ade7953::setChannelData(ChannelData* newChannelData) {
    logger.log("Setting data channel", "ade7953::setChannelData", CUSTOM_LOG_LEVEL_DEBUG);
    for(int i = 0; i < MULTIPLEXER_CHANNEL_COUNT + 1; i++) {
        channelData[i] = newChannelData[i];
    }
    saveChannelDataToSpiffs();
    logger.log("Successfully set data channel", "ade7953::setChannelData", CUSTOM_LOG_LEVEL_DEBUG);
}

bool Ade7953::saveChannelDataToSpiffs() {
    logger.log("Saving data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    if (serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, channelDataToJson())) {
        logger.log("Successfully saved data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    } else {
        logger.log("Failed to save data channel to SPIFFS", "ade7953::saveChannelDataToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
}

JsonDocument Ade7953::channelDataToJson() {
    logger.log("Converting data channel to JSON", "ade7953::channelDataToJson", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument;
    JsonArray _jsonArray = _jsonDocument.to<JsonArray>();

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        JsonObject _jsonChannel = _jsonArray.add<JsonObject>();
        _jsonChannel["index"] = channelData[i].index;
        _jsonChannel["active"] = channelData[i].active;
        _jsonChannel["reverse"] = channelData[i].reverse;
        _jsonChannel["label"] = channelData[i].label;

        JsonObject _jsonCalibrationValues = _jsonChannel["calibration"].to<JsonObject>();
        _jsonCalibrationValues["label"] = channelData[i].calibrationValues.label;
    }

    return _jsonDocument;
}

void Ade7953::parseJsonChannelData(JsonDocument jsonDocument, ChannelData* channelData) {
    logger.log("Parsing JSON data channel", "ade7953::parseJsonChannelData", CUSTOM_LOG_LEVEL_DEBUG);

    JsonArray jsonArray = jsonDocument.as<JsonArray>();

    int _i = 0;
    for (JsonVariant json : jsonArray) {
        channelData[_i].index = json["index"].as<int>();
        channelData[_i].active = json["active"].as<bool>();
        channelData[_i].reverse = json["reverse"].as<bool>();
        channelData[_i].label = json["label"].as<String>();
        channelData[_i].calibrationValues = findCalibrationValue(json["calibration"]["label"].as<String>());
        _i++;
        if (_i >= MULTIPLEXER_CHANNEL_COUNT + 1) {
            break;
        }
    }

    logger.log("Successfully parsed JSON data channel", "ade7953::parseJsonChannelData", CUSTOM_LOG_LEVEL_DEBUG);
}

void Ade7953::updateDataChannel() {
    logger.log("Updating data channel", "ade7953::updateDataChannel", CUSTOM_LOG_LEVEL_DEBUG);
    String _previousLabel; 
    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        _previousLabel = channelData[i].calibrationValues.label;
        channelData[i].calibrationValues = findCalibrationValue(_previousLabel);
    }
    _updateSampleTime();
}

void Ade7953::_updateSampleTime() {
    logger.log("Updating sample time", "ade7953::updateSampleTime", CUSTOM_LOG_LEVEL_DEBUG);

    int _activeChannelCount = getActiveChannelCount();
    if (_activeChannelCount > 0) {
        long linecyc = long(SAMPLE_TIME / _activeChannelCount);
        configuration.linecyc = linecyc;
        _applyConfiguration();
        saveConfigurationToSpiffs();
        logger.log("Successfully updated sample time", "ade7953::updateSampleTime", CUSTOM_LOG_LEVEL_DEBUG);
    } else {
        logger.log("No active channels found, sample time not updated", "ade7953::updateSampleTime", CUSTOM_LOG_LEVEL_WARNING);
    }
}


CalibrationValues Ade7953::findCalibrationValue(String label) {
    for (auto& calibrationValue : calibrationValues) {
        if (calibrationValue.label == label) {
            return calibrationValue;
        }
    }
    return calibrationValues[0];
}

int Ade7953::findNextActiveChannel(int currentChannel) {
    for (int i = currentChannel + 1; i < MULTIPLEXER_CHANNEL_COUNT + 1; i++) {
        if (channelData[i].active) {
            return i;
        }
    }
    for (int i = 0; i < currentChannel; i++) {
        if (channelData[i].active) {
            return i;
        }
    }
    logger.log("No active channel found, returning current channel", "ade7953::findNextActiveChannel", CUSTOM_LOG_LEVEL_DEBUG);
    return currentChannel;
}

int Ade7953::getActiveChannelCount() {
    int _activeChannelCount = 0;
    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT + 1; i++) {
        if (channelData[i].active) {
            _activeChannelCount++;
        }
    }
    return _activeChannelCount;
}


// Meter values
// --------------------

void Ade7953::_initializeMeterValues() {
    logger.log("Initializing meter values", "ade7953::_initializeMeterValues", CUSTOM_LOG_LEVEL_DEBUG);
    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        meterValues[i].voltage = 0.0;
        meterValues[i].current = 0.0;
        meterValues[i].activePower = 0.0;
        meterValues[i].reactivePower = 0.0;
        meterValues[i].apparentPower = 0.0;
        meterValues[i].powerFactor = 0.0;
        meterValues[i].activeEnergy = 0.0;
        meterValues[i].reactiveEnergy = 0.0;
        meterValues[i].apparentEnergy = 0.0;
        meterValues[i].lastMillis = millis();
    }
}

void Ade7953::readMeterValues(int channel) {
    long _currentMillis = millis();
    int _ade7953Channel = (channel == 0) ? CHANNEL_A : CHANNEL_B;

    meterValues[channel].voltage = readVoltageRms() * channelData[channel].calibrationValues.vLsb;
    meterValues[channel].current = readCurrentRms(_ade7953Channel) * channelData[channel].calibrationValues.aLsb * (channelData[channel].reverse ? -1 : 1);
    meterValues[channel].activePower = readActivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.wLsb * (channelData[channel].reverse ? -1 : 1);
    meterValues[channel].reactivePower = readReactivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.varLsb * (channelData[channel].reverse ? -1 : 1);
    meterValues[channel].apparentPower = readApparentPowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.vaLsb;
    meterValues[channel].powerFactor = readPowerFactor(_ade7953Channel) * POWER_FACTOR_CONVERSION_FACTOR;

    float _activeEnergy = readActiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.whLsb;
    float _reactiveEnergy = readReactiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.varhLsb;
    float _apparentEnergy = readApparentEnergy(_ade7953Channel) * channelData[channel].calibrationValues.vahLsb;

    if (_activeEnergy != 0.0) {
        long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
        meterValues[channel].activeEnergy += meterValues[channel].activePower * _deltaMillis / 1000.0 / 3600.0;
    } else {
        meterValues[channel].activePower = 0.0;
        meterValues[channel].powerFactor = 0.0;
    }

    if (_reactiveEnergy != 0.0) {
        long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
        meterValues[channel].reactiveEnergy += meterValues[channel].reactivePower * _deltaMillis / 1000.0 / 3600.0;
    } else {
        meterValues[channel].reactivePower = 0.0;
    }

    if (_apparentEnergy != 0.0) {
        long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
        meterValues[channel].apparentEnergy += meterValues[channel].apparentPower * _deltaMillis / 1000.0 / 3600.0;
    } else {
        meterValues[channel].current = 0.0;
        meterValues[channel].apparentPower = 0.0;
    }

    meterValues[channel].lastMillis = _currentMillis;
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

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
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

void Ade7953::setEnergyFromSpiffs() {
    logger.log("Reading energy from SPIFFS", "ade7953::readEnergyFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(ENERGY_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.log("Failed to read energy from SPIFFS", "ade7953::readEnergyFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
    } else {
        logger.log("Successfully read energy from SPIFFS", "ade7953::readEnergyFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        JsonObject _jsonObject = _jsonDocument.as<JsonObject>();
        for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
            meterValues[i].activeEnergy = _jsonObject[String(i)]["activeEnergy"].as<float>();
            meterValues[i].reactiveEnergy = _jsonObject[String(i)]["reactiveEnergy"].as<float>();
            meterValues[i].apparentEnergy = _jsonObject[String(i)]["apparentEnergy"].as<float>();
        }
    }
}

void Ade7953::saveEnergyToSpiffs() {
    logger.log("Saving energy to SPIFFS", "ade7953::saveEnergyToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(ENERGY_JSON_PATH);

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        _jsonDocument[String(i)]["activeEnergy"] = meterValues[i].activeEnergy;
        _jsonDocument[String(i)]["reactiveEnergy"] = meterValues[i].reactiveEnergy;
        _jsonDocument[String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) {
        logger.log("Successfully saved energy to SPIFFS", "ade7953::saveEnergyToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    } else {
        logger.log("Failed to save energy to SPIFFS", "ade7953::saveEnergyToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
    }
}

void Ade7953::saveDailyEnergyToSpiffs() {
    logger.log("Saving daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(DAILY_ENERGY_JSON_PATH);
    
    time_t now = time(nullptr);
    now -= 24 * 60 * 60;  // Subtract one day to get the previous day
    struct tm *timeinfo = localtime(&now);
    char _currentDate[11];
    strftime(_currentDate, sizeof(_currentDate), "%Y-%m-%d", timeinfo);

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        if (channelData[i].active) {
            JsonObject _jsonDailyObject = _jsonDocument[_currentDate][String(i)];
            _jsonDailyObject["activeEnergy"] = meterValues[i].activeEnergy;
            _jsonDailyObject["reactiveEnergy"] = meterValues[i].reactiveEnergy;
            _jsonDailyObject["apparentEnergy"] = meterValues[i].apparentEnergy;
        }
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) {
        logger.log("Successfully saved daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    } else {
        logger.log("Failed to save daily energy to SPIFFS", "ade7953::saveDailyEnergyToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
    }
}

void Ade7953::resetEnergyValues() {
    logger.log("Resetting energy values to 0", "ade7953::resetEnergyValues", CUSTOM_LOG_LEVEL_WARNING);

    for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT+1; i++) {
        meterValues[i].activeEnergy = 0.0;
        meterValues[i].reactiveEnergy = 0.0;
        meterValues[i].apparentEnergy = 0.0;
    }
    saveEnergyToSpiffs();
}


// Others
// --------------------

void Ade7953::setLinecyc(long linecyc) {
    linecyc = min(max(linecyc, 1L), 1000L);

    logger.log(
        ("Setting linecyc to " + String(linecyc)).c_str(),
        "ade7953::setLinecyc",
        CUSTOM_LOG_LEVEL_DEBUG
    );

    writeRegister(LINECYC_16, 16, linecyc);
}

void Ade7953::setPhaseCalibration(long phaseCalibration, int channel) {
    logger.log(
        ("Setting phase calibration to " + String(phaseCalibration) + " on channel " + String(channel)).c_str(),
        "ade7953::setPhaseCalibration",
        CUSTOM_LOG_LEVEL_DEBUG
    );
    
    if (channel == CHANNEL_A) {
        writeRegister(PHCALA_16, 16, phaseCalibration);
    } else {
        writeRegister(PHCALB_16, 16, phaseCalibration);
    }
}

void Ade7953::setPgaGain(long pgaGain, int channel, int measurementType) {
    logger.log(
        ("Setting PGA gain to " + String(pgaGain) + " on channel " + String(channel) + " for measurement type " + String(measurementType)).c_str(),
        "ade7953::setPgaGain",
        CUSTOM_LOG_LEVEL_DEBUG
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

void Ade7953::setGain(long gain, int channel, int measurementType) {
    logger.log(
        ("Setting gain to " + String(gain) + " on channel " + String(channel) + " for measurement type " + String(measurementType)).c_str(),
        "ade7953::setGain",
        CUSTOM_LOG_LEVEL_DEBUG
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

void Ade7953::setOffset(long offset, int channel, int measurementType) {
    logger.log(
        ("Setting offset to " + String(offset) + " on channel " + String(channel) + " for measurement type " + String(measurementType)).c_str(),
        "ade7953::setOffset",
        CUSTOM_LOG_LEVEL_DEBUG
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

long Ade7953::readApparentPowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVA_32, 32, true);} 
    else {return readRegister(BVA_32, 32, true);}
}

long Ade7953::readActivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AWATT_32, 32, true);} 
    else {return readRegister(BWATT_32, 32, true);}
}

long Ade7953::readReactivePowerInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AVAR_32, 32, true);} 
    else {return readRegister(BVAR_32, 32, true);}
}

long Ade7953::readCurrentInstantaneous(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IA_32, 32, true);} 
    else {return readRegister(IB_32, 32, true);}
}

long Ade7953::readVoltageInstantaneous() {
    return readRegister(V_32, 32, true);
}

long Ade7953::readCurrentRms(int channel) {
    if (channel == CHANNEL_A) {return readRegister(IRMSA_32, 32, false);} 
    else {return readRegister(IRMSB_32, 32, false);}
}

long Ade7953::readVoltageRms() {
    return readRegister(VRMS_32, 32, false);
}

long Ade7953::readActiveEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(AENERGYA_32, 32, true);} 
    else {return readRegister(AENERGYB_32, 32, true);}
}

long Ade7953::readReactiveEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(RENERGYA_32, 32, true);} 
    else {return readRegister(RENERGYB_32, 32, true);}
}

long Ade7953::readApparentEnergy(int channel) {
    if (channel == CHANNEL_A) {return readRegister(APENERGYA_32, 32, true);} 
    else {return readRegister(APENERGYB_32, 32, true);}
}

long Ade7953::readPowerFactor(int channel) {
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
    logger.log(
        ("Read " + String(_long_response) + " from register " + String(registerAddress) + " with " + String(nBits) + " bits").c_str(),
        "ade7953::readRegister",
        CUSTOM_LOG_LEVEL_VERBOSE
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
    logger.log(
        ("Writing " + String(data) + " to register " + String(registerAddress) + " with " + String(nBits) + " bits").c_str(),
        "ade7953::writeRegister",
        CUSTOM_LOG_LEVEL_DEBUG
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