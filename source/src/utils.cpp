#include "utils.h"
#include <WiFi.h>

static const char *TAG = "utils";

void getJsonProductInfo(JsonDocument& jsonDocument) { 
    logger.debug("Getting product info...", TAG);

    jsonDocument["companyName"] = COMPANY_NAME;
    jsonDocument["fullProductName"] = FULL_PRODUCT_NAME;
    jsonDocument["productName"] = PRODUCT_NAME;
    jsonDocument["productDescription"] = PRODUCT_DESCRIPTION;
    jsonDocument["githubUrl"] = GITHUB_URL;
    jsonDocument["author"] = AUTHOR;
    jsonDocument["authorEmail"] = AUTHOR_EMAIL;

    logger.debug("Product info retrieved", TAG);
}

void getJsonDeviceInfo(JsonDocument& jsonDocument)
{
    logger.debug("Getting device info...", TAG);

    jsonDocument["system"]["uptime"] = millis();
    char _timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(_timestampBuffer);
    jsonDocument["system"]["systemTime"] = _timestampBuffer;

    jsonDocument["firmware"]["buildVersion"] = FIRMWARE_BUILD_VERSION;
    jsonDocument["firmware"]["buildDate"] = FIRMWARE_BUILD_DATE;
    jsonDocument["firmware"]["buildTime"] = FIRMWARE_BUILD_TIME;

    jsonDocument["memory"]["heap"]["free"] = ESP.getFreeHeap();
    jsonDocument["memory"]["heap"]["total"] = ESP.getHeapSize();
    jsonDocument["memory"]["heap"]["used"] = ESP.getHeapSize() - ESP.getFreeHeap();
    jsonDocument["memory"]["heap"]["freePercentage"] = ((float)ESP.getFreeHeap() / ESP.getHeapSize()) * 100.0;
    jsonDocument["memory"]["heap"]["usedPercentage"] = ((float)(ESP.getHeapSize() - ESP.getFreeHeap()) / ESP.getHeapSize()) * 100.0;
    
    jsonDocument["memory"]["spiffs"]["free"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    jsonDocument["memory"]["spiffs"]["total"] = SPIFFS.totalBytes();
    jsonDocument["memory"]["spiffs"]["used"] = SPIFFS.usedBytes();
    jsonDocument["memory"]["spiffs"]["freePercentage"] = ((float)(SPIFFS.totalBytes() - SPIFFS.usedBytes()) / SPIFFS.totalBytes()) * 100.0;
    jsonDocument["memory"]["spiffs"]["usedPercentage"] = ((float)SPIFFS.usedBytes() / SPIFFS.totalBytes()) * 100.0;


    jsonDocument["chip"]["model"] = ESP.getChipModel();
    jsonDocument["chip"]["revision"] = ESP.getChipRevision();
    jsonDocument["chip"]["cpuFrequency"] = ESP.getCpuFreqMHz();
    jsonDocument["chip"]["sdkVersion"] = ESP.getSdkVersion();
    jsonDocument["chip"]["id"] = ESP.getEfuseMac();

    char _deviceId[DEVICE_ID_BUFFER_SIZE]; // TODO: understand if we can have a global device ID variable
    getDeviceId(_deviceId, sizeof(_deviceId));
    jsonDocument["device"]["id"] = _deviceId;

    logger.debug("Device info retrieved", TAG);
}

void deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument) {
    logger.debug("Deserializing JSON from SPIFFS", TAG);

    TRACE();
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
    
    // For debugging purposes, serialize to a string and log it
    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    serializeJson(jsonDocument, _jsonString, sizeof(_jsonString));
    logger.debug("JSON deserialized from SPIFFS correctly: %s", TAG, _jsonString);
}

bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument){
    logger.debug("Serializing JSON to SPIFFS...", TAG);

    TRACE();
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

    // For debugging purposes, serialize to a string and log it
    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    serializeJson(jsonDocument, _jsonString, sizeof(_jsonString));
    logger.debug("JSON serialized to SPIFFS correctly: %s", TAG, _jsonString);

    return true;
}

