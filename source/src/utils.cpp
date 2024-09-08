#include "utils.h"

extern AdvancedLogger logger;
extern CustomTime customTime;
extern Led led;
extern Ade7953 ade7953;
extern GeneralConfiguration generalConfiguration;

void getJsonProjectInfo(JsonDocument& jsonDocument) { 
    jsonDocument["companyName"] = COMPANY_NAME;
    jsonDocument["fullProductName"] = FULL_PRODUCT_NAME;
    jsonDocument["productName"] = PRODUCT_NAME;
    jsonDocument["productDescription"] = PRODUCT_DESCRIPTION;
    jsonDocument["productUrl"] = PRODUCT_URL;
    jsonDocument["githubUrl"] = GITHUB_URL;
    jsonDocument["author"] = AUTHOR;
    jsonDocument["authorEmail"] = AUTHOR_EMAIL;
}

void getJsonDeviceInfo(JsonDocument& jsonDocument)
{
    jsonDocument["system"]["uptime"] = millis();
    jsonDocument["system"]["systemTime"] = customTime.getTimestamp();

    jsonDocument["firmware"]["buildVersion"] = FIRMWARE_BUILD_VERSION;
    jsonDocument["firmware"]["buildDate"] = FIRMWARE_BUILD_DATE;

    jsonDocument["memory"]["heap"]["free"] = ESP.getFreeHeap();
    jsonDocument["memory"]["heap"]["total"] = ESP.getHeapSize();
    jsonDocument["memory"]["spiffs"]["free"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    jsonDocument["memory"]["spiffs"]["total"] = SPIFFS.totalBytes();

    jsonDocument["chip"]["model"] = ESP.getChipModel();
    jsonDocument["chip"]["revision"] = ESP.getChipRevision();
    jsonDocument["chip"]["cpuFrequency"] = ESP.getCpuFreqMHz();
    jsonDocument["chip"]["sdkVersion"] = ESP.getSdkVersion();
    jsonDocument["chip"]["id"] = ESP.getEfuseMac();
}

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument) {
    logger.debug("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs");

    File _file = SPIFFS.open(path, FILE_READ);
    if (!_file){
        logger.error("%s Failed to open file", "utils::deserializeJsonFromSpiffs", path);
        return;
    }

    DeserializationError _error = deserializeJson(jsonDocument, _file);
    _file.close();
    if (_error){
        logger.error("Failed to deserialize file %s. Error: %s", "utils::deserializeJsonFromSpiffs", path, _error.c_str());
        return;
    }

    if (jsonDocument.isNull() || jsonDocument.size() == 0){
        logger.warning("%s JSON is null", "utils::deserializeJsonFromSpiffs", path);
    }
    
    String _jsonString;
    serializeJson(jsonDocument, _jsonString);

    logger.debug("%s JSON deserialized from SPIFFS correctly", "utils::deserializeJsonFromSpiffs", _jsonString.c_str());
}

bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument){
    logger.debug("Serializing JSON to SPIFFS...", "utils::serializeJsonToSpiffs");

    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file){
        logger.error("%s Failed to open file", "utils::serializeJsonToSpiffs", path);
        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    if (jsonDocument.isNull()){
        logger.warning("%s JSON is null", "utils::serializeJsonToSpiffs", path);
    }

    String _jsonString;
    serializeJson(jsonDocument, _jsonString);
    logger.debug("%s JSON serialized to SPIFFS correctly", "utils::serializeJsonToSpiffs", _jsonString.c_str());

    return true;
}

void createEmptyJsonFile(const char* path) {
    logger.debug("Creating empty JSON file %s...", "utils::createEmptyJsonFile", path);

    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", "utils::createEmptyJsonFile", path);
        return;
    }

    _file.print("{}");
    _file.close();

    logger.debug("Empty JSON file %s created", "utils::createEmptyJsonFile", path);
}

