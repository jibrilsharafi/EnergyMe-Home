#include "utils.h"

static const char *TAG = "utils";

void getJsonProjectInfo(JsonDocument& jsonDocument) { 
    logger.debug("Getting project info...", TAG);

    jsonDocument["companyName"] = COMPANY_NAME;
    jsonDocument["fullProductName"] = FULL_PRODUCT_NAME;
    jsonDocument["productName"] = PRODUCT_NAME;
    jsonDocument["productDescription"] = PRODUCT_DESCRIPTION;
    jsonDocument["githubUrl"] = GITHUB_URL;
    jsonDocument["author"] = AUTHOR;
    jsonDocument["authorEmail"] = AUTHOR_EMAIL;

    logger.debug("Project info retrieved", TAG);
}

void getJsonDeviceInfo(JsonDocument& jsonDocument)
{
    logger.debug("Getting device info...", TAG);

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

    jsonDocument["device"]["id"] = getDeviceId();

    logger.debug("Device info retrieved", TAG);
}

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument) {
    logger.debug("Deserializing JSON from SPIFFS", TAG);

    TRACE
    File _file = SPIFFS.open(path, FILE_READ);
    if (!_file){
        logger.error("%s Failed to open file", TAG, path);
        return;
    }

    DeserializationError _error = deserializeJson(jsonDocument, _file);
    _file.close();
    if (_error){
        logger.error("Failed to deserialize file %s. Error: %s", TAG, path, _error.c_str());
        return;
    }

    if (jsonDocument.isNull() || jsonDocument.size() == 0){
        logger.debug("%s JSON being deserialized is {}", TAG, path);
    }
    
    String _jsonString;
    serializeJson(jsonDocument, _jsonString);

    logger.debug("JSON deserialized from SPIFFS correctly: %s", TAG, _jsonString.c_str());
}

bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument){
    logger.debug("Serializing JSON to SPIFFS...", TAG);

    TRACE
    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file){
        logger.error("%s Failed to open file", TAG, path);
        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    if (jsonDocument.isNull() || jsonDocument.size() == 0){ // It should never happen as createEmptyJsonFile should be used instead
        logger.debug("%s JSON being serialized is {}", TAG, path);
    }

    String _jsonString;
    serializeJson(jsonDocument, _jsonString);
    logger.debug("JSON serialized to SPIFFS correctly: %s", TAG, _jsonString.c_str());

    return true;
}

void createEmptyJsonFile(const char* path) {
    logger.debug("Creating empty JSON file %s...", TAG, path);

    TRACE
    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", TAG, path);
        return;
    }

    _file.print("{}");
    _file.close();

    logger.debug("Empty JSON file %s created", TAG, path);
}

void createDefaultGeneralConfigurationFile() {
    logger.debug("Creating default general %s...", TAG, GENERAL_CONFIGURATION_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["isCloudServicesEnabled"] = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    _jsonDocument["gmtOffset"] = DEFAULT_GMT_OFFSET;
    _jsonDocument["dstOffset"] = DEFAULT_DST_OFFSET;
    _jsonDocument["ledBrightness"] = DEFAULT_LED_BRIGHTNESS;
    _jsonDocument["sendPowerData"] = DEFAULT_SEND_POWER_DATA;

    serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, GENERAL_CONFIGURATION_JSON_PATH);
}

void createDefaultEnergyFile() {
    logger.debug("Creating default %s...", TAG, ENERGY_JSON_PATH);

    JsonDocument _jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        _jsonDocument[String(i)]["activeEnergyImported"] = 0;
        _jsonDocument[String(i)]["activeEnergyExported"] = 0;
        _jsonDocument[String(i)]["reactiveEnergyImported"] = 0;
        _jsonDocument[String(i)]["reactiveEnergyExported"] = 0;
        _jsonDocument[String(i)]["apparentEnergy"] = 0;
    }

    serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, ENERGY_JSON_PATH);
}

void createDefaultDailyEnergyFile() {
    logger.debug("Creating default %s...", TAG, DAILY_ENERGY_JSON_PATH);

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);

    logger.debug("Default %s created", TAG, DAILY_ENERGY_JSON_PATH);
}

void createDefaultFirmwareUpdateInfoFile() {
    logger.debug("Creating default %s...", TAG, FW_UPDATE_INFO_JSON_PATH);

    createEmptyJsonFile(FW_UPDATE_INFO_JSON_PATH);

    logger.debug("Default %s created", TAG, FW_UPDATE_INFO_JSON_PATH);
}