void createEmptyJsonFile(const char* path) {
    logger.debug("Creating empty JSON file %s...", TAG, path);

    TRACE();
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
        _jsonDocument[i]["activeEnergyImported"] = 0;
        _jsonDocument[i]["activeEnergyExported"] = 0;
        _jsonDocument[i]["reactiveEnergyImported"] = 0;
        _jsonDocument[i]["reactiveEnergyExported"] = 0;
        _jsonDocument[i]["apparentEnergy"] = 0;
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

    _jsonDocument["sampleTime"] = MINIMUM_SAMPLE_TIME;
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

    TRACE();
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

    TRACE();
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

    TRACE();
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
    snprintf(restartConfiguration.functionName, sizeof(restartConfiguration.functionName), "%s", functionName);
    snprintf(restartConfiguration.reason, sizeof(restartConfiguration.reason), "%s", reason);

    // Don't cleanup interrupts immediately - let MQTT finish final operations first
    logger.debug("Restart configuration set. Function: %s, Reason: %s, Required at %u", TAG, restartConfiguration.functionName, restartConfiguration.reason, restartConfiguration.requiredAt);
}

void checkIfRestartEsp32Required() {
    if (restartConfiguration.isRequired) {
        if ((millis() - restartConfiguration.requiredAt) > ESP32_RESTART_DELAY) {
            restartEsp32();
        }
    }
}

void restartEsp32() {
    TRACE();
    led.block();
    led.setBrightness(max(led.getBrightness(), 1)); // Show a faint light even if it is off
    led.setWhite(true);

    logger.info("Restarting ESP32 from function %s. Reason: %s", TAG, restartConfiguration.functionName, restartConfiguration.reason);

    TRACE();
    clearAllAuthTokens();

    // If a firmware evaluation is in progress, set the firmware to test again
    TRACE();
    FirmwareState _firmwareStatus = CrashMonitor::getFirmwareStatus();

    TRACE();
    if (_firmwareStatus == TESTING) {
        logger.info("Firmware evaluation is in progress. Setting firmware to test again", TAG);
        TRACE();
        if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) logger.error("Failed to set firmware status", TAG);
    }

    TRACE();
    logger.end();

    TRACE();
    ESP.restart();
}


// Print functions
// -----------------------------

