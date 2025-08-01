#include "utils.h"

static const char *TAG = "utils";

static TaskHandle_t _restartTaskHandle = NULL;
static TaskHandle_t _maintenanceTaskHandle = NULL;
static bool _maintenanceTaskShouldRun = false;


// New system info functions
void populateSystemStaticInfo(SystemStaticInfo& info) {
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
    const esp_partition_t *running = esp_ota_get_running_partition();
    snprintf(info.partitionAppName, sizeof(info.partitionAppName), "%s", running->label);
    
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
    info.lastResetReason = (unsigned long)esp_reset_reason();
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
    // Initialize the struct to ensure clean state
    memset(&info, 0, sizeof(info));

    // Time
    info.uptimeMilliseconds = millis64();
    info.uptimeSeconds = info.uptimeMilliseconds / 1000;
    CustomTime::getTimestamp(info.currentTimestamp, sizeof(info.currentTimestamp));
    CustomTime::getTimestampIso(info.currentTimestampIso, sizeof(info.currentTimestampIso));

    // Memory - Heap
    info.heapTotalBytes = ESP.getHeapSize();
    info.heapFreeBytes = ESP.getFreeHeap();
    info.heapUsedBytes = info.heapTotalBytes - info.heapFreeBytes;
    info.heapMinFreeBytes = ESP.getMinFreeHeap();
    info.heapMaxAllocBytes = ESP.getMaxAllocHeap();
    info.heapFreePercentage = info.heapTotalBytes > 0 ? ((float)info.heapFreeBytes / info.heapTotalBytes) * 100.0f : 0.0f;
    info.heapUsedPercentage = 100.0f - info.heapFreePercentage;
    
    // Memory - PSRAM
    unsigned long psramTotal = ESP.getPsramSize();
    if (psramTotal > 0) {
        info.psramFreeBytes = ESP.getFreePsram();
        info.psramUsedBytes = psramTotal - info.psramFreeBytes;
        info.psramMinFreeBytes = ESP.getMinFreePsram();
        info.psramMaxAllocBytes = ESP.getMaxAllocPsram();
        info.psramFreePercentage = ((float)info.psramFreeBytes / psramTotal) * 100.0f;
        info.psramUsedPercentage = 100.0f - info.psramFreePercentage;
    } else {
        info.psramFreeBytes = 0;
        info.psramUsedBytes = 0;
        info.psramMinFreeBytes = 0;
        info.psramMaxAllocBytes = 0;
        info.psramFreePercentage = 0.0f;
        info.psramUsedPercentage = 0.0f;
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
    if (CustomWifi::isFullyConnected()) {
        info.wifiConnected = true;
        info.wifiRssi = WiFi.RSSI();
        snprintf(info.wifiSsid, sizeof(info.wifiSsid), "%s", WiFi.SSID().c_str());
        snprintf(info.wifiLocalIp, sizeof(info.wifiLocalIp), "%s", WiFi.localIP().toString().c_str());
        snprintf(info.wifiGatewayIp, sizeof(info.wifiGatewayIp), "%s", WiFi.gatewayIP().toString().c_str());
        snprintf(info.wifiSubnetMask, sizeof(info.wifiSubnetMask), "%s", WiFi.subnetMask().toString().c_str());
        snprintf(info.wifiDnsIp, sizeof(info.wifiDnsIp), "%s", WiFi.dnsIP().toString().c_str());
        snprintf(info.wifiBssid, sizeof(info.wifiBssid), "%s", WiFi.BSSIDstr().c_str());
    } else {
        info.wifiConnected = false;
        info.wifiRssi = -100; // Invalid RSSI
        snprintf(info.wifiSsid, sizeof(info.wifiSsid), "Not connected");
        snprintf(info.wifiLocalIp, sizeof(info.wifiLocalIp), "0.0.0.0");
        snprintf(info.wifiGatewayIp, sizeof(info.wifiGatewayIp), "0.0.0.0");
        snprintf(info.wifiSubnetMask, sizeof(info.wifiSubnetMask), "0.0.0.0");
        snprintf(info.wifiDnsIp, sizeof(info.wifiDnsIp), "0.0.0.0");
        snprintf(info.wifiBssid, sizeof(info.wifiBssid), "00:00:00:00:00:00");
    }
    snprintf(info.wifiMacAddress, sizeof(info.wifiMacAddress), "%s", WiFi.macAddress().c_str()); // MAC is available even when disconnected
   
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
    doc["firmware"]["partitionAppName"] = info.partitionAppName;

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
    doc["time"]["currentTimestampIso"] = info.currentTimestampIso;

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
    char jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, jsonString, sizeof(jsonString), true);
    logger.debug("JSON deserialized from SPIFFS correctly: %s", TAG, jsonString);
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
    char jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, jsonString, sizeof(jsonString), true);
    logger.debug("JSON serialized to SPIFFS correctly: %s", TAG, jsonString);

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

    JsonDocument jsonDocument;

    for (int i = 0; i < CHANNEL_COUNT; i++) {
        jsonDocument[i]["activeEnergyImported"] = 0;
        jsonDocument[i]["activeEnergyExported"] = 0;
        jsonDocument[i]["reactiveEnergyImported"] = 0;
        jsonDocument[i]["reactiveEnergyExported"] = 0;
        jsonDocument[i]["apparentEnergy"] = 0;
    }

    // Note: Energy data will be stored in SPIFFS/LittleFS for historical data
    // This function will be updated when we migrate to LittleFS
    serializeJsonToSpiffs(ENERGY_JSON_PATH, jsonDocument);

    logger.debug("Default %s created", TAG, ENERGY_JSON_PATH);
}

