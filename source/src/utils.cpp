#include "utils.h"

static const char *TAG = "utils";

static TaskHandle_t _restartTaskHandle = NULL;
static TaskHandle_t _maintenanceTaskHandle = NULL;
static bool _maintenanceTaskShouldRun = false;

// Static function declarations
static void _factoryReset();
static void _restartSystem();
static void _maintenanceTask(void* parameter);

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
    info.consecutiveCrashCount = CrashMonitor::getConsecutiveCrashCount();
    info.resetCount = CrashMonitor::getResetCount();
    info.consecutiveResetCount = CrashMonitor::getConsecutiveResetCount();
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
    info.psramTotalBytes = ESP.getPsramSize();
    if (info.psramTotalBytes > 0) {
        info.psramFreeBytes = ESP.getFreePsram();
        info.psramUsedBytes = info.psramTotalBytes - info.psramFreeBytes;
        info.psramMinFreeBytes = ESP.getMinFreePsram();
        info.psramMaxAllocBytes = ESP.getMaxAllocPsram();
        info.psramFreePercentage = info.psramTotalBytes > 0 ? ((float)info.psramFreeBytes / info.psramTotalBytes) * 100.0f : 0.0f;
        info.psramUsedPercentage = 100.0f - info.psramFreePercentage;
    } else {
        info.psramFreeBytes = 0;
        info.psramUsedBytes = 0;
        info.psramMinFreeBytes = 0;
        info.psramMaxAllocBytes = 0;
        info.psramFreePercentage = 0.0f;
        info.psramUsedPercentage = 0.0f;
    }
    
    // Storage - SPIFFS
    info.spiffsTotalBytes = SPIFFS.totalBytes();
    info.spiffsUsedBytes = SPIFFS.usedBytes();
    info.spiffsFreeBytes = info.spiffsTotalBytes - info.spiffsUsedBytes;
    info.spiffsFreePercentage = info.spiffsTotalBytes > 0 ? ((float)info.spiffsFreeBytes / info.spiffsTotalBytes) * 100.0f : 0.0f;
    info.spiffsUsedPercentage = 100.0f - info.spiffsFreePercentage;

    // Storage - NVS
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    info.usedEntries = nvs_stats.used_entries;
    info.availableEntries = nvs_stats.available_entries;
    info.totalUsableEntries = info.usedEntries + info.availableEntries; // Some are reserved
    info.usedEntriesPercentage = info.totalUsableEntries > 0 ? ((float)info.usedEntries / info.totalUsableEntries) * 100.0f : 0.0f;
    info.availableEntriesPercentage = info.totalUsableEntries > 0 ? ((float)info.availableEntries / info.totalUsableEntries) * 100.0f : 0.0f;
    info.namespaceCount = nvs_stats.namespace_count;

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
    doc["monitoring"]["consecutiveCrashCount"] = info.consecutiveCrashCount;
    doc["monitoring"]["resetCount"] = info.resetCount;
    doc["monitoring"]["consecutiveResetCount"] = info.consecutiveResetCount;
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
    doc["memory"]["psram"]["totalBytes"] = info.psramTotalBytes;
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

    // Storage - NVS
    doc["storage"]["nvs"]["totalUsableEntries"] = info.totalUsableEntries;
    doc["storage"]["nvs"]["usedEntries"] = info.usedEntries;
    doc["storage"]["nvs"]["availableEntries"] = info.availableEntries;
    doc["storage"]["nvs"]["usedEntriesPercentage"] = info.usedEntriesPercentage;
    doc["storage"]["nvs"]["availableEntriesPercentage"] = info.availableEntriesPercentage;
    doc["storage"]["nvs"]["namespaceCount"] = info.namespaceCount;

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

    size_t size = measureJson(jsonDocument);
    if (size >= bufferSize) {
        if (truncateOnError) {
            // Truncate JSON to fit buffer
            serializeJson(jsonDocument, buffer, bufferSize);
            // Ensure null-termination (avoid weird last character issues)
            buffer[bufferSize - 1] = '\0';
            
            logger.debug("Truncating JSON to fit buffer size (%zu bytes vs %zu bytes)", TAG, bufferSize, size);
        } else {
            logger.warning("JSON size (%zu bytes) exceeds buffer size (%zu bytes)", TAG, size, bufferSize);
            snprintf(buffer, bufferSize, ""); // Clear buffer on failure
        }
        return false;
    }

    serializeJson(jsonDocument, buffer, bufferSize);
    logger.verbose("JSON serialized successfully (bytes: %zu): %s", TAG, size, buffer);
    return true;
}

