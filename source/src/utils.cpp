// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "utils.h"

static TaskHandle_t _restartTaskHandle = NULL;
static TaskHandle_t _maintenanceTaskHandle = NULL;
static bool _maintenanceTaskShouldRun = false;

// Static function declarations
static void _factoryReset();
static void _restartTask(void* parameter);
static void _restartSystem(bool factoryReset = false);
static void _maintenanceTask(void* parameter);
static bool _listLittleFsFilesRecursive(JsonDocument &doc, const char* dirname, const char* basePath, uint8_t levels);
static bool _ensureDirectoryExists(const char* dirPath);
static bool _decompressGzipFile(const char* gzPath, const char* outputPath);
static bool _appendFileToFile(const char* srcPath, File &destFile, bool skipHeader);

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

    LOG_DEBUG("Static system info populated");
}

void populateSystemDynamicInfo(SystemDynamicInfo& info) {
    // Initialize the struct to ensure clean state
    memset(&info, 0, sizeof(info));

    // Time
    info.uptimeMilliseconds = millis64();
    info.uptimeSeconds = info.uptimeMilliseconds / 1000;
    CustomTime::getTimestampIso(info.currentTimestampIso, sizeof(info.currentTimestampIso));

    // Memory - Heap
    info.heapTotalBytes = ESP.getHeapSize();
    info.heapFreeBytes = ESP.getFreeHeap();
    info.heapUsedBytes = info.heapTotalBytes - info.heapFreeBytes;
    info.heapMinFreeBytes = ESP.getMinFreeHeap();
    info.heapMaxAllocBytes = ESP.getMaxAllocHeap();
    info.heapFreePercentage = info.heapTotalBytes > 0 ? ((float)info.heapFreeBytes / (float)info.heapTotalBytes) * 100.0f : 0.0f;
    info.heapUsedPercentage = 100.0f - info.heapFreePercentage;
    
    // Memory - PSRAM
    info.psramTotalBytes = ESP.getPsramSize();
    if (info.psramTotalBytes > 0) {
        info.psramFreeBytes = ESP.getFreePsram();
        info.psramUsedBytes = info.psramTotalBytes - info.psramFreeBytes;
        info.psramMinFreeBytes = ESP.getMinFreePsram();
        info.psramMaxAllocBytes = ESP.getMaxAllocPsram();
        info.psramFreePercentage = info.psramTotalBytes > 0 ? ((float)info.psramFreeBytes / (float)info.psramTotalBytes) * 100.0f : 0.0f;
        info.psramUsedPercentage = 100.0f - info.psramFreePercentage;
    } else {
        info.psramFreeBytes = 0;
        info.psramUsedBytes = 0;
        info.psramMinFreeBytes = 0;
        info.psramMaxAllocBytes = 0;
        info.psramFreePercentage = 0.0f;
        info.psramUsedPercentage = 0.0f;
    }
    
    // Storage - LittleFS
    info.littlefsTotalBytes = LittleFS.totalBytes();
    info.littlefsUsedBytes = LittleFS.usedBytes();
    info.littlefsFreeBytes = info.littlefsTotalBytes - info.littlefsUsedBytes;
    info.littlefsFreePercentage = info.littlefsTotalBytes > 0 ? ((float)info.littlefsFreeBytes / (float)info.littlefsTotalBytes) * 100.0f : 0.0f;
    info.littlefsUsedPercentage = 100.0f - info.littlefsFreePercentage;

    // Storage - NVS
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err != ESP_OK) {
        LOG_ERROR("Failed to get NVS stats: %s", esp_err_to_name(err));
        info.usedEntries = 0;
        info.availableEntries = 0;
        info.totalUsableEntries = 0;
        info.usedEntriesPercentage = 0.0f;
        info.availableEntriesPercentage = 0.0f;
        info.namespaceCount = 0;
    } else {
        info.usedEntries = nvs_stats.used_entries;
        info.availableEntries = nvs_stats.available_entries;
        info.totalUsableEntries = info.usedEntries + info.availableEntries; // Some are reserved
        info.usedEntriesPercentage = info.totalUsableEntries > 0 ? ((float)info.usedEntries / (float)info.totalUsableEntries) * 100.0f : 0.0f;
        info.availableEntriesPercentage = info.totalUsableEntries > 0 ? ((float)info.availableEntries / (float)info.totalUsableEntries) * 100.0f : 0.0f;
        info.namespaceCount = nvs_stats.namespace_count;
    }

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

    // Tasks
    #ifdef HAS_SECRETS
    info.mqttTaskInfo = Mqtt::getMqttTaskInfo();
    info.mqttOtaTaskInfo = Mqtt::getMqttOtaTaskInfo();
    #endif
    info.customMqttTaskInfo = CustomMqtt::getTaskInfo();
    info.customServerHealthCheckTaskInfo = CustomServer::getHealthCheckTaskInfo();
    info.customServerOtaTimeoutTaskInfo = CustomServer::getOtaTimeoutTaskInfo();
    info.ledTaskInfo = Led::getTaskInfo();
    info.influxDbTaskInfo = InfluxDbClient::getTaskInfo();
    info.crashMonitorTaskInfo = CrashMonitor::getTaskInfo();
    info.buttonHandlerTaskInfo = ButtonHandler::getTaskInfo();
    info.udpLogTaskInfo = CustomLog::getTaskInfo();
    info.customWifiTaskInfo = CustomWifi::getTaskInfo();
    info.ade7953MeterReadingTaskInfo = Ade7953::getMeterReadingTaskInfo();
    info.ade7953EnergySaveTaskInfo = Ade7953::getEnergySaveTaskInfo();
    info.ade7953HourlyCsvTaskInfo = Ade7953::getHourlyCsvTaskInfo();
    info.maintenanceTaskInfo = getMaintenanceTaskInfo();

    LOG_DEBUG("Dynamic system info populated");
}

void systemStaticInfoToJson(SystemStaticInfo& info, JsonDocument &doc) {
    // No need to use String for dandling pointer data since most of it here is
    // coming from compiled static variables
        
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

    LOG_DEBUG("Static system info converted to JSON");
}

