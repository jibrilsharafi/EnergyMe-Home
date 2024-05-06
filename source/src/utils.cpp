#include "utils.h"

extern Logger logger;
extern CustomTime customTime;
extern Led led;
extern Ade7953 ade7953;

extern GeneralConfiguration generalConfiguration;

// Get the status of the device (firmware, memory, etc.)
JsonDocument getDeviceStatus()
{

    JsonDocument _jsonDocument;

    // system -> uptimeMs, uptimeDays
    JsonObject _jsonSystem = _jsonDocument["system"].to<JsonObject>();
    _jsonSystem["uptime"] = millis();

    JsonObject _jsonFirmware = _jsonDocument["firmware"].to<JsonObject>();
    _jsonFirmware["version"] = FIRMWARE_VERSION;
    _jsonFirmware["date"] = FIRMWARE_DATE;

    JsonObject _jsonFilesystem = _jsonDocument["filesystem"].to<JsonObject>();
    JsonDocument _jsonMetadata = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonMetadata.isNull()){
        _jsonFilesystem["version"] = "unknown";
        _jsonFilesystem["date"] = "unknown";
    } else {
        _jsonFilesystem["version"] = _jsonMetadata["filesystem"]["version"].as<String>();
        _jsonFilesystem["date"] = _jsonMetadata["filesystem"]["date"].as<String>();
    }

    JsonObject _jsonMemory = _jsonDocument["memory"].to<JsonObject>();
    JsonObject _jsonHeap = _jsonMemory["heap"].to<JsonObject>();
    _jsonHeap["free"] = ESP.getFreeHeap();
    _jsonHeap["total"] = ESP.getHeapSize();
    JsonObject _jsonFlash = _jsonMemory["flash"].to<JsonObject>();
    _jsonFlash["free"] = ESP.getFreeSketchSpace();
    _jsonFlash["total"] = ESP.getFlashChipSize();
    JsonObject _jsonSpiffs = _jsonMemory["spiffs"].to<JsonObject>();
    _jsonSpiffs["free"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    _jsonSpiffs["total"] = SPIFFS.totalBytes();

    JsonObject _jsonChip = _jsonDocument["chip"].to<JsonObject>();
    _jsonChip["model"] = ESP.getChipModel();
    _jsonChip["revision"] = ESP.getChipRevision();
    _jsonChip["cpuFrequency"] = ESP.getCpuFreqMHz();
    _jsonChip["sdkVersion"] = ESP.getSdkVersion();
    _jsonChip["id"] = ESP.getEfuseMac();

    return _jsonDocument;
}