void createDefaultFirmwareUpdateStatusFile() {
    logger.debug("Creating default %s...", TAG, FW_UPDATE_STATUS_JSON_PATH);

    createEmptyJsonFile(FW_UPDATE_STATUS_JSON_PATH);

    logger.debug("Default %s created", TAG, FW_UPDATE_STATUS_JSON_PATH);
}

void createDefaultAde7953ConfigurationFile() {
    logger.debug("Creating default %s...", TAG, CONFIGURATION_ADE7953_JSON_PATH);

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

    logger.debug("Default %s created", TAG, CONFIGURATION_ADE7953_JSON_PATH);
}

void createDefaultCalibrationFile() {
    logger.debug("Creating default %s...", TAG, CALIBRATION_JSON_PATH);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_calibration_json);

    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, CALIBRATION_JSON_PATH);
}

void createDefaultChannelDataFile() {
    logger.debug("Creating default %s...", TAG, CHANNEL_DATA_JSON_PATH);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    serializeJsonToSpiffs(CHANNEL_DATA_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, CHANNEL_DATA_JSON_PATH);
}

void createDefaultCustomMqttConfigurationFile() {
    logger.debug("Creating default %s...", TAG, CUSTOM_MQTT_CONFIGURATION_JSON_PATH);

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
    _jsonDocument["lastConnectionStatus"] = "Never attempted";
    _jsonDocument["lastConnectionAttemptTimestamp"] = "";

    serializeJsonToSpiffs(CUSTOM_MQTT_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, CUSTOM_MQTT_CONFIGURATION_JSON_PATH);
}

void createDefaultInfluxDbConfigurationFile() {
    logger.debug("Creating default %s...", TAG, INFLUXDB_CONFIGURATION_JSON_PATH);

    JsonDocument _jsonDocument;

    _jsonDocument["enabled"] = DEFAULT_IS_INFLUXDB_ENABLED;
    _jsonDocument["server"] = INFLUXDB_SERVER_DEFAULT;
    _jsonDocument["port"] = INFLUXDB_PORT_DEFAULT;
    _jsonDocument["version"] = INFLUXDB_VERSION_DEFAULT; // Default to v2
    _jsonDocument["database"] = INFLUXDB_DATABASE_DEFAULT;
    _jsonDocument["username"] = INFLUXDB_USERNAME_DEFAULT;
    _jsonDocument["password"] = INFLUXDB_PASSWORD_DEFAULT;
    _jsonDocument["organization"] = INFLUXDB_ORGANIZATION_DEFAULT;
    _jsonDocument["bucket"] = INFLUXDB_BUCKET_DEFAULT;
    _jsonDocument["token"] = INFLUXDB_TOKEN_DEFAULT;
    _jsonDocument["measurement"] = INFLUXDB_MEASUREMENT_DEFAULT;
    _jsonDocument["frequency"] = INFLUXDB_FREQUENCY_DEFAULT;
    _jsonDocument["useSSL"] = INFLUXDB_USE_SSL_DEFAULT;
    _jsonDocument["lastConnectionStatus"] = "Never attempted";
    _jsonDocument["lastConnectionAttemptTimestamp"] = "";

    serializeJsonToSpiffs(INFLUXDB_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, INFLUXDB_CONFIGURATION_JSON_PATH);
}

std::vector<const char*> checkMissingFiles() {
    logger.debug("Checking missing files...", TAG);

    std::vector<const char*> missingFiles;
    
    const char* CONFIG_FILE_PATHS[] = {
        GENERAL_CONFIGURATION_JSON_PATH,
        CONFIGURATION_ADE7953_JSON_PATH,
        CALIBRATION_JSON_PATH,
        CHANNEL_DATA_JSON_PATH,
        CUSTOM_MQTT_CONFIGURATION_JSON_PATH,
        INFLUXDB_CONFIGURATION_JSON_PATH,
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

    logger.debug("Missing files checked", TAG);
    return missingFiles;
}

void createDefaultFilesForMissingFiles(const std::vector<const char*>& missingFiles) {
    logger.debug("Creating default files for missing files...", TAG);

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
        } else if (strcmp(path, INFLUXDB_CONFIGURATION_JSON_PATH) == 0) {
            createDefaultInfluxDbConfigurationFile();
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
            logger.warning("No default creation function for path: %s", TAG, path);
        }
    }

    logger.debug("Default files created for missing files", TAG);
}

bool checkAllFiles() {
    logger.debug("Checking all files...", TAG);

    TRACE
    std::vector<const char*> missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        createDefaultFilesForMissingFiles(missingFiles);
        return true;
    }

    logger.debug("All files checked", TAG);
    return false;
}

