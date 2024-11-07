#include "utils.h"

void getJsonProjectInfo(JsonDocument& jsonDocument) { 
    logger.debug("Getting project info...", "utils::getJsonProjectInfo");

    jsonDocument["companyName"] = COMPANY_NAME;
    jsonDocument["fullProductName"] = FULL_PRODUCT_NAME;
    jsonDocument["productName"] = PRODUCT_NAME;
    jsonDocument["productDescription"] = PRODUCT_DESCRIPTION;
    jsonDocument["githubUrl"] = GITHUB_URL;
    jsonDocument["author"] = AUTHOR;
    jsonDocument["authorEmail"] = AUTHOR_EMAIL;

    logger.debug("Project info retrieved", "utils::getJsonProjectInfo");
}

void getJsonDeviceInfo(JsonDocument& jsonDocument)
{
    logger.debug("Getting device info...", "utils::getJsonDeviceInfo");

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

    logger.debug("Device info retrieved", "utils::getJsonDeviceInfo");
}

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument) {
    logger.debug("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs");

    TRACE
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

    logger.debug("JSON deserialized from SPIFFS correctly: %s", "utils::deserializeJsonFromSpiffs", _jsonString.c_str());
}

bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument){
    logger.debug("Serializing JSON to SPIFFS...", "utils::serializeJsonToSpiffs");

    TRACE
    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file){
        logger.error("%s Failed to open file", "utils::serializeJsonToSpiffs", path);
        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    if (jsonDocument.isNull() || jsonDocument.size() == 0){ // It should never happen as createEmptyJsonFile should be used instead
        logger.warning("%s JSON is null", "utils::serializeJsonToSpiffs", path);
    }

    String _jsonString;
    serializeJson(jsonDocument, _jsonString);
    logger.debug("JSON serialized to SPIFFS correctly: %s", "utils::serializeJsonToSpiffs", _jsonString.c_str());

    return true;
}

void createEmptyJsonFile(const char* path) {
    logger.debug("Creating empty JSON file %s...", "utils::createEmptyJsonFile", path);

    TRACE
    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", "utils::createEmptyJsonFile", path);
        return;
    }

    _file.print("{}");
    _file.close();

    logger.debug("Empty JSON file %s created", "utils::createEmptyJsonFile", path);
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
        _jsonDocument[String(i)]["activeEnergyImported"] = 0;
        _jsonDocument[String(i)]["activeEnergyExported"] = 0;
        _jsonDocument[String(i)]["reactiveEnergyImported"] = 0;
        _jsonDocument[String(i)]["reactiveEnergyExported"] = 0;
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

