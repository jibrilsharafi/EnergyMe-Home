// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "mqtt.h"
#include "issueregistry.h"
#include "crashmonitor.h"
#include "shadow.h"
#include "rollback_logic.h"
#include "shadow_logic.h"
#include "taskprofiler.h"
#include "duration_format.h"
#include "mqtt_grid_schedule.h"
#include "mqtt_energy_publish_gate.h"
#include "crash_archive_policy.h"
#include "mbedtls/base64.h"
#include <algorithm>

namespace Mqtt
{
    static TaskHeartbeat _mqttHeartbeat;

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
    static QueueHandle_t _gridQueue = nullptr;
    static QueueHandle_t _alarmQueue = nullptr;

    // Static queue structures and storage for PSRAM
    static StaticQueue_t _logQueueStruct;
    static StaticQueue_t _meterQueueStruct;
    static StaticQueue_t _gridQueueStruct;
    static StaticQueue_t _alarmQueueStruct;
    static uint8_t* _logQueueStorage = nullptr;
    static uint8_t* _meterQueueStorage = nullptr;
    static uint8_t* _gridQueueStorage = nullptr;
    static uint8_t* _alarmQueueStorage = nullptr;

    // Connection attempt tracking
    static uint32_t _mqttConnectionAttempt = 0;
    static uint64_t _nextMqttConnectionAttemptMillis = 0;

    // Connection state fact for the issue registry, updated once per task loop
    // (the registry tick must not call _clientMqtt.connected() cross-task)
    static volatile bool _lastConnectedState = false;

    // Last publish timestamps
    static uint64_t _lastMillisMeterPublished = 0;
    static uint64_t _lastMillisSystemDynamicPublished = 0;
    static uint64_t _lastMillisStatisticsPublished = 0;
    static uint64_t _lastMillisCrashPublished = 0;

    // Next grid publish deadline (unix seconds, wall-clock aligned); 0 = not yet scheduled
    static uint64_t _nextGridPublishUnixSecond = 0;
    // Next energy publish deadline (unix seconds, wall-clock aligned); 0 = not yet scheduled
    static uint64_t _nextEnergyPublishUnixSecond = 0;

    // Configuration
    static bool _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
    static bool _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;
    static bool _sendGridDataEnabled = DEFAULT_SEND_GRID_DATA_ENABLED;
    static uint32_t _meterPublishThresholdBytes = MQTT_METER_PUBLISH_THRESHOLD_BYTES_DEFAULT;
    static uint32_t _meterPublishMaxIntervalMs = MQTT_METER_PUBLISH_MAX_INTERVAL_MS_DEFAULT;
    static uint8_t _mqttLogLevelInt = DEFAULT_MQTT_LOG_LEVEL_INT;
    static LogLevel _mqttMinLogLevel = LogLevel::INFO;

    // Certificates storage (loaded from factory NVS) - allocated in PSRAM
    static char *_awsIotCoreCert = nullptr;
    static char *_awsIotCorePrivateKey = nullptr;
    static bool _certificatesLoaded = false;
    