void setRestartEsp32(const char* functionName, const char* reason) { 
    logger.info("Restart required from function %s. Reason: %s", TAG, functionName, reason);
    
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
    led.setWhite(true);

    TRACE
    clearAllAuthTokens();

    TRACE
    logger.info("Restarting ESP32 from function %s. Reason: %s", TAG, restartConfiguration.functionName.c_str(), restartConfiguration.reason.c_str());

    // If a firmware evaluation is in progress, set the firmware to test again
    TRACE
    FirmwareState _firmwareStatus = CrashMonitor::getFirmwareStatus();

    TRACE
    if (_firmwareStatus == TESTING) {
        logger.info("Firmware evaluation is in progress. Setting firmware to test again", TAG);
        TRACE
        if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) logger.error("Failed to set firmware status", TAG);
    }

    logger.end();

    TRACE
    ESP.restart();
}

// Print functions
// -----------------------------

void printMeterValues(MeterValues* meterValues, ChannelData* channelData) {
    logger.debug(
        "%s (%D): %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh <- | %.3f Wh -> | %.3f VARh <- | %.3f VARh -> | %.3f VAh", 
        TAG, 
        channelData->label.c_str(),
        channelData->index,
        meterValues->voltage, 
        meterValues->current, 
        meterValues->activePower, 
        meterValues->reactivePower, 
        meterValues->apparentPower, 
        meterValues->powerFactor, 
        meterValues->activeEnergyImported,
        meterValues->activeEnergyExported,
        meterValues->reactiveEnergyImported, 
        meterValues->reactiveEnergyExported, 
        meterValues->apparentEnergy
    );
}

void printDeviceStatus()
{
    unsigned int heapSize = ESP.getHeapSize();
    unsigned int freeHeap = ESP.getFreeHeap();
    unsigned int minFreeHeap = ESP.getMinFreeHeap();
    unsigned int maxAllocHeap = ESP.getMaxAllocHeap();

    unsigned int SpiffsTotalBytes = SPIFFS.totalBytes();
    unsigned int SpiffsUsedBytes = SPIFFS.usedBytes();
    unsigned int SpiffsFreeBytes = SpiffsTotalBytes - SpiffsUsedBytes;
    
    float heapUsedPercentage = ((heapSize - freeHeap) / heapSize) * 100.0;
    float spiffsUsedPercentage = ((float)SpiffsUsedBytes / SpiffsTotalBytes) * 100.0;

    logger.debug(
        "Heap: %.1f%% (%u/%u bytes) (min: %u bytes, max alloc: %u bytes) | SPIFFS: %.1f%% (%u/%u bytes) (free: %u bytes)",
        TAG,
        heapUsedPercentage,
        freeHeap,
        heapSize,
        minFreeHeap,
        maxAllocHeap,
        spiffsUsedPercentage,
        SpiffsUsedBytes,
        SpiffsTotalBytes,
        SpiffsFreeBytes
    );
}

// General configuration
// -----------------------------

bool setGeneralConfigurationFromSpiffs() {
    logger.debug("Setting general configuration from SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    if (!setGeneralConfiguration(_jsonDocument)) {
        logger.error("Failed to open general configuration file", TAG);
        setDefaultGeneralConfiguration();
        return false;
    }
    
    logger.debug("General configuration set from SPIFFS", TAG);
    return true;
}

void setDefaultGeneralConfiguration() {
    logger.debug("Setting default general configuration...", TAG);
    
    createDefaultGeneralConfigurationFile();

    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    setGeneralConfiguration(_jsonDocument);
    
    logger.debug("Default general configuration set", TAG);
}

void saveGeneralConfigurationToSpiffs() {
    logger.debug("Saving general configuration to SPIFFS...", TAG);

    JsonDocument _jsonDocument;
    generalConfigurationToJson(generalConfiguration, _jsonDocument);

    serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument);

    logger.debug("General configuration saved to SPIFFS", TAG);
}