void systemDynamicInfoToJson(SystemDynamicInfo& info, JsonDocument &doc) {
    // Time
    doc["time"]["uptimeMilliseconds"] = (uint64_t)info.uptimeMilliseconds;
    doc["time"]["uptimeSeconds"] = info.uptimeSeconds;
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
    doc["storage"]["littlefs"]["totalBytes"] = info.littlefsTotalBytes;
    doc["storage"]["littlefs"]["usedBytes"] = info.littlefsUsedBytes;
    doc["storage"]["littlefs"]["freeBytes"] = info.littlefsFreeBytes;
    doc["storage"]["littlefs"]["freePercentage"] = info.littlefsFreePercentage;
    doc["storage"]["littlefs"]["usedPercentage"] = info.littlefsUsedPercentage;

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
    doc["network"]["wifiSsid"] = JsonString(info.wifiSsid); // Ensure it is not a dangling pointer
    doc["network"]["wifiMacAddress"] = JsonString(info.wifiMacAddress); // Ensure it is not a dangling pointer
    doc["network"]["wifiLocalIp"] = JsonString(info.wifiLocalIp); // Ensure it is not a dangling pointer
    doc["network"]["wifiGatewayIp"] = JsonString(info.wifiGatewayIp); // Ensure it is not a dangling pointer
    doc["network"]["wifiSubnetMask"] = JsonString(info.wifiSubnetMask); // Ensure it is not a dangling pointer
    doc["network"]["wifiDnsIp"] = JsonString(info.wifiDnsIp); // Ensure it is not a dangling pointer
    doc["network"]["wifiBssid"] = JsonString(info.wifiBssid); // Ensure it is not a dangling pointer
    doc["network"]["wifiRssi"] = info.wifiRssi;

    // Tasks
    doc["tasks"]["mqtt"]["allocatedStack"] = info.mqttTaskInfo.allocatedStack;
    doc["tasks"]["mqtt"]["minimumFreeStack"] = info.mqttTaskInfo.minimumFreeStack;
    doc["tasks"]["mqtt"]["freePercentage"] = info.mqttTaskInfo.freePercentage;
    doc["tasks"]["mqtt"]["usedPercentage"] = info.mqttTaskInfo.usedPercentage;

    doc["tasks"]["mqttOta"]["allocatedStack"] = info.mqttOtaTaskInfo.allocatedStack;
    doc["tasks"]["mqttOta"]["minimumFreeStack"] = info.mqttOtaTaskInfo.minimumFreeStack;
    doc["tasks"]["mqttOta"]["freePercentage"] = info.mqttOtaTaskInfo.freePercentage;
    doc["tasks"]["mqttOta"]["usedPercentage"] = info.mqttOtaTaskInfo.usedPercentage;

    doc["tasks"]["customMqtt"]["allocatedStack"] = info.customMqttTaskInfo.allocatedStack;
    doc["tasks"]["customMqtt"]["minimumFreeStack"] = info.customMqttTaskInfo.minimumFreeStack;
    doc["tasks"]["customMqtt"]["freePercentage"] = info.customMqttTaskInfo.freePercentage;
    doc["tasks"]["customMqtt"]["usedPercentage"] = info.customMqttTaskInfo.usedPercentage;

    doc["tasks"]["customServerHealthCheck"]["allocatedStack"] = info.customServerHealthCheckTaskInfo.allocatedStack;
    doc["tasks"]["customServerHealthCheck"]["minimumFreeStack"] = info.customServerHealthCheckTaskInfo.minimumFreeStack;
    doc["tasks"]["customServerHealthCheck"]["freePercentage"] = info.customServerHealthCheckTaskInfo.freePercentage;
    doc["tasks"]["customServerHealthCheck"]["usedPercentage"] = info.customServerHealthCheckTaskInfo.usedPercentage;

    doc["tasks"]["customServerOtaTimeout"]["allocatedStack"] = info.customServerOtaTimeoutTaskInfo.allocatedStack;
    doc["tasks"]["customServerOtaTimeout"]["minimumFreeStack"] = info.customServerOtaTimeoutTaskInfo.minimumFreeStack;
    doc["tasks"]["customServerOtaTimeout"]["freePercentage"] = info.customServerOtaTimeoutTaskInfo.freePercentage;
    doc["tasks"]["customServerOtaTimeout"]["usedPercentage"] = info.customServerOtaTimeoutTaskInfo.usedPercentage;

    doc["tasks"]["led"]["allocatedStack"] = info.ledTaskInfo.allocatedStack;
    doc["tasks"]["led"]["minimumFreeStack"] = info.ledTaskInfo.minimumFreeStack;
    doc["tasks"]["led"]["freePercentage"] = info.ledTaskInfo.freePercentage;
    doc["tasks"]["led"]["usedPercentage"] = info.ledTaskInfo.usedPercentage;

    doc["tasks"]["influxDb"]["allocatedStack"] = info.influxDbTaskInfo.allocatedStack;
    doc["tasks"]["influxDb"]["minimumFreeStack"] = info.influxDbTaskInfo.minimumFreeStack;
    doc["tasks"]["influxDb"]["freePercentage"] = info.influxDbTaskInfo.freePercentage;
    doc["tasks"]["influxDb"]["usedPercentage"] = info.influxDbTaskInfo.usedPercentage;

    doc["tasks"]["crashMonitor"]["allocatedStack"] = info.crashMonitorTaskInfo.allocatedStack;
    doc["tasks"]["crashMonitor"]["minimumFreeStack"] = info.crashMonitorTaskInfo.minimumFreeStack;
    doc["tasks"]["crashMonitor"]["freePercentage"] = info.crashMonitorTaskInfo.freePercentage;
    doc["tasks"]["crashMonitor"]["usedPercentage"] = info.crashMonitorTaskInfo.usedPercentage;

    doc["tasks"]["buttonHandler"]["allocatedStack"] = info.buttonHandlerTaskInfo.allocatedStack;
    doc["tasks"]["buttonHandler"]["minimumFreeStack"] = info.buttonHandlerTaskInfo.minimumFreeStack;
    doc["tasks"]["buttonHandler"]["freePercentage"] = info.buttonHandlerTaskInfo.freePercentage;
    doc["tasks"]["buttonHandler"]["usedPercentage"] = info.buttonHandlerTaskInfo.usedPercentage;

    doc["tasks"]["udpLog"]["allocatedStack"] = info.udpLogTaskInfo.allocatedStack;
    doc["tasks"]["udpLog"]["minimumFreeStack"] = info.udpLogTaskInfo.minimumFreeStack;
    doc["tasks"]["udpLog"]["freePercentage"] = info.udpLogTaskInfo.freePercentage;
    doc["tasks"]["udpLog"]["usedPercentage"] = info.udpLogTaskInfo.usedPercentage;

    doc["tasks"]["customWifi"]["allocatedStack"] = info.customWifiTaskInfo.allocatedStack;
    doc["tasks"]["customWifi"]["minimumFreeStack"] = info.customWifiTaskInfo.minimumFreeStack;
    doc["tasks"]["customWifi"]["freePercentage"] = info.customWifiTaskInfo.freePercentage;
    doc["tasks"]["customWifi"]["usedPercentage"] = info.customWifiTaskInfo.usedPercentage;

    doc["tasks"]["ade7953MeterReading"]["allocatedStack"] = info.ade7953MeterReadingTaskInfo.allocatedStack;
    doc["tasks"]["ade7953MeterReading"]["minimumFreeStack"] = info.ade7953MeterReadingTaskInfo.minimumFreeStack;
    doc["tasks"]["ade7953MeterReading"]["freePercentage"] = info.ade7953MeterReadingTaskInfo.freePercentage;
    doc["tasks"]["ade7953MeterReading"]["usedPercentage"] = info.ade7953MeterReadingTaskInfo.usedPercentage;

    doc["tasks"]["ade7953EnergySave"]["allocatedStack"] = info.ade7953EnergySaveTaskInfo.allocatedStack;
    doc["tasks"]["ade7953EnergySave"]["minimumFreeStack"] = info.ade7953EnergySaveTaskInfo.minimumFreeStack;
    doc["tasks"]["ade7953EnergySave"]["freePercentage"] = info.ade7953EnergySaveTaskInfo.freePercentage;
    doc["tasks"]["ade7953EnergySave"]["usedPercentage"] = info.ade7953EnergySaveTaskInfo.usedPercentage;

    doc["tasks"]["ade7953HourlyCsv"]["allocatedStack"] = info.ade7953HourlyCsvTaskInfo.allocatedStack;
    doc["tasks"]["ade7953HourlyCsv"]["minimumFreeStack"] = info.ade7953HourlyCsvTaskInfo.minimumFreeStack;
    doc["tasks"]["ade7953HourlyCsv"]["freePercentage"] = info.ade7953HourlyCsvTaskInfo.freePercentage;
    doc["tasks"]["ade7953HourlyCsv"]["usedPercentage"] = info.ade7953HourlyCsvTaskInfo.usedPercentage;

    doc["tasks"]["maintenance"]["allocatedStack"] = info.maintenanceTaskInfo.allocatedStack;
    doc["tasks"]["maintenance"]["minimumFreeStack"] = info.maintenanceTaskInfo.minimumFreeStack;
    doc["tasks"]["maintenance"]["freePercentage"] = info.maintenanceTaskInfo.freePercentage;
    doc["tasks"]["maintenance"]["usedPercentage"] = info.maintenanceTaskInfo.usedPercentage;

    LOG_DEBUG("Dynamic system info converted to JSON");
}

void getJsonDeviceStaticInfo(JsonDocument &doc) {
    SystemStaticInfo info;
    populateSystemStaticInfo(info);
    systemStaticInfoToJson(info, doc);
}

void getJsonDeviceDynamicInfo(JsonDocument &doc) {
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);
    systemDynamicInfoToJson(info, doc);
}

bool safeSerializeJson(JsonDocument &jsonDocument, char* buffer, size_t bufferSize, bool truncateOnError) {
    // Validate inputs
    if (!buffer || bufferSize == 0) {
        LOG_WARNING("Invalid buffer parameters passed to safeSerializeJson");
        return false;
    }

    size_t size = measureJson(jsonDocument);
    if (size >= bufferSize) {
        if (truncateOnError) {
            // Truncate JSON to fit buffer
            serializeJson(jsonDocument, buffer, bufferSize);
            // Ensure null-termination (avoid weird last character issues)
            buffer[bufferSize - 1] = '\0';
            
            LOG_DEBUG("Truncating JSON to fit buffer size (%zu bytes vs %zu bytes)", bufferSize, size);
        } else {
            LOG_WARNING("JSON size (%zu bytes) exceeds buffer size (%zu bytes)", size, bufferSize);
            snprintf(buffer, bufferSize, "%s", ""); // Clear buffer on failure
        }
        return false;
    }

    serializeJson(jsonDocument, buffer, bufferSize);
    LOG_VERBOSE("JSON serialized successfully (bytes: %zu): %s", size, buffer);
    return true;
}

