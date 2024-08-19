#include "ade7953.h"

Ticker saveEnergyTicker;
bool Ade7953::saveEnergyFlag = false;

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
        MeterValues meterValues[CHANNEL_COUNT];
        ChannelData channelData[CHANNEL_COUNT];

        _initializeMeterValues();

        int _maxDurationMs = MAX_DURATION_AVERAGE_MEASUREMENT;
    }

bool Ade7953::begin() {
    logger.debug("Initializing Ade7953", "Ade7953::begin");

    logger.debug("Setting up pins", "Ade7953::begin");
    pinMode(_ssPin, OUTPUT);
    pinMode(_sckPin, OUTPUT);
    pinMode(_misoPin, INPUT);
    pinMode(_mosiPin, OUTPUT);
    pinMode(_resetPin, OUTPUT);

    logger.debug("Setting up SPI", "Ade7953::begin");
    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);
    SPI.setClockDivider(SPI_CLOCK_DIV64); // 64div -> 250kHz on 16MHz clock, but on 80MHz clock it's 1.25MHz. Max Ade7953 clock is 2MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    digitalWrite(_ssPin, HIGH);

    _reset();

    logger.debug("Verifying communication with Ade7953", "Ade7953::begin");
    if (!_verifyCommunication()) {
        logger.error("Failed to communicate with Ade7953", "Ade7953::begin");
        return false;
    }
    logger.debug("Successfully initialized Ade7953", "Ade7953::begin");
    
    logger.debug("Setting optimum settings", "Ade7953::begin");
    _setOptimumSettings();
    logger.debug("Successfully set optimum settings", "Ade7953::begin");

    logger.debug("Setting default configuration", "Ade7953::begin");
    _setDefaultLycmode();
    logger.debug("Successfully set default configuration", "Ade7953::begin");    

    logger.debug("Setting default no-load feature", "Ade7953::begin");
    _setDefaultNoLoadFeature();
    logger.debug("Successfully set no-load feature", "Ade7953::begin");
    
    logger.debug("Setting default config register", "Ade7953::begin");
    _setDefaultConfigRegister();
    logger.debug("Successfully set config register", "Ade7953::begin");

    logger.debug("Setting PGA gains", "Ade7953::begin");
    _setDefaultPgaGain();
    logger.debug("Successfully set PGA gains", "Ade7953::begin");

    logger.debug("Setting configuration from spiffs", "Ade7953::begin");
    _setConfigurationFromSpiffs();
    logger.debug("Done setting configuration from spiffs", "Ade7953::begin");
 
    logger.debug("Reading calibration values from SPIFFS", "Ade7953::begin");
    _setCalibrationValuesFromSpiffs();
    logger.debug("Done reading calibration values from SPIFFS", "Ade7953::begin");

    logger.debug("Reading energy from SPIFFS", "Ade7953::begin");
    setEnergyFromSpiffs();
    logger.debug("Done reading energy from SPIFFS", "Ade7953::begin");

    logger.debug("Reading channel data from SPIFFS", "Ade7953::begin");
    _setChannelDataFromSpiffs();
    logger.debug("Done reading channel data from SPIFFS", "Ade7953::begin");

    saveEnergyTicker.attach(ENERGY_SAVE_INTERVAL, [](){ saveEnergyFlag = true; }); // Inside ticker callbacks, only flags should be set

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
        saveDailyEnergyToSpiffs();
        saveEnergyFlag = false;
    }
}

void Ade7953::_reset() {
    logger.debug("Resetting Ade7953", "Ade7953::_reset");
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
        logger.error("Failed to communicate with Ade7953", "Ade7953::_verifyCommunication");
        return false;
    }
    logger.debug("Successfully communicated with Ade7953", "Ade7953::_verifyCommunication");
    return true;
}

// Configuration
// --------------------

