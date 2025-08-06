#if HAS_SECRETS
#include "mqtt.h"

static const char *TAG = "mqtt";

namespace Mqtt
{
    // Static variables
    // ==========================================================
    
    // MQTT client objects
    static WiFiClientSecure _net;
    static PubSubClient _clientMqtt(_net);
    static PublishMqtt _publishMqtt;

    // Debug flags for sending debug logs
    RTC_NOINIT_ATTR DebugFlagsRtc _debugFlagsRtc;

    // FreeRTOS queues
    static QueueHandle_t _logQueue = nullptr;
    static QueueHandle_t _meterQueue = nullptr;

    // Static queue structures and storage for PSRAM
    static StaticQueue_t _logQueueStruct;
    static StaticQueue_t _meterQueueStruct;
    static uint8_t* _logQueueStorage = nullptr;
    static uint8_t* _meterQueueStorage = nullptr;
    static bool _isLogQueueInitialized = false;
    static bool _isMeterQueueInitialized = false;

    // MQTT State Machine
    enum class MqttState {
        IDLE,
        CLAIMING_CERTIFICATES,
        SETTING_UP_CERTIFICATES,
        CONNECTING,
        CONNECTED,
        ERROR
    };
    
    // State variables
    static bool _isSetupDone = false;
    static MqttState _currentState = MqttState::IDLE;
    static uint32_t _mqttConnectionAttempt = 0;
    static uint64_t _nextMqttConnectionAttemptMillis = 0;

    // Track provisioning success (set by subscribe callback)
    static volatile bool _provisioningSucceeded = false;

    // Last publish timestamps
    static uint64_t _lastMillisMeterPublished = 0;
    static uint64_t _lastMillisSystemDynamicPublished = 0;
    static uint64_t _lastMillisStatisticsPublished = 0;

    // Configuration
    static bool _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
    static bool _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;

    // Version for OTA information
    static char _firmwareUpdateUrl[URL_BUFFER_SIZE];
    static char _firmwareUpdateVersion[VERSION_BUFFER_SIZE];
    
    // Certificates storage (decrypted)
    static char _awsIotCoreCert[CERTIFICATE_BUFFER_SIZE];
    static char _awsIotCorePrivateKey[CERTIFICATE_BUFFER_SIZE];
    
    // Topic buffers
    static char _mqttTopicMeter[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicSystemStatic[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicSystemDynamic[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicChannel[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicStatistics[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicCrash[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicLog[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicProvisioningRequest[MQTT_TOPIC_BUFFER_SIZE];

    // Task variables
    static TaskHandle_t _taskHandle = nullptr;
    static bool _taskShouldRun = false;

    // Thread safety
    static SemaphoreHandle_t _configMutex = nullptr;

    // Private function declarations
    // =========================================================

    // Queue initialization
    static bool _initializeLogQueue();
    static bool _initializeMeterQueue();
    
    // Configuration management
    static void _loadConfigFromPreferences();
    static void _saveConfigToPreferences();
    
    static void _setSendPowerDataEnabled(bool enabled);
    static void _setFirmwareUpdateUrl(const char* url);
    static void _setFirmwareUpdateVersion(const char* version);

    static void _saveCloudServicesEnabledToPreferences(bool enabled);
    static void _saveSendPowerDataEnabledToPreferences(bool enabled);
    static void _saveFirmwareUpdateUrlToPreferences(const char* url);
    static void _saveFirmwareUpdateVersionToPreferences(const char* version);

    // MQTT operations
    static void _setupMqttWithCertificates();
    static bool _connectMqtt();
    static bool _setCertificatesFromPreferences();
    static void _claimProcess();
    static bool _publishMessage(const char* topic, const char* message, bool retain = false);
    
    // Topic management
    static void _constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topic, size_t topicBufferSize);
    static void _constructMqttTopic(const char* finalTopic, char* topic, size_t topicBufferSize);
    static void _setupTopics();

    // Publish topics
    static void _setTopicMeter();
    static void _setTopicSystemStatic();
    static void _setTopicSystemDynamic();
    static void _setTopicChannel();
    static void _setTopicStatistics();
    static void _setTopicCrash();
    static void _setTopicLog();
    static void _setTopicProvisioningRequest();

    // Publishing functions
    static void _publishMeter();
    static void _publishSystemStatic();
    static void _publishSystemDynamic();
    static void _publishChannel();
    static void _publishStatistics();
    static void _publishCrash();
    static void _publishLog(const LogJson& logEntry);
    static bool _publishProvisioningRequest();
    
    // Queue processing
    static void _processLogQueue();
    
    // Publishing checks
    static void _checkPublishMqtt();
    static void _checkIfPublishMeterNeeded();
    static void _checkIfPublishSystemDynamicNeeded();
    static void _checkIfPublishStatisticsNeeded();
    
    // Subscription management
    static void _subscribeToTopics();
    static bool _subscribeToTopic(const char* topicSuffix, const char* description);
    static void _subscribeUpdateFirmware();
    static void _subscribeRestart();
    static void _subscribeProvisioningResponse();
    static void _subscribeEraseCertificates();
    static void _subscribeSetSendPowerData();
    static void _subscribeEnableDebugLogging();
    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length);
    
    // Message handlers
    static void _handleFirmwareUpdateMessage(const char* message);
    static void _handleRestartMessage();
    static void _handleProvisioningResponseMessage(const char* message);
    static void _handleEraseCertificatesMessage();
    static void _handleSetSendPowerDataMessage(const char* message);
    static void _handleEnableDebugLoggingMessage(const char* message);
    
    // Task management
    static void _mqttTask(void *parameter);
    static void _startTask();
    static void _stopTask();
    
    // Certificate management
    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize);
    static bool _isDeviceCertificatesPresent();
    static void _clearCertificates();
    
    // Utilities
    static const char* _getMqttStateReason(int32_t state);

    static void _decryptData(const char* encryptedDataBase64, const char* presharedKey, char* decryptedData, size_t decryptedDataSize);
    static void _deriveKey_SHA256(const char* presharedKey, const char* deviceId, uint8_t outKey32[32]);

    // Streaming helpers
    static size_t _calculateMeterMessageSize();
    static size_t _calculateCrashMessageSize();
    static bool _publishMeterStreaming();
    static bool _publishCrashStreaming();

    // Public API functions
    // =========================================================

    void begin()
    {
        logger.debug("Setting up MQTT client...", TAG);
        
        // Create configuration mutex
        if (_configMutex == nullptr) {
            _configMutex = xSemaphoreCreateMutex();
            if (_configMutex == nullptr) {
                logger.error("Failed to create configuration mutex", TAG);
                return;
            }
        }

        // Initialize RTC debug flags
        if (_debugFlagsRtc.signature != MAGIC_WORD_RTC) {
            logger.debug("RTC magic word is invalid, resetting debug flags", TAG);
            _debugFlagsRtc.enableMqttDebugLogging = false;
            _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
            _debugFlagsRtc.signature = MAGIC_WORD_RTC;
        } else if (_debugFlagsRtc.enableMqttDebugLogging) {
            // If the RTC is valid, and before the restart we were logging with debug info, reset the clock and start again the debugging
            logger.debug("RTC debug logging was enabled, restarting duration of %lu ms", TAG, _debugFlagsRtc.mqttDebugLoggingDurationMillis);
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = _debugFlagsRtc.mqttDebugLoggingDurationMillis;
        }

        _isSetupDone = true; // Must set before since we have checks on the setup later
        _setupTopics();
        _loadConfigFromPreferences(); // Here we load the config. The setCloudServicesEnabled will handle starting the task if enabled
        
        if (_cloudServicesEnabled) _startTask(); // TODO: remove this as it is probably reduntant (and also check that when setting from prefs, we use the correct methods)
        
        logger.debug("MQTT client setup complete", TAG);
    }

    void stop()
    {
        logger.debug("Stopping MQTT client...", TAG);
        _stopTask();
        
        _logQueue = nullptr;
        _meterQueue = nullptr;
        
        if (_isLogQueueInitialized && _logQueueStorage != nullptr) {
            heap_caps_free(_logQueueStorage);
            _logQueueStorage = nullptr;
            _isLogQueueInitialized = false;
            logger.debug("MQTT log queue PSRAM freed", TAG);
        }
        
        if (_isMeterQueueInitialized && _meterQueueStorage != nullptr) {
            heap_caps_free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            _isMeterQueueInitialized = false;
            logger.debug("MQTT meter queue PSRAM freed", TAG);
        }

        if (_configMutex != nullptr) {
            vSemaphoreDelete(_configMutex);
            _configMutex = nullptr;
        }

        // Zeroize decrypted material
        memset(_awsIotCoreCert, 0, sizeof(_awsIotCoreCert));
        memset(_awsIotCorePrivateKey, 0, sizeof(_awsIotCorePrivateKey));
        
        _isSetupDone = false;
        logger.info("MQTT client stopped", TAG);
    }

    void setCloudServicesEnabled(bool enabled)
    {
        if (!_isSetupDone) begin();
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for setCloudServicesEnabled", TAG);
            return;
        }

        logger.debug("Setting cloud services to %s...", TAG, enabled ? "enabled" : "disabled");
        
        _stopTask();

        _cloudServicesEnabled = enabled;
        
        // Reset connection attempt counters on configuration change
        _nextMqttConnectionAttemptMillis = 0;
        _mqttConnectionAttempt = 0;

        _saveConfigToPreferences();

        if (_cloudServicesEnabled) _startTask();
        
        releaseMutex(&_configMutex);

        logger.info("Cloud services %s", TAG, enabled ? "enabled" : "disabled");
    }

    bool isCloudServicesEnabled() { return _cloudServicesEnabled; }
    static bool _isSendPowerDataEnabled() { return _sendPowerDataEnabled; }

    void getFirmwareUpdateVersion(char* buffer, size_t bufferSize) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for getFirmwareUpdateVersion", TAG);
            snprintf(buffer, bufferSize, "%s", "");
            return;
        }
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateVersion);
        releaseMutex(&_configMutex);
    }

