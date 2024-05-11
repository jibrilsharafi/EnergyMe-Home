#include "utils.h"

extern Logger logger;
extern CustomTime customTime;
extern Led led;
extern Ade7953 ade7953;

extern GeneralConfiguration generalConfiguration;

// Get the status of the device (firmware, memory, etc.)
JsonDocument getDeviceStatus()
{

    JsonDocument _jsonDocument;

    // system -> uptimeMs, uptimeDays
    JsonObject _jsonSystem = _jsonDocument["system"].to<JsonObject>();
    _jsonSystem["uptime"] = millis();

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
    logger.log("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    File _file = SPIFFS.open(path, "r");
    if (!_file){
        logger.log(
            (
                "Failed to open file " + 
                String(path)
            ).c_str(),
            "utils::deserializeJsonFromSpiffs",
            CUSTOM_LOG_LEVEL_ERROR
        );

        return JsonDocument();
    }
    JsonDocument _jsonDocument;

    DeserializationError _error = deserializeJson(_jsonDocument, _file);
    _file.close();
    if (_error){
        logger.log(
            (
                "Failed to deserialize file " + 
                String(path) + 
                ". Error: " + 
                String(_error.c_str())
            ).c_str(),
            "utils::deserializeJsonFromSpiffs",
            CUSTOM_LOG_LEVEL_ERROR
        );
        
        return JsonDocument();
    }

    logger.log("JSON deserialized from SPIFFS correctly", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(_jsonDocument, Serial);
    Serial.println();
    return _jsonDocument;
}

bool serializeJsonToSpiffs(const char* path, JsonDocument jsonDocument){
    logger.log("Serializing JSON to SPIFFS", "utils::serializeJsonToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    File _file = SPIFFS.open(path, "w");
    if (!_file){
        logger.log(
            (
                "Failed to open file " + 
                String(path)
            ).c_str(),
            "utils::serializeJsonToSpiffs",
            CUSTOM_LOG_LEVEL_ERROR
        );

        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    logger.log("JSON serialized to SPIFFS correctly", "main::serializeJsonToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(jsonDocument, Serial);
    Serial.println();
    return true;
}

void restartEsp32(const char* functionName, const char* reason) {

    if (functionName != "utils::factoryReset") {
        ade7953.saveEnergyToSpiffs();
    }

    logger.log(
        (
            "Restarting ESP32 from function " + 
            String(functionName) + 
            ". Reason: " + 
            String(reason)
        ).c_str(),
        "main::restartEsp32",
        CUSTOM_LOG_LEVEL_FATAL
    );
  
    led.setBrightness(LED_MAX_BRIGHTNESS);
    led.block();
    for (int i = 0; i < 3; i++){
        led.setYellow(true);
        delay(200);
        led.setCyan(true);
        delay(200);
    }
    led.unblock();

    ESP.restart();
}

void printMeterValues(MeterValues meterValues, const char* channelLabel) {
    logger.log(
        (
            String(channelLabel) + ": " + 
            String(meterValues.voltage, 1) + " V | " + 
            String(meterValues.current, 3) + " A || " + 
            String(meterValues.activePower, 1) + " W | " + 
            String(meterValues.reactivePower, 1) + " VAR | " + 
            String(meterValues.apparentPower, 1) + " VA | " + 
            String(meterValues.powerFactor, 3) + " PF || " + 
            String(meterValues.activeEnergy, 3) + " Wh | " + 
            String(meterValues.reactiveEnergy, 3) + " VARh | " + 
            String(meterValues.apparentEnergy, 3) + " VAh"
        ).c_str(),
        "main::printMeterValues",
        CUSTOM_LOG_LEVEL_VERBOSE);
}

void printDeviceStatus()
{

    JsonDocument _jsonDocument = getDeviceStatus();

    logger.log(
        (
            "Free heap: " + String(_jsonDocument["memory"]["heap"]["free"].as<int>()) + " bytes | " +
            "Total heap: " + String(_jsonDocument["memory"]["heap"]["total"].as<int>()) + " bytes || " +
            "Free SPIFFS: " + String(_jsonDocument["memory"]["spiffs"]["free"].as<int>()) + " bytes | " +
            "Total SPIFFS: " + String(_jsonDocument["memory"]["spiffs"]["total"].as<int>()) + " bytes")
            .c_str(),
        "main::printDeviceStatus",
        CUSTOM_LOG_LEVEL_DEBUG);
}

bool checkIfFirstSetup() {
    logger.log("Checking if first setup...", "main::checkIfFirstSetup", CUSTOM_LOG_LEVEL_DEBUG);
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open metadata.json", "main::checkIfFirstSetup", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }

    return _jsonDocument["setup"]["isFirstTime"].as<bool>();
}

void logFirstSetupComplete() {
    logger.log("Logging first setup complete...", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_DEBUG);
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open metadata.json", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_ERROR);
        return;
    }

    _jsonDocument["setup"]["isFirstTime"] = false;
    _jsonDocument["setup"]["timestampFirstTime"] = customTime.getTimestamp();
    serializeJsonToSpiffs(METADATA_JSON_PATH, _jsonDocument);
    logger.log("First setup complete", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_DEBUG);
}

// General configuration
// -----------------------------

void setDefaultGeneralConfiguration() {
    logger.log("Setting default general configuration...", "utils::setDefaultGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
    
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    generalConfiguration.gmtOffset = DEFAULT_GMT_OFFSET;
    generalConfiguration.dstOffset = DEFAULT_DST_OFFSET;
    
    logger.log("Default general configuration set", "utils::setDefaultGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

void setGeneralConfiguration(GeneralConfiguration newGeneralConfiguration) {
    logger.log("Setting general configuration...", "utils::setGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);

    generalConfiguration = newGeneralConfiguration;

    logger.log("General configuration set", "utils::setGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

bool setGeneralConfigurationFromSpiffs() {
    logger.log("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    } else {
        setGeneralConfiguration(jsonToGeneralConfiguration(_jsonDocument));
        logger.log("General configuration set from SPIFFS", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    }
}

bool saveGeneralConfigurationToSpiffs() {
    logger.log("Saving general configuration to SPIFFS...", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = generalConfigurationToJson(generalConfiguration);
    if (serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument)){
        logger.log("General configuration saved to SPIFFS", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    } else {
        logger.log("Failed to save general configuration to SPIFFS", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
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

GeneralConfiguration jsonToGeneralConfiguration(JsonDocument jsonDocument) {
    GeneralConfiguration _generalConfiguration;
    
    _generalConfiguration.isCloudServicesEnabled = jsonDocument["isCloudServicesEnabled"].as<bool>();
    _generalConfiguration.gmtOffset = jsonDocument["gmtOffset"].as<int>();
    _generalConfiguration.dstOffset = jsonDocument["dstOffset"].as<int>();

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

            logger.log(
                (
                    "Location: " + 
                    String(_jsonDocument["city"].as<String>()) + 
                    ", " + 
                    String(_jsonDocument["country"].as<String>()) + 
                    " | Lat: " + 
                    String(_jsonDocument["lat"].as<float>(), 4) + 
                    " | Lon: " + 
                    String(_jsonDocument["lon"].as<float>(), 4)
                ).c_str(),
                "utils::getPublicLocation",
                CUSTOM_LOG_LEVEL_DEBUG
            );
        }
    } else {
        logger.log(
            ("Error on HTTP request: " + String(httpCode)).c_str(), 
            "utils::getPublicLocation", 
            CUSTOM_LOG_LEVEL_ERROR
        );
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

            logger.log(
                (
                    "GMT offset: " + 
                    String(_jsonDocument["rawOffset"].as<int>()) + 
                    " | DST offset: " + 
                    String(_jsonDocument["dstOffset"].as<int>())
                ).c_str(),
                "utils::getPublicTimezone",
                CUSTOM_LOG_LEVEL_DEBUG
            );

            _gmtOffset = _jsonDocument["rawOffset"].as<int>() * 3600; // Convert hours to seconds
            _dstOffset = _jsonDocument["dstOffset"].as<int>() * 3600 - _gmtOffset; // Convert hours to seconds. Remove GMT offset as it is already included in the dst offset
        }
    } else {
        logger.log(
            ("Error on HTTP request: " + String(httpCode)).c_str(), 
            "utils::getPublicTimezone", 
            CUSTOM_LOG_LEVEL_ERROR
        );
        deserializeJson(_jsonDocument, "{}");

        _gmtOffset = generalConfiguration.gmtOffset;
        _dstOffset = generalConfiguration.dstOffset;
    }

    return std::make_pair(_gmtOffset, _dstOffset);
}

void updateTimezone() {
    logger.log("Updating timezone", "utils::updateTimezone", CUSTOM_LOG_LEVEL_DEBUG);

    std::pair<int, int> _timezones = getPublicTimezone();

    generalConfiguration.gmtOffset = _timezones.first;
    generalConfiguration.dstOffset = _timezones.second;
    
    saveGeneralConfigurationToSpiffs();
}

void factoryReset() {
    logger.log("Factory reset requested", "utils::factoryReset", CUSTOM_LOG_LEVEL_FATAL);

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
        LOG_TXT_PATH
    };

    for (String _fileName : _files) {
        _file = SPIFFS.open(_fileName, "r");
        if (!_file) {
            logger.log(
                (
                    "Failed to open file " + 
                    String(_file)
                ).c_str(),
                "utils::factoryReset",
                CUSTOM_LOG_LEVEL_ERROR
            );
            return;
        }

        SPIFFS.rename(_fileName, ("/old" + String(_fileName)).c_str());
        if (!_duplicateFile((String(FACTORY_PATH) + String(_fileName)).c_str(), _fileName.c_str())) {
            logger.log(
                (
                    "Failed to duplicate file " + 
                    String(_fileName)
                ).c_str(),
                "utils::factoryReset",
                CUSTOM_LOG_LEVEL_ERROR
            );
            return;
        }
    }

    logger.log("Factory reset completed. We are back to the good old days. Now rebooting...", "utils::factoryReset", CUSTOM_LOG_LEVEL_FATAL);
    restartEsp32("utils::factoryReset", "Factory reset");
}

bool _duplicateFile(const char* sourcePath, const char* destinationPath) {
    logger.log("Duplicating file", "utils::_duplicateFile", CUSTOM_LOG_LEVEL_DEBUG);

    File _sourceFile = SPIFFS.open(sourcePath, "r");
    if (!_sourceFile) {
        logger.log(
            (
                "Failed to open source file: " + 
                String(sourcePath)
            ).c_str(),
            "utils::_duplicateFile",
            CUSTOM_LOG_LEVEL_ERROR
        );
        return false;
    }

    File _destinationFile = SPIFFS.open(destinationPath, "w");
    if (!_destinationFile) {
        logger.log(
            (
                "Failed to open destination file: " + 
                String(destinationPath)
            ).c_str(),
            "utils::_duplicateFile",
            CUSTOM_LOG_LEVEL_ERROR
        );
        return false;
    }

    while (_sourceFile.available()) {
        _destinationFile.write(_sourceFile.read());
    }

    _sourceFile.close();
    _destinationFile.close();

    logger.log("File duplicated", "utils::_duplicateFile", CUSTOM_LOG_LEVEL_DEBUG);
    return true;
}