void printMeterValues(MeterValues* meterValues, ChannelData* channelData) {
    logger.debug(
        "%s (%D): %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh <- | %.3f Wh -> | %.3f VARh <- | %.3f VARh -> | %.3f VAh", 
        TAG, 
        channelData->label,
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
    
    float heapUsedPercentage = ((float)(heapSize - freeHeap) / heapSize) * 100.0;
    float heapFreePercentage = ((float)freeHeap / heapSize) * 100.0;
    float spiffsUsedPercentage = ((float)SpiffsUsedBytes / SpiffsTotalBytes) * 100.0;
    float spiffsFreePercentage = ((float)SpiffsFreeBytes / SpiffsTotalBytes) * 100.0;

    logger.debug(
        "Heap: %.1f%% used, %.1f%% free (%u free / %u total bytes) (min: %u bytes, max alloc: %u bytes) | SPIFFS: %.1f%% used, %.1f%% free (%u free / %u total bytes)",
        TAG,
        heapUsedPercentage,
        heapFreePercentage,
        freeHeap,
        heapSize,
        minFreeHeap,
        maxAllocHeap,
        spiffsUsedPercentage,
        spiffsFreePercentage,
        SpiffsFreeBytes,
        SpiffsTotalBytes
    );
}

void printStatistics() {
    logger.debug("Statistics - ADE7953: %d total interrupts | %d handled interrupts | %d readings | %d reading failures", 
        TAG, 
        statistics.ade7953TotalInterrupts, 
        statistics.ade7953TotalHandledInterrupts, 
        statistics.ade7953ReadingCount, 
        statistics.ade7953ReadingCountFailure
    );

    logger.debug("Statistics - MQTT: %d messages published | %d errors", 
        TAG, 
        statistics.mqttMessagesPublished, 
        statistics.mqttMessagesPublishedError
    );

    logger.debug("Statistics - Custom MQTT: %d messages published | %d errors", 
        TAG, 
        statistics.customMqttMessagesPublished, 
        statistics.customMqttMessagesPublishedError
    );

    logger.debug("Statistics - Modbus: %d requests | %d errors", 
        TAG, 
        statistics.modbusRequests, 
        statistics.modbusRequestsError
    );

    logger.debug("Statistics - InfluxDB: %d uploads | %d errors", 
        TAG, 
        statistics.influxdbUploadCount, 
        statistics.influxdbUploadCountError
    );

    logger.debug("Statistics - WiFi: %d connections | %d errors", 
        TAG, 
        statistics.wifiConnection, 
        statistics.wifiConnectionError
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
    if (!publicLocation) {
        logger.error("Null pointer passed to getPublicLocation", TAG);
        return;
    }

    HTTPClient _http;
    JsonDocument _jsonDocument;

    _http.begin(PUBLIC_LOCATION_ENDPOINT);

    int httpCode = _http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            // Use stream directly - efficient and simple
            DeserializationError error = deserializeJson(_jsonDocument, _http.getStream());
            
            if (error) {
                logger.error("JSON parsing failed: %s", TAG, error.c_str());
                _http.end();
                return;
            }
            
            // Validate API response
            if (_jsonDocument["status"] != "success") {
                logger.error("API returned error status: %s", TAG, 
                           _jsonDocument["status"].as<const char*>());
                _http.end();
                return;
            }

            // Extract strings safely using const char* and copy to char arrays
            const char* country = _jsonDocument["country"].as<const char*>();
            const char* city = _jsonDocument["city"].as<const char*>();
            const char* latitude = _jsonDocument["lat"].as<const char*>();
            const char* longitude = _jsonDocument["lon"].as<const char*>();
            
            if (country) snprintf(publicLocation->country, sizeof(publicLocation->country), "%s", country);
            else publicLocation->country[0] = '\0'; // Ensure null termination
            if (city) snprintf(publicLocation->city, sizeof(publicLocation->city), "%s", city);
            else publicLocation->city[0] = '\0'; // Ensure null termination
            if (latitude) snprintf(publicLocation->latitude, sizeof(publicLocation->latitude), "%s", latitude);
            else publicLocation->latitude[0] = '\0'; // Ensure null termination
            if (longitude) snprintf(publicLocation->longitude, sizeof(publicLocation->longitude), "%s", longitude);
            else publicLocation->longitude[0] = '\0'; // Ensure null termination

            logger.debug(
                "Location: %s, %s | Lat: %s | Lon: %s",
                TAG,
                publicLocation->country,
                publicLocation->city,
                publicLocation->latitude,
                publicLocation->longitude
            );
        } else {
            logger.warning("HTTP request failed with code: %d", TAG, httpCode);
        }
    } else {
        logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
    }

    _http.end();
}

