#ifdef HAS_SECRETS
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
    static uint8_t _mqttLogLevelInt = DEFAULT_MQTT_LOG_LEVEL_INT;
    static LogLevel _mqttMinLogLevel = LogLevel::INFO;

    // Certificates storage (decrypted) - allocated in PSRAM
    static char *_awsIotCoreCert = nullptr;
    static char *_awsIotCorePrivateKey = nullptr;
    
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
    static bool _lastLoopToPublishData = false;
    static bool _taskShouldRun = false;

    // AWS IoT Jobs OTA task (global to ensure they work and do not get dereferenced)
    static char *_otaCurrentUrl = nullptr;  // URL_BUFFER_SIZE - allocated in PSRAM
    static char _otaCurrentJobId[NAME_BUFFER_SIZE];
    static TaskHandle_t _otaTaskHandle = nullptr;

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
    static void _setMqttLogLevel(const char* logLevel);
    static void _updateMqttMinLogLevel();

    static void _saveCloudServicesEnabledToPreferences(bool enabled);
    static void _saveSendPowerDataEnabledToPreferences(bool enabled);
    static void _saveMqttLogLevelToPreferences(uint8_t logLevel);
        
    // Task management
    static void _startTask();
    static void _stopTask();
    static void _mqttTask(void *parameter);

    // Topic management
    static void _constructMqttTopicReservedThings(const char* finalTopic, char* topicBuffer, size_t topicBufferSize);
    static void _constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topicBuffer, size_t topicBufferSize);
    static void _constructMqttTopic(const char* finalTopic, char* topicBuffer, size_t topicBufferSize);
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
    static void _subscribeCommand();
    static void _subscribeProvisioningResponse();
    static void _subscribeAwsIotJobs();
    
    // Subscription callback handler
    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length);
    static void _handleCommandMessage(const char* message);
    static void _handleAwsIotJobMessage(const char* message, const char* topic);
    static void _handleRestartMessage();
    static void _handleProvisioningResponseMessage(const char* message);
    static void _handleEraseCertificatesMessage();
    static void _handleSetSendPowerDataMessage(JsonDocument &dataDoc);
    static void _handleSetMqttLogLevelMessage(JsonDocument &dataDoc);
    
    // AWS IoT Jobs OTA functions
    static bool _validateAwsIotJobMessage(const char* message, const char* topic);
    static void _handleJobListResponse(JsonDocument &doc);
    static void _handleSingleJobExecution(JsonDocument &doc);
    static void _publishOtaJobDetail(const char* jobId);
    static void _otaTask(void* parameter);
    static esp_err_t _otaHttpEventHandler(esp_http_client_event_t *event);
    static bool _performOtaUpdate();

    // Publishing functions
    static void _publishMeter();
    static void _publishSystemStatic();
    static void _publishSystemDynamic();
    static void _publishChannel();
    static void _publishStatistics();
    static void _publishCrash();
    static void _publishLog(const LogEntry& entry);
    static bool _publishProvisioningRequest();
    static void _publishOtaJobsRequest();
    
    static void _checkPublishMqtt();
    static void _checkIfPublishMeterNeeded();
    static void _checkIfPublishSystemDynamicNeeded();
    static void _checkIfPublishStatisticsNeeded();

    // MQTT operations
    static bool _setCertificatesFromPreferences();
    static bool _setupMqttWithDeviceCertificates();
    static bool _connectMqtt();
    static bool _claimProcess();
    static bool _publishJsonStreaming(JsonDocument &jsonDocument, const char* topic, bool retain = false);

    // Queue processing and streaming
    static void _processLogQueue();
    static bool _publishMeterStreaming();
    static bool _publishMeterJson();
    static bool _publishCrashJson();
    static bool _publishOtaJobsRequestJson();
    
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
    bool extractHost(const char* url, char* buffer, size_t bufferSize);

    // Public API functions
    // ====================
    // ====================

    void begin()
    {
        LOG_DEBUG("Setting up MQTT client...");

        if (!createMutexIfNeeded(&_configMutex)) {
            LOG_ERROR("Failed to create configuration mutex");
            return;
        }

        // Static and permanent config
        _clientMqtt.setBufferSize(MQTT_BUFFER_SIZE);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);
        _clientMqtt.setServer(AWS_IOT_CORE_ENDPOINT, AWS_IOT_CORE_PORT);
        _clientMqtt.setCallback(_subscribeCallback);

        _setupTopics();
        _loadConfigFromPreferences();
        _initializeLogQueue();
        _initializeMeterQueue();

        // Initialize OTA buffers
        if (_otaCurrentUrl == nullptr) {
            _otaCurrentUrl = (char*)ps_malloc(OTA_PRESIGNED_URL_BUFFER_SIZE);
            if (_otaCurrentUrl == nullptr) {
                LOG_ERROR("Failed to allocate OTA URL buffer in PSRAM");
            } else {
                memset(_otaCurrentUrl, 0, OTA_PRESIGNED_URL_BUFFER_SIZE);
            }
        }
        
        memset(_otaCurrentJobId, 0, sizeof(_otaCurrentJobId));

        // Allocate certificate buffers in PSRAM
        if (_awsIotCoreCert == nullptr) {
            _awsIotCoreCert = (char*)ps_malloc(CERTIFICATE_BUFFER_SIZE);
            if (_awsIotCoreCert == nullptr) {
                LOG_ERROR("Failed to allocate certificate buffer in PSRAM");
            } else {
                memset(_awsIotCoreCert, 0, CERTIFICATE_BUFFER_SIZE);
            }
        }
        
        if (_awsIotCorePrivateKey == nullptr) {
            _awsIotCorePrivateKey = (char*)ps_malloc(CERTIFICATE_BUFFER_SIZE);
            if (_awsIotCorePrivateKey == nullptr) {
                LOG_ERROR("Failed to allocate private key buffer in PSRAM");
            } else {
                memset(_awsIotCorePrivateKey, 0, CERTIFICATE_BUFFER_SIZE);
            }
        }

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
            free(_logQueueStorage);
            _logQueueStorage = nullptr;
            LOG_DEBUG("MQTT log queue PSRAM freed");
        }
        
        if (_meterQueueStorage != nullptr) {
            free(_meterQueueStorage);
            _meterQueueStorage = nullptr;
            LOG_DEBUG("MQTT meter queue PSRAM freed");
        }

        deleteMutex(&_configMutex);

        // Zeroize and free certificate buffers
        if (_awsIotCoreCert != nullptr) {
            memset(_awsIotCoreCert, 0, CERTIFICATE_BUFFER_SIZE);
            free(_awsIotCoreCert);
            _awsIotCoreCert = nullptr;
        }
        
        if (_awsIotCorePrivateKey != nullptr) {
            memset(_awsIotCorePrivateKey, 0, CERTIFICATE_BUFFER_SIZE);
            free(_awsIotCorePrivateKey);
            _awsIotCorePrivateKey = nullptr;
        }
        
        // Free OTA URL buffer
        if (_otaCurrentUrl != nullptr) {
            memset(_otaCurrentUrl, 0, OTA_PRESIGNED_URL_BUFFER_SIZE);
            free(_otaCurrentUrl);
            _otaCurrentUrl = nullptr;
        }
        
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

    // Public methods for requesting MQTT publications
    // ===============================================

    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestCrashPublish() {_publishMqtt.crash = true; }

    // Public methods for pushing data to queues
    // =========================================

    void pushLog(const LogEntry& entry)
    {
        if (!_initializeLogQueue()) return;

        // Fast log level filtering using precomputed minimum level
        if (entry.level < _mqttMinLogLevel) {
            return;
        }
        
        // Filter out logs from MQTT publishing functions to prevent infinite loops
        if (strstr(entry.function, "_publishLog") != nullptr ||
            strstr(entry.function, "_publishJsonStreaming") != nullptr
        ) {
            return; // Skip logs from MQTT publishing functions
        }
        
        xQueueSend(_logQueue, &entry, pdMS_TO_TICKS(QUEUE_WAIT_TIMEOUT));
    }

    void pushMeter(const PayloadMeter& payload)
    {
        if (!_initializeMeterQueue()) return;
        if (_sendPowerDataEnabled) xQueueSend(_meterQueue, &payload, pdMS_TO_TICKS(QUEUE_WAIT_TIMEOUT)); // Only add to queue if we chose to send power data
    }

    TaskInfo getMqttTaskInfo()
    {
        return getTaskInfoSafely(_taskHandle, MQTT_TASK_STACK_SIZE);
    }

    TaskInfo getMqttOtaTaskInfo()
    {
        return getTaskInfoSafely(_otaTaskHandle, OTA_TASK_STACK_SIZE);
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
        _logQueueStorage = (uint8_t*)ps_malloc(realQueueSize);

        if (_logQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT log queue (%d bytes)\n", realQueueSize);
            return false;
        }

        _logQueue = xQueueCreateStatic(queueLength, sizeof(LogEntry), _logQueueStorage, &_logQueueStruct);
        if (_logQueue == nullptr) {
            Serial.println("[ERROR] Failed to create MQTT log queue");
            free(_logQueueStorage);
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
        _meterQueueStorage = (uint8_t*)ps_malloc(realQueueSize);

        if (_meterQueueStorage == nullptr) {
            LOG_ERROR("Failed to allocate PSRAM for MQTT meter queue (%zu bytes)\n", realQueueSize);
            return false;
        }

        _meterQueue = xQueueCreateStatic(queueLength, sizeof(PayloadMeter), _meterQueueStorage, &_meterQueueStruct);
        if (_meterQueue == nullptr) {
            LOG_ERROR("Failed to create MQTT meter queue\n");
            free(_meterQueueStorage);
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
        _mqttLogLevelInt = prefs.getUChar(MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY, DEFAULT_MQTT_LOG_LEVEL_INT);
        _updateMqttMinLogLevel(); // Convert integer to LogLevel enum

        LOG_DEBUG("Cloud services enabled: %s, Send power data enabled: %s, MQTT log level: %u",
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false",
                   _mqttLogLevelInt);

        prefs.end();
        releaseMutex(&_configMutex);

        _saveConfigToPreferences();
        LOG_DEBUG("MQTT preferences loaded");
    }

    static void _saveConfigToPreferences()
    {
        _saveCloudServicesEnabledToPreferences(_cloudServicesEnabled);
        _saveSendPowerDataEnabledToPreferences(_sendPowerDataEnabled);
        _saveMqttLogLevelToPreferences(_mqttLogLevelInt);
        
        LOG_DEBUG("MQTT preferences saved");
    }

    static void _setSendPowerDataEnabled(bool enabled)
    {
        _sendPowerDataEnabled = enabled;
        _saveSendPowerDataEnabledToPreferences(enabled);
        LOG_DEBUG("Set send power data enabled to %s", enabled ? "true" : "false");
    }

    static void _updateMqttMinLogLevel()
    {
        // Convert integer to LogLevel enum (0=VERBOSE, 1=DEBUG, 2=INFO, 3=WARNING, 4=ERROR, 5=FATAL)
        switch (_mqttLogLevelInt) {
            case 0: _mqttMinLogLevel = LogLevel::VERBOSE; break;
            case 1: _mqttMinLogLevel = LogLevel::DEBUG; break;
            case 2: _mqttMinLogLevel = LogLevel::INFO; break;
            case 3: _mqttMinLogLevel = LogLevel::WARNING; break;
            case 4: _mqttMinLogLevel = LogLevel::ERROR; break;
            case 5: _mqttMinLogLevel = LogLevel::FATAL; break;
            default: _mqttMinLogLevel = LogLevel::INFO; break; // Default fallback
        }
        LOG_DEBUG("Updated MQTT minimum log level to %u", _mqttLogLevelInt);
    }

    static void _setMqttLogLevel(const char* logLevel)
    {
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire configuration mutex for setting MQTT log level");
            return;
        }

        if (logLevel == nullptr) {
            LOG_ERROR("Invalid MQTT log level provided");
            releaseMutex(&_configMutex);
            return;
        }

        // Convert string to integer
        uint8_t levelInt = DEFAULT_MQTT_LOG_LEVEL_INT; // Default fallback
        if (strcmp(logLevel, "VERBOSE") == 0) levelInt = 0;
        else if (strcmp(logLevel, "DEBUG") == 0) levelInt = 1;
        else if (strcmp(logLevel, "INFO") == 0) levelInt = 2;
        else if (strcmp(logLevel, "WARNING") == 0) levelInt = 3;
        else if (strcmp(logLevel, "ERROR") == 0) levelInt = 4;
        else if (strcmp(logLevel, "FATAL") == 0) levelInt = 5;
        else {
            LOG_ERROR("Invalid log level: %s", logLevel);
            releaseMutex(&_configMutex);
            return;
        }

        _mqttLogLevelInt = levelInt;
        _updateMqttMinLogLevel();
        _saveMqttLogLevelToPreferences(_mqttLogLevelInt);
        releaseMutex(&_configMutex);

        LOG_DEBUG("MQTT log level set to %s (%d)", logLevel, _mqttLogLevelInt);
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

    static void _saveMqttLogLevelToPreferences(uint8_t logLevel) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putUChar(MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY, logLevel);
        if (bytesWritten == 0) {
            LOG_ERROR("Failed to save MQTT log level preference: %d", logLevel);
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

        _nextMqttConnectionAttemptMillis = 0;
        _mqttConnectionAttempt = 0;
        
        LOG_DEBUG("Starting MQTT task with %d bytes stack in internal RAM (uses NVS)", MQTT_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _mqttTask,
            MQTT_TASK_NAME,
            MQTT_TASK_STACK_SIZE,
            nullptr,
            MQTT_TASK_PRIORITY,
            &_taskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create MQTT task");
            _taskHandle = nullptr;
        }
    }

    static void _stopTask() { 
        stopTaskGracefully(&_taskHandle, "MQTT task"); 
    }

    static void _mqttTask(void *parameter)
    {
        LOG_DEBUG("MQTT task started");
        
        _taskShouldRun = true;
        _lastLoopToPublishData = false;

        while (_taskShouldRun)
        {
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
            
            // If we receive a signal to stop the task, we try to publish all the data and flushing the queues so we avoid losing data
            if (_lastLoopToPublishData) {
                _lastLoopToPublishData = false;
                _taskShouldRun = false;
            } else if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_LOOP_INTERVAL)) > 0) { 
                _lastLoopToPublishData = true; 
                _publishMqtt.meter = true;
                _publishMqtt.systemDynamic = true;
                _publishMqtt.statistics = true;
                break; 
            }
        }
        
        _clientMqtt.disconnect();
        _setState(MqttState::IDLE);
        LOG_DEBUG("MQTT task stopping");
        _taskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    // Topic management
    // ================
    static void _constructMqttTopicReservedThings(const char* finalTopic, char* topicBuffer, size_t topicBufferSize) {
        // Example: $aws/things/588c81c47a5c/jobs/notify-next
        snprintf(
            topicBuffer,
            topicBufferSize,
            "%s/%s/%s",
            MQTT_THINGS,
            DEVICE_ID,
            finalTopic
        );

        LOG_DEBUG("Constructing MQTT reserved things topic for %s | %s", finalTopic, topicBuffer);
    }

    static void _constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topicBuffer, size_t topicBufferSize) {
        snprintf(
            topicBuffer,
            topicBufferSize,
            "%s/%s/%s/%s/%s/%s",
            MQTT_BASIC_INGEST,
            ruleName,
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            DEVICE_ID,
            finalTopic
        );

        LOG_DEBUG("Constructing MQTT topic with rule for %s | %s", finalTopic, topicBuffer);
    }

    static void _constructMqttTopic(const char* finalTopic, char* topicBuffer, size_t topicBufferSize) {
        snprintf(
            topicBuffer,
            topicBufferSize,
            "%s/%s/%s/%s",
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            DEVICE_ID,
            finalTopic
        );

        LOG_DEBUG("Constructing MQTT topic for %s | %s", finalTopic, topicBuffer);
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

    static void _setTopicMeter() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_METER, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicSystemStatic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_STATIC, _mqttTopicSystemStatic, sizeof(_mqttTopicSystemStatic)); }
    static void _setTopicSystemDynamic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_DYNAMIC, _mqttTopicSystemDynamic, sizeof(_mqttTopicSystemDynamic)); }
    static void _setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel, sizeof(_mqttTopicChannel)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }
    static void _setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash, sizeof(_mqttTopicCrash)); }
    static void _setTopicLog() { _constructMqttTopic(MQTT_TOPIC_LOG, _mqttTopicLog, sizeof(_mqttTopicLog)); }
    static void _setTopicProvisioningRequest() { _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, _mqttTopicProvisioningRequest, sizeof(_mqttTopicProvisioningRequest)); }

    static void _subscribeToTopics() {
        _subscribeCommand();
        _subscribeProvisioningResponse();
        _subscribeAwsIotJobs();

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

    static void _subscribeCommand() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_COMMAND); }
    static void _subscribeProvisioningResponse() { _subscribeToTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE); }
    
    static void _subscribeAwsIotJobs() {
        char jobNotifyTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings("jobs/notify-next", jobNotifyTopic, sizeof(jobNotifyTopic));
        
        char jobsAcceptedTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings("jobs/get/accepted", jobsAcceptedTopic, sizeof(jobsAcceptedTopic));

        char jobAcceptedTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings("jobs/+/get/accepted", jobAcceptedTopic, sizeof(jobAcceptedTopic));

        LOG_DEBUG("Attempting to subscribe to: %s", jobNotifyTopic);
        if (_clientMqtt.subscribe(jobNotifyTopic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_DEBUG("Subscribed to AWS IoT Jobs notify topic: %s", jobNotifyTopic);
        } else {
            LOG_WARNING("Failed to subscribe to AWS IoT Jobs notify topic: %s", jobNotifyTopic);
        }
        
        LOG_DEBUG("Attempting to subscribe to: %s", jobsAcceptedTopic);
        if (_clientMqtt.subscribe(jobsAcceptedTopic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_DEBUG("Subscribed to AWS IoT Jobs accepted topic: %s", jobsAcceptedTopic);
        } else {
            LOG_WARNING("Failed to subscribe to AWS IoT Jobs accepted topic: %s", jobsAcceptedTopic);
        }

        LOG_DEBUG("Attempting to subscribe to: %s", jobAcceptedTopic);
        if (_clientMqtt.subscribe(jobAcceptedTopic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_DEBUG("Subscribed to AWS IoT Job accepted topic: %s", jobAcceptedTopic);
        } else {
            LOG_WARNING("Failed to subscribe to AWS IoT Job accepted topic: %s", jobAcceptedTopic);
        }
    }

    // Subscription callback handler
    // =============================

    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length)
    {
        // Allocate message buffer in PSRAM to save stack memory
        char *message = (char*)ps_malloc(MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE);
        if (!message) {
            LOG_ERROR("Failed to allocate subscribe message buffer in PSRAM");
            return;
        }
        
        // Ensure we don't exceed buffer bounds
        uint32_t maxLength = MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE - 1; // Reserve space for null terminator
        if (length > maxLength) {
            LOG_WARNING("MQTT message from topic %s too large (%u bytes), truncating to %u", topic, length, maxLength);
            length = maxLength;
        }
        
        snprintf(message, MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE, "%.*s", (int)length, (char*)payload);

        LOG_DEBUG("Received MQTT message from %s", topic);

        if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_COMMAND)) _handleCommandMessage(message);
        else if (endsWith(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE)) _handleProvisioningResponseMessage(message);
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_JOBS)) _handleAwsIotJobMessage(message, topic);
        else LOG_WARNING("Unknown MQTT topic received: %s", topic);
        
        // Clean up PSRAM allocation
        free(message);
    }

    static void _handleCommandMessage(const char* message)
    {
        // Expected JSON format: {"command": "command_name", "data": {...}}
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            LOG_ERROR("Failed to parse command JSON message (%s): %s", error.c_str(), message);
            return;
        }

        if (!doc["command"].is<const char*>()) {
            LOG_ERROR("Invalid command message: missing or invalid 'command' field");
            return;
        }

        const char* command = doc["command"].as<const char*>();
        SpiRamAllocator allocatorData;
        JsonDocument docCommandMessage(&allocatorData);
        docCommandMessage = doc["data"].as<JsonObject>();

        LOG_DEBUG("Processing MQTT command: %s", command);

        if (strcmp(command, "restart") == 0) {
            _handleRestartMessage();
        } else if (strcmp(command, "erase_certificates") == 0) {
            _handleEraseCertificatesMessage();
        } else if (strcmp(command, "set_send_power_data") == 0) {
            _handleSetSendPowerDataMessage(docCommandMessage);
        } else if (strcmp(command, "set_mqtt_log_level") == 0) {
            _handleSetMqttLogLevelMessage(docCommandMessage);
        } else {
            LOG_WARNING("Unknown command received: %s", command);
        }
    }

    static void _handleRestartMessage()
    {
        setRestartSystem("Restart requested from MQTT");
    }

    static void _handleProvisioningResponseMessage(const char* message)
    {
        // Expected JSON format: {"status": "success", "encryptedCertificatePem": "...", "encryptedPrivateKey": "..."}
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            LOG_ERROR("Failed to parse provisioning response JSON message (%s): %s", error.c_str(), message);
            return;
        }

        if (doc["status"] == "success")
        {
            // Validate that certificate fields exist and are strings
            if (!doc["encryptedCertificatePem"].is<const char*>() || 
                !doc["encryptedPrivateKey"].is<const char*>()) 
            {
                LOG_ERROR("Invalid provisioning response: missing or invalid certificate fields");
                return;
            }

            const char* certData = doc["encryptedCertificatePem"].as<const char*>();
            const char* keyData = doc["encryptedPrivateKey"].as<const char*>();
            
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
            safeSerializeJson(doc, buffer, sizeof(buffer), true);
            LOG_ERROR("Provisioning failed: %s", buffer);
        }
    }

    static void _handleEraseCertificatesMessage()
    {
        _clearCertificates();
        _setState(MqttState::IDLE);
    }

    static void _handleSetSendPowerDataMessage(JsonDocument &dataDoc)
    {
        // Expected data format: {"sendPowerData": true}
        if (dataDoc["sendPowerData"].is<bool>()) {
            bool sendPowerData = dataDoc["sendPowerData"].as<bool>();
            _setSendPowerDataEnabled(sendPowerData);
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(dataDoc, buffer, sizeof(buffer), true);
            LOG_ERROR("Invalid send power data JSON message: %s", buffer);
        }
    }

    static void _handleSetMqttLogLevelMessage(JsonDocument &dataDoc)
    {
        // Expected data format: {"level": "INFO"}
        if (dataDoc["level"].is<const char*>()) {
            const char* level = dataDoc["level"].as<const char*>();
            
            // Validate log level
            if (strcmp(level, "VERBOSE") == 0 || strcmp(level, "DEBUG") == 0 || 
                strcmp(level, "INFO") == 0 || strcmp(level, "WARNING") == 0 || 
                strcmp(level, "ERROR") == 0 || strcmp(level, "FATAL") == 0) {
                _setMqttLogLevel(level);
                LOG_INFO("MQTT log level set to %s", level);
            } else {
                LOG_ERROR("Invalid log level: %s. Valid levels: VERBOSE, DEBUG, INFO, WARNING, ERROR, FATAL", level);
            }
        } else {
            char buffer[STATUS_BUFFER_SIZE];
            safeSerializeJson(dataDoc, buffer, sizeof(buffer), true);
            LOG_ERROR("Invalid set MQTT log level JSON message: %s", buffer);
        }
    }

    // AWS IoT Jobs OTA functions
    // ==========================

    // Custom HTTPS OTA implementation
    // =================================

    static esp_err_t _otaHttpEventHandler(esp_http_client_event_t *event) {
        static size_t lastProgressBytes = 0;
        static size_t totalBytesReceived = 0;
        static size_t contentLength = 0;
        
        switch (event->event_id) {
            case HTTP_EVENT_ERROR:        LOG_DEBUG("OTA HTTPS Event Error"); break;
            case HTTP_EVENT_ON_CONNECTED: LOG_DEBUG("OTA HTTPS Event On Connected"); break;
            case HTTP_EVENT_HEADER_SENT:  LOG_DEBUG("OTA HTTPS Event Header Sent"); break;
            case HTTP_EVENT_ON_HEADER:    
                LOG_DEBUG("OTA HTTPS Event On Header, key=%s, value=%s", event->header_key, event->header_value);
                // Capture content length from headers
                if (strcmp(event->header_key, "Content-Length") == 0) {
                    contentLength = atoi(event->header_value);
                    LOG_DEBUG("OTA Content-Length: %zu bytes", contentLength);
                    totalBytesReceived = 0; // Reset on new download
                    lastProgressBytes = 0;
                }
                break;
            case HTTP_EVENT_ON_DATA:      
                // Track progress and log periodically
                totalBytesReceived += event->data_len;
                if (totalBytesReceived >= lastProgressBytes + MQTT_OTA_SIZE_REPORT_UPDATE || totalBytesReceived == event->data_len) {
                    float progress = contentLength > 0 ? (float)totalBytesReceived / (float)contentLength * 100.0f : 0.0f;
                    LOG_DEBUG("OTA MQTT progress: %.1f%% (%zu / %zu bytes)", progress, totalBytesReceived, contentLength);
                    lastProgressBytes = totalBytesReceived;
                }
                break;
            case HTTP_EVENT_ON_FINISH:    LOG_DEBUG("OTA HTTPS Event On Finish"); break;
            case HTTP_EVENT_DISCONNECTED: LOG_DEBUG("OTA HTTPS Event Disconnected"); break;
            case HTTP_EVENT_REDIRECT:     LOG_DEBUG("OTA HTTPS Event Redirect"); break;
        }
        return ESP_OK;
    }

    static bool _performOtaUpdate() { // TODO: improve process to ensure the response is actually sent after reboot
        LOG_DEBUG("Starting OTA update from URL: %.100s...", _otaCurrentUrl); // Truncate long URLs in logs

        WiFiClient testClient;
        // Extract the DNS to test from the URL
        char host[URL_BUFFER_SIZE]; // Small since we don't have all the presigned stuff
        if (extractHost(_otaCurrentUrl, host, sizeof(host))) {
            if (testClient.connect(host, 443)) { // Being HTTPS, the port is 443
                LOG_DEBUG("DNS resolution successful");
                testClient.stop();
            } else {
                LOG_WARNING("DNS resolution failed (URL: %.100s...). OTA may not work as expected.", _otaCurrentUrl);
            }
        } else {
            LOG_WARNING("Failed to extract host (URL: %.100s). Could not test DNS resolution.", _otaCurrentUrl);
        }

        esp_http_client_config_t _httpConfig = {
            .url = _otaCurrentUrl,
            .cert_pem = AWS_IOT_CORE_CA_CERT, // Same as the one used to connect to AWS IoT Core via MQTT
            .event_handler = _otaHttpEventHandler,
            .buffer_size_tx = OTA_HTTPS_BUFFER_SIZE_TX, // Increase TX buffer to handle large presigned URLs
            .skip_cert_common_name_check = false
        };

        esp_https_ota_config_t _otaConfig = {
            .http_config = &_httpConfig,
            .http_client_init_cb = nullptr,
            .bulk_flash_erase = false,
            .partial_http_download = false,
            .max_http_request_size = 0
        };

        // Do the actual OTA (your existing code)
        esp_err_t result = esp_https_ota(&_otaConfig);

        if (result == ESP_OK) {
            LOG_INFO("OTA update completed successfully");
            return true;
        } else {
            LOG_ERROR("OTA update failed with error: %s (%d)", esp_err_to_name(result), result);
            return false;
        }
    }
    
    static void _otaTask(void* parameter) {
        LOG_DEBUG("OTA task started");

        bool otaSuccess = _performOtaUpdate();

        // Prepare the final status update topic
        char partialTopic[MQTT_TOPIC_BUFFER_SIZE];
        snprintf(partialTopic, sizeof(partialTopic), "jobs/%s/update", _otaCurrentJobId);

        char fullTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(partialTopic, fullTopic, sizeof(fullTopic));

        // Prepare the final status payload
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        if (otaSuccess) {
            doc["status"] = "SUCCEEDED";
            doc["statusDetails"]["reason"] = "success";
            LOG_INFO("OTA update successful for job %s. Preparing to restart.", _otaCurrentJobId);
            
            // Publish success status
            _publishJsonStreaming(doc, fullTopic);

            setRestartSystem("OTA update successful");
        } else {
            doc["status"] = "FAILED";
            doc["statusDetails"]["reason"] = "download_failed";
            LOG_ERROR("OTA update failed for job %s.", _otaCurrentJobId);
            
            // Publish failure status
            _publishJsonStreaming(doc, fullTopic);
        }

        // Clean up
        _otaTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }

    static bool _validateAwsIotJobMessage(const char* message, const char* topic) {
        // Example of JSON from AWS:
        // Topic: jobs/<jobId>/get/accepted AND jobs/notify-next
        // {
        //     "timestamp": 1755593546,
        //     "execution": {
        //         "jobId": "energyme-home-deploy-00-12-31",
        //         "status": "QUEUED",
        //         "queuedAt": 1755593545,
        //         "lastUpdatedAt": 1755593545,
        //         "versionNumber": 1,
        //         "executionNumber": 1,
        //         "jobDocument": {
        //             "operation": "ota_update",
        //             "firmware": {
        //                 "version": "00.12.31",
        //                 "url": "XXX"
        //             }
        //         }
        //     }
        // }
        // Topic: jobs/get/accepted
        // {
        //     "timestamp": 1755608479,
        //     "inProgressJobs": [],
        //     "queuedJobs": [
        //             {
        //                 "jobId": "energyme-home-ota-00_12_31-thing-588c81c47a00",
        //                 "queuedAt": 1755607935,
        //                 "lastUpdatedAt": 1755607935,
        //                 "executionNumber": 1,
        //                 "versionNumber": 1
        //             }
        //         ]
        // }

        if (!message) {
            LOG_WARNING("Received null message in AWS IoT job handler");
            return false;
        }

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            LOG_ERROR("Failed to parse AWS IoT job message JSON (%s): %s", error.c_str(), message);
            return false;
        }

        if (endsWith(topic, "jobs/get/accepted")) {
            // Ensure at least inProgressJobs or queuedJobs is present
            if (!doc["inProgressJobs"].is<JsonArray>() && !doc["queuedJobs"].is<JsonArray>()) {
                LOG_WARNING("Job list response is missing both inProgressJobs and queuedJobs, ignoring.");
                return false;
            }
        } else if (endsWith(topic, "jobs/notify-next") || endsWith(topic, "/get/accepted")) {
            // Single job execution response (notify-next OR specific job get accepted)
            if (!doc["execution"].is<JsonObject>()) { LOG_DEBUG("Execution response missing 'execution' object, ignoring."); return false; } // An empty document is sent when the job queue is cleared, so null is expected
            if (!doc["execution"]["jobId"].is<const char*>()) { LOG_WARNING("Execution response missing jobId, ignoring."); return false; }
            if (!doc["execution"]["jobDocument"].is<JsonObject>()) { LOG_WARNING("Execution response missing jobDocument, ignoring."); return false; }
            if (!doc["execution"]["jobDocument"]["operation"].is<const char*>()) { LOG_WARNING("Execution response missing operation, ignoring."); return false; }
            if (!doc["execution"]["jobDocument"]["firmware"].is<JsonObject>()) { LOG_WARNING("Execution response missing firmware object, ignoring."); return false; }
            if (!doc["execution"]["jobDocument"]["firmware"]["url"].is<const char*>()) { LOG_WARNING("Execution response missing firmware URL, ignoring."); return false; }
        } else if (endsWith(topic, "/update/accepted") || endsWith(topic, "/update/rejected")) {
            // Handle job update response topics (AWS IoT sends these automatically when we publish job status updates)
            // These are confirmation messages that our job status updates were received - just acknowledge and ignore
            LOG_DEBUG("Received job update confirmation from AWS IoT: %s", topic);
            return false; // Don't process these further, just acknowledge receipt
        } else {
            LOG_WARNING("Unrecognized AWS IoT Jobs topic pattern: %s", topic);
            return false;
        }

        // We managed to pass all checks
        return true;
    }

    static void _handleJobListResponse(JsonDocument &doc) {
        // Handle queued jobs first
        if (doc["queuedJobs"].is<JsonArray>()) {
            JsonArray queuedJobs = doc["queuedJobs"].as<JsonArray>();
            LOG_DEBUG("Found %d queued job(s)", queuedJobs.size());
            
            for (JsonVariant job : queuedJobs) {
                if (job["jobId"].is<const char*>()) {
                    const char* jobId = job["jobId"].as<const char*>();
                    LOG_INFO("Requesting details for queued job: %s", jobId);
                    _publishOtaJobDetail(jobId);
                    break; // Process only the first job to avoid overwhelming the device
                }
            }
        }
        
        // Handle in-progress jobs
        if (doc["inProgressJobs"].is<JsonArray>()) {
            JsonArray inProgressJobs = doc["inProgressJobs"].as<JsonArray>();
            LOG_DEBUG("Found %d in-progress job(s)", inProgressJobs.size());
            
            for (JsonVariant job : inProgressJobs) {
                if (job["jobId"].is<const char*>()) {
                    const char* jobId = job["jobId"].as<const char*>();
                    LOG_INFO("Requesting details for in-progress job: %s", jobId);
                    _publishOtaJobDetail(jobId);
                    break; // Process only the first job
                }
            }
        }
    }

    static void _handleSingleJobExecution(JsonDocument &doc) {
        const char* jobId = doc["execution"]["jobId"].as<const char*>();
        const char* operation = doc["execution"]["jobDocument"]["operation"].as<const char*>();
        const char* url = doc["execution"]["jobDocument"]["firmware"]["url"].as<const char*>();

        // Additional validation for operation type (validation function only checks existence)
        if (strcmp(operation, "ota_update") != 0) {
            LOG_WARNING("Job operation '%s' is not supported, rejecting job %s.", operation, jobId);

            SpiRamAllocator allocator;
            JsonDocument docReject(&allocator);
            docReject["status"] = "REJECTED";
            docReject["statusDetails"]["reason"] = "unsupported_operation";

            char partialTopic[MQTT_TOPIC_BUFFER_SIZE];
            snprintf(partialTopic, sizeof(partialTopic), "jobs/%s/update", jobId);

            char fullTopic[MQTT_TOPIC_BUFFER_SIZE];
            _constructMqttTopicReservedThings(partialTopic, fullTopic, sizeof(fullTopic));

            _publishJsonStreaming(docReject, fullTopic);
            return;
        }

        LOG_INFO("Received OTA Job '%s'. Firmware URL length: %d", jobId, strlen(url));

        // 1. Acknowledge the job and set status to IN_PROGRESS
        char partialTopic[MQTT_TOPIC_BUFFER_SIZE];
        snprintf(partialTopic, sizeof(partialTopic), "jobs/%s/update", jobId);

        char fullTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(partialTopic, fullTopic, sizeof(fullTopic));

        SpiRamAllocator allocator;
        JsonDocument docStatus(&allocator);
        docStatus["status"] = "IN_PROGRESS";
        docStatus["statusDetails"]["reason"] = "downloading";
        _publishJsonStreaming(docStatus, fullTopic);

        // Save in the static variables to ensure we don't have any dangling pointers
        snprintf(_otaCurrentUrl, OTA_PRESIGNED_URL_BUFFER_SIZE, "%s", url);
        snprintf(_otaCurrentJobId, sizeof(_otaCurrentJobId), "%s", jobId);

        LOG_DEBUG("Starting OTA task with %d bytes stack in internal RAM (writes firmware to flash)", OTA_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _otaTask, 
            OTA_TASK_NAME, 
            OTA_TASK_STACK_SIZE, 
            nullptr,
            OTA_TASK_PRIORITY, 
            &_otaTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create OTA task");
        }
    }

    static void _handleAwsIotJobMessage(const char* message, const char* topic) {
        if (!_validateAwsIotJobMessage(message, topic)) return;

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            LOG_ERROR("Failed to deserialize validated AWS IoT job message (%s)", error.c_str());
            return;
        }

        if (endsWith(topic, "jobs/get/accepted")) _handleJobListResponse(doc);
        else if (endsWith(topic, "jobs/notify-next") || strstr(topic, "/get/accepted") != nullptr) _handleSingleJobExecution(doc);
    }

    // Publishing functions
    // ====================

    static void _publishMeter() {
        // Check if we have any data to publish
        UBaseType_t queueSize = _meterQueue ? uxQueueMessagesWaiting(_meterQueue) : 0;

        bool hasQueueData = (queueSize > 0 && _sendPowerDataEnabled); // Only consider queue if power data is enabled
        bool hasChannelData = false;
        
        // Check if any channels have valid data (always include energy data)
        for (uint8_t i = 0; i < CHANNEL_COUNT && !hasChannelData; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                hasChannelData = true;
            }
        }
        
        // Always publish if we have voltage or channel energy data, queue data is optional
        if (!hasChannelData && !hasQueueData) {
            LOG_VERBOSE("No valid meter data to publish");
            return;
        }
        
        if (!_publishMeterJson()) {
            LOG_ERROR("Failed to publish meter data");
            return;
        }

        _lastMillisMeterPublished = millis64();
    }
    
    static void _publishSystemStatic() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        JsonDocument docSystemStatic;
        SystemStaticInfo systemStaticInfo;
        populateSystemStaticInfo(systemStaticInfo);
        systemStaticInfoToJson(systemStaticInfo, docSystemStatic);
        doc["data"] = docSystemStatic;

        if (_publishJsonStreaming(doc, _mqttTopicSystemStatic, true)) { // retain static info since it is idempotent
            _publishMqtt.systemStatic = false; 
            LOG_DEBUG("System static info published to MQTT");
        } else {
            LOG_ERROR("Failed to publish system static info");
        }
    }

    static void _publishSystemDynamic() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        JsonDocument docSystemDynamic;
        SystemDynamicInfo systemDynamicInfo;
        populateSystemDynamicInfo(systemDynamicInfo);
        systemDynamicInfoToJson(systemDynamicInfo, docSystemDynamic);
        doc["data"] = docSystemDynamic;

        if (_publishJsonStreaming(doc, _mqttTopicSystemDynamic)) {
            _publishMqtt.systemDynamic = false;
            _lastMillisSystemDynamicPublished = millis64();
            LOG_DEBUG("System dynamic info published to MQTT");
        } else {
            LOG_ERROR("Failed to publish system dynamic info");
        }
    }

    static void _publishChannel() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        SpiRamAllocator allocatorData;
        JsonDocument docChannelData(&allocatorData);
        Ade7953::getAllChannelDataAsJson(docChannelData);

        doc["data"] = docChannelData;

        if (_publishJsonStreaming(doc, _mqttTopicChannel, true)) { // retain channel info since it is idempotent
            _publishMqtt.channel = false;
            LOG_DEBUG("Channel data published to MQTT");
        } else {
            LOG_ERROR("Failed to publish channel data");
        }
    }

    static void _publishStatistics() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        SpiRamAllocator allocatorStatistics;
        JsonDocument docStatistics(&allocatorStatistics);
        statisticsToJson(statistics, docStatistics);
        doc["statistics"] = docStatistics;

        if (_publishJsonStreaming(doc, _mqttTopicStatistics)) {
            _publishMqtt.statistics = false;
            _lastMillisStatisticsPublished = millis64();
            LOG_DEBUG("Statistics published to MQTT");
        } else {
            LOG_ERROR("Failed to publish statistics");
        }
    }

    static void _publishCrash() {
        if (!_publishCrashJson()) {
            LOG_ERROR("Failed to publish crash data");
            _publishMqtt.crash = false; // Need this to avoid infinite loop (fail - retry)
            return;
        }

        _publishMqtt.crash = false;
    }

    static void _publishOtaJobsRequest() {
        if (!_publishOtaJobsRequestJson()) {
            LOG_ERROR("Failed to publish OTA request");
            return;
        }

        _publishMqtt.requestOta = false;
    }

    static void _publishLog(const LogEntry& entry)
    {
        char logTopic[sizeof(_mqttTopicLog) + 8 + 2]; // 8 is the maximum size of the log level string
        snprintf(logTopic, sizeof(logTopic), "%s/%s", _mqttTopicLog, AdvancedLogger::logLevelToStringLower(entry.level));

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
        AdvancedLogger::getTimestampIsoUtcFromUnixTimeMilliseconds(entry.unixTimeMilliseconds, timestamp, sizeof(timestamp));
        
        doc["timestamp"] = timestamp;
        doc["millis"] = entry.millis;
        doc["core"] = entry.coreId;
        doc["file"] = entry.file;
        doc["function"] = entry.function;
        doc["message"] = entry.message;

        if (!_publishJsonStreaming(doc, logTopic)) {
            LOG_ERROR("Failed to publish log entry via streaming");
        }
    }

    static bool _publishProvisioningRequest() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        doc["firmwareVersion"] = FIRMWARE_BUILD_VERSION;
        doc["sketchMD5"] = ESP.getSketchMD5();
        doc["chipId"] = ESP.getEfuseMac();

        return _publishJsonStreaming(doc, _mqttTopicProvisioningRequest);
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.systemStatic) {_publishSystemStatic();}
        if (_publishMqtt.systemDynamic) {_publishSystemDynamic();}
        if (_publishMqtt.channel) {_publishChannel();}
        if (_publishMqtt.statistics) {_publishStatistics();}
        if (_publishMqtt.crash) {_publishCrash();}
        if (_publishMqtt.requestOta) {_publishOtaJobsRequest();}
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

        // Check if certificate buffers are allocated
        if (_awsIotCoreCert == nullptr || _awsIotCorePrivateKey == nullptr) {
            LOG_ERROR("Certificate buffers not allocated");
            return false;
        }

        // Validate certificates before setting them
        if (!_validateCertificateFormat(_awsIotCoreCert, "device cert") ||
            !_validateCertificateFormat(_awsIotCorePrivateKey, "private key")) {
            LOG_ERROR("Certificate validation failed");
            return false;
        }

        _net.setCACert(AWS_IOT_CORE_CA_CERT);
        _net.setCertificate(_awsIotCoreCert);
        _net.setPrivateKey(_awsIotCorePrivateKey);

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
            _publishMqtt.requestOta = true;

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

            // If we exceed the maximum number of connection attempts, we restart the device
            if (_mqttConnectionAttempt >= MQTT_MAX_CONNECTION_ATTEMPTS) {
                LOG_ERROR("Exceeded maximum MQTT connection attempts. Restarting device...");
                setRestartSystem("Exceeded maximum MQTT connection attempts");
            }

            uint64_t backoffDelay = calculateExponentialBackoff(_mqttConnectionAttempt, MQTT_INITIAL_RETRY_INTERVAL, MQTT_MAX_RETRY_INTERVAL, MQTT_RETRY_MULTIPLIER);
            _nextMqttConnectionAttemptMillis = millis64() + backoffDelay;
            LOG_WARNING("Failed to connect to MQTT (attempt %lu). Reason: %s. Next attempt in %llu ms", _mqttConnectionAttempt, _getMqttStateReason(currentState), backoffDelay);

            return false;
        }
    }

    static bool _setCertificatesFromPreferences() {
        // Check if certificate buffers are allocated
        if (_awsIotCoreCert == nullptr || _awsIotCorePrivateKey == nullptr) {
            LOG_ERROR("Certificate buffers not allocated");
            return false;
        }
        
        // Ensure the certificates are clean
        memset(_awsIotCoreCert, 0, CERTIFICATE_BUFFER_SIZE);
        memset(_awsIotCorePrivateKey, 0, CERTIFICATE_BUFFER_SIZE);

        _readEncryptedPreferences(PREFS_KEY_CERTIFICATE, preshared_encryption_key, _awsIotCoreCert, CERTIFICATE_BUFFER_SIZE);
        _readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, preshared_encryption_key, _awsIotCorePrivateKey, CERTIFICATE_BUFFER_SIZE);

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

        _net.setCACert(AWS_IOT_CORE_CA_CERT);
        _net.setCertificate(aws_iot_core_cert_certclaim);
        _net.setPrivateKey(aws_iot_core_cert_privateclaim);

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
            if (!_clientMqtt.loop()) {
                LOG_WARNING("MQTT loop failed");
                break;
            }

            LOG_DEBUG("Waiting for provisioning response (%llu ms remaining)...", deadline - millis64());

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

        LOG_WARNING("Provisioning response timeout. Will retry later");
        return false;
    }

    static bool _publishJsonStreaming(JsonDocument &jsonDocument, const char* topic, bool retain) {
        if (topic == nullptr) {
            LOG_WARNING("Null topic provided");
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.connected()) {
            LOG_WARNING("MQTT not connected (%s). Skipping streaming publish on %s", _getMqttStateReason(_clientMqtt.state()), topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        size_t payloadLength = measureJson(jsonDocument);
        if (payloadLength == 0) {
            LOG_WARNING("Empty JSON payload. Skipping streaming publish to %s", topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        LOG_DEBUG("Starting streaming publish to topic '%s' with payload size %zu bytes", topic, payloadLength);

        if (!_clientMqtt.beginPublish(topic, payloadLength, retain)) {
            LOG_WARNING("Failed to begin streaming publish to %s. MQTT client state: %s", topic, _getMqttStateReason(_clientMqtt.state()));
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        BufferingPrint bufferedMqttClient(_clientMqtt, STREAM_UTILS_MQTT_PACKET_SIZE);
        size_t bytesWritten = serializeJson(jsonDocument, bufferedMqttClient);
        bufferedMqttClient.flush();
        _clientMqtt.endPublish();

        if (bytesWritten != payloadLength) {
            LOG_WARNING("Streaming publish size mismatch on %s: expected %zu bytes, wrote %zu bytes", topic, payloadLength, bytesWritten);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        statistics.mqttMessagesPublished++;
        LOG_DEBUG("Streaming publish successful: %zu bytes written to topic '%s'", bytesWritten, topic);
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

    static bool _publishMeterStreaming() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        // Always add voltage data if available (independent of sendPowerDataEnabled)
        MeterValues meterValuesZeroChannel;
        
        if (Ade7953::getMeterValues(meterValuesZeroChannel, 0) && meterValuesZeroChannel.lastUnixTimeMilliseconds > 0) {
            JsonObject voltageObj = doc.add<JsonObject>();
            voltageObj["unixTime"] = meterValuesZeroChannel.lastUnixTimeMilliseconds;
            voltageObj["voltage"] = meterValuesZeroChannel.voltage;
        }

        // Always add channel energy data (independent of sendPowerDataEnabled)
        for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                MeterValues meterValues;

                if (!Ade7953::getMeterValues(meterValues, i)) {
                    LOG_DEBUG("Failed to get meter values for channel %d. Skipping for meter publishing", i);
                    continue;
                }
                
                JsonObject channelObj = doc.add<JsonObject>();
                channelObj["unixTime"] = meterValues.lastUnixTimeMilliseconds;
                channelObj["channel"] = i;
                channelObj["activeEnergyImported"] = roundToDecimals(meterValues.activeEnergyImported, ENERGY_DECIMALS);
                channelObj["activeEnergyExported"] = roundToDecimals(meterValues.activeEnergyExported, ENERGY_DECIMALS);
                channelObj["reactiveEnergyImported"] = roundToDecimals(meterValues.reactiveEnergyImported, ENERGY_DECIMALS);
                channelObj["reactiveEnergyExported"] = roundToDecimals(meterValues.reactiveEnergyExported, ENERGY_DECIMALS);
                channelObj["apparentEnergy"] = roundToDecimals(meterValues.apparentEnergy, ENERGY_DECIMALS);
            }
        }
        
        // Only add power data points if sendPowerDataEnabled is true
        uint32_t entriesAdded = 0;
        if (_sendPowerDataEnabled && _initializeMeterQueue()) {
            PayloadMeter payloadMeter;
            uint32_t loops = 0;
            while ((uxQueueMessagesWaiting(_meterQueue) > 0) && loops < MAX_LOOP_ITERATIONS) {
                loops++;

                if (xQueueReceive(_meterQueue, &payloadMeter, 0) != pdTRUE) break;

                JsonArray powerArray = doc.add<JsonArray>();
                powerArray.add(payloadMeter.unixTimeMs);
                powerArray.add(payloadMeter.channel);
                powerArray.add(roundToDecimals(payloadMeter.activePower, POWER_DECIMALS));
                powerArray.add(roundToDecimals(payloadMeter.powerFactor, POWER_FACTOR_DECIMALS));
                entriesAdded++;

                // Check if we're approaching memory limits (using psram automatically)
                if (measureJson(doc) > AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * 0.95) {
                    LOG_DEBUG("Meter data JSON size exceeds 95%% of AWS IoT Core MQTT payload limit, stopping queue processing");
                    break; // Stop adding new entries if the buffer is nearly full
                }
            }
        }

        // Validate that we have actual data before publishing
        if (doc.size() == 0) {
            LOG_DEBUG("No meter data available for publishing");
            return true; // Not an error, just no data
        }

        LOG_DEBUG("Publishing meter JSON with %u entries | Size: %u bytes | Queue entries added: %u | Remaining in queue: %u", 
                  doc.size(), measureJson(doc), entriesAdded, uxQueueMessagesWaiting(_meterQueue));
        
        return _publishJsonStreaming(doc, _mqttTopicMeter);
    }

    static bool _publishMeterJson() {
        if (!_initializeMeterQueue()) return false;

        // Single publish attempt - no loop to clear entire queue
        if (_publishMeterStreaming()) {
            _publishMqtt.meter = false;
            LOG_DEBUG("Meter data published successfully");
            return true;
        } else {
            LOG_ERROR("Failed to publish meter data");
            return false;
        }
    }

    static bool _publishCrashJson() {
        // Generate a unique crash ID for this crash event
        uint64_t crashId = CustomTime::getUnixTimeMilliseconds();
        
        // First, publish crash info metadata
        SpiRamAllocator allocator;
        JsonDocument docInfo(&allocator);
        docInfo["unixTime"] = crashId; // Use same timestamp as crash ID
        docInfo["crashId"] = crashId;
        docInfo["messageType"] = "crashInfo";
        
        SpiRamAllocator allocatorCrashInfo;
        JsonDocument docCoreDump(&allocatorCrashInfo);
        CrashMonitor::getCoreDumpInfoJson(docCoreDump);
        docInfo["crashInfo"] = docCoreDump;

        if (!_publishJsonStreaming(docInfo, _mqttTopicCrash)) {
            LOG_ERROR("Failed to publish crash info metadata");
            return false;
        }
        LOG_DEBUG("Crash info metadata published successfully with crash ID: %llu", crashId);

        // Then, send each core dump chunk as a separate message
        size_t coreDumpSize = CrashMonitor::getCoreDumpSize();
        LOG_DEBUG("Core dump size to send via MQTT: %zu bytes", coreDumpSize);
        size_t offset = 0;
        uint32_t chunkIndex = 0;
        uint32_t totalChunks = (coreDumpSize + CORE_DUMP_CHUNK_SIZE - 1) / CORE_DUMP_CHUNK_SIZE; // Calculate total chunks

        while (offset < coreDumpSize) {
            size_t thisChunkSize = (coreDumpSize - offset) < CORE_DUMP_CHUNK_SIZE ? (coreDumpSize - offset) : CORE_DUMP_CHUNK_SIZE;

            SpiRamAllocator allocatorChunk;
            JsonDocument docChunk(&allocatorChunk);
            docChunk["unixTime"] = CustomTime::getUnixTimeMilliseconds();
            docChunk["crashId"] = crashId; // Same crash ID for all chunks
            docChunk["messageType"] = "crashChunk";
            docChunk["chunkIndex"] = chunkIndex;
            docChunk["totalChunks"] = totalChunks;

            SpiRamAllocator allocatorCoreDumpChunk;
            JsonDocument docJsonCoreDumpChunk(&allocatorCoreDumpChunk);
            if (!CrashMonitor::getCoreDumpChunkJson(docJsonCoreDumpChunk, offset, thisChunkSize)) {
                LOG_ERROR("Failed to get core dump chunk at offset %zu", offset);
                return false;
            }
            
            // Copy chunk data directly into the message
            docChunk["chunk"] = docJsonCoreDumpChunk;
            
            if (!_publishJsonStreaming(docChunk, _mqttTopicCrash)) {
                LOG_ERROR("Failed to publish crash chunk %u/%u at offset %zu", chunkIndex + 1, totalChunks, offset);
                return false;
            }
            
            LOG_DEBUG("Published crash chunk %u/%u (crash ID: %llu, offset %zu, size %zu bytes)", chunkIndex + 1, totalChunks, crashId, offset, thisChunkSize);
            
            offset += thisChunkSize;
            chunkIndex++;
        }

        _publishMqtt.crash = false;
        
        // Clear core dump after successful transmission
        if (CrashMonitor::hasCoreDump()) {
            #ifndef ENV_DEV // In dev environment we keep the core dump for testing purposes, and eventually delete via API
            CrashMonitor::clearCoreDump();
            #endif
            LOG_INFO("Core dump cleared after successful MQTT transmission");
        }
        
        LOG_DEBUG("All crash data published successfully: %u chunks sent for crash ID %llu", totalChunks, crashId);
        return true;
    }

    static bool _publishOtaJobsRequestJson() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator); // This could be empty, but we send the time anyway
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings("jobs/get", topic, sizeof(topic));

        if (!_publishJsonStreaming(doc, topic)) {
            LOG_ERROR("Failed to publish OTA request");
            return false;
        }

        LOG_DEBUG("OTA request published successfully");
        return true;
    }

    static void _publishOtaJobDetail(const char* jobId) {
        char jobTopic[MQTT_TOPIC_BUFFER_SIZE];
        snprintf(jobTopic, sizeof(jobTopic), "jobs/%s/get", jobId);

        char fullTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(jobTopic, fullTopic, sizeof(fullTopic));

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator); // This could be empty, but we send the time anyway
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        if (_publishJsonStreaming(doc, fullTopic)) {
            LOG_DEBUG("Requested job details for: %s", jobId);
        } else {
            LOG_ERROR("Failed to request job details for: %s", jobId);
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

        char *encryptedData = (char*)ps_malloc(CERTIFICATE_BUFFER_SIZE);
        if (!encryptedData) {
            LOG_ERROR("Failed to allocate encrypted data buffer in PSRAM");
            preferences.end();
            return;
        }
        
        memset(encryptedData, 0, CERTIFICATE_BUFFER_SIZE); // Clear before setting
        preferences.getString(preferenceKey, encryptedData, CERTIFICATE_BUFFER_SIZE);
        preferences.end();

        if (strlen(encryptedData) == 0) {
            LOG_DEBUG("No encrypted data found for key %s", preferenceKey);
            free(encryptedData);
            return;
        }

        // Use GCM decrypt with SHA256(preshared||DEVICE_ID)
        _decryptData(encryptedData, presharedEncryptionKey, decryptedData, decryptedDataSize);
        free(encryptedData);
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
        uint8_t *decoded = (uint8_t*)ps_malloc(CERTIFICATE_BUFFER_SIZE);
        if (!decoded) {
            LOG_ERROR("Failed to allocate decoded buffer in PSRAM");
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            return;
        }
        
        size_t decodedLen = 0;
        int ret = mbedtls_base64_decode(decoded, CERTIFICATE_BUFFER_SIZE, &decodedLen,
                                        reinterpret_cast<const unsigned char*>(encryptedDataBase64), inputLength);
        if (ret != 0 || decodedLen < (12 + 16)) { // must at least contain IV + TAG
            if (decryptedData != nullptr && decryptedDataSize > 0) decryptedData[0] = '\0';
            LOG_ERROR("Failed to decode base64 or data too short (%u)", (unsigned)decodedLen);
            free(decoded);
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
            free(decoded);
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
            free(decoded);
            LOG_ERROR("AES-GCM auth decrypt failed (%d)", dec);
            return;
        }

        // Ensure null termination (certificate is ASCII PEM)
        if (ctLen >= decryptedDataSize) ctLen = decryptedDataSize - 1;
        decryptedData[ctLen] = '\0';
        // Clear sensitive buffers
        memset(decoded, 0, CERTIFICATE_BUFFER_SIZE);
        free(decoded);

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

    bool extractHost(const char* url, char* buffer, size_t bufferSize) {
        if (!url || !buffer || bufferSize == 0) return false;

        const char* start = strstr(url, "://");
        if (!start) return false;
        start += 3; // skip "://"

        const char* end = strchr(start, '/');
        if (!end) {
            // No slash after host, take entire remaining string
            end = url + strlen(url);
        }

        size_t length = end - start;
        if (length + 1 > bufferSize) return false; // not enough space

        snprintf(buffer, bufferSize, "%.*s", (int)length, start);
        return true;
    }
}
#endif