void createDefaultDailyEnergyFile() {
    logger.debug("Creating default %s...", TAG, DAILY_ENERGY_JSON_PATH);

    createEmptyJsonFile(DAILY_ENERGY_JSON_PATH);

    logger.debug("Default %s created", TAG, DAILY_ENERGY_JSON_PATH);
}

void createDefaultAde7953ConfigurationFile() {
    logger.debug("Creating default ADE7953 configuration...", TAG);

    JsonDocument jsonDocument;

    jsonDocument["sampleTime"] = MINIMUM_SAMPLE_TIME;
    jsonDocument["aVGain"] = DEFAULT_GAIN;
    jsonDocument["aIGain"] = DEFAULT_GAIN;
    jsonDocument["bIGain"] = DEFAULT_GAIN;
    jsonDocument["aIRmsOs"] = DEFAULT_OFFSET;
    jsonDocument["bIRmsOs"] = DEFAULT_OFFSET;
    jsonDocument["aWGain"] = DEFAULT_GAIN;
    jsonDocument["bWGain"] = DEFAULT_GAIN;
    jsonDocument["aWattOs"] = DEFAULT_OFFSET;
    jsonDocument["bWattOs"] = DEFAULT_OFFSET;
    jsonDocument["aVarGain"] = DEFAULT_GAIN;
    jsonDocument["bVarGain"] = DEFAULT_GAIN;
    jsonDocument["aVarOs"] = DEFAULT_OFFSET;
    jsonDocument["bVarOs"] = DEFAULT_OFFSET;
    jsonDocument["aVaGain"] = DEFAULT_GAIN;
    jsonDocument["bVaGain"] = DEFAULT_GAIN;
    jsonDocument["aVaOs"] = DEFAULT_OFFSET;
    jsonDocument["bVaOs"] = DEFAULT_OFFSET;
    jsonDocument["phCalA"] = DEFAULT_PHCAL;
    jsonDocument["phCalB"] = DEFAULT_PHCAL;

    logger.warning("Actually save this!!!!", TAG);

    logger.debug("Default ADE7953 configuration created", TAG);
}

void createDefaultCalibrationFile() {
    logger.debug("Creating default calibration...", TAG);

    JsonDocument jsonDocument;
    deserializeJson(jsonDocument, default_config_calibration_json);

    // Note: Calibration data is configuration, not historical data
    // This will be handled by the ADE7953 module's own Preferences
    // For now, keep SPIFFS for backward compatibility
    serializeJsonToSpiffs(CALIBRATION_JSON_PATH, jsonDocument);

    logger.debug("Default calibration created", TAG);
}

void createDefaultChannelDataFile() { // TODO: remove this weird json to preferences stuff
    logger.debug("Creating default channel configuration...", TAG);

    JsonDocument jsonDocument;
    deserializeJson(jsonDocument, default_config_channel_json);

    logger.warning("Actually save this!!!!", TAG);

    logger.debug("Default channel configuration created", TAG);
}