void getPublicTimezone(int* gmtOffset, int* dstOffset) {
    if (!gmtOffset || !dstOffset) {
        logger.error("Null pointer passed to getPublicTimezone", TAG);
        return;
    }

    PublicLocation _publicLocation;
    getPublicLocation(&_publicLocation);

    HTTPClient _http;
    JsonDocument _jsonDocument;

    char _url[URL_BUFFER_SIZE];
    snprintf(_url, sizeof(_url), "%slat=%s&lng=%s&username=%s",
        PUBLIC_TIMEZONE_ENDPOINT,
        _publicLocation.latitude,
        _publicLocation.longitude,
        PUBLIC_TIMEZONE_USERNAME);

    _http.begin(_url);
    int httpCode = _http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            // Use stream directly - efficient and simple
            DeserializationError error = deserializeJson(_jsonDocument, _http.getStream());
            
            if (error) {
                logger.error("JSON parsing failed: %s", TAG, error.c_str());
                _http.end();
                return;
            }

            *gmtOffset = _jsonDocument["rawOffset"].as<int>() * 3600; // Convert hours to seconds
            *dstOffset = _jsonDocument["dstOffset"].as<int>() * 3600 - *gmtOffset; // Convert hours to seconds. Remove GMT offset as it is already included in the dst offset

            logger.debug(
                "GMT offset: %d | DST offset: %d",
                TAG,
                _jsonDocument["rawOffset"].as<int>(),
                _jsonDocument["dstOffset"].as<int>()
            );
        } else {
            logger.warning("HTTP request failed with code: %d", TAG, httpCode);
        }
    } else {
        logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
    }

    _http.end();
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

    char _latestFirmwareVersion[VERSION_BUFFER_SIZE];
    char _currentFirmwareVersion[VERSION_BUFFER_SIZE];

    snprintf(_latestFirmwareVersion, sizeof(_latestFirmwareVersion), "%s", _jsonDocument["buildVersion"].as<const char*>());
    snprintf(_currentFirmwareVersion, sizeof(_currentFirmwareVersion), "%s", FIRMWARE_BUILD_VERSION);

    logger.debug(
        "Latest firmware version: %s | Current firmware version: %s",
        TAG,
        _latestFirmwareVersion,
        _currentFirmwareVersion
    );

    if (strlen(_latestFirmwareVersion) == 0 || strchr(_latestFirmwareVersion, '.') == NULL) {
        logger.warning("Latest firmware version is empty or in the wrong format", TAG);
        return true;
    }

    int _latestMajor, _latestMinor, _latestPatch;
    sscanf(_latestFirmwareVersion, "%d.%d.%d", &_latestMajor, &_latestMinor, &_latestPatch);

    int _currentMajor = atoi(FIRMWARE_BUILD_VERSION_MAJOR);
    int _currentMinor = atoi(FIRMWARE_BUILD_VERSION_MINOR);
    int _currentPatch = atoi(FIRMWARE_BUILD_VERSION_PATCH);

    if (_latestMajor < _currentMajor) return true;
    if (_latestMajor > _currentMajor) return false;
    if (_latestMinor < _currentMinor) return true;
    if (_latestMinor > _currentMinor) return false;
    return _latestPatch <= _currentPatch;
}

void updateJsonFirmwareStatus(const char *status, const char *reason)
{
    JsonDocument _jsonDocument;

    _jsonDocument["status"] = status;
    _jsonDocument["reason"] = reason;
    char _timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(_timestampBuffer);
    _jsonDocument["timestamp"] = _timestampBuffer;

    serializeJsonToSpiffs(FW_UPDATE_STATUS_JSON_PATH, _jsonDocument);
}

void getDeviceId(char* deviceId, size_t maxLength) {
    // Copied from WiFiSTA implementation
    if (!deviceId || maxLength < 13) { // 12 chars + null terminator
        logger.error(TAG, "Invalid parameters for device ID generation");
        return;
    }
    
    uint8_t mac[6];
    
    if (WiFiGenericClass::getMode() == WIFI_MODE_NULL) {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    } else {
        esp_wifi_get_mac((wifi_interface_t)ESP_IF_WIFI_STA, mac);
    }
    
    // Use lowercase hex formatting without colons
    snprintf(deviceId, maxLength, "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

void decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize) {
    if (!encryptedData || !key || !decryptedData || decryptedDataSize == 0) {
        if (decryptedData && decryptedDataSize > 0) {
            decryptedData[0] = '\0'; // Ensure null termination on error
        }
        return;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char *)key, KEY_SIZE);

    size_t decodedLength = 0;
    size_t inputLength = strlen(encryptedData);
    if (inputLength == 0) {
        decryptedData[0] = '\0'; // Ensure null termination for empty input
        mbedtls_aes_free(&aes);
        return;
    }
    
    unsigned char *decodedData = (unsigned char *)malloc(inputLength);
    if (!decodedData) {
        decryptedData[0] = '\0';
        mbedtls_aes_free(&aes);
        return;
    }

    int ret = mbedtls_base64_decode(decodedData, inputLength, &decodedLength,
                                   (const unsigned char *)encryptedData,
                                   inputLength);
    if (ret != 0) {
        decryptedData[0] = '\0';
        free(decodedData);
        mbedtls_aes_free(&aes);
        return;
    }
    if (decodedLength == 0) {
        decryptedData[0] = '\0'; // Ensure null termination for empty decoded data
        free(decodedData);
        mbedtls_aes_free(&aes);
        return;
    }
    
    unsigned char *output = (unsigned char *)malloc(decodedLength + 1);
    memset(output, 0, decodedLength + 1);
    
    for(size_t i = 0; i < decodedLength; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, &decodedData[i], &output[i]);
    }
    
    uint8_t paddingLength = output[decodedLength - 1];
    if(paddingLength <= 16) {
        output[decodedLength - paddingLength] = '\0';
    }
    
    snprintf(decryptedData, decryptedDataSize, "%s", (char*)output);

    free(output);
    free(decodedData);
    mbedtls_aes_free(&aes);
}

void readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize) {
    if (!preference_key || !preshared_encryption_key || !decryptedData || decryptedDataSize == 0) {
        if (decryptedData && decryptedDataSize > 0) {
            decryptedData[0] = '\0'; // Ensure null termination on error
        }
        return;
    }

    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) { // true = read-only mode
        logger.error("Failed to open preferences", TAG);
    }

    char _encryptedData[ENCRYPTED_DATA_BUFFER_SIZE];
    preferences.getString(preference_key, _encryptedData, ENCRYPTED_DATA_BUFFER_SIZE);
    preferences.end();

    if (strlen(_encryptedData) == 0) {
        logger.warning("No encrypted data found for key: %s", TAG, preference_key);
        return;
    }

    char _deviceId[DEVICE_ID_BUFFER_SIZE];
    getDeviceId(_deviceId, DEVICE_ID_BUFFER_SIZE);
    char _encryptionKey[ENCRYPTION_KEY_BUFFER_SIZE];
    snprintf(_encryptionKey, ENCRYPTION_KEY_BUFFER_SIZE, "%s%s", preshared_encryption_key, _deviceId);

    decryptData(_encryptedData, _encryptionKey, decryptedData, decryptedDataSize);
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
    
    // Read the stored password to check if it exists
    char storedPassword[AUTH_PASSWORD_BUFFER_SIZE];
    preferences.getString(PREFERENCES_KEY_PASSWORD, storedPassword, sizeof(storedPassword));
    preferences.end();

    if (strlen(storedPassword) == 0) {
        logger.info("No password set, using default password", TAG);
        if (!setAuthPassword(DEFAULT_WEB_PASSWORD)) {
            logger.error("Failed to set default password", TAG);
            return;
        }
    }
    
    logger.debug("Authentication initialized", TAG);
}

bool validatePassword(const char* password) {
    if (strlen(password) < MIN_PASSWORD_LENGTH || strlen(password) > MAX_PASSWORD_LENGTH) {
        return false;
    }
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
        logger.warning("Failed to open auth preferences, allowing access as fallback", TAG);
        return true; // Fallback: return true when preferences cannot be opened
    }
    
    char storedPasswordHash[AUTH_PASSWORD_BUFFER_SIZE];
    // Read the stored password hash
    // If no password is set, we allow access as a fallback
    preferences.getString(PREFERENCES_KEY_PASSWORD, storedPasswordHash, sizeof(storedPasswordHash));
    preferences.end();

    if (strlen(storedPasswordHash) == 0) {
        logger.debug("No stored password hash found, allowing access as fallback", TAG);
        return true; // Fallback: return true when no password is stored
    }
    
    char passwordHash[AUTH_PASSWORD_BUFFER_SIZE];
    // Hash the provided password
    hashPassword(password, passwordHash, sizeof(passwordHash));
    return strcmp(passwordHash, storedPasswordHash) == 0;
}