// Task function that handles periodic maintenance checks
static void _maintenanceTask(void* parameter) {
    LOG_DEBUG("Maintenance task started");
    
    _maintenanceTaskShouldRun = true;
    while (_maintenanceTaskShouldRun) {
        // Update and print statistics
        printStatistics();
        printDeviceStatusDynamic();

        // Check heap memory
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) {
            LOG_FATAL("Heap memory has degraded below safe minimum (%d bytes): %lu bytes", MINIMUM_FREE_HEAP_SIZE, ESP.getFreeHeap());
            setRestartSystem("Heap memory has degraded below safe minimum");
        }

        // Check PSRAM memory
        if (ESP.getFreePsram() < MINIMUM_FREE_PSRAM_SIZE) {
            LOG_FATAL("PSRAM memory has degraded below safe minimum (%d bytes): %lu bytes", MINIMUM_FREE_PSRAM_SIZE, ESP.getFreePsram());
            setRestartSystem("PSRAM memory has degraded below safe minimum");
        }

        // If the log file exceeds maximum size, clear it
        size_t logSize = getLogFileSize();
        if (logSize >= MAXIMUM_LOG_FILE_SIZE) {
            AdvancedLogger::clearLogKeepLatestXPercent(10);
            LOG_INFO("Log cleared due to size limit (size: %zu bytes, limit: %d bytes)", logSize, MAXIMUM_LOG_FILE_SIZE);
        }

        // Check LittleFS memory and clear log if needed
        if (LittleFS.totalBytes() - LittleFS.usedBytes() < MINIMUM_FREE_LITTLEFS_SIZE) {
            AdvancedLogger::clearLog(); // Here we clear all for safety
            LOG_WARNING("Log cleared due to low LittleFS memory");
        }
        
        LOG_DEBUG("Maintenance checks completed");

        // Wait for stop notification with timeout (blocking)
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MAINTENANCE_CHECK_INTERVAL)) > 0) {
            _maintenanceTaskShouldRun = false;
            break;
        }
    }

    LOG_DEBUG("Maintenance task stopping");
    
    _maintenanceTaskHandle = NULL;
    vTaskDelete(NULL);
}

void startMaintenanceTask() {
    if (_maintenanceTaskHandle != NULL) {
        LOG_DEBUG("Maintenance task is already running");
        return;
    }
    
    LOG_DEBUG("Starting maintenance task with %d bytes stack in internal RAM (performs flash I/O operations)", TASK_MAINTENANCE_STACK_SIZE);
    
    BaseType_t result = xTaskCreate(
        _maintenanceTask,
        TASK_MAINTENANCE_NAME,
        TASK_MAINTENANCE_STACK_SIZE,
        NULL,
        TASK_MAINTENANCE_PRIORITY,
        &_maintenanceTaskHandle);
    
    if (result != pdPASS) {
        LOG_ERROR("Failed to create maintenance task");
    }
}

size_t getLogFileSize() {
    if (!LittleFS.exists(LOG_PATH)) {
        return 0;
    }
    
    File logFile = LittleFS.open(LOG_PATH, FILE_READ);
    if (!logFile) {
        LOG_WARNING("Failed to open log file to check size");
        return 0;
    }
    
    size_t size = logFile.size();
    logFile.close();
    
    return size;
}

void stopTaskGracefully(TaskHandle_t* taskHandle, const char* taskName) {
    if (!taskHandle || *taskHandle == NULL) {
        LOG_DEBUG("%s was not running", taskName ? taskName : "Task");
        return;
    }

    LOG_DEBUG("Stopping %s...", taskName ? taskName : "task");
    
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
        LOG_WARNING("Force stopping %s", taskName ? taskName : "task");
        vTaskDelete(*taskHandle);
        *taskHandle = NULL;
    } else {
        LOG_DEBUG("%s stopped successfully", taskName ? taskName : "Task");
    }
}

void stopMaintenanceTask() {
    stopTaskGracefully(&_maintenanceTaskHandle, "maintenance task");
}

TaskInfo getMaintenanceTaskInfo()
{
    return getTaskInfoSafely(_maintenanceTaskHandle, TASK_MAINTENANCE_STACK_SIZE);
}

// Task function that handles delayed restart. No need for complex handling here, just a simple delay and restart.
static void _restartTask(void* parameter) {
    bool factoryReset = (bool)(uintptr_t)parameter;

    LOG_DEBUG(
        "Restart task started, stopping all services and waiting %d ms before restart (factory reset: %s)",
        SYSTEM_RESTART_DELAY,
        factoryReset ? "true" : "false"
    );

    // Only stop Ade7953 as we need to save the energy data and MQTT to avoid trying to send data while rebooting. Everything else can just die abruptly
    // Actually also stop the webserver to avoid requests on non-existent resources
    // We do this in an Async way so if for any reason the stopping takes too long or blocks forever, it won't block the restart
    xTaskCreate([](void*) {
        LOG_DEBUG("Stopping critical services before restart");
        CustomServer::stop(); // Stop first customserver to avoid deadlocks with MQTT
        Ade7953::stop();
        ModbusTcp::stop();
        #ifdef HAS_SECRETS
        Mqtt::stop();
        #endif
        LOG_DEBUG("Critical services stopped");
        vTaskDelete(NULL);
    }, STOP_SERVICES_TASK_NAME, STOP_SERVICES_TASK_STACK_SIZE, NULL, STOP_SERVICES_TASK_PRIORITY, NULL);

    _restartSystem(factoryReset);
    
    // Task should never reach here, but clean up just in case
    vTaskDelete(NULL);
}

static void _restartSystem(bool factoryReset) {
    Led::setBrightness(max(Led::getBrightness(), (uint8_t)1)); // Show a faint light even if it is off
    Led::setOrange(Led::PRIO_CRITICAL);

    delay(SYSTEM_RESTART_DELAY); // Allow for logs to flush

    // Ensure the log file is properly saved and closed
    AdvancedLogger::end();

    LOG_INFO("Restarting system. Factory reset: %s", factoryReset ? "true" : "false");
    if (factoryReset) {_factoryReset();}

    ESP.restart();
}

bool setRestartSystem(const char* reason, bool factoryReset) {
    LOG_INFO("Restart required for reason: %s. Factory reset: %s", reason, factoryReset ? "true" : "false");

    // Check if we can restart now (safe mode protection)
    if (!CrashMonitor::canRestartNow()) {
        uint32_t remainingMs = CrashMonitor::getMinimumUptimeRemaining();
        uint32_t remainingSec = remainingMs / 1000;
        
        if (CrashMonitor::isInSafeMode()) {
            LOG_FATAL(
                "SAFE MODE: Restart blocked for %lu s to prevent infinite loops. "
                "Reason: %s. WiFi/OTA active for remote recovery", 
                remainingSec, reason
            );
        } else {
            LOG_WARNING(
                "Restart delayed: minimum uptime not reached (%lu s remaining to "
                "prevent rapid restart loops)", 
                remainingSec
            );
        }
        
        return false;
    }

    if (_restartTaskHandle != NULL) {
        LOG_INFO("A restart is already scheduled. Keeping the existing one.");
        return false; // Prevent overwriting an existing restart request
    }

    // Create a task that will handle the delayed restart/factory reset and stop services safely
    LOG_DEBUG("Starting restart task with %d bytes stack in internal RAM (performs flash I/O operations)", TASK_RESTART_STACK_SIZE);

    BaseType_t result = xTaskCreate(
        _restartTask,
        TASK_RESTART_NAME,
        TASK_RESTART_STACK_SIZE,
        (void*)(uintptr_t)factoryReset,
        TASK_RESTART_PRIORITY,
        &_restartTaskHandle);

    if (result != pdPASS) {
        LOG_ERROR("Failed to create restart task, performing immediate operation");
        CustomServer::stop();
        Ade7953::stop();
        ModbusTcp::stop();
        #ifdef HAS_SECRETS
        Mqtt::stop();
        #endif
        _restartSystem(factoryReset);
    } else {
        LOG_DEBUG("Restart task created successfully");
    }

    return true;
}

// Print functions
// -----------------------------

void printDeviceStatusStatic()
{
    SystemStaticInfo *info = (SystemStaticInfo*)ps_malloc(sizeof(SystemStaticInfo));
    if (!info) {
        LOG_ERROR("Failed to allocate SystemStaticInfo in PSRAM");
        return;
    }
    
    populateSystemStaticInfo(*info);

    LOG_DEBUG("--- Static System Info ---");
    LOG_DEBUG("Product: %s (%s)", info->fullProductName, info->productName);
    LOG_DEBUG("Company: %s | Author: %s", info->companyName, info->author);
    LOG_DEBUG("Firmware: %s | Build: %s %s", info->buildVersion, info->buildDate, info->buildTime);
    LOG_DEBUG("Sketch MD5: %s | Partition app name: %s", info->sketchMD5, info->partitionAppName);
    LOG_DEBUG("Flash: %lu bytes, %lu Hz | PSRAM: %lu bytes", info->flashChipSizeBytes, info->flashChipSpeedHz, info->psramSizeBytes);
    LOG_DEBUG("Chip: %s, rev %u, cores %u, id 0x%llx, CPU: %lu MHz", info->chipModel, info->chipRevision, info->chipCores, info->chipId, info->cpuFrequencyMHz);
    LOG_DEBUG("SDK: %s | Core: %s", info->sdkVersion, info->coreVersion);
    LOG_DEBUG("Device ID: %s", info->deviceId);
    LOG_DEBUG("Monitoring: %lu crashes (%lu consecutive), %lu resets (%lu consecutive) | Last reset: %s", info->crashCount, info->consecutiveCrashCount, info->resetCount, info->consecutiveResetCount, info->lastResetReasonString);

    free(info);
    LOG_DEBUG("------------------------");
}

