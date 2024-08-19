#include "utils.h"

extern AdvancedLogger logger;
extern CustomTime customTime;
extern Led led;
extern Ade7953 ade7953;

extern GeneralConfiguration generalConfiguration;

JsonDocument getProjectInfo() {
    JsonDocument _jsonDocument;

    _jsonDocument["companyName"] = COMPANY_NAME;
    _jsonDocument["fullProductName"] = FULL_PRODUCT_NAME;
    _jsonDocument["productName"] = PRODUCT_NAME;
    _jsonDocument["productDescription"] = PRODUCT_DESCRIPTION;
    _jsonDocument["productUrl"] = PRODUCT_URL;
    _jsonDocument["githubUrl"] = GITHUB_URL;
    _jsonDocument["author"] = AUTHOR;
    _jsonDocument["authorEmail"] = AUTHOR_EMAIL;
    _jsonDocument["copyrightYear"] = COPYRIGHT_YEAR;
    _jsonDocument["copyrightHolder"] = COPYRIGHT_HOLDER;
    _jsonDocument["license"] = LICENSE;
    _jsonDocument["licenseUrl"] = LICENSE_URL;

    return _jsonDocument;
}

JsonDocument getDeviceInfo()
{
    JsonDocument _jsonDocument;

    JsonObject _jsonSystem = _jsonDocument["system"].to<JsonObject>();
    _jsonSystem["uptime"] = millis();
    _jsonSystem["systemTime"] = customTime.getTimestamp();

    JsonObject _jsonFirmware = _jsonDocument["firmware"].to<JsonObject>();
    _jsonFirmware["buildVersion"] = FIRMWARE_BUILD_VERSION;
    _jsonFirmware["buildDate"] = FIRMWARE_BUILD_DATE;

    JsonObject _jsonMemory = _jsonDocument["memory"].to<JsonObject>();
    JsonObject _jsonHeap = _jsonMemory["heap"].to<JsonObject>();
    _jsonHeap["free"] = ESP.getFreeHeap();
    _jsonHeap["total"] = ESP.getHeapSize();
    JsonObject _jsonSpiffs = _jsonMemory["spiffs"].to<JsonObject>();
    _jsonSpiffs["free"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    _jsonSpiffs["total"] = SPIFFS.totalBytes();

    JsonObject _jsonChip = _jsonDocument["chip"].to<JsonObject>();
    _jsonChip["model"] = ESP.getChipModel();
    _jsonChip["revision"] = ESP.getChipRevision();
    _jsonChip["cpuFrequency"] = ESP.getCpuFreqMHz();
    _jsonChip["sdkVersion"] = ESP.getSdkVersion();
    _jsonChip["id"] = ESP.getEfuseMac();

    return _jsonDocument;
}

JsonDocument deserializeJsonFromSpiffs(const char* path){
    logger.debug("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs");

    File _file = SPIFFS.open(path, FILE_READ);
    if (!_file){
        logger.error("Failed to open file %s", "utils::deserializeJsonFromSpiffs", path);

        return JsonDocument();
    }
    JsonDocument _jsonDocument;

    DeserializationError _error = deserializeJson(_jsonDocument, _file);
    _file.close();
    if (_error){
        logger.error("Failed to deserialize file %s. Error: %s", "utils::deserializeJsonFromSpiffs", path, _error.c_str());
        
        return JsonDocument();
    }

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0){
        logger.warning("%s is empty", "utils::deserializeJsonFromSpiffs", path);
    }
    
    String _jsonString;
    serializeJson(_jsonDocument, _jsonString);
    logger.debug("JSON deserialized from SPIFFS correctly: %s", "utils::serializeJsonToSpiffs", _jsonString.c_str());

    return _jsonDocument;
}

bool serializeJsonToSpiffs(const char* path, JsonDocument _jsonDocument){
    logger.debug("Serializing JSON to SPIFFS", "utils::serializeJsonToSpiffs");

    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file){
        logger.error("Failed to open file %s", "utils::serializeJsonToSpiffs", path);
        return false;
    }

    serializeJson(_jsonDocument, _file);
    _file.close();

    if (_jsonDocument.isNull()){
        logger.warning("%s is null", "utils::serializeJsonToSpiffs", path);
    }

    String _jsonString;
    serializeJson(_jsonDocument, _jsonString);
    logger.debug("JSON serialized to SPIFFS correctly: %s", "utils::serializeJsonToSpiffs", _jsonString.c_str());

    return true;
}

