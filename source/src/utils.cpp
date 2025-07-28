#include "utils.h"
#include <Preferences.h>

static const char *TAG = "utils";

// New system info functions
void populateSystemStaticInfo(SystemStaticInfo& info) {
    logger.debug("Populating static system info...", TAG);

    // Initialize the struct to ensure clean state
    memset(&info, 0, sizeof(info));

    // Product info
    snprintf(info.companyName, sizeof(info.companyName), "%s", COMPANY_NAME);
    snprintf(info.productName, sizeof(info.productName), "%s", PRODUCT_NAME);
    snprintf(info.fullProductName, sizeof(info.fullProductName), "%s", FULL_PRODUCT_NAME);
    snprintf(info.productDescription, sizeof(info.productDescription), "%s", PRODUCT_DESCRIPTION);
    snprintf(info.githubUrl, sizeof(info.githubUrl), "%s", GITHUB_URL);
    snprintf(info.author, sizeof(info.author), "%s", AUTHOR);
    snprintf(info.authorEmail, sizeof(info.authorEmail), "%s", AUTHOR_EMAIL);
    
    // Firmware info
    snprintf(info.buildVersion, sizeof(info.buildVersion), "%s", FIRMWARE_BUILD_VERSION);
    snprintf(info.buildDate, sizeof(info.buildDate), "%s", FIRMWARE_BUILD_DATE);
    snprintf(info.buildTime, sizeof(info.buildTime), "%s", FIRMWARE_BUILD_TIME);
    snprintf(info.sketchMD5, sizeof(info.sketchMD5), "%s", ESP.getSketchMD5().c_str());
    
    // Hardware info
    snprintf(info.chipModel, sizeof(info.chipModel), "%s", ESP.getChipModel());
    info.chipRevision = ESP.getChipRevision();
    info.chipCores = ESP.getChipCores();
    info.chipId = ESP.getEfuseMac();
    info.flashChipSizeBytes = ESP.getFlashChipSize();
    info.flashChipSpeedHz = ESP.getFlashChipSpeed();
    info.psramSizeBytes = ESP.getPsramSize();
    info.cpuFrequencyMHz = ESP.getCpuFreqMHz();
    
    // // Crash and reset monitoring
    info.crashCount = CrashMonitor::getCrashCount();
    info.resetCount = CrashMonitor::getResetCount();
    info.lastResetReason = (uint32_t)esp_reset_reason();
    snprintf(info.lastResetReasonString, sizeof(info.lastResetReasonString), "%s", CrashMonitor::getResetReasonString(esp_reset_reason()));
    info.lastResetWasCrash = CrashMonitor::isLastResetDueToCrash();
    
    // SDK info
    snprintf(info.sdkVersion, sizeof(info.sdkVersion), "%s", ESP.getSdkVersion());
    snprintf(info.coreVersion, sizeof(info.coreVersion), "%s", ESP.getCoreVersion());
    
    // Device ID
    getDeviceId(info.deviceId, sizeof(info.deviceId));

    logger.debug("Static system info populated", TAG);
}