void createDefaultAde7953ConfigurationFile() {
    logger.debug("Creating default %s...", "utils::createDefaultAde7953ConfigurationFile", CONFIGURATION_ADE7953_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["sampleTime"] = DEFAULT_SAMPLE_TIME;
    _jsonDocument["aVGain"] = DEFAULT_GAIN;
    _jsonDocument["aIGain"] = DEFAULT_GAIN;
    _jsonDocument["bIGain"] = DEFAULT_GAIN;
    _jsonDocument["aIRmsOs"] = DEFAULT_OFFSET;
    _jsonDocument["bIRmsOs"] = DEFAULT_OFFSET;
    _jsonDocument["aWGain"] = DEFAULT_GAIN;
    _jsonDocument["bWGain"] = DEFAULT_GAIN;
    _jsonDocument["aWattOs"] = DEFAULT_OFFSET;
    _jsonDocument["bWattOs"] = DEFAULT_OFFSET;
    _jsonDocument["aVarGain"] = DEFAULT_GAIN;
    _jsonDocument["bVarGain"] = DEFAULT_GAIN;
    _jsonDocument["aVarOs"] = DEFAULT_OFFSET;
    _jsonDocument["bVarOs"] = DEFAULT_OFFSET;
    _jsonDocument["aVaGain"] = DEFAULT_GAIN;
    _jsonDocument["bVaGain"] = DEFAULT_GAIN;
    _jsonDocument["aVaOs"] = DEFAULT_OFFSET;
    _jsonDocument["bVaOs"] = DEFAULT_OFFSET;
    _jsonDocument["phCalA"] = DEFAULT_PHCAL;
    _jsonDocument["phCalB"] = DEFAULT_PHCAL;

    serializeJsonToSpiffs(CONFIGURATION_ADE7953_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultAde7953ConfigurationFile", CONFIGURATION_ADE7953_JSON_PATH);
}

void createDefaultCalibrationFile() {
    logger.debug("Creating default %s...", "utils::createDefaultCalibrationFile", CALIBRATION_JSON_PATH);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_calibration_json);

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultCalibrationFile", CALIBRATION_JSON_PATH);
}

void createDefaultChannelDataFile() {
    logger.debug("Creating default %s...", "utils::createDefaultChannelDataFile", CHANNEL_DATA_JSON_PATH);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultChannelDataFile", CHANNEL_DATA_JSON_PATH);
}

void createDefaultCustomMqttConfigurationFile() {
    logger.debug("Creating default %s...", "utils::createDefaultCustomMqttConfigurationFile", CUSTOM_MQTT_CONFIGURATION_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["enabled"] = DEFAULT_IS_CUSTOM_MQTT_ENABLED;
    _jsonDocument["server"] = MQTT_CUSTOM_SERVER_DEFAULT;
    _jsonDocument["port"] = MQTT_CUSTOM_PORT_DEFAULT;
    _jsonDocument["clientid"] = MQTT_CUSTOM_CLIENTID_DEFAULT;
    _jsonDocument["topic"] = MQTT_CUSTOM_TOPIC_DEFAULT;
    _jsonDocument["frequency"] = MQTT_CUSTOM_FREQUENCY_DEFAULT;
    _jsonDocument["useCredentials"] = MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT;
    _jsonDocument["username"] = MQTT_CUSTOM_USERNAME_DEFAULT;
    _jsonDocument["password"] = MQTT_CUSTOM_PASSWORD_DEFAULT;

    serializeJsonToSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", "utils::createDefaultCustomMqttConfigurationFile", CUSTOM_MQTT_CONFIGURATION_JSON_PATH);
}

std::vector<const char*> checkMissingFiles() {
    logger.debug("Checking missing files...", "utils::checkMissingFiles");

    std::vector<const char*> missingFiles;
    
    const char* CONFIG_FILE_PATHS[] = {
        GENERAL_CONFIGURATION_JSON_PATH,
        CONFIGURATION_ADE7953_JSON_PATH,
        CALIBRATION_JSON_PATH,
        CHANNEL_DATA_JSON_PATH,
        CUSTOM_MQTT_CONFIGURATION_JSON_PATH,
        ENERGY_JSON_PATH,
        DAILY_ENERGY_JSON_PATH,
        FW_UPDATE_INFO_JSON_PATH,
        FW_UPDATE_STATUS_JSON_PATH
    };

    const size_t CONFIG_FILE_COUNT = sizeof(CONFIG_FILE_PATHS) / sizeof(CONFIG_FILE_PATHS[0]);

    TRACE
    for (size_t i = 0; i < CONFIG_FILE_COUNT; ++i) {
        const char* path = CONFIG_FILE_PATHS[i];
        if (!SPIFFS.exists(path)) {
            missingFiles.push_back(path);
        }
    }

    logger.debug("Missing files checked", "utils::checkMissingFiles");
    return missingFiles;
}

void createDefaultFilesForMissingFiles(const std::vector<const char*>& missingFiles) {
    logger.debug("Creating default files for missing files...", "utils::createDefaultFilesForMissingFiles");

    TRACE
    for (const char* path : missingFiles) {
        if (strcmp(path, GENERAL_CONFIGURATION_JSON_PATH) == 0) {
            createDefaultGeneralConfigurationFile();
        } else if (strcmp(path, CONFIGURATION_ADE7953_JSON_PATH) == 0) {
            createDefaultAde7953ConfigurationFile();
        } else if (strcmp(path, CALIBRATION_JSON_PATH) == 0) {
            createDefaultCalibrationFile();
        } else if (strcmp(path, CHANNEL_DATA_JSON_PATH) == 0) {
            createDefaultChannelDataFile();
        } else if (strcmp(path, CUSTOM_MQTT_CONFIGURATION_JSON_PATH) == 0) {
            createDefaultCustomMqttConfigurationFile();
        } else if (strcmp(path, ENERGY_JSON_PATH) == 0) {
            createDefaultEnergyFile();
        } else if (strcmp(path, DAILY_ENERGY_JSON_PATH) == 0) {
            createDefaultDailyEnergyFile();
        } else if (strcmp(path, FW_UPDATE_INFO_JSON_PATH) == 0) {
            createDefaultFirmwareUpdateInfoFile();
        } else if (strcmp(path, FW_UPDATE_STATUS_JSON_PATH) == 0) {
            createDefaultFirmwareUpdateStatusFile();
        } else {
            // Handle other files if needed
            logger.warning("No default creation function for path: %s", "utils::createDefaultFilesForMissingFiles", path);
        }
    }

    logger.debug("Default files created for missing files", "utils::createDefaultFilesForMissingFiles");
}

bool checkAllFiles() {
    logger.debug("Checking all files...", "utils::checkAllFiles");

    TRACE
    std::vector<const char*> missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        createDefaultFilesForMissingFiles(missingFiles);
        return true;
    }

    logger.debug("All files checked", "utils::checkAllFiles");
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
    TRACE
    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setRed(true);

    TRACE
    logger.warning("Restarting ESP32 from function %s. Reason: %s", "utils::restartEsp32", restartConfiguration.functionName.c_str(), restartConfiguration.reason.c_str());

    // If a firmware evaluation is in progress, set the firmware to test again
    TRACE
    FirmwareState _firmwareStatus = CrashMonitor::getFirmwareStatus();

    TRACE
    if (_firmwareStatus == TESTING) {
        logger.warning("Firmware evaluation is in progress. Setting firmware to test again", "utils::restartEsp32");
        TRACE
        if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) logger.error("Failed to set firmware status", "utils::restartEsp32");
    }

    TRACE
    ESP.restart();
}