void printDeviceStatusDynamic()
{
    SystemDynamicInfo *info = (SystemDynamicInfo*)ps_malloc(sizeof(SystemDynamicInfo));
    if (!info) {
        LOG_ERROR("Failed to allocate SystemDynamicInfo in PSRAM");
        return;
    }
    
    populateSystemDynamicInfo(*info);

    LOG_DEBUG("--- Dynamic System Info ---");
    LOG_DEBUG(
        "Uptime: %llu s (%llu ms) | Timestamp: %s | Temperature: %.2f C", 
        info->uptimeSeconds, info->uptimeMilliseconds, info->currentTimestampIso, info->temperatureCelsius
    );

    LOG_DEBUG("Heap: %lu total, %lu free (%.1f%%), %lu used (%.1f%%), %lu min free, %lu max alloc",  
        info->heapTotalBytes, 
        info->heapFreeBytes, info->heapFreePercentage, 
        info->heapUsedBytes, info->heapUsedPercentage, 
        info->heapMinFreeBytes, info->heapMaxAllocBytes
    );
    if (info->psramFreeBytes > 0 || info->psramUsedBytes > 0) {
        LOG_DEBUG("PSRAM: %lu total, %lu free (%.1f%%), %lu used (%.1f%%), %lu min free, %lu max alloc", 
            info->psramTotalBytes,
            info->psramFreeBytes, info->psramFreePercentage, 
            info->psramUsedBytes, info->psramUsedPercentage, 
            info->psramMinFreeBytes, info->psramMaxAllocBytes
        );
    }
    LOG_DEBUG("LittleFS: %lu total, %lu free (%.1f%%), %lu used (%.1f%%)",  
        info->littlefsTotalBytes, 
        info->littlefsFreeBytes, info->littlefsFreePercentage, 
        info->littlefsUsedBytes, info->littlefsUsedPercentage
    );
    LOG_DEBUG("NVS: %lu total, %lu free (%.1f%%), %lu used (%.1f%%), %u namespaces",  
        info->totalUsableEntries, info->availableEntries, info->availableEntriesPercentage, 
        info->usedEntries, info->usedEntriesPercentage, info->namespaceCount
    );

    if (info->wifiConnected) {
        LOG_DEBUG("WiFi: Connected to '%s' (BSSID: %s) | RSSI %ld dBm | MAC %s", info->wifiSsid, info->wifiBssid, info->wifiRssi, info->wifiMacAddress);
        LOG_DEBUG("WiFi: IP %s | Gateway %s | DNS %s | Subnet %s", info->wifiLocalIp, info->wifiGatewayIp, info->wifiDnsIp, info->wifiSubnetMask);
    } else {
        LOG_DEBUG("WiFi: Disconnected | MAC %s", info->wifiMacAddress);
    }

    LOG_DEBUG("Tasks - MQTT: %lu total, %lu minimum free (%.1f%%)",
        info->mqttTaskInfo.allocatedStack, 
        info->mqttTaskInfo.minimumFreeStack, 
        info->mqttTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - MQTT OTA: %lu total, %lu minimum free (%.1f%%)",
        info->mqttOtaTaskInfo.allocatedStack, 
        info->mqttOtaTaskInfo.minimumFreeStack, 
        info->mqttOtaTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Custom MQTT: %lu total, %lu minimum free (%.1f%%)",
        info->customMqttTaskInfo.allocatedStack, 
        info->customMqttTaskInfo.minimumFreeStack, 
        info->customMqttTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Custom Server Health Check: %lu total, %lu minimum free (%.1f%%)",
        info->customServerHealthCheckTaskInfo.allocatedStack, 
        info->customServerHealthCheckTaskInfo.minimumFreeStack, 
        info->customServerHealthCheckTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Custom Server OTA Timeout: %lu total, %lu minimum free (%.1f%%)",
        info->customServerOtaTimeoutTaskInfo.allocatedStack, 
        info->customServerOtaTimeoutTaskInfo.minimumFreeStack, 
        info->customServerOtaTimeoutTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - LED: %lu total, %lu minimum free (%.1f%%)",
        info->ledTaskInfo.allocatedStack, 
        info->ledTaskInfo.minimumFreeStack, 
        info->ledTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - InfluxDB: %lu total, %lu minimum free (%.1f%%)",
        info->influxDbTaskInfo.allocatedStack, 
        info->influxDbTaskInfo.minimumFreeStack, 
        info->influxDbTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Crash Monitor: %lu total, %lu minimum free (%.1f%%)",
        info->crashMonitorTaskInfo.allocatedStack, 
        info->crashMonitorTaskInfo.minimumFreeStack, 
        info->crashMonitorTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Button Handler: %lu total, %lu minimum free (%.1f%%)",
        info->buttonHandlerTaskInfo.allocatedStack, 
        info->buttonHandlerTaskInfo.minimumFreeStack, 
        info->buttonHandlerTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - UDP Log: %lu total, %lu minimum free (%.1f%%)",
        info->udpLogTaskInfo.allocatedStack, 
        info->udpLogTaskInfo.minimumFreeStack, 
        info->udpLogTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Custom WiFi: %lu total, %lu minimum free (%.1f%%)",
        info->customWifiTaskInfo.allocatedStack, 
        info->customWifiTaskInfo.minimumFreeStack, 
        info->customWifiTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - ADE7953 Meter Reading: %lu total, %lu minimum free (%.1f%%)",
        info->ade7953MeterReadingTaskInfo.allocatedStack, 
        info->ade7953MeterReadingTaskInfo.minimumFreeStack, 
        info->ade7953MeterReadingTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - ADE7953 Energy Save: %lu total, %lu minimum free (%.1f%%)",
        info->ade7953EnergySaveTaskInfo.allocatedStack, 
        info->ade7953EnergySaveTaskInfo.minimumFreeStack, 
        info->ade7953EnergySaveTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - ADE7953 Hourly CSV: %lu total, %lu minimum free (%.1f%%)",
        info->ade7953HourlyCsvTaskInfo.allocatedStack, 
        info->ade7953HourlyCsvTaskInfo.minimumFreeStack, 
        info->ade7953HourlyCsvTaskInfo.freePercentage
    );
    LOG_DEBUG("Tasks - Maintenance: %lu total, %lu minimum free (%.1f%%)",
        info->maintenanceTaskInfo.allocatedStack, 
        info->maintenanceTaskInfo.minimumFreeStack, 
        info->maintenanceTaskInfo.freePercentage
    );

    free(info);
    LOG_DEBUG("-------------------------");
}

void updateStatistics() {
    // The only statistic which is (currently) updated manually here is the log count
    statistics.logVerbose = AdvancedLogger::getVerboseCount();
    statistics.logDebug = AdvancedLogger::getDebugCount();
    statistics.logInfo = AdvancedLogger::getInfoCount();
    statistics.logWarning = AdvancedLogger::getWarningCount();
    statistics.logError = AdvancedLogger::getErrorCount();
    statistics.logFatal = AdvancedLogger::getFatalCount();
    statistics.logDropped = AdvancedLogger::getDroppedCount();

    LOG_DEBUG("Statistics updated");
}

void printStatistics() {
    updateStatistics();

    LOG_DEBUG("--- Statistics ---");
    LOG_DEBUG("Statistics - ADE7953: %llu total interrupts | %llu handled interrupts | %llu readings | %llu reading failures",  
        statistics.ade7953TotalInterrupts, 
        statistics.ade7953TotalHandledInterrupts, 
        statistics.ade7953ReadingCount, 
        statistics.ade7953ReadingCountFailure
    );

    LOG_DEBUG("Statistics - MQTT: %llu messages published | %llu errors | %llu connections | %llu connection errors",  
        statistics.mqttMessagesPublished, 
        statistics.mqttMessagesPublishedError,
        statistics.mqttConnections,
        statistics.mqttConnectionErrors
    );

    LOG_DEBUG("Statistics - Custom MQTT: %llu messages published | %llu errors",  
        statistics.customMqttMessagesPublished, 
        statistics.customMqttMessagesPublishedError
    );

    LOG_DEBUG("Statistics - Modbus: %llu requests | %llu errors",  
        statistics.modbusRequests, 
        statistics.modbusRequestsError
    );

    LOG_DEBUG("Statistics - InfluxDB: %llu uploads | %llu errors",  
        statistics.influxdbUploadCount, 
        statistics.influxdbUploadCountError
    );

    LOG_DEBUG("Statistics - WiFi: %llu connections | %llu errors",  
        statistics.wifiConnection, 
        statistics.wifiConnectionError
    );

    LOG_DEBUG("Statistics - Web Server: %llu requests | %llu errors",  
        statistics.webServerRequests, 
        statistics.webServerRequestsError
    );

    LOG_DEBUG("Statistics - Log: %llu verbose | %llu debug | %llu info | %llu warning | %llu error | %llu fatal, %llu dropped",
        statistics.logVerbose, 
        statistics.logDebug, 
        statistics.logInfo, 
        statistics.logWarning, 
        statistics.logError, 
        statistics.logFatal,
        statistics.logDropped
    );
    LOG_DEBUG("-------------------");
}

