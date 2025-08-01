#include "mqtt.h"

static const char *TAG = "mqtt";

namespace Mqtt
{
    // Static variables (moved from main.cpp)
    static WiFiClientSecure _net;
    static PubSubClient _clientMqtt(_net);
    static PublishMqtt _publishMqtt;
    
    RTC_NOINIT_ATTR DebugFlagsRtc _debugFlagsRtc;

    // FreeRTOS queues
    static QueueHandle_t _logQueue = NULL;
    static QueueHandle_t _meterQueue = NULL;
    
    // Static queue structures and storage for PSRAM
    static StaticQueue_t _logQueueStruct;
    static StaticQueue_t _meterQueueStruct;
    static uint8_t* _logQueueStorage = nullptr;
    static uint8_t* _meterQueueStorage = nullptr;
    static bool _isLogQueueInitialized = false;
    static bool _isMeterQueueInitialized = false;

    // Helper functions for lazy queue initialization
    static bool _initializeLogQueue();
    static bool _initializeMeterQueue();

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
    
    // Task handle
    static TaskHandle_t _taskHandle = NULL;
    
    // Configuration state variables
    static bool _cloudServicesEnabled = DEFAULT_CLOUD_SERVICES_ENABLED;
    static bool _sendPowerDataEnabled = DEFAULT_SEND_POWER_DATA_ENABLED;
    static bool _debugLogsEnabled = DEFAULT_DEBUG_LOGS_ENABLED;

    // Version for OTA information
    static char _firmwareUpdatesUrl[SERVER_NAME_BUFFER_SIZE];
    static char _firmwareUpdatesVersion[VERSION_BUFFER_SIZE];
    
    // Timing variables
    static unsigned long long _lastMillisMqttLoop = 0;
    static unsigned long long _lastMillisMqttFailed = 0;
    static unsigned long long _lastMillisMeterPublished = 0;
    static unsigned long long _lastMillisStatusPublished = 0;
    static unsigned long long _lastMillisStatisticsPublished = 0;
    
    // Connection state
    static bool _isSetupDone = false;
    static bool _isClaimInProgress = false;
    static unsigned long long _mqttConnectionAttempt = 0;
    static unsigned long long _nextMqttConnectionAttemptMillis = 0;
    
    // Certificates storage
    static char _awsIotCoreCert[CERTIFICATE_BUFFER_SIZE];
    static char _awsIotCorePrivateKey[CERTIFICATE_BUFFER_SIZE];
    