void populateSystemDynamicInfo(SystemDynamicInfo& info) {
    logger.debug("Populating dynamic system info...", TAG);

    // Initialize the struct to ensure clean state
    memset(&info, 0, sizeof(info));

    // Time
    info.uptimeMilliseconds = millis();
    info.uptimeSeconds = info.uptimeMilliseconds / 1000;
    CustomTime::getTimestamp(info.currentTimestamp, sizeof(info.currentTimestamp));
    
    // Memory - Heap
    info.heapTotalBytes = ESP.getHeapSize();
    info.heapFreeBytes = ESP.getFreeHeap();
    info.heapUsedBytes = info.heapTotalBytes - info.heapFreeBytes;
    info.heapMinFreeBytes = ESP.getMinFreeHeap();
    info.heapMaxAllocBytes = ESP.getMaxAllocHeap();
    info.heapFreePercentage = info.heapTotalBytes > 0 ? ((float)info.heapFreeBytes / info.heapTotalBytes) * 100.0f : 0.0f;
    info.heapUsedPercentage = 100.0f - info.heapFreePercentage;
    
    // Memory - PSRAM
    uint32_t psramTotal = ESP.getPsramSize();
    if (psramTotal > 0) {
        info.psramFreeBytes = ESP.getFreePsram();
        info.psramUsedBytes = psramTotal - info.psramFreeBytes;
        info.psramMinFreeBytes = ESP.getMinFreePsram();
        info.psramMaxAllocBytes = ESP.getMaxAllocPsram();
        info.psramFreePercentage = ((float)info.psramFreeBytes / psramTotal) * 100.0f;
        info.psramUsedPercentage = 100.0f - info.psramFreePercentage;
    }
    
    // Storage
    info.spiffsTotalBytes = SPIFFS.totalBytes();
    info.spiffsUsedBytes = SPIFFS.usedBytes();
    info.spiffsFreeBytes = info.spiffsTotalBytes - info.spiffsUsedBytes;
    info.spiffsFreePercentage = info.spiffsTotalBytes > 0 ? ((float)info.spiffsFreeBytes / info.spiffsTotalBytes) * 100.0f : 0.0f;
    info.spiffsUsedPercentage = 100.0f - info.spiffsFreePercentage;
    
    // Performance
    info.temperatureCelsius = temperatureRead();
    
    // Network (if connected)
    if (WiFi.isConnected()) {
        info.wifiConnected = true;
        info.wifiRssi = WiFi.RSSI();
        snprintf(info.wifiSsid, sizeof(info.wifiSsid), "%s", WiFi.SSID().c_str());
        snprintf(info.wifiMacAddress, sizeof(info.wifiMacAddress), "%s", WiFi.macAddress().c_str());
        snprintf(info.wifiLocalIp, sizeof(info.wifiLocalIp), "%s", WiFi.localIP().toString().c_str());
        snprintf(info.wifiGatewayIp, sizeof(info.wifiGatewayIp), "%s", WiFi.gatewayIP().toString().c_str());
        snprintf(info.wifiSubnetMask, sizeof(info.wifiSubnetMask), "%s", WiFi.subnetMask().toString().c_str());
        snprintf(info.wifiDnsIp, sizeof(info.wifiDnsIp), "%s", WiFi.dnsIP().toString().c_str());
        snprintf(info.wifiBssid, sizeof(info.wifiBssid), "%s", WiFi.BSSIDstr().c_str());
    } else {
        info.wifiConnected = false;
        info.wifiRssi = -100; // Invalid RSSI
        snprintf(info.wifiSsid, sizeof(info.wifiSsid), "Not connected");
        snprintf(info.wifiMacAddress, sizeof(info.wifiMacAddress), "%s", WiFi.macAddress().c_str()); // MAC is available even when disconnected
        snprintf(info.wifiLocalIp, sizeof(info.wifiLocalIp), "0.0.0.0");
        snprintf(info.wifiGatewayIp, sizeof(info.wifiGatewayIp), "0.0.0.0");
        snprintf(info.wifiSubnetMask, sizeof(info.wifiSubnetMask), "0.0.0.0");
        snprintf(info.wifiDnsIp, sizeof(info.wifiDnsIp), "0.0.0.0");
        snprintf(info.wifiBssid, sizeof(info.wifiBssid), "00:00:00:00:00:00");
    }

    logger.debug("Dynamic system info populated", TAG);
}

void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument& doc) {
    logger.debug("Converting static system info to JSON...", TAG);

    // Product
    doc["product"]["companyName"] = info.companyName;
    doc["product"]["productName"] = info.productName;
    doc["product"]["fullProductName"] = info.fullProductName;
    doc["product"]["productDescription"] = info.productDescription;
    doc["product"]["githubUrl"] = info.githubUrl;
    doc["product"]["author"] = info.author;
    doc["product"]["authorEmail"] = info.authorEmail;
    
    // Firmware
    doc["firmware"]["buildVersion"] = info.buildVersion;
    doc["firmware"]["buildDate"] = info.buildDate;
    doc["firmware"]["buildTime"] = info.buildTime;
    doc["firmware"]["sketchMD5"] = info.sketchMD5;
    
    // Hardware
    doc["hardware"]["chipModel"] = info.chipModel;
    doc["hardware"]["chipRevision"] = info.chipRevision;
    doc["hardware"]["chipCores"] = info.chipCores;
    doc["hardware"]["chipId"] = (uint64_t)info.chipId;
    doc["hardware"]["cpuFrequencyMHz"] = info.cpuFrequencyMHz;
    doc["hardware"]["flashChipSizeBytes"] = info.flashChipSizeBytes;
    doc["hardware"]["flashChipSpeedHz"] = info.flashChipSpeedHz;
    doc["hardware"]["psramSizeBytes"] = info.psramSizeBytes;
    doc["hardware"]["cpuFrequencyMHz"] = info.cpuFrequencyMHz;

    // Crash monitoring
    doc["monitoring"]["crashCount"] = info.crashCount;
    doc["monitoring"]["resetCount"] = info.resetCount;
    doc["monitoring"]["lastResetReason"] = info.lastResetReason;
    doc["monitoring"]["lastResetReasonString"] = info.lastResetReasonString;
    doc["monitoring"]["lastResetWasCrash"] = info.lastResetWasCrash;
    
    // SDK
    doc["sdk"]["sdkVersion"] = info.sdkVersion;
    doc["sdk"]["coreVersion"] = info.coreVersion;
    
    // Device
    doc["device"]["id"] = info.deviceId;

    logger.debug("Static system info converted to JSON", TAG);
}