void createDefaultFiles() {
    logger.debug("Creating default files...", "utils::createDefaultFiles");

    SPIFFS.format();

    createDefaultConfigurationAde7953File();
    createDefaultGeneralConfigurationFile();
    createDefaultEnergyFile();
    createDefaultDailyEnergyFile();
    createDefaultFirmwareUpdateInfoFile();
    createDefaultFirmwareUpdateStatusFile();

    createFirstSetupFile();

    logger.debug("Default files created", "utils::createDefaultFiles");
}

void createDefaultConfigurationAde7953File() {
    logger.debug("Creating default %s...", "utils::createDefaultAde7953File", CONFIGURATION_ADE7953_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["expectedApNoLoad"] = DEFAULT_EXPECTED_AP_NOLOAD_REGISTER;
    _jsonDocument["xNoLoad"] = DEFAULT_X_NOLOAD_REGISTER;
    _jsonDocument["disNoLoad"] = DEFAULT_DISNOLOAD_REGISTER;
    _jsonDocument["lcycMode"] = DEFAULT_LCYCMODE_REGISTER;
    _jsonDocument["linecyc"] = DEFAULT_LINECYC_REGISTER;
    _jsonDocument["pga"] = DEFAULT_PGA_REGISTER;
    _jsonDocument["config"] = DEFAULT_CONFIG_REGISTER;
    _jsonDocument["aWGain"] = DEFAULT_AWGAIN;
    _jsonDocument["aWattOs"] = DEFAULT_AWATTOS;
    _jsonDocument["aVarGain"] = DEFAULT_AVARGAIN;
    _jsonDocument["aVarOs"] = DEFAULT_AVAROS;
    _jsonDocument["aVaGain"] = DEFAULT_AVAGAIN;
    _jsonDocument["aVaOs"] = DEFAULT_AVAOS;
    _jsonDocument["aIGain"] = DEFAULT_AIGAIN;
    _jsonDocument["aIRmsOs"] = DEFAULT_AIRMSOS;
    _jsonDocument["bIGain"] = DEFAULT_BIGAIN;
    _jsonDocument["bIRmsOs"] = DEFAULT_BIRMSOS;
    _jsonDocument["phCalA"] = DEFAULT_PHCALA;
    _jsonDocument["phCalB"] = DEFAULT_PHCALB;

    serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultAde7953File", CONFIGURATION_ADE7953_JSON_PATH);
}

void createDefaultGeneralConfigurationFile() {
    logger.debug("Creating default general %s...", "utils::createDefaultGeneralConfigurationFile", GENERAL_CONFIGURATION_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["sampleCycles"] = DEFAULT_SAMPLE_CYCLES;
    _jsonDocument["isCloudServicesEnabled"] = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    _jsonDocument["gmtOffset"] = DEFAULT_GMT_OFFSET;
    _jsonDocument["dstOffset"] = DEFAULT_DST_OFFSET;
    _jsonDocument["ledBrightness"] = DEFAULT_LED_BRIGHTNESS;

    serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultGeneralConfigurationFile", GENERAL_CONFIGURATION_JSON_PATH);
}

void createDefaultEnergyFile() {
    logger.debug("Creating default %s...", "utils::createDefaultEnergyFile", ENERGY_JSON_PATH);

    JsonDocument _jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) { // TODO: change to "0", "1", ...
        _jsonDocument[i]["activeEnergy"] = 0;
        _jsonDocument[i]["reactiveEnergy"] = 0;
        _jsonDocument[i]["apparentEnergy"] = 0;
    }

    serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultEnergyFile", ENERGY_JSON_PATH);
}

void createDefaultDailyEnergyFile() {
    logger.debug("Creating default %s...", "utils::createDefaultDailyEnergyFile", DAILY_ENERGY_JSON_PATH);

    JsonDocument _jsonDocument;
    _jsonDocument.to<JsonObject>(); // Empty JSON

    serializeJsonToSpiffs(DAILY_ENERGY_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultDailyEnergyFile", DAILY_ENERGY_JSON_PATH);
}

void createDefaultFirmwareUpdateInfoFile() {
    logger.debug("Creating default %s...", "utils::createDefaultFirmwareUpdateInfoFile", FIRMWARE_UPDATE_INFO_PATH);

    JsonDocument _jsonDocument;

    serializeJsonToSpiffs(FIRMWARE_UPDATE_INFO_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultFirmwareUpdateInfoFile", FIRMWARE_UPDATE_INFO_PATH);
}

void createDefaultFirmwareUpdateStatusFile() {
    logger.debug("Creating default %s...", "utils::createDefaultFirmwareUpdateStatusFile", FIRMWARE_UPDATE_STATUS_PATH);

    JsonDocument _jsonDocument;

    serializeJsonToSpiffs(FIRMWARE_UPDATE_STATUS_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultFirmwareUpdateStatusFile", FIRMWARE_UPDATE_STATUS_PATH);
}

void createFirstSetupFile() {
    logger.debug("Creating %s...", "utils::createFirstSetupFile", FIRST_SETUP_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["isFirstTime"] = false;
    _jsonDocument["timestampFirstTime"] = customTime.getTimestamp();

    serializeJsonToSpiffs(FIRST_SETUP_PATH, _jsonDocument);

    logger.debug("%s created", "utils::createFirstSetupFile", FIRST_SETUP_PATH);
}

bool checkIfFirstSetup() {
    logger.debug("Checking if first setup...", "main::checkIfFirstSetup");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(FIRST_SETUP_PATH);

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        logger.debug("First setup file is empty", "main::checkIfFirstSetup");
        return true;
    }

    return _jsonDocument["isFirstTime"].as<bool>();
}

bool checkAllFiles() {
    logger.debug("Checking all files...", "main::checkAllFiles");

    if (!SPIFFS.exists(FIRST_SETUP_PATH)) return true;
    if (!SPIFFS.exists(GENERAL_CONFIGURATION_JSON_PATH)) return true;
    if (!SPIFFS.exists(CONFIGURATION_ADE7953_JSON_PATH)) return true;
    if (!SPIFFS.exists(CALIBRATION_JSON_PATH)) return true;
    if (!SPIFFS.exists(CHANNEL_DATA_JSON_PATH)) return true;
    if (!SPIFFS.exists(ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(DAILY_ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(FIRMWARE_UPDATE_INFO_PATH)) return true;
    if (!SPIFFS.exists(FIRMWARE_UPDATE_STATUS_PATH)) return true;

    return false;
}

void restartEsp32(const char* functionName, const char* reason) { //TODO: modify this to set a flag and reboot in the main loop
    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setRed(true);

    publishMeter();
    
    if (functionName != "utils::factoryReset") {
        ade7953.saveDailyEnergyToSpiffs();
        ade7953.saveEnergyToSpiffs();
    }
    logger.fatal("Restarting ESP32 from function %s. Reason: %s", "main::restartEsp32", functionName, reason);

    ESP.restart();
}

// Print functions
// -----------------------------

void printMeterValues(MeterValues meterValues, const char* channelLabel) {
    logger.verbose(
        "%s: %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh | %.3f VARh | %.3f VAh", 
        "main::printMeterValues", 
        channelLabel, 
        meterValues.voltage, 
        meterValues.current, 
        meterValues.activePower, 
        meterValues.reactivePower, 
        meterValues.apparentPower, 
        meterValues.powerFactor, 
        meterValues.activeEnergy, 
        meterValues.reactiveEnergy, 
        meterValues.apparentEnergy
    );
}

void printDeviceStatus()
{

    JsonDocument _jsonDocument = getDeviceInfo();

    logger.debug(
        "Free heap: %d bytes | Total heap: %d bytes || Free SPIFFS: %d bytes | Total SPIFFS: %d bytes",
        "main::printDeviceStatus",
        _jsonDocument["memory"]["heap"]["free"].as<int>(),
        _jsonDocument["memory"]["heap"]["total"].as<int>(),
        _jsonDocument["memory"]["spiffs"]["free"].as<int>(),
        _jsonDocument["memory"]["spiffs"]["total"].as<int>()
    );
}

// General configuration
// -----------------------------

void setDefaultGeneralConfiguration() {
    logger.debug("Setting default general configuration...", "utils::setDefaultGeneralConfiguration");
    
    generalConfiguration.sampleCycles = DEFAULT_SAMPLE_CYCLES;
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    generalConfiguration.gmtOffset = DEFAULT_GMT_OFFSET;
    generalConfiguration.dstOffset = DEFAULT_DST_OFFSET;
    generalConfiguration.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    
    logger.debug("Default general configuration set", "utils::setDefaultGeneralConfiguration");
}

void setGeneralConfiguration(GeneralConfiguration newGeneralConfiguration) {
    logger.debug("Setting general configuration...", "utils::setGeneralConfiguration");

    GeneralConfiguration previousConfiguration = generalConfiguration;
    generalConfiguration = newGeneralConfiguration;

    applyGeneralConfiguration();

    saveGeneralConfigurationToSpiffs();
    publishGeneralConfiguration();

    if (checkIfRebootRequiredGeneralConfiguration(previousConfiguration, newGeneralConfiguration)) {
        restartEsp32("utils::setGeneralConfiguration", "General configuration set with reboot required");
    }

    logger.debug("General configuration set", "utils::setGeneralConfiguration");
}

bool checkIfRebootRequiredGeneralConfiguration(GeneralConfiguration previousConfiguration, GeneralConfiguration newConfiguration) {
    if (previousConfiguration.isCloudServicesEnabled != newConfiguration.isCloudServicesEnabled) return true;

    return false;
}

bool setGeneralConfigurationFromSpiffs() {
    logger.debug("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs");

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.error("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs");
        return false;
    } else {
        setGeneralConfiguration(jsonToGeneralConfiguration(_jsonDocument));
        logger.debug("General configuration set from SPIFFS", "utils::setGeneralConfigurationFromSpiffs");
        return true;
    }
}

bool saveGeneralConfigurationToSpiffs() {
    logger.debug("Saving general configuration to SPIFFS...", "utils::saveGeneralConfigurationToSpiffs");

    JsonDocument _jsonDocument = generalConfigurationToJson(generalConfiguration);
    if (serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument)){
        logger.debug("General configuration saved to SPIFFS", "utils::saveGeneralConfigurationToSpiffs");
        return true;
    } else {
        logger.error("Failed to save general configuration to SPIFFS", "utils::saveGeneralConfigurationToSpiffs");
        return false;
    }
}

JsonDocument generalConfigurationToJson(GeneralConfiguration generalConfiguration) {
    JsonDocument _jsonDocument;
    
    _jsonDocument["sampleCycles"] = generalConfiguration.sampleCycles;
    _jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    _jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
    _jsonDocument["dstOffset"] = generalConfiguration.dstOffset;
    _jsonDocument["ledBrightness"] = generalConfiguration.ledBrightness;
    
    return _jsonDocument;
}

GeneralConfiguration jsonToGeneralConfiguration(JsonDocument _jsonDocument) {
    GeneralConfiguration _generalConfiguration;
    
    _generalConfiguration.sampleCycles = _jsonDocument["sampleCycles"] ? _jsonDocument["sampleCycles"].as<int>() : DEFAULT_SAMPLE_CYCLES;
    _generalConfiguration.isCloudServicesEnabled = _jsonDocument["isCloudServicesEnabled"] ? _jsonDocument["isCloudServicesEnabled"].as<bool>() : DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    _generalConfiguration.gmtOffset = _jsonDocument["gmtOffset"] ? _jsonDocument["gmtOffset"].as<int>() : DEFAULT_GMT_OFFSET;
    _generalConfiguration.dstOffset = _jsonDocument["dstOffset"] ? _jsonDocument["dstOffset"].as<int>() : DEFAULT_DST_OFFSET;
    _generalConfiguration.ledBrightness = _jsonDocument["ledBrightness"] ? _jsonDocument["ledBrightness"].as<int>() : DEFAULT_LED_BRIGHTNESS;

    return _generalConfiguration;
}

void applyGeneralConfiguration() {
    logger.debug("Applying general configuration...", "utils::applyGeneralConfiguration");

    led.setBrightness(generalConfiguration.ledBrightness);

    logger.debug("General configuration applied", "utils::applyGeneralConfiguration");
}

JsonDocument getPublicLocation() {
    HTTPClient http;

    JsonDocument _jsonDocument;

    http.begin(PUBLIC_LOCATION_ENDPOINT);
    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            payload.trim();
            
            deserializeJson(_jsonDocument, payload);

            logger.debug(
                "Location: %s, %s | Lat: %.4f | Lon: %.4f",
                "utils::getPublicLocation",
                _jsonDocument["city"].as<String>().c_str(),
                _jsonDocument["country"].as<String>().c_str(),
                _jsonDocument["lat"].as<float>(),
                _jsonDocument["lon"].as<float>()
            );
        }
    } else {
        logger.error("Error on HTTP request: %d", "utils::getPublicLocation", httpCode);
        deserializeJson(_jsonDocument, "{}");
    }

    http.end();

    return _jsonDocument;
}

// Helper functions
// -----------------------------

std::pair<int, int> getPublicTimezone() {
    int _gmtOffset;
    int _dstOffset;

    JsonDocument _jsonDocument = getPublicLocation();

    HTTPClient http;
    String _url = PUBLIC_TIMEZONE_ENDPOINT;
    _url += "lat=" + String(_jsonDocument["lat"].as<float>(), 4);
    _url += "&lng=" + String(_jsonDocument["lon"].as<float>(), 4);
    _url += "&username=" + String(PUBLIC_TIMEZONE_USERNAME);

    http.begin(_url);
    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            payload.trim();
            
            deserializeJson(_jsonDocument, payload);

            logger.debug(
                "GMT offset: %d | DST offset: %d",
                "utils::getPublicTimezone",
                _jsonDocument["rawOffset"].as<int>(),
                _jsonDocument["dstOffset"].as<int>()
            );

            _gmtOffset = _jsonDocument["rawOffset"].as<int>() * 3600; // Convert hours to seconds
            _dstOffset = _jsonDocument["dstOffset"].as<int>() * 3600 - _gmtOffset; // Convert hours to seconds. Remove GMT offset as it is already included in the dst offset
        }
    } else {
        logger.error(
            "Error on HTTP request: %d", 
            "utils::getPublicTimezone", 
            httpCode
        );
        deserializeJson(_jsonDocument, "{}");

        _gmtOffset = generalConfiguration.gmtOffset;
        _dstOffset = generalConfiguration.dstOffset;
    }

    return std::make_pair(_gmtOffset, _dstOffset);
}