    // Topic buffers
    static char _mqttTopicConnectivity[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicMeter[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicStatus[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicMetadata[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicChannel[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicStatistics[MQTT_TOPIC_BUFFER_SIZE];

    // Private function declarations
    static void _mqttTask(void *parameter);
    static void _setupMqttWithCertificates();
    static bool _connectMqtt();
    static void _setCertificates();
    static void _claimProcess();
    static void _constructMqttTopicWithRule(const char* ruleName, const char* finalTopic, char* topic, size_t topicBufferSize);
    static void _constructMqttTopic(const char* finalTopic, char* topic, size_t topicBufferSize);
    static void _setupTopics();
    static void _setTopicConnectivity();
    static void _setTopicMeter();
    static void _setTopicStatus();
    static void _setTopicMetadata();
    static void _setTopicChannel();
    static void _setTopicStatistics();
    static void _meterQueueToJson(JsonDocument* jsonDocument, QueueHandle_t payloadMeterQueue);
    static void _publishConnectivity(bool isOnline = true);
    static void _publishMeter();
    static void _publishStatus();
    static void _publishMetadata();
    static void _publishChannel();
    static void _publishStatistics();
    static bool _publishProvisioningRequest();
    static bool _publishMessage(const char* topic, const char* message, bool retain = false);
    static void _checkIfPublishMeterNeeded();
    static void _checkIfPublishStatusNeeded();
    static void _checkIfPublishStatisticsNeeded();
    static void _checkPublishMqtt();
    static void _subscribeToTopics();
    static void _subscribeUpdateFirmware();
    static void _subscribeRestart();
    static void _subscribeEraseCertificates();
    static void _subscribeEnableDebugLogging();
    static void _subscribeProvisioningResponse();
    
    // Queue processing functions
    static void _processLogQueue();
    static void _processMeterQueue();
    static void _publishLogEntry(const LogJson& logEntry);
    
    // Helper functions for preferences
    static void _loadPreferences();
    static bool _isSendPowerDataEnabled();
    static void _setSendPowerDataEnabled(bool enabled);

    static bool _setCloudServicesEnabled(bool enabled);
    static bool _getCloudServicesEnabled();
    static bool _setSendPowerData(bool enabled);
    static bool _getSendPowerData();

    static bool _setFirmwareUpdatesVersion(const char* version);
    static bool _getFirmwareUpdatesVersion(char* buffer, size_t bufferSize);
    static bool _setFirmwareUpdatesUrl(const char* url);
    static bool _getFirmwareUpdatesUrl(char* buffer, size_t bufferSize);

    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize);
    static bool _checkCertificatesExist();
    static void _writeEncryptedPreferences(const char* preference_key, const char* value);
    static void _clearCertificates();

    void begin()
    {
        logger.debug("Setting up MQTT client...", TAG);

        // Load preferences at startup
        _loadPreferences();

        if (_debugFlagsRtc.signature != MAGIC_WORD_RTC) {
            logger.debug("RTC magic word is invalid, resetting debug flags", TAG);
            _debugFlagsRtc.enableMqttDebugLogging = false;
            _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
            _debugFlagsRtc.signature = MAGIC_WORD_RTC;
        } else if (_debugFlagsRtc.enableMqttDebugLogging) {
             // If the RTC is valid, and before the restart we were logging with debug info, reset the clock and start again the debugging
            logger.debug("RTC debug logging was enabled, restarting duration", TAG);
            _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = _debugFlagsRtc.mqttDebugLoggingDurationMillis;
        }

#if HAS_SECRETS
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

        // Start the FreeRTOS task
        if (_taskHandle == NULL)
        {
            xTaskCreate(_mqttTask, MQTT_TASK_NAME, MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, &_taskHandle);
            if (_taskHandle != NULL)
            {
                logger.debug("MQTT task started", TAG);
            }
            else
            {
                logger.error("Failed to create MQTT task", TAG);
                // Note: We don't clean up queues here since they might already contain
                // important early logs/meter data. They'll be cleaned up in stop().
            }
        }
#else
        logger.info("MQTT setup skipped - no secrets available", TAG);
#endif
    }

    void stop()
    {
        logger.debug("Stopping MQTT client...", TAG);
        
        if (_taskHandle != NULL)
        {
            vTaskDelete(_taskHandle);
            _taskHandle = NULL;
            logger.debug("MQTT task stopped", TAG);
        }
        
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

        logger.info("MQTT client stopped", TAG);
    }

    // Public methods for requesting MQTT publications
    void requestConnectivityPublish() { _publishMqtt.connectivity = true; }
    void requestMeterPublish() {_publishMqtt.meter = true; }
    void requestStatusPublish() {_publishMqtt.status = true; }
    void requestMetadataPublish() {_publishMqtt.metadata = true; }
    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestStatisticsPublish() {_publishMqtt.statistics = true; }

    // Public methods for pushing data to queues
    void pushLog(const char* timestamp, unsigned long long millisEsp, const char* level, unsigned int coreId, const char* function, const char* message)
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
        
        // Try to send to queue (non-blocking)
        if (xQueueSend(_logQueue, &logEntry, 0) != pdTRUE) {
            // Queue is full, could increment a counter here for monitoring
            statistics.mqttMessagesPublishedError++;
        }
    }

    void pushMeter(const PayloadMeter& payload)
    {
        // Initialize meter queue on first use if not already done
        if (!_isMeterQueueInitialized) {
            if (!_initializeMeterQueue()) {
                return; // Failed to initialize, drop this meter data
            }
        }
        
        // Try to send to queue (non-blocking)
        if (xQueueSend(_meterQueue, &payload, 0) != pdTRUE) {
            // Queue is full, could increment a counter here for monitoring
            // For now, we'll just drop the data as it's better than blocking
        }
    }

    static void _subscribeCallback(const char* topic, byte *payload, unsigned int length)
    {
        char message[MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE];
        if (length >= sizeof(message)) {
            logger.warning("The MQTT message received from the topic %s has a size of %d (too big). It will be truncated", TAG, topic, length);
        }
        snprintf(message, sizeof(message), "%.*s", length, (char*)payload);

        logger.debug("Received MQTT message from %s: %s", TAG, topic, message);

        if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE))
        {
            // Expected JSON format: {"version": "1.0.0", "url": "https://example.com/firmware.bin"}
            JsonDocument jsonDocument;
            if (deserializeJson(jsonDocument, message)) {
                return;
            }

            if (jsonDocument["version"].is<const char*>()) {
                _setFirmwareUpdatesVersion(jsonDocument["version"].as<const char*>());
            }

            if (jsonDocument["url"].is<const char*>()) {
                _setFirmwareUpdatesUrl(jsonDocument["url"].as<const char*>());
            }
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_RESTART))
        {
            setRestartSystem(TAG, "Restart requested from MQTT");
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE))
        {
            // Expected JSON format: {"status": "success", "encryptedCertificatePem": "...", "encryptedPrivateKey": "..."}
            JsonDocument jsonDocument;
            if (deserializeJson(jsonDocument, message)) {
                return;
            }

            if (jsonDocument["status"] == "success")
            {
                const char* encryptedCertPem = jsonDocument["encryptedCertificatePem"].as<const char*>();
                const char* encryptedPrivateKey = jsonDocument["encryptedPrivateKey"].as<const char*>();

                _writeEncryptedPreferences(PREFS_KEY_CERTIFICATE, encryptedCertPem);
                _writeEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, encryptedPrivateKey);

                // Restart MQTT connection
                setRestartSystem(TAG, "Restarting after successful certificates provisioning");
            }
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES))
        {
            _clearCertificates();
            setRestartSystem(TAG, "Certificates erase requested from MQTT");
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION))
        {
            // Expected JSON format: {"sendPowerData": true, "cloudServicesEnabled": true}
            JsonDocument jsonDocument;
            if (deserializeJson(jsonDocument, message)) {
                return;
            }
            
            // Handle sendPowerData setting
            if (jsonDocument["sendPowerData"].is<bool>()) {
                bool sendPowerData = jsonDocument["sendPowerData"].as<bool>();
                _setSendPowerDataEnabled(sendPowerData);
            }
            
            // Handle cloudServicesEnabled setting
            if (jsonDocument["cloudServicesEnabled"].is<bool>()) {
                bool cloudServicesEnabled = jsonDocument["cloudServicesEnabled"].as<bool>();
                setCloudServicesEnabled(cloudServicesEnabled);
            }
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING))
        {
            // Expected JSON format: {"enable": true, "duration_minutes": 10}
            JsonDocument jsonDocument;
            if (deserializeJson(jsonDocument, message)) {
                return;
            }

            bool enable = false;
            if (jsonDocument["enable"].is<bool>()) {
                enable = jsonDocument["enable"].as<bool>();
            }

            if (enable)
            {
                int durationMinutes = MQTT_DEBUG_LOGGING_DEFAULT_DURATION / (60 * 1000);
                if (jsonDocument["duration_minutes"].is<int>()) {
                    durationMinutes = jsonDocument["duration_minutes"].as<int>();
                }
                
                int durationMs = durationMinutes * 60 * 1000;
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
    }

    static void _mqttTask(void *parameter)
    {
        logger.debug("MQTT task started", TAG);
        
        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(MQTT_LOOP_INTERVAL);

        while (true)
        {
            vTaskDelayUntil(&xLastWakeTime, xFrequency); // TODO: is this the correct approach? Maybe more event driven?

            if (!CustomWifi::isFullyConnected())
            {
                continue;
            }

            // Check if the debug logs have expired in duration
            if (_debugFlagsRtc.enableMqttDebugLogging && millis64() > _debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
                logger.debug("The MQTT debug period has ended", TAG);
                _debugFlagsRtc.enableMqttDebugLogging = false;
                _debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
                _debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
                _debugFlagsRtc.signature = 0;
            }

            // Handle cloud services being disabled
            if (!isCloudServicesEnabled())
            {
                if (_isSetupDone || _isClaimInProgress)
                {
                    logger.warning("Cloud services disabled. Disconnecting MQTT and clearing certificates", TAG);
                    
                    if (_clientMqtt.connected())
                    {
                        // Send last messages before disconnecting
                        _clientMqtt.loop();
                        _publishConnectivity(false);
                        _publishMeter();
                        _publishStatus();
                        _publishMetadata();
                        _publishChannel();
                        _publishStatistics();
                        _clientMqtt.loop();
                        
                        _clientMqtt.disconnect();
                    }
                    
                    // Clear certificates since cloud services are disabled
                    _clearCertificates();
                    _isSetupDone = false;
                    _isClaimInProgress = false;
                }
                continue;
            }

            // Handle restart requirement
            // TODO: this should be handled by the stop function
            // if (restartConfiguration.isRequired)
            // {
            //     if (_isSetupDone)
            //     {
            //         logger.info("Restart required. Disconnecting MQTT", TAG);
                    
            //         // Send last messages before disconnecting
            //         _clientMqtt.loop();
            //         _publishConnectivity(false);
            //         _publishMeter();
            //         _publishStatus();
            //         _publishMetadata();
            //         _publishChannel();
            //         _publishStatistics();
            //         _clientMqtt.loop();

            //         _clientMqtt.disconnect();
                    
            //         _isSetupDone = false;
            //     }
            //     continue;
            // }

            // If cloud services are enabled but we're in claim process, just loop the client
            if (_isClaimInProgress)
            {
                _clientMqtt.loop();
                continue;
            }

            // If cloud services are enabled but setup not done, check certificates and setup
            if (!_isSetupDone)
            {
                if (!_checkCertificatesExist())
                {
                    // Start claim process only if cloud services are enabled
                    _claimProcess();
                    continue;
                }
                else
                {
                    // Certificates exist and cloud services enabled, do setup
                    _setupMqttWithCertificates();
                }
            }

            if (!_clientMqtt.connected())
            {
                // Use exponential backoff timing
                if (millis64() >= _nextMqttConnectionAttemptMillis)
                {
                    logger.debug("MQTT client not connected. Attempting to reconnect...", TAG);
                    _connectMqtt();
                }
            }
            else
            {
                // If connected, reset connection attempts counter for backoff calculation
                if (_mqttConnectionAttempt > 0)
                {
                    logger.debug("MQTT reconnected successfully after %d attempts.", TAG, _mqttConnectionAttempt);
                    _mqttConnectionAttempt = 0;
                }
            }

            // Only loop if connected
            if (_clientMqtt.connected())
            {
                _clientMqtt.loop();

                // Process queues
                _processLogQueue();  // Process logs immediately when connected
                _processMeterQueue(); // Transfer meter data to circular buffer

                _checkIfPublishMeterNeeded();
                _checkIfPublishStatusNeeded();
                _checkIfPublishStatisticsNeeded();

                _checkPublishMqtt();
            }
            else
            {
                // Even if not connected, we can still process meter queue to avoid overflow
                _processMeterQueue();
            }
        }
    }

    static void _setupMqttWithCertificates() {
        logger.debug("Setting up MQTT with existing certificates...", TAG);

        _setupTopics();

        _clientMqtt.setCallback(_subscribeCallback);

        _setCertificates();

        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(_awsIotCoreCert);
        _net.setPrivateKey(_awsIotCorePrivateKey);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        logger.info("MQTT setup complete", TAG);
        _isSetupDone = true;
    }    
    
    static bool _connectMqtt()
    {
        logger.debug("Attempting to connect to MQTT (attempt %d)...", TAG, _mqttConnectionAttempt + 1);

        if (
            _clientMqtt.connect(
                DEVICE_ID,
                _mqttTopicConnectivity,
                MQTT_WILL_QOS,
                MQTT_WILL_RETAIN,
                MQTT_WILL_MESSAGE))
        {
            logger.info("Connected to MQTT", TAG);

            _mqttConnectionAttempt = 0; // Reset attempt counter on success

            _subscribeToTopics();
            // The meter flag should only be set based on buffer status or time from last publish
            _publishMqtt.connectivity = true;
            _publishMqtt.status = true;
            _publishMqtt.metadata = true;
            _publishMqtt.channel = true;
            _publishMqtt.statistics = true;

            return true;
        }
        else
        {
            int currentState = _clientMqtt.state();
            logger.warning(
                "Failed to connect to MQTT (attempt %d). Reason: %s (%d). Retrying...",
                TAG,
                _mqttConnectionAttempt + 1,
                getMqttStateReason(currentState),
                currentState
            );

            _lastMillisMqttFailed = millis64();
            _mqttConnectionAttempt++;

            // Check for specific errors that warrant clearing certificates
            if (currentState == MQTT_CONNECT_BAD_CREDENTIALS || currentState == MQTT_CONNECT_UNAUTHORIZED) {
                logger.error("MQTT connection failed due to authorization/credentials error (%d). Erasing certificates and restarting...",
                    TAG,
                    currentState);
                _clearCertificates();
                setRestartSystem(TAG, "MQTT Authentication/Authorization Error");
                _nextMqttConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts before restart
                return false; // Prevent further processing in this cycle
            }

            // Calculate next attempt time using exponential backoff
            unsigned long _backoffDelay = MQTT_INITIAL_RECONNECT_INTERVAL; // TODO: use function from utils
            for (int i = 0; i < _mqttConnectionAttempt - 1 && _backoffDelay < MQTT_MAX_RECONNECT_INTERVAL; ++i) {
                _backoffDelay *= MQTT_RECONNECT_MULTIPLIER;
            }
            _backoffDelay = min(_backoffDelay, (unsigned long)MQTT_MAX_RECONNECT_INTERVAL);

            _nextMqttConnectionAttemptMillis = millis64() + _backoffDelay;

            logger.info("Next MQTT connection attempt in %llu ms", TAG, _backoffDelay);

            return false;
        }
    }

    static void _setCertificates() {
        logger.debug("Setting certificates...", TAG);

        _readEncryptedPreferences(PREFS_KEY_CERTIFICATE, preshared_encryption_key, _awsIotCoreCert, sizeof(_awsIotCoreCert));
        _readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, preshared_encryption_key, _awsIotCorePrivateKey, sizeof(_awsIotCorePrivateKey));

        // Debug certificate lengths and format
        size_t certLen = strlen(_awsIotCoreCert);
        size_t keyLen = strlen(_awsIotCorePrivateKey);
        logger.debug("Certificate length: %d bytes, Private key length: %d bytes", TAG, certLen, keyLen);

        // Validate certificate format
        if (certLen < 10 || strstr(_awsIotCoreCert, "-----BEGIN") == nullptr) {
            logger.error("Invalid certificate format detected. Certificate may be corrupted.", TAG);
            _clearCertificates();
            setRestartSystem(TAG, "Invalid certificate format");
            return;
        }

        if (keyLen < 10 || strstr(_awsIotCorePrivateKey, "-----BEGIN") == nullptr) {
            logger.error("Invalid private key format detected. Private key may be corrupted.", TAG);
            _clearCertificates();
            setRestartSystem(TAG, "Invalid private key format");
            return;
        }

        logger.debug("Certificates set and validated", TAG);
    }

    static void _claimProcess() {
        logger.debug("Claiming certificates...", TAG);
        _isClaimInProgress = true;

        _clientMqtt.setCallback(_subscribeCallback);
        _net.setCACert(aws_iot_core_cert_ca);
        _net.setCertificate(aws_iot_core_cert_certclaim);
        _net.setPrivateKey(aws_iot_core_cert_privateclaim);

        _clientMqtt.setServer(aws_iot_core_endpoint, AWS_IOT_CORE_PORT);

        _clientMqtt.setBufferSize(MQTT_PAYLOAD_LIMIT);
        _clientMqtt.setKeepAlive(MQTT_OVERRIDE_KEEPALIVE);

        logger.debug("MQTT setup for claiming certificates complete", TAG);

        int connectionAttempt = 0;
        unsigned int loops = 0;
        while (connectionAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            logger.debug("Attempting to connect to MQTT for claiming certificates (%d/%d)...", TAG, connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_clientMqtt.connect(DEVICE_ID)) {
                logger.debug("Connected to MQTT for claiming certificates", TAG);
                break;
            }

            logger.warning(
                "Failed to connect to MQTT for claiming certificates (%d/%d). Reason: %s. Retrying...",
                TAG,
                connectionAttempt + 1,
                MQTT_CLAIM_MAX_CONNECTION_ATTEMPT,
                getMqttStateReason(_clientMqtt.state())
            );

            connectionAttempt++;
        }

        if (connectionAttempt >= MQTT_CLAIM_MAX_CONNECTION_ATTEMPT) {
            logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", TAG, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
            setRestartSystem(TAG, "Failed to claim certificates");
            return;
        }

        _subscribeProvisioningResponse();
        
        int publishAttempt = 0;
        loops = 0;
        while (publishAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && loops < MAX_LOOP_ITERATIONS) {
            loops++;
            logger.debug("Attempting to publish provisioning request (%d/%d)...", TAG, publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_publishProvisioningRequest()) {
                logger.debug("Provisioning request published", TAG);
                break;
            }

            logger.warning(
                "Failed to publish provisioning request (%d/%d). Retrying...",
                TAG,
                publishAttempt + 1,
                MQTT_CLAIM_MAX_CONNECTION_ATTEMPT
            );

            publishAttempt++;
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

        _setTopicConnectivity();
        _setTopicMeter();
        _setTopicStatus();
        _setTopicMetadata();
        _setTopicChannel();    
        _setTopicStatistics();

        logger.debug("MQTT topics setup complete", TAG);
    }

    static void _setTopicConnectivity() { _constructMqttTopic(MQTT_TOPIC_CONNECTIVITY, _mqttTopicConnectivity, sizeof(_mqttTopicConnectivity)); }
    static void _setTopicMeter() { _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicStatus() { _constructMqttTopic(MQTT_TOPIC_STATUS, _mqttTopicStatus, sizeof(_mqttTopicStatus)); }
    static void _setTopicMetadata() { _constructMqttTopic(MQTT_TOPIC_METADATA, _mqttTopicMetadata, sizeof(_mqttTopicMetadata)); }
    static void _setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel, sizeof(_mqttTopicChannel)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }

    // TODO: use messagepack instead of JSON here for efficiency
    static void _meterQueueToJson(JsonDocument* jsonDocument, QueueHandle_t payloadMeterQueue) {
        logger.debug("Converting payload meter queue to JSON...", TAG);
        
        // Additional safety check - if restart is in progress and enough time has passed, 
        // avoid operations that might conflict with cleanup
        // TODO: handled by the stop function
        // if (restartConfiguration.isRequired && 
        //     (millis64() - restartConfiguration.requiredAt) > (SYSTEM_RESTART_DELAY - 1000)) {
        //     logger.warning("Restart imminent, skipping payload meter queue to JSON conversion", TAG);
        //     return;
        // }
        
        JsonArray jsonArray = jsonDocument->to<JsonArray>();
                
        // Process all items from the queue
        PayloadMeter payloadMeter;
        unsigned int loops = 0;
        while (uxQueueMessagesWaiting(payloadMeterQueue) > 0 && loops < MAX_LOOP_ITERATIONS && _isSendPowerDataEnabled()) {
            loops++;
            
            // Receive from queue (non-blocking)
            if (xQueueReceive(payloadMeterQueue, &payloadMeter, 0) != pdTRUE) {
                break; // No more items in queue
            }

            if (payloadMeter.unixTimeMs == 0) {
                logger.debug("Payload meter has zero unixTime, skipping...", TAG);
                continue;
            }

            if (!validateUnixTime(payloadMeter.unixTimeMs)) {
                logger.warning("Invalid unixTime in payload meter: %llu", TAG, payloadMeter.unixTimeMs);
                continue;
            }

            JsonObject jsonObject = jsonArray.add<JsonObject>();
            jsonObject["unixTime"] = payloadMeter.unixTimeMs;
            jsonObject["channel"] = payloadMeter.channel;
            jsonObject["activePower"] = payloadMeter.activePower;
            jsonObject["powerFactor"] = payloadMeter.powerFactor;
        }

        for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
            if (Ade7953::channelData[i].active) {
                JsonObject jsonObject = jsonArray.add<JsonObject>();

                if (Ade7953::meterValues[i].lastUnixTimeMilliseconds == 0) {
                    logger.debug("Meter values for channel %d have zero unixTime, skipping...", TAG, i);
                    continue;
                }

                if (!validateUnixTime(Ade7953::meterValues[i].lastUnixTimeMilliseconds)) {
                    logger.warning("Invalid unixTime in meter values for channel %d: %llu", TAG, i, Ade7953::meterValues[i].lastUnixTimeMilliseconds);
                    continue;
                }

                jsonObject["unixTime"] = Ade7953::meterValues[i].lastUnixTimeMilliseconds;
                jsonObject["channel"] = i;
                jsonObject["activeEnergyImported"] = Ade7953::meterValues[i].activeEnergyImported;
                jsonObject["activeEnergyExported"] = Ade7953::meterValues[i].activeEnergyExported;
                jsonObject["reactiveEnergyImported"] = Ade7953::meterValues[i].reactiveEnergyImported;
                jsonObject["reactiveEnergyExported"] = Ade7953::meterValues[i].reactiveEnergyExported;
                jsonObject["apparentEnergy"] = Ade7953::meterValues[i].apparentEnergy;
            }
        }

        JsonObject jsonObject = jsonArray.add<JsonObject>();

        if (Ade7953::meterValues[0].lastUnixTimeMilliseconds == 0) {
            logger.debug("Meter values have zero unixTime, skipping...", TAG);
            return;
        }

        if (!validateUnixTime(Ade7953::meterValues[0].lastUnixTimeMilliseconds)) {
            logger.warning("Invalid unixTime in meter values: %llu", TAG, Ade7953::meterValues[0].lastUnixTimeMilliseconds);
            return;
        }
        jsonObject["unixTime"] = Ade7953::meterValues[0].lastUnixTimeMilliseconds;
        jsonObject["voltage"] = Ade7953::meterValues[0].voltage;

        logger.debug("Circular buffer converted to JSON", TAG);
    }

    static void _publishConnectivity(bool isOnline) {
        logger.debug("Publishing connectivity to MQTT...", TAG);

        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        jsonDocument["connectivity"] = isOnline ? "online" : "offline";

        char connectivityMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, connectivityMessage, sizeof(connectivityMessage));

        if (_publishMessage(_mqttTopicConnectivity, connectivityMessage, true)) {_publishMqtt.connectivity = false;} // Publish with retain

        logger.debug("Connectivity published to MQTT", TAG);
    }

    static void _publishMeter() {
        logger.debug("Publishing meter data to MQTT...", TAG);

        JsonDocument jsonDocument;
        _meterQueueToJson(&jsonDocument, _meterQueue);

        char meterMessage[JSON_MQTT_LARGE_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, meterMessage, sizeof(meterMessage));

        if (_publishMessage(_mqttTopicMeter, meterMessage)) {_publishMqtt.meter = false;}

        logger.debug("Meter data published to MQTT", TAG);
    }

    static void _publishStatus() {
        logger.debug("Publishing status to MQTT...", TAG);

        JsonDocument jsonDocument;

        SystemDynamicInfo systemInfo;
        populateSystemDynamicInfo(systemInfo);
        
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        char statusMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, statusMessage, sizeof(statusMessage));

        if (_publishMessage(_mqttTopicStatus, statusMessage)) {_publishMqtt.status = false;}

        logger.debug("Status published to MQTT", TAG);
    }

    static void _publishMetadata() {
        logger.debug("Publishing metadata to MQTT...", TAG);

        JsonDocument jsonDocument;

        SystemStaticInfo systemStaticInfo;
        populateSystemStaticInfo(systemStaticInfo);
        systemStaticInfoToJson(systemStaticInfo, jsonDocument);

        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();

        char metadataMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, metadataMessage, sizeof(metadataMessage));

        if (_publishMessage(_mqttTopicMetadata, metadataMessage)) {_publishMqtt.metadata = false;}
        
        logger.debug("Metadata published to MQTT", TAG);
    }

    static void _publishChannel() {
        logger.debug("Publishing channel data to MQTT", TAG);

        JsonDocument _jsonChannelData;
        Ade7953::channelDataToJson(_jsonChannelData);
        
        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        jsonDocument["data"] = _jsonChannelData;

        char channelMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, channelMessage, sizeof(channelMessage));
     
        if (_publishMessage(_mqttTopicChannel, channelMessage)) {_publishMqtt.channel = false;}

        logger.debug("Channel data published to MQTT", TAG);
    }

    static void _publishStatistics() {
        logger.debug("Publishing statistics to MQTT", TAG);

        JsonDocument jsonDocument;
        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        JsonDocument _jsonDocumentStatistics;
        statisticsToJson(statistics, _jsonDocumentStatistics);
        jsonDocument["statistics"] = _jsonDocumentStatistics;

        char statisticsMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, statisticsMessage, sizeof(statisticsMessage));

        if (_publishMessage(_mqttTopicStatistics, statisticsMessage)) {_publishMqtt.statistics = false;}

        logger.debug("Statistics published to MQTT", TAG);
    }

    static bool _publishProvisioningRequest() {
        logger.debug("Publishing provisioning request to MQTT", TAG);

        JsonDocument jsonDocument;

        jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        jsonDocument["firmwareVersion"] = FIRMWARE_BUILD_VERSION;

        char provisioningRequestMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(jsonDocument, provisioningRequestMessage, sizeof(provisioningRequestMessage));

        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, topic, sizeof(topic));

        return _publishMessage(topic, provisioningRequestMessage);
    }

    static bool _publishMessage(const char* topic, const char* message, bool retain) {
        logger.debug(
            "Publishing message to topic %s",
            TAG,
            topic
        );

        if (topic == nullptr || message == nullptr) {
            logger.warning("Null pointer or message passed, meaning MQTT not initialized yet", TAG);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.connected()) {
            logger.warning("MQTT client not connected. State: %s. Skipping publishing on %s", TAG, getMqttStateReason(_clientMqtt.state()), topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }    
        
        // Ensure time has been synced
        if (!CustomTime::isTimeSynched()) {
            logger.warning("Time not synced. Skipping publishing on %s", TAG, topic);
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        if (!_clientMqtt.publish(topic, message, retain)) {
            logger.error("Failed to publish message on %s. MQTT client state: %s", TAG, topic, getMqttStateReason(_clientMqtt.state()));
            statistics.mqttMessagesPublishedError++;
            return false;
        }

        statistics.mqttMessagesPublished++;
        logger.debug("Message published: %s", TAG, message);
        return true;
    }

    static void _checkIfPublishMeterNeeded() {
        UBaseType_t queueSize = uxQueueMessagesWaiting(_meterQueue);
        if (
            ((queueSize > (MQTT_METER_QUEUE_SIZE * 0.90)) && _isSendPowerDataEnabled()) || 
            (millis64() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH
        ) { // Either queue is 90% full (and we are sending power data) or time has passed
            logger.debug("Setting flag to publish %d meter data points", TAG, queueSize);

            _publishMqtt.meter = true;
            
            _lastMillisMeterPublished = millis64();
        }
    }

    static void _checkIfPublishStatusNeeded() {
        if ((millis64() - _lastMillisStatusPublished) > MQTT_MAX_INTERVAL_STATUS_PUBLISH) {
            logger.debug("Setting flag to publish status", TAG);
            
            _publishMqtt.status = true;
            
            _lastMillisStatusPublished = millis64();
        }
    }

    static void _checkIfPublishStatisticsNeeded() {
        if ((millis64() - _lastMillisStatisticsPublished) > MQTT_MAX_INTERVAL_STATISTICS_PUBLISH) {
            logger.debug("Setting flag to publish statistics", TAG);
            
            _publishMqtt.statistics = true;
            
            _lastMillisStatisticsPublished = millis64();
        }
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.connectivity) {_publishConnectivity();}
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.status) {_publishStatus();}
        if (_publishMqtt.metadata) {_publishMetadata();}
        if (_publishMqtt.channel) {_publishChannel();}
        if (_publishMqtt.statistics) {_publishStatistics();}
    }

    static void _subscribeToTopics() {
        logger.debug("Subscribing to topics...", TAG);

        _subscribeUpdateFirmware();
        _subscribeRestart();
        _subscribeEraseCertificates();
        _subscribeEnableDebugLogging(); 

        logger.debug("Subscribed to topics", TAG);
    }

    static void _subscribeUpdateFirmware() {
        logger.debug("Subscribing to firmware update topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE);
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to firmware update topic", TAG);
        }
    }

    static void _subscribeRestart() {
        logger.debug("Subscribing to restart topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_RESTART);
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to restart topic", TAG);
        }
    }

    static void _subscribeEraseCertificates() {
        logger.debug("Subscribing to erase certificates topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES);
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to erase certificates topic", TAG);
        }
    }

    static void _subscribeEnableDebugLogging() { 
        logger.debug("Subscribing to enable debug logging topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING);
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to enable debug logging topic", TAG);
        }
    }

    static void _subscribeProvisioningResponse() {
        logger.debug("Subscribing to provisioning response topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE);
        char topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, topic, sizeof(topic));
        
        if (!_clientMqtt.subscribe(topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to provisioning response topic", TAG);
        }
    }

    // Queue processing functions
    static void _processLogQueue()
    {
        LogJson logEntry;
        
        // Process all available log entries (non-blocking)
        while (xQueueReceive(_logQueue, &logEntry, 0) == pdTRUE) {
            // Only process if MQTT is connected and WiFi is available
            if (_clientMqtt.connected() && CustomWifi::isFullyConnected()) {
                _publishLogEntry(logEntry);
            } else {
                // If not connected, put it back in the queue if there's space
                xQueueSendToFront(_logQueue, &logEntry, 0);
                break; // Stop processing if not connected
            }
        }
    }
    
    static void _processMeterQueue()
    {
        // This function is no longer needed since _meterQueue directly contains
        // the data for publishing. The queue itself replaces the CircularBuffer.
        // Data flows: pushMeter() -> _meterQueue -> _publishMeter() -> _meterQueueToJson()
    }
    
    static void _publishLogEntry(const LogJson& logEntry)
    {
        // Generate the MQTT topic
        char logTopic[MQTT_TOPIC_BUFFER_SIZE * 2]; // Extra space for /log/level
        snprintf(logTopic, sizeof(logTopic), 
                "%s/%s/%s/%s/%s", 
                MQTT_TOPIC_1, MQTT_TOPIC_2, DEVICE_ID, MQTT_TOPIC_LOG, logEntry.level);
        
        // Generate JSON message
        char logMessage[JSON_MQTT_BUFFER_SIZE];
        snprintf(logMessage, sizeof(logMessage),
            "{\"timestamp\":\"%s\","
            "\"millis\":%lu,"
            "\"core\":%u,"
            "\"function\":\"%s\","
            "\"message\":\"%s\"}",
            logEntry.timestamp,
            logEntry.millisEsp,
            logEntry.coreId,
            logEntry.function,
            logEntry.message);
        
        // Publish the log entry
        if (!_publishMessage(logTopic, logMessage)) {
            // If publish failed, we could put it back in the queue
            // For now, we'll just increment the error counter
            statistics.mqttMessagesPublishedError++;
        }
    }
    
    // Helper functions for preferences - now using centralized PreferencesConfig
    
    static void _loadPreferences()
    {
        logger.debug("Loading MQTT preferences...", TAG);
        
        _cloudServicesEnabled = _getCloudServicesEnabled();
        _sendPowerDataEnabled = _getSendPowerData();
        _getFirmwareUpdatesUrl(_firmwareUpdatesUrl, sizeof(_firmwareUpdatesUrl));
        _getFirmwareUpdatesVersion(_firmwareUpdatesVersion, sizeof(_firmwareUpdatesVersion));
        
        logger.debug("Cloud services enabled: %s, Send power data enabled: %s", TAG, 
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false");
        logger.debug("Firmware updates | URL: %s, Version: %s", TAG, 
                   _firmwareUpdatesUrl,
                   _firmwareUpdatesVersion);
    }

    bool isCloudServicesEnabled() { return _cloudServicesEnabled; }
    static bool _isSendPowerDataEnabled() { return _sendPowerDataEnabled; }

    void getFirmwareUpdatesVersion(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s", _firmwareUpdatesVersion);
    }

    void getFirmwareUpdatesUrl(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "%s", _firmwareUpdatesUrl);
    }

    // Configuration methods - now using centralized PreferencesConfig
    void setCloudServicesEnabled(bool enabled)
    {
        logger.debug("Setting cloud services to %s...", TAG, enabled ? "enabled" : "disabled");
        
        // Update state variable
        _cloudServicesEnabled = enabled;
        
        // Save via centralized preferences
        if (_setCloudServicesEnabled(enabled)) {
            logger.info("Cloud services %s", TAG, enabled ? "enabled" : "disabled");
        } else {
            logger.error("Failed to save cloud services setting", TAG);
        }
    }
    
    static void _setSendPowerDataEnabled(bool enabled)
    {
        logger.debug("Setting send power data enabled to %s", TAG, enabled ? "true" : "false");
        
        // Update state variable
        _sendPowerDataEnabled = enabled;
        
        // Save via centralized preferences
        if (_setSendPowerData(enabled)) {
            logger.info("Send power data %s", TAG, enabled ? "enabled" : "disabled");
        } else {
            logger.error("Failed to save send power data setting", TAG);
        }
    }

    static bool _setCloudServicesEnabled(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return false;
        }
        bool success = prefs.putBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, enabled) > 0;
        prefs.end();
        return success;
    }

    static bool _getCloudServicesEnabled() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return false;
        }
        bool enabled;
        enabled = prefs.getBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY);
        prefs.end();
        return enabled;
    }

    static bool _setSendPowerData(bool enabled) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return false;
        }
        bool success = prefs.putBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, enabled) > 0;
        prefs.end();
        return success;
    }

    static bool _getSendPowerData() {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open MQTT preferences", TAG);
            return false;
        }
        bool enabled;
        enabled = prefs.getBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY);
        prefs.end();
        return enabled;
    }

    static bool _setFirmwareUpdatesVersion(const char* version) { // TODO: this could become more complex to allow for AWS IoT Core Jobs (so SHA256, etc.)
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open firmware updates preferences", TAG);
            return false;
        }
        bool success = prefs.putString(MQTT_PREFERENCES_FW_UPDATES_VERSION_KEY, version) > 0;
        prefs.end();
        return success;
    }

    static bool _getFirmwareUpdatesVersion(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open firmware updates preferences", TAG);
            buffer[0] = '\0';
            return false;
        }
        prefs.getString(MQTT_PREFERENCES_FW_UPDATES_VERSION_KEY, buffer, bufferSize);
        prefs.end();
        return true;
    }

    static bool _setFirmwareUpdatesUrl(const char* url) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, false)) {
            logger.error("Failed to open firmware updates preferences", TAG);
            return false;
        }
        bool success = prefs.putString(MQTT_PREFERENCES_FW_UPDATES_URL_KEY, url) > 0;
        prefs.end();
        return success;
    }

    static bool _getFirmwareUpdatesUrl(char* buffer, size_t bufferSize) {
        Preferences prefs;
        if (!prefs.begin(PREFERENCES_NAMESPACE_MQTT, true)) {
            logger.error("Failed to open firmware updates preferences", TAG);
            buffer[0] = '\0';
            return false;
        }
        prefs.getString(MQTT_PREFERENCES_FW_UPDATES_URL_KEY, buffer, bufferSize);
        prefs.end();
        return true;
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
        mbedtls_aes_setkey_dec(&aes, (const unsigned char *)key, ENCRYPTION_KEY_BUFFER_SIZE);

        // Use stack-allocated buffers instead of malloc
        unsigned char decodedData[CERTIFICATE_BUFFER_SIZE];
        unsigned char output[CERTIFICATE_BUFFER_SIZE];

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
        unsigned char paddingLength = output[decodedLength - 1];
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

    static void _readEncryptedPreferences(const char* preference_key, const char* preshared_encryption_key, char* decryptedData, size_t decryptedDataSize) {
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

        char encryptedData[CERTIFICATE_BUFFER_SIZE];
        preferences.getString(preference_key, encryptedData, CERTIFICATE_BUFFER_SIZE);
        preferences.end();

        if (strlen(encryptedData) == 0) {
            logger.warning("No encrypted data found for key: %s", TAG, preference_key);
            return;
        }

        char encryptionKey[ENCRYPTION_KEY_BUFFER_SIZE];
        snprintf(encryptionKey, ENCRYPTION_KEY_BUFFER_SIZE, "%s%s", preshared_encryption_key, DEVICE_ID);

        decryptData(encryptedData, encryptionKey, decryptedData, decryptedDataSize);
    }

    static bool _checkCertificatesExist() {
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

    static void _writeEncryptedPreferences(const char* preference_key, const char* value) {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_CERTIFICATES, false)) {
            logger.error("Failed to open preferences", TAG);
            return;
        }

        preferences.putString(preference_key, value);
        preferences.end();
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

        logger.info("Certificates for cloud services cleared", TAG);
    }
}