bool setGeneralConfiguration(JsonDocument& jsonDocument) {
    logger.debug("Setting general configuration...", TAG);

    if (!validateGeneralConfigurationJson(jsonDocument)) {
        logger.warning("Failed to set general configuration", TAG);
        return false;
    }
#if HAS_SECRETS
    generalConfiguration.isCloudServicesEnabled = jsonDocument["isCloudServicesEnabled"].as<bool>();
    generalConfiguration.sendPowerData = jsonDocument["sendPowerData"].as<bool>();
#else
    logger.info("Cloud services cannot be enabled due to missing secrets", TAG);
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    generalConfiguration.sendPowerData = DEFAULT_SEND_POWER_DATA;
#endif
    generalConfiguration.gmtOffset = jsonDocument["gmtOffset"].as<int>();
    generalConfiguration.dstOffset = jsonDocument["dstOffset"].as<int>();
    generalConfiguration.ledBrightness = jsonDocument["ledBrightness"].as<int>();

    applyGeneralConfiguration();

    saveGeneralConfigurationToSpiffs();

    publishMqtt.generalConfiguration = true;

    logger.debug("General configuration set", TAG);

    return true;
}

void generalConfigurationToJson(GeneralConfiguration& generalConfiguration, JsonDocument& jsonDocument) {
    logger.debug("Converting general configuration to JSON...", TAG);

    jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    jsonDocument["gmtOffset"] = generalConfiguration.gmtOffset;
    jsonDocument["dstOffset"] = generalConfiguration.dstOffset;
    jsonDocument["ledBrightness"] = generalConfiguration.ledBrightness;
    jsonDocument["sendPowerData"] = generalConfiguration.sendPowerData;

    logger.debug("General configuration converted to JSON", TAG);
}

void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument) {
    logger.debug("Converting statistics to JSON...", TAG);

    jsonDocument["ade7953TotalInterrupts"] = statistics.ade7953TotalInterrupts;
    jsonDocument["ade7953TotalHandledInterrupts"] = statistics.ade7953TotalHandledInterrupts;
    jsonDocument["ade7953ReadingCount"] = statistics.ade7953ReadingCount;
    jsonDocument["ade7953ReadingCountFailure"] = statistics.ade7953ReadingCountFailure;
    
    jsonDocument["mqttMessagesPublished"] = statistics.mqttMessagesPublished;
    jsonDocument["mqttMessagesPublishedError"] = statistics.mqttMessagesPublishedError;
    
    jsonDocument["customMqttMessagesPublished"] = statistics.customMqttMessagesPublished;
    jsonDocument["customMqttMessagesPublishedError"] = statistics.customMqttMessagesPublishedError;
    
    jsonDocument["modbusRequests"] = statistics.modbusRequests;
    jsonDocument["modbusRequestsError"] = statistics.modbusRequestsError;
    
    jsonDocument["influxdbUploadCount"] = statistics.influxdbUploadCount;
    jsonDocument["influxdbUploadCountError"] = statistics.influxdbUploadCountError;
    
    jsonDocument["wifiConnection"] = statistics.wifiConnection;
    jsonDocument["wifiConnectionError"] = statistics.wifiConnectionError;

    logger.debug("Statistics converted to JSON", TAG);
}

void applyGeneralConfiguration() {
    logger.debug("Applying general configuration...", TAG);

    led.setBrightness(generalConfiguration.ledBrightness);

    logger.debug("General configuration applied", TAG);
}

bool validateGeneralConfigurationJson(JsonDocument& jsonDocument) {
    logger.debug("Validating general configuration JSON...", TAG);

    if (!jsonDocument.is<JsonObject>()) { logger.warning("JSON is not an object", TAG); return false; }
    
    if (!jsonDocument["isCloudServicesEnabled"].is<bool>()) { logger.warning("isCloudServicesEnabled is not a boolean", TAG); return false; }
    if (!jsonDocument["gmtOffset"].is<int>()) { logger.warning("gmtOffset is not an integer", TAG); return false; }
    if (!jsonDocument["dstOffset"].is<int>()) { logger.warning("dstOffset is not an integer", TAG); return false; }
    if (!jsonDocument["ledBrightness"].is<int>()) { logger.warning("ledBrightness is not an integer", TAG); return false; }
    if (!jsonDocument["sendPowerData"].is<bool>()) { logger.warning("sendPowerData is not a boolean", TAG); return false; }

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
                TAG,
                publicLocation->country.c_str(),
                publicLocation->city.c_str(),
                publicLocation->latitude.toFloat(),
                publicLocation->longitude.toFloat()
            );
        }
    } else {
        logger.error("Error on HTTP request: %d", TAG, httpCode);
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
                TAG,
                _jsonDocument["rawOffset"].as<int>(),
                _jsonDocument["dstOffset"].as<int>()
            );
        }
    } else {
        logger.error(
            "Error on HTTP request: %d", 
            TAG, 
            httpCode
        );
    }
}