    void getFirmwareUpdateUrl(char* buffer, size_t bufferSize) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for getFirmwareUpdateUrl", TAG);
            snprintf(buffer, bufferSize, "%s", "");
            return;
        }
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateUrl);
        releaseMutex(&_configMutex);
    }

    bool isLatestFirmwareInstalled() {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for isLatestFirmwareInstalled", TAG);
            return false; // Assume not installed if we can't access
        }

        if (strlen(_firmwareUpdateVersion) == 0 || strchr(_firmwareUpdateVersion, '.') == NULL) {
            logger.debug("Latest firmware version is empty or in the wrong format", TAG);
            releaseMutex(&_configMutex);
            return true;
        }

        int32_t latestMajor, latestMinor, latestPatch;
        sscanf(_firmwareUpdateVersion, "%ld.%ld.%ld", &latestMajor, &latestMinor, &latestPatch);

        int32_t currentMajor = atoi(FIRMWARE_BUILD_VERSION_MAJOR);
        int32_t currentMinor = atoi(FIRMWARE_BUILD_VERSION_MINOR);
        int32_t currentPatch = atoi(FIRMWARE_BUILD_VERSION_PATCH);

        if (latestMajor < currentMajor) return true; // Current major is higher (thus no need to check minor/patch)
        if (latestMajor > currentMajor) return false; // Current major is lower (thus latest is newer)
        if (latestMinor < currentMinor) return true; // Current minor is higher (thus no need to check patch)
        if (latestMinor > currentMinor) return false; // Current minor is lower (thus latest is newer)
        if (latestPatch < currentPatch) return true; // Current patch is higher
        if (latestPatch > currentPatch) return false; // Current patch is lower (thus latest is newer)

        releaseMutex(&_configMutex);
        return true; // Versions are equal
    }

    // Public methods for requesting MQTT publications
    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestCrashPublish() {_publishMqtt.crash = true; }

    // Public methods for pushing data to queues
    void pushLog(const char* timestamp, uint64_t millisEsp, const char* level, uint32_t coreId, const char* function, const char* message)
    {
        if (!_isLogQueueInitialized) {
            if (!_initializeLogQueue()) {
                return; // Failed to initialize, drop this log
            }
        }
        
        if ((strcmp(level, "debug") == 0 && !_debugFlagsRtc.enableMqttDebugLogging) || 
            (strcmp(level, "verbose") == 0)) {
            return; // Never send verbose logs via MQTT
        }
        
        LogJson logEntry(timestamp, millisEsp, level, coreId, function, message);
        
        // Check if queue is full, if so remove oldest entry
        if (uxQueueSpacesAvailable(_logQueue) == 0) {
            LogJson discardedEntry;
            xQueueReceive(_logQueue, &discardedEntry, 0); // Remove oldest
        }
        // Now send the new entry
        xQueueSend(_logQueue, &logEntry, 0);
    }

    void pushMeter(const PayloadMeter& payload)
    {
        if (!_isMeterQueueInitialized) {
            if (!_initializeMeterQueue()) {
                return; // Failed to initialize, drop this meter data
            }
        }
        
        // Check if queue is full, if so remove oldest entry
        if (uxQueueSpacesAvailable(_meterQueue) == 0) {
            PayloadMeter discardedEntry;
            xQueueReceive(_meterQueue, &discardedEntry, 0); // Remove oldest
        }
        // Now send the new entry
        xQueueSend(_meterQueue, &payload, 0);
    }

    // Private functions
    // =================
    // =================

    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length)
    {
        char message[MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE];
        
        // Ensure we don't exceed buffer bounds
        uint32_t maxLength = sizeof(message) - 1; // Reserve space for null terminator
        if (length > maxLength) {
            logger.warning("MQTT message from topic %s too large (%u bytes), truncating to %u", TAG, topic, length, maxLength);
            length = maxLength;
        }
        
        snprintf(message, sizeof(message), "%.*s", (int)length, (char*)payload);

        logger.debug("Received MQTT message from %s", TAG, topic);

        if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE)) _handleFirmwareUpdateMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_RESTART)) _handleRestartMessage();
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) _handleProvisioningResponseMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES)) _handleEraseCertificatesMessage();
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_SET_SEND_POWER_DATA)) _handleSetSendPowerDataMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING)) _handleEnableDebugLoggingMessage(message);
        else logger.warning("Unknown MQTT topic received: %s", TAG, topic);
    }

    static void _handleFirmwareUpdateMessage(const char* message)
    {
        // Expected JSON format: {"version": "1.0.0", "url": "https://example.com/firmware.bin"}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            logger.error("Failed to parse firmware update JSON message", TAG);
            return;
        }

        if (jsonDocument["version"].is<const char*>()) _setFirmwareUpdateVersion(jsonDocument["version"].as<const char*>());
        if (jsonDocument["url"].is<const char*>()) _setFirmwareUpdateUrl(jsonDocument["url"].as<const char*>());
    }

    static void _handleRestartMessage()
    {
        setRestartSystem(TAG, "Restart requested from MQTT");
    }

    static void _handleProvisioningResponseMessage(const char* message)
    {
        // Expected JSON format: {"status": "success", "encryptedCertificatePem": "...", "encryptedPrivateKey": "..."}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            logger.error("Failed to parse provisioning response JSON message", TAG);
            return;
        }

        if (jsonDocument["status"] == "success")
        {
            // Validate that certificate fields exist and are strings
            if (!jsonDocument["encryptedCertificatePem"].is<const char*>() || 
                !jsonDocument["encryptedPrivateKey"].is<const char*>()) {
                logger.error("Invalid provisioning response: missing or invalid certificate fields", TAG);
                return;
            }

            const char* certData = jsonDocument["encryptedCertificatePem"].as<const char*>();
            const char* keyData = jsonDocument["encryptedPrivateKey"].as<const char*>();
            
            size_t certLen = strlen(certData);
            size_t keyLen = strlen(keyData);
            
            // TODO: use maybe setcerts function here?
            if (certLen >= CERTIFICATE_BUFFER_SIZE || keyLen >= CERTIFICATE_BUFFER_SIZE) {
                logger.error("Provisioning payload too large (cert %zu, key %zu)", TAG, certLen, keyLen);
                return;
            }

            Preferences preferences;
            if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) {
                logger.error("Failed to open preferences", TAG);
                return;
            }

            size_t writtenSize = preferences.putString(PREFS_KEY_CERTIFICATE, certData);
            if (writtenSize != certLen) {
                logger.error("Failed to write encrypted certificate to preferences (%zu != %zu)", TAG, writtenSize, certLen);
                preferences.end();
                return;
            }

            writtenSize = preferences.putString(PREFS_KEY_PRIVATE_KEY, keyData);
            if (writtenSize != keyLen) {
                logger.error("Failed to write encrypted private key to preferences (%zu != %zu)", TAG, writtenSize, keyLen);
                preferences.end();
                return;
            }

            preferences.end();

            _provisioningSucceeded = true; // signal to claim loop

            // Restart to reconfigure with new certs (kept for simplicity and clean TLS ctx)
            setRestartSystem(TAG, "Restarting after successful certificates provisioning");
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(jsonDocument, buffer, sizeof(buffer));
            logger.error("Provisioning failed: %s", TAG, buffer);
        }
    }

    static void _handleEraseCertificatesMessage()
    {
        _clearCertificates();
        setRestartSystem(TAG, "Certificates erase requested from MQTT");
    }

    static void _handleSetSendPowerDataMessage(const char* message)
    {
        // Expected JSON format: {"sendPowerData": true}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            logger.error("Failed to parse send power data JSON message", TAG);
            return;
        }
        
        if (jsonDocument["sendPowerData"].is<bool>()) {
            bool sendPowerData = jsonDocument["sendPowerData"].as<bool>();
            _setSendPowerDataEnabled(sendPowerData);
        }
    }

    static void _handleEnableDebugLoggingMessage(const char* message)
    {
        // Expected JSON format: {"enable": true, "duration_minutes": 10}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            logger.error("Failed to parse enable debug logging JSON message", TAG);
            return;
        }

        bool enable = jsonDocument["enable"].is<bool>() ? jsonDocument["enable"].as<bool>() : false;

        if (enable)
        {
            int32_t durationMinutes = MQTT_DEBUG_LOGGING_DEFAULT_DURATION / (60 * 1000);
            if (jsonDocument["duration_minutes"].is<int32_t>()) {
                durationMinutes = jsonDocument["duration_minutes"].as<int32_t>();
            }
            
            int32_t durationMs = durationMinutes * 60 * 1000;
            if (durationMs <= 0 || durationMs > MQTT_DEBUG_LOGGING_MAX_DURATION) {
                durationMs = MQTT_DEBUG_LOGGING_DEFAULT_DURATION;
            }
            
            _debugFlagsRtc.enableMqttDebugLogging = true;
            _debugFlagsRtc.mqttDebugLoggingDurationMillis = durationMs;
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = millis64() + durationMs;
            _debugFlagsRtc.signature = MAGIC_WORD_RTC;
        }
        else
        {
            _debugFlagsRtc.enableMqttDebugLogging = false;
            _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
            _debugFlagsRtc.signature = 0;
        }
    }

    bool _initializeLogQueue() // Cannot use logger here to avoid circular dependency
    {
        if (_isLogQueueInitialized) return true;

        // Allocate queue storage in PSRAM
        size_t queueStorageSize = MQTT_LOG_QUEUE_SIZE * sizeof(LogJson);
        _logQueueStorage = (uint8_t*)heap_caps_malloc(queueStorageSize, MALLOC_CAP_SPIRAM);
        
        if (_logQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT log queue (%d bytes)\n", queueStorageSize);
            return false;
        }

        _logQueue = xQueueCreateStatic(MQTT_LOG_QUEUE_SIZE, sizeof(LogJson), _logQueueStorage, &_logQueueStruct);
        if (_logQueue == nullptr) {
            Serial.printf("[ERROR] Failed to create MQTT log queue\n");
            heap_caps_free(_logQueueStorage);
            _logQueueStorage = nullptr;
            return false;
        }

        _isLogQueueInitialized = true;
        Serial.printf("[DEBUG] MQTT log queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", queueStorageSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    bool _initializeMeterQueue()
    {
        if (_isMeterQueueInitialized) return true;

        // Allocate queue storage in PSRAM
        size_t queueStorageSize = MQTT_METER_QUEUE_SIZE * sizeof(PayloadMeter);
        _meterQueueStorage = (uint8_t*)heap_caps_malloc(queueStorageSize, MALLOC_CAP_SPIRAM);
        
        if (_meterQueueStorage == nullptr) {
            logger.error("Failed to allocate PSRAM for MQTT meter queue (%zu bytes)\n", TAG, queueStorageSize);
            return false;
        }

        _meterQueue = xQueueCreateStatic(MQTT_METER_QUEUE_SIZE, sizeof(PayloadMeter), _meterQueueStorage, &_meterQueueStruct);
        if (_meterQueue == nullptr) {
            logger.error("Failed to create MQTT meter queue\n", TAG);
            heap_caps_free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            return false;
        }

        _isMeterQueueInitialized = true;
        logger.debug("MQTT meter queue initialized with PSRAM buffer (%zu bytes) | Free PSRAM: %zu bytes\n", TAG, queueStorageSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    // State management helper functions
    static void _setState(MqttState newState) {
        if (_currentState != newState) {
            logger.debug("MQTT state transition: %d -> %d", TAG, static_cast<int>(_currentState), static_cast<int>(newState));
            _currentState = newState;
        }
    }

    static void _handleIdleState() {
        if (_isDeviceCertificatesPresent()) {
            _setState(MqttState::SETTING_UP_CERTIFICATES);
        } else {
            _setState(MqttState::CLAIMING_CERTIFICATES);
        }
    }

    static void _handleClaimingState() {
        _claimProcess();
        // State will be updated in claim process completion or timeout
    }

    static void _handleSettingUpCertificatesState() {
        _setupMqttWithCertificates();
        _setState(MqttState::CONNECTING);
    }

    static void _handleConnectingState() {
        if (_clientMqtt.connected()) {
            _setState(MqttState::CONNECTED);
            return;
        }
        if (millis64() >= _nextMqttConnectionAttemptMillis) {
            _connectMqtt();
            if (_clientMqtt.connected()) {
                _setState(MqttState::CONNECTED);
            }
        }
    }

    static void _handleConnectedState() {
        if (!_clientMqtt.connected() || !_clientMqtt.loop()) { // Also process incoming messages with loop()
            logger.debug("MQTT disconnected, transitioning to connecting state", TAG);
            statistics.mqttConnectionErrors++;
            _setState(MqttState::CONNECTING);
            return;
        }

        // Process queues and publishing
        _processLogQueue();
        _checkIfPublishMeterNeeded();
        _checkIfPublishSystemDynamicNeeded();
        _checkIfPublishStatisticsNeeded();
        _checkPublishMqtt();
    }

    static void _mqttTask(void *parameter)
    {
        logger.debug("MQTT task started", TAG);
        
        _taskShouldRun = true;
        const TickType_t xFrequency = pdMS_TO_TICKS(MQTT_LOOP_INTERVAL);

        while (_taskShouldRun)
        {
            // Skip processing if WiFi is not connected
            if (!CustomWifi::isFullyConnected()) {
                uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, xFrequency);
                if (notificationValue > 0) { _taskShouldRun = false; break; }
                continue;
            }

            // Check if debug logs have expired
            if (_debugFlagsRtc.enableMqttDebugLogging && millis64() > _debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
                _debugFlagsRtc.enableMqttDebugLogging = false;
                _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
                _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
                _debugFlagsRtc.signature = 0;
                logger.debug("The MQTT debug period has ended", TAG);
            }

            switch (_currentState) {
                case MqttState::IDLE:                    _handleIdleState(); break;
                case MqttState::CLAIMING_CERTIFICATES:   _handleClaimingState(); break;
                case MqttState::SETTING_UP_CERTIFICATES: _handleSettingUpCertificatesState(); break;
                case MqttState::CONNECTING:              _handleConnectingState(); break;
                case MqttState::CONNECTED:               _handleConnectedState(); break;
                case MqttState::ERROR:                   _setState(MqttState::IDLE); break;
            }
            
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, xFrequency);
            if (notificationValue > 0) { _taskShouldRun = false; break; }
        }
        
        _clientMqtt.disconnect();
        _setState(MqttState::IDLE);
        logger.debug("MQTT task stopping", TAG);
        _taskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    // Certificate validation helper function
    static bool _validateCertificateFormat(const char* cert, const char* certType) {
        if (!cert || strlen(cert) == 0) {
            logger.error("Certificate %s is empty or null", TAG, certType);
            return false;
        }

        // Check for valid PEM format (either standard or RSA private key)
        bool hasValidHeader = strstr(cert, "-----BEGIN CERTIFICATE-----") != nullptr ||
                              strstr(cert, "-----BEGIN PRIVATE KEY-----") != nullptr ||
                              strstr(cert, "-----BEGIN RSA PRIVATE KEY-----") != nullptr;

        if (!hasValidHeader) {
            logger.error("Certificate %s does not have valid PEM header", TAG, certType);
            return false;
        }

        return true;
    }

    // Simple error handling wrapper
    static bool _handleMqttError(const char* operation, int32_t errorCode = 0) {
        if (errorCode == MQTT_CONNECT_BAD_CREDENTIALS || errorCode == MQTT_CONNECT_UNAUTHORIZED) { // TODO: this could disable after a bit or limit the reboots/similar
            logger.error("MQTT %s failed due to authorization error (%d). Clearing certificates", TAG, operation, errorCode);
            _clearCertificates();
            _setState(MqttState::ERROR);
            setRestartSystem(TAG, "MQTT Authentication/Authorization Error");
            return false;
        }
        
        if (errorCode != 0) {
            logger.error("MQTT %s failed with error: %s (%d)", TAG, operation, _getMqttStateReason(errorCode), errorCode);
        } else {
            logger.error("MQTT %s failed", TAG, operation);
        }
        
        _setState(MqttState::ERROR);
        return false;
    }

    static void _setupMqttWithCertificates() {
        _clientMqtt.setCallback(_subscribeCallback);

        if (!_setCertificatesFromPreferences()) {
            logger.error("Failed to set certificates", TAG);
            _setState(MqttState::ERROR);
            return;
        }

        // Validate certificates before setting them
        if (!_validateCertificateFormat(_awsIotCoreCert, "device cert") ||
            !_validateCertificateFormat(_awsIotCorePrivateKey, "private key")) {
            logger.error("Certificate validation failed", TAG);
            _setState(MqttState::ERROR);
            return;
        }

        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(_awsIotCoreCert);
        _net.setPrivateKey(_awsIotCorePrivateKey);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        logger.debug("MQTT certificates setup complete", TAG);
    }    
    
    static bool _connectMqtt()
    {
        logger.debug("Attempting to connect to MQTT (attempt %lu)...", TAG, _mqttConnectionAttempt + 1);

        if (_clientMqtt.connect(DEVICE_ID))
        {
            logger.info("Connected to MQTT", TAG);

            _mqttConnectionAttempt = 0; // Reset attempt counter on success
            statistics.mqttConnections++;

            _subscribeToTopics();

            // Publish data on connection
            _publishMqtt.systemStatic = true;
            _publishMqtt.systemDynamic = true;
            _publishMqtt.statistics = true;
            _publishMqtt.channel = true;

            return true;
        }
        else
        {
            int32_t currentState = _clientMqtt.state();
            _mqttConnectionAttempt++;
            statistics.mqttConnectionErrors++;

            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                return _handleMqttError("connection", currentState);
            }

            uint64_t _backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_INITIAL_RETRY_INTERVAL, MQTT_MAX_RETRY_INTERVAL, MQTT_RETRY_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;
            logger.warning("Failed to connect to MQTT (attempt %lu). Reason: %s. Next attempt in %llu ms", TAG, _mqttConnectionAttempt, _getMqttStateReason(currentState), _backoffDelay);

            return false;
        }
    }

    static bool _setCertificatesFromPreferences() {
        // Ensure the certificates are clean
        memset(_awsIotCoreCert, 0, sizeof(_awsIotCoreCert));
        memset(_awsIotCorePrivateKey, 0, sizeof(_awsIotCorePrivateKey));

        _readEncryptedPreferences(PREFS_KEY_CERTIFICATE, preshared_encryption_key, _awsIotCoreCert, sizeof(_awsIotCoreCert));
        _readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, preshared_encryption_key, _awsIotCorePrivateKey, sizeof(_awsIotCorePrivateKey));

        // Ensure the certs are valid by looking for the PEM header
        if (strlen(_awsIotCoreCert) == 0 || 
            strlen(_awsIotCorePrivateKey) == 0 ||
            !strstr(_awsIotCoreCert, "-----BEGIN CERTIFICATE-----") ||
            (!strstr(_awsIotCorePrivateKey, "-----BEGIN PRIVATE KEY-----") &&
             !strstr(_awsIotCorePrivateKey, "-----BEGIN RSA PRIVATE KEY-----"))) 
        {
            logger.error("Invalid device certificates", TAG);
            _clearCertificates();
            setRestartSystem(TAG, "Invalid device certificates. Cleared and waiting for claim process");
            return false;
        }

        logger.debug("Certificates set and validated", TAG);
        return true;
    }

    static void _claimProcess() {
        logger.debug("Claiming certificates...", TAG);
        _provisioningSucceeded = false;

        _clientMqtt.setCallback(_subscribeCallback);
        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(aws_iot_core_cert_certclaim);
        _net.setPrivateKey(aws_iot_core_cert_privateclaim);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);
        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        logger.debug("MQTT setup for claiming certificates complete", TAG);

        // Connect with controlled attempts + backoff
        for (int32_t connectionAttempt = 0; connectionAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && _taskShouldRun; ++connectionAttempt) {
            logger.debug("Attempting to connect to MQTT for claiming certificates (%d/%d)...", TAG, connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_clientMqtt.connect(DEVICE_ID)) {
                logger.debug("Connected to MQTT for claiming certificates", TAG);
                statistics.mqttConnections++;
                break;
            }

            logger.warning("Failed to connect to MQTT for claiming certificates (attempt %d). Reason: %s", TAG, connectionAttempt + 1, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttConnectionErrors++;

            uint64_t backoffDelay = calculateExponentialBackoff(connectionAttempt, MQTT_CLAIM_INITIAL_RETRY_INTERVAL, MQTT_CLAIM_MAX_RETRY_INTERVAL, MQTT_CLAIM_RETRY_MULTIPLIER);
            uint64_t start = millis64();
            while (_taskShouldRun && (millis64() - start) < backoffDelay) {
                vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
            }

            if (connectionAttempt == MQTT_CLAIM_MAX_CONNECTION_ATTEMPT - 1) {
                logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", TAG, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
                _setState(MqttState::ERROR);
                setRestartSystem(TAG, "Failed to claim certificates");
                return;
            }
        }

        _subscribeProvisioningResponse();

        // Publish provisioning request (limited retries)
        for (int32_t publishAttempt = 0; publishAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && _taskShouldRun; ++publishAttempt) {
            logger.debug("Attempting to publish provisioning request (%d/%d)...", TAG, publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
            if (_publishProvisioningRequest()) {
                logger.debug("Provisioning request published", TAG);
                break;
            }
            logger.warning("Failed to publish provisioning request (%d/%d)", TAG, publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
            vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
            if (publishAttempt == MQTT_CLAIM_MAX_CONNECTION_ATTEMPT - 1) {
                _setState(MqttState::ERROR);
                setRestartSystem(TAG, "Failed to publish provisioning request");
                return;
            }
        }

        // Wait for response or presence of certs
        logger.debug("Waiting for provisioning response...", TAG);
        uint64_t deadline = millis64() + MQTT_CLAIM_TIMEOUT;

        while (_taskShouldRun && millis64() < deadline) {
            _clientMqtt.loop();

            if (_provisioningSucceeded || _isDeviceCertificatesPresent()) {
                logger.debug("Certificates provisioning confirmed", TAG);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
        }

        // Always disconnect the claim session before switching state
        _clientMqtt.disconnect();

        if (_provisioningSucceeded || _isDeviceCertificatesPresent()) {
            _setState(MqttState::SETTING_UP_CERTIFICATES);
            return;
        }

        logger.warning("Provisioning response timeout. Will retry later.", TAG);
        // Stay in CLAIMING; outer task loop will re-enter with backoff via connect attempts
    }

    static void _constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topic, size_t topicBufferSize) {
        snprintf(
            topic,
            topicBufferSize,
            "%s/%s/%s/%s/%s/%s",
            MQTT_BASIC_INGEST,
            ruleName,
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            DEVICE_ID,
            finalTopic
        );

        logger.debug("Constructing MQTT topic with rule for %s | %s", TAG, finalTopic, topic);
    }

    static void _constructMqttTopic(const char* finalTopic, char* topic, size_t topicBufferSize) {
        snprintf(
            topic,
            topicBufferSize,
            "%s/%s/%s/%s",
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            DEVICE_ID,
            finalTopic
        );

        logger.debug("Constructing MQTT topic for %s | %s", TAG, finalTopic, topic);
    }

    static void _setupTopics() {
        _setTopicMeter();
        _setTopicSystemStatic();
        _setTopicSystemDynamic();
        _setTopicChannel();
        _setTopicStatistics();
        _setTopicCrash();
        _setTopicLog();
        _setTopicProvisioningRequest();

        logger.debug("MQTT topics setup complete", TAG);
    }

    static void _setTopicMeter() { _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicSystemStatic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_STATIC, _mqttTopicSystemStatic, sizeof(_mqttTopicSystemStatic)); }
    static void _setTopicSystemDynamic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_DYNAMIC, _mqttTopicSystemDynamic, sizeof(_mqttTopicSystemDynamic)); }
    static void _setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel, sizeof(_mqttTopicChannel)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }
    static void _setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash, sizeof(_mqttTopicCrash)); }
    static void _setTopicLog() { _constructMqttTopic(MQTT_TOPIC_LOG, _mqttTopicLog, sizeof(_mqttTopicLog)); }
    static void _setTopicProvisioningRequest() { _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, _mqttTopicProvisioningRequest, sizeof(_mqttTopicProvisioningRequest)); }

    static void _publishMeter() {
        // Check if we have any data to publish
        MeterValues meterValuesZeroChannel;
        Ade7953::getMeterValues(meterValuesZeroChannel, 0);
        UBaseType_t queueSize = uxQueueMessagesWaiting(_meterQueue);
        
        bool hasVoltageData = (meterValuesZeroChannel.lastUnixTimeMilliseconds > 0);
        bool hasQueueData = (queueSize > 0 && _isSendPowerDataEnabled());
        bool hasChannelData = false;
        
        // Check if any channels have valid data
        for (uint8_t i = 0; i < CHANNEL_COUNT && !hasChannelData; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                hasChannelData = true;
            }
        }
        
        if (!hasVoltageData && !hasQueueData && !hasChannelData) {
            logger.verbose("No valid meter data to publish", TAG);
            return;
        }
        
        if (!_publishMeterStreaming()) {
            logger.error("Failed to publish meter data via streaming", TAG);
            return;
        }

        _publishMqtt.meter = false;
        _lastMillisMeterPublished = millis64();
    }
    
    static void _publishSystemStatic() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        JsonDocument jsonDocumentSystemStatic;
        SystemStaticInfo systemStaticInfo;
        populateSystemStaticInfo(systemStaticInfo);
        systemStaticInfoToJson(systemStaticInfo, jsonDocumentSystemStatic);
        jsonDocument["data"] = jsonDocumentSystemStatic;

        char staticMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, staticMessage, sizeof(staticMessage));

        if (_publishMessage(_mqttTopicSystemStatic, staticMessage, true)) {
            _publishMqtt.systemStatic = false; // retain static info since it is idempotent
        }

        logger.debug("System static info published to MQTT", TAG);
    }

    static void _publishSystemDynamic() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        JsonDocument jsonDocumentSystemDynamic;
        SystemDynamicInfo systemDynamicInfo;
        populateSystemDynamicInfo(systemDynamicInfo);
        systemDynamicInfoToJson(systemDynamicInfo, jsonDocumentSystemDynamic);
        jsonDocument["data"] = jsonDocumentSystemDynamic;

        char dynamicMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, dynamicMessage, sizeof(dynamicMessage));

        if (_publishMessage(_mqttTopicSystemDynamic, dynamicMessage)) {
            _publishMqtt.systemDynamic = false;
            _lastMillisSystemDynamicPublished = millis64();
        }

        logger.debug("System dynamic info published to MQTT", TAG);
    }

    static void _publishChannel() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            JsonDocument jsonChannelData;
            Ade7953::getChannelDataAsJson(jsonChannelData, i);
            jsonDocument["data"][i] = jsonChannelData;
        }

        char channelMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, channelMessage, sizeof(channelMessage));
     
        if (_publishMessage(_mqttTopicChannel, channelMessage, true)) {
            _publishMqtt.channel = false; // retain channel info since it is idempotent
        }

        logger.debug("Channel data published to MQTT", TAG);
    }

    static void _publishStatistics() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        JsonDocument jsonDocumentStatistics;
        statisticsToJson(statistics, jsonDocumentStatistics);
        jsonDocument["statistics"] = jsonDocumentStatistics;

        char statisticsMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, statisticsMessage, sizeof(statisticsMessage));

        if (_publishMessage(_mqttTopicStatistics, statisticsMessage)) {
            _publishMqtt.statistics = false;
            _lastMillisStatisticsPublished = millis64();
        }

        logger.debug("Statistics published to MQTT", TAG);
    }

    static void _publishCrash() {
        if (!_publishCrashStreaming()) {
            logger.error("Failed to publish crash data via streaming", TAG);
        }
    }

    static bool _publishProvisioningRequest() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        jsonDocument["firmwareVersion"] = FIRMWARE_BUILD_VERSION;
        jsonDocument["sketchMD5"] = ESP.getSketchMD5();
        jsonDocument["chipId"] = ESP.getEfuseMac();

        char provisioningRequestMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, provisioningRequestMessage, sizeof(provisioningRequestMessage));

        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, topic, sizeof(topic));

        return _publishMessage(topic, provisioningRequestMessage);
    }

    static bool _publishMessage(const char* topic, const char* message, bool retain) {
        if (topic == nullptr || message == nullptr) {
            logger.warning("Null ptr, MQTT not initialized yet", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.connected()) {
            logger.warning("MQTT not connected (%s). Skipping publish on %s", TAG, _getMqttStateReason(_clientMqtt.state()), topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.publish(topic, message, retain)) {
            logger.error("Failed to publish on %s (state %s)", TAG, topic, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        statistics.mqttMessagesPublished++;
        logger.verbose("Message published: %s", TAG, message); // Cannot be debug otherwise when remote debugging on MQTT is enabled, we get a loop
        return true;
    }

    static void _checkIfPublishMeterNeeded() {
        UBaseType_t queueSize = uxQueueMessagesWaiting(_meterQueue);
        if ( ((queueSize > (MQTT_METER_QUEUE_SIZE * MQTT_METER_QUEUE_ALMOST_FULL_THRESHOLD)) && _isSendPowerDataEnabled()) || 
             ((millis64() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH && queueSize > 0) ) { // Queue has to have at least one element to publish
            _publishMqtt.meter = true;
            logger.debug("Set flag to publish %u meter data points", TAG, queueSize);
        }
    }

    static void _checkIfPublishSystemDynamicNeeded() {
        if ((millis64() - _lastMillisSystemDynamicPublished) > MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH) {
            _publishMqtt.systemDynamic = true;
            logger.debug("Set flag to publish system dynamic", TAG);
        }
    }
    
    static void _checkIfPublishStatisticsNeeded() {
        if ((millis64() - _lastMillisStatisticsPublished) > MQTT_MAX_INTERVAL_STATISTICS_PUBLISH) {
            _publishMqtt.statistics = true;
            logger.debug("Set flag to publish statistics", TAG);
        }
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.systemStatic) {_publishSystemStatic();}
        if (_publishMqtt.systemDynamic) {_publishSystemDynamic();}
        if (_publishMqtt.channel) {_publishChannel();}
        if (_publishMqtt.statistics) {_publishStatistics();}
        if (_publishMqtt.crash) {_publishCrash();}
    }

    static void _subscribeToTopics() {
        _subscribeUpdateFirmware();
        _subscribeRestart();
        _subscribeProvisioningResponse();
        _subscribeEraseCertificates();
        _subscribeSetSendPowerData();
        _subscribeEnableDebugLogging();

        logger.debug("Subscribed to topics", TAG);
    }

    static bool _subscribeToTopic(const char* topicSuffix, const char* description) {
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(topicSuffix, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to %s", TAG, description);
            return false;
        }

        logger.debug("Subscribed to %s topic: %s", TAG, description, topicSuffix);
        return true;
    }

    static void _subscribeUpdateFirmware() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE, "firmware update"); }
    static void _subscribeRestart() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, "restart"); }
    static void _subscribeProvisioningResponse() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, "provisioning response"); }
    static void _subscribeEraseCertificates() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES, "erase certificates"); }
    static void _subscribeSetSendPowerData() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_SET_SEND_POWER_DATA, "set send power data"); }
    static void _subscribeEnableDebugLogging() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING, "enable debug logging"); }

    static void _processLogQueue()
    {
        LogJson logEntry;
        uint32_t loops = 0;
        while (xQueueReceive(_logQueue, &logEntry, 0) == pdTRUE &&
                loops < MAX_LOOP_ITERATIONS) {
            // Only process if MQTT is connected and WiFi is available
            if (CustomWifi::isFullyConnected() && _clientMqtt.connected()) {
                _publishLog(logEntry);
            } else {
                // If not connected, put it back in the queue if there's space
                xQueueSendToFront(_logQueue, &logEntry, 0);
                break; // Stop processing if not connected
            }
        }
    }
    
    static void _publishLog(const LogJson& logEntry)
    {
        char logTopic[sizeof(_mqttTopicLog) + sizeof(logEntry.level) + 2];
        snprintf(logTopic, sizeof(logTopic), "%s/%s", _mqttTopicLog, logEntry.level);

        JsonDocument jsonDocument;
        jsonDocument["timestamp"] = logEntry.timestamp;
        jsonDocument["millis"] = logEntry.millisEsp;
        jsonDocument["core"] = logEntry.coreId;
        jsonDocument["function"] = logEntry.function;
        jsonDocument["message"] = logEntry.message;

        char logMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, logMessage, sizeof(logMessage));
        
        if (!_publishMessage(logTopic, logMessage)) xQueueSendToFront(_logQueue, &logEntry, 0);
    }
    
    static void _loadConfigFromPreferences()
    {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for loading preferences", TAG);
            return;
        }

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences. Setting defaults.", TAG);
            _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
            _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;
            snprintf(_firmwareUpdateUrl, sizeof(_firmwareUpdateUrl), "%s", "");
            snprintf(_firmwareUpdateVersion, sizeof(_firmwareUpdateVersion), "%s", "");
            prefs.end();
            releaseMutex(&_configMutex);
            _saveConfigToPreferences();
            return;
        }
        
        _cloudServicesEnabled = prefs.getBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, DEFAULT_CLOUD_SERVICES_ENABLED);
        _sendPowerDataEnabled = prefs.getBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, DEFAULT_SEND_POWER_DATA_ENABLED);
        prefs.getString(MQTT_PREFERENCES_FW_UPDATE_URL_KEY, _firmwareUpdateUrl, sizeof(_firmwareUpdateUrl));
        prefs.getString(MQTT_PREFERENCES_FW_UPDATE_VERSION_KEY, _firmwareUpdateVersion, sizeof(_firmwareUpdateVersion));
        
        logger.debug("Cloud services enabled: %s, Send power data enabled: %s", TAG, 
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false");
        logger.debug("Firmware update | URL: %s, Version: %s", TAG, 
                   _firmwareUpdateUrl,
                   _firmwareUpdateVersion);

        prefs.end();
        releaseMutex(&_configMutex);

        _saveConfigToPreferences();
        logger.debug("MQTT preferences loaded", TAG);
    }

    static void _saveConfigToPreferences()
    {
        _saveCloudServicesEnabledToPreferences(_cloudServicesEnabled);
        _saveSendPowerDataEnabledToPreferences(_sendPowerDataEnabled);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);
        
        logger.debug("MQTT preferences saved", TAG);
    }

    static void _startTask()
    {
        if (_taskHandle != nullptr) {
            logger.debug("MQTT task is already running", TAG);
            return;
        }

        logger.debug("Starting MQTT task", TAG);

        if (!_initializeLogQueue()) {
            logger.error("Failed to initialize MQTT log queue", TAG);
            return;
        }

        if (!_initializeMeterQueue()) {
            logger.error("Failed to initialize MQTT meter queue", TAG);
            return;
        }

        size_t logQueueStorageSize = MQTT_LOG_QUEUE_SIZE * sizeof(LogJson);
        size_t meterQueueStorageSize = MQTT_METER_QUEUE_SIZE * sizeof(PayloadMeter);
        logger.debug("MQTT queues created with PSRAM - Log: %d bytes, Meter: %d bytes", TAG, logQueueStorageSize, meterQueueStorageSize);

        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);
        
        BaseType_t result = xTaskCreate(
            _mqttTask,
            MQTT_TASK_NAME,
            MQTT_TASK_STACK_SIZE,
            nullptr,
            MQTT_TASK_PRIORITY,
            &_taskHandle
        );

        if (result != pdPASS) {
            logger.error("Failed to create MQTT task", TAG);
            _taskHandle = nullptr;
        }
    }

    static void _stopTask() { stopTaskGracefully(&_taskHandle, "MQTT task"); }

    static void _setSendPowerDataEnabled(bool enabled)
    {
        _sendPowerDataEnabled = enabled;
        _saveSendPowerDataEnabledToPreferences(enabled);
        logger.debug("Set send power data enabled to %s", TAG, enabled ? "true" : "false");
    }
    
    static void _setFirmwareUpdateVersion(const char* version) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for setting firmware update version", TAG);
            return;
        }
        if (version == nullptr) {
            logger.error("Invalid firmware update version provided", TAG);
            releaseMutex(&_configMutex);
            return;
        }
        snprintf(_firmwareUpdateVersion, sizeof(_firmwareUpdateVersion), "%s", version);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);
        releaseMutex(&_configMutex);
        logger.debug("Firmware update version set to %s", TAG, _firmwareUpdateVersion);
    }

    static void _setFirmwareUpdateUrl(const char* url) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            logger.error("Failed to acquire configuration mutex for setting firmware update URL", TAG);
            return;
        }
        if (url == nullptr) {
            logger.error("Invalid firmware update URL provided", TAG);
            releaseMutex(&_configMutex);
            return;
        }
        snprintf(_firmwareUpdateUrl, sizeof(_firmwareUpdateUrl), "%s", url);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);
        releaseMutex(&_configMutex);
        logger.debug("Firmware update URL set to %s", TAG, _firmwareUpdateUrl);
    }

    static void _saveCloudServicesEnabledToPreferences(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return;
        }
        size_t bytesWritten = prefs.putBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, enabled);
        if (bytesWritten == 0) {
            logger.error("Failed to save cloud services enabled preference", TAG);
        }
        prefs.end();
    }

    static void _saveSendPowerDataEnabledToPreferences(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return;
        }
        size_t bytesWritten = prefs.putBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, enabled);
        if (bytesWritten == 0) {
            logger.error("Failed to save send power data enabled preference", TAG);
        }
        prefs.end();
    }

    static void _saveFirmwareUpdateVersionToPreferences(const char* version) {
        // TODO: this could become more complex to allow for AWS IoT Core Jobs (so SHA256, etc.)
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open firmware update preferences", TAG);
            return;
        }
        size_t bytesWritten = prefs.putString(MQTT_PREFERENCES_FW_UPDATE_VERSION_KEY, version);
        if (bytesWritten != strlen(version)) {
            logger.error(
                "Failed to save firmware update version preference (written %zu | expected %zu): %s",
                TAG,
                bytesWritten, strlen(version), version
            );
        }
        prefs.end();
    }

    static void _saveFirmwareUpdateUrlToPreferences(const char* url) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open firmware update preferences", TAG);
            return;
        }
        size_t bytesWritten = prefs.putString(MQTT_PREFERENCES_FW_UPDATE_URL_KEY, url);
        if (bytesWritten != strlen(url)) {
            logger.error(
                "Failed to save firmware update URL preference (written %zu | expected %zu): %s", 
                TAG, 
                bytesWritten, strlen(url), url
            );
        }
        prefs.end();
    }

    // Certificates management
    // =======================

    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize) {
        if (!preference_key || !preshared_encryption_key || !decryptedData || decryptedDataSize == 0) {
            if (decryptedData && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            logger.error("Failed to open preferences", TAG);
            if (decryptedData && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        char encryptedData[CERTIFICATE_BUFFER_SIZE];
        // Initialize buffer to prevent garbage data
        memset(encryptedData, 0, sizeof(encryptedData));
        preferences.getString(preference_key, encryptedData, sizeof(encryptedData));
        preferences.end();

        if (strlen(encryptedData) == 0) {
            // no log spam
            return;
        }

        // Use GCM decrypt with SHA256(preshared||DEVICE_ID)
        _decryptData(encryptedData, preshared_encryption_key, decryptedData, decryptedDataSize);
    }

    static bool _isDeviceCertificatesPresent() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            return false;
        }

        char buffer[CERTIFICATE_BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));
        size_t deviceCertLen = preferences.getString(PREFS_KEY_CERTIFICATE, buffer, sizeof(buffer));
        bool _deviceCertExists = (deviceCertLen >= MINIMUM_CERTIFICATE_LENGTH && deviceCertLen < CERTIFICATE_BUFFER_SIZE);

        memset(buffer, 0, sizeof(buffer));
        size_t privateKeyLen = preferences.getString(PREFS_KEY_PRIVATE_KEY, buffer, sizeof(buffer));
        bool _privateKeyExists = (privateKeyLen >= MINIMUM_CERTIFICATE_LENGTH && privateKeyLen < CERTIFICATE_BUFFER_SIZE);

        preferences.end();
        return _deviceCertExists && _privateKeyExists;
    }

    static void _clearCertificates() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) {
            logger.error("Failed to open preferences", TAG);
            return;
        }
        preferences.clear();
        preferences.end();
        _setState(MqttState::IDLE);
        logger.info("Certificates for cloud services cleared", TAG);
    }

    // Utilities
    // =========

    static const char* _getMqttStateReason(int32_t state)
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

    static void _deriveKey_SHA256(const char* presharedKey, const char* deviceId, uint8_t outKey32[32]) {
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(presharedKey), strlen(presharedKey));
        mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(deviceId), strlen(deviceId));
        mbedtls_sha256_finish(&sha, outKey32);
        mbedtls_sha256_free(&sha);
    }

    void _decryptData(const char* encryptedDataBase64, const char* presharedKey, char* decryptedData, size_t decryptedDataSize) {
        if (!encryptedDataBase64 || !presharedKey || !decryptedData || decryptedDataSize == 0) {
            if (decryptedData && decryptedDataSize > 0) decryptedData[0] = '\0';
            logger.warning("Invalid parameters for decryption", TAG);
            return;
        }

        size_t inputLength = strlen(encryptedDataBase64);
        if (inputLength == 0) {
            decryptedData[0] = '\0';
            return;
        }

        // Base64 decode
        uint8_t decoded[CERTIFICATE_BUFFER_SIZE];
        size_t decodedLen = 0;
        int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLen,
                                        reinterpret_cast<const unsigned char*>(encryptedDataBase64), inputLength);
        if (ret != 0 || decodedLen < (12 + 16)) { // must at least contain IV + TAG
            decryptedData[0] = '\0';
            logger.error("Failed to decode base64 or data too short (%u)", TAG, (unsigned)decodedLen);
            return;
        }

        const size_t IV_LEN = 12;
        const size_t TAG_LEN = 16;
        const uint8_t* iv  = decoded;
        const uint8_t* tag = decoded + decodedLen - TAG_LEN;
        const uint8_t* ct  = decoded + IV_LEN;
        size_t ctLen = decodedLen - IV_LEN - TAG_LEN;

        uint8_t key[32];
        _deriveKey_SHA256(presharedKey, DEVICE_ID, key);

        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0) {
            decryptedData[0] = '\0';
            mbedtls_gcm_free(&gcm);
            memset(key, 0, sizeof(key));
            logger.error("Failed to set AES-GCM key", TAG);
            return;
        }

        // Decrypt+auth
        int dec = mbedtls_gcm_auth_decrypt(&gcm,
                                           ctLen,
                                           iv, IV_LEN,
                                           nullptr, 0, // AAD (none)
                                           tag, TAG_LEN,
                                           ct,
                                           reinterpret_cast<uint8_t*>(decryptedData));
        mbedtls_gcm_free(&gcm);
        // wipe key
        memset(key, 0, sizeof(key));

        if (dec != 0) {
            decryptedData[0] = '\0';
            logger.error("AES-GCM auth decrypt failed (%d)", TAG, dec);
            return;
        }

        // Ensure null termination (certificate is ASCII PEM)
        if (ctLen >= decryptedDataSize) ctLen = decryptedDataSize - 1;
        decryptedData[ctLen] = '\0';

        // Clear sensitive buffers
        memset(decoded, 0, sizeof(decoded));

        logger.debug("Decrypted data successfully", TAG);
    }


    static size_t _calculateMeterMessageSize() {
        size_t estimatedSize = 100; // Base JSON structure: {"data":[]}
        
        // Add size for voltage data (always present if valid)
        MeterValues meterValuesZeroChannel;
        Ade7953::getMeterValues(meterValuesZeroChannel, 0);
        if (meterValuesZeroChannel.lastUnixTimeMilliseconds > 0) {
estimatedSize += 80; // {"unixTime":1234567890123,"voltage":230.5}
        }
        
        // Add size for queue items
        UBaseType_t queueSize = uxQueueMessagesWaiting(_meterQueue);
        if (_isSendPowerDataEnabled()) { // TODO: eventually this will become a simple array with no keys
            // Each queue item: {"unixTime":1234567890123,"channel":1,"activePower":1234.56,"powerFactor":0.95}
            estimatedSize += queueSize * 100;
        }
        
        // Add size for channel energy data
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                // Each channel: {"unixTime":123,"channel":1,"activeEnergyImported":123.45,...}
                estimatedSize += 300; // Conservative estimate for all energy fields
            }
        }
        
        logger.debug("Estimated meter message size: %zu bytes", TAG, estimatedSize);
        return estimatedSize;
    }

    static size_t _calculateCrashMessageSize() { // TODO: make the estimates a constants in mqtt.h
        size_t estimatedSize = 150; // Base JSON: {"unixTime":123,"crashInfo":{...}}
        
        // Add crash info size - conservative estimate
        estimatedSize += 500; // Basic crash info fields
        
        // Add dump data size - this is the big one
        size_t dumpDataSize = CrashMonitor::getCoreDumpSize();
        if (dumpDataSize > 0) {
            // Base64 encoding increases size by ~33%, plus JSON field structure
            estimatedSize += (dumpDataSize * 4 / 3) + 100;
        }
        
        logger.debug("Estimated crash message size: %zu bytes", TAG, estimatedSize);
        return estimatedSize;
    }

    // TODO: use messagepack instead of JSON here for efficiency
    static bool _publishMeterStreaming() {
        size_t estimatedSize = _calculateMeterMessageSize();
        
        if (!_clientMqtt.beginPublish(_mqttTopicMeter, estimatedSize, false)) {
            logger.error("Failed to begin meter data streaming publish", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        _clientMqtt.print("{\"data\":[");
        bool firstItem = true;

        MeterValues meterValuesZeroChannel;
        Ade7953::getMeterValues(meterValuesZeroChannel, 0);
        if (meterValuesZeroChannel.lastUnixTimeMilliseconds > 0) {
            _clientMqtt.print("{\"unixTime\":");
            _clientMqtt.print(meterValuesZeroChannel.lastUnixTimeMilliseconds);
            _clientMqtt.print(",\"voltage\":");
            _clientMqtt.print(meterValuesZeroChannel.voltage);
            _clientMqtt.print("}");
            firstItem = false;
        }

        
        PayloadMeter payloadMeter;
        uint32_t loops = 0;
        while (uxQueueMessagesWaiting(_meterQueue) > 0 && loops < MAX_LOOP_ITERATIONS && _isSendPowerDataEnabled()) {
            loops++;
            if (xQueueReceive(_meterQueue, &payloadMeter, 0) != pdTRUE) break;
            
            if (payloadMeter.unixTimeMs == 0) {
                logger.debug("Payload meter has zero unixTime, skipping...", TAG);
                continue;
            }

            if (!firstItem) _clientMqtt.print(",");
            _clientMqtt.print("{\"unixTime\":");
            _clientMqtt.print(payloadMeter.unixTimeMs);
            _clientMqtt.print(",\"channel\":");
            _clientMqtt.print(payloadMeter.channel);
            _clientMqtt.print(",\"activePower\":");
            _clientMqtt.print(payloadMeter.activePower);
            _clientMqtt.print(",\"powerFactor\":");
            _clientMqtt.print(payloadMeter.powerFactor);
            _clientMqtt.print("}");
            firstItem = false;
        }

        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                MeterValues meterValues;
                Ade7953::getMeterValues(meterValues, i);

                if (meterValues.lastUnixTimeMilliseconds == 0) {
                    logger.debug("Meter values for channel %d have zero unixTime, skipping...", TAG, i);
                    continue;
                }

                if (!firstItem) _clientMqtt.print(",");
                _clientMqtt.print("{\"unixTime\":");
                _clientMqtt.print(meterValues.lastUnixTimeMilliseconds);
                _clientMqtt.print(",\"channel\":");
                _clientMqtt.print(i);
                _clientMqtt.print(",\"activeEnergyImported\":");
                _clientMqtt.print(meterValues.activeEnergyImported);
                _clientMqtt.print(",\"activeEnergyExported\":");
                _clientMqtt.print(meterValues.activeEnergyExported);
                _clientMqtt.print(",\"reactiveEnergyImported\":");
                _clientMqtt.print(meterValues.reactiveEnergyImported);
                _clientMqtt.print(",\"reactiveEnergyExported\":");
                _clientMqtt.print(meterValues.reactiveEnergyExported);
                _clientMqtt.print(",\"apparentEnergy\":");
                _clientMqtt.print(meterValues.apparentEnergy);
                _clientMqtt.print("}");
                firstItem = false;
            }
        }

        _clientMqtt.print("]}");

        if (_clientMqtt.endPublish()) {
            _publishMqtt.meter = false;
            statistics.mqttMessagesPublished++;
            logger.debug("Meter data streamed successfully", TAG);
            return true;
        } else {
            logger.error("Failed to complete meter data streaming", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }
    }

    static bool _publishCrashStreaming() {
        size_t estimatedSize = _calculateCrashMessageSize();
        
        if (!_clientMqtt.beginPublish(_mqttTopicCrash, estimatedSize, false)) {
            logger.error("Failed to begin crash data streaming publish", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        _clientMqtt.print("{\"unixTime\":");
        _clientMqtt.print(CustomTime::getUnixTimeMilliseconds());
        _clientMqtt.print(",\"crashInfo\":{");

        JsonDocument crashInfoJson;
        CrashMonitor::getCoreDumpInfoJson(crashInfoJson);
        
        bool firstField = true;
        for (JsonPair field : crashInfoJson.as<JsonObject>()) {
            if (!firstField) _clientMqtt.print(",");
            _clientMqtt.print("\"");
            _clientMqtt.print(field.key().c_str());
            _clientMqtt.print("\":");
            if (field.value().is<const char*>()) {
                _clientMqtt.print("\"");
                _clientMqtt.print(field.value().as<const char*>());
                _clientMqtt.print("\"");
            } else if (field.value().is<int>()) {
                _clientMqtt.print(field.value().as<int>());
            } else if (field.value().is<bool>()) {
                _clientMqtt.print(field.value().as<bool>() ? "true" : "false");
            }
            firstField = false;
        }

        size_t dumpDataSize = CrashMonitor::getCoreDumpSize();
        if (dumpDataSize > 0) {
            if (!firstField) _clientMqtt.print(",");
            _clientMqtt.print("\"dumpData\":\"");

            // Stream dump data in base64 encoded chunks
            const size_t CHUNK_SIZE = 1024; // Process 1KB at a time
            uint8_t buffer[CHUNK_SIZE];
            char base64Buffer[CHUNK_SIZE * 4 / 3 + 4]; // Base64 encoding buffer
            
            for (size_t offset = 0; offset < dumpDataSize; offset += CHUNK_SIZE) {
                size_t chunkSize = min(CHUNK_SIZE, dumpDataSize - offset);
                size_t bytesRead = 0;
                if (CrashMonitor::getCoreDumpChunk(buffer, offset, chunkSize, &bytesRead)) {
                    size_t base64Len = 0;
                    mbedtls_base64_encode((unsigned char*)base64Buffer, sizeof(base64Buffer), &base64Len, buffer, bytesRead);
                    base64Buffer[base64Len] = '\0';
                    _clientMqtt.print(base64Buffer);
                }
            }
            _clientMqtt.print("\"");
        }

        _clientMqtt.print("}}");

        if (_clientMqtt.endPublish()) {
            _publishMqtt.crash = false;
            CrashMonitor::clearCoreDump();
            statistics.mqttMessagesPublished++;
            logger.debug("Crash data with full dump streamed successfully", TAG);
            return true;
        } else {
            logger.error("Failed to complete crash data streaming", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }
    }
}
#endif