void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument& doc) {
    logger.debug("Converting dynamic system info to JSON...", TAG);

    // Time
    doc["time"]["uptimeMilliseconds"] = (uint64_t)info.uptimeMilliseconds;
    doc["time"]["uptimeSeconds"] = info.uptimeSeconds;
    doc["time"]["currentTimestamp"] = info.currentTimestamp;
    
    // Memory - Heap
    doc["memory"]["heap"]["totalBytes"] = info.heapTotalBytes;
    doc["memory"]["heap"]["freeBytes"] = info.heapFreeBytes;
    doc["memory"]["heap"]["usedBytes"] = info.heapUsedBytes;
    doc["memory"]["heap"]["minFreeBytes"] = info.heapMinFreeBytes;
    doc["memory"]["heap"]["maxAllocBytes"] = info.heapMaxAllocBytes;
    doc["memory"]["heap"]["freePercentage"] = info.heapFreePercentage;
    doc["memory"]["heap"]["usedPercentage"] = info.heapUsedPercentage;
    
    // Memory - PSRAM
    doc["memory"]["psram"]["freeBytes"] = info.psramFreeBytes;
    doc["memory"]["psram"]["usedBytes"] = info.psramUsedBytes;
    doc["memory"]["psram"]["minFreeBytes"] = info.psramMinFreeBytes;
    doc["memory"]["psram"]["maxAllocBytes"] = info.psramMaxAllocBytes;
    doc["memory"]["psram"]["freePercentage"] = info.psramFreePercentage;
    doc["memory"]["psram"]["usedPercentage"] = info.psramUsedPercentage;
    
    // Storage
    doc["storage"]["spiffs"]["totalBytes"] = info.spiffsTotalBytes;
    doc["storage"]["spiffs"]["usedBytes"] = info.spiffsUsedBytes;
    doc["storage"]["spiffs"]["freeBytes"] = info.spiffsFreeBytes;
    doc["storage"]["spiffs"]["freePercentage"] = info.spiffsFreePercentage;
    doc["storage"]["spiffs"]["usedPercentage"] = info.spiffsUsedPercentage;
    
    // Performance
    doc["performance"]["temperatureCelsius"] = info.temperatureCelsius;
    
    // Network
    doc["network"]["wifiConnected"] = info.wifiConnected;
    doc["network"]["wifiSsid"] = info.wifiSsid;
    doc["network"]["wifiMacAddress"] = info.wifiMacAddress;
    doc["network"]["wifiLocalIp"] = info.wifiLocalIp;
    doc["network"]["wifiGatewayIp"] = info.wifiGatewayIp;
    doc["network"]["wifiSubnetMask"] = info.wifiSubnetMask;
    doc["network"]["wifiDnsIp"] = info.wifiDnsIp;
    doc["network"]["wifiBssid"] = info.wifiBssid;
    doc["network"]["wifiRssi"] = info.wifiRssi;

    logger.debug("Dynamic system info converted to JSON", TAG);
}

void getJsonDeviceStaticInfo(JsonDocument& doc) {
    SystemStaticInfo info;
    populateSystemStaticInfo(info);
    systemStaticInfoToJson(info, doc);
}

void getJsonDeviceDynamicInfo(JsonDocument& doc) {
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);
    systemDynamicInfoToJson(info, doc);
}

bool safeSerializeJson(JsonDocument& jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError) {
    // Validate inputs
    if (!buffer || bufferSize == 0) {
        logger.warning("Invalid buffer parameters passed to safeSerializeJson", TAG);
        return false;
    }

    size_t _size = measureJson(jsonDocument);
    if (_size >= bufferSize) {
        if (truncateOnError) {
            // Truncate JSON to fit buffer
            serializeJson(jsonDocument, buffer, bufferSize);
            // Ensure null-termination (avoid weird last character issues)
            buffer[bufferSize - 1] = '\0';
            
            logger.debug("Truncating JSON to fit buffer size (%zu bytes vs %zu bytes)", TAG, bufferSize, _size);
        } else {
            logger.warning("JSON size (%zu bytes) exceeds buffer size (%zu bytes)", TAG, _size, bufferSize);
            snprintf(buffer, bufferSize, ""); // Clear buffer on failure
        }
        return false;
    }

    serializeJson(jsonDocument, buffer, bufferSize);
    logger.debug("JSON serialized successfully (bytes: %zu): %s", TAG, _size, buffer);
    return true;
}

// Legacy SPIFFS functions for backward compatibility during transition
bool deserializeJsonFromSpiffs(const char* path, JsonDocument& jsonDocument) {
    logger.debug("Deserializing JSON from SPIFFS", TAG);

    File _file = SPIFFS.open(path, FILE_READ);
    if (!_file){
        logger.error("Failed to open file %s", TAG, path);
        return false;
    }

    DeserializationError _error = deserializeJson(jsonDocument, _file);
    _file.close();
    if (_error){
        logger.error("Failed to deserialize file %s. Error: %s", TAG, path, _error.c_str());
        return false;
    }

    if (jsonDocument.isNull() || jsonDocument.size() == 0){
        logger.debug("JSON being deserialized is {}", TAG);
    }
    
    // For debugging purposes, serialize to a string and log it
    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, _jsonString, sizeof(_jsonString), true);
    logger.debug("JSON deserialized from SPIFFS correctly: %s", TAG, _jsonString);
    return true;
}

bool serializeJsonToSpiffs(const char* path, JsonDocument& jsonDocument){
    logger.debug("Serializing JSON to SPIFFS...", TAG);

    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file){
        logger.error("Failed to open file %s", TAG, path);
        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    if (jsonDocument.isNull() || jsonDocument.size() == 0){
        logger.debug("JSON being serialized is {}", TAG);
    }

    // For debugging purposes, serialize to a string and log it
    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, _jsonString, sizeof(_jsonString), true);
    logger.debug("JSON serialized to SPIFFS correctly: %s", TAG, _jsonString);

    return true;
}