void Ade7953::setDefaultConfiguration() {
    logger.debug("Setting default configuration", "Ade7953::setDefaultConfiguration");

    configuration.expectedApNoLoad = DEFAULT_EXPECTED_AP_NOLOAD_REGISTER;
    configuration.xNoLoad = DEFAULT_X_NOLOAD_REGISTER;
    configuration.disNoLoad = DEFAULT_DISNOLOAD_REGISTER;
    configuration.lcycMode = DEFAULT_LCYCMODE_REGISTER;
    configuration.linecyc = DEFAULT_LINECYC_REGISTER;
    configuration.pga = DEFAULT_PGA_REGISTER;
    configuration.config = DEFAULT_CONFIG_REGISTER;
    configuration.aWGain = DEFAULT_AWGAIN;
    configuration.aWattOs = DEFAULT_AWATTOS;
    configuration.aVarGain = DEFAULT_AVARGAIN;
    configuration.aVarOs = DEFAULT_AVAROS;
    configuration.aVaGain = DEFAULT_AVAGAIN;
    configuration.aVaOs = DEFAULT_AVAOS;
    configuration.aIGain = DEFAULT_AIGAIN;
    configuration.aIRmsOs = DEFAULT_AIRMSOS;
    configuration.bIGain = DEFAULT_BIGAIN;
    configuration.bIRmsOs = DEFAULT_BIRMSOS;
    configuration.phCalA = DEFAULT_PHCALA;
    configuration.phCalB = DEFAULT_PHCALB;

    _applyConfiguration();

    logger.debug("Default configuration set", "Ade7953::setDefaultConfiguration");
}

void Ade7953::_setConfigurationFromSpiffs() {
    logger.debug("Setting configuration from SPIFFS", "Ade7953::_setConfigurationFromSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CONFIGURATION_ADE7953_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.error("Failed to read configuration from SPIFFS. Keeping default one", "Ade7953::_setConfigurationFromSpiffs");
        setDefaultConfiguration();
    } else {
        setConfiguration(parseJsonConfiguration(_jsonDocument));
        logger.debug("Successfully read and set configuration from SPIFFS", "Ade7953::_setConfigurationFromSpiffs");
    }
}

void Ade7953::setConfiguration(Ade7953Configuration newConfiguration) {
    logger.debug("Setting configuration", "Ade7953::setConfiguration");
    configuration = newConfiguration;

    _applyConfiguration();

    if (!saveConfigurationToSpiffs()) {
        logger.error("Failed to save configuration to SPIFFS", "Ade7953::setConfiguration");
    } else {
        logger.debug("Successfully saved configuration to SPIFFS", "Ade7953::setConfiguration");
    }
}