void formatAndCreateDefaultFiles() {
    logger.debug("Creating default files...", "utils::formatAndCreateDefaultFiles");

    SPIFFS.format();

    createDefaultGeneralConfigurationFile();
    createDefaultEnergyFile();
    createDefaultDailyEnergyFile();
    createDefaultFirmwareUpdateInfoFile();
    createDefaultFirmwareUpdateStatusFile();

    createFirstSetupFile();

    logger.debug("Default files created", "utils::formatAndCreateDefaultFiles");
}

void createDefaultGeneralConfigurationFile() {
    logger.debug("Creating default general %s...", "utils::createDefaultGeneralConfigurationFile", GENERAL_CONFIGURATION_JSON_PATH);

    JsonDocument _jsonDocument;

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

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["activeEnergy"] = 0;
        _jsonDocument[String(i)]["reactiveEnergy"] = 0;
        _jsonDocument[String(i)]["apparentEnergy"] = 0;
    }

    serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultEnergyFile", ENERGY_JSON_PATH);
}

void createDefaultDailyEnergyFile() {
    logger.debug("Creating default %s...", "utils::createDefaultDailyEnergyFile", DAILY_ENERGY_JSON_PATH);

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);

    logger.debug("Default %s created", "utils::createDefaultDailyEnergyFile", DAILY_ENERGY_JSON_PATH);
}

void createDefaultFirmwareUpdateInfoFile() {
    logger.debug("Creating default %s...", "utils::createDefaultFirmwareUpdateInfoFile", FW_UPDATE_INFO_JSON_PATH);

    createEmptyJsonFile(FW_UPDATE_INFO_JSON_PATH);

    logger.debug("Default %s created", "utils::createDefaultFirmwareUpdateInfoFile", FW_UPDATE_INFO_JSON_PATH);
}

void createDefaultFirmwareUpdateStatusFile() {
    logger.debug("Creating default %s...", "utils::createDefaultFirmwareUpdateStatusFile", FW_UPDATE_STATUS_JSON_PATH);

    createEmptyJsonFile(FW_UPDATE_STATUS_JSON_PATH);

    logger.debug("Default %s created", "utils::createDefaultFirmwareUpdateStatusFile", FW_UPDATE_STATUS_JSON_PATH);
}