void createEmptyJsonFile(const char* path) {
    logger.debug("Creating empty JSON file %s...", TAG, path);

    
    File _file = SPIFFS.open(path, FILE_WRITE);
    if (!_file) {
        logger.error("Failed to open file %s", TAG, path);
        return;
    }

    _file.print("{}");
    _file.close();

    logger.debug("Empty JSON file %s created", TAG, path);
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

    // Note: Energy data will be stored in SPIFFS/LittleFS for historical data
    // This function will be updated when we migrate to LittleFS
    serializeJsonToSpiffs(ENERGY_JSON_PATH, _jsonDocument);

    logger.debug("Default %s created", TAG, ENERGY_JSON_PATH);
}

void createDefaultDailyEnergyFile() {
    logger.debug("Creating default %s...", TAG, DAILY_ENERGY_JSON_PATH);

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);

    logger.debug("Default %s created", TAG, DAILY_ENERGY_JSON_PATH);
}

void createDefaultAde7953ConfigurationFile() {
    logger.debug("Creating default ADE7953 configuration...", TAG);

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

    logger.warning("Actually save this!!!!", TAG);

    logger.debug("Default ADE7953 configuration created", TAG);
}

void createDefaultCalibrationFile() {
    logger.debug("Creating default calibration...", TAG);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_calibration_json);

    // Note: Calibration data is configuration, not historical data
    // This will be handled by the ADE7953 module's own Preferences
    // For now, keep SPIFFS for backward compatibility
    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, _jsonDocument);

    logger.debug("Default calibration created", TAG);
}

void createDefaultChannelDataFile() { // TODO: remove this weird json to preferences stuff
    logger.debug("Creating default channel configuration...", TAG);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    logger.warning("Actually save this!!!!", TAG);

    logger.debug("Default channel configuration created", TAG);
}

void createDefaultCustomMqttConfigurationFile() {
    logger.debug("Creating default custom MQTT configuration...", TAG);

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

    logger.warning("Actually save this!!!!", TAG);

    logger.debug("Default custom MQTT configuration created", TAG);
}

std::vector<const char*> checkMissingFiles() {
    logger.debug("Checking missing files...", TAG);

    std::vector<const char*> missingFiles;
    
    const char* CONFIG_FILE_PATHS[] = {
        CONFIGURATION_ADE7953_JSON_PATH,
        CALIBRATION_JSON_PATH,
        CHANNEL_DATA_JSON_PATH,
        ENERGY_JSON_PATH,
        DAILY_ENERGY_JSON_PATH,
    };

    const size_t CONFIG_FILE_COUNT = sizeof(CONFIG_FILE_PATHS) / sizeof(CONFIG_FILE_PATHS[0]);

    
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

    
    for (const char* path : missingFiles) {
        if (strcmp(path, CONFIGURATION_ADE7953_JSON_PATH) == 0) {
            createDefaultAde7953ConfigurationFile();
        } else if (strcmp(path, CALIBRATION_JSON_PATH) == 0) {
            createDefaultCalibrationFile();
        } else if (strcmp(path, CHANNEL_DATA_JSON_PATH) == 0) {
            createDefaultChannelDataFile();
        } else if (strcmp(path, ENERGY_JSON_PATH) == 0) {
            createDefaultEnergyFile();
        } else if (strcmp(path, DAILY_ENERGY_JSON_PATH) == 0) {
            createDefaultDailyEnergyFile();
        } else {
            // Handle other files if needed
            logger.warning("No default creation function for path: %s", TAG, path);
        }
    }

    logger.debug("Default files created for missing files", TAG);
}

bool checkAllFiles() {
    logger.debug("Checking all files...", TAG);

    
    std::vector<const char*> missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        createDefaultFilesForMissingFiles(missingFiles);
        return true;
    }

    logger.debug("All files checked", TAG);
    return false;
}

// Task function that handles periodic maintenance checks
void maintenanceTask(void* parameter) {
    logger.debug("Maintenance task started", TAG);
    
    while (true) {
        // Wait for the maintenance check interval
        vTaskDelay(pdMS_TO_TICKS(MAINTENANCE_CHECK_INTERVAL));
        
        logger.debug("Running maintenance checks...", TAG);
        
        // Update and print statistics
        updateStatistics();
        printStatistics();
        printDeviceStatusDynamic();

        // Check heap memory
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) {
            logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
            setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
        }

        // Check SPIFFS memory and clear log if needed
        if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
            logger.clearLog();
            logger.warning("Log cleared due to low memory", TAG);
        }
        
        logger.debug("Maintenance checks completed", TAG);
    }

    vTaskDelete(NULL);
}

void startMaintenanceTask() {
    logger.debug("Starting maintenance task...", TAG);
    
    TaskHandle_t maintenanceTaskHandle = NULL;
    BaseType_t result = xTaskCreate(
        maintenanceTask,              // Task function
        TASK_MAINTENANCE_NAME,        // Task name
        TASK_MAINTENANCE_STACK_SIZE,  // Stack size (words)
        NULL,                         // Parameter passed to task
        TASK_MAINTENANCE_PRIORITY,    // Priority
        &maintenanceTaskHandle        // Task handle
    );
    
    if (result != pdPASS) {
        logger.error("Failed to create maintenance task", TAG);
    } else {
        logger.info("Maintenance task created successfully", TAG);
    }
}