void updateTimezone() {
    logger.debug("Updating timezone", "utils::updateTimezone");

    std::pair<int, int> _timezones = getPublicTimezone();

    generalConfiguration.gmtOffset = _timezones.first;
    generalConfiguration.dstOffset = _timezones.second;
    
    saveGeneralConfigurationToSpiffs();
}

void factoryReset() {
    logger.fatal("Factory reset requested", "utils::factoryReset");

    createDefaultFiles();

    restartEsp32("utils::factoryReset", "Factory reset completed. We are back to the good old days");
}

bool _duplicateFile(const char* sourcePath, const char* destinationPath) {
    logger.debug("Duplicating file", "utils::_duplicateFile");

    File _sourceFile = SPIFFS.open(sourcePath, FILE_READ);
    if (!_sourceFile) {
        logger.error("Failed to open source file: %s", "utils::_duplicateFile", sourcePath);
        return false;
    }

    File _destinationFile = SPIFFS.open(destinationPath, FILE_WRITE);
    if (!_destinationFile) {
        logger.error("Failed to open destination file: %s", "utils::_duplicateFile", destinationPath);
        return false;
    }

    while (_sourceFile.available()) {
        _destinationFile.write(_sourceFile.read());
    }

    _sourceFile.close();
    _destinationFile.close();

    logger.debug("File duplicated", "utils::_duplicateFile");
    return true;
}