void createFirstSetupFile() {
    logger.debug("Creating %s...", "utils::createFirstSetupFile", FIRST_SETUP_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["timestamp"] = customTime.getTimestamp();
    _jsonDocument["buildVersion"] = FIRMWARE_BUILD_VERSION;
    _jsonDocument["buildDate"] = FIRMWARE_BUILD_DATE;

    serializeJsonToSpiffs(FIRST_SETUP_JSON_PATH, _jsonDocument);

    logger.debug("%s created", "utils::createFirstSetupFile", FIRST_SETUP_JSON_PATH);
}

bool checkIfFirstSetup() {
    logger.debug("Checking if first setup...", "utils::checkIfFirstSetup");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(FIRST_SETUP_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        logger.debug("First setup file is empty", "utils::checkIfFirstSetup");
        return true;
    }

    return false;
}

bool checkAllFiles() {
    logger.debug("Checking all files...", "utils::checkAllFiles");

    if (!SPIFFS.exists(FIRST_SETUP_JSON_PATH)) return true;
    if (!SPIFFS.exists(GENERAL_CONFIGURATION_JSON_PATH)) return true;
    if (!SPIFFS.exists(CONFIGURATION_ADE7953_JSON_PATH)) return true;
    if (!SPIFFS.exists(CALIBRATION_JSON_PATH)) return true;
    if (!SPIFFS.exists(CHANNEL_DATA_JSON_PATH)) return true;
    if (!SPIFFS.exists(ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(DAILY_ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(FW_UPDATE_INFO_JSON_PATH)) return true;
    if (!SPIFFS.exists(FW_UPDATE_STATUS_JSON_PATH)) return true;

    return false;
}

void setRestartEsp32(const char* functionName, const char* reason) { 
    logger.warning("Restart required from function %s. Reason: %s", "utils::setRestartEsp32", functionName, reason);
    
    restartConfiguration.isRequired = true;
    restartConfiguration.requiredAt = millis();
    restartConfiguration.functionName = String(functionName);
    restartConfiguration.reason = String(reason);
}

void checkIfRestartEsp32Required() {
    if (restartConfiguration.isRequired) {
        if ((millis() - restartConfiguration.requiredAt) > ESP32_RESTART_DELAY) {
            restartEsp32();
        }
    }
}

void restartEsp32() {
    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setRed(true);

    if (restartConfiguration.functionName != "utils::factoryReset") {
        ade7953.saveEnergy();
    }
    logger.fatal("Restarting ESP32 from function %s. Reason: %s", "utils::restartEsp32", restartConfiguration.functionName.c_str(), restartConfiguration.reason.c_str());

    ESP.restart();
}

// Print functions
// -----------------------------

void printMeterValues(MeterValues meterValues, const char* channelLabel) {
    logger.verbose(
        "%s: %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh | %.3f VARh | %.3f VAh", 
        "utils::printMeterValues", 
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
    logger.info(
        "Free heap: %d bytes | Total heap: %d bytes || Free SPIFFS: %d bytes | Total SPIFFS: %d bytes",
        "utils::printDeviceStatus",
        ESP.getFreeHeap(),
        ESP.getHeapSize(),
        SPIFFS.totalBytes() - SPIFFS.usedBytes(),
        SPIFFS.totalBytes()
    );
}

// General configuration
// -----------------------------

void setDefaultGeneralConfiguration() {
    logger.debug("Setting default general configuration...", "utils::setDefaultGeneralConfiguration");
    
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    generalConfiguration.gmtOffset = DEFAULT_GMT_OFFSET;
    generalConfiguration.dstOffset = DEFAULT_DST_OFFSET;
    generalConfiguration.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    
    logger.debug("Default general configuration set", "utils::setDefaultGeneralConfiguration");
}

void setGeneralConfiguration(GeneralConfiguration& newGeneralConfiguration) {
    logger.debug("Setting general configuration...", "utils::setGeneralConfiguration");

    GeneralConfiguration previousConfiguration = generalConfiguration;
    generalConfiguration = newGeneralConfiguration;

    applyGeneralConfiguration();
    saveGeneralConfigurationToSpiffs();

    publishMqtt.generalConfiguration = true;
    
    logger.debug("General configuration set", "utils::setGeneralConfiguration");
}

bool setGeneralConfigurationFromSpiffs() {
    logger.debug("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()){
        logger.error("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs");
        return false;
    } else {
        GeneralConfiguration _generalConfiguration;
        jsonToGeneralConfiguration(_jsonDocument, _generalConfiguration);
        setGeneralConfiguration(_generalConfiguration);

        logger.debug("General configuration set from SPIFFS", "utils::setGeneralConfigurationFromSpiffs");
        return true;
    }
}

bool saveGeneralConfigurationToSpiffs() {
    logger.debug("Saving general configuration to SPIFFS...", "utils::saveGeneralConfigurationToSpiffs");

    JsonDocument _jsonDocument;
    generalConfigurationToJson(generalConfiguration, _jsonDocument);

    if (serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument)){
        logger.debug("General configuration saved to SPIFFS", "utils::saveGeneralConfigurationToSpiffs");
        return true;
    } else {
        logger.error("Failed to save general configuration to SPIFFS", "utils::saveGeneralConfigurationToSpiffs");
        return false;
    }
}

void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument) {
    logger.debug("Converting general configuration to JSON...", "utils::generalConfigurationToJson");

    jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
    jsonDocument["dstOffset"] = generalConfiguration.dstOffset;
    jsonDocument["ledBrightness"] = generalConfiguration.ledBrightness;

    logger.debug("General configuration converted to JSON", "utils::generalConfigurationToJson");
}

void jsonToGeneralConfiguration(JsonDocument& jsonDocument, GeneralConfiguration& generalConfiguration) {
    logger.debug("Converting JSON to general configuration...", "utils::jsonToGeneralConfiguration");

    generalConfiguration.isCloudServicesEnabled = jsonDocument["isCloudServicesEnabled"].as<bool>();
    generalConfiguration.gmtOffset = jsonDocument["gmtOffset"].as<int>();
    generalConfiguration.dstOffset = jsonDocument["dstOffset"].as<int>();
    generalConfiguration.ledBrightness = jsonDocument["ledBrightness"].as<int>();

    logger.debug("JSON converted to general configuration", "utils::jsonToGeneralConfiguration");
}

void applyGeneralConfiguration() {
    logger.debug("Applying general configuration...", "utils::applyGeneralConfiguration");

    led.setBrightness(generalConfiguration.ledBrightness);

    logger.debug("General configuration applied", "utils::applyGeneralConfiguration");
}

// Helper functions
// -----------------------------

void getPublicLocation(PublicLocation* publicLocation) {
    HTTPClient _http;
    JsonDocument _jsonDocument;

    _http.begin(PUBLIC_LOCATION_ENDPOINT);

    int httpCode = _http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = _http.getString();
            payload.trim();
            
            deserializeJson(_jsonDocument, payload);

            publicLocation->country = _jsonDocument["country"].as<String>();
            publicLocation->city = _jsonDocument["city"].as<String>();
            publicLocation->latitude = _jsonDocument["lat"].as<String>();
            publicLocation->longitude = _jsonDocument["lon"].as<String>();

            logger.debug(
                "Location: %s, %s | Lat: %.4f | Lon: %.4f",
                "utils::getPublicLocation",
                publicLocation->country.c_str(),
                publicLocation->city.c_str(),
                publicLocation->latitude.toFloat(),
                publicLocation->longitude.toFloat()
            );
        }
    } else {
        logger.error("Error on HTTP request: %d", "utils::getPublicLocation", httpCode);
    }

    _http.end();
}