void Ade7953::_applyConfiguration() {
    logger.debug("Applying configuration", "Ade7953::applyConfiguration");

    setLinecyc(configuration.linecyc);
    setGain(configuration.aWGain, CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    setOffset(configuration.aWattOs, CHANNEL_A, ACTIVE_POWER_MEASUREMENT);
    setGain(configuration.aVarGain, CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    setOffset(configuration.aVarOs, CHANNEL_A, REACTIVE_POWER_MEASUREMENT);
    setGain(configuration.aVaGain, CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    setOffset(configuration.aVaOs, CHANNEL_A, APPARENT_POWER_MEASUREMENT);
    setGain(configuration.aIGain, CHANNEL_A, CURRENT_MEASUREMENT);
    setOffset(configuration.aIRmsOs, CHANNEL_A, CURRENT_MEASUREMENT);
    setGain(configuration.bIGain, CHANNEL_B, CURRENT_MEASUREMENT);
    setOffset(configuration.bIRmsOs, CHANNEL_B, CURRENT_MEASUREMENT);
    setPhaseCalibration(configuration.phCalA, CHANNEL_A);
    setPhaseCalibration(configuration.phCalB, CHANNEL_B);

    logger.debug("Successfully applied configuration", "Ade7953::applyConfiguration");
}

bool Ade7953::saveConfigurationToSpiffs() {
    logger.debug("Saving configuration to SPIFFS", "Ade7953::saveConfigurationToSpiffs");

    if (serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, configurationToJson())) {
        logger.debug("Successfully serialize the JSON configuration to SPIFFS", "Ade7953::saveConfigurationToSpiffs");
        return true;
    } else {
        logger.error("Failed to serialize the JSON configuration to SPIFFS", "Ade7953::saveConfigurationToSpiffs");
        return false;
    }
}

JsonDocument Ade7953::configurationToJson() {
    logger.debug("Converting configuration to JSON", "Ade7953::configurationToJson");

    JsonDocument _jsonDocument;

    _jsonDocument["xNoLoad"] = configuration.xNoLoad;
    _jsonDocument["disNoLoad"] = configuration.disNoLoad;
    _jsonDocument["lcycMode"] = configuration.lcycMode;
    _jsonDocument["linecyc"] = configuration.linecyc;
    _jsonDocument["pga"] = configuration.pga;
    _jsonDocument["config"] = configuration.config;
    _jsonDocument["aWGain"] = configuration.aWGain;
    _jsonDocument["aWattOs"] = configuration.aWattOs;
    _jsonDocument["aVarGain"] = configuration.aVarGain;
    _jsonDocument["aVarOs"] = configuration.aVarOs;
    _jsonDocument["aVaGain"] = configuration.aVaGain;
    _jsonDocument["aVaOs"] = configuration.aVaOs;
    _jsonDocument["aIGain"] = configuration.aIGain;
    _jsonDocument["aIRmsOs"] = configuration.aIRmsOs;
    _jsonDocument["bIGain"] = configuration.bIGain;
    _jsonDocument["bIRmsOs"] = configuration.bIRmsOs;
    _jsonDocument["phCalA"] = configuration.phCalA;
    _jsonDocument["phCalB"] = configuration.phCalB;

    return _jsonDocument;
}

Ade7953Configuration Ade7953::parseJsonConfiguration(JsonDocument jsonDocument) {
    logger.debug("Parsing JSON configuration", "Ade7953::parseJsonConfiguration");

    Ade7953Configuration newConfiguration;

    newConfiguration.xNoLoad = jsonDocument["xNoLoad"].as<long>();
    newConfiguration.disNoLoad = jsonDocument["disNoLoad"].as<long>();
    newConfiguration.lcycMode = jsonDocument["lcycMode"].as<long>();
    newConfiguration.linecyc = jsonDocument["linecyc"].as<long>();
    newConfiguration.pga = jsonDocument["pga"].as<long>();
    newConfiguration.config = jsonDocument["config"].as<long>();
    newConfiguration.aWGain = jsonDocument["aWGain"].as<long>();
    newConfiguration.aWattOs = jsonDocument["aWattOs"].as<long>();
    newConfiguration.aVarGain = jsonDocument["aVarGain"].as<long>();
    newConfiguration.aVarOs = jsonDocument["aVarOs"].as<long>();
    newConfiguration.aVaGain = jsonDocument["aVaGain"].as<long>();
    newConfiguration.aVaOs = jsonDocument["aVaOs"].as<long>();
    newConfiguration.aIGain = jsonDocument["aIGain"].as<long>();
    newConfiguration.aIRmsOs = jsonDocument["aIRmsOs"].as<long>();
    newConfiguration.bIGain = jsonDocument["bIGain"].as<long>();
    newConfiguration.bIRmsOs = jsonDocument["bIRmsOs"].as<long>();
    newConfiguration.phCalA = jsonDocument["phCalA"].as<long>();
    newConfiguration.phCalB = jsonDocument["phCalB"].as<long>();

    return newConfiguration;
}

// Calibration values
// --------------------

void Ade7953::setDefaultCalibrationValues() {
    logger.debug("Setting default calibration values", "Ade7953::setDefaultCalibrationValues");
    
    CalibrationValues _defaultValue;

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, DEFAULT_CALIBRATION_JSON);

    setCalibrationValues(parseJsonCalibrationValues(_jsonDocument));

    logger.debug("Successfully set default calibration values", "Ade7953::setDefaultCalibrationValues");
}

void Ade7953::_setCalibrationValuesFromSpiffs() {
    logger.debug("Setting calibration values from SPIFFS", "Ade7953::_setCalibrationValuesFromSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CALIBRATION_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.error("Failed to read calibration values from SPIFFS. Setting default ones...", "Ade7953::_setCalibrationValuesFromSpiffs");
        setDefaultCalibrationValues();
    } else {
        logger.debug("Successfully read calibration values from SPIFFS. Setting values...", "Ade7953::_setCalibrationValuesFromSpiffs");
        setCalibrationValues(parseJsonCalibrationValues(_jsonDocument));
    }
}

bool Ade7953::saveCalibrationValuesToSpiffs() {
    logger.debug("Saving calibration values to SPIFFS", "Ade7953::saveCalibrationValuesToSpiffs");

    if (serializeJsonToSpiffs(CALIBRATION_JSON_PATH, calibrationValuesToJson())) {
        logger.debug("Successfully saved calibration values to SPIFFS", "Ade7953::saveCalibrationValuesToSpiffs");
        return true;
    } else {
        logger.error("Failed to save calibration values to SPIFFS", "Ade7953::saveCalibrationValuesToSpiffs");
        return false;
    }
}

void Ade7953::setCalibrationValues(std::vector<CalibrationValues> newCalibrationValues) { // TODO: This is quite convoluted and can be improved
    logger.debug("Setting new calibration values", "Ade7953::setCalibrationValues");

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
        logger.error("Failed to save calibration values to SPIFFS", "Ade7953::setCalibrationValues");
    } else {
        logger.debug("Successfully saved calibration values to SPIFFS", "Ade7953::setCalibrationValues");
    }

    logger.debug("Successfully set new calibration values", "Ade7953::setCalibrationValues");
}