void updateTimezone() {
    if (!WiFi.isConnected()) {
        logger.warning("WiFi is not connected. Cannot update timezone", TAG);
        return;
    }

    logger.debug("Updating timezone...", TAG);

    getPublicTimezone(&generalConfiguration.gmtOffset, &generalConfiguration.dstOffset);
    saveGeneralConfigurationToSpiffs();

    logger.debug("Timezone updated", TAG);
}

void factoryReset() { 
    logger.fatal("Factory reset requested", TAG);

    mainFlags.blockLoop = true;

    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setRed(true);

    clearAllPreferences();
    SPIFFS.format();

    // Directly call ESP.restart() so that a fresh start is done
    ESP.restart();
}

void clearAllPreferences() {
    logger.fatal("Clear all preferences requested", TAG);

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
    
    preferences.begin(PREFERENCES_NAMESPACE_AUTH, false); // false = read-write mode
    preferences.clear();
    preferences.end();
}

bool isLatestFirmwareInstalled() {
    JsonDocument _jsonDocument;
    deserializeJsonFromSpiffs(FW_UPDATE_INFO_JSON_PATH, _jsonDocument);
    
    if (_jsonDocument.isNull() || _jsonDocument.size() == 0) {
        logger.debug("Firmware update info file is empty", TAG);
        return true;
    }

    String _latestFirmwareVersion = _jsonDocument["buildVersion"].as<String>();
    String _currentFirmwareVersion = FIRMWARE_BUILD_VERSION;

    logger.debug(
        "Latest firmware version: %s | Current firmware version: %s",
        TAG,
        _latestFirmwareVersion.c_str(),
        _currentFirmwareVersion.c_str()
    );

    if (_latestFirmwareVersion.length() == 0 || _latestFirmwareVersion.indexOf(".") == -1) {
        logger.warning("Latest firmware version is empty or in the wrong format", TAG);
        return true;
    }

    int _latestMajor, _latestMinor, _latestPatch;
    sscanf(_latestFirmwareVersion.c_str(), "%d.%d.%d", &_latestMajor, &_latestMinor, &_latestPatch);

    int _currentMajor = atoi(FIRMWARE_BUILD_VERSION_MAJOR);
    int _currentMinor = atoi(FIRMWARE_BUILD_VERSION_MINOR);
    int _currentPatch = atoi(FIRMWARE_BUILD_VERSION_PATCH);

    if (_latestMajor < _currentMajor) return true;
    if (_latestMajor > _currentMajor) return false;
    if (_latestMinor < _currentMinor) return true;
    if (_latestMinor > _currentMinor) return false;
    return _latestPatch <= _currentPatch;
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
        logger.error("Failed to open preferences", TAG);
        return String("");
    }

    String _encryptedData = preferences.getString(preference_key, "");
    preferences.end();

    if (_encryptedData.isEmpty()) {
        logger.warning("No encrypted data found for key: %s", TAG, preference_key);
        return String("");
    }

    return decryptData(_encryptedData, String(preshared_encryption_key) + getDeviceId());
}

bool checkCertificatesExist() {
    logger.debug("Checking if certificates exist...", TAG);

    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
        logger.error("Failed to open preferences", TAG);
        return false;
    }

    bool _deviceCertExists = !preferences.getString(PREFS_KEY_CERTIFICATE, "").isEmpty(); 
    bool _privateKeyExists = !preferences.getString(PREFS_KEY_PRIVATE_KEY, "").isEmpty();

    preferences.end();

    bool _allCertificatesExist = _deviceCertExists && _privateKeyExists;

    logger.debug("Certificates exist: %s", TAG, _allCertificatesExist ? "true" : "false");
    return _allCertificatesExist;
}

void writeEncryptedPreferences(const char* preference_key, const char* value) {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) { // false = read-write mode
        logger.error("Failed to open preferences", TAG);
        return;
    }

    preferences.putString(preference_key, value);
    preferences.end();
}

void clearCertificates() {
    logger.debug("Clearing certificates...", TAG);

    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) {
        logger.error("Failed to open preferences", TAG);
        return;
    }

    preferences.clear();
    preferences.end();

    logger.info("Certificates for cloud services cleared", TAG);
}

// Authentication functions
// -----------------------------

// Static variables for token management
static int activeTokenCount = 0;