bool isLatestFirmwareInstalled() {
    File _file = SPIFFS.open(FIRMWARE_UPDATE_INFO_PATH, FILE_READ);
    if (!_file) {
        logger.error("Failed to open firmware update info file", "utils::isLatestFirmwareInstalled");
        return false;
    }

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(FIRMWARE_UPDATE_INFO_PATH);

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        logger.debug("Firmware update info file is empty", "utils::isLatestFirmwareInstalled");
        return true;
    }

    String _latestFirmwareVersion = _jsonDocument["buildVersion"].as<String>();
    String _currentFirmwareVersion = FIRMWARE_BUILD_VERSION;

    logger.debug(
        "Latest firmware version: %s | Current firmware version: %s",
        "utils::isLatestFirmwareInstalled",
        _latestFirmwareVersion.c_str(),
        _currentFirmwareVersion.c_str()
    );

    if (_latestFirmwareVersion.length() == 0 || _latestFirmwareVersion.indexOf(".") == -1) {
        logger.warning("Latest firmware version is empty or in the wrong format", "utils::isLatestFirmwareInstalled");
        return true;
    }

    int _latestMajor, _latestMinor, _latestPatch;
    sscanf(_latestFirmwareVersion.c_str(), "%d.%d.%d", &_latestMajor, &_latestMinor, &_latestPatch);

    int _currentMajor = atoi(FIRMWARE_BUILD_VERSION_MAJOR);
    int _currentMinor = atoi(FIRMWARE_BUILD_VERSION_MINOR);
    int _currentPatch = atoi(FIRMWARE_BUILD_VERSION_PATCH);

    if (_latestMajor > _currentMajor) return false;
    if (_latestMinor > _currentMinor) return false;
    if (_latestPatch > _currentPatch) return false;

    return true;
}

String getDeviceId() {
    String _macAddress = WiFi.macAddress();
    _macAddress.replace(":", "");
    return _macAddress;
}