JsonDocument Ade7953::calibrationValuesToJson() {
    logger.debug("Converting calibration values to JSON", "Ade7953::calibrationValuesToJson");

    JsonDocument _jsonDocument;

    for (auto& calibrationValue : calibrationValues) {
        _jsonDocument[calibrationValue.label]["vLsb"] = calibrationValue.vLsb;
        _jsonDocument[calibrationValue.label]["aLsb"] = calibrationValue.aLsb;
        _jsonDocument[calibrationValue.label]["wLsb"] = calibrationValue.wLsb;
        _jsonDocument[calibrationValue.label]["varLsb"] = calibrationValue.varLsb;
        _jsonDocument[calibrationValue.label]["vaLsb"] = calibrationValue.vaLsb;
        _jsonDocument[calibrationValue.label]["whLsb"] = calibrationValue.whLsb;
        _jsonDocument[calibrationValue.label]["varhLsb"] = calibrationValue.varhLsb;
        _jsonDocument[calibrationValue.label]["vahLsb"] = calibrationValue.vahLsb;
    }

    return _jsonDocument;
}

std::vector<CalibrationValues> Ade7953::parseJsonCalibrationValues(JsonDocument jsonDocument) {
    logger.debug("Parsing JSON calibration values", "Ade7953::parseJsonCalibrationValues");
    
    JsonObject _jsonObject = jsonDocument.as<JsonObject>();

    std::vector<CalibrationValues> calibrationValuesVector;

    for (JsonPair _kv : _jsonObject) {

        CalibrationValues _calibrationValues;
        
        _calibrationValues.label = _kv.key().c_str();
        _calibrationValues.vLsb = _kv.value()["vLsb"].as<float>();
        _calibrationValues.aLsb = _kv.value()["aLsb"].as<float>();
        _calibrationValues.wLsb = _kv.value()["wLsb"].as<float>();
        _calibrationValues.varLsb = _kv.value()["varLsb"].as<float>();
        _calibrationValues.vaLsb = _kv.value()["vaLsb"].as<float>();
        _calibrationValues.whLsb = _kv.value()["whLsb"].as<float>();
        _calibrationValues.varhLsb = _kv.value()["varhLsb"].as<float>();
        _calibrationValues.vahLsb = _kv.value()["vahLsb"].as<float>();

        logger.debug("Calibration %s parsed", "Ade7953::parseJsonCalibrationValues", _calibrationValues.label.c_str());

        calibrationValuesVector.push_back(_calibrationValues);
    }

    logger.debug("Successfully parsed JSON calibration values", "Ade7953::parseJsonCalibrationValues");
    
    return calibrationValuesVector;
}