void initializeAuthentication() {
    logger.debug("Initializing authentication...", TAG);
    
    clearAllAuthTokens();
    activeTokenCount = 0;
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
        logger.warning("Failed to open auth preferences for reading, trying to create namespace", TAG);
        
        // Try to create the namespace by opening in write mode
        if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
            logger.error("Failed to create auth preferences namespace", TAG);
            return;
        }
        preferences.end();
        
        // Now try to open in read mode again
        if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
            logger.error("Failed to open auth preferences after creation", TAG);
            return;
        }
    }
    
    String storedPassword = preferences.getString(PREFERENCES_KEY_PASSWORD, "");
    preferences.end();
    
    if (storedPassword.isEmpty()) {
        logger.info("No password set, using default password", TAG);
        if (!setAuthPassword(DEFAULT_WEB_PASSWORD)) {
            logger.error("Failed to set default password", TAG);
            return;
        }
    }
    
    logger.debug("Authentication initialized", TAG);
}

bool validatePassword(const String& password) {
    if (password.length() < MIN_PASSWORD_LENGTH || password.length() > MAX_PASSWORD_LENGTH) {
        return false;
    }
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
        logger.warning("Failed to open auth preferences, allowing access as fallback", TAG);
        return true; // Fallback: return true when preferences cannot be opened
    }
    
    String storedPasswordHash = preferences.getString(PREFERENCES_KEY_PASSWORD, "");
    preferences.end();
    
    if (storedPasswordHash.isEmpty()) {
        logger.debug("No stored password hash found, allowing access as fallback", TAG);
        return true; // Fallback: return true when no password is stored
    }
    
    String passwordHash = hashPassword(password);
    return passwordHash.equals(storedPasswordHash);
}

bool setAuthPassword(const String& newPassword) {
    if (newPassword.length() < MIN_PASSWORD_LENGTH || newPassword.length() > MAX_PASSWORD_LENGTH) {
        logger.warning("Password length invalid: %d characters", TAG, newPassword.length());
        return false;
    }
    
    String passwordHash = hashPassword(newPassword);
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        logger.warning("Failed to open auth preferences, treating as successful fallback", TAG);
        return true; // Fallback: return true when preferences cannot be opened
    }
    
    preferences.putString(PREFERENCES_KEY_PASSWORD, passwordHash);
    preferences.end();
    
    // Clear all existing tokens when password changes
    clearAllAuthTokens();
    
    logger.info("Password updated successfully", TAG);
    return true;
}
String generateAuthToken() {
    // Check if we can accept more tokens
    if (!canAcceptMoreTokens()) {
        logger.warning("Cannot generate new token: maximum concurrent sessions reached (%d)", TAG, MAX_CONCURRENT_SESSIONS);
        return "";
    }
    
    String token = "";
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    for (int i = 0; i < AUTH_TOKEN_LENGTH; i++) {
        token += chars[random(0, sizeof(chars) - 1)];
    }
    
    // Store token with timestamp using a hash-based short key
    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        // Create a short hash from the token for the key (max 15 chars for NVS)
        uint32_t tokenHash = 0;
        for (int i = 0; i < token.length(); i++) {
            tokenHash = tokenHash * 31 + token.charAt(i);
        }
        String shortKey = "t" + String(tokenHash, HEX); // "t" + 8-char hex = 9 chars max
        
        // Store both the timestamp and the original token
        preferences.putULong64(shortKey.c_str(), millis());
        // Store mapping from short key to full token
        preferences.putString((shortKey + "_f").c_str(), token); // "_f" for full token
        preferences.end();
        
        // Increment the active token count
        activeTokenCount++;
        logger.debug("Token generated successfully. Active tokens: %d/%d", TAG, activeTokenCount, MAX_CONCURRENT_SESSIONS);
    }
    
    return token;
}

bool validateAuthToken(const String& token) {
    if (token.length() != AUTH_TOKEN_LENGTH) {
        return false;
    }
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
        logger.warning("Failed to open auth preferences for token validation, allowing access as fallback", TAG);
        return true; // Fallback: return true when preferences cannot be opened
    }
    
    // Create the same hash-based short key
    uint32_t tokenHash = 0;
    for (int i = 0; i < token.length(); i++) {
        tokenHash = tokenHash * 31 + token.charAt(i);
    }
    String shortKey = "t" + String(tokenHash, HEX);
    
    unsigned long long tokenTimestamp = preferences.getULong64(shortKey.c_str(), 0);
    
    // Verify the full token matches
    String storedToken = preferences.getString((shortKey + "_f").c_str(), "");
    preferences.end();
    
    if (tokenTimestamp == 0 || !storedToken.equals(token)) {
        return false; // Token doesn't exist or doesn't match
    }
    
    // Check if token has expired
    if (millis() - tokenTimestamp > AUTH_SESSION_TIMEOUT) {
        logger.debug("Token expired, removing it", TAG);
        clearAuthToken(token);
        return false;
    }
    
    return true;
}