void createDefaultCustomMqttConfigurationFile() {
    logger.debug("Creating default custom MQTT configuration...", TAG);

    JsonDocument jsonDocument;

    // FIXME: deprecate and move this

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
    
    _maintenanceTaskShouldRun = true;
    while (_maintenanceTaskShouldRun) {
        logger.debug("Running maintenance checks...", TAG);
        
        // Update and print statistics
        updateStatistics();
        printStatistics();
        printDeviceStatusDynamic();

        // Check heap memory
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) {
            logger.fatal("Heap memory has degraded below safe minimum (%d bytes): %d bytes", TAG, MINIMUM_FREE_HEAP_SIZE, ESP.getFreeHeap());
            setRestartSystem(TAG, "Heap memory has degraded below safe minimum");
        }

        // Check PSRAM memory
        if (ESP.getFreePsram() < MINIMUM_FREE_PSRAM_SIZE) {
            logger.fatal("PSRAM memory has degraded below safe minimum (%d bytes): %d bytes", TAG, MINIMUM_FREE_PSRAM_SIZE, ESP.getFreePsram());
            setRestartSystem(TAG, "PSRAM memory has degraded below safe minimum");
        }

        // Check SPIFFS memory and clear log if needed
        if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
            logger.clearLog();
            logger.warning("Log cleared due to low memory", TAG);
        }
        
        logger.debug("Maintenance checks completed", TAG);

        // Wait for stop notification with timeout (blocking)
        unsigned long notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MAINTENANCE_CHECK_INTERVAL));
        if (notificationValue > 0) {
            _maintenanceTaskShouldRun = false;
            break;
        }
    }

    logger.debug("Maintenance task stopping", TAG);
    _maintenanceTaskHandle = NULL;
    vTaskDelete(NULL);
}

void startMaintenanceTask() {
    if (_maintenanceTaskHandle != NULL) {
        logger.debug("Maintenance task is already running", TAG);
        return;
    }
    
    logger.debug("Starting maintenance task", TAG);
    
    BaseType_t result = xTaskCreate(
        maintenanceTask,
        TASK_MAINTENANCE_NAME,
        TASK_MAINTENANCE_STACK_SIZE,
        NULL,
        TASK_MAINTENANCE_PRIORITY,
        &_maintenanceTaskHandle
    );
    
    if (result != pdPASS) {
        logger.error("Failed to create maintenance task", TAG);
    }
}

void stopTaskGracefully(TaskHandle_t* taskHandle, const char* taskName) {
    if (!taskHandle || *taskHandle == NULL) {
        logger.debug("%s was not running", TAG, taskName ? taskName : "Task");
        return;
    }

    logger.debug("Stopping %s...", TAG, taskName ? taskName : "task");
    
    xTaskNotifyGive(*taskHandle);
    
    // Wait with timeout for clean shutdown
    int timeout = TASK_STOPPING_TIMEOUT;
    while (*taskHandle != NULL && timeout > 0) {
        delay(TASK_STOPPING_CHECK_INTERVAL);
        timeout -= TASK_STOPPING_CHECK_INTERVAL;
    }
    
    // Force cleanup if needed
    if (*taskHandle != NULL) {
        logger.warning("Force stopping %s", TAG, taskName ? taskName : "task");
        vTaskDelete(*taskHandle);
        *taskHandle = NULL;
    } else {
        logger.debug("%s stopped successfully", TAG, taskName ? taskName : "Task");
    }
}

void stopMaintenanceTask() {
    stopTaskGracefully(&_maintenanceTaskHandle, "maintenance task");
}

// Task function that handles delayed restart. No need for complex handling here, just a simple delay and restart.
void restartTask(void* parameter) {
    logger.debug("Restart task started, waiting %d ms before restart", TAG, SYSTEM_RESTART_DELAY);
    
    // Wait for the specified delay
    delay(SYSTEM_RESTART_DELAY);
    
    // Execute the restart
    restartSystem();
    
    // Task should never reach here, but clean up just in case
    vTaskDelete(NULL);
}

