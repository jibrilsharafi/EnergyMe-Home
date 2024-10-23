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
        logger.debug("%s JSON is null", "utils::deserializeJsonFromSpiffs", path);
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
    createDefaultFirmwareRollbackFile();
    createDefaultCrashCounterFile();

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

void createDefaultFirmwareRollbackFile() {
    logger.debug("Creating default %s...", "utils::createDefaultFirmwareRollbackFile", FW_ROLLBACK_TXT);

    File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", "utils::createDefaultFirmwareRollbackFile", FW_ROLLBACK_TXT);
        return;
    }

    _file.print(STABLE_FIRMWARE);
    _file.close();

    logger.debug("Default %s created", "utils::createDefaultFirmwareRollbackFile", FW_ROLLBACK_TXT);
}

// CRASH_COUNTER_TXT
void createDefaultCrashCounterFile() {
    logger.debug("Creating default %s...", "utils::createDefaultCrashCounterFile", CRASH_COUNTER_TXT);

    File _file = SPIFFS.open(CRASH_COUNTER_TXT, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", "utils::createDefaultCrashCounterFile", CRASH_COUNTER_TXT);
        return;
    }

    _file.print(0);
    _file.close();

    logger.debug("Default %s created", "utils::createDefaultCrashCounterFile", CRASH_COUNTER_TXT);
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
    if (!SPIFFS.exists(CUSTOM_MQTT_CONFIGURATION_JSON_PATH)) return true;
    if (!SPIFFS.exists(ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(DAILY_ENERGY_JSON_PATH)) return true;
    if (!SPIFFS.exists(FW_UPDATE_INFO_JSON_PATH)) return true;
    if (!SPIFFS.exists(FW_UPDATE_STATUS_JSON_PATH)) return true;
    if (!SPIFFS.exists(FW_ROLLBACK_TXT)) return true;
    if (!SPIFFS.exists(CRASH_COUNTER_TXT)) return true;

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

    // If a firmware evaluation is in progress, set the firmware to test again
    File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_READ);
    String _firmwareStatus;
    if (_file) {
        _firmwareStatus = _file.readString();
        logger.debug("Firmware status: %s", "utils::restartEsp32", _firmwareStatus.c_str());
        _file.close();
    } else {
        logger.error("Failed to open firmware rollback file", "utils::restartEsp32");
    }

    if (_firmwareStatus == NEW_FIRMWARE_TESTING) {
        logger.warning("Firmware evaluation is in progress. Setting firmware to test again", "utils::restartEsp32");

        _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(NEW_FIRMWARE_TO_BE_TESTED);
            _file.close();
        } else {
            logger.error("Failed to open firmware rollback file for writing", "utils::restartEsp32");
        }
    }

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