// Task function that handles delayed restart
void restartTask(void* parameter) {
    logger.debug("Restart task started, waiting %d ms before restart", TAG, ESP32_RESTART_DELAY);
    
    // Wait for the specified delay
    vTaskDelay(pdMS_TO_TICKS(ESP32_RESTART_DELAY));
    
    // Execute the restart
    restartEsp32();
    
    // Task should never reach here, but clean up just in case
    vTaskDelete(NULL);
}

void setRestartEsp32(const char* functionName, const char* reason) { 
    logger.info("Restart required from function %s. Reason: %s", TAG, functionName, reason);
    
    // Store restart information for logging purposes
    restartConfiguration.isRequired = true;
    restartConfiguration.requiredAt = millis();
    snprintf(restartConfiguration.functionName, sizeof(restartConfiguration.functionName), "%s", functionName);
    snprintf(restartConfiguration.reason, sizeof(restartConfiguration.reason), "%s", reason);

    //TODO: we should start stopping tasks here

    logger.debug("Creating restart task. Function: %s, Reason: %s", TAG, restartConfiguration.functionName, restartConfiguration.reason);
    
    // Create a task that will handle the delayed restart
    TaskHandle_t restartTaskHandle = NULL;
    BaseType_t result = xTaskCreate(
        restartTask,
        TASK_RESTART_NAME,
        TASK_RESTART_STACK_SIZE,
        NULL,
        TASK_RESTART_PRIORITY,
        &restartTaskHandle
    );
    
    if (result != pdPASS) {
        logger.error("Failed to create restart task, performing immediate restart", TAG);
        restartEsp32();
    } else {
        logger.debug("Restart task created successfully", TAG);
    }
}

void restartEsp32() {
    Led::block();
    Led::setBrightness(max(Led::getBrightness(), 1)); // Show a faint light even if it is off
    Led::setWhite(true);

    logger.info("Restarting ESP32 from function %s. Reason: %s", TAG, restartConfiguration.functionName, restartConfiguration.reason);
    logger.end();

    // Give time for AsyncTCP connections to close gracefully
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Disable WiFi to prevent AsyncTCP crashes during restart
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Additional delay to ensure clean shutdown
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP.restart();
}

// Convenience methods for SystemInfo struct
void SystemInfo::updateStatic() {
    populateSystemStaticInfo(static_info);
}

void SystemInfo::updateDynamic() {
    populateSystemDynamicInfo(dynamic_info);
}

// Legacy compatibility function
void populateSystemInfo(SystemInfo& systemInfo) {
    logger.debug("Populating system info (legacy)...", TAG);
    
    // Use the new methods to populate both static and dynamic info
    systemInfo.updateAll();

    logger.debug("System info populated (legacy)", TAG);
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

void printDeviceStatusStatic()
{
    SystemStaticInfo info;
    populateSystemStaticInfo(info);

    logger.debug("--- Static System Info ---", TAG);
    logger.debug("Product: %s (%s)", TAG, info.fullProductName, info.productName);
    logger.debug("Company: %s | Author: %s", TAG, info.companyName, info.author);
    logger.debug("Firmware: %s | Build: %s %s", TAG, info.buildVersion, info.buildDate, info.buildTime);
    logger.debug("Sketch MD5: %s", TAG, info.sketchMD5);
    logger.debug("Flash: %lu bytes, %lu Hz", TAG, info.flashChipSizeBytes, info.flashChipSpeedHz);
    logger.debug("PSRAM: %lu bytes", TAG, info.psramSizeBytes);
    logger.debug("Chip: %s, rev %u, cores %u, id 0x%llx, CPU: %lu MHz", TAG, info.chipModel, info.chipRevision, info.chipCores, info.chipId, info.cpuFrequencyMHz);
    logger.debug("SDK: %s | Core: %s", TAG, info.sdkVersion, info.coreVersion);
    logger.debug("Device ID: %s", TAG, info.deviceId);
    logger.debug("Monitoring: %lu crashes, %lu resets | Last reset: %s", TAG, info.crashCount, info.resetCount, info.lastResetReasonString);

    logger.debug("------------------------", TAG);
}

void printDeviceStatusDynamic()
{
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);

    logger.debug("--- Dynamic System Info ---", TAG);
    logger.debug("Uptime: %lu s (%llu ms)", TAG, info.uptimeSeconds, (unsigned long long)info.uptimeMilliseconds);
    logger.debug("Timestamp: %s", TAG, info.currentTimestamp);
    logger.debug("Heap: %lu total, %lu free (%.2f%%), %lu used (%.2f%%), %lu min free, %lu max alloc", 
        TAG, 
        info.heapTotalBytes, 
        info.heapFreeBytes, info.heapFreePercentage, 
        info.heapUsedBytes, info.heapUsedPercentage, 
        info.heapMinFreeBytes, info.heapMaxAllocBytes
    );
    if (info.psramFreeBytes > 0 || info.psramUsedBytes > 0) {
        logger.debug("PSRAM: %lu free (%.2f%%), %lu used (%.2f%%), %lu min free, %lu max alloc", 
            TAG, 
            info.psramFreeBytes, info.psramFreePercentage, 
            info.psramUsedBytes, info.psramUsedPercentage, 
            info.psramMinFreeBytes, info.psramMaxAllocBytes
        );
    }
    logger.debug("SPIFFS: %lu total, %lu used (%.2f%%), %lu free (%.2f%%)", 
        TAG, 
        info.spiffsTotalBytes, 
        info.spiffsUsedBytes, info.spiffsUsedPercentage, 
        info.spiffsFreeBytes, info.spiffsFreePercentage
    );
    logger.debug("Temperature: %.2f C", TAG, info.temperatureCelsius);
    
    // WiFi information
    if (info.wifiConnected) {
        logger.debug("WiFi: Connected to '%s' (BSSID: %s)", TAG, info.wifiSsid, info.wifiBssid);
        logger.debug("WiFi: RSSI %d dBm | MAC %s", TAG, info.wifiRssi, info.wifiMacAddress);
        logger.debug("WiFi: IP %s | Gateway %s | DNS %s | Subnet %s", TAG, info.wifiLocalIp, info.wifiGatewayIp, info.wifiDnsIp, info.wifiSubnetMask);
    } else {
        logger.debug("WiFi: Disconnected | MAC %s", TAG, info.wifiMacAddress);
    }

    logger.debug("-------------------------", TAG);
}