    // Topic buffers
    static char _mqttTopicMeter[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicGrid[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicEnergy[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicSystemDynamic[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicStatistics[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicCrash[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicLog[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicAlarm[MQTT_TOPIC_BUFFER_SIZE];

    // Task variables
    static TaskHandle_t _taskHandle = nullptr;
    static bool _lastLoopToPublishData = false;
    static bool _taskShouldRun = false;

    // AWS IoT Jobs OTA task (global to ensure they work and do not get dereferenced)
    static char *_otaCurrentUrl = nullptr;  // URL_BUFFER_SIZE - allocated in PSRAM
    static char _otaCurrentJobId[NAME_BUFFER_SIZE];
    static TaskHandle_t _otaTaskHandle = nullptr;
    static TaskHandle_t _otaValidationTaskHandle = nullptr;
    static bool _otaRebootPending = false;

    // Thread safety
    static SemaphoreHandle_t _configMutex = nullptr;

    // Private function declarations
    // =============================
    // =============================

    // Queue initialization
    static bool _initializeLogQueue();
    static bool _initializeMeterQueue();
    static bool _initializeGridQueue();
    static bool _initializeAlarmQueue();
    
    // Configuration management
    static void _loadConfigFromPreferences();
    static void _saveConfigToPreferences();
    
    static void _setSendPowerDataEnabled(bool enabled);
    static void _setSendGridDataEnabled(bool enabled);
    static void _setMeterPublishThresholdBytes(uint32_t bytes);
    static void _setMeterPublishMaxIntervalMs(uint32_t intervalMs);
    static void _setMqttLogLevel(const char* logLevel);
    static void _updateMqttMinLogLevel();

    static void _saveCloudServicesEnabledToPreferences(bool enabled);
    static void _saveSendPowerDataEnabledToPreferences(bool enabled);
    static void _saveSendGridDataEnabledToPreferences(bool enabled);
    static void _saveMeterPublishThresholdBytesToPreferences(uint32_t bytes);
    static void _saveMeterPublishMaxIntervalMsToPreferences(uint32_t intervalMs);
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
    static void _setTopicGrid();
    static void _setTopicEnergy();
    static void _setTopicSystemDynamic();
    static void _setTopicStatistics();
    static void _setTopicCrash();
    static void _setTopicLog();
    static void _setTopicAlarm();

    // Subscription management
    static void _subscribeToTopics();
    static void _subscribeAwsIotJobs();
    static void _subscribeIotCommands();

    // Subscription callback handler
    static void _subscribeCallback(const char* topic, byte *payload, uint32_t length);
    static void _handleCommandExecution(const char* topic, const char* message);
    static void _publishCommandStatus(const char* executionId, const char* status, const char* reasonCode, const char* reasonDescription);
    static void _queueCommand(const char* topic, const char* payload);
    static void _drainPendingCommand();
    static void _handleAwsIotJobMessage(const char* message, const char* topic);
    
    // AWS IoT Jobs OTA functions
    static bool _validateAwsIotJobMessage(const char* message, const char* topic);
    static void _handleJobListResponse(JsonDocument &doc);
    static void _handleSingleJobExecution(JsonDocument &doc);
    static void _publishOtaJobDetail(const char* jobId);
    static void _otaTask(void* parameter);
    static esp_err_t _otaHttpEventHandler(esp_http_client_event_t *event);
    static bool _performOtaUpdate();
    
    // OTA validation functions
    static void _clearOtaPendingState();
    static void _otaValidationTask(void* parameter);
    static void _checkPendingOtaValidation();
    static void _publishOtaStatus(const char* jobId, const char* status, const char* reason);

    // Publishing functions
    static void _publishMeter();
    static void _publishGrid();
    static void _publishEnergy();
    static void _publishSystemDynamic();
    static void _publishStatistics();
    static void _publishCrash();
    static void _publishLog(const LogEntry& entry);
    static void _publishAlarm(const AlarmEntry& entry);
    static void _publishOtaJobsRequest();

    static void _processAlarmQueue();
    static void _checkPublishMqtt();
    static void _checkIfPublishMeterNeeded();
    static void _checkIfPublishGridNeeded();
    static bool _allActiveChannelsFreshSinceBoundary(uint64_t boundaryUnixMs);
    static void _checkIfPublishEnergyNeeded();
    static void _checkIfPublishSystemDynamicNeeded();
    static void _checkIfPublishStatisticsNeeded();
    static void _checkIfPublishCrashNeeded();

    // Outcome of publishing one archived crash record. The two failure modes are
    // kept apart deliberately: Retry leaves the record at the head of the queue,
    // Skip steps over one that can never be sent so it cannot stall the rest.
    enum class CrashPublishOutcome {
        Published,
        Empty,   // Nothing archived at this index
        Retry,   // Transient failure, try the same record again later
        Skip,    // Permanently unpublishable, but kept on flash
        Dropped, // Corrupt record removed; the remaining ones shifted down
    };
    static CrashPublishOutcome _publishCrashJson(uint32_t index);

    // MQTT operations
    static bool _setCertificatesFromPreferences();
    static bool _setupMqttWithDeviceCertificates();
    static bool _connectMqtt();
    static bool _publishJsonStreaming(JsonDocument &jsonDocument, const char* topic, bool retain = false);

    // Queue processing and streaming
    static void _processLogQueue();
    static bool _publishMeterStreaming();
    static bool _publishMeterJson();
    static bool _publishOtaJobsRequestJson();
    
    // Certificate management
    static void _clearCertificatesRuntime();
    static bool _validateCertificateFormat(const char* cert, const char* certType);

    // Connection handling
    static void _handleConnecting();
    static void _handleConnectedState();

    // Utilities
    static const char* _getMqttStateReason(int32_t state);
    static void _sha256ToHex(const uint8_t sha256[32], char hexOut[65]);
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
        if (!_clientMqtt.setBufferSize(MQTT_BUFFER_SIZE)) {
            LOG_ERROR("Failed to allocate %d-byte MQTT buffer; cloud connection may be unstable", MQTT_BUFFER_SIZE);
        }
        LOG_DEBUG("MQTT buffer set to %d bytes; free internal heap now %u bytes", MQTT_BUFFER_SIZE, (unsigned)ESP.getFreeHeap());
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);
        _clientMqtt.setServer(AWS_IOT_CORE_ENDPOINT, AWS_IOT_CORE_PORT);
        _clientMqtt.setCallback(_subscribeCallback);

        _setupTopics();
        _loadConfigFromPreferences();
        Shadow::begin();
        _initializeLogQueue();
        _initializeMeterQueue();
        _initializeGridQueue();
        _initializeAlarmQueue();
        
        // Initialize OTA buffers before checking pending validation
        if (_otaCurrentUrl == nullptr) {
            _otaCurrentUrl = (char*)ps_malloc(OTA_PRESIGNED_URL_BUFFER_SIZE);
            if (_otaCurrentUrl == nullptr) {
                LOG_ERROR("Failed to allocate OTA URL buffer in PSRAM");
            } else {
                memset(_otaCurrentUrl, 0, OTA_PRESIGNED_URL_BUFFER_SIZE);
            }
        }
        
        memset(_otaCurrentJobId, 0, sizeof(_otaCurrentJobId));
        
        // Check if we just rebooted after an OTA update (must be AFTER memset to allow job ID loading)
        _checkPendingOtaValidation();

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

        // Eager certificate loading: certs are factory-provisioned and permanent
        _certificatesLoaded = _setupMqttWithDeviceCertificates();
        if (!_certificatesLoaded) {
            LOG_ERROR("Failed to load device certificates in begin(). Cloud services will not connect.");
        }

        if (_cloudServicesEnabled && _certificatesLoaded) _startTask();
        else if (_cloudServicesEnabled && !_certificatesLoaded) LOG_WARNING("Cloud services enabled but certificates not loaded - reprovision required");
        else LOG_DEBUG("Cloud services are disabled, MQTT task will not start");

        LOG_DEBUG("MQTT client setup complete");
    }

    void stop()
    {
        LOG_DEBUG("Stopping MQTT client...");
        _stopTask();
        
        _logQueue = nullptr;
        _meterQueue = nullptr;
        _gridQueue = nullptr;
        _alarmQueue = nullptr;

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

        if (_gridQueueStorage != nullptr) {
            free(_gridQueueStorage);
            _gridQueueStorage = nullptr;
            LOG_DEBUG("MQTT grid queue PSRAM freed");
        }

        if (_alarmQueueStorage != nullptr) {
            free(_alarmQueueStorage);
            _alarmQueueStorage = nullptr;
            LOG_DEBUG("MQTT alarm queue PSRAM freed");
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

        if (_cloudServicesEnabled && _certificatesLoaded) _startTask();
        else if (_cloudServicesEnabled && !_certificatesLoaded) LOG_WARNING("Cannot start MQTT task: device certificates not loaded - reprovision required");
        
        releaseMutex(&_configMutex);

        LOG_INFO("Cloud services %s", enabled ? "enabled" : "disabled");
    }

    bool isCloudServicesEnabled() { return _cloudServicesEnabled; }

    bool isConnected() { return _lastConnectedState; }

    // Public methods for requesting MQTT publications
    // ===============================================

    // Channel config now lives in the channels shadow; a channel change (REST,
    // auto-polarity, etc.) refreshes the shadow's reported state instead of
    // publishing the retired `channel` topic.
    void requestChannelPublish() { Shadow::requestReport("channels"); }
    void requestCrashPublish() {_publishMqtt.crash = true; }

    void requestImmediatePublish()
    {
        if (_taskHandle == nullptr) return;
        xTaskNotify(_taskHandle, MQTT_NOTIFY_WAKE_BIT, eSetBits);
    }

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
        if (!_sendPowerDataEnabled) return;

        // Drop-oldest on overflow: recent points are worth more than old ones
        if (xQueueSend(_meterQueue, &payload, 0) != pdTRUE) {
            PayloadMeter discarded;
            xQueueReceive(_meterQueue, &discarded, 0);
            xQueueSend(_meterQueue, &payload, 0);
            statistics.mqttMeterPointsDropped++;
        }
    }

    void pushGrid(const PayloadGridPoint& point)
    {
        if (!_initializeGridQueue()) return;
        if (!_sendGridDataEnabled) return; // Sampler already gates on this

        // Drop-oldest on overflow: recent points are worth more than old ones
        if (xQueueSend(_gridQueue, &point, 0) != pdTRUE) {
            PayloadGridPoint discarded;
            xQueueReceive(_gridQueue, &discarded, 0);
            xQueueSend(_gridQueue, &point, 0);
            statistics.mqttGridPointsDropped++;
        }
    }

    void pushAlarm(const AlarmEntry& entry)
    {
        if (!_initializeAlarmQueue()) return;
        // No drop-oldest (unlike meter/grid) - losing an alarm silently isn't
        // acceptable; block briefly instead (same as pushLog).
        if (xQueueSend(_alarmQueue, &entry, pdMS_TO_TICKS(QUEUE_WAIT_TIMEOUT)) != pdTRUE) {
            LOG_WARNING("MQTT alarm queue full, dropping alarm: %s", entry.type);
        }
        requestImmediatePublish();
    }

    TaskInfo getMqttTaskInfo()
    {
        return getTaskInfoSafely(_taskHandle, MQTT_TASK_STACK_SIZE, &_mqttHeartbeat);
    }

    TaskInfo getMqttOtaTaskInfo()
    {
        return getTaskInfoSafely(_otaTaskHandle, OTA_TASK_STACK_SIZE);
    }

    // Shadow module helpers
    // =====================

    bool subscribeReservedThings(const char* finalTopic) {
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(finalTopic, topic, sizeof(topic));
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_WARNING("Failed to subscribe to %s", topic);
            return false;
        }
        LOG_DEBUG("Subscribed to %s", topic);
        return true;
    }

    bool publishReservedThings(JsonDocument& jsonDocument, const char* finalTopic, bool retain) {
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(finalTopic, topic, sizeof(topic));
        return _publishJsonStreaming(jsonDocument, topic, retain);
    }

    int getMqttLogLevel() { return _mqttLogLevelInt; }

    void setMqttLogLevel(const char* level) { _setMqttLogLevel(level); } // validates + persists

    void setRuntimeLogLevel(int level) {
        if (level < 0 || level > 5) {
            LOG_WARNING("Invalid runtime log level %d", level);
            return;
        }
        if (!acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            LOG_ERROR("Failed to acquire config mutex for runtime log level");
            return;
        }
        _mqttLogLevelInt = (uint8_t)level;
        _updateMqttMinLogLevel();
        releaseMutex(&_configMutex);
        // Intentionally NOT persisted here: the runtime int stays transient. The
        // separate transient marker (saveTransientLogLevel) is what survives a
        // reboot; the persisted baseline key is never touched by a transient.
    }

    void saveTransientLogLevel(int level) {
        if (level < 0 || level > 5) return;
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            LOG_WARNING("Failed to open preferences to save transient log level");
            return;
        }
        // Write only on change: a long cloud-held verbose window re-asserts the
        // same level every <5 min, and we must not wear flash for a no-op.
        if (prefs.getUChar(MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY, 0xFF) != (uint8_t)level) {
            prefs.putUChar(MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY, (uint8_t)level);
        }
        prefs.end();
    }

    void clearTransientLogLevel() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) return;
        if (prefs.isKey(MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY)) {
            prefs.remove(MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY);
        }
        prefs.end();
    }

    void clearOtaPendingState() { _clearOtaPendingState(); }

    int getTransientLogLevel() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) return -1;
        uint8_t v = prefs.getUChar(MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY, 0xFF);
        prefs.end();
        return (v == 0xFF) ? -1 : (int)v;
    }

    bool getSendPowerData() { return _sendPowerDataEnabled; }

    void setSendPowerData(bool enabled) { _setSendPowerDataEnabled(enabled); } // persists

    bool getSendGridData() { return _sendGridDataEnabled; }

    void setSendGridData(bool enabled) { _setSendGridDataEnabled(enabled); } // persists

    uint32_t getMeterPublishThresholdBytes() { return _meterPublishThresholdBytes; }

    void setMeterPublishThresholdBytes(uint32_t bytes) { _setMeterPublishThresholdBytes(bytes); } // persists, clamped

    uint32_t getMeterPublishMaxIntervalMs() { return _meterPublishMaxIntervalMs; }

    void setMeterPublishMaxIntervalMs(uint32_t intervalMs) { _setMeterPublishMaxIntervalMs(intervalMs); } // persists, clamped

    // Private functions
    // =================
    // =================


    // MQTT log queue management
    // =========================

    bool _initializeLogQueue() // Cannot use logger here to avoid recursion
    {
        static bool isInitializing = false; // Guard against re-entry during initialization
        
        if (_logQueueStorage != nullptr) return true;
        
        // Prevent recursion if ESP-IDF logs during preferences access
        if (isInitializing) {
            Serial.printf("[WARNING] Re-entry detected in _initializeLogQueue, skipping to prevent recursion\n");
            return false;
        }
        
        isInitializing = true;

        // Load MQTT log level from preferences before initializing queue
        // Using read-only mode to minimize risk of triggering internal ESP-IDF logs
        Preferences prefs;
        if (prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            _mqttLogLevelInt = prefs.getUChar(MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY, DEFAULT_MQTT_LOG_LEVEL_INT);
            prefs.end();
            _updateMqttMinLogLevel(); // Convert integer to LogLevel enum
            Serial.printf("[DEBUG] MQTT log level loaded from preferences: %u\n", _mqttLogLevelInt);
        } else {
            // Failed to open preferences, use default
            Serial.printf("[WARNING] Failed to load MQTT log level from preferences, using default: %u\n", DEFAULT_MQTT_LOG_LEVEL_INT);
            _mqttLogLevelInt = DEFAULT_MQTT_LOG_LEVEL_INT;
            _updateMqttMinLogLevel();
        }

        // Allocate queue storage in PSRAM
        uint32_t queueLength = MQTT_LOG_QUEUE_SIZE / sizeof(LogEntry);
        size_t realQueueSize = queueLength * sizeof(LogEntry);
        _logQueueStorage = (uint8_t*)ps_malloc(realQueueSize);

        if (_logQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for MQTT log queue (%d bytes)\n", realQueueSize);
            isInitializing = false;
            return false;
        }

        _logQueue = xQueueCreateStatic(queueLength, sizeof(LogEntry), _logQueueStorage, &_logQueueStruct);
        if (_logQueue == nullptr) {
            Serial.println("[ERROR] Failed to create MQTT log queue");
            free(_logQueueStorage);
            _logQueueStorage = nullptr;
            isInitializing = false;
            return false;
        }

        Serial.printf("[DEBUG] MQTT log queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        isInitializing = false;
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

    bool _initializeGridQueue()
    {
        if (_gridQueueStorage != nullptr) return true;

        // Allocate queue storage in PSRAM
        uint32_t queueLength = MQTT_GRID_QUEUE_SIZE / sizeof(PayloadGridPoint);
        size_t realQueueSize = queueLength * sizeof(PayloadGridPoint);
        _gridQueueStorage = (uint8_t*)ps_malloc(realQueueSize);

        if (_gridQueueStorage == nullptr) {
            LOG_ERROR("Failed to allocate PSRAM for MQTT grid queue (%zu bytes)", realQueueSize);
            return false;
        }

        _gridQueue = xQueueCreateStatic(queueLength, sizeof(PayloadGridPoint), _gridQueueStorage, &_gridQueueStruct);
        if (_gridQueue == nullptr) {
            LOG_ERROR("Failed to create MQTT grid queue");
            free(_gridQueueStorage);
            _gridQueueStorage = nullptr;
            return false;
        }

        LOG_DEBUG("MQTT grid queue initialized with PSRAM buffer (%zu bytes) | Free PSRAM: %zu bytes", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    bool _initializeAlarmQueue()
    {
        if (_alarmQueueStorage != nullptr) return true;

        // Allocate queue storage in PSRAM
        uint32_t queueLength = MQTT_ALARM_QUEUE_SIZE / sizeof(AlarmEntry);
        size_t realQueueSize = queueLength * sizeof(AlarmEntry);
        _alarmQueueStorage = (uint8_t*)ps_malloc(realQueueSize);

        if (_alarmQueueStorage == nullptr) {
            LOG_ERROR("Failed to allocate PSRAM for MQTT alarm queue (%zu bytes)", realQueueSize);
            return false;
        }

        _alarmQueue = xQueueCreateStatic(queueLength, sizeof(AlarmEntry), _alarmQueueStorage, &_alarmQueueStruct);
        if (_alarmQueue == nullptr) {
            LOG_ERROR("Failed to create MQTT alarm queue");
            free(_alarmQueueStorage);
            _alarmQueueStorage = nullptr;
            return false;
        }

        LOG_DEBUG("MQTT alarm queue initialized with PSRAM buffer (%zu bytes) | Free PSRAM: %zu bytes", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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
        _sendGridDataEnabled = prefs.getBool(MQTT_PREFERENCES_SEND_GRID_DATA_KEY, DEFAULT_SEND_GRID_DATA_ENABLED);
        _meterPublishThresholdBytes = prefs.getUInt(MQTT_PREFERENCES_METER_PUBLISH_THRESHOLD_KEY, MQTT_METER_PUBLISH_THRESHOLD_BYTES_DEFAULT);
        _meterPublishMaxIntervalMs = prefs.getUInt(MQTT_PREFERENCES_METER_PUBLISH_INTERVAL_KEY, MQTT_METER_PUBLISH_MAX_INTERVAL_MS_DEFAULT);
        _mqttLogLevelInt = prefs.getUChar(MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY, DEFAULT_MQTT_LOG_LEVEL_INT);
        _updateMqttMinLogLevel(); // Convert integer to LogLevel enum

        LOG_DEBUG("Cloud services enabled: %s, Send power data enabled: %s, Send grid data enabled: %s, "
                   "Meter publish threshold: %u bytes, Meter publish max interval: %u ms, MQTT log level: %u",
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false",
                   _sendGridDataEnabled ? "true" : "false",
                   _meterPublishThresholdBytes,
                   _meterPublishMaxIntervalMs,
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
        _saveSendGridDataEnabledToPreferences(_sendGridDataEnabled);
        _saveMeterPublishThresholdBytesToPreferences(_meterPublishThresholdBytes);
        _saveMeterPublishMaxIntervalMsToPreferences(_meterPublishMaxIntervalMs);
        _saveMqttLogLevelToPreferences(_mqttLogLevelInt);
        
        LOG_DEBUG("MQTT preferences saved");
    }

    static void _setSendPowerDataEnabled(bool enabled)
    {
        _sendPowerDataEnabled = enabled;
        _saveSendPowerDataEnabledToPreferences(enabled);
        LOG_DEBUG("Set send power data enabled to %s", enabled ? "true" : "false");
    }

    static void _setSendGridDataEnabled(bool enabled)
    {
        _sendGridDataEnabled = enabled;
        _saveSendGridDataEnabledToPreferences(enabled);
        LOG_DEBUG("Set send grid data enabled to %s", enabled ? "true" : "false");
    }

    static void _setMeterPublishThresholdBytes(uint32_t bytes)
    {
        uint32_t clamped = std::clamp<uint32_t>(bytes, MQTT_METER_PUBLISH_THRESHOLD_BYTES_MIN, MQTT_METER_PUBLISH_THRESHOLD_BYTES_MAX);
        if (clamped != bytes) {
            LOG_WARNING("Requested meter publish threshold %u bytes out of range [%u, %u], clamped to %u bytes",
                        bytes, MQTT_METER_PUBLISH_THRESHOLD_BYTES_MIN, MQTT_METER_PUBLISH_THRESHOLD_BYTES_MAX, clamped);
        }
        _meterPublishThresholdBytes = clamped;
        _saveMeterPublishThresholdBytesToPreferences(clamped);
        LOG_DEBUG("Set meter publish threshold to %u bytes", clamped);
    }

    static void _setMeterPublishMaxIntervalMs(uint32_t intervalMs)
    {
        uint32_t clamped = std::clamp<uint32_t>(intervalMs, MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MIN, MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MAX);
        if (clamped != intervalMs) {
            LOG_WARNING("Requested meter publish max interval %u ms out of range [%u, %u], clamped to %u ms",
                        intervalMs, MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MIN, MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MAX, clamped);
        }
        _meterPublishMaxIntervalMs = clamped;
        _saveMeterPublishMaxIntervalMsToPreferences(clamped);
        LOG_DEBUG("Set meter publish max interval to %u ms", clamped);
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
        Serial.printf("[DEBUG] Updated MQTT minimum log level to %u\n", _mqttLogLevelInt); // Cannot use LOG_DEBUG here to avoid recursion (this is called in _initializeLogQueue)
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

    static void _saveSendGridDataEnabledToPreferences(bool enabled) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putBool(MQTT_PREFERENCES_SEND_GRID_DATA_KEY, enabled);
        if (bytesWritten == 0) LOG_ERROR("Failed to save send grid data enabled preference");

        prefs.end();
    }

    static void _saveMeterPublishThresholdBytesToPreferences(uint32_t bytes) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putUInt(MQTT_PREFERENCES_METER_PUBLISH_THRESHOLD_KEY, bytes);
        if (bytesWritten == 0) LOG_ERROR("Failed to save meter publish threshold preference");

        prefs.end();
    }

    static void _saveMeterPublishMaxIntervalMsToPreferences(uint32_t intervalMs) {
        Preferences prefs;

        prefs.begin(PREFERENCES_NAMESPACE_MQTT, false);
        size_t bytesWritten = prefs.putUInt(MQTT_PREFERENCES_METER_PUBLISH_INTERVAL_KEY, intervalMs);
        if (bytesWritten == 0) LOG_ERROR("Failed to save meter publish max interval preference");

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
        
        LOG_DEBUG("Starting MQTT task with %d bytes stack", MQTT_TASK_STACK_SIZE);

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
            TASK_HEARTBEAT(_mqttHeartbeat);

            bool connectedNow = false;
            if (CustomWifi::isFullyConnected()) {
                if (_clientMqtt.connected()) {
                    connectedNow = true;
                    _handleConnectedState();
                } else {
                    _handleConnecting();
                }
            }
            _lastConnectedState = connectedNow;

            // If we receive a signal to stop the task, we try to publish all the data and flushing the queues so we avoid losing data
            if (_lastLoopToPublishData) {
                _lastLoopToPublishData = false;
                _taskShouldRun = false;
            } else {
                // Wake early on notification (e.g. pushAlarm's requestImmediatePublish)
                // instead of waiting the full interval. Only the shutdown bit takes
                // the flush-and-stop branch below.
                uint32_t notifiedBits = 0;
                xTaskNotifyWait(0, ULONG_MAX, &notifiedBits, pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));

                if (notifiedBits & TASK_NOTIFY_SHUTDOWN_BIT) {
                    _lastLoopToPublishData = true;
                    _publishMqtt.meter = true;
                    _publishMqtt.systemDynamic = true;
                    _publishMqtt.statistics = true;
                    break;
                }
            }
        }

        _clientMqtt.disconnect();
        _lastConnectedState = false;
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
            "%s/%s/%s/%s/%s/%s/%s",
            MQTT_BASIC_INGEST,
            ruleName,
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            MQTT_TOPIC_VERSION,
            DEVICE_ID,
            finalTopic
        );

        LOG_DEBUG("Constructing MQTT topic with rule for %s | %s", finalTopic, topicBuffer);
    }

    static void _constructMqttTopic(const char* finalTopic, char* topicBuffer, size_t topicBufferSize) {
        snprintf(
            topicBuffer,
            topicBufferSize,
            "%s/%s/%s/%s/%s",
            MQTT_TOPIC_1,
            MQTT_TOPIC_2,
            MQTT_TOPIC_VERSION,
            DEVICE_ID,
            finalTopic
        );

        LOG_DEBUG("Constructing MQTT topic for %s | %s", finalTopic, topicBuffer);
    }

    // AWS IoT Commands execution topic: $aws/commands/things/<thing>/executions/
    // <executionId>/<verb>. executionId is "+" for the subscribe wildcard; verb is
    // "request/json" or "response/json".
    static void _constructCommandTopic(const char* executionId, const char* verb,
                                       char* topicBuffer, size_t topicBufferSize) {
        snprintf(topicBuffer, topicBufferSize, "%s/commands/things/%s/executions/%s/%s",
                 AWS_TOPIC, DEVICE_ID, executionId, verb);
    }

    static void _setupTopics() {
        _setTopicMeter();
        _setTopicGrid();
        _setTopicEnergy();
        _setTopicSystemDynamic();
        _setTopicStatistics();
        _setTopicCrash();
        _setTopicLog();
        _setTopicAlarm();

        LOG_DEBUG("MQTT topics setup complete");
    }

    static void _setTopicMeter() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_METER, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicGrid() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_GRID, MQTT_TOPIC_GRID, _mqttTopicGrid, sizeof(_mqttTopicGrid)); }
    static void _setTopicEnergy() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_ENERGY, MQTT_TOPIC_ENERGY, _mqttTopicEnergy, sizeof(_mqttTopicEnergy)); }
    static void _setTopicSystemDynamic() { _constructMqttTopic(MQTT_TOPIC_SYSTEM_DYNAMIC, _mqttTopicSystemDynamic, sizeof(_mqttTopicSystemDynamic)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }
    static void _setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash, sizeof(_mqttTopicCrash)); }
    static void _setTopicLog() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_LOG, MQTT_TOPIC_LOG, _mqttTopicLog, sizeof(_mqttTopicLog)); }
    static void _setTopicAlarm() { _constructMqttTopicWithRule(AWS_IOT_CORE_RULE_ALARM, MQTT_TOPIC_ALARM, _mqttTopicAlarm, sizeof(_mqttTopicAlarm)); }

    static void _subscribeToTopics() {
        _subscribeAwsIotJobs();
        // IoT Commands request topic (payload-format pinned to "json"; see
        // MQTT_IOT_COMMANDS_SUBSCRIBE_ENABLED). The earlier '+' at the payload-format
        // position was an unsupported reserved-topic subscribe -> broker CLIENT_ERROR
        // reconnect storm; the corrected topic is safe even before a dispatcher exists.
        if (MQTT_IOT_COMMANDS_SUBSCRIBE_ENABLED) _subscribeIotCommands();
        Shadow::onMqttConnected(); // subscribe shadow deltas + queue initial reports

        LOG_DEBUG("Subscribed to topics");
    }

    static void _subscribeIotCommands() {
        // AWS IoT Commands request topic. '+' is the execution-id wildcard only;
        // the payload-format segment must be a concrete value ("json"), NOT a '+'
        // wildcard. A '+' there is an unsupported reserved-topic subscribe and the
        // broker drops the whole session with CLIENT_ERROR (the ~1.2 s reconnect
        // storm we hit). The cloud dispatcher must create commands with contentType
        // application/json to match. Policy AllowSubscribe covers
        // $aws/commands/things/<thing>/*.
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructCommandTopic("+", "request/json", topic, sizeof(topic));
        if (_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            LOG_DEBUG("Subscribed to IoT Commands: %s", topic);
        } else {
            LOG_WARNING("Failed to subscribe to IoT Commands: %s", topic);
        }
    }

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

        if (Shadow::routeMessage(topic, message)) { /* handled by shadow module (copy + flag only) */ }
        // Only the subscribed request topic (.../executions/<id>/request/json) is an
        // inbound command. Match it precisely - matching any "/commands/things/" topic
        // also catches AWS's own .../response/rejected/json echo of our status publish,
        // which has no 'operation', gets re-rejected, and echoes again -> infinite
        // publish loop hammering the broker.
        else if (strstr(topic, "/commands/things/") != nullptr && endsWith(topic, "/request/json")) _queueCommand(topic, message);
        else if (strstr(topic, "/commands/things/") != nullptr) {
            // AWS echoed our own command-status publish back (e.g. rejecting a status
            // for an unknown/expired execution). Never reprocess it - just note it.
            LOG_DEBUG("Ignoring non-request command topic: %s", topic);
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_JOBS)) _handleAwsIotJobMessage(message, topic);
        else LOG_WARNING("Unknown MQTT topic received: %s", topic);
        
        // Clean up PSRAM allocation
        free(message);
    }

    // AWS IoT Commands (transient operations)
    // =======================================

    // reasonCode MUST match the AWS IoT Commands pattern [A-Z0-9_-]+ (uppercase);
    // a lowercase code makes AWS reject the status update. reasonDescription is free text.
    static void _publishCommandStatus(const char* executionId, const char* status,
                                       const char* reasonCode, const char* reasonDescription) {
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructCommandTopic(executionId, "response/json", topic, sizeof(topic));

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["status"] = status;
        if (reasonCode != nullptr) {
            doc["statusReason"]["reasonCode"] = reasonCode;
            if (reasonDescription != nullptr) doc["statusReason"]["reasonDescription"] = reasonDescription;
        }

        if (_publishJsonStreaming(doc, topic)) LOG_DEBUG("Command %s status '%s' published", executionId, status);
        else LOG_WARNING("Failed to publish command %s status '%s'", executionId, status);
    }

    // Parses the executionId from the topic, validates, dispatches the operation
    // and publishes IN_PROGRESS then a terminal status. Runs on the MQTT task
    // (from the RX callback or the dev injection drain) so publishing is safe.
    static void _handleCommandExecution(const char* topic, const char* message) {
        char executionId[NAME_BUFFER_SIZE];
        if (!ShadowLogic::extractExecutionId(topic, executionId, sizeof(executionId))) {
            LOG_WARNING("Could not extract executionId from command topic: %s", topic);
            return;
        }

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            LOG_ERROR("Failed to parse command %s payload (%s)", executionId, error.c_str());
            _publishCommandStatus(executionId, "FAILED", "BAD_PAYLOAD", "Could not parse command JSON");
            return;
        }

        const char* operation = doc["operation"].as<const char*>();
        if (operation == nullptr) {
            LOG_WARNING("Command %s missing 'operation'", executionId);
            _publishCommandStatus(executionId, "REJECTED", "MISSING_OPERATION", "No 'operation' field");
            return;
        }

        // Staleness guard. MQTT 3.1.1 carries no server timestamp, so this relies
        // on the command payload providing 'created_at' (unix seconds); absent it,
        // the guard is skipped (logged) - see the cloud contract in doc 07.
        uint64_t createdAt = doc["created_at"] | 0ULL;
        if (ShadowLogic::isCommandStale(createdAt, CustomTime::getUnixTime(), COMMAND_MAX_AGE_SECONDS)) {
            LOG_WARNING("Command %s (%s) is stale (created %llu); rejecting", executionId, operation, createdAt);
            _publishCommandStatus(executionId, "REJECTED", "STALE_COMMAND", "Command older than the staleness window");
            return;
        }
        if (createdAt == 0) LOG_DEBUG("Command %s has no created_at; staleness guard skipped", executionId);

        LOG_INFO("Processing command %s: %s", executionId, operation);
        _publishCommandStatus(executionId, "IN_PROGRESS", nullptr, nullptr);

        if (strcmp(operation, "restart") == 0) {
            _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
            delay(2000); // let the status flush before the reboot (same pattern as OTA)
            setRestartSystem("Command: restart");
        } else if (strcmp(operation, "factory_reset") == 0) {
            const char* confirm = doc["confirm"].as<const char*>();
            if (confirm == nullptr || strcmp(confirm, DEVICE_ID) != 0) {
                LOG_WARNING("Command %s factory_reset confirm mismatch", executionId);
                _publishCommandStatus(executionId, "REJECTED", "CONFIRM_MISMATCH", "confirm must equal the device id");
                return;
            }
            _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
            delay(2000);
            setRestartSystem("Command: factory_reset", true); // wipe user NVS, keep factory, reboot
        } else if (strcmp(operation, "energy_reset") == 0) {
            JsonVariantConst channels = doc["channels"];
            if (channels.is<const char*>() && strcmp(channels.as<const char*>(), "all") == 0) {
                Ade7953::resetEnergyValues();
                LOG_INFO("Command %s: reset energy for all channels", executionId);
            } else if (channels.is<const char*>()) {
                // AWS IoT Commands params are string-typed end-to-end, so this is the
                // only shape the cloud can actually deliver for a selective reset
                // (e.g. "5" or "0,2,5"). See ShadowLogic::parseChannelList.
                uint8_t indices[MAX_CHANNEL_COUNT];
                bool invalidTokenSeen = false;
                size_t count = ShadowLogic::parseChannelList(channels.as<const char*>(), globalHwProfile->totalChannelCount,
                                                               indices, MAX_CHANNEL_COUNT, &invalidTokenSeen);
                if (count == 0) {
                    _publishCommandStatus(executionId, "REJECTED", "BAD_CHANNELS", "channels string contained no valid channel index");
                    return;
                }
                if (invalidTokenSeen) LOG_WARNING("Command %s: invalid token(s) in energy_reset channels list", executionId);
                for (size_t i = 0; i < count; i++) Ade7953::resetChannelEnergyValues(indices[i]);
                LOG_INFO("Command %s: reset energy for listed channels", executionId);
            } else if (channels.is<JsonArrayConst>()) {
                for (JsonVariantConst ch : channels.as<JsonArrayConst>()) {
                    if (!ch.is<int>()) continue;
                    uint8_t idx = (uint8_t)ch.as<int>();
                    if (isChannelValid(idx)) Ade7953::resetChannelEnergyValues(idx);
                    else LOG_WARNING("Command %s: invalid channel %u in energy_reset", executionId, idx);
                }
                LOG_INFO("Command %s: reset energy for listed channels", executionId);
            } else {
                _publishCommandStatus(executionId, "REJECTED", "BAD_CHANNELS", "channels must be \"all\", a comma-separated index string, or an array of indices");
                return;
            }
            _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
        } else if (strcmp(operation, "issue_ack") == 0) {
            // Same two payload shapes as the local REST issue-ack endpoint
            // (customserver.cpp's ackIssueHandler): {"all": true} or
            // {"code": "...", "channel": <optional>}. AWS IoT Commands deliver
            // params string-typed end-to-end (same constraint as energy_reset's
            // channels above), so "all" and "channel" each also accept their
            // string-encoded form.
            JsonVariantConst all = doc["all"];
            bool ackAllRequested = (all.is<bool>() && all.as<bool>()) ||
                                   (all.is<const char*>() && strcmp(all.as<const char*>(), "true") == 0);
            if (ackAllRequested) {
                uint32_t ackedCount = IssueRegistry::ackAll();
                LOG_INFO("Command %s: acknowledged %lu issue(s)", executionId, (unsigned long)ackedCount);
                _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
            } else if (!doc["code"].is<const char*>()) {
                _publishCommandStatus(executionId, "REJECTED", "MISSING_CODE", "No 'code' (or 'all') field");
            } else {
                uint8_t channel = ISSUE_GLOBAL_SCOPE;
                JsonVariantConst channelField = doc["channel"];
                if (channelField.is<uint8_t>()) {
                    channel = channelField.as<uint8_t>();
                } else if (channelField.is<const char*>()) {
                    // Falls back to ISSUE_GLOBAL_SCOPE on an invalid/out-of-range
                    // digit string; a bogus channel then just misses in ack()
                    // below and reports NO_SUCH_ISSUE rather than misparsing.
                    ShadowLogic::parseChannelIndex(channelField.as<const char*>(), globalHwProfile->totalChannelCount, &channel);
                }
                if (IssueRegistry::ack(doc["code"].as<const char*>(), channel)) {
                    _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
                } else {
                    _publishCommandStatus(executionId, "REJECTED", "NO_SUCH_ISSUE", "Unknown issue code or no matching instance");
                }
            }
        } else if (strcmp(operation, "firmware_rollback") == 0) {
            // Boot the passive OTA partition without a download (#237). The command
            // must carry the 64-hex app_elf_sha256 it expects to find there: that
            // makes QoS1 redelivery idempotent (a duplicate matches the now-running
            // slot and no-ops) and refuses to boot a partial or unknown image. A
            // fingerprint, not a version: esp_app_desc_t.version is a frozen
            // arduino-lib-builder constant on this toolchain, identical in every build.
            const char* expectedSha = doc["expected_sha256"].is<const char*>()
                                          ? doc["expected_sha256"].as<const char*>()
                                          : nullptr;

            char runningSha[SHA256_HEX_BUFFER_SIZE] = {0};
            char passiveSha[SHA256_HEX_BUFFER_SIZE] = {0};
            getRunningPartitionSha256(runningSha, sizeof(runningSha));
            bool passiveReadable = getOtherPartitionSha256(passiveSha, sizeof(passiveSha));

            RollbackLogic::Decision decision = RollbackLogic::decide(expectedSha, passiveReadable, passiveSha, runningSha);
            if (decision == RollbackLogic::Decision::MISSING_SHA) {
                _publishCommandStatus(executionId, "REJECTED", "MISSING_SHA256", "expected_sha256 must be 64 hex chars");
            } else if (decision == RollbackLogic::Decision::NO_TARGET) {
                _publishCommandStatus(executionId, "REJECTED", "NO_ROLLBACK_TARGET", "Passive partition has no valid firmware");
            } else if (decision == RollbackLogic::Decision::MISMATCH) {
                _publishCommandStatus(executionId, "REJECTED", "TARGET_MISMATCH", "expected_sha256 matches neither partition");
            } else if (decision == RollbackLogic::Decision::NOOP_ALREADY_DONE) {
                // Already running the requested firmware (redelivery after a
                // completed rollback): success without touching the slots.
                LOG_INFO("Command %s: already running expected firmware, no-op", executionId);
                _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
            } else {
                FirmwareRollbackResult result = attemptFirmwareRollback("Command: firmware_rollback");
                if (result == FirmwareRollbackResult::SUCCESS) {
                    // Restart is scheduled but its task stops four other services
                    // before Mqtt::stop(), leaving time for this publish + flush.
                    _publishCommandStatus(executionId, "SUCCEEDED", nullptr, nullptr);
                    delay(2000); // let the status flush before the reboot (same pattern as restart)
                } else if (result == FirmwareRollbackResult::RESTART_BLOCKED) {
                    _publishCommandStatus(executionId, "FAILED", "ROLLBACK_FAILED", "Restart blocked; boot partition unchanged");
                } else {
                    _publishCommandStatus(executionId, "FAILED", "ROLLBACK_FAILED", "Passive image failed validation");
                }
            }
        } else {
            LOG_WARNING("Unknown command operation: %s", operation);
            _publishCommandStatus(executionId, "REJECTED", "UNKNOWN_OPERATION", operation);
        }
    }

    // Command handoff. Both the broker RX path (_subscribeCallback) and the dev
    // inject copy the request out + stash it here; the MQTT task body drains it
    // through _handleCommandExecution. So the apply (per-channel NVS writes, up to
    // ~1 s) and the status publishes run in the task body, NEVER inside the
    // PubSubClient callback - same rule as shadow deltas (#138). Publishing inside
    // the callback would also reuse PubSubClient's single buffer and corrupt the
    // QoS1 PUBACK it writes right after the callback returns. Single slot is safe:
    // loop() delivers one PUBLISH per call and we drain every iteration. Overwrite
    // is warned, never silent, so a dropped command is always visible.
    static char* _pendingCmdTopic = nullptr;
    static char* _pendingCmdPayload = nullptr;

    static void _queueCommand(const char* topic, const char* payload) {
        if (topic == nullptr || payload == nullptr) return;
        size_t tlen = strlen(topic) + 1, plen = strlen(payload) + 1;
        char* t = (char*)ps_malloc(tlen);
        char* p = (char*)ps_malloc(plen);
        if (t == nullptr || p == nullptr) {
            if (t) free(t);
            if (p) free(p);
            LOG_ERROR("Failed to allocate pending command buffers");
            return;
        }
        memcpy(t, topic, tlen);
        memcpy(p, payload, plen);

        if (acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            if (_pendingCmdTopic != nullptr || _pendingCmdPayload != nullptr) {
                LOG_WARNING("Overwriting an undrained pending command; previous one dropped");
                if (_pendingCmdTopic) free(_pendingCmdTopic);
                if (_pendingCmdPayload) free(_pendingCmdPayload);
            }
            _pendingCmdTopic = t;
            _pendingCmdPayload = p;
            releaseMutex(&_configMutex);
        } else {
            free(t);
            free(p);
            LOG_ERROR("Failed to acquire mutex queuing command");
        }
    }

    static void _drainPendingCommand() {
        char* t = nullptr;
        char* p = nullptr;
        if (acquireMutex(&_configMutex, CONFIG_MUTEX_TIMEOUT_MS)) {
            t = _pendingCmdTopic;
            _pendingCmdTopic = nullptr;
            p = _pendingCmdPayload;
            _pendingCmdPayload = nullptr;
            releaseMutex(&_configMutex);
        }
        if (t != nullptr && p != nullptr) _handleCommandExecution(t, p);
        if (t) free(t);
        if (p) free(p);
    }