// Print functions
// -----------------------------

void printMeterValues(MeterValues meterValues, const char* channelLabel) {
    logger.verbose(
        "%s: %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh | %.3f Wh | %.3f VARh | %.3f VARh | %.3f VAh", 
        "utils::printMeterValues", 
        channelLabel, 
        meterValues.voltage, 
        meterValues.current, 
        meterValues.activePower, 
        meterValues.reactivePower, 
        meterValues.apparentPower, 
        meterValues.powerFactor, 
        meterValues.activeEnergyImported,
        meterValues.activeEnergyExported,
        meterValues.reactiveEnergyImported, 
        meterValues.reactiveEnergyExported, 
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

bool setGeneralConfigurationFromSpiffs() {
    logger.debug("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs");

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (!setGeneralConfiguration(_jsonDocument)) {
        logger.error("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs");
        setDefaultGeneralConfiguration();
        return false;
    }
    
    logger.debug("General configuration set from SPIFFS", "utils::setGeneralConfigurationFromSpiffs");
    return true;
}

void setDefaultGeneralConfiguration() {
    logger.debug("Setting default general configuration...", "utils::setDefaultGeneralConfiguration");
    
    createDefaultGeneralConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    setGeneralConfiguration(_jsonDocument);
    
    logger.debug("Default general configuration set", "utils::setDefaultGeneralConfiguration");
}

void saveGeneralConfigurationToSpiffs() {
    logger.debug("Saving general configuration to SPIFFS...", "utils::saveGeneralConfigurationToSpiffs");

    JsonDocument _jsonDocument;
    generalConfigurationToJson(generalConfiguration, _jsonDocument);

    serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("General configuration saved to SPIFFS", "utils::saveGeneralConfigurationToSpiffs");
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
    logger.debug("Validating general configuration JSON...", "utils::validateGeneralConfigurationJson");

    if (!jsonDocument.is<JsonObject>()) { logger.warning("JSON is not an object", "utils::validateGeneralConfigurationJson"); return false; }
    
    if (!jsonDocument["isCloudServicesEnabled"].is<bool>()) { logger.warning("isCloudServicesEnabled is not a boolean", "utils::validateGeneralConfigurationJson"); return false; }
    if (!jsonDocument["gmtOffset"].is<int>()) { logger.warning("gmtOffset is not an integer", "utils::validateGeneralConfigurationJson"); return false; }
    if (!jsonDocument["dstOffset"].is<int>()) { logger.warning("dstOffset is not an integer", "utils::validateGeneralConfigurationJson"); return false; }
    if (!jsonDocument["ledBrightness"].is<int>()) { logger.warning("ledBrightness is not an integer", "utils::validateGeneralConfigurationJson"); return false; }

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
    if (!WiFi.isConnected()) {
        logger.warning("WiFi is not connected. Cannot update timezone", "utils::updateTimezone");
        return;
    }

    logger.debug("Updating timezone...", "utils::updateTimezone");

    getPublicTimezone(&generalConfiguration.gmtOffset, &generalConfiguration.dstOffset);
    saveGeneralConfigurationToSpiffs();

    logger.debug("Timezone updated", "utils::updateTimezone");
}

void factoryReset() { 
    logger.fatal("Factory reset requested", "utils::factoryReset");

    mainFlags.blockLoop = true;

    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setRed(true);

    clearAllPreferences();
    SPIFFS.format();

    // Directly call ESP.restart() so that a fresh start is done
    ESP.restart();

    mainFlags.blockLoop = false;
}

void clearAllPreferences() {
    logger.fatal("Clear all preferences requested", "utils::clearAllPreferences");

    Preferences preferences;
    preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false); // false = read-write mode
    preferences.clear();
    preferences.end();
    
    preferences.begin(PREFERENCES_DATA_KEY, false); // false = read-write mode
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false); // false = read-write mode
    preferences.clear();
    preferences.end();
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

String decryptData(String encryptedData, String key) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char *)key.c_str(), KEY_SIZE);

    size_t decodedLength = 0;
    size_t inputLength = encryptedData.length();
    
    unsigned char *decodedData = (unsigned char *)malloc(inputLength);
    
    int ret = mbedtls_base64_decode(decodedData, inputLength, &decodedLength, 
                                   (const unsigned char *)encryptedData.c_str(), 
                                   encryptedData.length());
    
    unsigned char *output = (unsigned char *)malloc(decodedLength + 1);
    memset(output, 0, decodedLength + 1);
    
    for(size_t i = 0; i < decodedLength; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, &decodedData[i], &output[i]);
    }
    
    uint8_t paddingLength = output[decodedLength - 1];
    if(paddingLength <= 16) {
        output[decodedLength - paddingLength] = '\0';
    }
    
    String decryptedData = String((char *)output);
    
    free(output);
    free(decodedData);
    mbedtls_aes_free(&aes);

    return decryptedData;
}