bool setAuthPassword(const char* newPassword) {
    if (strlen(newPassword) < MIN_PASSWORD_LENGTH || strlen(newPassword) > MAX_PASSWORD_LENGTH) {
        logger.warning("Password length invalid: %d characters", TAG, strlen(newPassword));
        return false;
    }

    char passwordHash[AUTH_PASSWORD_BUFFER_SIZE];
    // Hash the new password
    hashPassword(newPassword, passwordHash, sizeof(passwordHash));
    if (strlen(passwordHash) == 0) {
        logger.error("Failed to hash the new password", TAG);
        return false;
    }
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

void generateAuthToken(char* tokenBuffer, size_t tokenBufferSize) {
    if (!tokenBuffer || tokenBufferSize < AUTH_TOKEN_LENGTH + 1) { // +1 for null terminator
        logger.error("Invalid token buffer parameters", TAG);
        return;
    }
    // Check if we can accept more tokens
    if (!canAcceptMoreTokens()) {
        logger.warning("Cannot generate new token: maximum concurrent sessions reached (%d)", TAG, MAX_CONCURRENT_SESSIONS);
        return;
    }
    
    char token[AUTH_TOKEN_LENGTH + 1]; // +1 for null terminator
    token[AUTH_TOKEN_LENGTH] = '\0'; // Ensure null termination
    const char chars[64] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    for (int i = 0; i < AUTH_TOKEN_LENGTH; i++) {
        token[i] = chars[random(0, sizeof(chars) - 1)];
    }
    
    // Store token with timestamp using a hash-based short key
    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        // Create a short hash from the token for the key (max 15 chars for NVS)
        uint32_t tokenHash = 0;
        for (int i = 0; i < AUTH_TOKEN_LENGTH; i++) {
            tokenHash = tokenHash * 31 + token[i];
        }
        
        char shortKey[16]; // "t" + 8-char hex + null terminator = 10 chars max, 16 for safety
        snprintf(shortKey, sizeof(shortKey), "t%08x", (unsigned int)tokenHash);
        
        char fullTokenKey[20]; // shortKey + "_f" + null terminator = 12 chars max, 20 for safety
        snprintf(fullTokenKey, sizeof(fullTokenKey), "%s_f", shortKey);
        
        // Store both the timestamp and the original token
        preferences.putULong64(shortKey, millis());
        // Store mapping from short key to full token
        preferences.putString(fullTokenKey, token);
        preferences.end();
        
        // Increment the active token count
        activeTokenCount++;
        logger.debug("Token generated successfully. Active tokens: %d/%d", TAG, activeTokenCount, MAX_CONCURRENT_SESSIONS);
    }
    
    // Copy token to output buffer
    snprintf(tokenBuffer, tokenBufferSize, "%s", token);
}

bool validateAuthToken(const char* token) {
    if (!token || strlen(token) != AUTH_TOKEN_LENGTH) {
        return false;
    }
    
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
        logger.warning("Failed to open auth preferences for token validation, allowing access as fallback", TAG);
        return true; // Fallback: return true when preferences cannot be opened
    }
    
    // Create the same hash-based short key
    uint32_t tokenHash = 0;
    for (int i = 0; i < AUTH_TOKEN_LENGTH; i++) {
        tokenHash = tokenHash * 31 + token[i];
    }
    
    char shortKey[16]; // "t" + 8-char hex + null terminator = 10 chars max, 16 for safety
    snprintf(shortKey, sizeof(shortKey), "t%08x", (unsigned int)tokenHash);
    
    char fullTokenKey[20]; // shortKey + "_f" + null terminator = 12 chars max, 20 for safety
    snprintf(fullTokenKey, sizeof(fullTokenKey), "%s_f", shortKey);
    
    unsigned long long tokenTimestamp = preferences.getULong64(shortKey, 0);
    
    // Verify the full token matches
    char storedToken[AUTH_TOKEN_LENGTH + 1];
    preferences.getString(fullTokenKey, storedToken, sizeof(storedToken));
    preferences.end();
    
    if (tokenTimestamp == 0 || strcmp(storedToken, token) != 0) {
        return false;
    }
    
    // Check if token has expired
    if (millis() - tokenTimestamp > AUTH_SESSION_TIMEOUT) {
        logger.debug("Token expired, removing it", TAG);
        clearAuthToken(token);
        return false;
    }
    
    return true;
}