void setRestartSystem(const char* functionName, const char* reason) { 
    logger.info("Restart required from function %s. Reason: %s", TAG, functionName, reason);
    
    if (_restartTaskHandle != NULL) {
        logger.warning("A restart is already scheduled. Keeping the existing configuration.", TAG);
        return; // Prevent overwriting an existing restart request
    }

    stopMaintenanceTask();
    // TODO: add MQTT, ade7953
    CustomMqtt::stop();
    InfluxDbClient::stop();
    ModbusTcp::stop();
    CustomServer::stop();

    // Create a task that will handle the delayed restart
    BaseType_t result = xTaskCreate(
        restartTask,
        TASK_RESTART_NAME,
        TASK_RESTART_STACK_SIZE,
        NULL,
        TASK_RESTART_PRIORITY,
        &_restartTaskHandle
    );
    
    if (result != pdPASS) {
        logger.error("Failed to create restart task, performing immediate restart", TAG);
        restartSystem();
    } else {
        logger.debug("Restart task created successfully", TAG);
    }
}

void restartSystem() {
    Led::setBrightness(max(Led::getBrightness(), (unsigned int)1)); // Show a faint light even if it is off
    Led::setOrange(Led::PRIO_CRITICAL);

    logger.info("Restarting system", TAG);
    logger.end(); // Only stop at last to ensure stopping logs are sent

    // Give time for AsyncTCP connections to close gracefully
    delay(1000);

    // Stop at last to ensure all logs are sent
    CustomLog::stop();
    CustomWifi::stop();
    
    // Additional delay to ensure clean shutdown
    delay(500);

    ESP.restart();
}

// Print functions
// -----------------------------

void printDeviceStatusStatic()
{
    SystemStaticInfo info;
    populateSystemStaticInfo(info);

    logger.debug("--- Static System Info ---", TAG);
    logger.debug("Product: %s (%s)", TAG, info.fullProductName, info.productName);
    logger.debug("Company: %s | Author: %s", TAG, info.companyName, info.author);
    logger.debug("Firmware: %s | Build: %s %s", TAG, info.buildVersion, info.buildDate, info.buildTime);
    logger.debug("Sketch MD5: %s | Partition app name: %s", TAG, info.sketchMD5, info.partitionAppName);
    logger.debug("Flash: %lu bytes, %lu Hz | PSRAM: %lu bytes", TAG, info.flashChipSizeBytes, info.flashChipSpeedHz, info.psramSizeBytes);
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
    logger.debug("Uptime: %llu s (%llu ms) | Timestamp: %s", TAG, info.uptimeSeconds, info.uptimeMilliseconds, info.currentTimestamp);
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
    logger.debug("SPIFFS: %lu total, %lu free (%.2f%%), %lu used (%.2f%%)", 
        TAG, 
        info.spiffsTotalBytes, 
        info.spiffsFreeBytes, info.spiffsFreePercentage, 
        info.spiffsUsedBytes, info.spiffsUsedPercentage
    );
    logger.debug("Temperature: %.2f C", TAG, info.temperatureCelsius);
    
    // WiFi information
    if (info.wifiConnected) {
        logger.debug("WiFi: Connected to '%s' (BSSID: %s) | RSSI %d dBm | MAC %s", TAG, info.wifiSsid, info.wifiBssid, info.wifiRssi, info.wifiMacAddress);
        logger.debug("WiFi: IP %s | Gateway %s | DNS %s | Subnet %s", TAG, info.wifiLocalIp, info.wifiGatewayIp, info.wifiDnsIp, info.wifiSubnetMask);
    } else {
        logger.debug("WiFi: Disconnected | MAC %s", TAG, info.wifiMacAddress);
    }

    logger.debug("-------------------------", TAG);
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
    logger.debug("--- Statistics ---", TAG);
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

    logger.debug("Statistics - Web Server: %ld requests | %ld errors", 
        TAG, 
        statistics.webServerRequests, 
        statistics.webServerRequestsError
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
    logger.debug("-------------------", TAG);
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

    // Web Server statistics
    jsonDocument["webServer"]["requests"] = statistics.webServerRequests;
    jsonDocument["webServer"]["requestsError"] = statistics.webServerRequestsError;

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

void factoryReset() { 
    logger.fatal("Factory reset requested", TAG);

    Led::setBrightness(max(Led::getBrightness(), (unsigned int)1)); // Show a faint light even if it is off
    Led::blinkRedFast(Led::PRIO_CRITICAL);

    clearAllPreferences();

    logger.fatal("Formatting SPIFFS. This will take some time.", TAG);
    SPIFFS.format();

    CrashMonitor::clearCrashCount(); // Reset crash monitor to clear crash count and last reset reason

    // Properly shutdown WiFi to prevent crashes during restart
    CustomWifi::stop();
    delay(500);

    // Directly call ESP.restart() so that a fresh start is done
    ESP.restart();
}

void clearAllPreferences() {
    logger.fatal("Clear all preferences requested", TAG);

    Preferences preferences;
    preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CALIBRATION, false);
    preferences.clear();
    preferences.end();
    
    preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false);
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

    preferences.begin(PREFERENCES_NAMESPACE_WIFI, false);
    preferences.clear();
    preferences.end();
    
    preferences.begin(PREFERENCES_NAMESPACE_TIME, false);
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
}