void statisticsToJson(Statistics& statistics, JsonDocument &jsonDocument) {
    // Update to have latest values
    updateStatistics();

    // ADE7953 statistics
    jsonDocument["ade7953"]["totalInterrupts"] = statistics.ade7953TotalInterrupts;
    jsonDocument["ade7953"]["totalHandledInterrupts"] = statistics.ade7953TotalHandledInterrupts;
    jsonDocument["ade7953"]["readingCount"] = statistics.ade7953ReadingCount;
    jsonDocument["ade7953"]["readingCountFailure"] = statistics.ade7953ReadingCountFailure;

    // MQTT statistics
    jsonDocument["mqtt"]["messagesPublished"] = statistics.mqttMessagesPublished;
    jsonDocument["mqtt"]["messagesPublishedError"] = statistics.mqttMessagesPublishedError;
    jsonDocument["mqtt"]["connections"] = statistics.mqttConnections;
    jsonDocument["mqtt"]["connectionErrors"] = statistics.mqttConnectionErrors;

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
    jsonDocument["log"]["dropped"] = statistics.logDropped;

    LOG_VERBOSE("Statistics converted to JSON");
}

// Helper functions
// -----------------------------

static void _factoryReset() { // No logger here it is likely destroyed already
    Serial.println("[WARNING] Factory reset requested");

    Led::setBrightness(max(Led::getBrightness(), (uint8_t)1)); // Show a faint light even if it is off
    Led::blinkRedFast(Led::PRIO_CRITICAL);

    clearAllPreferences(false);

    Serial.println("[WARNING] Formatting LittleFS. This will take some time.");
    LittleFS.format();

    // Removed ESP.restart() call since the factory reset can only be called from the restart task
}

bool isFirstBootDone() {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_GENERAL, true)) {
        LOG_DEBUG("Could not open preferences namespace: %s. Assuming first boot", PREFERENCES_NAMESPACE_GENERAL);
        return false;
    }
    bool firstBoot = preferences.getBool(IS_FIRST_BOOT_DONE_KEY, false);
    preferences.end();

    return firstBoot;
}

void setFirstBootDone() { // No arguments because the only way to set first boot done to false it through a complete wipe - thus automatically setting it to "false"
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
        LOG_ERROR("Failed to open preferences namespace: %s", PREFERENCES_NAMESPACE_GENERAL);
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

    LOG_DEBUG("All namespaces created");
}

void clearAllPreferences(bool nuclearOption) {
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
    
    if (nuclearOption) nvs_flash_erase(); // Nuclear solution. In development, the NVS can get overcrowded with test data, so we clear it completely (losing also WiFi credentials, etc.)

    LOG_WARNING("Cleared all preferences");
}

void getDeviceId(char* deviceId, size_t maxLength) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    // Use lowercase hex formatting without colons
    snprintf(deviceId, maxLength, "%02x%02x%02x%02x%02x%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool readEfuseProvisioningData(EfuseProvisioningData& data) {
    // Read 32 bytes from BLOCK_USR_DATA (USER_DATA eFuse block)
    uint8_t efuseData[32];
    
    #include <esp_efuse.h>
    
    // Read the user data block
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, efuseData, sizeof(efuseData) * 8);
    
    if (err != ESP_OK) {
        LOG_DEBUG("Failed to read eFuse user data: %s", esp_err_to_name(err));
        data.isProvisioned = false;
        return false;
    }
    
    // Check provisioning flag at byte 0
    if (efuseData[0] != 0x01) {
        LOG_DEBUG("Device not provisioned (flag: 0x%02X)", efuseData[0]);
        data.isProvisioned = false;
        return false;
    }
    
    // Parse data structure (matching Python script format)
    data.isProvisioned = true;
    data.serial = *((uint32_t*)&efuseData[4]);           // Bytes 4-7: Serial (little-endian)
    data.manufacturingDate = *((uint64_t*)&efuseData[8]); // Bytes 8-15: Manufacturing date (little-endian)
    data.hardwareVersion = *((uint16_t*)&efuseData[16]);  // Bytes 16-17: Hardware version (little-endian)
    
    LOG_DEBUG("eFuse provisioning data read: serial=0x%08X, mfgDate=%llu, hwVer=%u", 
              data.serial, data.manufacturingDate, data.hardwareVersion);
    
    return true;
}

uint64_t calculateExponentialBackoff(uint64_t attempt, uint64_t initialInterval, uint64_t maxInterval, uint64_t multiplier) {
    if (attempt == 0) return 0;
    
    // Direct calculation using bit shifting for power of 2 multipliers
    if (multiplier == 2) {
        // For multiplier=2, use bit shifting: delay = initial * 2^(attempt-1)
        if (attempt >= 64) return maxInterval; // Prevent overflow
        uint64_t backoffDelay = initialInterval << (attempt - 1);
        return min(backoffDelay, maxInterval);
    }
    
    // General case: calculate multiplier^(attempt-1)
    uint64_t backoffDelay = initialInterval;
    for (uint64_t i = 1; i < attempt; ++i) {
        // Check for overflow before multiplication
        if (backoffDelay > maxInterval / multiplier) {
            return maxInterval;
        }
        backoffDelay *= multiplier;
    }
    
    return min(backoffDelay, maxInterval);
}
    
// === LittleFS FILE OPERATIONS ===

bool listLittleFsFiles(JsonDocument &doc, const char* folderPath) {
    if (folderPath && strlen(folderPath) > 0) {
        // Ensure folder path starts with /
        char normalizedPath[NAME_BUFFER_SIZE];
        if (folderPath[0] != '/') {
            snprintf(normalizedPath, sizeof(normalizedPath), "/%s", folderPath);
        } else {
            snprintf(normalizedPath, sizeof(normalizedPath), "%s", folderPath);
        }
        
        // Check if the folder exists
        if (!LittleFS.exists(normalizedPath)) {
            LOG_DEBUG("Folder does not exist: %s", normalizedPath);
            return true; // Return true with empty doc - not an error
        }
        
        return _listLittleFsFilesRecursive(doc, normalizedPath, normalizedPath, 0);
    }
    return _listLittleFsFilesRecursive(doc, "/", nullptr, 0);
}

static bool _listLittleFsFilesRecursive(JsonDocument &doc, const char* dirname, const char* basePath, uint8_t levels) {
    File root = LittleFS.open(dirname);
    if (!root) {
        LOG_ERROR("Failed to open LittleFS directory: %s", dirname);
        return false;
    }
    
    if (!root.isDirectory()) {
        LOG_ERROR("Path is not a directory: %s", dirname);
        root.close();
        return false;
    }

    File file = root.openNextFile();
    uint32_t loops = 0;
    
    while (file && loops < MAX_LOOP_ITERATIONS) {
        loops++;
        const char* filepath = file.path();

        if (file.isDirectory()) {
            // Recursively list subdirectory contents (limit depth to prevent infinite recursion)
            if (levels < 5) {
                _listLittleFsFilesRecursive(doc, filepath, basePath, levels + 1);
            }
        } else {
            const char* displayPath = filepath;
            
            // If we have a basePath, make paths relative to it
            if (basePath) {
                size_t baseLen = strlen(basePath);
                if (strncmp(filepath, basePath, baseLen) == 0) {
                    displayPath = filepath + baseLen;
                    // Skip leading slash after base path
                    if (displayPath[0] == '/') displayPath++;
                }
            } else {
                // Remove leading slash for consistency (global listing)
                if (filepath[0] == '/') displayPath++;
            }
            
            // Add file with its size to the JSON document
            doc[displayPath] = file.size();
        }
        
        file = root.openNextFile();
    }

    root.close();
    return true;
}

bool getLittleFsFileContent(const char* filepath, char* buffer, size_t bufferSize) {
    if (!filepath || !buffer || bufferSize == 0) {
        LOG_ERROR("Invalid arguments provided");
        return false;
    }
    
    // Check if file exists
    if (!LittleFS.exists(filepath)) {
        LOG_DEBUG("File not found: %s", filepath);
        return false;
    }
    
    File file = LittleFS.open(filepath, FILE_READ);
    if (!file) {
        LOG_ERROR("Failed to open file: %s", filepath);
        return false;
    }

    size_t bytesRead = file.readBytes(buffer, bufferSize - 1);
    buffer[bytesRead] = '\0';  // Null-terminate the string
    file.close();

    LOG_DEBUG("Successfully read file: %s (%d bytes)", filepath, bytesRead);
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
        extension[i] = (char)tolower(ext[i]);
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
    if (strcmp(extension, ".gz") == 0) return "application/gzip";
    
    return "application/octet-stream";
}

