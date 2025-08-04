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

    // Last publish timestamps
    static uint64_t _lastMillisMeterPublished = 0;
    static uint64_t _lastMillisSystemDynamicPublished = 0;
    static uint64_t _lastMillisStatisticsPublished = 0;

    // Configuration
    static bool _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
    static bool _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;
    static bool _debugLogsEnabled = DEFAULT_DEBUG_LOGS_ENABLED;

    // Version for OTA information
    static char _firmwareUpdateUrl[URL_BUFFER_SIZE];
    static char _firmwareUpdateVersion[VERSION_BUFFER_SIZE];
    
    // Certificates storage
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
    
    static void _setCloudServicesEnabled(bool enabled);
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
    static bool _setCertificates();
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
    static void _meterQueueToJson(JsonDocument* jsonDocument);
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
    static void _decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize);

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
        logger.debug("MQTT client setup complete", TAG);
    }

    void stop()
    {
        logger.debug("Stopping MQTT client...", TAG);
        _stopTask();
        
        // Clean up queues and PSRAM storage
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

        // Clean up mutex
        if (_configMutex != nullptr) {
            vSemaphoreDelete(_configMutex);
            _configMutex = nullptr;
        }
        
        _isSetupDone = false;
        logger.info("MQTT client stopped", TAG);
    }

    void setCloudServicesEnabled(bool enabled)
    {
        if (!_isSetupDone) begin();

        // Acquire mutex with timeout
        if (_configMutex == nullptr || xSemaphoreTake(_configMutex, pdMS_TO_TICKS(CONFIG_MUTEX_TIMEOUT_MS)) != pdTRUE) {
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

        // Release mutex
        xSemaphoreGive(_configMutex);

        logger.info("Cloud services %s", TAG, enabled ? "enabled" : "disabled");
    }

    bool isCloudServicesEnabled() { return _cloudServicesEnabled; }
    static bool _isSendPowerDataEnabled() { return _sendPowerDataEnabled; }

    void getFirmwareUpdateVersion(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateVersion);
    }

    void getFirmwareUpdateUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateUrl);
    }

    bool isLatestFirmwareInstalled() {
        if (strlen(_firmwareUpdateVersion) == 0 || strchr(_firmwareUpdateVersion, '.') == NULL) {
            logger.debug("Latest firmware version is empty or in the wrong format", TAG);
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

        return true; // Versions are equal
    }

    // Public methods for requesting MQTT publications
    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestCrashPublish() {_publishMqtt.crash = true; }

    // Public methods for pushing data to queues
    void pushLog(const char* timestamp, uint64_t millisEsp, const char* level, uint32_t coreId, const char* function, const char* message)
    {
        // Initialize log queue on first use if not already done
        if (!_isLogQueueInitialized) {
            if (!_initializeLogQueue()) {
                return; // Failed to initialize, drop this log
            }
        }
        
        // Filter debug logs if not enabled
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
        // Initialize meter queue on first use if not already done
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
        if (length >= sizeof(message)) {
            logger.warning("The MQTT message received from the topic %s has a size of %d (too big). It will be truncated", TAG, topic, length);
        }
        snprintf(message, sizeof(message), "%.*s", length, (char*)payload);

        logger.debug("Received MQTT message from %s: %s", TAG, topic, message);

        if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE)) _handleFirmwareUpdateMessage(message);
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_RESTART)) _handleRestartMessage();
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) _handleProvisioningResponseMessage(message);
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES)) _handleEraseCertificatesMessage();
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_SET_SEND_POWER_DATA)) _handleSetSendPowerDataMessage(message);
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING)) _handleEnableDebugLoggingMessage(message);
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
            char encryptedCertPem[CERTIFICATE_BUFFER_SIZE];
            char encryptedPrivateKey[CERTIFICATE_BUFFER_SIZE];

            snprintf(encryptedCertPem, sizeof(encryptedCertPem), "%s", jsonDocument["encryptedCertificatePem"].as<const char*>());
            snprintf(encryptedPrivateKey, sizeof(encryptedPrivateKey), "%s", jsonDocument["encryptedPrivateKey"].as<const char*>());

            Preferences preferences;
            if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) {
                logger.error("Failed to open preferences", TAG);
                return;
            }

            size_t writtenSize = 0;
            writtenSize = preferences.putString(PREFS_KEY_CERTIFICATE, encryptedCertPem);
            logger.debug(
                "Written encrypted certificate to preferences (size written: %zu | size to write: %zu)", 
                TAG, 
                writtenSize, strlen(encryptedCertPem)
            );
            if (writtenSize != strlen(encryptedCertPem)) {
                logger.error("Failed to write encrypted certificate to preferences. Sizes don't match (%zu != %zu)", TAG, writtenSize, strlen(encryptedCertPem));
                preferences.end();
                return;
            }

            writtenSize = preferences.putString(PREFS_KEY_PRIVATE_KEY, encryptedPrivateKey);
            logger.debug(
                "Written encrypted private key to preferences (size written: %zu | size to write: %zu)", 
                TAG, 
                writtenSize, strlen(encryptedPrivateKey)
            );
            if (writtenSize != strlen(encryptedPrivateKey)) {
                logger.error("Failed to write encrypted private key to preferences. Sizes don't match (%zu != %zu)", TAG, writtenSize, strlen(encryptedPrivateKey));
                preferences.end();
                return;
            }

            preferences.end();

            // Restart MQTT connection
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
        
        // Handle sendPowerData setting
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

        bool enable = false;
        if (jsonDocument["enable"].is<bool>()) {
            enable = jsonDocument["enable"].as<bool>();
        }

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
        if (_isLogQueueInitialized) {
            return true;
        }

        // Allocate queue storage in PSRAM
        size_t queueStorageSize = MQTT_LOG_QUEUE_SIZE * sizeof(LogJson);
        _logQueueStorage = (uint8_t*)heap_caps_malloc(queueStorageSize, MALLOC_CAP_SPIRAM);
        
        if (_logQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT log queue (%d bytes)\n", queueStorageSize);
            return false;
        }

        // Create FreeRTOS static queue using PSRAM buffer
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

    bool _initializeMeterQueue() // Cannot use logger here to avoid circular dependency
    {
        if (_isMeterQueueInitialized) {
            return true;
        }

        // Allocate queue storage in PSRAM
        size_t queueStorageSize = MQTT_METER_QUEUE_SIZE * sizeof(PayloadMeter);
        _meterQueueStorage = (uint8_t*)heap_caps_malloc(queueStorageSize, MALLOC_CAP_SPIRAM);
        
        if (_meterQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT meter queue (%d bytes)\n", queueStorageSize);
            return false;
        }

        // Create FreeRTOS static queue using PSRAM buffer
        _meterQueue = xQueueCreateStatic(MQTT_METER_QUEUE_SIZE, sizeof(PayloadMeter), _meterQueueStorage, &_meterQueueStruct);
        if (_meterQueue == nullptr) {
            Serial.printf("[ERROR] Failed to create MQTT meter queue\n");
            heap_caps_free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            return false;
        }

        _isMeterQueueInitialized = true;
        Serial.printf("[DEBUG] MQTT meter queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", queueStorageSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
        // State will be updated in claim process completion
    }

    static void _handleSettingUpCertificatesState() {
        _setupMqttWithCertificates();
        // After setup, move to connecting state
        _setState(MqttState::CONNECTING);
    }

    static void _handleConnectingState() {
        if (_clientMqtt.connected()) {
            _setState(MqttState::CONNECTED);
        } else {
            if (millis64() >= _nextMqttConnectionAttemptMillis) {
                _connectMqtt();
                if (_clientMqtt.connected()) {
                    _setState(MqttState::CONNECTED);
                }
            }
        }
    }

    static void _handleConnectedState() {
        if (!_clientMqtt.connected()) {
            logger.debug("MQTT disconnected, transitioning to connecting state", TAG);
            _setState(MqttState::CONNECTING);
            return;
        }

        _clientMqtt.loop();  // Process incoming messages

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
                if (notificationValue > 0) {
                    _taskShouldRun = false;
                    break;
                }
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

            // State machine processing
            switch (_currentState) {
                case MqttState::IDLE:
                    _handleIdleState();
                    break;
                case MqttState::CLAIMING_CERTIFICATES:
                    _handleClaimingState();
                    break;
                case MqttState::SETTING_UP_CERTIFICATES:
                    _handleSettingUpCertificatesState();
                    break;
                case MqttState::CONNECTING:
                    _handleConnectingState();
                    break;
                case MqttState::CONNECTED:
                    _handleConnectedState();
                    break;
                case MqttState::ERROR:
                    // Reset to idle after error
                    _setState(MqttState::IDLE);
                    break;
            }
            
            // Wait for stop notification with timeout
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, xFrequency);
            if (notificationValue > 0) {
                _taskShouldRun = false;
                break;
            }
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

        logger.debug("Certificate %s validation passed", TAG, certType);
        return true;
    }

    // Simple error handling wrapper
    static bool _handleMqttError(const char* operation, int32_t errorCode = 0) {
        if (errorCode == MQTT_CONNECT_BAD_CREDENTIALS || errorCode == MQTT_CONNECT_UNAUTHORIZED) {
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

        if (!_setCertificates()) {
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

            _subscribeToTopics();

            // Publish data on connections
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

            // Use standardized error handling
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                return _handleMqttError("connection", currentState);
            }

            // Calculate next attempt time using exponential backoff
            uint64_t _backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_INITIAL_RETRY_INTERVAL, MQTT_MAX_RETRY_INTERVAL, MQTT_RETRY_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;
            logger.warning(
                "Failed to connect to MQTT (attempt %lu). Reason: %s. Next MQTT connection attempt in %llu ms", 
                TAG, 
                _mqttConnectionAttempt, _getMqttStateReason(currentState), _backoffDelay
            );

            return false;
        }
    }

    static bool _setCertificates() {
        _readEncryptedPreferences(PREFS_KEY_CERTIFICATE, preshared_encryption_key, _awsIotCoreCert, sizeof(_awsIotCoreCert));
        _readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, preshared_encryption_key, _awsIotCorePrivateKey, sizeof(_awsIotCorePrivateKey));

        // Ensure the certs are valid by looking for the PEM header
        if (strlen(_awsIotCoreCert) == 0 || 
            strlen(_awsIotCorePrivateKey) == 0 ||
            !strstr(_awsIotCoreCert, "-----BEGIN CERTIFICATE-----") ||
            (!strstr(_awsIotCorePrivateKey, "-----BEGIN PRIVATE KEY-----") && // Either PKCS#8 or PKCS#1
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

        _clientMqtt.setCallback(_subscribeCallback);
        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(aws_iot_core_cert_certclaim);
        _net.setPrivateKey(aws_iot_core_cert_privateclaim);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        logger.debug("MQTT setup for claiming certificates complete", TAG);

        int32_t connectionAttempt = 0;
        uint32_t loops = 0;
        uint64_t backoffDelay = 0;
        while (backoffDelay < MQTT_CLAIM_MAX_RETRY_INTERVAL && loops < MAX_LOOP_ITERATIONS && _taskShouldRun) {
            loops++;
            logger.debug(
                "Attempting to connect to MQTT for claiming certificates (%d/%d)...", 
                TAG, 
                connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT
            );

            if (_clientMqtt.connect(DEVICE_ID)) {
                logger.debug("Connected to MQTT for claiming certificates", TAG);
                break;
            }

            logger.warning(
                "Failed to connect to MQTT for claiming certificates (attempt %d). Reason: %s",
                TAG,
                connectionAttempt + 1,
                _getMqttStateReason(_clientMqtt.state())
            );

            backoffDelay = calculateExponentialBackoff(
                connectionAttempt,
                MQTT_CLAIM_INITIAL_RETRY_INTERVAL,
                MQTT_CLAIM_MAX_RETRY_INTERVAL,
                MQTT_CLAIM_RETRY_MULTIPLIER
            );
            _nextMqttConnectionAttemptMillis = millis64() + backoffDelay;
            logger.info("Next MQTT connection for claiming certificates attempt in %llu ms", TAG, _nextMqttConnectionAttemptMillis - millis64());

            connectionAttempt++;
            
            // Check for task stop notification during backoff delay
            if (backoffDelay > 0) {
                uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(min(backoffDelay, 1000ULL)));
                if (notificationValue > 0) {
                    logger.debug("MQTT claim process interrupted by stop notification", TAG);
                    _taskShouldRun = false;
                    return;
                }
            }
        }

        if (connectionAttempt >= MQTT_CLAIM_MAX_CONNECTION_ATTEMPT || !_taskShouldRun) {
            if (!_taskShouldRun) {
                logger.debug("MQTT claim process interrupted by stop notification", TAG);
                return;
            }
            logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", TAG, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
            _setState(MqttState::ERROR);
            setRestartSystem(TAG, "Failed to claim certificates");
            return;
        }

        _subscribeProvisioningResponse();
        
        int32_t publishAttempt = 0;
        loops = 0;
        while (publishAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && loops < MAX_LOOP_ITERATIONS && _taskShouldRun) {
            loops++;
            logger.debug("Attempting to publish provisioning request (%d/%d)...", TAG, publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_publishProvisioningRequest()) {
                logger.debug("Provisioning request published", TAG);
                break;
            }

            logger.warning(
                "Failed to publish provisioning request (%d/%d)",
                TAG,
                publishAttempt + 1,
                MQTT_CLAIM_MAX_CONNECTION_ATTEMPT
            );

            publishAttempt++;
            
            // Brief pause and check for stop notification
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            if (notificationValue > 0) {
                logger.debug("MQTT claim process interrupted by stop notification during publish attempts", TAG);
                _taskShouldRun = false;
                return;
            }
        }

        // Check if certificates were successfully provisioned
        if (_isDeviceCertificatesPresent()) {
            logger.debug("Certificates successfully provisioned", TAG);
            _setState(MqttState::SETTING_UP_CERTIFICATES);
        } else {
            logger.debug("Claim process completed but certificates not yet available", TAG);
            // Stay in claiming state, will retry on next loop
        }
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
        logger.debug("Setting up MQTT topics...", TAG);

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

    // TODO: use messagepack instead of JSON here for efficiency
    static void _meterQueueToJson(JsonDocument* jsonDocument) {
        JsonArray jsonArray = jsonDocument->to<JsonArray>();

        JsonObject jsonObject = jsonArray.add<JsonObject>();

        MeterValues meterValuesZeroChannel;
        Ade7953::getMeterValues(meterValuesZeroChannel, 0);

        // Early return if even channel 0 has no valid measurements
        if (meterValuesZeroChannel.lastUnixTimeMilliseconds == 0) {
            logger.debug("Meter values have zero unixTime, skipping...", TAG);
            return;
        }

        jsonObject["unixTime"] = meterValuesZeroChannel.lastUnixTimeMilliseconds;
        jsonObject["voltage"] = meterValuesZeroChannel.voltage;
                
        // Process all items from the queue
        PayloadMeter payloadMeter;
        uint32_t loops = 0;
        while (uxQueueMessagesWaiting(_meterQueue) > 0 && loops < MAX_LOOP_ITERATIONS && _isSendPowerDataEnabled()) {
            loops++;
            
            // Receive from queue (non-blocking)
            if (xQueueReceive(_meterQueue, &payloadMeter, 0) != pdTRUE) break; // No more items in queue

            if (payloadMeter.unixTimeMs == 0) {
                logger.debug("Payload meter has zero unixTime, skipping...", TAG);
                continue;
            }

            // TODO: to further optimize, remove the keys and only use the array index
            JsonObject jsonObject = jsonArray.add<JsonObject>();
            jsonObject["unixTime"] = payloadMeter.unixTimeMs;
            jsonObject["channel"] = payloadMeter.channel;
            jsonObject["activePower"] = payloadMeter.activePower;
            jsonObject["powerFactor"] = payloadMeter.powerFactor;
        }

        for (int32_t i = 0; i < CHANNEL_COUNT; i++) {
            if (Ade7953::isChannelActive(i)  && Ade7953::hasChannelValidMeasurements(i)) {
                JsonObject jsonObject = jsonArray.add<JsonObject>();

                MeterValues meterValues;
                Ade7953::getMeterValues(meterValues, i);

                if (meterValues.lastUnixTimeMilliseconds == 0) {
                    logger.debug("Meter values for channel %d have zero unixTime, skipping...", TAG, i);
                    continue;
                }

                jsonObject["unixTime"] = meterValues.lastUnixTimeMilliseconds;
                jsonObject["channel"] = i;
                jsonObject["activeEnergyImported"] = meterValues.activeEnergyImported;
                jsonObject["activeEnergyExported"] = meterValues.activeEnergyExported;
                jsonObject["reactiveEnergyImported"] = meterValues.reactiveEnergyImported;
                jsonObject["reactiveEnergyExported"] = meterValues.reactiveEnergyExported;
                jsonObject["apparentEnergy"] = meterValues.apparentEnergy;
            }
        }

        logger.debug("Meter queue converted to JSON", TAG);
    }

    static void _publishMeter() {
        JsonDocument jsonDocument;
        _meterQueueToJson(&jsonDocument);

        if (jsonDocument.isNull()) {
            logger.debug("No valid meter data to publish", TAG);
            return; // No data to publish
        }

        char meterMessage[JSON_MQTT_LARGE_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, meterMessage, sizeof(meterMessage));

        if (_publishMessage(_mqttTopicMeter, meterMessage)) {_publishMqtt.meter = false;}

        logger.debug("Meter data published to MQTT", TAG);
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

        if (_publishMessage(_mqttTopicSystemStatic, staticMessage)) {_publishMqtt.systemStatic = false;}

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

        if (_publishMessage(_mqttTopicSystemDynamic, dynamicMessage)) {_publishMqtt.systemDynamic = false;}

        logger.debug("System dynamic info published to MQTT", TAG);
    }

    static void _publishChannel() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        for (uint32_t i = 0; i < CHANNEL_COUNT; i++) {
            JsonDocument jsonChannelData;
            Ade7953::getChannelDataAsJson(jsonChannelData, i);
            jsonDocument["data"][i] = jsonChannelData;
        }

        char channelMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, channelMessage, sizeof(channelMessage));
     
        if (_publishMessage(_mqttTopicChannel, channelMessage)) {_publishMqtt.channel = false;}

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

        if (_publishMessage(_mqttTopicStatistics, statisticsMessage)) {_publishMqtt.statistics = false;}

        logger.debug("Statistics published to MQTT", TAG);
    }

    static void _publishCrash() {
        // TODO: upload all partition data (up to 64kB)
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        JsonDocument crashInfoJson;
        CrashMonitor::getCoreDumpInfoJson(crashInfoJson);
        jsonDocument["crashInfo"] = crashInfoJson;

        char crashMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, crashMessage, sizeof(crashMessage));

        if (_publishMessage(_mqttTopicCrash, crashMessage)) {_publishMqtt.crash = false; CrashMonitor::clearCoreDump();}

        logger.debug("Crash published to MQTT", TAG);
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
            logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.connected()) {
            logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", TAG, _getMqttStateReason(_clientMqtt.state()), topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.publish(topic, message, retain)) {
            logger.error("Failed to publish message on %s. MQTT client state: %s", TAG, topic, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        statistics.mqttMessagesPublished++;
        logger.verbose("Message published: %s", TAG, message); // Cannot be debug otherwise when remote debugging on MQTT is enabled, we get a loop
        return true;
    }

    static void _checkIfPublishMeterNeeded() {
        UBaseType_t queueSize = uxQueueMessagesWaiting(_meterQueue);
        if (
            ((queueSize > (MQTT_METER_QUEUE_SIZE * MQTT_METER_QUEUE_ALMOST_FULL_THRESHOLD)) && _isSendPowerDataEnabled()) || 
            (millis64() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH
        ) { // Either queue is almost full (and we are sending power data) or enough time has passed
            _publishMqtt.meter = true;
            _lastMillisMeterPublished = millis64();
            logger.debug("Set flag to publish %d meter data points", TAG, queueSize);
        }
    }

    static void _checkIfPublishSystemDynamicNeeded() {
        if ((millis64() - _lastMillisSystemDynamicPublished) > MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH) {
            _publishMqtt.systemDynamic = true;
            _lastMillisSystemDynamicPublished = millis64();
            logger.debug("Set flag to publish system dynamic", TAG);
        }
    }
    
    static void _checkIfPublishStatisticsNeeded() {
        if ((millis64() - _lastMillisStatisticsPublished) > MQTT_MAX_INTERVAL_STATISTICS_PUBLISH) {
            _publishMqtt.statistics = true;
            _lastMillisStatisticsPublished = millis64();
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
        _subscribeEraseCertificates();
        _subscribeEnableDebugLogging(); 

        logger.debug("Subscribed to topics", TAG);
    }

    // Helper function to reduce code duplication in subscription functions
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

    static void _subscribeUpdateFirmware() {
        _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE, "firmware update");
    }

    static void _subscribeRestart() {
        _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, "restart");
    }

    static void _subscribeEraseCertificates() {
        _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES, "erase certificates");
    }

    static void _subscribeEnableDebugLogging() { 
        _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING, "enable debug logging");
    }

    static void _subscribeProvisioningResponse() {
        _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, "provisioning response");
    }

    // Queue processing functions
    static void _processLogQueue()
    {
        LogJson logEntry;
        
        // Process all available log entries (non-blocking)
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
        // Generate the MQTT topic
        char logTopic[MQTT_TOPIC_BUFFER_SIZE];
        snprintf(logTopic, sizeof(logTopic), 
                "%s/%s", 
                _mqttTopicLog, logEntry.level);
        
        // Generate JSON message
        char logMessage[JSON_MQTT_BUFFER_SIZE];
        snprintf(logMessage, sizeof(logMessage),
            "{\"timestamp\":\"%s\","
            "\"millis\":%llu,"
            "\"core\":%lu,"
            "\"function\":\"%s\","
            "\"message\":\"%s\"}",
            logEntry.timestamp,
            logEntry.millisEsp,
            logEntry.coreId,
            logEntry.function,
            logEntry.message);
        
        // Publish the log entry
        if (!_publishMessage(logTopic, logMessage)) xQueueSendToFront(_logQueue, &logEntry, 0);
    }
    
    // Helper functions for preferences - now using centralized PreferencesConfig
    
    static void _loadConfigFromPreferences()
    {
        logger.debug("Loading MQTT preferences...", TAG);

        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences. Setting defaults.", TAG);
            _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
            _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;
            snprintf(_firmwareUpdateUrl, sizeof(_firmwareUpdateUrl), "%s", "");
            snprintf(_firmwareUpdateVersion, sizeof(_firmwareUpdateVersion), "%s", "");
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

        _saveConfigToPreferences(); // Save loaded config to ensure it's up-to-date

        // Start task if cloud services are enabled
        if (_cloudServicesEnabled) _startTask();
    }

    static void _saveConfigToPreferences()
    {
        logger.debug("Saving MQTT preferences...", TAG);
        
        _saveCloudServicesEnabledToPreferences(_cloudServicesEnabled);
        _saveSendPowerDataEnabledToPreferences(_sendPowerDataEnabled);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);
        
        logger.debug("MQTT preferences saved", TAG);
    }

    // Private function implementations
    // =========================================================

    static void _startTask()
    {
        if (_taskHandle != nullptr) {
            logger.debug("MQTT task is already running", TAG);
            return;
        }

        logger.debug("Starting MQTT task", TAG);

        // Initialize queues if not already done
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
        logger.debug("MQTT queues created with PSRAM - Log: %d bytes, Meter: %d bytes", TAG,
                   logQueueStorageSize, meterQueueStorageSize);

        // Configure MQTT client before starting task
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
    
    static void _setCloudServicesEnabled(bool enabled)
    {
        _cloudServicesEnabled = enabled;        
        _saveCloudServicesEnabledToPreferences(enabled);
        
        // Start or stop task based on new state
        if (enabled) _startTask();
        else _stopTask();

        logger.debug("Set cloud services enabled to %s", TAG, enabled ? "true" : "false");
    }

    static void _setSendPowerDataEnabled(bool enabled)
    {
        _sendPowerDataEnabled = enabled;
        _saveSendPowerDataEnabledToPreferences(enabled);

        logger.debug("Set send power data enabled to %s", TAG, enabled ? "true" : "false");
    }
    
    static void _setFirmwareUpdateVersion(const char* version) {
        if (version == nullptr) {
            logger.error("Invalid firmware update version provided", TAG);
            return;
        }

        snprintf(_firmwareUpdateVersion, sizeof(_firmwareUpdateVersion), "%s", version);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);

        logger.debug("Firmware update version set to %s", TAG, _firmwareUpdateVersion);
    }

    static void _setFirmwareUpdateUrl(const char* url) {
        if (url == nullptr) {
            logger.error("Invalid firmware update URL provided", TAG);
            return;
        }

        snprintf(_firmwareUpdateUrl, sizeof(_firmwareUpdateUrl), "%s", url);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);

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
    // =======================

    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize) {
        if (!preference_key || !preshared_encryption_key || !decryptedData || decryptedDataSize == 0) {
            if (decryptedData && decryptedDataSize > 0) {
                decryptedData[0] = '\0'; // Ensure null termination on error
            }
            return;
        }

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            logger.error("Failed to open preferences", TAG);
        }

        char encryptedData[CERTIFICATE_BUFFER_SIZE];
        preferences.getString(preference_key, encryptedData, CERTIFICATE_BUFFER_SIZE);
        preferences.end();

        if (strlen(encryptedData) == 0) {
            logger.warning("No encrypted data found for key: %s", TAG, preference_key);
            return;
        }

        char encryptionKey[ENCRYPTION_KEY_BUFFER_SIZE];
        // The final decryption key is a combination of the preshared key and the device ID
        snprintf(encryptionKey, ENCRYPTION_KEY_BUFFER_SIZE, "%s%s", preshared_encryption_key, DEVICE_ID);

        _decryptData(encryptedData, encryptionKey, decryptedData, decryptedDataSize);
    }

    static bool _isDeviceCertificatesPresent() {
        logger.debug("Checking if device certificates are present...", TAG);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            logger.error("Failed to open preferences", TAG);
            return false;
        }

        char buffer[CERTIFICATE_BUFFER_SIZE];

        size_t deviceCertLen = preferences.getString(PREFS_KEY_CERTIFICATE, buffer, sizeof(buffer));
        bool _deviceCertExists = (
            deviceCertLen >= MINIMUM_CERTIFICATE_LENGTH && 
            deviceCertLen < CERTIFICATE_BUFFER_SIZE
        );
        logger.debug("Device certificate length: %zu | Certs present: %s | Cert: %s", TAG, deviceCertLen, _deviceCertExists ? "yes" : "no", buffer);

        size_t privateKeyLen = preferences.getString(PREFS_KEY_PRIVATE_KEY, buffer, sizeof(buffer));
        bool _privateKeyExists = (
            privateKeyLen >= MINIMUM_CERTIFICATE_LENGTH && 
            privateKeyLen < CERTIFICATE_BUFFER_SIZE
        );
        logger.debug("Private key length: %zu | Certs present: %s | Cert: %s", TAG, privateKeyLen, _privateKeyExists ? "yes" : "no", buffer);

        preferences.end();

        bool _allCertificatesExist = _deviceCertExists && _privateKeyExists;
        return _allCertificatesExist;
    }

    static void _clearCertificates() {
        logger.debug("Clearing certificates...", TAG);

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

    void _decryptData(const char* encryptedData, const char* key, char* decryptedData, size_t decryptedDataSize) {
        // Early validation and consistent error handling
        if (!encryptedData || !key || !decryptedData || decryptedDataSize == 0) {
            if (decryptedData && decryptedDataSize > 0) {
                decryptedData[0] = '\0';
            }
            logger.warning("Invalid parameters for decryption", TAG);
            return;
        }

        size_t inputLength = strlen(encryptedData);
        if (inputLength == 0) {
            decryptedData[0] = '\0';
            logger.warning("No encrypted data provided for decryption", TAG);
            return;
        }

        size_t keyLength = strlen(key);
        if (keyLength != 32) {
            decryptedData[0] = '\0';
            logger.error("Invalid key length: %zu. Expected 32 bytes", TAG, keyLength);
            return;
        }

        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_dec(&aes, (const uint8_t *)key, 256); // 256 bits for AES-256

        // Use stack-allocated buffers instead of malloc
        uint8_t decodedData[CERTIFICATE_BUFFER_SIZE];
        uint8_t output[CERTIFICATE_BUFFER_SIZE];

        size_t decodedLength = 0;
        int32_t ret = mbedtls_base64_decode(decodedData, sizeof(decodedData), &decodedLength,
                                    (const uint8_t *)encryptedData, inputLength);
        
        if (ret != 0 || decodedLength == 0) {
            decryptedData[0] = '\0';
            mbedtls_aes_free(&aes);
            logger.error("Failed to decode base64 data or no data decoded", TAG);
            return;
        }
        
        // Decrypt in 16-byte blocks
        for (size_t i = 0; i < decodedLength; i += 16) {
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, &decodedData[i], &output[i]);
        }
        
        // Handle PKCS7 padding removal (proper implementation)
        if (decodedLength > 0) {
            uint8_t paddingLength = output[decodedLength - 1];
            
            // Validate PKCS7 padding
            if (paddingLength > 0 && paddingLength <= 16 && paddingLength <= decodedLength) {
                // Verify all padding bytes are correct
                bool validPadding = true;
                for (size_t i = decodedLength - paddingLength; i < decodedLength; i++) {
                    if (output[i] != paddingLength) {
                        validPadding = false;
                        break;
                    }
                }
                
                if (validPadding) {
                    // Remove padding
                    size_t actualLength = decodedLength - paddingLength;
                    output[actualLength] = '\0';
                    
                    // Ensure we don't exceed our output buffer
                    if (actualLength >= decryptedDataSize) {
                        actualLength = decryptedDataSize - 1;
                        output[actualLength] = '\0';
                    }
                } else {
                    logger.warning("Invalid PKCS7 padding detected, using fallback", TAG);
                    // Fallback: find last printable character for certificates
                    size_t actualLength = decodedLength;
                    for (size_t i = decodedLength - 1; i > 0; i--) {
                        if (output[i] >= 32 && output[i] <= 126) { // Printable ASCII
                            actualLength = i + 1;
                            break;
                        }
                    }
                    if (actualLength >= decryptedDataSize) {
                        actualLength = decryptedDataSize - 1;
                    }
                    output[actualLength] = '\0';
                }
            } else {
                logger.warning("Invalid padding length: %d", TAG, paddingLength);
                // Fallback: ensure null termination
                if (decodedLength >= decryptedDataSize) {
                    decodedLength = decryptedDataSize - 1;
                }
                output[decodedLength] = '\0';
            }
        }
        
        // Copy result safely
        snprintf(decryptedData, decryptedDataSize, "%s", (char*)output);

        // Clear sensitive data from stack buffers for security
        memset(decodedData, 0, sizeof(decodedData));
        memset(output, 0, sizeof(output));

        mbedtls_aes_free(&aes);
        logger.debug("Decrypted data successfully", TAG);
    }
}
#endif