void printDeviceStatus()
{
    printDeviceStatusStatic();
    printDeviceStatusDynamic();
}

void updateStatistics() {
    logger.debug("Updating statistics...", TAG);

    // The only statistic which is (currently) updated manually here is the log count
    statistics.logVerbose = logger.getVerboseCount();
    statistics.logDebug = logger.getDebugCount();
    statistics.logInfo = logger.getInfoCount();
    statistics.logWarning = logger.getWarningCount();
    statistics.logError = logger.getErrorCount();
    statistics.logFatal = logger.getFatalCount();

    logger.debug("Statistics updated", TAG);
}

void printStatistics() {
    logger.info("--- Statistics ---", TAG);
    logger.debug("Statistics - ADE7953: %ld total interrupts | %ld handled interrupts | %ld readings | %ld reading failures", 
        TAG, 
        statistics.ade7953TotalInterrupts, 
        statistics.ade7953TotalHandledInterrupts, 
        statistics.ade7953ReadingCount, 
        statistics.ade7953ReadingCountFailure
    );

    logger.debug("Statistics - MQTT: %ld messages published | %ld errors", 
        TAG, 
        statistics.mqttMessagesPublished, 
        statistics.mqttMessagesPublishedError
    );

    logger.debug("Statistics - Custom MQTT: %ld messages published | %ld errors", 
        TAG, 
        statistics.customMqttMessagesPublished, 
        statistics.customMqttMessagesPublishedError
    );

    logger.debug("Statistics - Modbus: %ld requests | %ld errors", 
        TAG, 
        statistics.modbusRequests, 
        statistics.modbusRequestsError
    );

    logger.debug("Statistics - InfluxDB: %ld uploads | %ld errors", 
        TAG, 
        statistics.influxdbUploadCount, 
        statistics.influxdbUploadCountError
    );

    logger.debug("Statistics - WiFi: %ld connections | %ld errors", 
        TAG, 
        statistics.wifiConnection, 
        statistics.wifiConnectionError
    );

    logger.debug("Statistics - Log: %ld verbose | %ld debug | %ld info | %ld warning | %ld error | %ld fatal", 
        TAG, 
        statistics.logVerbose, 
        statistics.logDebug, 
        statistics.logInfo, 
        statistics.logWarning, 
        statistics.logError, 
        statistics.logFatal
    );
    logger.info("-------------------", TAG);
}

void systemInfoToJson(JsonDocument& jsonDocument) {
    logger.debug("Converting system info to JSON (legacy)...", TAG);

    SystemInfo info;
    populateSystemInfo(info);

    // Combine static and dynamic info for legacy compatibility
    systemStaticInfoToJson(info.static_info, jsonDocument);
    
    JsonDocument dynamicDoc;
    systemDynamicInfoToJson(info.dynamic_info, dynamicDoc);
    
    // Merge dynamic info into the main document
    for (JsonPair kv : dynamicDoc.as<JsonObject>()) {
        jsonDocument[kv.key()] = kv.value();
    }

    logger.debug("System info converted to JSON (legacy)", TAG);
}

