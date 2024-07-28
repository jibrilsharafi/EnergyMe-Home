#include "utils.h"

extern AdvancedLogger logger;
extern CustomTime customTime;
extern Led led;
extern Ade7953 ade7953;

extern GeneralConfiguration generalConfiguration;

JsonDocument getDeviceStatus()
{

    JsonDocument _jsonDocument;

    JsonObject _jsonSystem = _jsonDocument["system"].to<JsonObject>();
    _jsonSystem["uptime"] = millis();
    _jsonSystem["systemTime"] = customTime.getTimestamp();

    JsonObject _jsonFirmware = _jsonDocument["firmware"].to<JsonObject>();
    _jsonFirmware["version"] = FIRMWARE_VERSION;
    _jsonFirmware["date"] = FIRMWARE_DATE;

    JsonObject _jsonFilesystem = _jsonDocument["filesystem"].to<JsonObject>();
    JsonDocument _jsonMetadata = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonMetadata.isNull()){
        _jsonFilesystem["version"] = "unknown";
        _jsonFilesystem["date"] = "unknown";
    } else {
        _jsonFilesystem["version"] = _jsonMetadata["filesystem"]["version"].as<String>();
        _jsonFilesystem["date"] = _jsonMetadata["filesystem"]["date"].as<String>();
    }

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

    File _file = SPIFFS.open(path, "r");
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
    
    String _jsonString;
    serializeJson(_jsonDocument, _jsonString);
    logger.debug("JSON serialized to SPIFFS correctly: %s", "main::serializeJsonToSpiffs", _jsonString.c_str());
    return _jsonDocument;
}

bool serializeJsonToSpiffs(const char* path, JsonDocument _jsonDocument){
    logger.debug("Serializing JSON to SPIFFS", "utils::serializeJsonToSpiffs");

    File _file = SPIFFS.open(path, "w");
    if (!_file){
        logger.error("Failed to open file %s", "utils::serializeJsonToSpiffs", path);
        return false;
    }

    serializeJson(_jsonDocument, _file);
    _file.close();

    String _jsonString;
    serializeJson(_jsonDocument, _jsonString);
    logger.debug("JSON serialized to SPIFFS correctly: %s", "main::serializeJsonToSpiffs", _jsonString.c_str());
    return true;
}

void restartEsp32(const char* functionName, const char* reason) {

    led.block();
    led.setBrightness(LED_MAX_BRIGHTNESS);
    led.setRed(true);
    
    if (functionName != "utils::factoryReset") {
        ade7953.saveEnergyToSpiffs();
    }
    logger.fatal("Restarting ESP32 from function %s. Reason: %s", "main::restartEsp32", functionName, reason);

    ESP.restart();
}

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

    JsonDocument _jsonDocument = getDeviceStatus();

    logger.debug(
        "Free heap: %d bytes | Total heap: %d bytes || Free SPIFFS: %d bytes | Total SPIFFS: %d bytes",
        "main::printDeviceStatus",
        _jsonDocument["memory"]["heap"]["free"].as<int>(),
        _jsonDocument["memory"]["heap"]["total"].as<int>(),
        _jsonDocument["memory"]["spiffs"]["free"].as<int>(),
        _jsonDocument["memory"]["spiffs"]["total"].as<int>()
    );
}

bool checkIfFirstSetup() {
    logger.debug("Checking if first setup...", "main::checkIfFirstSetup");
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.error("Failed to open metadata.json", "main::checkIfFirstSetup");
        return false;
    }

    return _jsonDocument["setup"]["isFirstTime"].as<bool>();
}

void logFirstSetupComplete() {
    logger.debug("Logging first setup complete...", "main::logFirstSetupComplete");
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.error("Failed to open metadata.json", "main::logFirstSetupComplete");
        return;
    }

    _jsonDocument["setup"]["isFirstTime"] = false;
    _jsonDocument["setup"]["timestampFirstTime"] = customTime.getTimestamp();
    serializeJsonToSpiffs(METADATA_JSON_PATH, _jsonDocument);
    logger.debug("First setup complete", "main::logFirstSetupComplete");
}

// General configuration
// -----------------------------

void setDefaultGeneralConfiguration() {
    logger.debug("Setting default general configuration...", "utils::setDefaultGeneralConfiguration");
    
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    generalConfiguration.gmtOffset = DEFAULT_GMT_OFFSET;
    generalConfiguration.dstOffset = DEFAULT_DST_OFFSET;
    
    logger.debug("Default general configuration set", "utils::setDefaultGeneralConfiguration");
}

void setGeneralConfiguration(GeneralConfiguration newGeneralConfiguration) {
    logger.debug("Setting general configuration...", "utils::setGeneralConfiguration");

    generalConfiguration = newGeneralConfiguration;

    logger.debug("General configuration set", "utils::setGeneralConfiguration");
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
    
    _jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    _jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
    _jsonDocument["dstOffset"] = generalConfiguration.dstOffset;
    
    return _jsonDocument;
}

GeneralConfiguration jsonToGeneralConfiguration(JsonDocument _jsonDocument) {
    GeneralConfiguration _generalConfiguration;
    
    _generalConfiguration.isCloudServicesEnabled = _jsonDocument["isCloudServicesEnabled"].as<bool>();
    _generalConfiguration.gmtOffset = _jsonDocument["gmtOffset"].as<int>();
    _generalConfiguration.dstOffset = _jsonDocument["dstOffset"].as<int>();

    return _generalConfiguration;
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

    File _file;

    std::vector<String> _files = {
        METADATA_JSON_PATH,
        GENERAL_CONFIGURATION_JSON_PATH,
        CONFIGURATION_ADE7953_JSON_PATH,
        CALIBRATION_JSON_PATH,
        CHANNEL_DATA_JSON_PATH,
        LOGGER_JSON_PATH,
        ENERGY_JSON_PATH,
        DAILY_ENERGY_JSON_PATH,
        LOG_PATH
    };

    for (String _fileName : _files) {
        _file = SPIFFS.open(_fileName, "r");
        if (!_file) {
            logger.error("Failed to open file %s", "utils::factoryReset", _fileName.c_str());
            return;
        }

        SPIFFS.rename(_fileName, ("/old" + String(_fileName)).c_str());
        if (!_duplicateFile((String(FACTORY_PATH) + String(_fileName)).c_str(), _fileName.c_str())) {

            logger.error(
                "Failed to duplicate file %s", 
                "utils::factoryReset", 
                _fileName.c_str()
            );
            return;
        }
    }

    logger.fatal("Factory reset completed. We are back to the good old days. Now rebooting...", "utils::factoryReset");
    restartEsp32("utils::factoryReset", "Factory reset");
}

bool _duplicateFile(const char* sourcePath, const char* destinationPath) {
    logger.debug("Duplicating file", "utils::_duplicateFile");

    File _sourceFile = SPIFFS.open(sourcePath, "r");
    if (!_sourceFile) {
        logger.error("Failed to open source file: %s", "utils::_duplicateFile", sourcePath);
        return false;
    }

    File _destinationFile = SPIFFS.open(destinationPath, "w");
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