void clearAuthToken(const String& token) {
    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        // Create the same hash-based short key
        uint32_t tokenHash = 0;
        for (int i = 0; i < token.length(); i++) {
            tokenHash = tokenHash * 31 + token.charAt(i);
        }
        String shortKey = "t" + String(tokenHash, HEX);
        
        // Check if token exists before removing
        if (preferences.getULong64(shortKey.c_str(), 0) != 0) {
            preferences.remove(shortKey.c_str());
            preferences.remove((shortKey + "_f").c_str());
            
            // Decrement the active token count
            if (activeTokenCount > 0) {
                activeTokenCount--;
                logger.debug("Token cleared. Active tokens: %d/%d", TAG, activeTokenCount, MAX_CONCURRENT_SESSIONS);
            }
        }
        preferences.end();
    }
}

void clearAllAuthTokens() {
    logger.debug("Clearing all auth tokens...", TAG);
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        return;
    }
    
    // Clear all preferences except password to remove all tokens
    String storedPassword = preferences.getString(PREFERENCES_KEY_PASSWORD, "");
    preferences.clear();
    
    // Restore the password
    if (!storedPassword.isEmpty()) {
        preferences.putString(PREFERENCES_KEY_PASSWORD, storedPassword);
    }
    
    preferences.end();
    
    // Reset the token count
    activeTokenCount = 0;
    logger.debug("All tokens cleared. Active tokens: %d", TAG, activeTokenCount);
}

String hashPassword(const String& password) {
    // Simple hash using device ID as salt
    String saltedPassword = password + getDeviceId();
    
    // Basic hash implementation (you might want to use a more secure method)
    uint32_t hash = 0;
    for (int i = 0; i < saltedPassword.length(); i++) {
        hash = hash * 31 + saltedPassword.charAt(i);
    }
    
    return String(hash, HEX);
}

int getActiveTokenCount() {
    return activeTokenCount;
}

bool canAcceptMoreTokens() {
    return activeTokenCount < MAX_CONCURRENT_SESSIONS;
}

bool setupMdns()
{
    logger.debug("Setting up mDNS...", TAG);
    
    // Ensure mDNS is stopped before starting
    MDNS.end();
    delay(100);

    if (
        MDNS.begin(MDNS_HOSTNAME) &&
        MDNS.addService("http", "tcp", WEBSERVER_PORT) &&
        MDNS.addService("mqtt", "tcp", MQTT_CUSTOM_PORT_DEFAULT) &&
        MDNS.addService("modbus", "tcp", MODBUS_TCP_PORT))
    {
        logger.info("mDNS setup done", TAG);
        return true;
    }
    else
    {
        logger.warning("Error setting up mDNS", TAG);
        return false;
    }
}

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds) {
    if (isMilliseconds) {
        return (unixTime >= MINIMUM_UNIX_TIME_MILLISECONDS && unixTime <= MAXIMUM_UNIX_TIME_MILLISECONDS);
    } else {
        return (unixTime >= MINIMUM_UNIX_TIME && unixTime <= MAXIMUM_UNIX_TIME);
    }
}

bool isUsingDefaultPassword() {
    return validatePassword(DEFAULT_WEB_PASSWORD);
}

// Rate limiting functions for DoS protection
// -----------------------------

// Static array to store rate limit entries (ESP32 memory constraint)
static RateLimitEntry rateLimitEntries[MAX_TRACKED_IPS];
static int rateLimitEntryCount = 0;
static unsigned long lastCleanupTime = 0;

void initializeRateLimiting() {
    logger.debug("Initializing rate limiting...", TAG);
    
    // Clear all rate limit entries
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        rateLimitEntries[i] = RateLimitEntry();
    }
    rateLimitEntryCount = 0;
    lastCleanupTime = millis();
    
    logger.debug("Rate limiting initialized", TAG);
}

bool isIpBlocked(const String& clientIp) {
    if (clientIp.isEmpty()) {
        return false; // Don't block if IP is unknown
    }
    
    // Clean up old entries periodically
    if (millis() - lastCleanupTime > RATE_LIMIT_CLEANUP_INTERVAL) {
        cleanupOldRateLimitEntries();
        lastCleanupTime = millis();
    }
    
    // Look for existing entry for this IP
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (rateLimitEntries[i].ipAddress.equals(clientIp)) {
            // Check if IP is currently blocked
            if (rateLimitEntries[i].blockedUntil > millis()) {
                logger.debug("IP %s is blocked until %lu (current: %lu)", TAG, 
                           clientIp.c_str(), rateLimitEntries[i].blockedUntil, millis());
                return true;
            }
            break;
        }
    }
    
    return false;
}