JsonDocument deserializeJsonFromSpiffs(const char* path){
    logger.log("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    File _file = SPIFFS.open(path, "r");
    if (!_file){
        char _buffer[50+strlen(path)];
        snprintf(_buffer, sizeof(_buffer), "Failed to open file %s", path);
        logger.log(_buffer, "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);

        return JsonDocument();
    }
    JsonDocument _jsonDocument;

    DeserializationError _error = deserializeJson(_jsonDocument, _file);
    _file.close();
    if (_error){
        char _buffer[50+strlen(path)+strlen(_error.c_str())];
        snprintf(_buffer, sizeof(_buffer), "Failed to deserialize file %s. Error: %s", path, _error.c_str());
        logger.log(_buffer, "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        
        return JsonDocument();
    }

    logger.log("JSON deserialized from SPIFFS correctly", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(_jsonDocument, Serial);
    Serial.println();
    return _jsonDocument;
}

bool serializeJsonToSpiffs(const char* path, JsonDocument jsonDocument){
    logger.log("Serializing JSON to SPIFFS", "utils::serializeJsonToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    File _file = SPIFFS.open(path, "w");
    if (!_file){
        char _buffer[50+strlen(path)];
        snprintf(_buffer, sizeof(_buffer), "Failed to open file %s", path);
        logger.log(_buffer, "utils::serializeJsonToSpiffs", CUSTOM_LOG_LEVEL_ERROR);

        return false;
    }

    serializeJson(jsonDocument, _file);
    _file.close();

    logger.log("JSON serialized to SPIFFS correctly", "main::serializeJsonToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(jsonDocument, Serial);
    Serial.println();
    return true;
}

void restartEsp32(const char* functionName, const char* reason) {

    ade7953.saveEnergyToSpiffs();

    char _buffer[50+strlen(functionName)+strlen(reason)];
    
    snprintf(
        _buffer, 
        sizeof(_buffer), 
        "Restarting ESP32 from function %s. Reason: %s",
        functionName,
        reason
    );
    logger.log(_buffer, "main::restartEsp32", CUSTOM_LOG_LEVEL_FATAL);
  
    led.setBrightness(LED_MAX_BRIGHTNESS);
    led.block();
    for (int i = 0; i < 3; i++){
        led.setYellow(true);
        delay(200);
        led.setCyan(true);
        delay(200);
    }
    led.unblock();

    ESP.restart();
}

void printMeterValues(MeterValues meterValues, const char* channelLabel) {
  char _buffer[200];
  snprintf(
    _buffer, 
    sizeof(_buffer), 
    "%s: %.1f V | %.3f A || %.1f W | %.1f VAR | %.1f VA | %.3f PF || %.3f Wh | %.3f VARh | %.3f VAh",
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
  logger.log(_buffer, "main::printMeterValues", CUSTOM_LOG_LEVEL_DEBUG);
}

void printDeviceStatus() {
  char _buffer[250];

  JsonDocument _jsonDocument = getDeviceStatus();

  snprintf(
    _buffer, 
    sizeof(_buffer), 
    "Free heap: %.2f kB | Total heap: %.2f kB || Free flash: %.2f kB | Total flash: %.2f kB || Free SPIFFS: %.2f kB | Total SPIFFS: %.2f kB",
    _jsonDocument["freeHeap"].as<float>() * BYTE_TO_KILOBYTE,
    _jsonDocument["totalHeap"].as<float>() * BYTE_TO_KILOBYTE,
    _jsonDocument["freeFlash"].as<float>() * BYTE_TO_KILOBYTE,
    _jsonDocument["totalFlash"].as<float>() * BYTE_TO_KILOBYTE,
    _jsonDocument["spiffsUsedSize"].as<float>() * BYTE_TO_KILOBYTE,
    _jsonDocument["spiffsTotalSize"].as<float>() * BYTE_TO_KILOBYTE
  );
  logger.log(_buffer, "main::printDeviceStatus", CUSTOM_LOG_LEVEL_DEBUG);
}

bool checkIfFirstSetup() {
    logger.log("Checking if first setup...", "main::checkIfFirstSetup", CUSTOM_LOG_LEVEL_DEBUG);
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open metadata.json", "main::checkIfFirstSetup", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }

    return _jsonDocument["setup"]["isFirstTime"].as<bool>();
}

void logFirstSetupComplete() {
    logger.log("Logging first setup complete...", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_DEBUG);
    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(METADATA_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open metadata.json", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_ERROR);
        return;
    }

    _jsonDocument["setup"]["isFirstTime"] = false;
    _jsonDocument["setup"]["timestampFirstTime"] = customTime.getTimestamp();
    serializeJsonToSpiffs(METADATA_JSON_PATH, _jsonDocument);
    logger.log("First setup complete", "main::logFirstSetupComplete", CUSTOM_LOG_LEVEL_DEBUG);
}

// General configuration
// -----------------------------

void setDefaultGeneralConfiguration() {
    logger.log("Setting default general configuration...", "utils::setDefaultGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
    
    generalConfiguration.isCloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    
    logger.log("Default general configuration set", "utils::setDefaultGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

void setGeneralConfiguration(GeneralConfiguration newGeneralConfiguration) {
    logger.log("Setting general configuration...", "utils::setGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);

    generalConfiguration = newGeneralConfiguration;

    logger.log("General configuration set", "utils::setGeneralConfiguration", CUSTOM_LOG_LEVEL_DEBUG);
}

bool setGeneralConfigurationFromSpiffs() {
    logger.log("Setting general configuration from SPIFFS...", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = deserializeJsonFromSpiffs(GENERAL_CONFIGURATION_JSON_PATH);
    if (_jsonDocument.isNull()){
        logger.log("Failed to open general configuration file", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    } else {
        setGeneralConfiguration(jsonToGeneralConfiguration(_jsonDocument));
        logger.log("General configuration set from SPIFFS", "utils::setGeneralConfigurationFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    }
}

bool saveGeneralConfigurationToSpiffs() {
    logger.log("Saving general configuration to SPIFFS...", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    JsonDocument _jsonDocument = generalConfigurationToJson(generalConfiguration);
    if (serializeJsonToSpiffs(GENERAL_CONFIGURATION_JSON_PATH, _jsonDocument)){
        logger.log("General configuration saved to SPIFFS", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
        return true;
    } else {
        logger.log("Failed to save general configuration to SPIFFS", "utils::saveGeneralConfigurationToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
}

JsonDocument generalConfigurationToJson(GeneralConfiguration generalConfiguration) {
    JsonDocument _jsonDocument;
    
    _jsonDocument["isCloudServicesEnabled"] = generalConfiguration.isCloudServicesEnabled;
    
    return _jsonDocument;
}

GeneralConfiguration jsonToGeneralConfiguration(JsonDocument jsonDocument) {
    GeneralConfiguration _generalConfiguration;
    
    _generalConfiguration.isCloudServicesEnabled = jsonDocument["isCloudServicesEnabled"].as<bool>();

    return _generalConfiguration;
}