bool setGeneralConfigurationFromSpiffs() {
    logger.debug("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (_jsonDocument.isNull()){
        logger.error("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs");
        return false;
    } else {
        setGeneralConfiguration(_jsonDocument);

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

bool setGeneralConfiguration(JsonDocument& jsonDocument) {
    logger.debug("Setting general configuration...", "utils::setGeneralConfiguration");

    if (!validateGeneralConfigurationJson(jsonDocument)) {
        logger.error("Failed to set general configuration", "utils::setGeneralConfiguration");
        return false;
    }

    generalConfiguration.isCloudServicesEnabled = jsonDocument["isCloudServicesEnabled"].as<bool>();
    generalConfiguration.gmtOffset = jsonDocument["gmtOffset"].as<int>();
    generalConfiguration.dstOffset = jsonDocument["dstOffset"].as<int>();
    generalConfiguration.ledBrightness = jsonDocument["ledBrightness"].as<int>();

    applyGeneralConfiguration();

    saveGeneralConfigurationToSpiffs();

    publishMqtt.generalConfiguration = true;

    logger.debug("General configuration set", "utils::setGeneralConfiguration");

    return true;
}

void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument) {
    logger.debug("Converting general configuration to JSON...", "utils::generalConfigurationToJson");

    jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
    jsonDocument["dstOffset"] = generalConfiguration.dstOffset;
    jsonDocument["ledBrightness"] = generalConfiguration.ledBrightness;

    logger.debug("General configuration converted to JSON", "utils::generalConfigurationToJson");
}

void applyGeneralConfiguration() {
    logger.debug("Applying general configuration...", "utils::applyGeneralConfiguration");

    led.setBrightness(generalConfiguration.ledBrightness);

    logger.debug("General configuration applied", "utils::applyGeneralConfiguration");
}

bool validateGeneralConfigurationJson(JsonDocument& jsonDocument) {
    if (!jsonDocument.is<JsonObject>()) return false;

    if (!jsonDocument.containsKey("isCloudServicesEnabled") || !jsonDocument["isCloudServicesEnabled"].is<bool>()) return false;
    if (!jsonDocument.containsKey("gmtOffset") || !jsonDocument["gmtOffset"].is<int>()) return false;
    if (!jsonDocument.containsKey("dstOffset") || !jsonDocument["dstOffset"].is<int>()) return false;
    if (!jsonDocument.containsKey("ledBrightness") || !jsonDocument["ledBrightness"].is<int>()) return false;

    return true;
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

    if (_latestMajor < _currentMajor) return true;
    else if (_latestMinor < _currentMinor) return true;
    else if (_latestPatch < _currentPatch) return true;
    else return false;
}

String getDeviceId() {
    String _macAddress = WiFi.macAddress();
    _macAddress.replace(":", "");
    return _macAddress;
}



const char* getMqttStateReason(int state)
{

    // Full description of the MQTT state codes
    // -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
    // -3 : MQTT_CONNECTION_LOST - the network connection was broken
    // -2 : MQTT_CONNECT_FAILED - the network connection failed
    // -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
    // 0 : MQTT_CONNECTED - the client is connected
    // 1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
    // 2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
    // 3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
    // 4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
    // 5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect

    switch (state)
    {
    case -4:
        return "MQTT_CONNECTION_TIMEOUT";
    case -3:
        return "MQTT_CONNECTION_LOST";
    case -2:
        return "MQTT_CONNECT_FAILED";
    case -1:
        return "MQTT_DISCONNECTED";
    case 0:
        return "MQTT_CONNECTED";
    case 1:
        return "MQTT_CONNECT_BAD_PROTOCOL";
    case 2:
        return "MQTT_CONNECT_BAD_CLIENT_ID";
    case 3:
        return "MQTT_CONNECT_UNAVAILABLE";
    case 4:
        return "MQTT_CONNECT_BAD_CREDENTIALS";
    case 5:
        return "MQTT_CONNECT_UNAUTHORIZED";
    default:
        return "Unknown MQTT state";
    }
}

void incrementCrashCounter() {
    logger.debug("Incrementing crash counter...", "utils::incrementCrashCounter");

    File file = SPIFFS.open(CRASH_COUNTER_TXT, FILE_READ);
    int _crashCounter = 0;
    if (file) {
        _crashCounter = file.parseInt();
        file.close();
    }

    _crashCounter++;

    file = SPIFFS.open(CRASH_COUNTER_TXT, FILE_WRITE);
    if (file) {
        file.print(_crashCounter);
        file.close();
    }

    logger.debug("Crash counter incremented to %d", "utils::incrementCrashCounter", _crashCounter);
}

void handleCrashCounter() { // TODO: Move this to RTC
    logger.debug("Handling crash counter...", "utils::handleCrashCounter");

    File file = SPIFFS.open(CRASH_COUNTER_TXT, FILE_READ);
    int crashCounter = 0;
    if (file) {
        crashCounter = file.parseInt();
        file.close();
    }
    logger.debug("Crash counter: %d", "utils::handleCrashCounter", crashCounter);

    if (crashCounter >= MAX_CRASH_COUNT) {
        logger.fatal("Crash counter reached the maximum allowed crashes. Rolling back to stable firmware...", "utils::handleCrashCounter");
        
        if (!Update.rollBack()) {
            logger.error("No firmware to rollback available. Keeping current firmware", "utils::handleCrashCounter");
        }

        SPIFFS.format(); // Factory reset

        ESP.restart(); // Only place where ESP.restart is directly called as we need to avoid again to crash
    } else {
        incrementCrashCounter();
    }
}

void crashCounterLoop() {
    if (isCrashCounterReset) return; // Counter already reset

    if (millis() > CRASH_COUNTER_TIMEOUT) {
        isCrashCounterReset = true;
        logger.debug("Timeout reached. Resetting crash counter...", "utils::crashCounterLoop");

        File file = SPIFFS.open(CRASH_COUNTER_TXT, FILE_WRITE);
        if (file) {
            file.print(0); // Reset crash counter
            file.close();
        }
    }
}

void handleFirmwareTesting() { // TODO: Move this to RTC
    logger.debug("Checking if rollback is needed...", "utils::handleFirmwareTesting");

    String _rollbackStatus;
    File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_READ);
    if (!_file) {
        logger.error("Failed to open firmware rollback file", "utils::handleFirmwareTesting");
        return;
    } else {
        _rollbackStatus = _file.readString();
        _file.close();
    }

    logger.debug("Rollback status: %s", "utils::handleFirmwareTesting", _rollbackStatus);
    if (_rollbackStatus == NEW_FIRMWARE_TO_BE_TESTED) { // First restart after new firmware is installed
        logger.info("Testing new firmware", "utils::handleFirmwareTesting");

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(NEW_FIRMWARE_TESTING); // Set the flag to test the new firmware
            _file.close();
        }
        isFirmwareUpdate = true;
        return;
    } else if (_rollbackStatus == NEW_FIRMWARE_TESTING) { // If the flag did not get set to stable firmware, then the new firmware is not working
        logger.fatal("Testing new firmware failed. Rolling back to stable firmware", "utils::handleFirmwareTesting");
        
        if (!Update.rollBack()) {
            logger.error("No firmware to rollback available. Keeping current firmware", "utils::handleFirmwareTesting");
        }

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(STABLE_FIRMWARE);
            _file.close();
        }

        setRestartEsp32("utils::handleFirmwareTesting", "Testing new firmware failed. Rolling back to stable firmware");
    } else {
        logger.debug("No rollback needed", "utils::handleFirmwareTesting");
    }
}