#ifdef ENV_DEV
    // Dev-only synthetic command injection: build the real request topic and route
    // it through the same queue + task-body drain the broker path uses.
    void injectCommandExecution(const char* executionId, const char* payload) {
        if (executionId == nullptr || payload == nullptr) return;
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructCommandTopic(executionId, "request/json", topic, sizeof(topic));
        _queueCommand(topic, payload);
        LOG_INFO("Injected synthetic command execution %s", executionId);
    }
#endif

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

    static bool _performOtaUpdate() {
        LOG_DEBUG("Starting OTA update from URL: %.100s...", _otaCurrentUrl); // Truncate long URLs in logs

        WiFiClient testClient;
        // Extract the DNS to test from the URL
        char host[URL_BUFFER_SIZE]; // Small since we don't have all the presigned stuff
        if (extractHost(_otaCurrentUrl, host, sizeof(host))) {
            if (testClient.connect(host, 443, 3000)) { // Being HTTPS, the port is 443, and timeout
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

        // Perform OTA without validating the partition (allows rollback if new firmware crashes)
        esp_err_t result = esp_https_ota(&_otaConfig);

        if (result == ESP_OK) {
            LOG_INFO("OTA update downloaded successfully (not yet validated)");
            return true;
        } else {
            LOG_ERROR("OTA update failed with error: %s (%d)", esp_err_to_name(result), result);
            return false;
        }
    }
    
    static void _otaTask(void* parameter) {
        LOG_DEBUG("OTA task started");

        bool otaSuccess = _performOtaUpdate();

        if (otaSuccess) {
            LOG_INFO("OTA download successful for job %s. Preparing to reboot.", _otaCurrentJobId);
            
            // Get the update partition that was just written
            const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
            if (!update_partition) {
                LOG_ERROR("Failed to get update partition");
                _publishOtaStatus(_otaCurrentJobId, "FAILED", "partition_error");
                _otaTaskHandle = nullptr;
                vTaskDelete(nullptr);
                return;
            }
            
            // Read the app description from the new firmware to get its SHA256
            esp_app_desc_t new_app_desc;
            esp_err_t err = esp_ota_get_partition_description(update_partition, &new_app_desc);
            if (err != ESP_OK) {
                LOG_ERROR("Failed to get partition description: %s", esp_err_to_name(err));
                _publishOtaStatus(_otaCurrentJobId, "FAILED", "sha256_read_error");
                _otaTaskHandle = nullptr;
                vTaskDelete(nullptr);
                return;
            }
            
            // Convert SHA256 to hex string
            char sha256Hex[65];
            _sha256ToHex(new_app_desc.app_elf_sha256, sha256Hex);
            
            // Save OTA job ID, pending state, and expected SHA256 to Preferences
            Preferences prefs;  
            if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {  
                LOG_ERROR("Failed to save OTA pending state - aborting OTA");  
                _publishOtaStatus(_otaCurrentJobId, "FAILED", "preferences_error");  
                _otaTaskHandle = nullptr;  
                vTaskDelete(nullptr);  
                return;  
            }  
            prefs.putString(MQTT_PREFERENCES_OTA_JOB_ID_KEY, _otaCurrentJobId);
            prefs.putBool(MQTT_PREFERENCES_OTA_PENDING_KEY, true);
            prefs.putString(MQTT_PREFERENCES_OTA_EXPECTED_SHA256_KEY, sha256Hex);
            prefs.end();
            _otaRebootPending = true;
            LOG_DEBUG("Saved OTA pending state for job %s, expected SHA256: %s", _otaCurrentJobId, sha256Hex);

            // Publish IN_PROGRESS status (download complete, about to reboot)
            _publishOtaStatus(_otaCurrentJobId, "IN_PROGRESS", "rebooting");

            // Fresh image in the partition - give it its own rollback chance,
            // independent of whatever the previous firmware already consumed
            CrashMonitor::clearRollbackTried();

            // Give time for MQTT message to be sent - no need to rush to the restart
            delay(2000);

            setRestartSystem("OTA download complete, rebooting for validation");
        } else {
            LOG_ERROR("OTA download failed for job %s.", _otaCurrentJobId);
            
            // Publish failure status immediately
            _publishOtaStatus(_otaCurrentJobId, "FAILED", "download_failed");
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
        const char* targetVersion = doc["execution"]["jobDocument"]["firmware"]["version"].as<const char*>();
        // Read force strictly as a bool: ArduinoJson's as<bool>() returns true
        // for ANY non-null value regardless of type (v6.12+), so a job document
        // with force sent as the JSON string "false" would otherwise silently
        // bypass the guard below. is<bool>() rejects non-bool JSON types first.
        JsonVariant forceVariant = doc["execution"]["jobDocument"]["force"];
        bool force = forceVariant.is<bool>() ? forceVariant.as<bool>() : false;

        // This job is the one currently being validated post-reboot: its target
        // version now equals FIRMWARE_BUILD_VERSION, which would otherwise hit the
        // "already up to date" branch below and publish REJECTED - a terminal status
        // that then makes the validation task's later SUCCEEDED publish be refused
        // by AWS IoT Jobs (terminal states cannot transition again).
        if (_otaValidationTaskHandle != nullptr && strcmp(jobId, _otaCurrentJobId) == 0) {
            LOG_DEBUG("Job '%s' is the one currently being validated, skipping re-evaluation", jobId);
            return;
        }

        // Additional validation for operation type (validation function only checks existence)
        if (strcmp(operation, "ota_update") != 0) {
            LOG_WARNING("Job operation '%s' is not supported, rejecting job %s.", operation, jobId);
            _publishOtaStatus(jobId, "REJECTED", "unsupported_operation");
            return;
        }

        // Reject non-upgrade targets unless explicitly forced (a stale/orphaned CONTINUOUS
        // job could otherwise keep re-applying an old version to devices indefinitely)
        if (!force) {
            // A missing or non-string firmware.version must reject, not silently
            // proceed - guard-by-default is the point of this check, and `force`
            // is already the documented escape hatch for a version-less job.
            if (!targetVersion) {
                LOG_WARNING("Job '%s' has no valid firmware.version and force is not set, rejecting.", jobId);
                _publishOtaStatus(jobId, "REJECTED", "invalid_version");
                return;
            }
            int comparison = compareVersions(FIRMWARE_BUILD_VERSION, targetVersion);
            if (comparison > 0) {
                LOG_WARNING("Job '%s' targets older version '%s' (current '%s'), rejecting.", jobId, targetVersion, FIRMWARE_BUILD_VERSION);
                _publishOtaStatus(jobId, "REJECTED", "downgrade_not_allowed");
                return;
            }
            if (comparison == 0) {
                LOG_INFO("Job '%s' targets current version '%s', rejecting as already up to date.", jobId, targetVersion);
                _publishOtaStatus(jobId, "REJECTED", "already_up_to_date");
                return;
            }
        }

        // Check if there is already a OTA job being validated (no need to check the ID since only one can be active at a time)
        // If so, skip it to prevent download loop during validation period
        if (_otaValidationTaskHandle != nullptr) {
            LOG_DEBUG("Skipping OTA job '%s' - already being validated", jobId);
            return;
        }

        // Check if there is already an OTA job being downloaded
        if (_otaTaskHandle != nullptr) {
            LOG_DEBUG("Skipping OTA job '%s' - download already in progress", jobId);
            return;
        }

        // Check if a successful download is awaiting reboot for validation.
        // Between download success and reboot, MQTT may reconnect and the job still
        // shows IN_PROGRESS on AWS, which would otherwise trigger a second download.
        if (_otaRebootPending) {
            LOG_DEBUG("Skipping OTA job '%s' - reboot pending after successful download", jobId);
            return;
        }

        LOG_INFO("Received OTA Job '%s'. Firmware URL length: %d", jobId, strlen(url));

        // Acknowledge the job and set status to IN_PROGRESS
        _publishOtaStatus(jobId, "IN_PROGRESS", "downloading");

        // Save in the static variables to ensure we don't have any dangling pointers
        snprintf(_otaCurrentUrl, OTA_PRESIGNED_URL_BUFFER_SIZE, "%s", url);
        snprintf(_otaCurrentJobId, sizeof(_otaCurrentJobId), "%s", jobId);

        LOG_DEBUG("Starting OTA task with %d bytes stack", OTA_TASK_STACK_SIZE);

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
        // Power-only topic: nothing to publish unless the queue has entries
        // and power data is enabled (voltage/energy moved to _publishEnergy()).
        UBaseType_t queueSize = _meterQueue ? uxQueueMessagesWaiting(_meterQueue) : 0;

        if (queueSize == 0 || !_sendPowerDataEnabled) {
            LOG_VERBOSE("No valid meter data to publish");
            // Clear the flag and reset the timer here too, not just on a real
            // publish - otherwise the max-interval trigger in
            // _checkIfPublishMeterNeeded() re-sets this flag every loop tick
            // (100 ms) forever whenever there's nothing to send (power data
            // disabled, or queue still empty), spinning instead of going quiet.
            _publishMqtt.meter = false;
            _lastMillisMeterPublished = millis64();
            return;
        }

        // Any error is already logged in _publishMeterJson()
        if (_publishMeterJson()) _lastMillisMeterPublished = millis64();
    }
    
    static void _publishGrid() {
        if (_gridQueue == nullptr || uxQueueMessagesWaiting(_gridQueue) == 0) return;

        // Bare top-level array of [t, f, v] triplets (cross-repo contract v1),
        // FIFO-drained so the array is sorted; gaps stay explicit
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        JsonArray points = doc.to<JsonArray>();

        PayloadGridPoint point;
        uint32_t loops = 0;
        while (xQueueReceive(_gridQueue, &point, 0) == pdTRUE && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            JsonArray triplet = points.add<JsonArray>();
            triplet.add(point.unixTimeMs);
            triplet.add(roundToDecimals(point.frequency, MQTT_GRID_FREQUENCY_PAYLOAD_DECIMALS));
            triplet.add(roundToDecimals(point.voltage, MQTT_GRID_VOLTAGE_PAYLOAD_DECIMALS));

            if (measureJson(doc) > AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER) break; // Remainder ships at the next aligned boundary
        }

        if (_publishJsonStreaming(doc, _mqttTopicGrid)) {
            _publishMqtt.grid = false;
            _nextGridPublishUnixSecond = MqttGridSchedule::nextAlignedBoundarySeconds(CustomTime::getUnixTime(), MQTT_GRID_PUBLISH_ALIGN_SECONDS);
        }
    }

    static void _publishEnergy() {
        // Bare array of self-contained objects (cross-repo contract v1): one
        // voltage object, then one energy object per active channel with valid
        // measurements. Unconditional - independent of _sendPowerDataEnabled.
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        MeterValues meterValuesZeroChannel;
        if (Ade7953::getMeterValues(meterValuesZeroChannel, 0) && meterValuesZeroChannel.lastUnixTimeMilliseconds > 0) {
            JsonObject voltageObj = doc.add<JsonObject>();
            voltageObj["unixTime"] = meterValuesZeroChannel.lastUnixTimeMilliseconds;
            voltageObj["voltage"] = meterValuesZeroChannel.voltage;
        }

        for (uint8_t i = 0; i < globalHwProfile->totalChannelCount; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                MeterValues meterValues;

                if (!Ade7953::getMeterValues(meterValues, i)) {
                    LOG_DEBUG("Failed to get meter values for channel %d. Skipping for energy publishing", i);
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

        if (doc.size() == 0) {
            LOG_DEBUG("No energy data available for publishing");
            // Clear the flag and advance the boundary anyway - otherwise, with
            // no active channels/voltage yet (e.g. right after boot), this
            // would re-fire every loop tick (100 ms) instead of waiting for
            // the next minute boundary. Unlike grid, energy has no queue-size
            // guard to prevent the flag from being set with nothing to send.
            _publishMqtt.energy = false;
            _nextEnergyPublishUnixSecond = MqttGridSchedule::nextAlignedBoundarySeconds(CustomTime::getUnixTime(), MQTT_ENERGY_PUBLISH_ALIGN_SECONDS);
            return;
        }

        if (_publishJsonStreaming(doc, _mqttTopicEnergy)) {
            _publishMqtt.energy = false;
            _nextEnergyPublishUnixSecond = MqttGridSchedule::nextAlignedBoundarySeconds(CustomTime::getUnixTime(), MQTT_ENERGY_PUBLISH_ALIGN_SECONDS);
        }
    }

    static void _publishSystemDynamic() {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        SpiRamAllocator allocatorSystemDynamic;
        JsonDocument docSystemDynamic(&allocatorSystemDynamic);
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

    // One record per cycle. A backlog of archived crashes is a few hundred kB of
    // back-to-back publishes, so draining across cycles lets meter and log
    // traffic still get a turn. The flag is only ever cleared here; re-arming is
    // _checkIfPublishCrashNeeded()'s job, so a failure costs one interval rather
    // than every crash publish until the next reboot.
    static void _publishCrash() {
        uint32_t index = 0;

        for (uint32_t attempt = 0; attempt < CRASH_PUBLISH_MAX_ATTEMPTS_PER_CYCLE; attempt++) {
            switch (_publishCrashJson(index)) {
                case CrashPublishOutcome::Published:
                    _lastMillisCrashPublished = millis64();
                    _publishMqtt.crash = false;
                    return;

                case CrashPublishOutcome::Empty:
                    // Clock advanced even though nothing was sent: an archive
                    // holding only Skip records still reports hasArchivedCrash(),
                    // so _checkIfPublishCrashNeeded() would re-arm on the next
                    // 100 ms tick and rescan the whole archive forever.
                    _lastMillisCrashPublished = millis64();
                    _publishMqtt.crash = false;
                    return;

                case CrashPublishOutcome::Retry:
                    LOG_ERROR("Failed to publish crash data, retrying on a later cycle");
                    _lastMillisCrashPublished = millis64();
                    _publishMqtt.crash = false;
                    return;

                case CrashPublishOutcome::Skip:
                    index++; // Record left in place, so step past it
                    break;

                case CrashPublishOutcome::Dropped:
                    break; // Removed, so the same index now holds the next record
            }
        }

        LOG_WARNING("Gave up on this crash publish cycle after %d records that could not be sent",
                    CRASH_PUBLISH_MAX_ATTEMPTS_PER_CYCLE);
        _lastMillisCrashPublished = millis64();
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

    // Fast path meant to beat the capacitor hold-up window: published directly,
    // not through Shadow. Power/pf/voltage snapshot fetched here (not carried in
    // AlarmEntry) to stay fresh and keep the queued struct tiny.
    static void _publishAlarm(const AlarmEntry& entry)
    {
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        doc["eventId"] = entry.eventId;
        doc["unixTime"] = entry.unixTimeMs;
        doc["type"] = entry.type;

        MeterValues voltageValues;
        if (Ade7953::getMeterValues(voltageValues, 0)) {
            doc["voltage"] = roundToDecimals(voltageValues.voltage, MQTT_GRID_VOLTAGE_PAYLOAD_DECIMALS);
        }

        JsonArray channels = doc["channels"].to<JsonArray>();
        for (uint8_t i = 0; i < globalHwProfile->totalChannelCount; i++) {
            if (!Ade7953::isChannelActive(i)) continue;
            MeterValues meterValues;
            if (!Ade7953::getMeterValues(meterValues, i)) continue;
            JsonObject channel = channels.add<JsonObject>();
            channel["channel"] = i;
            channel["power"] = roundToDecimals(meterValues.activePower, POWER_DECIMALS);
            channel["pf"] = roundToDecimals(meterValues.powerFactor, POWER_FACTOR_DECIMALS);
        }

        if (!_publishJsonStreaming(doc, _mqttTopicAlarm)) {
            LOG_ERROR("Failed to publish alarm via streaming");
        }
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.grid) {_publishGrid();}
        if (_publishMqtt.energy) {_publishEnergy();}
        if (_publishMqtt.systemDynamic) {_publishSystemDynamic();}
        if (_publishMqtt.statistics) {_publishStatistics();}
        if (_publishMqtt.crash) {_publishCrash();}
        if (_publishMqtt.requestOta) {_publishOtaJobsRequest();}
    }

    static void _checkIfPublishMeterNeeded() {
        UBaseType_t queueSize = _meterQueue ? uxQueueMessagesWaiting(_meterQueue) : 0;
        size_t estimatedJsonSize = queueSize * MQTT_METER_ESTIMATED_PER_ENTRY;

        // Queue capacity in entries; used to force a publish before pushMeter()'s
        // ring buffer starts silently dropping the oldest queued entry
        UBaseType_t meterQueueCapacity = MQTT_METER_QUEUE_SIZE / sizeof(PayloadMeter);
        bool queueAlmostFull = queueSize >= (UBaseType_t)(meterQueueCapacity * MQTT_METER_QUEUE_ALMOST_FULL_RATIO);

        // Publish (power-only payload) if power data is enabled AND either:
        // 1. Queue reaches the byte threshold (at least the billable size for efficiency), OR
        // 2. The queue is close to capacity, regardless of threshold (avoid silently dropping entries)
        // OR, independent of power data being enabled:
        // 3. Enough time has passed since the last publish (flush whatever is queued)
        // Real JSON size is checked during _publishMeterStreaming() with measureJson()
        // It is better to underestimate here (thus, the real payload will be more than 5kB) so we use all of the billable size
        // and only "risk" losing a few entries, which will just be published on the next cycle
        // Threshold and max interval are shadow-configurable (system.meter_publish_threshold_bytes / max_interval_ms)
        if (
            (((estimatedJsonSize >= _meterPublishThresholdBytes) || queueAlmostFull) && _sendPowerDataEnabled) ||
            ((millis64() - _lastMillisMeterPublished) > _meterPublishMaxIntervalMs)
        ) {
            _publishMqtt.meter = true;
            LOG_DEBUG("Set flag to publish meter data (queue: %u/%u entries, real size checked during publish)", queueSize, meterQueueCapacity);
        }
    }

    static void _checkIfPublishGridNeeded() {
        UBaseType_t queueSize = _gridQueue ? uxQueueMessagesWaiting(_gridQueue) : 0;
        if (queueSize == 0) return;

        uint64_t nowUnixSecond = CustomTime::getUnixTime();

        // First check since (re)connect: schedule the next aligned boundary
        // rather than publishing immediately, so even the first grid publish
        // lands on wall-clock alignment.
        if (_nextGridPublishUnixSecond == 0) {
            _nextGridPublishUnixSecond = MqttGridSchedule::nextAlignedBoundarySeconds(nowUnixSecond, MQTT_GRID_PUBLISH_ALIGN_SECONDS);
            return;
        }

        // Wall-clock-aligned, not a relative interval: `>=` (not `==`) so a
        // delayed loop tick still catches the boundary instead of missing it
        if (nowUnixSecond >= _nextGridPublishUnixSecond) {
            _publishMqtt.grid = true;
            LOG_DEBUG("Set flag to publish grid data (queue: %u points)", queueSize);
        }
    }

    // True once every active channel with valid measurements (plus the
    // base-phase voltage read, channel 0) has been serviced at or after the
    // given boundary - i.e. no point in the snapshot would actually be
    // leftover data from before the boundary. Channels are read round-robin
    // over a shared mux (WDRR-scheduled), so this can take a few seconds to
    // become true; the caller caps the wait with a deadline.
    static bool _allActiveChannelsFreshSinceBoundary(uint64_t boundaryUnixMs) {
        MeterValues meterValuesZeroChannel;
        if (!Ade7953::getMeterValues(meterValuesZeroChannel, 0) ||
            meterValuesZeroChannel.lastUnixTimeMilliseconds < boundaryUnixMs) {
            return false;
        }

        for (uint8_t i = 0; i < globalHwProfile->totalChannelCount; i++) {
            if (Ade7953::isChannelActive(i) && Ade7953::hasChannelValidMeasurements(i)) {
                MeterValues meterValues;
                if (!Ade7953::getMeterValues(meterValues, i) ||
                    meterValues.lastUnixTimeMilliseconds < boundaryUnixMs) {
                    return false;
                }
            }
        }
        return true;
    }

    static void _checkIfPublishEnergyNeeded() {
        uint64_t nowUnixSecond = CustomTime::getUnixTime();

        // First check since (re)connect: schedule the next aligned boundary
        // rather than publishing immediately, so even the first energy publish
        // lands on wall-clock alignment.
        if (_nextEnergyPublishUnixSecond == 0) {
            _nextEnergyPublishUnixSecond = MqttGridSchedule::nextAlignedBoundarySeconds(nowUnixSecond, MQTT_ENERGY_PUBLISH_ALIGN_SECONDS);
            return;
        }

        // Once the boundary is reached, hold the publish until every channel
        // has crossed it too (so the snapshot can't mix a fresh reading with
        // one that's actually leftover from before the boundary), capped by
        // a deadline so one starved channel can't block the topic forever.
        bool allFresh = nowUnixSecond >= _nextEnergyPublishUnixSecond &&
                         _allActiveChannelsFreshSinceBoundary(_nextEnergyPublishUnixSecond * 1000ULL);
        if (MqttEnergyPublishGate::shouldPublishNow(nowUnixSecond, _nextEnergyPublishUnixSecond,
                                                     MQTT_ENERGY_PUBLISH_DEADLINE_SECONDS, allFresh)) {
            _publishMqtt.energy = true;
            LOG_DEBUG("Set flag to publish energy data");
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

    // The interval is checked before the archive, since hasArchivedCrash() walks
    // a LittleFS directory and this runs on every MQTT loop tick.
    static void _checkIfPublishCrashNeeded() {
        if ((millis64() - _lastMillisCrashPublished) <= MQTT_MAX_INTERVAL_CRASH_PUBLISH) return;

        if (CrashMonitor::hasArchivedCrash()) {
            _publishMqtt.crash = true;
            LOG_DEBUG("Set flag to publish crash");
        } else {
            // Nothing pending: reset the clock so an idle device is not walking
            // the directory on every single tick
            _lastMillisCrashPublished = millis64();
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
            _publishMqtt.systemDynamic = true;
            _publishMqtt.statistics = true;
            _publishMqtt.requestOta = true;

            return true;
        } else {
            int32_t currentState = _clientMqtt.state();
            _mqttConnectionAttempt++;
            statistics.mqttConnectionErrors++;

            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                LOG_ERROR("MQTT connection failed due to authorization error (%s, %d). Stopping MQTT task; check factory certificates",
                          _getMqttStateReason(currentState), currentState);
                _taskShouldRun = false; // Signal the task loop to exit (cannot call _stopTask from inside the task: it would deadlock)
                return false;
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
            char backoffHuman[DURATION_FORMAT_BUFFER_SIZE];
            DurationFormat::humanizeDuration(backoffDelay, backoffHuman, sizeof(backoffHuman));
            LOG_WARNING("Failed to connect to MQTT (attempt %lu). Reason: %s. Next attempt in %s", _mqttConnectionAttempt, _getMqttStateReason(currentState), backoffHuman);

            return false;
        }
    }

    static bool _setCertificatesFromPreferences() {
        if (_awsIotCoreCert == nullptr || _awsIotCorePrivateKey == nullptr) {
            LOG_ERROR("Certificate buffers not allocated");
            return false;
        }

        memset(_awsIotCoreCert, 0, CERTIFICATE_BUFFER_SIZE);
        memset(_awsIotCorePrivateKey, 0, CERTIFICATE_BUFFER_SIZE);

        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
            LOG_ERROR("Failed to open factory namespace");
            return false;
        }

        size_t maxLenCertRetrieval = CERTIFICATE_BUFFER_SIZE - 1; // Leave space for null terminator
        preferences.getString(FACTORY_KEY_CERT_PEM, _awsIotCoreCert, maxLenCertRetrieval);
        preferences.getString(FACTORY_KEY_KEY_PEM, _awsIotCorePrivateKey, maxLenCertRetrieval);
        preferences.end();

        if (strlen(_awsIotCoreCert) == 0 || strlen(_awsIotCorePrivateKey) == 0 ||
            !_validateCertificateFormat(_awsIotCoreCert, "device cert") ||
            !_validateCertificateFormat(_awsIotCorePrivateKey, "private key")) {
            LOG_ERROR("Invalid device certificates in factory namespace");
            _clearCertificatesRuntime();
            return false;
        }

        LOG_DEBUG("Certificates loaded from factory namespace");
        return true;
    }

    static bool _publishJsonStreaming(JsonDocument &jsonDocument, const char* topic, bool retain) {
        if (topic == nullptr) {
            LOG_WARNING("Null topic provided");
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!CustomWifi::isFullyConnected()) { // No need to check for internet since connected() will do it anyway
            LOG_WARNING("WiFi not connected. Skipping streaming publish on %s", topic);
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

    // Drained ahead of the log queue in _handleConnectedState().
    static void _processAlarmQueue()
    {
        if (!_initializeAlarmQueue()) return;

        AlarmEntry entry;
        uint32_t loops = 0;
        while (xQueueReceive(_alarmQueue, &entry, 0) == pdTRUE && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            if (CustomWifi::isFullyConnected() && _clientMqtt.connected()) {
                _publishAlarm(entry);
            } else {
                xQueueSendToFront(_alarmQueue, &entry, 0);
                break;
            }
        }
    }

    static bool _publishMeterStreaming() {
        // Power-only: bare array of [unixTimeMs, channel, activePower, powerFactor]
        // points. Voltage and per-channel energy counters are published
        // separately on the dedicated energy topic (see _publishEnergy()).
        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);

        // Only add power data points if sendPowerDataEnabled is true and connected
        // CRITICAL: Check connectivity BEFORE dequeuing to prevent data loss
        uint32_t entriesAdded = 0;
        if (
            _sendPowerDataEnabled && // Send only if the send power data flag is enabled (to save on data)
            _initializeMeterQueue() && 
            // Ensure connectivity again!
            CustomWifi::isFullyConnected() &&  // Fail fast
            _clientMqtt.connected()
        ) {
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

                // Check if we're approaching the minimum billable size (optimize costs by staying just under)
                if (measureJson(doc) > AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER) {
                    LOG_DEBUG(
                        "Meter data JSON approaching billable threshold (%u bytes, max %u), stopping queue processing (missing %d points)",
                        measureJson(doc), AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE, uxQueueMessagesWaiting(_meterQueue)
                    );
                    break; // Remaining entries will be sent in the next publish
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

    // Publishes one archived crash as a single message: it either lands whole or
    // does not land at all. The chunked sequence this replaced left permanently
    // incomplete records behind whenever it failed part-way.
    static CrashPublishOutcome _publishCrashJson(uint32_t index) {
        char baseName[CRASH_ARCHIVE_NAME_BUFFER_SIZE];
        if (!CrashMonitor::getArchivedCrashAt(index, baseName, sizeof(baseName))) {
            LOG_DEBUG("No archived crash pending publication at index %lu", index);
            return CrashPublishOutcome::Empty;
        }

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        if (!CrashMonitor::getArchivedCrashMetadata(baseName, doc)) {
            // Unparseable metadata will never become publishable, and leaving it
            // in place would block every later record behind it
            LOG_ERROR("Dropping archived crash %s: its metadata could not be read", baseName);
            CrashMonitor::removeArchivedCrash(baseName);
            return CrashPublishOutcome::Dropped;
        }

        size_t compressedSize = CrashMonitor::getArchivedCrashDumpSize(baseName);
        if (compressedSize == 0) {
            LOG_ERROR("Dropping archived crash %s: its core dump is missing or empty", baseName);
            CrashMonitor::removeArchivedCrash(baseName);
            return CrashPublishOutcome::Dropped;
        }

        doc["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        doc["coreDumpEncoding"] = "gzip+base64";

        // Checked before spending PSRAM on the encode. See fitsPublishLimit() for
        // where the ceiling comes from and why overshooting it is not benign.
        size_t topicLength = strlen(_mqttTopicCrash);
        size_t metadataBytes = measureJson(doc) + CRASH_PUBLISH_COREDUMP_FIELD_OVERHEAD;
        if (!CrashArchivePolicy::fitsPublishLimit(compressedSize, metadataBytes, topicLength)) {
            LOG_ERROR(
                "Archived crash %s does not fit one publish: %zu compressed bytes plus %zu bytes of metadata exceed the %zu byte payload limit. Record kept on flash for retrieval over the local API.",
                baseName, compressedSize, metadataBytes, CrashArchivePolicy::maxPublishPayloadBytes(topicLength)
            );
            // Never publishable, but still worth keeping on flash. Stepped over
            // rather than retried, otherwise it stays the oldest record forever
            // and no later crash ever reaches the cloud.
            return CrashPublishOutcome::Skip;
        }

        uint8_t* compressed = (uint8_t*)ps_malloc(compressedSize);
        if (compressed == nullptr) {
            LOG_ERROR("Failed to allocate %zu bytes in PSRAM for the archived core dump", compressedSize);
            return CrashPublishOutcome::Retry;
        }

        size_t readSize = 0;
        if (!CrashMonitor::readArchivedCrashDump(baseName, compressed, compressedSize, &readSize) || readSize == 0) {
            LOG_ERROR("Failed to read the archived core dump for %s", baseName);
            free(compressed);
            return CrashPublishOutcome::Retry;
        }

        size_t encodedBufferSize = CrashArchivePolicy::base64EncodedSize(readSize) + 1; // Null terminator
        uint8_t* encoded = (uint8_t*)ps_malloc(encodedBufferSize);
        if (encoded == nullptr) {
            LOG_ERROR("Failed to allocate %zu bytes in PSRAM for the base64 core dump", encodedBufferSize);
            free(compressed);
            return CrashPublishOutcome::Retry;
        }

        size_t encodedLength = 0;
        int32_t ret = mbedtls_base64_encode(encoded, encodedBufferSize, &encodedLength, compressed, readSize);
        free(compressed);

        if (ret != 0) {
            LOG_ERROR("Base64 encoding failed for crash %s (mbedtls: %d)", baseName, ret);
            free(encoded);
            return CrashPublishOutcome::Retry;
        }
        encoded[encodedLength] = '\0';

        // Linked, not copied: the document holds the pointer, so `encoded` has to
        // outlive serialization and must not be freed before the publish returns
        doc["coreDump"] = JsonString((const char*)encoded, encodedLength, true);

        bool published = _publishJsonStreaming(doc, _mqttTopicCrash);
        free(encoded);

        if (!published) {
            LOG_ERROR("Failed to publish archived crash %s, keeping it on flash to retry", baseName);
            return CrashPublishOutcome::Retry;
        }

        LOG_DEBUG("Published crash %s in a single message (%zu bytes of gzipped dump, %zu bytes encoded)",
                 baseName, readSize, encodedLength);

        // Only remove the crash in production builds; in dev we might want to inspect locally also
        // NOTE: this means that all crashes will be continously published via MQTT at each boot; 
        // given they have the same ID, this should not cause problems
        #ifndef ENV_DEV
        CrashMonitor::removeArchivedCrash(baseName);
        #endif
        return CrashPublishOutcome::Published;
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

    static void _clearCertificatesRuntime() {
        if (_awsIotCoreCert) memset(_awsIotCoreCert, 0, CERTIFICATE_BUFFER_SIZE);
        if (_awsIotCorePrivateKey) memset(_awsIotCorePrivateKey, 0, CERTIFICATE_BUFFER_SIZE);
        LOG_INFO("In-memory certificates cleared");
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

    // Connection handling
    // ===================

    static void _handleConnecting() {
        // Wait for time sync before attempting connection to avoid LWIP lock conflicts
        if (!CustomTime::isTimeSynched()) {
            delay(5000);
            LOG_DEBUG("Waiting for time sync before MQTT connection");
            return;
        }

        if (millis64() >= _nextMqttConnectionAttemptMillis) {
            // Small delay to allow LWIP/SNTP operations to complete
            delay(100);
            if (CustomWifi::isFullyConnected(true)) _connectMqtt();
        }
    }

    static void _handleConnectedState() {
        // MQTT connection check is sufficient - if TCP to AWS fails, we'll detect it here
        // Use vars explicitly here so later in the logs they have the same exact values
        bool wifiOk = CustomWifi::isFullyConnected();
        bool mqttConnected = _clientMqtt.connected();
        bool mqttLoopOk = _clientMqtt.loop(); // Also process incoming messages with loop()

        if (!wifiOk || !mqttConnected || !mqttLoopOk) {
            LOG_DEBUG(
                "MQTT disconnected, will reconnect next loop (wifi: %d, connected: %d, loop: %d)",
                wifiOk, mqttConnected, mqttLoopOk
            );
            statistics.mqttConnectionErrors++;
            return;
        }

        // Process queues and publishing. Alarm goes first, ahead of routine logs.
        _processAlarmQueue();
        _processLogQueue();
        _checkIfPublishMeterNeeded();
        _checkIfPublishGridNeeded();
        _checkIfPublishEnergyNeeded();
        _checkIfPublishSystemDynamicNeeded();
        _checkIfPublishStatisticsNeeded();
        _checkIfPublishCrashNeeded();
        _checkPublishMqtt();
        Shadow::checkPublish(); // drain shadow deltas/local-edits/reports (MQTT task body)
        _drainPendingCommand(); // process queued IoT Command (broker RX or dev inject) off the callback
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

    static void _sha256ToHex(const uint8_t sha256[32], char hexOut[65]) {
        for (int i = 0; i < 32; i++) {
            snprintf(&hexOut[i*2], 3, "%02x", sha256[i]);
        }
        hexOut[64] = '\0';
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

    // OTA validation functions
    // ========================

    static void _clearOtaPendingState() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            LOG_ERROR("Failed to clear OTA pending state");
            return;
        }
        prefs.remove(MQTT_PREFERENCES_OTA_PENDING_KEY);
        prefs.remove(MQTT_PREFERENCES_OTA_JOB_ID_KEY);
        prefs.remove(MQTT_PREFERENCES_OTA_EXPECTED_SHA256_KEY);
        prefs.end();
    }

    static void _publishOtaStatus(const char* jobId, const char* status, const char* reason) {
        if (!jobId || !status || !reason) return;

        char partialTopic[MQTT_TOPIC_BUFFER_SIZE];
        snprintf(partialTopic, sizeof(partialTopic), "jobs/%s/update", jobId);

        char fullTopic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopicReservedThings(partialTopic, fullTopic, sizeof(fullTopic));

        SpiRamAllocator allocator;
        JsonDocument doc(&allocator);
        doc["status"] = status;
        doc["statusDetails"]["reason"] = reason;

        if (_publishJsonStreaming(doc, fullTopic)) {
            LOG_DEBUG("Published OTA status '%s' for job %s", status, jobId);
        } else {
            LOG_ERROR("Failed to publish OTA status '%s' for job %s", status, jobId);
        }
    }

    static void _checkPendingOtaValidation() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            LOG_ERROR("Failed to open preferences for OTA validation check");
            return;
        }

        bool otaPending = prefs.getBool(MQTT_PREFERENCES_OTA_PENDING_KEY, false);
        char jobId[NAME_BUFFER_SIZE];
        memset(jobId, 0, sizeof(jobId));
        prefs.getString(MQTT_PREFERENCES_OTA_JOB_ID_KEY, jobId, sizeof(jobId));
        prefs.end();

        if (!otaPending || strlen(jobId) == 0) {
            LOG_DEBUG("No pending OTA validation");
            return;
        }

        LOG_INFO("Detected pending OTA validation for job %s", jobId);
        
        // Copy job ID to global variable for validation task
        snprintf(_otaCurrentJobId, sizeof(_otaCurrentJobId), "%s", jobId);

        // Create validation task to monitor stability
        LOG_DEBUG("Starting OTA validation task with %d bytes stack", OTA_VALIDATION_TASK_STACK_SIZE);
        
        BaseType_t result = xTaskCreate(
            _otaValidationTask,
            OTA_VALIDATION_TASK_NAME,
            OTA_VALIDATION_TASK_STACK_SIZE,
            nullptr,
            OTA_VALIDATION_TASK_PRIORITY,
            &_otaValidationTaskHandle);

        if (result != pdPASS) {
            LOG_ERROR("Failed to create OTA validation task");
            _otaValidationTaskHandle = nullptr;
            _clearOtaPendingState();
        }
    }

    static void _otaValidationTask(void* parameter) {
        LOG_INFO("OTA validation task started - monitoring stability for %d seconds", OTA_VALIDATION_TIMEOUT / 1000);
        
        uint64_t validationStartTime = millis64();
        const esp_partition_t* boot_partition = esp_ota_get_boot_partition();
        
        if (!boot_partition) {
            LOG_ERROR("Failed to get boot partition for OTA validation");
            _clearOtaPendingState();
            _otaValidationTaskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }

        // Monitor for stability period (here we just wait, rollback will occur automatically on failure)
        while (millis64() - validationStartTime < OTA_VALIDATION_TIMEOUT) {
            char otaRemainingHuman[DURATION_FORMAT_BUFFER_SIZE];
            DurationFormat::humanizeDuration(OTA_VALIDATION_TIMEOUT + validationStartTime - millis64(), otaRemainingHuman, sizeof(otaRemainingHuman));
            LOG_DEBUG("OTA validation in progress - %s remaining", otaRemainingHuman);
            delay(OTA_VALIDATION_CHECK_INTERVAL);
        }

        // If we reached here, the firmware is stable - verify SHA256 before marking as valid
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            LOG_ERROR("Failed to open preferences for SHA256 validation");
            _clearOtaPendingState();
            _otaValidationTaskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        
        char expectedSha256[65];
        memset(expectedSha256, 0, sizeof(expectedSha256));
        prefs.getString(MQTT_PREFERENCES_OTA_EXPECTED_SHA256_KEY, expectedSha256, sizeof(expectedSha256));
        prefs.end();
        
        if (strlen(expectedSha256) != 64) {
            LOG_ERROR("Invalid expected SHA256 in preferences (length: %d)", strlen(expectedSha256));
            _clearOtaPendingState();
            _otaValidationTaskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        
        // Get current running partition's SHA256
        esp_app_desc_t current_app_desc;
        esp_err_t err = esp_ota_get_partition_description(boot_partition, &current_app_desc);
        if (err != ESP_OK) {
            LOG_ERROR("Failed to get current partition description: %s", esp_err_to_name(err));
            _clearOtaPendingState();
            _otaValidationTaskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        
        // Convert current SHA256 to hex string
        char currentSha256[65];
        _sha256ToHex(current_app_desc.app_elf_sha256, currentSha256);
        
        // Compare SHA256 hashes
        if (strcmp(expectedSha256, currentSha256) != 0) {
            LOG_ERROR("OTA validation failed - SHA256 mismatch (expected: %s, current: %s) - firmware rolled back", expectedSha256, currentSha256);
            _publishOtaStatus(_otaCurrentJobId, "FAILED", "sha256_mismatch_firmware_rollback");
            _clearOtaPendingState();
            _otaValidationTaskHandle = nullptr;
            vTaskDelete(nullptr);
            return;
        }
        
        LOG_INFO("OTA validation successful - SHA256 verified: %s", currentSha256);
        _publishOtaStatus(_otaCurrentJobId, "SUCCEEDED", "validated after successful boot and stability period");
        _clearOtaPendingState();
        LOG_INFO("OTA update completed and validated successfully");

        _otaValidationTaskHandle = nullptr;
        vTaskDelete(nullptr);
    }
}