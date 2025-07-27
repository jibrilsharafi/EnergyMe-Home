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
    
    // SDK info
    snprintf(info.sdkVersion, sizeof(info.sdkVersion), "%s", ESP.getSdkVersion());
    snprintf(info.coreVersion, sizeof(info.coreVersion), "%s", ESP.getCoreVersion());
    
    // Device ID
    getDeviceId(info.deviceId, sizeof(info.deviceId));
    
    // Firmware state
    info.firmwareState = CrashMonitor::getFirmwareStatus();
    snprintf(info.firmwareStateString, sizeof(info.firmwareStateString), "%s", 
             CrashMonitor::getFirmwareStatusString(info.firmwareState));

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
    
    // Crash monitoring data
    info.crashCount = CrashMonitor::getCrashCount();
    info.resetCount = CrashMonitor::getResetCount();
    
    // Get crash data if available
    CrashData crashData;
    if (CrashMonitor::getSavedCrashData(crashData)) {
        info.lastResetReason = crashData.lastResetReason;
        snprintf(info.lastResetReasonString, sizeof(info.lastResetReasonString), "%s", 
                CrashMonitor::getResetReasonString((esp_reset_reason_t)crashData.lastResetReason));
    } else {
        info.lastResetReason = (uint32_t)esp_reset_reason();
        snprintf(info.lastResetReasonString, sizeof(info.lastResetReasonString), "%s",
                CrashMonitor::getResetReasonString(esp_reset_reason()));
    }
    
    info.hasCoreDump = CrashMonitor::hasCoreDump();
    info.coreDumpSize = info.hasCoreDump ? CrashMonitor::getCoreDumpSize() : 0;

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
    
    // SDK
    doc["sdk"]["sdkVersion"] = info.sdkVersion;
    doc["sdk"]["coreVersion"] = info.coreVersion;
    
    // Device
    doc["device"]["id"] = info.deviceId;
    
    // Firmware state
    doc["firmware"]["state"] = info.firmwareState;
    doc["firmware"]["stateString"] = CrashMonitor::getFirmwareStatusString(info.firmwareState);

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
    
    // Crash monitoring
    doc["monitoring"]["crashCount"] = info.crashCount;
    doc["monitoring"]["resetCount"] = info.resetCount;
    doc["monitoring"]["lastResetReason"] = info.lastResetReason;
    doc["monitoring"]["lastResetReasonString"] = info.lastResetReasonString;
    doc["monitoring"]["hasCoreDump"] = info.hasCoreDump;
    doc["monitoring"]["coreDumpSize"] = info.coreDumpSize;

    logger.debug("Dynamic system info converted to JSON", TAG);
}

// Convenience functions for API endpoints
void getJsonDeviceStaticInfo(JsonDocument& doc) {
    logger.debug("Getting static device info JSON...", TAG);
    SystemStaticInfo info;
    populateSystemStaticInfo(info);
    systemStaticInfoToJson(info, doc);
    logger.debug("Static device info JSON retrieved", TAG);
}

void getJsonDeviceDynamicInfo(JsonDocument& doc) {
    logger.debug("Getting dynamic device info JSON...", TAG);
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);
    systemDynamicInfoToJson(info, doc);
    logger.debug("Dynamic device info JSON retrieved", TAG);
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