// Data channel
// --------------------

void Ade7953::setDefaultChannelData() {
    logger.debug("Setting default data channel %s", "Ade7953::setDefaultChannelData", DEFAULT_CHANNEL_JSON);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, DEFAULT_CHANNEL_JSON);

    ChannelData _defaultValue;

    setChannelData(parseJsonChannelData(_jsonDocument));    

    logger.debug("Successfully initialized data channel", "Ade7953::setDefaultChannelData");
}

void Ade7953::_setChannelDataFromSpiffs() {
    logger.debug("Setting data channel from SPIFFS", "Ade7953::_setChannelDataFromSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(CHANNEL_DATA_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.error("Failed to read data channel from SPIFFS. Setting default one", "Ade7953::_setChannelDataFromSpiffs");
        setDefaultChannelData();
    } else {
        logger.debug("Successfully read data channel from SPIFFS. Setting values...", "Ade7953::_setChannelDataFromSpiffs");
        
        setChannelData(parseJsonChannelData(_jsonDocument));
    }
    updateDataChannel();
}

void Ade7953::setChannelData(ChannelData* newChannelData) {
    logger.debug("Setting data channel", "Ade7953::setChannelData");
    for(int i = 0; i < CHANNEL_COUNT; i++) {
        channelData[i] = newChannelData[i];
    }
    saveChannelDataToSpiffs();
    logger.debug("Successfully set data channel", "Ade7953::setChannelData");
}

bool Ade7953::saveChannelDataToSpiffs() {
    logger.debug("Saving data channel to SPIFFS", "Ade7953::saveChannelDataToSpiffs");

    if (serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, channelDataToJson())) {
        logger.debug("Successfully saved data channel to SPIFFS", "Ade7953::saveChannelDataToSpiffs");
        return true;
    } else {
        logger.error("Failed to save data channel to SPIFFS", "Ade7953::saveChannelDataToSpiffs");
        return false;
    }
}

JsonDocument Ade7953::channelDataToJson() {
    logger.debug("Converting data channel to JSON", "Ade7953::channelDataToJson");

    JsonDocument _jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[i]["active"] = channelData[i].active;
        _jsonDocument[i]["reverse"] = channelData[i].reverse;
        _jsonDocument[i]["label"] = channelData[i].label;
        _jsonDocument[i]["calibrationLabel"] = channelData[i].calibrationValues.label;
    }

    logger.debug("Successfully converted data channel to JSON", "Ade7953::channelDataToJson");
    return _jsonDocument;
}

ChannelData* Ade7953::parseJsonChannelData(JsonDocument jsonDocument) {
    logger.debug("Parsing JSON data channel", "Ade7953::parseJsonChannelData");

    ChannelData* _channelData = new ChannelData[CHANNEL_COUNT];

    JsonObject _jsonObject = jsonDocument.as<JsonObject>();

    for (JsonPair _kv : _jsonObject) {
        int _index = atoi(_kv.key().c_str());

        // Check if _index is within bounds
        if (_index < 0 || _index >= CHANNEL_COUNT) {
            logger.error("Index out of bounds: %d", "Ade7953::parseJsonChannelData", _index);
            continue;
        }

        logger.debug("Parsing data channel %d", "Ade7953::parseJsonChannelData", _index);

        _channelData[_index].index = _index;
        _channelData[_index].active = _kv.value()["active"].as<bool>();
        _channelData[_index].reverse = _kv.value()["reverse"].as<bool>();
        _channelData[_index].label = _kv.value()["label"].as<String>();
        _channelData[_index].calibrationValues = findCalibrationValue(_kv.value()["calibrationLabel"].as<String>());
    }

    logger.debug("Successfully parsed JSON data channel", "Ade7953::parseJsonChannelData");
    return _channelData;
}

void Ade7953::updateDataChannel() {
    logger.debug("Updating data channel", "Ade7953::updateDataChannel");

    String _previousLabel; 
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _previousLabel = channelData[i].calibrationValues.label;
        channelData[i].calibrationValues = findCalibrationValue(_previousLabel);
    }
    
    _updateSampleTime();
}