void firmwareTestingLoop() {
    if (!isFirmwareUpdate) return;

    logger.verbose("Checking if firmware has passed the testing period...", "utils::firmwareTestingLoop");

    if (millis() > ROLLBACK_TESTING_TIMEOUT) {
        logger.info("Testing period of new firmware has passed. Keeping current firmware", "utils::firmwareTestingLoop");
        isFirmwareUpdate = false;

        File _file = SPIFFS.open(FW_ROLLBACK_TXT, FILE_WRITE);
        if (_file) {
            _file.print(STABLE_FIRMWARE);
            _file.close();
        }
    }
}

String decryptData(String encryptedData, String key) {
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 3);
    if (encryptedData.length() == 0) {
        logger.error("Empty encrypted data", "utils::decryptData");
        return String("");
    }
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 4);
    if (key.length() == 0) {
        logger.error("Empty key", "utils::decryptData");
        return String("");
    }
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 5);

    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 6);
    if (key.length() != 32) {
        logger.error("Invalid key length: %d. Expected 32 bytes", "utils::decryptData", key.length());
        return String("");
    }

    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 7);
    unsigned char _decodedData[CERTIFICATE_LENGTH];
    size_t _decodedLength;
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 8);
    int _ret = mbedtls_base64_decode(_decodedData, CERTIFICATE_LENGTH, &_decodedLength, (const unsigned char*)encryptedData.c_str(), encryptedData.length());
    if (_ret != 0) {
        logger.error("Second base64 decoding failed: %d", "utils::decryptData", _ret);
        return String("");
    }
    logger.info("Decoded data: %s", "utils::decryptData", _decodedData);
    
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 9);
    mbedtls_aes_context aes;
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 10);
    mbedtls_aes_init(&aes);
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 11);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key.c_str(), 256);
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 12);
    unsigned char decryptedData[CERTIFICATE_LENGTH];
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 13);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, _decodedData, decryptedData);

    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 14);
    return String(reinterpret_cast<const char*>(decryptedData));
}

String readEncryptedFile(const char* path) {
    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 0);
    File file = SPIFFS.open(path, FILE_READ);
    if (!file) {
        logger.error("Failed to open file for reading", "utils::readEncryptedFile");
        return String("");
    }

    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 1);
    String _encryptedData = file.readString();
    file.close();

    crashMonitor.leaveBreadcrumb(CustomModule::UTILS, 2);
    // return decryptData(_encryptedData, String(preshared_encryption_key) + getDeviceId()); //FIXME: Uncomment this line and fix the panic in the decryptData function
    return _encryptedData;
}