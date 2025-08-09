#if HAS_SECRETS
#include "mqtt.h"

namespace Mqtt
{
    // Static variables
    // ================
    // ================
    
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

    // MQTT State Machine
    enum class MqttState {
        IDLE,
        CLAIMING_CERTIFICATES,
        SETTING_UP_CERTIFICATES,
        CONNECTING,
        CONNECTED
    };
    
    // State variables
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
    // =============================
    // =============================

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
        
    // Task management
    static void _startTask();
    static void _stopTask();
    static void _mqttTask(void *parameter);

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

    // Subscription management
    static void _subscribeToTopics();
    static bool _subscribeToTopic(const char* topicSuffix);
    static void _subscribeUpdateFirmware();
    static void _subscribeRestart();
    static void _subscribeProvisioningResponse();
    static void _subscribeEraseCertificates();
    static void _subscribeSetSendPowerData();
    static void _subscribeEnableDebugLogging();
    
    // Subscription callback handler
    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length);
    static void _handleFirmwareUpdateMessage(const char* message);
    static void _handleRestartMessage();
    static void _handleProvisioningResponseMessage(const char* message);
    static void _handleEraseCertificatesMessage();
    static void _handleSetSendPowerDataMessage(const char* message);
    static void _handleEnableDebugLoggingMessage(const char* message);
    
    // Publishing functions
    static void _publishMeter();
    static void _publishSystemStatic();
    static void _publishSystemDynamic();
    static void _publishChannel();
    static void _publishStatistics();
    static void _publishCrash();
    static void _publishLog(const LogEntry& entry);
    static bool _publishProvisioningRequest();
    
    static void _checkPublishMqtt();
    static void _checkIfPublishMeterNeeded();
    static void _checkIfPublishSystemDynamicNeeded();
    static void _checkIfPublishStatisticsNeeded();

    // MQTT operations
    static bool _setCertificatesFromPreferences();
    static bool _setupMqttWithDeviceCertificates();
    static bool _connectMqtt();
    static bool _claimProcess();
    static bool _publishMessage(const char* topic, const char* message, bool retain = false);

    // Queue processing and streaming
    static void _processLogQueue();
    static bool _publishMeterJson();
    static bool _publishCrashJson();
    
    // Certificate management
    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize);
    static bool _isDeviceCertificatesPresent();
    static void _clearCertificates();
    static void _decryptData(const char* encryptedDataBase64, const char* presharedKey, char* decryptedData, size_t decryptedDataSize);
    static void _deriveKey_SHA256(const char* presharedKey, const char* deviceId, uint8_t outKey32[32]);
    static bool _validateCertificateFormat(const char* cert, const char* certType);

    // State machine management
    static void _setState(MqttState newState);
    static void _handleIdleState();
    static void _handleClaimingState();
    static void _handleSettingUpCertificatesState();
    static void _handleConnectingState();
    static void _handleConnectedState();

    // Utilities
    static const char* _getMqttStateReason(int32_t state);
    static const char* _getMqttStateMachineName(MqttState state);

    // Public API functions
    // ====================
    // ====================

    void begin()
    {
        LOG_DEBUG("Setting up MQTT client...");
        
        // Create configuration mutex
        if (_configMutex == nullptr) {
            _configMutex = xSemaphoreCreateMutex();
            if (_configMutex == nullptr) {
                LOG_ERROR("Failed to create configuration mutex");
                return;
            }
        }

        // Initialize RTC debug flags
        if (_debugFlagsRtc.signature != MAGIC_WORD_RTC) {
            LOG_DEBUG("RTC magic word is invalid, resetting debug flags");
            _debugFlagsRtc.enableMqttDebugLogging = false;
            _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
            _debugFlagsRtc.signature = MAGIC_WORD_RTC;
        } else if (_debugFlagsRtc.enableMqttDebugLogging) {
            // If the RTC is valid, and before the restart we were logging with debug info, reset the clock and start again the debugging
            LOG_DEBUG("RTC debug logging was enabled, restarting duration of %lu ms", _debugFlagsRtc.mqttDebugLoggingDurationMillis);
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = _debugFlagsRtc.mqttDebugLoggingDurationMillis;
        }

        _setupTopics();
        _loadConfigFromPreferences();
        _initializeLogQueue();
        _initializeMeterQueue();

        if (_cloudServicesEnabled) _startTask();
        else LOG_DEBUG("Cloud services are disabled, MQTT task will not start");
        
        LOG_DEBUG("MQTT client setup complete");
    }

    void stop()
    {
        LOG_DEBUG("Stopping MQTT client...");
        _stopTask();
        
        _logQueue = nullptr;
        _meterQueue = nullptr;
        
        if (_logQueueStorage != nullptr) {
            heap_caps_free(_logQueueStorage);
            _logQueueStorage = nullptr;
            LOG_DEBUG("MQTT log queue PSRAM freed");
        }
        
        if (_meterQueueStorage != nullptr) {
            heap_caps_free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            LOG_DEBUG("MQTT meter queue PSRAM freed");
        }

        if (_configMutex != nullptr) {
            vSemaphoreDelete(_configMutex);
            _configMutex = nullptr;
        }

        // Zeroize decrypted material
        memset(_awsIotCoreCert, 0, sizeof(_awsIotCoreCert));
        memset(_awsIotCorePrivateKey, 0, sizeof(_awsIotCorePrivateKey));
        
        LOG_INFO("MQTT client stopped");
    }

    // Cloud services methods
    // ======================

    void setCloudServicesEnabled(bool enabled)
    {
        if (_configMutex == nullptr) begin();
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for setCloudServicesEnabled");
            return;
        }

        if (_cloudServicesEnabled == enabled) {
            LOG_DEBUG("Cloud services already set to %s, skipping", enabled ? "enabled" : "disabled");
            releaseMutex(&_configMutex);
            return;
        }

        LOG_DEBUG("Setting cloud services to %s...", enabled ? "enabled" : "disabled");
        
        _stopTask();

        _cloudServicesEnabled = enabled;
        _saveCloudServicesEnabledToPreferences(enabled);

        if (_cloudServicesEnabled) _startTask();
        
        releaseMutex(&_configMutex);

        LOG_INFO("Cloud services %s", enabled ? "enabled" : "disabled");
    }

    bool isCloudServicesEnabled() { return _cloudServicesEnabled; }

    // Firmware update methods
    // =======================

    void getFirmwareUpdateVersion(char* buffer, size_t bufferSize) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for getFirmwareUpdateVersion");
            snprintf(buffer, bufferSize, "%s", "");
            return;
        }
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateVersion);
        releaseMutex(&_configMutex);
    }

    void getFirmwareUpdateUrl(char* buffer, size_t bufferSize) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for getFirmwareUpdateUrl");
            snprintf(buffer, bufferSize, "%s", "");
            return;
        }
        snprintf(buffer, bufferSize, "%s", _firmwareUpdateUrl);
        releaseMutex(&_configMutex);
    }

    bool isLatestFirmwareInstalled() {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for isLatestFirmwareInstalled");
            return false; // Assume not installed if we can't access
        }

        if (strlen(_firmwareUpdateVersion) == 0 || strchr(_firmwareUpdateVersion, '.') == NULL) {
            LOG_DEBUG("Latest firmware version is empty or in the wrong format");
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
    // ===============================================

    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestCrashPublish() {_publishMqtt.crash = true; }

    // Public methods for pushing data to queues
    // =========================================

    void pushLog(const LogEntry& entry)
    {
        if (!_initializeLogQueue()) return;

        if ((entry.level == LogLevel::DEBUG && (!_debugFlagsRtc.enableMqttDebugLogging && _debugFlagsRtc.signature == MAGIC_WORD_RTC)) || // DEBUG level and (real) debug logging disabled
            (entry.level == LogLevel::VERBOSE)) { // Never send verbose logs via MQTT
            return;
        }
        
        xQueueSend(_logQueue, &entry, pdMS_TO_TICKS(QUEUE_WAIT_TIMEOUT));
    }

    void pushMeter(const PayloadMeter& payload)
    {
        if (!_initializeMeterQueue()) return;
        xQueueSend(_meterQueue, &payload, pdMS_TO_TICKS(QUEUE_WAIT_TIMEOUT));
    }

    TaskInfo getTaskInfo()
    {
        if (_taskHandle != nullptr) {
            return TaskInfo(MQTT_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(_taskHandle));
        } else {
            return TaskInfo(); // Return empty/default TaskInfo if task is not running
        }
    }

    // Private functions
    // =================
    // =================


    // MQTT log queue management
    // =========================

    bool _initializeLogQueue() // Cannot use logger here to avoid recursion
    {
        if (_logQueueStorage != nullptr) return true;

        // Allocate queue storage in PSRAM
        uint32_t queueLength = MQTT_LOG_QUEUE_SIZE / sizeof(LogEntry);
        size_t realQueueSize = queueLength * sizeof(LogEntry);
        _logQueueStorage = (uint8_t*)heap_caps_malloc(realQueueSize, MALLOC_CAP_SPIRAM);

        if (_logQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT log queue (%d bytes)\n", realQueueSize);
            return false;
        }

        _logQueue = xQueueCreateStatic(queueLength, sizeof(LogEntry), _logQueueStorage, &_logQueueStruct);
        if (_logQueue == nullptr) {
            Serial.println("[ERROR] Failed to create MQTT log queue");
            heap_caps_free(_logQueueStorage);
            _logQueueStorage = nullptr;
            return false;
        }

        Serial.printf("[DEBUG] MQTT log queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    bool _initializeMeterQueue()
    {
        if (_meterQueueStorage != nullptr) return true;

        // Allocate queue storage in PSRAM
        uint32_t queueLength = MQTT_METER_QUEUE_SIZE / sizeof(PayloadMeter);
        size_t realQueueSize = queueLength * sizeof(PayloadMeter);
        _meterQueueStorage = (uint8_t*)heap_caps_malloc(realQueueSize, MALLOC_CAP_SPIRAM);

        if (_meterQueueStorage == nullptr) {
            LOG_ERROR("Failed to allocate PSRAM for MQTT meter queue (%zu bytes)\n", realQueueSize);
            return false;
        }

        _meterQueue = xQueueCreateStatic(queueLength, sizeof(PayloadMeter), _meterQueueStorage, &_meterQueueStruct);
        if (_meterQueue == nullptr) {
            LOG_ERROR("Failed to create MQTT meter queue\n");
            heap_caps_free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            return false;
        }

        LOG_DEBUG("MQTT meter queue initialized with PSRAM buffer (%zu bytes) | Free PSRAM: %zu bytes\n", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    // Configuration management
    // ========================
    
    static void _loadConfigFromPreferences()
    {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for loading preferences");
            return;
        }

        Preferences prefs;
        prefs.begin(PREFERENCES_NAMESPACE_MQTT, true);

        _cloudServicesEnabled = prefs.getBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, DEFAULT_CLOUD_SERVICES_ENABLED);
        _sendPowerDataEnabled = prefs.getBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, DEFAULT_SEND_POWER_DATA_ENABLED);
        prefs.getString(MQTT_PREFERENCES_FW_UPDATE_URL_KEY, _firmwareUpdateUrl, sizeof(_firmwareUpdateUrl));
        prefs.getString(MQTT_PREFERENCES_FW_UPDATE_VERSION_KEY, _firmwareUpdateVersion, sizeof(_firmwareUpdateVersion));
        
        LOG_DEBUG("Cloud services enabled: %s, Send power data enabled: %s", 
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false");
        LOG_DEBUG("Firmware update | URL: %s, Version: %s", 
                   _firmwareUpdateUrl,
                   _firmwareUpdateVersion);

        prefs.end();
        releaseMutex(&_configMutex);

        _saveConfigToPreferences();
        LOG_DEBUG("MQTT preferences loaded");
    }

    static void _saveConfigToPreferences()
    {
        _saveCloudServicesEnabledToPreferences(_cloudServicesEnabled);
        _saveSendPowerDataEnabledToPreferences(_sendPowerDataEnabled);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);
        
        LOG_DEBUG("MQTT preferences saved");
    }

    static void _setSendPowerDataEnabled(bool enabled)
    {
        _sendPowerDataEnabled = enabled;
        _saveSendPowerDataEnabledToPreferences(enabled);
        LOG_DEBUG("Set send power data enabled to %s", enabled ? "true" : "false");
    }
    
    static void _setFirmwareUpdateVersion(const char* version) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for setting firmware update version");
            return;
        }

        if (version == nullptr) {
            LOG_ERROR("Invalid firmware update version provided");
            releaseMutex(&_configMutex);
            return;
        }

        snprintf(_firmwareUpdateVersion, sizeof(_firmwareUpdateVersion), "%s", version);
        _saveFirmwareUpdateVersionToPreferences(_firmwareUpdateVersion);
        releaseMutex(&_configMutex);

        LOG_DEBUG("Firmware update version set to %s", _firmwareUpdateVersion);
    }

    static void _setFirmwareUpdateUrl(const char* url) {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for setting firmware update URL");
            return;
        }

        if (url == nullptr) {
            LOG_ERROR("Invalid firmware update URL provided");
            releaseMutex(&_configMutex);
            return;
        }

        snprintf(_firmwareUpdateUrl, sizeof(_firmwareUpdateUrl), "%s", url);
        _saveFirmwareUpdateUrlToPreferences(_firmwareUpdateUrl);
        releaseMutex(&_configMutex);

        LOG_DEBUG("Firmware update URL set to %s", _firmwareUpdateUrl);
    }

    static void _saveCloudServicesEnabledToPreferences(bool enabled) {
        Preferences prefs;
        
        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, enabled);
        if (bytesWritten == 0) LOG_ERROR("Failed to save cloud services enabled preference");

        prefs.end();
    }

    static void _saveSendPowerDataEnabledToPreferences(bool enabled) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, enabled);
        if (bytesWritten == 0) LOG_ERROR("Failed to save send power data enabled preference");
        
        prefs.end();
    }

    static void _saveFirmwareUpdateVersionToPreferences(const char* version) {
        Preferences prefs;
        
        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putString(MQTT_PREFERENCES_FW_UPDATE_VERSION_KEY, version);
        if (bytesWritten != strlen(version)) {
            LOG_ERROR(
                "Failed to save firmware update version preference (written %zu | expected %zu): %s",
                bytesWritten, strlen(version), version
            );
        }

        prefs.end();
    }

    static void _saveFirmwareUpdateUrlToPreferences(const char* url) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putString(MQTT_PREFERENCES_FW_UPDATE_URL_KEY, url);
        if (bytesWritten != strlen(url)) {
            LOG_ERROR(
                "Failed to save firmware update URL preference (written %zu | expected %zu): %s", 
                bytesWritten, strlen(url), url
            );
        }

        prefs.end();
    }

    // Task management
    // ===============

    static void _startTask()
    {
        if (_taskHandle != nullptr) {
            LOG_DEBUG("MQTT task is already running");
            return;
        }

        LOG_DEBUG("Starting MQTT task");

        if (!_initializeLogQueue()) {
            LOG_ERROR("Failed to initialize MQTT log queue");
            return;
        }

        if (!_initializeMeterQueue()) {
            LOG_ERROR("Failed to initialize MQTT meter queue");
            return;
        }

        _clientMqtt.setBufferSize(JSON_MQTT_BUFFER_SIZE);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        _nextMqttConnectionAttemptMillis = 0;
        _mqttConnectionAttempt = 0;
        
        BaseType_t result = xTaskCreate(
            _mqttTask,
            MQTT_TASK_NAME,
            MQTT_TASK_STACK_SIZE,
            nullptr,
            MQTT_TASK_PRIORITY,
            &_taskHandle
        );

        if (result != pdPASS) {
            LOG_ERROR("Failed to create MQTT task");
            _taskHandle = nullptr;
        }
    }

    static void _stopTask() { stopTaskGracefully(&_taskHandle, "MQTT task"); }

    static void _mqttTask(void *parameter)
    {
        LOG_DEBUG("MQTT task started");
        
        _taskShouldRun = true;

        while (_taskShouldRun)
        {
            // Check if debug logs have expired
            if (_debugFlagsRtc.enableMqttDebugLogging && millis64() > _debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
                _debugFlagsRtc.enableMqttDebugLogging = false;
                _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
                _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
                _debugFlagsRtc.signature = 0;
                LOG_DEBUG("The MQTT debug period has ended");
            }

            // Skip processing if WiFi is not connected
            if (CustomWifi::isFullyConnected()) {
                switch (_currentState) {
                    case MqttState::IDLE:                    _handleIdleState(); break;
                    case MqttState::CLAIMING_CERTIFICATES:   _handleClaimingState(); break;
                    case MqttState::SETTING_UP_CERTIFICATES: _handleSettingUpCertificatesState(); break;
                    case MqttState::CONNECTING:              _handleConnectingState(); break;
                    case MqttState::CONNECTED:               _handleConnectedState(); break;
                }
            }
            
            uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
            if (notificationValue > 0) { _taskShouldRun = false; break; }
        }
        
        _clientMqtt.disconnect();
        _setState(MqttState::IDLE);
        LOG_DEBUG("MQTT task stopping");
        _taskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    // Topic management
    // ================

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

        LOG_DEBUG("Constructing MQTT topic with rule for %s | %s", finalTopic, topic);
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

        LOG_DEBUG("Constructing MQTT topic for %s | %s", finalTopic, topic);
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

        LOG_DEBUG("MQTT topics setup complete");
    }

    // static void _setTopicMeter() { _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); } // FIXME
    static void _setTopicMeter() { _constructMqttTopic(MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicSystemStatic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_STATIC, _mqttTopicSystemStatic, sizeof(_mqttTopicSystemStatic)); }
    static void _setTopicSystemDynamic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_DYNAMIC, _mqttTopicSystemDynamic, sizeof(_mqttTopicSystemDynamic)); }
    static void _setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel, sizeof(_mqttTopicChannel)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }
    static void _setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash, sizeof(_mqttTopicCrash)); }
    static void _setTopicLog() { _constructMqttTopic(MQTT_TOPIC_LOG, _mqttTopicLog, sizeof(_mqttTopicLog)); }
    static void _setTopicProvisioningRequest() { _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, _mqttTopicProvisioningRequest, sizeof(_mqttTopicProvisioningRequest)); }

    static void _subscribeToTopics() {
        _subscribeUpdateFirmware();
        _subscribeRestart();
        _subscribeProvisioningResponse();
        _subscribeEraseCertificates();
        _subscribeSetSendPowerData();
        _subscribeEnableDebugLogging();

        LOG_DEBUG("Subscribed to topics");
    }

    static bool _subscribeToTopic(const char* topicSuffix) {
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(topicSuffix, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_WARNING("Failed to subscribe to %s", topicSuffix);
            return false;
        }

        LOG_DEBUG("Subscribed to topic: %s", topicSuffix);
        return true;
    }

    static void _subscribeUpdateFirmware() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE); }
    static void _subscribeRestart() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_RESTART); }
    static void _subscribeProvisioningResponse() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE); }
    static void _subscribeEraseCertificates() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES); }
    static void _subscribeSetSendPowerData() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_SET_SEND_POWER_DATA); }
    static void _subscribeEnableDebugLogging() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING); }

    // Subscription callback handler
    // =============================

    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length)
    {
        char message[MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE];
        
        // Ensure we don't exceed buffer bounds
        uint32_t maxLength = sizeof(message) - 1; // Reserve space for null terminator
        if (length > maxLength) {
            LOG_WARNING("MQTT message from topic %s too large (%u bytes), truncating to %u", topic, length, maxLength);
            length = maxLength;
        }
        
        snprintf(message, sizeof(message), "%.*s", (int)length, (char*)payload);

        LOG_DEBUG("Received MQTT message from %s", topic);

        if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_FIRMWARE_UPDATE)) _handleFirmwareUpdateMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_RESTART)) _handleRestartMessage();
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) _handleProvisioningResponseMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES)) _handleEraseCertificatesMessage();
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_SET_SEND_POWER_DATA)) _handleSetSendPowerDataMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING)) _handleEnableDebugLoggingMessage(message);
        else LOG_WARNING("Unknown MQTT topic received: %s", topic);
    }

    static void _handleFirmwareUpdateMessage(const char* message)
    {
        // Expected JSON format: {"version": "1.0.0", "url": "https://example.com/firmware.bin"}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            LOG_ERROR("Failed to parse firmware update JSON message: %s", message);
            return;
        }

        if (jsonDocument["version"].is<const char*>()) _setFirmwareUpdateVersion(jsonDocument["version"].as<const char*>());
        if (jsonDocument["url"].is<const char*>()) _setFirmwareUpdateUrl(jsonDocument["url"].as<const char*>());
    }

    static void _handleRestartMessage()
    {
        setRestartSystem("Restart requested from MQTT");
    }

    static void _handleProvisioningResponseMessage(const char* message)
    {
        // Expected JSON format: {"status": "success", "encryptedCertificatePem": "...", "encryptedPrivateKey": "..."}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            LOG_ERROR("Failed to parse provisioning response JSON message");
            return;
        }

        if (jsonDocument["status"] == "success")
        {
            // Validate that certificate fields exist and are strings
            if (!jsonDocument["encryptedCertificatePem"].is<const char*>() || 
                !jsonDocument["encryptedPrivateKey"].is<const char*>()) 
            {
                LOG_ERROR("Invalid provisioning response: missing or invalid certificate fields");
                return;
            }

            const char* certData = jsonDocument["encryptedCertificatePem"].as<const char*>();
            const char* keyData = jsonDocument["encryptedPrivateKey"].as<const char*>();
            
            size_t certLen = strlen(certData);
            size_t keyLen = strlen(keyData);
            
            if (certLen >= CERTIFICATE_BUFFER_SIZE || keyLen >= CERTIFICATE_BUFFER_SIZE) {
                LOG_ERROR("Provisioning payload too large (cert %zu, key %zu)", certLen, keyLen);
                return;
            }

            Preferences preferences;
            preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false);

            size_t writtenSize = preferences.putString(PREFS_KEY_CERTIFICATE, certData);
            if (writtenSize != certLen) {
                LOG_ERROR("Failed to write encrypted certificate to preferences (%zu != %zu)", writtenSize, certLen);
                preferences.end();
                return;
            }

            writtenSize = preferences.putString(PREFS_KEY_PRIVATE_KEY, keyData);
            if (writtenSize != keyLen) {
                LOG_ERROR("Failed to write encrypted private key to preferences (%zu != %zu)", writtenSize, keyLen);
                preferences.end();
                return;
            }

            preferences.end();
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(jsonDocument, buffer, sizeof(buffer), true);
            LOG_ERROR("Provisioning failed: %s", buffer);
        }
    }

    static void _handleEraseCertificatesMessage()
    {
        _clearCertificates();
        _setState(MqttState::IDLE);
    }

    static void _handleSetSendPowerDataMessage(const char* message)
    {
        // Expected JSON format: {"sendPowerData": true}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            LOG_ERROR("Failed to parse send power data JSON message: %s", message);
            return;
        }
        
        if (jsonDocument["sendPowerData"].is<bool>()) {
            bool sendPowerData = jsonDocument["sendPowerData"].as<bool>();
            _setSendPowerDataEnabled(sendPowerData);
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(jsonDocument, buffer, sizeof(buffer), true);
            LOG_ERROR("Invalid send power data JSON message: %s", buffer);
        }
    }

    static void _handleEnableDebugLoggingMessage(const char* message)
    {
        // Expected JSON format: {"enable": true, "duration_minutes": 10}
        JsonDocument jsonDocument;
        if (deserializeJson(jsonDocument, message)) {
            LOG_ERROR("Failed to parse enable debug logging JSON message: %s", message);
            return;
        }

        if (jsonDocument["enable"].is<bool>())
        {
            if (jsonDocument["enable"].as<bool>()) {
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
            } else {
                _debugFlagsRtc.enableMqttDebugLogging = false;
                _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
                _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
                _debugFlagsRtc.signature = 0;
            }
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(jsonDocument, buffer, sizeof(buffer), true);
            LOG_ERROR("Invalid enable debug logging JSON message: %s", buffer);
        }
    }

    // Publishing functions
    // ====================

    static void _publishMeter() {
        // Check if we have any data to publish
        MeterValues meterValuesZeroChannel;
        Ade7953::getMeterValues(meterValuesZeroChannel, 0);
        UBaseType_t queueSize = _meterQueue ? uxQueueMessagesWaiting(_meterQueue) : 0;
        
        bool hasVoltageData = (meterValuesZeroChannel.lastUnixTimeMilliseconds > 0);
        bool hasQueueData = (queueSize > 0 && _sendPowerDataEnabled);
        bool hasChannelData = false;
        
        // Check if any channels have valid data
        for (uint8_t i = 0; i < CHANNEL_COUNT && !hasChannelData; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                hasChannelData = true;
            }
        }
        
        if (!hasVoltageData && !hasQueueData && !hasChannelData) {
            LOG_VERBOSE("No valid meter data to publish");
            return;
        }
        
        if (!_publishMeterJson()) {
            LOG_ERROR("Failed to publish meter data via JSON");
            return;
        }

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

        if (_publishMessage(_mqttTopicSystemStatic, staticMessage, true)) { // retain static info since it is idempotent
            _publishMqtt.systemStatic = false; 
        }

        LOG_DEBUG("System static info published to MQTT");
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

        LOG_DEBUG("System dynamic info published to MQTT");
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

        LOG_DEBUG("Channel data published to MQTT");
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

        LOG_DEBUG("Statistics published to MQTT");
    }

    static void _publishCrash() {
        if (!_publishCrashJson()) {
            LOG_ERROR("Failed to publish crash data via JSON");
            return;
        }

        _publishMqtt.crash = false;
    }

    static void _publishLog(const LogEntry& entry)
    {
        char logTopic[sizeof(_mqttTopicLog) + 8 + 2]; // 8 is the maximum size of the log level string
        snprintf(logTopic, sizeof(logTopic), "%s/%s", _mqttTopicLog, AdvancedLogger::logLevelToStringLower(entry.level));

        JsonDocument jsonDocument;
        char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
        AdvancedLogger::getTimestampIsoUtcFromUnixTimeMilliseconds(entry.unixTimeMilliseconds, timestamp, sizeof(timestamp));
        jsonDocument["timestamp"] = timestamp;
        jsonDocument["millis"] = entry.millis;
        jsonDocument["core"] = entry.coreId;
        jsonDocument["file"] = entry.file;
        jsonDocument["function"] = entry.function;
        jsonDocument["message"] = entry.message;

        char logMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, logMessage, sizeof(logMessage));

        _publishMessage(logTopic, logMessage);
    }

    static bool _publishProvisioningRequest() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        jsonDocument["firmwareVersion"] = FIRMWARE_BUILD_VERSION;
        jsonDocument["sketchMD5"] = ESP.getSketchMD5();
        jsonDocument["chipId"] = ESP.getEfuseMac();

        char provisioningRequestMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, provisioningRequestMessage, sizeof(provisioningRequestMessage));

        return _publishMessage(_mqttTopicProvisioningRequest, provisioningRequestMessage);
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.systemStatic) {_publishSystemStatic();}
        if (_publishMqtt.systemDynamic) {_publishSystemDynamic();}
        if (_publishMqtt.channel) {_publishChannel();}
        if (_publishMqtt.statistics) {_publishStatistics();}
        if (_publishMqtt.crash) {_publishCrash();}
    }

    static void _checkIfPublishMeterNeeded() {
        UBaseType_t queueSize = _meterQueue ? uxQueueMessagesWaiting(_meterQueue) : 0;
        if (
            (queueSize > 0) &&
            (
                ((queueSize > (MQTT_METER_QUEUE_SIZE * MQTT_METER_QUEUE_ALMOST_FULL_THRESHOLD)) && _sendPowerDataEnabled) || // Either we are sending power data and the queue is almost full
                ((millis64() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH) // Or enought time has passed and we need to publish anyway (voltage and energy)
            )
        ) {
            _publishMqtt.meter = true;
            LOG_DEBUG("Set flag to publish %u meter data points", queueSize);
        }
    }

    static void _checkIfPublishSystemDynamicNeeded() {
        if ((millis64() - _lastMillisSystemDynamicPublished) > MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH) {
            _publishMqtt.systemDynamic = true;
            LOG_DEBUG("Set flag to publish system dynamic");
        }
    }
    
    static void _checkIfPublishStatisticsNeeded() {
        if ((millis64() - _lastMillisStatisticsPublished) > MQTT_MAX_INTERVAL_STATISTICS_PUBLISH) {
            _publishMqtt.statistics = true;
            LOG_DEBUG("Set flag to publish statistics");
        }
    }

    // MQTT operations
    // ===============

    static bool _setupMqttWithDeviceCertificates() {
        if (!_setCertificatesFromPreferences()) {
            LOG_ERROR("Failed to set certificates");
            return false;
        }

        // Validate certificates before setting them
        if (!_validateCertificateFormat(_awsIotCoreCert, "device cert") ||
            !_validateCertificateFormat(_awsIotCorePrivateKey, "private key")) {
            LOG_ERROR("Certificate validation failed");
            return false;
        }

        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(_awsIotCoreCert);
        _net.setPrivateKey(_awsIotCorePrivateKey);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);
        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);
        _clientMqtt.setCallback(_subscribeCallback);

        LOG_DEBUG("MQTT certificates setup complete");
        return true;
    }

    static bool _connectMqtt()
    {
        LOG_DEBUG("Attempting to connect to MQTT (attempt %lu)...", _mqttConnectionAttempt + 1);

        if (_clientMqtt.connect(DEVICE_ID)) // Automatically uses the certificates set in _setupMqttWithDeviceCertificates
        {
            LOG_INFO("Connected to MQTT");

            _mqttConnectionAttempt = 0; // Reset attempt counter on success
            _nextMqttConnectionAttemptMillis = 0; // Reset next attempt time
            statistics.mqttConnections++;

            _subscribeToTopics();

            // Publish data on connection (except meter and crash)
            _publishMqtt.systemStatic = true;
            _publishMqtt.systemDynamic = true;
            _publishMqtt.statistics = true;
            _publishMqtt.channel = true;

            return true;
        } else {
            int32_t currentState = _clientMqtt.state();
            _mqttConnectionAttempt++;
            statistics.mqttConnectionErrors++;

            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                LOG_ERROR("MQTT connection failed due to authorization error (%d). Clearing certificates", currentState);
                _clearCertificates();

                // Hold the MQTT task for a bit before going again at the whole process
                uint64_t backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_INITIAL_RETRY_INTERVAL, MQTT_MAX_RETRY_INTERVAL, MQTT_RETRY_MULTIPLIER);
                delay((uint32_t)backoffDelay);
                
                _setState(MqttState::IDLE); // Error state so we restart whole MQTT provisioning process (maybe certs expired?)
            } else {
                if (currentState != 0) {
                    LOG_ERROR("MQTT connection failed with error: %s (%d)", _getMqttStateReason(currentState), currentState);
                } else {
                    LOG_ERROR("MQTT connection failed");
                }
            }

            uint64_t backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_INITIAL_RETRY_INTERVAL, MQTT_MAX_RETRY_INTERVAL, MQTT_RETRY_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + backoffDelay;
            LOG_WARNING("Failed to connect to MQTT (attempt %lu). Reason: %s. Next attempt in %llu ms", _mqttConnectionAttempt, _getMqttStateReason(currentState), backoffDelay);

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
        if (
            strlen(_awsIotCoreCert) == 0 || 
            strlen(_awsIotCorePrivateKey) == 0 ||
            !strstr(_awsIotCoreCert, "-----BEGIN CERTIFICATE-----") ||
            (
                !strstr(_awsIotCorePrivateKey, "-----BEGIN PRIVATE KEY-----") &&
                !strstr(_awsIotCorePrivateKey, "-----BEGIN RSA PRIVATE KEY-----")
            )
        ) {
            LOG_ERROR("Invalid device certificates");
            _clearCertificates();
            return false;
        }

        LOG_DEBUG("Certificates set and validated");
        return true;
    }

    static bool _claimProcess() {
        LOG_DEBUG("Claiming certificates...");

        _clientMqtt.setCallback(_subscribeCallback);
        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(aws_iot_core_cert_certclaim);
        _net.setPrivateKey(aws_iot_core_cert_privateclaim);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);
        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        LOG_DEBUG("MQTT setup for claiming certificates complete");

        // Connect with controlled attempts + backoff
        for (int32_t connectionAttempt = 0; connectionAttempt < MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT; ++connectionAttempt) {
            LOG_DEBUG("Attempting to connect to MQTT for claiming certificates (%d/%d)...", connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT);

            if (_clientMqtt.connect(DEVICE_ID)) {
                LOG_DEBUG("Connected to MQTT for claiming certificates");
                statistics.mqttConnections++;
                break;
            }

            LOG_WARNING("Failed to connect to MQTT for claiming certificates (attempt %d). Reason: %s", connectionAttempt + 1, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttConnectionErrors++;

            uint64_t backoffDelay = calculateExponentialBackoff(connectionAttempt, MQTT_CLAIM_INITIAL_RETRY_INTERVAL, MQTT_CLAIM_MAX_RETRY_INTERVAL, MQTT_CLAIM_RETRY_MULTIPLIER);
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoffDelay)) > 0) { _taskShouldRun = false; return false; }

            if (connectionAttempt == MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT - 1) {
                LOG_ERROR("Failed to connect to MQTT for claiming certificates after %d attempts", MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT);
                return false;
            }
        }

        // We are connected. Now subscribe to the provisioning response topic (only one needed now)
        _subscribeProvisioningResponse();

        // Publish provisioning request (limited retries)
        for (int32_t publishAttempt = 0; publishAttempt < MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT; ++publishAttempt) {
            LOG_DEBUG("Attempting to publish provisioning request (%d/%d)...", publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT);

            if (_publishProvisioningRequest()) {
                LOG_DEBUG("Provisioning request published");
                break;
            }
            
            LOG_WARNING("Failed to publish provisioning request (%d/%d)", publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT);
            statistics.mqttMessagesPublishedError++;

            uint64_t backoffDelay = calculateExponentialBackoff(publishAttempt, MQTT_CLAIM_INITIAL_RETRY_INTERVAL, MQTT_CLAIM_MAX_RETRY_INTERVAL, MQTT_CLAIM_RETRY_MULTIPLIER);
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoffDelay)) > 0) { _taskShouldRun = false; return false; }

            if (publishAttempt == MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT - 1) {
                return false;
            }
        }

        // We published, now we wait for response or presence of certs
        LOG_DEBUG("Waiting for provisioning response...");
        uint64_t deadline = millis64() + MQTT_CLAIM_TIMEOUT;

        while (millis64() < deadline) {
            _clientMqtt.loop();

            if (_isDeviceCertificatesPresent()) {
                LOG_DEBUG("Certificates provisioning confirmed");
                break;
            }

            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_CLAIMING_INTERVAL)) > 0) { _taskShouldRun = false; return false; }
        }

        // Always disconnect the claim session before switching state
        _clientMqtt.disconnect();

        if (_isDeviceCertificatesPresent()) { // Last check to ensure we have the certs
            return true;
        }

        LOG_WARNING("Provisioning response timeout. Will retry in %d ms.", MQTT_CLAIM_TIMEOUT);
        return false;
    }

    static bool _publishMessage(const char* topic, const char* message, bool retain) {
        if (topic == nullptr || message == nullptr) {
            LOG_WARNING("Null ptr, MQTT not initialized yet"); // TODO: will this cause a loop with log publish?
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.connected()) {
            LOG_WARNING("MQTT not connected (%s). Skipping publish on %s", _getMqttStateReason(_clientMqtt.state()), topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.publish(topic, message, retain)) {
            LOG_ERROR("Failed to publish on %s (state %s)", topic, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        statistics.mqttMessagesPublished++;
        LOG_VERBOSE("Message published: %s", message); // Cannot be debug otherwise when remote debugging on MQTT is enabled, we get a loop
        return true;
    }

    // Queue processing and streaming
    // ==============================

    static void _processLogQueue()
    {
        if (!_initializeLogQueue()) return;

        LogEntry entry;
        uint32_t loops = 0;
        while (xQueueReceive(_logQueue, &entry, 0) == pdTRUE && loops < MAX_LOOP_ITERATIONS) { // Time to wait should be 0 so we don't block the publisher
            if (CustomWifi::isFullyConnected() && _clientMqtt.connected()) {
                _publishLog(entry);
            } else {
                // If not connected, put it back in the queue if there's space
                xQueueSendToFront(_logQueue, &entry, 0);
                break; // Stop processing if not connected
            }
        }
    }

    static bool _publishMeterBatch(JsonDocument& jsonDocument, bool includeStaticData) {
        if (includeStaticData) {
            // Add voltage data if available
            MeterValues meterValuesZeroChannel;
            Ade7953::getMeterValues(meterValuesZeroChannel, 0);
            if (meterValuesZeroChannel.lastUnixTimeMilliseconds > 0) {
                JsonObject voltageObj = jsonDocument.add<JsonObject>();
                voltageObj["unixTime"] = meterValuesZeroChannel.lastUnixTimeMilliseconds;
                voltageObj["voltage"] = meterValuesZeroChannel.voltage;
            }

            // Add channel energy data
            for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
                if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                    MeterValues meterValues;
                    Ade7953::getMeterValues(meterValues, i);

                    if (meterValues.lastUnixTimeMilliseconds == 0) {
                        LOG_DEBUG("Meter values for channel %d have zero unixTime, skipping...", i);
                        continue;
                    }

                    JsonObject channelObj = jsonDocument.add<JsonObject>();
                    channelObj["unixTime"] = meterValues.lastUnixTimeMilliseconds;
                    channelObj["channel"] = i;
                    channelObj["activeEnergyImported"] = meterValues.activeEnergyImported;
                    channelObj["activeEnergyExported"] = meterValues.activeEnergyExported;
                    channelObj["reactiveEnergyImported"] = meterValues.reactiveEnergyImported;
                    channelObj["reactiveEnergyExported"] = meterValues.reactiveEnergyExported;
                    channelObj["apparentEnergy"] = meterValues.apparentEnergy;
                }
            }
        }
        
        // Add queue data if power data is enabled
        uint32_t entriesAdded = 0;
        if (_sendPowerDataEnabled) {
            PayloadMeter payloadMeter;
            uint32_t loops = 0;
            while ((uxQueueMessagesWaiting(_meterQueue) > 0) && loops < MAX_LOOP_ITERATIONS) {
                loops++;

                if (xQueueReceive(_meterQueue, &payloadMeter, 0) != pdTRUE) break;

                JsonArray powerArray = jsonDocument.add<JsonArray>();
                powerArray.add(payloadMeter.unixTimeMs);
                powerArray.add(payloadMeter.channel);
                powerArray.add(payloadMeter.activePower);
                powerArray.add(payloadMeter.powerFactor);
                entriesAdded++;

                if (measureJson(jsonDocument) > JSON_MQTT_BUFFER_SIZE * 0.95) {
                    LOG_DEBUG("Meter data JSON size exceeds 95%% of buffer, will publish batch and continue");
                    break; // Stop adding new entries if the buffer is nearly full
                }
            }
        }

        char meterMessage[JSON_MQTT_BUFFER_SIZE];
        LOG_DEBUG("Serializing meter JSON with %u entries | Size: %u bytes | Queue entries added: %u | Remaining in queue: %u", 
                  jsonDocument.size(), measureJson(jsonDocument), entriesAdded, uxQueueMessagesWaiting(_meterQueue));
        
        if (!safeSerializeJson(jsonDocument, meterMessage, sizeof(meterMessage))) {
            LOG_ERROR("Failed to serialize meter JSON");
            return false;
        }

        if (_publishMessage(_mqttTopicMeter, meterMessage)) {
            statistics.mqttMessagesPublished++;
            LOG_DEBUG("Meter data batch published to %s", _mqttTopicMeter);
            return true;
        } else {
            LOG_ERROR("Failed to publish meter data batch");
            statistics.mqttMessagesPublishedError++;
            return false;
        }
    }

    static bool _publishMeterJson() {
        if (!_initializeMeterQueue()) return false;

        bool overallSuccess = true;
        bool isFirstBatch = true;
        uint32_t batchCount = 0;
        
        // Continue publishing batches until queue is empty or we hit max batches
        while (uxQueueMessagesWaiting(_meterQueue) > 0 || isFirstBatch) {
            JsonDocument jsonDocument;
            
            // Publish the batch (include static data only in first batch)
            if (!_publishMeterBatch(jsonDocument, isFirstBatch)) {
                overallSuccess = false;
                break;
            }
            
            batchCount++;
            isFirstBatch = false;
            
            // Safety check to prevent infinite loops
            if (batchCount >= MQTT_METER_MAX_BATCHES) {
                LOG_WARNING("Maximum meter batch count reached, stopping to prevent infinite loop");
                break;
            }
            
            // If no queue items were processed in this batch, we're done
            if (!_sendPowerDataEnabled || uxQueueMessagesWaiting(_meterQueue) == 0) {
                break;
            }
        }
        
        if (overallSuccess) {
            _publishMqtt.meter = false;
            LOG_DEBUG("All meter data published successfully in %u batch(es)", batchCount);
        }
        
        return overallSuccess;
    }

    static bool _publishCrashJson() {
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        JsonDocument crashInfoJson;
        CrashMonitor::getCoreDumpInfoJson(crashInfoJson);
        jsonDocument["crashInfo"] = crashInfoJson;

        // Add dump data if available
        // size_t dumpDataSize = CrashMonitor::getCoreDumpSize();
        // if (dumpDataSize > 0) {
        //     // Read all dump data
        //     uint8_t* dumpBuffer = (uint8_t*)malloc(dumpDataSize);
        //     if (dumpBuffer != nullptr) {
        //         size_t bytesRead = 0;
        //         if (CrashMonitor::getCoreDumpChunk(dumpBuffer, 0, dumpDataSize, &bytesRead)) {
        //             // Encode to base64
        //             size_t base64Len = 0;
        //             char* base64Buffer = (char*)malloc(dumpDataSize * 4 / 3 + 4);
        //             if (base64Buffer != nullptr) {
        //                 if (mbedtls_base64_encode((unsigned char*)base64Buffer, dumpDataSize * 4 / 3 + 4, 
        //                                           &base64Len, dumpBuffer, bytesRead) == 0) {
        //                     base64Buffer[base64Len] = '\0';
        //                     jsonDocument["crashInfo"]["dumpData"] = base64Buffer;
        //                 }
        //                 free(base64Buffer);
        //             }
        //         }
        //         free(dumpBuffer);
        //     }
        // }

        char crashMessage[JSON_MQTT_BUFFER_SIZE];
        if (!safeSerializeJson(jsonDocument, crashMessage, sizeof(crashMessage))) {
            LOG_ERROR("Failed to serialize crash JSON");
            return false;
        }

        if (_publishMessage(_mqttTopicCrash, crashMessage)) {
            _publishMqtt.crash = false;
            // CrashMonitor::clearCoreDump(); /// FIXME: eventually delete
            statistics.mqttMessagesPublished++;
            LOG_DEBUG("Crash data published to %s", _mqttTopicCrash);
            return true;
        } else {
            LOG_ERROR("Failed to publish crash data");
            statistics.mqttMessagesPublishedError++;
            return false;
        }
    }


    // Certificates management
    // =======================

    static void _readEncryptedPreferences(const char* preferenceKey, const char* presharedEncryptionKey, char* decryptedData, size_t decryptedDataSize) {
        if (preferenceKey == nullptr || presharedEncryptionKey == nullptr || decryptedData == nullptr || decryptedDataSize == 0) {
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            LOG_ERROR("Failed to open preferences");
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        // Avoid NOT_FOUND spam by checking key existence first
        if (!preferences.isKey(preferenceKey)) {
            preferences.end();
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        char encryptedData[CERTIFICATE_BUFFER_SIZE];
        memset(encryptedData, 0, sizeof(encryptedData)); // Clear before setting
        preferences.getString(preferenceKey, encryptedData, sizeof(encryptedData));
        preferences.end();

        if (strlen(encryptedData) == 0) {
            LOG_DEBUG("No encrypted data found for key %s", preferenceKey);
            return;
        }

        // Use GCM decrypt with SHA256(preshared||DEVICE_ID)
        _decryptData(encryptedData, presharedEncryptionKey, decryptedData, decryptedDataSize);
    }

    static bool _isDeviceCertificatesPresent() {
        Preferences preferences;

        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, true)) {
            return false;
        }

        // Avoid NOT_FOUND spam by checking key presence only
        bool deviceCertExists = preferences.isKey(PREFS_KEY_CERTIFICATE);
        bool privateKeyExists = preferences.isKey(PREFS_KEY_PRIVATE_KEY);
        preferences.end();
        
        return deviceCertExists && privateKeyExists;
    }

    static void _clearCertificates() {
        Preferences preferences;
        
        preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false);
        preferences.clear();
        preferences.end();
        
        LOG_INFO("Certificates for cloud services cleared");
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
        if (encryptedDataBase64 == nullptr || presharedKey == nullptr || decryptedData == nullptr || decryptedDataSize == 0) {
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            LOG_WARNING("Invalid parameters for decryption");
            return;
        }

        size_t inputLength = strlen(encryptedDataBase64);
        if (inputLength == 0) {
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }

        // Base64 decode
        uint8_t decoded[CERTIFICATE_BUFFER_SIZE];
        size_t decodedLen = 0;
        int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &decodedLen,
                                        reinterpret_cast<const unsigned char*>(encryptedDataBase64), inputLength);
        if (ret != 0 || decodedLen < (12 + 16)) { // must at least contain IV + TAG
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            LOG_ERROR("Failed to decode base64 or data too short (%u)", (unsigned)decodedLen);
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
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            mbedtls_gcm_free(&gcm);
            memset(key, 0, sizeof(key));
            LOG_ERROR("Failed to set AES-GCM key");
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
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            LOG_ERROR("AES-GCM auth decrypt failed (%d)", dec);
            return;
        }

        // Ensure null termination (certificate is ASCII PEM)
        if (ctLen >= decryptedDataSize) ctLen = decryptedDataSize - 1;
        decryptedData[ctLen] = '\0';

        // Clear sensitive buffers
        memset(decoded, 0, sizeof(decoded));

        LOG_DEBUG("Decrypted data successfully");
    }

    static bool _validateCertificateFormat(const char* cert, const char* certType) {
        if (!cert || strlen(cert) == 0) {
            LOG_ERROR("Certificate %s is empty or null", certType);
            return false;
        }

        // Check for valid PEM format (either standard or RSA private key)
        bool hasValidHeader = strstr(cert, "-----BEGIN CERTIFICATE-----") != nullptr ||
                              strstr(cert, "-----BEGIN PRIVATE KEY-----") != nullptr ||
                              strstr(cert, "-----BEGIN RSA PRIVATE KEY-----") != nullptr;

        if (!hasValidHeader) {
            LOG_ERROR("Certificate %s does not have valid PEM header", certType);
            return false;
        }

        return true;
    }

    // State machine management
    // ========================

    static void _setState(MqttState newState) {
        if (_currentState != newState) {
            LOG_DEBUG("MQTT state transition: %s -> %s", _getMqttStateMachineName(_currentState), _getMqttStateMachineName(newState));
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
        if (_claimProcess()) _setState(MqttState::SETTING_UP_CERTIFICATES);
        else _setState(MqttState::IDLE); // Reset to idle if claiming failed
    }

    static void _handleSettingUpCertificatesState() {
        if (_setupMqttWithDeviceCertificates()) {
            _setState(MqttState::CONNECTING);
        } else {
            _setState(MqttState::IDLE);
        }
    }

    static void _handleConnectingState() {
        if (_clientMqtt.connected()) {
            _setState(MqttState::CONNECTED);
            return;
        }
        if (millis64() >= _nextMqttConnectionAttemptMillis) {
            if (_connectMqtt() && _clientMqtt.connected()) { // Both connect and check immediately after
                _setState(MqttState::CONNECTED);
            }
        }
    }

    static void _handleConnectedState() {
        if (!_clientMqtt.connected() || !_clientMqtt.loop()) { // Also process incoming messages with loop()
            LOG_DEBUG("MQTT disconnected, transitioning to connecting state");
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

    static const char* _getMqttStateMachineName(MqttState state)
    {
        switch (state)
        {
            case MqttState::IDLE: return "IDLE";
            case MqttState::CLAIMING_CERTIFICATES: return "CLAIMING_CERTIFICATES";
            case MqttState::SETTING_UP_CERTIFICATES: return "SETTING_UP_CERTIFICATES";
            case MqttState::CONNECTING: return "CONNECTING";
            case MqttState::CONNECTED: return "CONNECTED";
            default: return "Unknown";
        }
    }
}
#endif