void Ade7953::_updateSampleTime() {
    logger.debug("Updating sample time", "Ade7953::updateSampleTime");

    int _activeChannelCount = getActiveChannelCount();
    if (_activeChannelCount > 0) {
        long linecyc = long(generalConfiguration.sampleCycles / _activeChannelCount);
        configuration.linecyc = linecyc;
        _applyConfiguration();
        saveConfigurationToSpiffs();
        logger.debug("Successfully updated sample time", "Ade7953::updateSampleTime");
    } else {
        logger.error("No active channels found, sample time not updated", "Ade7953::updateSampleTime");
    }
}


CalibrationValues Ade7953::findCalibrationValue(String label) {
    logger.debug("Finding calibration %s", "Ade7953::findCalibrationValue", label.c_str());
    for (auto& calibrationValue : calibrationValues) {
        logger.debug("Checking calibration %s", "Ade7953::findCalibrationValue", calibrationValue.label.c_str());
        if (calibrationValue.label == label) {
            logger.debug("Calibration %s found", "Ade7953::findCalibrationValue", label.c_str());
            return calibrationValue;
        }
    }

    logger.warning("Calibration %s not found, returning default calibration", "Ade7953::findCalibrationValue", label.c_str());
    return calibrationValues[0];
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
    logger.debug("No active channel found, returning current channel", "Ade7953::findNextActiveChannel");
    return currentChannel;
}

int Ade7953::getActiveChannelCount() {
    int _activeChannelCount = 0;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (channelData[i].active) {
            _activeChannelCount++;
        }
    }
    return _activeChannelCount;
}


// Meter values
// --------------------