void statisticsToJson(Statistics& statistics, JsonDocument& jsonDocument) {
    logger.debug("Converting statistics to JSON...", TAG);

    // ADE7953 statistics
    jsonDocument["ade7953"]["totalInterrupts"] = statistics.ade7953TotalInterrupts;
    jsonDocument["ade7953"]["totalHandledInterrupts"] = statistics.ade7953TotalHandledInterrupts;
    jsonDocument["ade7953"]["readingCount"] = statistics.ade7953ReadingCount;
    jsonDocument["ade7953"]["readingCountFailure"] = statistics.ade7953ReadingCountFailure;

    // MQTT statistics
    jsonDocument["mqtt"]["messagesPublished"] = statistics.mqttMessagesPublished;
    jsonDocument["mqtt"]["messagesPublishedError"] = statistics.mqttMessagesPublishedError;

    // Custom MQTT statistics
    jsonDocument["customMqtt"]["messagesPublished"] = statistics.customMqttMessagesPublished;
    jsonDocument["customMqtt"]["messagesPublishedError"] = statistics.customMqttMessagesPublishedError;

    // Modbus statistics
    jsonDocument["modbus"]["requests"] = statistics.modbusRequests;
    jsonDocument["modbus"]["requestsError"] = statistics.modbusRequestsError;

    // InfluxDB statistics
    jsonDocument["influxdb"]["uploadCount"] = statistics.influxdbUploadCount;
    jsonDocument["influxdb"]["uploadCountError"] = statistics.influxdbUploadCountError;

    // WiFi statistics
    jsonDocument["wifi"]["connection"] = statistics.wifiConnection;
    jsonDocument["wifi"]["connectionError"] = statistics.wifiConnectionError;

    // Log statistics
    jsonDocument["log"]["verbose"] = statistics.logVerbose;
    jsonDocument["log"]["debug"] = statistics.logDebug;
    jsonDocument["log"]["info"] = statistics.logInfo;
    jsonDocument["log"]["warning"] = statistics.logWarning;
    jsonDocument["log"]["error"] = statistics.logError;
    jsonDocument["log"]["fatal"] = statistics.logFatal;

    logger.debug("Statistics converted to JSON", TAG);
}

// Helper functions
// -----------------------------

bool getPublicLocation(PublicLocation* publicLocation) {
    if (!publicLocation) {
        logger.error("Null pointer passed to getPublicLocation", TAG);
        return false;
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
                return false;
            }
            
            // Validate API response
            if (_jsonDocument["status"] != "success") {
                logger.error("API returned error status: %s", TAG, 
                           _jsonDocument["status"].as<const char*>());
                _http.end();
                return false;
            }

            // Extract strings safely using const char* and copy to char arrays
            const char* country = _jsonDocument["country"].as<const char*>();
            const char* city = _jsonDocument["city"].as<const char*>();
            float latitude = _jsonDocument["lat"].as<float>();
            float longitude = _jsonDocument["lon"].as<float>();

            // Extract strings safely - use empty string if NULL
            snprintf(publicLocation->country, sizeof(publicLocation->country), "%s", country ? country : "");
            snprintf(publicLocation->city, sizeof(publicLocation->city), "%s", city ? city : "");
            publicLocation->latitude = latitude;
            publicLocation->longitude = longitude;

            logger.debug(
                "Location: %s, %s | Lat: %f | Lon: %f",
                TAG,
                publicLocation->country,
                publicLocation->city,
                publicLocation->latitude,
                publicLocation->longitude
            );
        } else {
            logger.warning("HTTP request failed with code: %d", TAG, httpCode);
            return false;
        }
    } else {
        logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
        return false;
    }

    _http.end();
    return true;
}

bool getPublicTimezone(int* gmtOffset, int* dstOffset) {
    if (!gmtOffset || !dstOffset) {
        logger.error("Null pointer passed to getPublicTimezone", TAG);
        return false;
    }

    PublicLocation _publicLocation;
    if (!getPublicLocation(&_publicLocation)) {
        logger.warning("Could not retrieve the public location. Skipping timezone fetching", TAG);
        return false;
    }

    HTTPClient _http;
    JsonDocument _jsonDocument;

    char _url[URL_BUFFER_SIZE];
    // The URL for the timezone API endpoint requires passing as params the latitude, longitude and username (which is a sort of free "public" api key)
    snprintf(_url, sizeof(_url), "%slat=%f&lng=%f&username=%s",
        PUBLIC_TIMEZONE_ENDPOINT,
        _publicLocation.latitude,
        _publicLocation.longitude,
        PUBLIC_TIMEZONE_USERNAME);

    _http.begin(_url);
    int httpCode = _http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            // Example JSON response:
            // {"sunrise":"2025-07-27 05:55","lng":14.168099,"countryCode":"IT","gmtOffset":1,"rawOffset":1,"sunset":"2025-07-27 20:24","timezoneId":"Europe/Rome","dstOffset":2,"countryName":"Italy","time":"2025-07-27 20:53","lat":40.813198}
            DeserializationError error = deserializeJson(_jsonDocument, _http.getString()); // Unfortunately, the stream method returns null so we have to use string
            
            if (error) {
                logger.error("JSON parsing failed: %s", TAG, error.c_str());
                _http.end();
                return false;
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
            return false;
        }
    } else {
        logger.error("HTTP request error: %s", TAG, _http.errorToString(httpCode).c_str());
        return false;
    }

    _http.end();
    return true;
}