void recordFailedLogin(const String& clientIp) {
    if (clientIp.isEmpty()) {
        return; // Can't track empty IP
    }
    
    logger.debug("Recording failed login for IP: %s", TAG, clientIp.c_str());
    
    // Look for existing entry for this IP
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (rateLimitEntries[i].ipAddress.equals(clientIp)) {
            rateLimitEntries[i].failedAttempts++;
            rateLimitEntries[i].lastFailedAttempt = millis();
            
            // Block IP if too many failed attempts
            if (rateLimitEntries[i].failedAttempts >= MAX_LOGIN_ATTEMPTS) {
                rateLimitEntries[i].blockedUntil = millis() + LOGIN_BLOCK_DURATION;
                logger.warning("IP %s blocked for %d minutes after %d failed login attempts", TAG,
                             clientIp.c_str(), LOGIN_BLOCK_DURATION / (60 * 1000), rateLimitEntries[i].failedAttempts);
            } else {
                logger.debug("IP %s has %d failed attempts", TAG, clientIp.c_str(), rateLimitEntries[i].failedAttempts);
            }
            return;
        }
    }
    
    // Create new entry if we have space
    if (rateLimitEntryCount < MAX_TRACKED_IPS) {
        rateLimitEntries[rateLimitEntryCount] = RateLimitEntry(clientIp);
        rateLimitEntries[rateLimitEntryCount].failedAttempts = 1;
        rateLimitEntries[rateLimitEntryCount].lastFailedAttempt = millis();
        rateLimitEntryCount++;
        logger.debug("New rate limit entry created for IP: %s", TAG, clientIp.c_str());
    } else {
        // Array is full, replace oldest entry
        int oldestIndex = 0;
        unsigned long oldestTime = rateLimitEntries[0].lastFailedAttempt;
        
        for (int i = 1; i < MAX_TRACKED_IPS; i++) {
            if (rateLimitEntries[i].lastFailedAttempt < oldestTime) {
                oldestTime = rateLimitEntries[i].lastFailedAttempt;
                oldestIndex = i;
            }
        }
        
        logger.debug("Rate limit array full, replacing oldest entry (IP: %s) with new IP: %s", TAG, 
                   rateLimitEntries[oldestIndex].ipAddress.c_str(), clientIp.c_str());
        
        rateLimitEntries[oldestIndex] = RateLimitEntry(clientIp);
        rateLimitEntries[oldestIndex].failedAttempts = 1;
        rateLimitEntries[oldestIndex].lastFailedAttempt = millis();
    }
}

void recordSuccessfulLogin(const String& clientIp) {
    if (clientIp.isEmpty()) {
        return; // Can't track empty IP
    }
    
    // Reset failed attempts for this IP on successful login
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (rateLimitEntries[i].ipAddress.equals(clientIp)) {
            logger.debug("Resetting failed attempts for IP %s after successful login", TAG, clientIp.c_str());
            rateLimitEntries[i].failedAttempts = 0;
            rateLimitEntries[i].blockedUntil = 0;
            return;
        }
    }
}

void cleanupOldRateLimitEntries() {
    unsigned long currentTime = millis();
    int newCount = 0;
    
    // Remove entries that are no longer blocked and haven't had recent activity
    for (int i = 0; i < rateLimitEntryCount; i++) {
        bool shouldKeep = false;
        
        // Keep if currently blocked
        if (rateLimitEntries[i].blockedUntil > currentTime) {
            shouldKeep = true;
        }
        // Keep if had recent failed attempts (within cleanup interval)
        else if (currentTime - rateLimitEntries[i].lastFailedAttempt < RATE_LIMIT_CLEANUP_INTERVAL) {
            shouldKeep = true;
        }
        
        if (shouldKeep) {
            if (newCount != i) {
                rateLimitEntries[newCount] = rateLimitEntries[i];
            }
            newCount++;
        } else {
            logger.debug("Cleaning up old rate limit entry for IP: %s", TAG, rateLimitEntries[i].ipAddress.c_str());
        }
    }
    
    // Clear remaining entries
    for (int i = newCount; i < rateLimitEntryCount; i++) {
        rateLimitEntries[i] = RateLimitEntry();
    }
    
    int removedCount = rateLimitEntryCount - newCount;
    rateLimitEntryCount = newCount;
    
    if (removedCount > 0) {
        logger.debug("Rate limit cleanup completed. Removed %d entries, %d entries remaining", TAG, removedCount, rateLimitEntryCount);
    }
}