String readEncryptedPreferences(const char* preference_key) {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) { // true = read-only mode
        logger.error("Failed to open preferences", "utils::readEncryptedPreferences");
        return String("");
    }

    String _encryptedData = preferences.getString(preference_key, "");
    preferences.end();

    if (_encryptedData.isEmpty()) {
        logger.warning("No encrypted data found for key: %s", "utils::readEncryptedPreferences", preference_key);
        return String("");
    }

    return decryptData(_encryptedData, String(preshared_encryption_key) + getDeviceId());
}

bool checkCertificatesExist() {
    logger.debug("Checking if certificates exist...", "utils::checkCertificatesExist");

    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
        logger.error("Failed to open preferences", "utils::checkCertificatesExist");
        return false;
    }

    bool _deviceCertExists = !preferences.getString(PREFS_KEY_CERTIFICATE, "").isEmpty(); 
    bool _privateKeyExists = !preferences.getString(PREFS_KEY_PRIVATE_KEY, "").isEmpty();

    preferences.end();

    bool _allCertificatesExist = _deviceCertExists && _privateKeyExists;

    logger.debug("Certificates exist: %s", "utils::checkCertificatesExist", _allCertificatesExist ? "true" : "false");
    return _allCertificatesExist;
}

void writeEncryptedPreferences(const char* preference_key, const char* value) {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) { // false = read-write mode
        logger.error("Failed to open preferences", "utils::writeEncryptedPreferences");
        return;
    }

    preferences.putString(preference_key, value);
    preferences.end();
}

void clearCertificates() {
    logger.debug("Clearing certificates...", "utils::clearCertificates");

    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) logger.error("Failed to open preferences", "utils::clearCertificates");

    preferences.clear();
    preferences.end();

    logger.warning("Certificates cleared", "utils::clearCertificates");
}

bool setupMdns()
{
    logger.info("Setting up mDNS...", "utils::setupMdns");
    if (
        MDNS.begin(MDNS_HOSTNAME) &&
        MDNS.addService("http", "tcp", WEBSERVER_PORT) &&
        MDNS.addService("mqtt", "tcp", MQTT_CUSTOM_PORT_DEFAULT) &&
        MDNS.addService("modbus", "tcp", MODBUS_TCP_PORT))
    {
        logger.info("mDNS setup done", "utils::setupMdns");
        return true;
    }
    else
    {
        logger.error("Error setting up mDNS", "utils::setupMdns");
        return false;
    }
}