void Ade7953::_initializeMeterValues() {
    logger.debug("Initializing meter values", "Ade7953::_initializeMeterValues");
    for (int i = 0; i < CHANNEL_COUNT; i++) {
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
    long _deltaMillis = _currentMillis - meterValues[channel].lastMillis;
    meterValues[channel].lastMillis = _currentMillis;

    int _ade7953Channel = (channel == 0) ? CHANNEL_A : CHANNEL_B;

    float _voltage = readVoltageRms() * channelData[channel].calibrationValues.vLsb;
    float _current = readCurrentRms(_ade7953Channel) * channelData[channel].calibrationValues.aLsb;
    float _activePower = readActivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.wLsb * (channelData[channel].reverse ? -1 : 1);
    float _reactivePower = readReactivePowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.varLsb * (channelData[channel].reverse ? -1 : 1);
    float _apparentPower = readApparentPowerInstantaneous(_ade7953Channel) * channelData[channel].calibrationValues.vaLsb;
    float _powerFactor = readPowerFactor(_ade7953Channel) * POWER_FACTOR_CONVERSION_FACTOR * (channelData[channel].reverse ? -1 : 1);

    meterValues[channel].voltage = _validateVoltage(meterValues[channel].voltage, _voltage);
    meterValues[channel].current = _validateCurrent(meterValues[channel].current, _current);
    meterValues[channel].activePower = _validatePower(meterValues[channel].activePower, _activePower);
    meterValues[channel].reactivePower = _validatePower(meterValues[channel].reactivePower, _reactivePower);
    meterValues[channel].apparentPower = _validatePower(meterValues[channel].apparentPower, _apparentPower);
    meterValues[channel].powerFactor = _validatePowerFactor(meterValues[channel].powerFactor, _powerFactor);
    
    float _activeEnergy = readActiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.whLsb;
    float _reactiveEnergy = readReactiveEnergy(_ade7953Channel) * channelData[channel].calibrationValues.varhLsb;
    float _apparentEnergy = readApparentEnergy(_ade7953Channel) * channelData[channel].calibrationValues.vahLsb;

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
        logger.warning("Value %f out of range (minimum: %f, maximum: %f). Keeping old value %f", "Ade7953::_validateValue", newValue, min, max, oldValue);
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

void Ade7953::setEnergyFromSpiffs() {
    logger.debug("Reading energy from SPIFFS", "Ade7953::readEnergyFromSpiffs");
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(ENERGY_JSON_PATH);
    if (_jsonDocument.isNull()) {
        logger.error("Failed to read energy from SPIFFS", "Ade7953::readEnergyFromSpiffs");
    } else {
        logger.debug("Successfully read energy from SPIFFS", "Ade7953::readEnergyFromSpiffs");
        JsonObject _jsonObject = _jsonDocument.as<JsonObject>();
        for (int i = 0; i < CHANNEL_COUNT; i++) {
            meterValues[i].activeEnergy = _jsonObject[String(i)]["activeEnergy"].as<float>();
            meterValues[i].reactiveEnergy = _jsonObject[String(i)]["reactiveEnergy"].as<float>();
            meterValues[i].apparentEnergy = _jsonObject[String(i)]["apparentEnergy"].as<float>();
        }
    }
}

void Ade7953::saveEnergyToSpiffs() {
    logger.debug("Saving energy to SPIFFS", "Ade7953::saveEnergyToSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(ENERGY_JSON_PATH);

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["activeEnergy"] = meterValues[i].activeEnergy;
        _jsonDocument[String(i)]["reactiveEnergy"] = meterValues[i].reactiveEnergy;
        _jsonDocument[String(i)]["apparentEnergy"] = meterValues[i].apparentEnergy;
    }

    if (serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument)) {
        logger.debug("Successfully saved energy to SPIFFS", "Ade7953::saveEnergyToSpiffs");
    } else {
        logger.error("Failed to save energy to SPIFFS", "Ade7953::saveEnergyToSpiffs");
    }
}

void Ade7953::saveDailyEnergyToSpiffs() {
    logger.debug("Saving daily energy to SPIFFS", "Ade7953::saveDailyEnergyToSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(DAILY_ENERGY_JSON_PATH);
    
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
        logger.debug("Successfully saved daily energy to SPIFFS", "Ade7953::saveDailyEnergyToSpiffs");
    } else {
        logger.error("Failed to save daily energy to SPIFFS", "Ade7953::saveDailyEnergyToSpiffs");
    }
}

void Ade7953::resetEnergyValues() {
    logger.warning("Resetting energy values to 0", "Ade7953::resetEnergyValues");

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        meterValues[i].activeEnergy = 0.0;
        meterValues[i].reactiveEnergy = 0.0;
        meterValues[i].apparentEnergy = 0.0;
    }
    saveEnergyToSpiffs();
}


// Others
// --------------------

void Ade7953::setLinecyc(long linecyc) {
    linecyc = min(max(linecyc, 1L), 1000L); // Linecyc must be between reasonable values, 1 and 1000

    logger.debug(
        "Setting linecyc to %d",
        "Ade7953::setLinecyc",
        linecyc
    );

    writeRegister(LINECYC_16, 16, linecyc);
}

void Ade7953::setPhaseCalibration(long phaseCalibration, int channel) {
    logger.debug(
        "Setting phase calibration to %d on channel %d",
        "Ade7953::setPhaseCalibration",
        phaseCalibration,
        channel
    );
    
    if (channel == CHANNEL_A) {
        writeRegister(PHCALA_16, 16, phaseCalibration);
    } else {
        writeRegister(PHCALB_16, 16, phaseCalibration);
    }
}

void Ade7953::setPgaGain(long pgaGain, int channel, int measurementType) {
    logger.debug(
        "Setting PGA gain to %d on channel %d for measurement type %d",
        "Ade7953::setPgaGain",
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

void Ade7953::setGain(long gain, int channel, int measurementType) {
    logger.debug(
        "Setting gain to %d on channel %d for measurement type %d",
        "Ade7953::setGain",
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

void Ade7953::setOffset(long offset, int channel, int measurementType) {
    logger.debug(
        "Setting offset to %ld on channel %d for measurement type %d",
        "Ade7953::setOffset",
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
    logger.verbose(
        "Read %ld from register %ld with %d bits",
        "Ade7953::readRegister",
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
    logger.debug(
        "Writing %l to register %d with %d bits",
        "Ade7953::writeRegister",
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