void factoryReset() { 
    logger.fatal("Factory reset requested", TAG);

    Led::block();
    Led::setBrightness(max(Led::getBrightness(), 1)); // Show a faint light even if it is off
    Led::setRed(true);

    clearAllPreferences();

    logger.fatal("Formatting SPIFFS. This will take some time.", TAG);
    SPIFFS.format();

    // Directly call ESP.restart() so that a fresh start is done
    ESP.restart();
}

void clearAllPreferences() {
    logger.fatal("Clear all preferences requested", TAG);

    Preferences preferences;
    preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false); // false = read-write mode
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CALIBRATION, false); // false = read-write mode
    preferences.clear();
    preferences.end();
    
    preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false); // false = read-write mode
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_MQTT, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_BUTTON, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_WIFI, false); // false = read-write mode
    preferences.clear();
    preferences.end();
    
    preferences.begin(PREFERENCES_NAMESPACE_TIME, false); // false = read-write mode
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_LED, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_FIRMWARE_UPDATES, false);
    preferences.clear();
    preferences.end();
}

bool isLatestFirmwareInstalled() {
    JsonDocument _jsonDocument;
    // deserializeJsonFromSpiffs(FW_UPDATE_INFO_JSON_PATH, _jsonDocument);
    // TODO: switch to Preferences
    logger.warning("IMPLEMENT THIS: deserializeJsonFromSpiffs for FW_UPDATE_INFO_JSON_PATH", TAG);
    return true; // For now, return true as we don't have the implementation yet
    
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
    CustomTime::getTimestamp(_timestampBuffer, sizeof(_timestampBuffer)); // TODO: maybe everything should be returned in unix so it is UTC, and then converted on the other side? or standard iso utc timestamp?
    _jsonDocument["timestamp"] = _timestampBuffer;

    // Note: Firmware status is temporary data, keep in SPIFFS for now
    // This will be updated when we migrate to LittleFS for temporary data
    // serializeJsonToSpiffs(FW_UPDATE_STATUS_JSON_PATH, _jsonDocument);
    logger.warning("IMPLEMENT THIS: updateJsonFirmwareStatus", TAG); // TODO: switch to Preferences
}

void getDeviceId(char* deviceId, size_t maxLength) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
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
        case -4: return "MQTT_CONNECTION_TIMEOUT";
        case -3: return "MQTT_CONNECTION_LOST";
        case -2: return "MQTT_CONNECT_FAILED";
        case -1: return "MQTT_DISCONNECTED";
        case 0: return "MQTT_CONNECTED";
        case 1: return "MQTT_CONNECT_BAD_PROTOCOL";
        case 2: return "MQTT_CONNECT_BAD_CLIENT_ID";
        case 3: return "MQTT_CONNECT_UNAVAILABLE";
        case 4: return "MQTT_CONNECT_BAD_CREDENTIALS";
        case 5: return "MQTT_CONNECT_UNAUTHORIZED";
        default: return "Unknown MQTT state";
    }
}

void decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize) {
    // Early validation and consistent error handling
    if (!encryptedData || !key || !decryptedData || decryptedDataSize == 0) {
        if (decryptedData && decryptedDataSize > 0) {
            decryptedData[0] = '\0';
        }
        return;
    }

    size_t inputLength = strlen(encryptedData);
    if (inputLength == 0) {
        decryptedData[0] = '\0';
        return;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, (const unsigned char *)key, KEY_SIZE);

    // Use stack-allocated buffers instead of malloc
    unsigned char decodedData[DECRYPTION_WORKING_BUFFER_SIZE];
    unsigned char output[DECRYPTION_WORKING_BUFFER_SIZE];

    size_t decodedLength = 0;
    int ret = mbedtls_base64_decode(decodedData, sizeof(decodedData), &decodedLength,
                                   (const unsigned char *)encryptedData, inputLength);
    
    if (ret != 0 || decodedLength == 0) {
        decryptedData[0] = '\0';
        mbedtls_aes_free(&aes);
        return;
    }
    
    // Decrypt in 16-byte blocks
    for (size_t i = 0; i < decodedLength; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, &decodedData[i], &output[i]);
    }
    
    // Handle PKCS7 padding removal
    uint8_t paddingLength = output[decodedLength - 1];
    if (paddingLength > 0 && paddingLength <= 16 && paddingLength < decodedLength) {
        output[decodedLength - paddingLength] = '\0';
    } else {
        output[decodedLength - 1] = '\0'; // Fallback: just ensure null termination
    }
    
    // Copy result safely
    snprintf(decryptedData, decryptedDataSize, "%s", (char*)output);

    // Clear sensitive data from stack buffers for security
    memset(decodedData, 0, sizeof(decodedData));
    memset(output, 0, sizeof(output));

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

    char _encryptionKey[ENCRYPTION_KEY_BUFFER_SIZE];
    snprintf(_encryptionKey, ENCRYPTION_KEY_BUFFER_SIZE, "%s%s", preshared_encryption_key, DEVICE_ID);

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

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds) {
    if (isMilliseconds) {
        return (unixTime >= MINIMUM_UNIX_TIME_MILLISECONDS && unixTime <= MAXIMUM_UNIX_TIME_MILLISECONDS);
    } else {
        return (unixTime >= MINIMUM_UNIX_TIME && unixTime <= MAXIMUM_UNIX_TIME);
    }
}