bool compressFile(const char* filepath) {
    if (!filepath) {
        LOG_ERROR("Invalid file path");
        return false;
    }

    char sourcePath[NAME_BUFFER_SIZE];
    char destinationPath[NAME_BUFFER_SIZE + 3];      // Plus .gz
    char tempPath[NAME_BUFFER_SIZE + 7];      // Plus .gz.tmp

    snprintf(sourcePath, sizeof(sourcePath), "%s", filepath);
    snprintf(destinationPath, sizeof(destinationPath), "%s.gz", sourcePath);
    snprintf(tempPath, sizeof(tempPath), "%s.gz.tmp", sourcePath);

    if (!LittleFS.exists(sourcePath)) {
        LOG_WARNING("No finished csv to compress: %s", sourcePath);
        return false;
    }

    // Remove any existing .gz.tmp file before starting
    if (LittleFS.exists(tempPath)) {
        LOG_DEBUG("Found existing temp file %s. Removing it", tempPath);
        if (!LittleFS.remove(tempPath)) {
            LOG_ERROR("Failed to remove existing temp file: %s", tempPath);
            return false;
        }
    }

    // Remove any existing .gz file before renaming (atomic replace)
    if (LittleFS.exists(destinationPath)) {
        LOG_DEBUG("Found existing compressed file %s. Removing it", destinationPath);
        if (!LittleFS.remove(destinationPath)) {
            LOG_ERROR("Failed to remove existing compressed file: %s", destinationPath);
            return false;
        }
    }

    File srcFile = LittleFS.open(sourcePath, FILE_READ);
    if (!srcFile) {
        LOG_ERROR("Failed to open source file: %s", sourcePath);
        return false;
    }
    size_t sourceSize = srcFile.size();

    File tempFile = LittleFS.open(tempPath, FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to open temporary file: %s", tempPath);
        srcFile.close();
        return false;
    }

    size_t compressedSize = LZPacker::compress(&srcFile, sourceSize, &tempFile);
    srcFile.close();
    tempFile.close();
    
    if (compressedSize > 0) {
        LOG_DEBUG("Compressed finished CSV %s (%zu bytes) -> %s (%zu bytes)", sourcePath, sourceSize, tempPath, compressedSize);

        // Rename temp file to final .gz name
        if (!LittleFS.rename(tempPath, destinationPath)) {
            LOG_ERROR("Failed to rename temporary file %s to final %s", tempPath, destinationPath);
            // Clean up temp file
            LittleFS.remove(tempPath);
            return false;
        }

        if (!LittleFS.remove(sourcePath)) {
            LOG_WARNING("Could not delete original %s after compression", sourcePath);
            return false; // Compression succeeded, but cleanup failed - treat as failure
        }
    } else {
        LOG_ERROR("Failed to compress finished CSV %s", sourcePath);
        // Clean up temp file if created
        LittleFS.remove(tempPath);
        return false;
    }

    LOG_DEBUG("Successfully compressed %s (%zu bytes) to %s (%zu bytes)", sourcePath, sourceSize, destinationPath, compressedSize);
    return true;
}

void migrateCsvToGzip(const char* dirPath, const char* excludePrefix) {
    LOG_DEBUG("Starting CSV -> gzip migration in %s", dirPath);

    if (!LittleFS.exists(dirPath)) {
        LOG_DEBUG("Energy folder not present, nothing to migrate");
        return;
    }

    File dir = LittleFS.open(dirPath);
    if (!dir) {
        LOG_WARNING("Cannot open dir %s", dirPath);
        return;
    }
    dir.rewindDirectory();

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const char* path = file.name();
            char fullPath[NAME_BUFFER_SIZE];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, path);

            if (excludePrefix && startsWith(fullPath, excludePrefix)) {
                LOG_DEBUG("Skipping file %s due to exclude prefix", fullPath);
                file.close(); // Close file handle before continuing
                file = dir.openNextFile();
                continue;
            }

            if (endsWith(fullPath, ".csv")) {
                file.close(); // Close file handle before attempting compression/deletion
                LOG_DEBUG("Migrating %s -> %s.gz", fullPath, fullPath);
                if (compressFile(fullPath)) {
                    LOG_INFO("Compressed and removed original %s", fullPath);
                } else {
                    LOG_ERROR("Compression failed for %s", fullPath);
                }
            } else {
                file.close(); // Close file handle if not processing
            }
        } else {
            file.close(); // Close directory handle
        }
        file = dir.openNextFile();
    }
    dir.close();

    LOG_DEBUG("CSV -> gzip migration finished");
}

// === ENERGY FILE CONSOLIDATION ===

static bool _ensureDirectoryExists(const char* dirPath) {
    if (LittleFS.exists(dirPath)) return true;
    
    if (LittleFS.mkdir(dirPath)) {
        LOG_DEBUG("Created directory: %s", dirPath);
        return true;
    }
    
    LOG_ERROR("Failed to create directory: %s", dirPath);
    return false;
}

static bool _decompressGzipFile(const char* gzPath, const char* outputPath) {
    if (!LittleFS.exists(gzPath)) {
        LOG_WARNING("Gzip file not found: %s", gzPath);
        return false;
    }
    
    GzUnpacker *unpacker = new GzUnpacker();
    if (!unpacker) {
        LOG_ERROR("Failed to allocate GzUnpacker");
        return false;
    }
    
    unpacker->haltOnError(false);
    
    bool success = unpacker->gzExpander(LittleFS, gzPath, LittleFS, outputPath);
    if (!success) {
        LOG_ERROR("Failed to decompress %s (error %d)", gzPath, unpacker->tarGzGetError());
    }
    
    delete unpacker;
    return success;
}

static bool _appendFileToFile(const char* srcPath, File &destFile, bool skipHeader) {
    File srcFile = LittleFS.open(srcPath, FILE_READ);
    if (!srcFile) {
        LOG_ERROR("Failed to open source file: %s", srcPath);
        return false;
    }
    
    // Skip header line if requested
    if (skipHeader && srcFile.available()) {
        // Read and discard the first line (header)
        while (srcFile.available()) {
            char c = static_cast<char>(srcFile.read());
            if (c == '\n') break;
        }
    }
    
    // Copy remaining content
    uint8_t buffer[512];
    while (srcFile.available()) {
        size_t bytesRead = srcFile.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            destFile.write(buffer, bytesRead);
        }
    }
    
    srcFile.close();
    return true;
}

bool migrateEnergyFilesToDailyFolder() {
    LOG_DEBUG("Starting energy files migration to daily folder");
    
    // Ensure base energy directory exists
    if (!_ensureDirectoryExists(ENERGY_CSV_PREFIX)) return false;
    
    // Check if there are any files in the root energy folder to migrate
    File dir = LittleFS.open(ENERGY_CSV_PREFIX);
    if (!dir) {
        LOG_DEBUG("Energy folder not present, nothing to migrate");
        return true;
    }
    
    // Ensure daily subdirectory exists
    if (!_ensureDirectoryExists(ENERGY_CSV_DAILY_PREFIX)) {
        dir.close();
        return false;
    }
    
    dir.rewindDirectory();
    uint32_t migratedCount = 0;
    uint32_t loops = 0;
    
    File file = dir.openNextFile();
    while (file && loops < MAX_LOOP_ITERATIONS) {
        loops++;
        
        if (!file.isDirectory()) {
            const char* filename = file.name();
            
            // Only migrate .csv.gz files (daily energy files)
            if (endsWith(filename, ".csv.gz")) {
                char srcPath[NAME_BUFFER_SIZE];
                char destPath[NAME_BUFFER_SIZE];
                snprintf(srcPath, sizeof(srcPath), "%s/%s", ENERGY_CSV_PREFIX, filename);
                snprintf(destPath, sizeof(destPath), "%s/%s", ENERGY_CSV_DAILY_PREFIX, filename);
                
                file.close(); // Close before rename
                
                if (LittleFS.rename(srcPath, destPath)) {
                    LOG_DEBUG("Migrated %s -> %s", srcPath, destPath);
                    migratedCount++;
                } else {
                    LOG_ERROR("Failed to migrate %s", srcPath);
                }
                
                file = dir.openNextFile();
                continue;
            }
            // Also migrate uncompressed .csv files (except today's)
            else if (endsWith(filename, ".csv")) {
                char srcPath[NAME_BUFFER_SIZE];
                char destPath[NAME_BUFFER_SIZE];
                snprintf(srcPath, sizeof(srcPath), "%s/%s", ENERGY_CSV_PREFIX, filename);
                snprintf(destPath, sizeof(destPath), "%s/%s", ENERGY_CSV_DAILY_PREFIX, filename);
                
                file.close(); // Close before rename
                
                if (LittleFS.rename(srcPath, destPath)) {
                    LOG_DEBUG("Migrated %s -> %s", srcPath, destPath);
                    migratedCount++;
                } else {
                    LOG_ERROR("Failed to migrate %s", srcPath);
                }
                
                file = dir.openNextFile();
                continue;
            }
        }
        
        file.close();
        file = dir.openNextFile();
    }
    
    dir.close();
    
    if (migratedCount > 0) {
        LOG_INFO("Migrated %lu energy files to daily folder", migratedCount);
    } else {
        LOG_DEBUG("No energy files needed migration");
    }
    
    return true;
}

