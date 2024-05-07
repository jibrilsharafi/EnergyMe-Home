#include "logger.h"

Logger::Logger() {
    _print_level = DEFAULT_LOG_PRINT_LEVEL;
    _save_level = DEFAULT_LOG_SAVE_LEVEL;
}

void Logger::begin(){
    if(!setLogLevelsFromSpiffs()){
        setDefaultLogLevels();
    }
    log("Logger initialized", "logger::begin", CUSTOM_LOG_LEVEL_DEBUG);
}

void Logger::log(const char* message, const char* function, int logLevel) {
    logLevel = _saturateLogLevel(logLevel);
    if (logLevel < _print_level && logLevel < _save_level){
        return;
    }
    
    char _message_formatted[26+strlen(message)+strlen(function)+70]; // 26 = length of timestamp, 70 = length of log level

    snprintf(
        _message_formatted, 
        sizeof(_message_formatted), 
        CUSTOM_LOG_FORMAT, 
        customTime.getTimestamp().c_str(),
        millis(),
        _logLevelToString(logLevel).c_str(), 
        xPortGetCoreID(),
        function, 
        message);

    if (logLevel >= _print_level){
        Serial.println(_message_formatted);
        Serial.flush();
    }

    if (logLevel >= _save_level){
        _save(_message_formatted);
    }

    switch (logLevel){
        case CUSTOM_LOG_LEVEL_WARNING:
            led.setYellow(true);
            break;
        case CUSTOM_LOG_LEVEL_ERROR:
            led.setOrange(true);
            break;
        case CUSTOM_LOG_LEVEL_FATAL:
            led.setRed(true);
            break;
    }
}

void Logger::logOnly(const char* message, const char* function, int logLevel) {
    logLevel = _saturateLogLevel(logLevel);
    char _message_formatted[26+strlen(message)+strlen(function)+70]; // 26 = length of timestamp, 70 = length of log level

    snprintf(
        _message_formatted, 
        sizeof(_message_formatted), 
        CUSTOM_LOG_FORMAT, 
        customTime.getTimestamp().c_str(),
        millis(),
        _logLevelToString(logLevel).c_str(), 
        xPortGetCoreID(),
        function, 
        message);

    if (logLevel >= _print_level){
        Serial.println(_message_formatted);
    }
}

void Logger::setPrintLevel(int level){
    log(
        ("Setting print level to " + String(level)).c_str(),
        "logger::setPrintLevel",
        CUSTOM_LOG_LEVEL_INFO
    );

    _print_level = _saturateLogLevel(level);
    _saveLogLevelsToSpiffs();
}

void Logger::setSaveLevel(int level){
    log(
        ("Setting save level to " + String(level)).c_str(),
        "logger::setSaveLevel",
        CUSTOM_LOG_LEVEL_INFO
    );

    _save_level = _saturateLogLevel(level);
    _saveLogLevelsToSpiffs();
}

String Logger::getPrintLevel(){
    return _logLevelToString(_print_level);
}

String Logger::getSaveLevel(){
    return _logLevelToString(_save_level);
}


void Logger::setDefaultLogLevels() {
    setPrintLevel(DEFAULT_LOG_PRINT_LEVEL);
    setSaveLevel(DEFAULT_LOG_SAVE_LEVEL);
    log("Log levels set to default", "logger::setDefaultLogLevels", CUSTOM_LOG_LEVEL_DEBUG);
}