// Task function that handles periodic maintenance checks
static void _maintenanceTask(void* parameter) {
    logger.debug("Maintenance task started", TAG);
    
    _maintenanceTaskShouldRun = true;
    while (_maintenanceTaskShouldRun) {
        // Update and print statistics
        updateStatistics();
        printStatistics();
        printDeviceStatusDynamic();

        // Check heap memory
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) {
            logger.fatal("Heap memory has degraded below safe minimum (%d bytes): %lu bytes", TAG, MINIMUM_FREE_HEAP_SIZE, ESP.getFreeHeap());
            setRestartSystem(TAG, "Heap memory has degraded below safe minimum");
        }

        // Check PSRAM memory
        if (ESP.getFreePsram() < MINIMUM_FREE_PSRAM_SIZE) {
            logger.fatal("PSRAM memory has degraded below safe minimum (%d bytes): %lu bytes", TAG, MINIMUM_FREE_PSRAM_SIZE, ESP.getFreePsram());
            setRestartSystem(TAG, "PSRAM memory has degraded below safe minimum");
        }

        // Check SPIFFS memory and clear log if needed
        if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
            logger.clearLog();
            logger.warning("Log cleared due to low memory", TAG);
        }
        
        logger.debug("Maintenance checks completed", TAG);

        // Wait for stop notification with timeout (blocking)
        uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MAINTENANCE_CHECK_INTERVAL));
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
        _maintenanceTask,
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
    int32_t timeout = TASK_STOPPING_TIMEOUT;
    uint32_t loops = 0;
    while (*taskHandle != NULL && timeout > 0 && loops < MAX_LOOP_ITERATIONS) {
        loops++;
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
    bool factoryReset = (bool)(uintptr_t)parameter;

    logger.debug(
        "Restart task started, stopping all services and waiting %d ms before restart (factory reset: %s)",
        TAG,
        SYSTEM_RESTART_DELAY,
        factoryReset ? "true" : "false"
    );

    // Wait for the specified delay
    // In theory we could restart immediately since the task stopping was done earlier.. but let's be careful
    // and give time for other possible tasks or things to clean up properly
    delay(SYSTEM_RESTART_DELAY);
    
    // Execute the operation
    if (factoryReset) _factoryReset();
    _restartSystem();
    
    // Task should never reach here, but clean up just in case
    vTaskDelete(NULL);
}

void setRestartSystem(const char* functionName, const char* reason, bool factoryReset) {
    logger.info("Restart required from function %s. Reason: %s. Factory reset: %s", TAG, functionName, reason, factoryReset ? "true" : "false");
    
    if (_restartTaskHandle != NULL) {
        logger.info("A restart is already scheduled. Keeping the existing one.", TAG);
        return; // Prevent overwriting an existing restart request
    }

    // Create a task that will handle the delayed restart/factory reset
    BaseType_t result = xTaskCreate(
        restartTask,
        TASK_RESTART_NAME,
        TASK_RESTART_STACK_SIZE,
        (void*)(uintptr_t)factoryReset,
        TASK_RESTART_PRIORITY,
        &_restartTaskHandle
    );

    // We need to stop all services here instead of in the restart task
    // so to ensure that even if something is blocking, the restart will happen no matter what
    stopMaintenanceTask();
    Ade7953::stop();
    #if HAS_SECRETS
    Mqtt::stop();
    #endif
    CustomMqtt::stop();
    InfluxDbClient::stop();
    ModbusTcp::stop();
    CustomServer::stop();
    
    if (result != pdPASS) {
        logger.error("Failed to create restart task, performing immediate operation", TAG);
        if (factoryReset) { _factoryReset(); }
        _restartSystem();
    } else {
        logger.debug("Restart task created successfully", TAG);
    }
}