void getPublicTimezone(int* gmtOffset, int* dstOffset) {
    PublicLocation _publicLocation;
    getPublicLocation(&_publicLocation);

    HTTPClient _http;
    String _url = PUBLIC_TIMEZONE_ENDPOINT;
    _url += "lat=" + _publicLocation.latitude;
    _url += "&lng=" + _publicLocation.longitude;
    _url += "&username=" + String(PUBLIC_TIMEZONE_USERNAME);

    _http.begin(_url);
    int httpCode = _http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = _http.getString();
            payload.trim();
            
            JsonDocument _jsonDocument;

            deserializeJson(_jsonDocument, payload);

            *gmtOffset = _jsonDocument["rawOffset"].as<int>() * 3600; // Convert hours to seconds
            *dstOffset = _jsonDocument["dstOffset"].as<int>() * 3600 - *gmtOffset; // Convert hours to seconds. Remove GMT offset as it is already included in the dst offset

            logger.debug(
                "GMT offset: %d | DST offset: %d",
                "utils::getPublicTimezone",
                _jsonDocument["rawOffset"].as<int>(),
                _jsonDocument["dstOffset"].as<int>()
            );
        }
    } else {
        logger.error(
            "Error on HTTP request: %d", 
            "utils::getPublicTimezone", 
            httpCode
        );
    }
}

void updateTimezone() {
    logger.debug("Updating timezone...", "utils::updateTimezone");

    getPublicTimezone(&generalConfiguration.gmtOffset, &generalConfiguration.dstOffset);
    saveGeneralConfigurationToSpiffs();

    logger.debug("Timezone updated", "utils::updateTimezone");
}

void factoryReset() { 
    logger.fatal("Factory reset requested", "utils::factoryReset");

    formatAndCreateDefaultFiles();

    setRestartEsp32("utils::factoryReset", "Factory reset completed. We are back to the good old days");
}

bool isLatestFirmwareInstalled() {
    File _file = SPIFFS.open(FW_UPDATE_INFO_JSON_PATH, FILE_READ);
    if (!_file) {
        logger.error("Failed to open firmware update info file", "utils::isLatestFirmwareInstalled");
        return false;
    }

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, _file);

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