bool isLatestFirmwareInstalled() {
    JsonDocument jsonDocument;
    // deserializeJsonFromSpiffs(FW_UPDATE_INFO_JSON_PATH, jsonDocument);
    // TODO: switch to Preferences
    logger.warning("IMPLEMENT THIS: deserializeJsonFromSpiffs for FW_UPDATE_INFO_JSON_PATH", TAG);
    return true; // For now, return true as we don't have the implementation yet
    
    if (jsonDocument.isNull() || jsonDocument.size() == 0) {
        logger.debug("Firmware update info file is empty", TAG);
        return true;
    }

    char latestFirmwareVersion[VERSION_BUFFER_SIZE];
    char currentFirmwareVersion[VERSION_BUFFER_SIZE];

    snprintf(latestFirmwareVersion, sizeof(latestFirmwareVersion), "%s", jsonDocument["buildVersion"].as<const char*>());
    snprintf(currentFirmwareVersion, sizeof(currentFirmwareVersion), "%s", FIRMWARE_BUILD_VERSION);

    logger.debug(
        "Latest firmware version: %s | Current firmware version: %s",
        TAG,
        latestFirmwareVersion,
        currentFirmwareVersion
    );

    if (strlen(latestFirmwareVersion) == 0 || strchr(latestFirmwareVersion, '.') == NULL) {
        logger.warning("Latest firmware version is empty or in the wrong format", TAG);
        return true;
    }

    int _latestMajor, _latestMinor, _latestPatch;
    sscanf(latestFirmwareVersion, "%d.%d.%d", &_latestMajor, &_latestMinor, &_latestPatch);

    int currentMajor = atoi(FIRMWARE_BUILD_VERSION_MAJOR);
    int currentMinor = atoi(FIRMWARE_BUILD_VERSION_MINOR);
    int currentPatch = atoi(FIRMWARE_BUILD_VERSION_PATCH);

    if (_latestMajor < currentMajor) return true;
    if (_latestMajor > currentMajor) return false;
    if (_latestMinor < currentMinor) return true;
    if (_latestMinor > currentMinor) return false;
    return _latestPatch <= currentPatch;
}

void updateJsonFirmwareStatus(const char *status, const char *reason)
{
    JsonDocument jsonDocument;

    jsonDocument["status"] = status;
    jsonDocument["reason"] = reason;
    char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer)); // TODO: maybe everything should be returned in unix so it is UTC, and then converted on the other side? or standard iso utc timestamp?
    jsonDocument["timestamp"] = timestampBuffer;

    // Note: Firmware status is temporary data, keep in SPIFFS for now
    // This will be updated when we migrate to LittleFS for temporary data
    // serializeJsonToSpiffs(FW_UPDATE_STATUS_JSON_PATH, jsonDocument);
    logger.warning("IMPLEMENT THIS: updateJsonFirmwareStatus", TAG); // TODO: switch to Preferences
}

void getDeviceId(char* deviceId, size_t maxLength) {
    unsigned char mac[6];
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

bool validateUnixTime(unsigned long long unixTime, bool isMilliseconds) {
    if (isMilliseconds) {
        return (unixTime >= MINIMUM_UNIX_TIME_MILLISECONDS && unixTime <= MAXIMUM_UNIX_TIME_MILLISECONDS);
    } else {
        return (unixTime >= MINIMUM_UNIX_TIME && unixTime <= MAXIMUM_UNIX_TIME);
    }
}

unsigned long long calculateExponentialBackoff(unsigned long long attempt, unsigned long long initialInterval, unsigned long long maxInterval, unsigned long long multiplier) {
    if (attempt == 0) {
        return 0;
    }
    
    unsigned long long backoffDelay = initialInterval;
    
    for (unsigned long long i = 0; i < attempt - 1 && backoffDelay < maxInterval; ++i) {
        backoffDelay *= multiplier;
    }
    
    return min(backoffDelay, maxInterval);
}