bool consolidateDailyFilesToMonthly(const char* yearMonth, const char* excludeDate) {
    if (!yearMonth || strlen(yearMonth) != 7) { // Format: YYYY-MM
        LOG_ERROR("Invalid yearMonth format: %s (expected YYYY-MM)", yearMonth ? yearMonth : "null");
        return false;
    }
    
    LOG_DEBUG("Starting daily -> monthly consolidation for %s (excluding: %s)", yearMonth, excludeDate ? excludeDate : "none");
    
    // Ensure monthly directory exists
    if (!_ensureDirectoryExists(ENERGY_CSV_MONTHLY_PREFIX)) return false;
    
    // Check if daily folder exists
    if (!LittleFS.exists(ENERGY_CSV_DAILY_PREFIX)) {
        LOG_DEBUG("Daily folder does not exist, nothing to consolidate");
        return true;
    }
    
    // Prepare paths
    char monthlyTempPath[NAME_BUFFER_SIZE];
    char monthlyFinalPath[NAME_BUFFER_SIZE];
    char monthlyGzPath[NAME_BUFFER_SIZE];
    char monthlyGzTempPath[NAME_BUFFER_SIZE];
    snprintf(monthlyTempPath, sizeof(monthlyTempPath), "%s/%s.csv.tmp", ENERGY_CSV_MONTHLY_PREFIX, yearMonth);
    snprintf(monthlyFinalPath, sizeof(monthlyFinalPath), "%s/%s.csv", ENERGY_CSV_MONTHLY_PREFIX, yearMonth);
    snprintf(monthlyGzPath, sizeof(monthlyGzPath), "%s/%s.csv.gz", ENERGY_CSV_MONTHLY_PREFIX, yearMonth);
    snprintf(monthlyGzTempPath, sizeof(monthlyGzTempPath), "%s/%s.csv.gz.tmp", ENERGY_CSV_MONTHLY_PREFIX, yearMonth);
    
    // Check if monthly archive already exists - we'll need to decompress and append
    bool existingArchive = LittleFS.exists(monthlyGzPath);
    if (existingArchive) {
        LOG_DEBUG("Monthly archive already exists for %s, will append new daily files", yearMonth);
    }
    
    // Clean up any existing temp files from previous failed attempts
    if (LittleFS.exists(monthlyTempPath)) LittleFS.remove(monthlyTempPath);
    if (LittleFS.exists(monthlyGzTempPath)) LittleFS.remove(monthlyGzTempPath);
    
    // Collect matching daily files (excluding the specified date if provided)
    std::vector<char*> dailyFiles;
    File dir = LittleFS.open(ENERGY_CSV_DAILY_PREFIX);
    if (!dir) {
        LOG_ERROR("Failed to open daily folder");
        return false;
    }
    
    dir.rewindDirectory();
    uint32_t loops = 0;
    
    File file = dir.openNextFile();
    while (file && loops < MAX_LOOP_ITERATIONS) {
        loops++;
        
        if (!file.isDirectory()) {
            const char* filename = file.name();
            
            // Check if file matches the month pattern (YYYY-MM-DD.csv.gz)
            if (endsWith(filename, ".csv.gz") && strncmp(filename, yearMonth, 7) == 0) {
                // Check if this date should be excluded (e.g., today's date)
                if (excludeDate && strncmp(filename, excludeDate, 10) == 0) {
                    LOG_DEBUG("Skipping excluded date: %s", filename);
                    file.close();
                    file = dir.openNextFile();
                    continue;
                }
                
                char* fileCopy = (char*)ps_malloc(strlen(filename) + 1);
                if (fileCopy) {
                    strcpy(fileCopy, filename);
                    dailyFiles.push_back(fileCopy);
                } else {
                    LOG_WARNING("Failed to allocate %zu bytes from PSRAM for filename: %s (free PSRAM: %lu)", 
                                strlen(filename) + 1, filename, ESP.getFreePsram());
                }
            }
        }
        
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    if (dailyFiles.empty()) {
        LOG_DEBUG("No daily files found for %s", yearMonth);
        return true;
    }
    
    LOG_DEBUG("Found %d daily files for %s", dailyFiles.size(), yearMonth);
    
    // Sort files by name (chronological order)
    std::sort(dailyFiles.begin(), dailyFiles.end(), [](const char* a, const char* b) {
        return strcmp(a, b) < 0;
    });
    
    // Create temporary consolidated CSV file
    File tempFile = LittleFS.open(monthlyTempPath, FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to create temp file: %s", monthlyTempPath);
        for (auto f : dailyFiles) free(f);
        return false;
    }
    
    bool success = true;
    
    // If existing archive exists, decompress it and copy content first (skip writing new header)
    if (existingArchive) {
        char existingCsvPath[NAME_BUFFER_SIZE];
        snprintf(existingCsvPath, sizeof(existingCsvPath), "%s/_existing_monthly.csv", ENERGY_CSV_MONTHLY_PREFIX);
        
        // Clean up any existing temp file
        if (LittleFS.exists(existingCsvPath)) LittleFS.remove(existingCsvPath);
        
        // Decompress existing archive
        if (_decompressGzipFile(monthlyGzPath, existingCsvPath)) {
            // Append existing content (including header since it's the first content)
            if (!_appendFileToFile(existingCsvPath, tempFile, false)) {
                LOG_ERROR("Failed to append existing monthly archive content");
                success = false;
            }
            LittleFS.remove(existingCsvPath);
        } else {
            LOG_ERROR("Failed to decompress existing monthly archive");
            success = false;
        }
        
        if (!success) {
            tempFile.close();
            LittleFS.remove(monthlyTempPath);
            for (auto f : dailyFiles) free(f);
            return false;
        }
    } else {
        // Write header once (only for new archives)
        tempFile.println(DAILY_ENERGY_CSV_HEADER);
    }
    
    std::vector<char*> processedFiles;
    
    for (size_t i = 0; i < dailyFiles.size() && success; i++) {
        char gzPath[NAME_BUFFER_SIZE];
        char csvPath[NAME_BUFFER_SIZE];
        snprintf(gzPath, sizeof(gzPath), "%s/%s", ENERGY_CSV_DAILY_PREFIX, dailyFiles[i]);
        
        // Create temp path for decompressed CSV
        char tempCsvName[NAME_BUFFER_SIZE];
        snprintf(tempCsvName, sizeof(tempCsvName), "%s/_temp_decomp.csv", ENERGY_CSV_DAILY_PREFIX);
        snprintf(csvPath, sizeof(csvPath), "%s", tempCsvName);
        
        // Clean up any existing temp CSV
        if (LittleFS.exists(csvPath)) LittleFS.remove(csvPath);
        
        // Decompress the daily file
        if (!_decompressGzipFile(gzPath, csvPath)) {
            LOG_ERROR("Failed to decompress %s", gzPath);
            success = false;
            break;
        }
        
        // Append to consolidated file (skip header)
        if (!_appendFileToFile(csvPath, tempFile, true)) {
            LOG_ERROR("Failed to append %s to consolidated file", dailyFiles[i]);
            LittleFS.remove(csvPath);
            success = false;
            break;
        }
        
        // Remove temp decompressed file
        LittleFS.remove(csvPath);
        
        processedFiles.push_back(dailyFiles[i]);
        LOG_VERBOSE("Consolidated %s", dailyFiles[i]);
    }
    
    tempFile.close();
    
    if (!success) {
        LittleFS.remove(monthlyTempPath);
        for (auto f : dailyFiles) free(f);
        return false;
    }
    
    // Verify temp file has content
    File verifyFile = LittleFS.open(monthlyTempPath, FILE_READ);
    if (!verifyFile || verifyFile.size() < ENERGY_CONSOLIDATION_MIN_SIZE) {
        LOG_ERROR("Consolidated temp file too small or invalid");
        if (verifyFile) verifyFile.close();
        LittleFS.remove(monthlyTempPath);
        for (auto f : dailyFiles) free(f);
        return false;
    }
    verifyFile.close();
    
    // Rename temp to final CSV
    if (!LittleFS.rename(monthlyTempPath, monthlyFinalPath)) {
        LOG_ERROR("Failed to rename temp file to final CSV");
        LittleFS.remove(monthlyTempPath);
        for (auto f : dailyFiles) free(f);
        return false;
    }
    
    // Compress the consolidated CSV
    if (!compressFile(monthlyFinalPath)) {
        LOG_ERROR("Failed to compress consolidated file");
        for (auto f : dailyFiles) free(f);
        return false;
    }
    
    // Verify compressed file exists and has content
    File verifyGz = LittleFS.open(monthlyGzPath, FILE_READ);
    if (!verifyGz || verifyGz.size() < ENERGY_CONSOLIDATION_MIN_SIZE) {
        LOG_ERROR("Compressed file too small or invalid");
        if (verifyGz) verifyGz.close();
        for (auto f : dailyFiles) free(f);
        return false;
    }
    size_t finalSize = verifyGz.size();
    verifyGz.close();
    
    // Delete original daily files
    uint32_t deletedCount = 0;
    for (auto filename : processedFiles) {
        char gzPath[NAME_BUFFER_SIZE];
        snprintf(gzPath, sizeof(gzPath), "%s/%s", ENERGY_CSV_DAILY_PREFIX, filename);
        if (LittleFS.remove(gzPath)) {
            deletedCount++;
        } else {
            LOG_WARNING("Failed to delete %s after consolidation", gzPath);
        }
    }
    
    // Cleanup
    for (auto f : dailyFiles) free(f);
    
    LOG_INFO("Consolidated %d daily files for %s into %zu bytes (%lu deleted)", 
             processedFiles.size(), yearMonth, finalSize, deletedCount);
    
    return true;
}

bool consolidateMonthlyFilesToYearly(const char* year, const char* excludeMonth) {
    if (!year || strlen(year) != 4) { // Format: YYYY
        LOG_ERROR("Invalid year format: %s (expected YYYY)", year ? year : "null");
        return false;
    }
    
    LOG_DEBUG("Starting monthly -> yearly consolidation for %s (excluding: %s)", year, excludeMonth ? excludeMonth : "none");
    
    // Ensure yearly directory exists
    if (!_ensureDirectoryExists(ENERGY_CSV_YEARLY_PREFIX)) return false;
    
    // Check if monthly folder exists
    if (!LittleFS.exists(ENERGY_CSV_MONTHLY_PREFIX)) {
        LOG_DEBUG("Monthly folder does not exist, nothing to consolidate");
        return true;
    }
    
    // Prepare paths
    char yearlyTempPath[NAME_BUFFER_SIZE];
    char yearlyFinalPath[NAME_BUFFER_SIZE];
    char yearlyGzPath[NAME_BUFFER_SIZE];
    snprintf(yearlyTempPath, sizeof(yearlyTempPath), "%s/%s.csv.tmp", ENERGY_CSV_YEARLY_PREFIX, year);
    snprintf(yearlyFinalPath, sizeof(yearlyFinalPath), "%s/%s.csv", ENERGY_CSV_YEARLY_PREFIX, year);
    snprintf(yearlyGzPath, sizeof(yearlyGzPath), "%s/%s.csv.gz", ENERGY_CSV_YEARLY_PREFIX, year);
    
    // Check if yearly archive already exists - we'll need to decompress and append
    bool existingArchive = LittleFS.exists(yearlyGzPath);
    if (existingArchive) {
        LOG_DEBUG("Yearly archive already exists for %s, will append new monthly files", year);
    }
    
    // Clean up any existing temp files
    if (LittleFS.exists(yearlyTempPath)) LittleFS.remove(yearlyTempPath);
    
    // Collect matching monthly files (excluding the specified month if provided)
    std::vector<char*> monthlyFiles;
    File dir = LittleFS.open(ENERGY_CSV_MONTHLY_PREFIX);
    if (!dir) {
        LOG_ERROR("Failed to open monthly folder");
        return false;
    }
    
    dir.rewindDirectory();
    uint32_t loops = 0;
    
    File file = dir.openNextFile();
    while (file && loops < MAX_LOOP_ITERATIONS) {
        loops++;
        
        if (!file.isDirectory()) {
            const char* filename = file.name();
            
            // Check if file matches the year pattern (YYYY-MM.csv.gz)
            if (endsWith(filename, ".csv.gz") && strncmp(filename, year, 4) == 0) {
                // Check if this month should be excluded (e.g., current month)
                if (excludeMonth && strncmp(filename, excludeMonth, 7) == 0) {
                    LOG_DEBUG("Skipping excluded month: %s", filename);
                    file.close();
                    file = dir.openNextFile();
                    continue;
                }
                
                char* fileCopy = (char*)ps_malloc(strlen(filename) + 1);
                if (fileCopy) {
                    strcpy(fileCopy, filename);
                    monthlyFiles.push_back(fileCopy);
                } else {
                    LOG_WARNING("Failed to allocate %zu bytes from PSRAM for filename: %s (free PSRAM: %lu)", 
                                strlen(filename) + 1, filename, ESP.getFreePsram());
                }
            }
        }
        
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    if (monthlyFiles.empty()) {
        LOG_DEBUG("No monthly files found for %s", year);
        return true;
    }
    
    LOG_DEBUG("Found %d monthly files for %s", monthlyFiles.size(), year);
    
    // Sort files by name (chronological order)
    std::sort(monthlyFiles.begin(), monthlyFiles.end(), [](const char* a, const char* b) {
        return strcmp(a, b) < 0;
    });
    
    // If existing archive, decompress it first as a starting point
    if (existingArchive) {
        if (!_decompressGzipFile(yearlyGzPath, yearlyTempPath)) {
            LOG_ERROR("Failed to decompress existing yearly archive %s", yearlyGzPath);
            for (auto f : monthlyFiles) free(f);
            return false;
        }
        LOG_DEBUG("Decompressed existing yearly archive for appending");
    }
    
    // Create/append to temporary consolidated CSV file
    File tempFile = LittleFS.open(yearlyTempPath, existingArchive ? FILE_APPEND : FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to open temp file: %s", yearlyTempPath);
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    
    // Write header only if new file
    if (!existingArchive) {
        tempFile.println(DAILY_ENERGY_CSV_HEADER);
    }
    
    bool success = true;
    std::vector<char*> processedFiles;
    
    for (size_t i = 0; i < monthlyFiles.size() && success; i++) {
        char gzPath[NAME_BUFFER_SIZE];
        char csvPath[NAME_BUFFER_SIZE];
        snprintf(gzPath, sizeof(gzPath), "%s/%s", ENERGY_CSV_MONTHLY_PREFIX, monthlyFiles[i]);
        
        // Create temp path for decompressed CSV
        char tempCsvName[NAME_BUFFER_SIZE];
        snprintf(tempCsvName, sizeof(tempCsvName), "%s/_temp_decomp.csv", ENERGY_CSV_MONTHLY_PREFIX);
        snprintf(csvPath, sizeof(csvPath), "%s", tempCsvName);
        
        // Clean up any existing temp CSV
        if (LittleFS.exists(csvPath)) LittleFS.remove(csvPath);
        
        // Decompress the monthly file
        if (!_decompressGzipFile(gzPath, csvPath)) {
            LOG_ERROR("Failed to decompress %s", gzPath);
            success = false;
            break;
        }
        
        // Append to consolidated file (skip header)
        if (!_appendFileToFile(csvPath, tempFile, true)) {
            LOG_ERROR("Failed to append %s to consolidated file", monthlyFiles[i]);
            LittleFS.remove(csvPath);
            success = false;
            break;
        }
        
        // Remove temp decompressed file
        LittleFS.remove(csvPath);
        
        processedFiles.push_back(monthlyFiles[i]);
        LOG_VERBOSE("Consolidated %s", monthlyFiles[i]);
    }
    
    tempFile.close();
    
    if (!success) {
        LittleFS.remove(yearlyTempPath);
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    
    // Verify temp file has content
    File verifyFile = LittleFS.open(yearlyTempPath, FILE_READ);
    if (!verifyFile || verifyFile.size() < ENERGY_CONSOLIDATION_MIN_SIZE) {
        LOG_ERROR("Consolidated temp file too small or invalid");
        if (verifyFile) verifyFile.close();
        LittleFS.remove(yearlyTempPath);
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    verifyFile.close();
    
    // Rename temp to final CSV
    if (!LittleFS.rename(yearlyTempPath, yearlyFinalPath)) {
        LOG_ERROR("Failed to rename temp file to final CSV");
        LittleFS.remove(yearlyTempPath);
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    
    // Compress the consolidated CSV
    if (!compressFile(yearlyFinalPath)) {
        LOG_ERROR("Failed to compress consolidated file");
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    
    // Verify compressed file exists and has content
    File verifyGz = LittleFS.open(yearlyGzPath, FILE_READ);
    if (!verifyGz || verifyGz.size() < ENERGY_CONSOLIDATION_MIN_SIZE) {
        LOG_ERROR("Compressed file too small or invalid");
        if (verifyGz) verifyGz.close();
        for (auto f : monthlyFiles) free(f);
        return false;
    }
    size_t finalSize = verifyGz.size();
    verifyGz.close();
    
    // Delete original monthly files
    uint32_t deletedCount = 0;
    for (auto filename : processedFiles) {
        char gzPath[NAME_BUFFER_SIZE];
        snprintf(gzPath, sizeof(gzPath), "%s/%s", ENERGY_CSV_MONTHLY_PREFIX, filename);
        if (LittleFS.remove(gzPath)) {
            deletedCount++;
        } else {
            LOG_WARNING("Failed to delete %s after consolidation", gzPath);
        }
    }
    
    // Cleanup
    for (auto f : monthlyFiles) free(f);
    
    LOG_INFO("Consolidated %d monthly files for %s into %zu bytes (%lu deleted)", 
             processedFiles.size(), year, finalSize, deletedCount);
    
    return true;
}