void setFactoryReset(const char* functionName, const char* reason) {
    setRestartSystem(functionName, reason, true);
}

static void _restartSystem() {
    Led::setBrightness(max(Led::getBrightness(), (uint32_t)1)); // Show a faint light even if it is off
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
    logger.debug("Monitoring: %lu crashes (%lu consecutive), %lu resets (%lu consecutive) | Last reset: %s", TAG, info.crashCount, info.consecutiveCrashCount, info.resetCount, info.consecutiveResetCount, info.lastResetReasonString);

    logger.debug("------------------------", TAG);
}

void printDeviceStatusDynamic()
{
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);

    logger.debug("--- Dynamic System Info ---", TAG);
    logger.debug(
        "Uptime: %llu s (%llu ms) | Timestamp: %s | Temperature: %.2f C", 
        TAG,
        info.uptimeSeconds, info.uptimeMilliseconds, info.currentTimestamp, info.temperatureCelsius
    );

    logger.debug("Heap: %lu total, %lu free (%.2f%%), %lu used (%.2f%%), %lu min free, %lu max alloc", 
        TAG, 
        info.heapTotalBytes, 
        info.heapFreeBytes, info.heapFreePercentage, 
        info.heapUsedBytes, info.heapUsedPercentage, 
        info.heapMinFreeBytes, info.heapMaxAllocBytes
    );
    if (info.psramFreeBytes > 0 || info.psramUsedBytes > 0) {
        logger.debug("PSRAM: %lu total, %lu free (%.2f%%), %lu used (%.2f%%), %lu min free, %lu max alloc", 
            TAG, 
            info.psramTotalBytes,
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
    logger.debug("NVS: %lu total, %lu free (%.2f%%), %lu used (%.2f%%), %u namespaces", 
        TAG, 
        info.totalUsableEntries, info.availableEntries, info.availableEntriesPercentage, 
        info.usedEntries, info.usedEntriesPercentage, info.namespaceCount
    );

    if (info.wifiConnected) {
        logger.debug("WiFi: Connected to '%s' (BSSID: %s) | RSSI %ld dBm | MAC %s", TAG, info.wifiSsid, info.wifiBssid, info.wifiRssi, info.wifiMacAddress);
        logger.debug("WiFi: IP %s | Gateway %s | DNS %s | Subnet %s", TAG, info.wifiLocalIp, info.wifiGatewayIp, info.wifiDnsIp, info.wifiSubnetMask);
    } else {
        logger.debug("WiFi: Disconnected | MAC %s", TAG, info.wifiMacAddress);
    }

    logger.debug("-------------------------", TAG);
}

void updateStatistics() {
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

static void _factoryReset() { 
    logger.warning("Factory reset requested", TAG);

    Led::setBrightness(max(Led::getBrightness(), (uint32_t)1)); // Show a faint light even if it is off
    Led::blinkRedFast(Led::PRIO_CRITICAL);

    clearAllPreferences();

    logger.warning("Formatting SPIFFS. This will take some time.", TAG);
    SPIFFS.format();

    CrashMonitor::clearConsecutiveCrashCount(); // Reset crash monitor to clear crash count and last reset reason

    // Removed ESP.restart() call since the factory reset can only be called from the restart task
}

bool isFirstBootDone() {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_GENERAL, true)) {
        logger.debug("Could not open preferences namespace: %s. Assuming first boot", TAG, PREFERENCES_NAMESPACE_GENERAL);
        return false;
    }
    bool firstBoot = preferences.getBool(IS_FIRST_BOOT_DONE_KEY, false);
    preferences.end();

    return firstBoot;
}

void setFirstBootDone() { // No arguments because the only way to set first boot done to false it through a complete wipe - thus automatically setting it to "false"
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
        logger.error("Failed to open preferences namespace: %s", TAG, PREFERENCES_NAMESPACE_GENERAL);
        return;
    }
    preferences.putBool(IS_FIRST_BOOT_DONE_KEY, true);
    preferences.end();
}