bool Logger::setLogLevelsFromSpiffs() {
    // JsonDocument _jsonDocument = deserializeJsonFromSpiffs(LOGGER_JSON_PATH);

    log("Deserializing JSON from SPIFFS", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    File _file = SPIFFS.open(LOGGER_JSON_PATH, "r");
    if (!_file){
        log(
            (
                "Failed to open file " +
                String(LOGGER_JSON_PATH))
                .c_str(),
            "utils::deserializeJsonFromSpiffs",
            CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }
    JsonDocument _jsonDocument;

    DeserializationError _error = deserializeJson(_jsonDocument, _file);
    _file.close();
    if (_error){
        log(
            (
                "Failed to deserialize file " +
                String(LOGGER_JSON_PATH) +
                ". Error: " +
                String(_error.c_str()))
                .c_str(),
            "utils::deserializeJsonFromSpiffs",
            CUSTOM_LOG_LEVEL_ERROR);
        return false;
    }

    log("JSON deserialized from SPIFFS correctly", "utils::deserializeJsonFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
    serializeJson(_jsonDocument, Serial);
    Serial.println();

    if (_jsonDocument.isNull()){
        return false;
    }
    setPrintLevel(_jsonDocument["level"]["print"].as<int>());
    setSaveLevel(_jsonDocument["level"]["save"].as<int>());
    log("Log levels set from SPIFFS", "logger::setLogLevelsFromSpiffs", CUSTOM_LOG_LEVEL_DEBUG);

    return true;
}

void Logger::_saveLogLevelsToSpiffs() {
    JsonDocument _jsonDocument;
    _jsonDocument["level"]["print"] = _print_level;
    _jsonDocument["level"]["save"] = _save_level;
    File _file = SPIFFS.open(LOGGER_JSON_PATH, "w");
    if (!_file){
        log("Failed to open logger.json", "logger::_saveLogLevelsToSpiffs", CUSTOM_LOG_LEVEL_ERROR);
        return;
    }
    serializeJson(_jsonDocument, _file);
    _file.close();
    log("Log levels saved to SPIFFS", "logger::_saveLogLevelsToSpiffs", CUSTOM_LOG_LEVEL_DEBUG);
}

void Logger::_save(const char* messageFormatted){
    File file = SPIFFS.open(LOG_TXT_PATH, "a");
    if (file){
    
        file.println(messageFormatted);
        file.close();
    } else {
        logOnly("Failed to open log file", "logger::_save", CUSTOM_LOG_LEVEL_ERROR);
    }
}

int Logger::getNumberOfLinesInLogFile() {
    File _file = SPIFFS.open(LOG_TXT_PATH, "r");
    if (!_file) {
        logOnly("Failed to open log file", "logger::getNumberOfLinesInLogFile", CUSTOM_LOG_LEVEL_ERROR);
        return -1;
    }

    int _totalLines = 0;
    while (_file.available()) {
        _file.readStringUntil('\n');
        _totalLines++;
    }

    _file.close();
    return _totalLines;
}

void Logger::clearLog() {
    logOnly("Clearing log", "logger::clearLog", CUSTOM_LOG_LEVEL_WARNING);
    SPIFFS.remove(LOG_TXT_PATH);
    File _file = SPIFFS.open(LOG_TXT_PATH, "w");
    if (!_file) {
        logOnly("Failed to open log file", "logger::clearLog", CUSTOM_LOG_LEVEL_ERROR);
        return;
    }
    _file.close();
    log("Log cleared", "logger::clearLog", CUSTOM_LOG_LEVEL_WARNING);
}

String Logger::_logLevelToString(int logLevel){
    switch (logLevel){
        case CUSTOM_LOG_LEVEL_VERBOSE:
            return "VERBOSE";
        case CUSTOM_LOG_LEVEL_DEBUG:
            return "DEBUG";
        case CUSTOM_LOG_LEVEL_INFO:
            return "INFO";
        case CUSTOM_LOG_LEVEL_WARNING:
            return "WARNING";
        case CUSTOM_LOG_LEVEL_ERROR:
            return "ERROR";
        case CUSTOM_LOG_LEVEL_FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

int Logger::_saturateLogLevel(int logLevel){
    return min(max(logLevel, CUSTOM_LOG_LEVEL_VERBOSE), CUSTOM_LOG_LEVEL_FATAL);
}