bool loadConfigFromPreferences(const char* configType, JsonDocument& jsonDocument) {
    logger.debug("Loading %s configuration from Preferences", TAG, configType);

    // Route to appropriate PreferencesConfig functions based on config type
    if (strcmp(configType, "ade7953") == 0) {
        jsonDocument["sampleTime"] = PreferencesConfig::getSampleTime();
        jsonDocument["aVGain"] = PreferencesConfig::getVoltageGain();
        jsonDocument["aIGain"] = PreferencesConfig::getCurrentGainA();
        jsonDocument["bIGain"] = PreferencesConfig::getCurrentGainB();
        
        // Load additional ADE7953 parameters directly from Preferences
        Preferences prefs;
        if (prefs.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            jsonDocument["aIRmsOs"] = prefs.getUInt("aIRmsOs", DEFAULT_OFFSET);
            jsonDocument["bIRmsOs"] = prefs.getUInt("bIRmsOs", DEFAULT_OFFSET);
            jsonDocument["aWGain"] = prefs.getUInt("aWGain", DEFAULT_GAIN);
            jsonDocument["bWGain"] = prefs.getUInt("bWGain", DEFAULT_GAIN);
            jsonDocument["aWattOs"] = prefs.getUInt("aWattOs", DEFAULT_OFFSET);
            jsonDocument["bWattOs"] = prefs.getUInt("bWattOs", DEFAULT_OFFSET);
            jsonDocument["aVarGain"] = prefs.getUInt("aVarGain", DEFAULT_GAIN);
            jsonDocument["bVarGain"] = prefs.getUInt("bVarGain", DEFAULT_GAIN);
            jsonDocument["aVarOs"] = prefs.getUInt("aVarOs", DEFAULT_OFFSET);
            jsonDocument["bVarOs"] = prefs.getUInt("bVarOs", DEFAULT_OFFSET);
            jsonDocument["aVaGain"] = prefs.getUInt("aVaGain", DEFAULT_GAIN);
            jsonDocument["bVaGain"] = prefs.getUInt("bVaGain", DEFAULT_GAIN);
            jsonDocument["aVaOs"] = prefs.getUInt("aVaOs", DEFAULT_OFFSET);
            jsonDocument["bVaOs"] = prefs.getUInt("bVaOs", DEFAULT_OFFSET);
            jsonDocument["phCalA"] = prefs.getUInt("phCalA", DEFAULT_PHCAL);
            jsonDocument["phCalB"] = prefs.getUInt("phCalB", DEFAULT_PHCAL);
            prefs.end();
        }
        
    } else if (strcmp(configType, "channels") == 0) {
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            jsonDocument[i]["active"] = PreferencesConfig::getChannelActive(i);
            
            char labelBuffer[64];
            PreferencesConfig::getChannelLabel(i, labelBuffer, sizeof(labelBuffer));
            jsonDocument[i]["label"] = labelBuffer;
            
            jsonDocument[i]["phase"] = PreferencesConfig::getChannelPhase(i);
        }
        
    } else if (strcmp(configType, "mqtt") == 0) {
        jsonDocument["enabled"] = PreferencesConfig::getMqttEnabled();
        jsonDocument["port"] = PreferencesConfig::getMqttPort();
        
        char buffer[256];
        if (PreferencesConfig::getMqttServer(buffer, sizeof(buffer))) {
            jsonDocument["server"] = buffer;
        }
        if (PreferencesConfig::getMqttUsername(buffer, sizeof(buffer))) {
            jsonDocument["username"] = buffer;
        }
        if (PreferencesConfig::getMqttPassword(buffer, sizeof(buffer))) {
            jsonDocument["password"] = buffer;
        }
        
        // Load additional MQTT parameters directly from Preferences
        Preferences prefs;
        if (prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            jsonDocument["clientid"] = prefs.getString("clientid", MQTT_CUSTOM_CLIENTID_DEFAULT);
            jsonDocument["topic"] = prefs.getString("topic", MQTT_CUSTOM_TOPIC_DEFAULT);
            jsonDocument["frequency"] = prefs.getUInt("frequency", MQTT_CUSTOM_FREQUENCY_DEFAULT);
            jsonDocument["useCredentials"] = prefs.getBool("useCredentials", MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT);
            jsonDocument["lastConnectionStatus"] = prefs.getString("lastStatus", "Never attempted");
            jsonDocument["lastConnectionAttemptTimestamp"] = prefs.getString("lastAttempt", "");
            prefs.end();
        }
        
    } else if (strcmp(configType, "general") == 0) {
        char buffer[256];
        if (PreferencesConfig::getTimezone(buffer, sizeof(buffer))) {
            jsonDocument["timezone"] = buffer;
        }
        jsonDocument["sendPowerData"] = PreferencesConfig::getSendPowerData();
        
    } else {
        logger.warning("Unknown config type: %s", TAG, configType);
        return false;
    }

    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, _jsonString, sizeof(_jsonString), true);
    logger.debug("%s configuration loaded from Preferences: %s", TAG, configType, _jsonString);
    return true;
}