void createAllNamespaces() {
    Preferences preferences;

    preferences.begin(PREFERENCES_NAMESPACE_GENERAL, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CALIBRATION, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_MQTT, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_BUTTON, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_WIFI, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_TIME, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_LED, false); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_AUTH, false); preferences.end();

    logger.debug("All namespaces created", TAG);
}

void clearAllPreferences() {
    Preferences preferences;

    preferences.begin(PREFERENCES_NAMESPACE_GENERAL, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CALIBRATION, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_ENERGY, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_MQTT, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CUSTOM_MQTT, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_BUTTON, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_WIFI, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_TIME, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CRASHMONITOR, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_LED, false); preferences.clear(); preferences.end();
    preferences.begin(PREFERENCES_NAMESPACE_AUTH, false); preferences.clear(); preferences.end();

    #if ENV_DEV
    nvs_flash_erase(); // Nuclear solution. In development, the NVS can get overcrowded with test data, so we clear it completely.
    #endif

    logger.warning("Cleared all preferences", TAG);
}

void getDeviceId(char* deviceId, size_t maxLength) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    // Use lowercase hex formatting without colons
    snprintf(deviceId, maxLength, "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint64_t calculateExponentialBackoff(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval, uint64_t multiplier) {
    if (attempt == 0) return 0;
    
    uint64_t backoffDelay = initialInterval;
    
    for (uint64_t i = 0; i < attempt - 1 && backoffDelay < maxInterval; ++i) {
        backoffDelay *= multiplier;
    }
    
    return min(backoffDelay, maxInterval);
}
    
// === SPIFFS FILE OPERATIONS ===

bool listSpiffsFiles(JsonDocument& doc) {
    File root = SPIFFS.open("/");
    if (!root) {
        logger.error("Failed to open SPIFFS root directory", TAG);
        return false;
    }

    File file = root.openNextFile();
    uint32_t loops = 0;
    
    while (file && loops < MAX_LOOP_ITERATIONS) {
        loops++;
        const char* filename = file.path();

        // Remove first char which is always a slash 
        // (useful to remove so later in the get content we can directly use the filename)
        if (filename[0] == '/') filename++; // Skip the leading slash
        
        // Add file with its size to the JSON document
        doc[filename] = file.size();
        
        file = root.openNextFile();
    }

    root.close();
    return true;
}

bool getSpiffsFileContent(const char* filepath, char* buffer, size_t bufferSize) {
    if (!filepath || !buffer || bufferSize == 0) {
        logger.error("Invalid arguments provided", TAG);
        return false;
    }
    
    // Check if file exists
    if (!SPIFFS.exists(filepath)) {
        logger.debug("File not found: %s", TAG, filepath);
        return false;
    }
    
    File file = SPIFFS.open(filepath, "r");
    if (!file) {
        logger.error("Failed to open file: %s", TAG, filepath);
        return false;
    }

    size_t bytesRead = file.readBytes(buffer, bufferSize - 1);
    buffer[bytesRead] = '\0';  // Null-terminate the string
    file.close();

    logger.debug("Successfully read file: %s (%d bytes)", TAG, filepath, bytesRead);
    return true;
}

const char* getContentTypeFromFilename(const char* filename) {
    if (!filename) return "application/octet-stream";
    
    // Find the file extension
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    
    // Convert to lowercase for comparison
    char extension[16];
    size_t extLen = strlen(ext);
    if (extLen >= sizeof(extension)) return "application/octet-stream";
    
    for (size_t i = 0; i < extLen; i++) {
        extension[i] = tolower(ext[i]);
    }
    extension[extLen] = '\0';
    
    // Common file types used in the project
    if (strcmp(extension, ".json") == 0) return "application/json";
    if (strcmp(extension, ".txt") == 0) return "text/plain";
    if (strcmp(extension, ".log") == 0) return "text/plain";
    if (strcmp(extension, ".csv") == 0) return "text/csv";
    if (strcmp(extension, ".xml") == 0) return "application/xml";
    if (strcmp(extension, ".html") == 0) return "text/html";
    if (strcmp(extension, ".css") == 0) return "text/css";
    if (strcmp(extension, ".js") == 0) return "application/javascript";
    if (strcmp(extension, ".bin") == 0) return "application/octet-stream";
    
    return "application/octet-stream";
}