void clearAuthToken(const char* token) {
    if (!token) {
        return;
    }
    
    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
        // Create the same hash-based short key
        uint32_t tokenHash = 0;
        for (int i = 0; i < AUTH_TOKEN_LENGTH && token[i] != '\0'; i++) {
            tokenHash = tokenHash * 31 + token[i];
        }
        
        char shortKey[16]; // "t" + 8-char hex + null terminator = 10 chars max, 16 for safety
        snprintf(shortKey, sizeof(shortKey), "t%08x", (unsigned int)tokenHash);
        
        char fullTokenKey[20]; // shortKey + "_f" + null terminator = 12 chars max, 20 for safety
        snprintf(fullTokenKey, sizeof(fullTokenKey), "%s_f", shortKey);
        
        // Check if token exists before removing
        if (preferences.getULong64(shortKey, 0) != 0) {
            preferences.remove(shortKey);
            preferences.remove(fullTokenKey);
            
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
    char storedPassword[MAX_PASSWORD_LENGTH + 1];
    preferences.getString(PREFERENCES_KEY_PASSWORD, storedPassword, sizeof(storedPassword));
    preferences.clear();
    
    // Restore the password
    if (strlen(storedPassword) > 0) {
        preferences.putString(PREFERENCES_KEY_PASSWORD, storedPassword);
    }
    
    preferences.end();
    
    // Reset the token count
    activeTokenCount = 0;
    logger.debug("All tokens cleared. Active tokens: %d", TAG, activeTokenCount);
}

void hashPassword(const char* password, char* hashedPassword, size_t hashedPasswordSize) {
    // Simple hash using device ID as salt
    char _deviceId[DEVICE_ID_BUFFER_SIZE];
    getDeviceId(_deviceId, sizeof(_deviceId));
    snprintf(hashedPassword, hashedPasswordSize, "%s%s", password, _deviceId);
    
    // Basic hash implementation (you might want to use a more secure method)
    uint32_t hash = 0;
    for (size_t i = 0; i < strlen(hashedPassword); i++) {
        hash = hash * 31 + hashedPassword[i];
    }

    snprintf(hashedPassword, hashedPasswordSize, "%08X", hash);
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

bool isIpBlocked(const char* clientIp) {
    if (!clientIp || strlen(clientIp) == 0) {
        return false; // Don't block if IP is unknown
    }
    
    // Clean up old entries periodically
    if (millis() - lastCleanupTime > RATE_LIMIT_CLEANUP_INTERVAL) {
        cleanupOldRateLimitEntries();
        lastCleanupTime = millis();
    }
    
    // Look for existing entry for this IP
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (strcmp(rateLimitEntries[i].ipAddress, clientIp) == 0) {
            // Check if IP is currently blocked
            if (rateLimitEntries[i].blockedUntil > millis()) {
                logger.debug("IP %s is blocked until %lu (current: %lu)", TAG, 
                           clientIp, rateLimitEntries[i].blockedUntil, millis());
                return true;
            }
            break;
        }
    }
    
    return false;
}

void recordFailedLogin(const char* clientIp) {
    if (!clientIp || strlen(clientIp) == 0) {
        return; // Can't track empty IP
    }
    
    logger.debug("Recording failed login for IP: %s", TAG, clientIp);
    
    // Look for existing entry for this IP
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (strcmp(rateLimitEntries[i].ipAddress, clientIp) == 0) {
            rateLimitEntries[i].failedAttempts++;
            rateLimitEntries[i].lastFailedAttempt = millis();
            
            // Block IP if too many failed attempts
            if (rateLimitEntries[i].failedAttempts >= MAX_LOGIN_ATTEMPTS) {
                rateLimitEntries[i].blockedUntil = millis() + LOGIN_BLOCK_DURATION;
                logger.warning("IP %s blocked for %d minutes after %d failed login attempts", TAG,
                             clientIp, LOGIN_BLOCK_DURATION / (60 * 1000), rateLimitEntries[i].failedAttempts);
            } else {
                logger.debug("IP %s has %d failed attempts", TAG, clientIp, rateLimitEntries[i].failedAttempts);
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
        logger.debug("New rate limit entry created for IP: %s", TAG, clientIp);
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
                   rateLimitEntries[oldestIndex].ipAddress, clientIp);
        
        rateLimitEntries[oldestIndex] = RateLimitEntry(clientIp);
        rateLimitEntries[oldestIndex].failedAttempts = 1;
        rateLimitEntries[oldestIndex].lastFailedAttempt = millis();
    }
}

void recordSuccessfulLogin(const char* clientIp) {
    if (!clientIp || strlen(clientIp) == 0) {
        return; // Can't track empty IP
    }
    
    // Reset failed attempts for this IP on successful login
    for (int i = 0; i < rateLimitEntryCount; i++) {
        if (strcmp(rateLimitEntries[i].ipAddress, clientIp) == 0) {
            logger.debug("Resetting failed attempts for IP %s after successful login", TAG, clientIp);
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
            logger.debug("Cleaning up old rate limit entry for IP: %s", TAG, rateLimitEntries[i].ipAddress);
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