bool saveConfigToPreferences(const char* configType, JsonDocument& jsonDocument) {
    logger.debug("Saving %s configuration to Preferences", TAG, configType);

    bool success = true;
    
    // Route to appropriate PreferencesConfig functions based on config type
    if (strcmp(configType, "ade7953") == 0) {
        success &= PreferencesConfig::setSampleTime(jsonDocument["sampleTime"].as<uint32_t>());
        success &= PreferencesConfig::setVoltageGain(jsonDocument["aVGain"].as<uint32_t>());
        success &= PreferencesConfig::setCurrentGainA(jsonDocument["aIGain"].as<uint32_t>());
        success &= PreferencesConfig::setCurrentGainB(jsonDocument["bIGain"].as<uint32_t>());
        
        // Save additional ADE7953 parameters directly to Preferences
        Preferences prefs;
        if (prefs.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            prefs.putUInt("aIRmsOs", jsonDocument["aIRmsOs"].as<uint32_t>());
            prefs.putUInt("bIRmsOs", jsonDocument["bIRmsOs"].as<uint32_t>());
            prefs.putUInt("aWGain", jsonDocument["aWGain"].as<uint32_t>());
            prefs.putUInt("bWGain", jsonDocument["bWGain"].as<uint32_t>());
            prefs.putUInt("aWattOs", jsonDocument["aWattOs"].as<uint32_t>());
            prefs.putUInt("bWattOs", jsonDocument["bWattOs"].as<uint32_t>());
            prefs.putUInt("aVarGain", jsonDocument["aVarGain"].as<uint32_t>());
            prefs.putUInt("bVarGain", jsonDocument["bVarGain"].as<uint32_t>());
            prefs.putUInt("aVarOs", jsonDocument["aVarOs"].as<uint32_t>());
            prefs.putUInt("bVarOs", jsonDocument["bVarOs"].as<uint32_t>());
            prefs.putUInt("aVaGain", jsonDocument["aVaGain"].as<uint32_t>());
            prefs.putUInt("bVaGain", jsonDocument["bVaGain"].as<uint32_t>());
            prefs.putUInt("aVaOs", jsonDocument["aVaOs"].as<uint32_t>());
            prefs.putUInt("bVaOs", jsonDocument["bVaOs"].as<uint32_t>());
            prefs.putUInt("phCalA", jsonDocument["phCalA"].as<uint32_t>());
            prefs.putUInt("phCalB", jsonDocument["phCalB"].as<uint32_t>());
            prefs.end();
        } else {
            success = false;
        }
        
    } else if (strcmp(configType, "channels") == 0) {
        for (uint8_t i = 0; i < CHANNEL_COUNT && i < jsonDocument.size(); i++) {
            success &= PreferencesConfig::setChannelActive(i, jsonDocument[i]["active"].as<bool>());
            success &= PreferencesConfig::setChannelLabel(i, jsonDocument[i]["label"].as<const char*>());
            success &= PreferencesConfig::setChannelPhase(i, jsonDocument[i]["phase"].as<uint8_t>());
        }
        
    } else if (strcmp(configType, "mqtt") == 0) {
        success &= PreferencesConfig::setMqttEnabled(jsonDocument["enabled"].as<bool>());
        success &= PreferencesConfig::setMqttServer(jsonDocument["server"].as<const char*>());
        success &= PreferencesConfig::setMqttPort(jsonDocument["port"].as<uint16_t>());
        success &= PreferencesConfig::setMqttUsername(jsonDocument["username"].as<const char*>());
        success &= PreferencesConfig::setMqttPassword(jsonDocument["password"].as<const char*>());
        
        // Save additional MQTT parameters directly to Preferences
        Preferences prefs;
        if (prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            prefs.putString("clientid", jsonDocument["clientid"].as<String>());
            prefs.putString("topic", jsonDocument["topic"].as<String>());
            prefs.putUInt("frequency", jsonDocument["frequency"].as<uint32_t>());
            prefs.putBool("useCredentials", jsonDocument["useCredentials"].as<bool>());
            prefs.putString("lastStatus", jsonDocument["lastConnectionStatus"].as<String>());
            prefs.putString("lastAttempt", jsonDocument["lastConnectionAttemptTimestamp"].as<String>());
            prefs.end();
        } else {
            success = false;
        }
        
    } else if (strcmp(configType, "general") == 0) {
        success &= PreferencesConfig::setTimezone(jsonDocument["timezone"].as<const char*>());
        success &= PreferencesConfig::setSendPowerData(jsonDocument["sendPowerData"].as<bool>());
        
    } else {
        logger.warning("Unknown config type: %s", TAG, configType);
        return false;
    }

    char _jsonString[JSON_STRING_PRINT_BUFFER_SIZE];
    safeSerializeJson(jsonDocument, _jsonString, sizeof(_jsonString), true);
    logger.debug("%s configuration saved to Preferences: %s", TAG, configType, _jsonString);
    
    if (!success) {
        logger.error("Failed to save %s configuration to Preferences", TAG, configType);
    }
    
    return success;
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

    // Save to Preferences instead of SPIFFS
    saveConfigToPreferences("ade7953", _jsonDocument);

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

void createDefaultChannelDataFile() {
    logger.debug("Creating default channel configuration...", TAG);

    JsonDocument _jsonDocument;
    deserializeJson(_jsonDocument, default_config_channel_json);

    // Save to Preferences instead of SPIFFS
    saveConfigToPreferences("channels", _jsonDocument);

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

    // Save to Preferences instead of SPIFFS
    saveConfigToPreferences("mqtt", _jsonDocument);

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

void setRestartEsp32(const char* functionName, const char* reason) { 
    logger.info("Restart required from function %s. Reason: %s", TAG, functionName, reason);
    
    restartConfiguration.isRequired = true;
    restartConfiguration.requiredAt = millis();
    snprintf(restartConfiguration.functionName, sizeof(restartConfiguration.functionName), "%s", functionName);
    snprintf(restartConfiguration.reason, sizeof(restartConfiguration.reason), "%s", reason);

    //TODO: we should start stopping tasks here

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
    
    Led::block();
    Led::setBrightness(max(Led::getBrightness(), 1)); // Show a faint light even if it is off
    Led::setWhite(true);

    logger.info("Restarting ESP32 from function %s. Reason: %s", TAG, restartConfiguration.functionName, restartConfiguration.reason);

    // If a firmware evaluation is in progress, set the firmware to test again
    FirmwareState _firmwareStatus = CrashMonitor::getFirmwareStatus();
    
    if (_firmwareStatus == TESTING) {
        logger.info("Firmware evaluation is in progress. Setting firmware to test again", TAG);
        
        if (!CrashMonitor::setFirmwareStatus(NEW_TO_TEST)) logger.error("Failed to set firmware status", TAG);
    }

    logger.end();

    // Give time for AsyncTCP connections to close gracefully
    delay(1000);

    // Disable WiFi to prevent AsyncTCP crashes during restart
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Additional delay to ensure clean shutdown
    delay(500);

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

    logger.info("--- Static System Info ---", TAG);
    logger.info("Product: %s (%s)", TAG, info.fullProductName, info.productName);
    logger.info("Company: %s | Author: %s", TAG, info.companyName, info.author);
    logger.info("Firmware: %s | Build: %s %s", TAG, info.buildVersion, info.buildDate, info.buildTime);
    logger.info("Sketch MD5: %s", TAG, info.sketchMD5);
    logger.info("Flash: %lu bytes, %lu Hz", TAG, info.flashChipSizeBytes, info.flashChipSpeedHz);
    logger.info("PSRAM: %lu bytes", TAG, info.psramSizeBytes);
    logger.info("Chip: %s, rev %u, cores %u, id 0x%llx, CPU: %lu MHz", TAG, info.chipModel, info.chipRevision, info.chipCores, info.chipId, info.cpuFrequencyMHz);
    logger.info("SDK: %s | Core: %s", TAG, info.sdkVersion, info.coreVersion);
    logger.info("Device ID: %s", TAG, info.deviceId);
    logger.info("Firmware State: %s", TAG, CrashMonitor::getFirmwareStatusString(info.firmwareState));

    logger.info("------------------------", TAG);
}

void printDeviceStatusDynamic()
{
    SystemDynamicInfo info;
    populateSystemDynamicInfo(info);

    logger.info("--- Dynamic System Info ---", TAG);
    logger.info("Uptime: %lu s (%llu ms)", TAG, info.uptimeSeconds, (unsigned long long)info.uptimeMilliseconds);
    logger.info("Timestamp: %s", TAG, info.currentTimestamp);
    logger.info("Heap: %lu total, %lu free, %lu min free, %lu max alloc", TAG, info.heapTotalBytes, info.heapFreeBytes, info.heapMinFreeBytes, info.heapMaxAllocBytes);
    if (info.psramFreeBytes > 0 || info.psramUsedBytes > 0) {
        logger.info("PSRAM: %lu free, %lu used, %lu min free, %lu max alloc", TAG, info.psramFreeBytes, info.psramUsedBytes, info.psramMinFreeBytes, info.psramMaxAllocBytes);
    }
    logger.info("SPIFFS: %lu total, %lu used, %lu free", TAG, info.spiffsTotalBytes, info.spiffsUsedBytes, info.spiffsFreeBytes);
    logger.info("Temperature: %.2f C", TAG, info.temperatureCelsius);
    
    // WiFi information
    if (info.wifiConnected) {
        logger.info("WiFi: Connected to '%s' (BSSID: %s)", TAG, info.wifiSsid, info.wifiBssid);
        logger.info("WiFi: RSSI %d dBm | MAC %s", TAG, info.wifiRssi, info.wifiMacAddress);
        logger.info("WiFi: IP %s | Gateway %s | DNS %s", TAG, info.wifiLocalIp, info.wifiGatewayIp, info.wifiDnsIp);
        logger.info("WiFi: Subnet %s", TAG, info.wifiSubnetMask);
    } else {
        logger.info("WiFi: Disconnected | MAC %s", TAG, info.wifiMacAddress);
    }
    
    // Crash monitoring info
    logger.info("Monitoring: %lu crashes, %lu resets | Last reset: %s", TAG, 
               info.crashCount, info.resetCount, info.lastResetReasonString);
    if (info.hasCoreDump) {
        logger.info("Monitoring: Core dump available (%zu bytes)", TAG, info.coreDumpSize);
    }
    
    logger.info("-------------------------", TAG);
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
    snprintf(_url, sizeof(_url), "%slat=%f&lng=%f&username=%s",
        PUBLIC_TIMEZONE_ENDPOINT,
        _publicLocation.latitude,
        _publicLocation.longitude,
        PUBLIC_TIMEZONE_USERNAME);

    _http.begin(_url);
    int httpCode = _http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            // Use stream directly - efficient and simple
            DeserializationError error = deserializeJson(_jsonDocument, _http.getStream()); // FIXME: this returns null
            
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

    // Clear the new configuration namespaces
    preferences.begin(PREFERENCES_NAMESPACE_GENERAL, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_ADE7953, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_CHANNELS, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_MQTT, false);
    preferences.clear();
    preferences.end();

    preferences.begin(PREFERENCES_NAMESPACE_INFLUXDB, false);
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

// =============================================================================
// Preferences Configuration Utilities
// =============================================================================

namespace PreferencesConfig {
    static const char* TAG_PREFS = "preferencesconfig";
    
    bool setTimezone(const char* timezone) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
            logger.error("Failed to open general preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putString(PREF_KEY_TIMEZONE, timezone) > 0;
        prefs.end();
        return success;
    }
    
    bool getTimezone(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_GENERAL, true)) {
            logger.error("Failed to open general preferences", TAG_PREFS);
            return false;
        }
        String value = prefs.getString(PREF_KEY_TIMEZONE, "UTC");
        prefs.end();
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    bool setSendPowerData(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_GENERAL, false)) {
            logger.error("Failed to open general preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putBool(PREF_KEY_SEND_POWER_DATA, enabled);
        prefs.end();
        return success;
    }
    
    bool getSendPowerData() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_GENERAL, true)) {
            logger.error("Failed to open general preferences", TAG_PREFS);
            return true; // Default to enabled
        }
        bool value = prefs.getBool(PREF_KEY_SEND_POWER_DATA, true);
        prefs.end();
        return value;
    }
    
    // ADE7953 configuration
    bool setSampleTime(uint32_t sampleTime) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putUInt(PREF_KEY_SAMPLE_TIME, sampleTime);
        prefs.end();
        return success;
    }
    
    uint32_t getSampleTime() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return 200; // Default sample time
        }
        uint32_t value = prefs.getUInt(PREF_KEY_SAMPLE_TIME, 200);
        prefs.end();
        return value;
    }
    
    bool setVoltageGain(uint32_t gain) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putUInt(PREF_KEY_A_V_GAIN, gain);
        prefs.end();
        return success;
    }
    
    uint32_t getVoltageGain() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return 4194304; // Default gain
        }
        uint32_t value = prefs.getUInt(PREF_KEY_A_V_GAIN, 4194304);
        prefs.end();
        return value;
    }
    
    bool setCurrentGainA(uint32_t gain) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putUInt(PREF_KEY_A_I_GAIN, gain);
        prefs.end();
        return success;
    }
    
    uint32_t getCurrentGainA() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return 4194304; // Default gain
        }
        uint32_t value = prefs.getUInt(PREF_KEY_A_I_GAIN, 4194304);
        prefs.end();
        return value;
    }
    
    bool setCurrentGainB(uint32_t gain) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, false)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putUInt(PREF_KEY_B_I_GAIN, gain);
        prefs.end();
        return success;
    }
    
    uint32_t getCurrentGainB() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_ADE7953, true)) {
            logger.error("Failed to open ADE7953 preferences", TAG_PREFS);
            return 4194304; // Default gain
        }
        uint32_t value = prefs.getUInt(PREF_KEY_B_I_GAIN, 4194304);
        prefs.end();
        return value;
    }
    
    // Channel configuration
    bool setChannelActive(uint8_t channel, bool active) {
        if (channel >= CHANNEL_COUNT) return false;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            return false;
        }
        
        char key[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_ACTIVE_FMT, channel);
        bool success = prefs.putBool(key, active);
        prefs.end();
        return success;
    }
    
    bool getChannelActive(uint8_t channel) {
        if (channel >= CHANNEL_COUNT) return false;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            return (channel == 0); // Default: only channel 0 active
        }
        
        char key[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_ACTIVE_FMT, channel);
        bool value = prefs.getBool(key, (channel == 0)); // Default: only channel 0 active
        prefs.end();
        return value;
    }
    
    bool setChannelLabel(uint8_t channel, const char* label) {
        if (channel >= CHANNEL_COUNT) return false;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            return false;
        }
        
        char key[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_LABEL_FMT, channel);
        bool success = prefs.putString(key, label) > 0;
        prefs.end();
        return success;
    }
    
    bool getChannelLabel(uint8_t channel, char* buffer, size_t bufferSize) {
        if (channel >= CHANNEL_COUNT) return false;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            snprintf(buffer, bufferSize, "Channel %d", channel);
            return false;
        }
        
        char key[32];
        char defaultLabel[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_LABEL_FMT, channel);
        snprintf(defaultLabel, sizeof(defaultLabel), "Channel %d", channel);
        
        String value = prefs.getString(key, defaultLabel);
        prefs.end();
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    bool setChannelPhase(uint8_t channel, uint8_t phase) {
        if (channel >= CHANNEL_COUNT) return false;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, false)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            return false;
        }
        
        char key[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_PHASE_FMT, channel);
        bool success = prefs.putUChar(key, phase);
        prefs.end();
        return success;
    }
    
    uint8_t getChannelPhase(uint8_t channel) {
        if (channel >= CHANNEL_COUNT) return 1;
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_CHANNELS, true)) {
            logger.error("Failed to open channels preferences", TAG_PREFS);
            return 1; // Default phase
        }
        
        char key[32];
        snprintf(key, sizeof(key), PREF_KEY_CHANNEL_PHASE_FMT, channel);
        uint8_t value = prefs.getUChar(key, 1);
        prefs.end();
        return value;
    }
    
    // MQTT configuration
    bool setMqttEnabled(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putBool(PREF_KEY_MQTT_ENABLED, enabled);
        prefs.end();
        return success;
    }
    
    bool getMqttEnabled() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false; // Default to disabled
        }
        bool value = prefs.getBool(PREF_KEY_MQTT_ENABLED, false);
        prefs.end();
        return value;
    }
    
    bool setMqttServer(const char* server) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putString(PREF_KEY_MQTT_SERVER, server) > 0;
        prefs.end();
        return success;
    }
    
    bool getMqttServer(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            buffer[0] = '\0';
            return false;
        }
        String value = prefs.getString(PREF_KEY_MQTT_SERVER, "");
        prefs.end();
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    bool setMqttPort(uint16_t port) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putUShort(PREF_KEY_MQTT_PORT, port);
        prefs.end();
        return success;
    }
    
    uint16_t getMqttPort() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return 1883; // Default MQTT port
        }
        uint16_t value = prefs.getUShort(PREF_KEY_MQTT_PORT, 1883);
        prefs.end();
        return value;
    }
    
    bool setMqttUsername(const char* username) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putString(PREF_KEY_MQTT_USERNAME, username) > 0;
        prefs.end();
        return success;
    }
    
    bool getMqttUsername(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            buffer[0] = '\0';
            return false;
        }
        String value = prefs.getString(PREF_KEY_MQTT_USERNAME, "");
        prefs.end();
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    bool setMqttPassword(const char* password) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            return false;
        }
        bool success = prefs.putString(PREF_KEY_MQTT_PASSWORD, password) > 0;
        prefs.end();
        return success;
    }
    
    bool getMqttPassword(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG_PREFS);
            buffer[0] = '\0';
            return false;
        }
        String value = prefs.getString(PREF_KEY_MQTT_PASSWORD, "");
        prefs.end();
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    // Authentication functions
    bool setWebPassword(const char* password) {
        if (!validatePasswordStrength(password)) {
            logger.error("Password does not meet strength requirements", TAG);
            return false;
        }
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, false)) {
            logger.error("Failed to open auth preferences for writing", TAG);
            return false;
        }
        
        bool success = prefs.putString(PREFERENCES_KEY_PASSWORD, password) > 0;
        prefs.end();
        
        if (success) {
            logger.info("Web password updated successfully", TAG);
        } else {
            logger.error("Failed to save web password", TAG);
        }
        
        return success;
    }
    
    bool getWebPassword(char* buffer, size_t bufferSize) {
        logger.debug("Getting web password", TAG);
        
        if (buffer == nullptr || bufferSize == 0) {
            logger.error("Invalid buffer for getWebPassword", TAG);
            return false;
        }
        
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_AUTH, true)) {
            logger.error("Failed to open auth preferences for reading", TAG);
            return false;
        }
        
        String value = prefs.getString(PREFERENCES_KEY_PASSWORD, "");
        prefs.end();
        
        if (value.isEmpty()) {
            // Return default password if not set
            snprintf(buffer, bufferSize, "%s", WEBSERVER_DEFAULT_PASSWORD);
            return true;
        }
        
        if (value.length() >= bufferSize) {
            logger.error("Password too long for buffer", TAG);
            return false;
        }
        
        snprintf(buffer, bufferSize, "%s", value.c_str());
        return true;
    }
    
    bool resetWebPassword() {
        logger.info("Resetting web password to default", TAG);
        return setWebPassword(WEBSERVER_DEFAULT_PASSWORD);
    }
    
    bool validatePasswordStrength(const char* password) {
        if (password == nullptr) {
            return false;
        }
        
        size_t length = strlen(password);
        
        // Check minimum length
        if (length < MIN_PASSWORD_LENGTH) {
            logger.warning("Password too short", TAG);
            return false;
        }
        
        // Check maximum length
        if (length > MAX_PASSWORD_LENGTH) {
            logger.warning("Password too long", TAG);
            return false;
        }
        
        // For simplicity, just check length constraints
        // Could add complexity requirements here (uppercase, numbers, etc.)
        return true;
    }
    
    // Utility functions
    bool hasConfiguration(const char* prefsNamespace) {
        Preferences prefs;
        if (!prefs.begin(prefsNamespace, true)) {
            return false;
        }
        
        // Check if namespace has any keys
        bool hasKeys = prefs.getBytesLength("__check__") == 0 && prefs.freeEntries() < 500; // Rough heuristic
        prefs.end();
        return hasKeys;
    }
}