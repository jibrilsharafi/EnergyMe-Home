#include "mqtt.h"

extern AdvancedLogger logger;
extern Ade7953 ade7953;

static const char *TAG = "mqtt";

namespace Mqtt
{
    // Static variables (moved from main.cpp)
    static WiFiClientSecure _net;
    static PubSubClient _clientMqtt(_net);
    static PublishMqtt _publishMqtt;
    static CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> _payloadMeter;
    
    // FreeRTOS queues
    static QueueHandle_t _logQueue = NULL;
    static QueueHandle_t _meterQueue = NULL;
    
    // Task handle
    static TaskHandle_t _taskHandle = NULL;
    
    // Configuration state variables
    static bool _cloudServicesEnabled = DEFAULT_IS_CLOUD_SERVICES_ENABLED;
    static bool _sendPowerDataEnabled = DEFAULT_IS_SEND_POWER_DATA_ENABLED;
    
    // Timing variables
    static unsigned long _lastMillisMqttLoop = 0;
    static unsigned long _lastMillisMqttFailed = 0;
    static unsigned long _lastMillisMeterPublished = 0;
    static unsigned long _lastMillisStatusPublished = 0;
    static unsigned long _lastMillisStatisticsPublished = 0;
    
    // Connection state
    static bool _isSetupDone = false;
    static bool _isClaimInProgress = false;
    static unsigned long _mqttConnectionAttempt = 0;
    static unsigned long _nextMqttConnectionAttemptMillis = 0;
    
    // Certificates storage
    static char _awsIotCoreCert[AWS_IOT_CORE_CERT_BUFFER_SIZE];
    static char _awsIotCorePrivateKey[AWS_IOT_CORE_PRIVATE_KEY_BUFFER_SIZE];
    
    // Topic buffers
    static char _mqttTopicConnectivity[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicMeter[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicStatus[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicMetadata[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicChannel[MQTT_TOPIC_BUFFER_SIZE];
    static char _mqttTopicCrash[MQTT_TOPIC_BUFFER_SIZE];
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
    static void _setTopicCrash();
    static void _setTopicStatistics();
    static void _circularBufferToJson(JsonDocument* jsonDocument, CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &payloadMeter);
    static void _publishConnectivity(bool isOnline = true);
    static void _publishMeter();
    static void _publishStatus();
    static void _publishMetadata();
    static void _publishChannel();
    static void _publishCrash();
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
    static bool _isCloudServicesEnabled();
    static bool _isSendPowerDataEnabled();
    static void _setSendPowerDataEnabled(bool enabled);

    void begin()
    {
        logger.debug("Setting up MQTT client...", TAG);

        // Load preferences at startup
        _loadPreferences();

#if HAS_SECRETS
        // Create FreeRTOS queues
        _logQueue = xQueueCreate(MQTT_LOG_QUEUE_SIZE, sizeof(LogJson));
        if (_logQueue == NULL) {
            logger.error("Failed to create MQTT log queue", TAG);
            return;
        }
        
        _meterQueue = xQueueCreate(MQTT_METER_QUEUE_SIZE, sizeof(PayloadMeter));
        if (_meterQueue == NULL) {
            logger.error("Failed to create MQTT meter queue", TAG);
            vQueueDelete(_logQueue);
            _logQueue = NULL;
            return;
        }

        // Start the FreeRTOS task
        if (_taskHandle == NULL)
        {
            xTaskCreate(_mqttTask, "MqttTask", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, &_taskHandle);
            if (_taskHandle != NULL)
            {
                logger.debug("MQTT task started", TAG);
            }
            else
            {
                logger.error("Failed to create MQTT task", TAG);
                // Clean up queues
                if (_logQueue != NULL) {
                    vQueueDelete(_logQueue);
                    _logQueue = NULL;
                }
                if (_meterQueue != NULL) {
                    vQueueDelete(_meterQueue);
                    _meterQueue = NULL;
                }
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
        
        // Clean up queues
        if (_logQueue != NULL) {
            vQueueDelete(_logQueue);
            _logQueue = NULL;
            logger.debug("MQTT log queue deleted", TAG);
        }
        
        if (_meterQueue != NULL) {
            vQueueDelete(_meterQueue);
            _meterQueue = NULL;
            logger.debug("MQTT meter queue deleted", TAG);
        }

        logger.info("MQTT client stopped", TAG);
    }

    void enable()
    {
        logger.debug("Enabling cloud services...", TAG);
        
        // Update state variable
        _cloudServicesEnabled = true;
        
        // Save preference for cloud services enabled
        Preferences prefs;
        prefs.begin(MQTT_PREFERENCES_NAMESPACE, false);
        prefs.putBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, true);
        prefs.end();
        
        logger.info("Cloud services enabled", TAG);
    }

    void disable()
    {
        logger.debug("Disabling cloud services...", TAG);
        
        // Update state variable
        _cloudServicesEnabled = false;
        
        // Save preference for cloud services disabled
        Preferences prefs;
        prefs.begin(MQTT_PREFERENCES_NAMESPACE, false);
        prefs.putBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, false);
        prefs.end();

        stop();
        
        logger.info("Cloud services disabled", TAG);
    }

    // Public methods for requesting MQTT publications
    void requestConnectivityPublish(bool isOnline)
    {
        _publishMqtt.connectivity = true;
        // Store the online state for later use in _publishConnectivity
    }

    void requestMeterPublish() {_publishMqtt.meter = true; }
    void requestStatusPublish() {_publishMqtt.status = true; }
    void requestMetadataPublish() {_publishMqtt.metadata = true; }
    void requestChannelPublish() {_publishMqtt.channel = true; }
    void requestCrashPublish() {_publishMqtt.crash = true; }
    void requestStatisticsPublish() {_publishMqtt.statistics = true; }

    // Public methods for pushing data to queues
    void pushLog(const char* timestamp, unsigned long millisEsp, const char* level, unsigned int coreId, const char* function, const char* message)
    {
        if (_logQueue == NULL) {
            return; // Queue not initialized
        }
        
        // Filter debug logs if not enabled
        if ((strcmp(level, "debug") == 0 && !debugFlagsRtc.enableMqttDebugLogging) || 
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
        if (_meterQueue == NULL) {
            return; // Queue not initialized
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
        size_t copyLength = (length < sizeof(message) - 1) ? length : sizeof(message) - 1;
        memcpy(message, payload, copyLength);
        message[copyLength] = '\0';

        if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE))
        {
            JsonDocument _jsonDocument;
            if (deserializeJson(_jsonDocument, message)) {
                return;
            }
            serializeJsonToSpiffs(FW_UPDATE_INFO_JSON_PATH, _jsonDocument);
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_RESTART))
        {
            setRestartEsp32("subscribeCallback", "Restart requested from MQTT");
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE))
        {
            JsonDocument _jsonDocument;
            if (deserializeJson(_jsonDocument, message)) {
                return;
            }

            if (_jsonDocument["status"] == "success")
            {
                const char* encryptedCertPem = _jsonDocument["encryptedCertificatePem"];
                const char* encryptedPrivateKey = _jsonDocument["encryptedPrivateKey"];
                
                writeEncryptedPreferences(PREFS_KEY_CERTIFICATE, encryptedCertPem);
                writeEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, encryptedPrivateKey);

                // Restart MQTT connection
                setRestartEsp32("subscribeCallback", "Restarting after successful certificates provisioning");
            }
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES))
        {
            clearCertificates();
            setRestartEsp32("subscribeCallback", "Certificates erase requested from MQTT");
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION))
        {
            JsonDocument _jsonDocument;
            if (deserializeJson(_jsonDocument, message)) {
                return;
            }
            
            // Only handle sendPowerData setting
            if (_jsonDocument["sendPowerData"].is<bool>()) {
                bool sendPowerData = _jsonDocument["sendPowerData"].as<bool>();
                _setSendPowerDataEnabled(sendPowerData);
            }
        }
        else if (strstr(topic, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING))
        {
            JsonDocument _jsonDocument;
            if (deserializeJson(_jsonDocument, message)) {
                return;
            }

            bool enable = false;
            if (_jsonDocument["enable"].is<bool>()) {
                enable = _jsonDocument["enable"].as<bool>();
            }

            if (enable)
            {
                int durationMinutes = MQTT_DEBUG_LOGGING_DEFAULT_DURATION / (60 * 1000);
                if (_jsonDocument["duration_minutes"].is<int>()) {
                    durationMinutes = _jsonDocument["duration_minutes"].as<int>();
                }
                
                int durationMs = durationMinutes * 60 * 1000;
                if (durationMs <= 0 || durationMs > MQTT_DEBUG_LOGGING_MAX_DURATION) {
                    durationMs = MQTT_DEBUG_LOGGING_DEFAULT_DURATION;
                }
                
                debugFlagsRtc.enableMqttDebugLogging = true;
                debugFlagsRtc.mqttDebugLoggingDurationMillis = durationMs;
                debugFlagsRtc.mqttDebugLoggingEndTimeMillis = millis() + durationMs;
                debugFlagsRtc.signature = DEBUG_FLAGS_RTC_SIGNATURE;
            }
            else
            {
                debugFlagsRtc.enableMqttDebugLogging = false;
                debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
                debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
                debugFlagsRtc.signature = 0;
            }
        }
    }

    static void _mqttTask(void *parameter)
    {
        logger.debug("MQTT task started", TAG);

        while (true)
        {
            // Wait for the MQTT loop interval
            vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));

            if (!CustomWifi::isFullyConnected())
            {
                continue;
            }

            // Handle cloud services being disabled
            if (!_isCloudServicesEnabled())
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
                    clearCertificates();
                    _isSetupDone = false;
                    _isClaimInProgress = false;
                }
                continue;
            }

            // Handle restart requirement
            if (restartConfiguration.isRequired)
            {
                if (_isSetupDone)
                {
                    logger.info("Restart required. Disconnecting MQTT", TAG);
                    
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
                    
                    _isSetupDone = false;
                }
                continue;
            }

            // If cloud services are enabled but we're in claim process, just loop the client
            if (_isClaimInProgress)
            {
                _clientMqtt.loop();
                continue;
            }

            // If cloud services are enabled but setup not done, check certificates and setup
            if (!_isSetupDone)
            {
                if (!checkCertificatesExist())
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
                if (millis() >= _nextMqttConnectionAttemptMillis)
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
    }    static bool _connectMqtt()
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
            int _currentState = _clientMqtt.state();
            logger.warning(
                "Failed to connect to MQTT (attempt %d). Reason: %s (%d). Retrying...",
                TAG,
                _mqttConnectionAttempt + 1,
                getMqttStateReason(_currentState),
                _currentState
            );

            _lastMillisMqttFailed = millis();
            _mqttConnectionAttempt++;

            // Check for specific errors that warrant clearing certificates
            if (_currentState == MQTT_CONNECT_BAD_CREDENTIALS || _currentState == MQTT_CONNECT_UNAUTHORIZED) {
                logger.error("MQTT connection failed due to authorization/credentials error (%d). Erasing certificates and restarting...",
                    TAG,
                    _currentState);
                clearCertificates();
                setRestartEsp32(TAG, "MQTT Authentication/Authorization Error");
                _nextMqttConnectionAttemptMillis = UINT32_MAX; // Prevent further attempts before restart
                return false; // Prevent further processing in this cycle
            }

            // Calculate next attempt time using exponential backoff
            unsigned long _backoffDelay = MQTT_INITIAL_RECONNECT_INTERVAL;
            for (int i = 0; i < _mqttConnectionAttempt - 1 && _backoffDelay < MQTT_MAX_RECONNECT_INTERVAL; ++i) {
                _backoffDelay *= MQTT_RECONNECT_MULTIPLIER;
            }
            _backoffDelay = min(_backoffDelay, (unsigned long)MQTT_MAX_RECONNECT_INTERVAL);

            _nextMqttConnectionAttemptMillis = millis() + _backoffDelay;

            logger.info("Next MQTT connection attempt in %lu ms", TAG, _backoffDelay);

            return false;
        }
    }

    static void _setCertificates() {
        logger.debug("Setting certificates...", TAG);

        readEncryptedPreferences(PREFS_KEY_CERTIFICATE, preshared_encryption_key, _awsIotCoreCert, sizeof(_awsIotCoreCert));
        readEncryptedPreferences(PREFS_KEY_PRIVATE_KEY, preshared_encryption_key, _awsIotCorePrivateKey, sizeof(_awsIotCorePrivateKey));

        // Debug certificate lengths and format
        size_t certLen = strlen(_awsIotCoreCert);
        size_t keyLen = strlen(_awsIotCorePrivateKey);
        logger.debug("Certificate length: %d bytes, Private key length: %d bytes", TAG, certLen, keyLen);

        // Validate certificate format
        if (certLen < 10 || strstr(_awsIotCoreCert, "-----BEGIN") == nullptr) {
            logger.error("Invalid certificate format detected. Certificate may be corrupted.", TAG);
            clearCertificates();
            setRestartEsp32(TAG, "Invalid certificate format");
            return;
        }

        if (keyLen < 10 || strstr(_awsIotCorePrivateKey, "-----BEGIN") == nullptr) {
            logger.error("Invalid private key format detected. Private key may be corrupted.", TAG);
            clearCertificates();
            setRestartEsp32(TAG, "Invalid private key format");
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

        int _connectionAttempt = 0;
        unsigned int _loops = 0;
        while (_connectionAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && _loops < MAX_LOOP_ITERATIONS) {
            _loops++;
            logger.debug("Attempting to connect to MQTT for claiming certificates (%d/%d)...", TAG, _connectionAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_clientMqtt.connect(DEVICE_ID)) {
                logger.debug("Connected to MQTT for claiming certificates", TAG);
                break;
            }

            logger.warning(
                "Failed to connect to MQTT for claiming certificates (%d/%d). Reason: %s. Retrying...",
                TAG,
                _connectionAttempt + 1,
                MQTT_CLAIM_MAX_CONNECTION_ATTEMPT,
                getMqttStateReason(_clientMqtt.state())
            );

            _connectionAttempt++;
        }

        if (_connectionAttempt >= MQTT_CLAIM_MAX_CONNECTION_ATTEMPT) {
            logger.error("Failed to connect to MQTT for claiming certificates after %d attempts", TAG, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);
            setRestartEsp32(TAG, "Failed to claim certificates");
            return;
        }

        _subscribeProvisioningResponse();
        
        int _publishAttempt = 0;
        _loops = 0;
        while (_publishAttempt < MQTT_CLAIM_MAX_CONNECTION_ATTEMPT && _loops < MAX_LOOP_ITERATIONS) {
            _loops++;
            logger.debug("Attempting to publish provisioning request (%d/%d)...", TAG, _publishAttempt + 1, MQTT_CLAIM_MAX_CONNECTION_ATTEMPT);

            if (_publishProvisioningRequest()) {
                logger.debug("Provisioning request published", TAG);
                break;
            }

            logger.warning(
                "Failed to publish provisioning request (%d/%d). Retrying...",
                TAG,
                _publishAttempt + 1,
                MQTT_CLAIM_MAX_CONNECTION_ATTEMPT
            );

            _publishAttempt++;
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
        _setTopicCrash();
        _setTopicStatistics();

        logger.debug("MQTT topics setup complete", TAG);
    }

    static void _setTopicConnectivity() { _constructMqttTopic(MQTT_TOPIC_CONNECTIVITY, _mqttTopicConnectivity, sizeof(_mqttTopicConnectivity)); }
    static void _setTopicMeter() { _constructMqttTopicWithRule(aws_iot_core_rulemeter, MQTT_TOPIC_METER, _mqttTopicMeter, sizeof(_mqttTopicMeter)); }
    static void _setTopicStatus() { _constructMqttTopic(MQTT_TOPIC_STATUS, _mqttTopicStatus, sizeof(_mqttTopicStatus)); }
    static void _setTopicMetadata() { _constructMqttTopic(MQTT_TOPIC_METADATA, _mqttTopicMetadata, sizeof(_mqttTopicMetadata)); }
    static void _setTopicChannel() { _constructMqttTopic(MQTT_TOPIC_CHANNEL, _mqttTopicChannel, sizeof(_mqttTopicChannel)); }
    static void _setTopicCrash() { _constructMqttTopic(MQTT_TOPIC_CRASH, _mqttTopicCrash, sizeof(_mqttTopicCrash)); }
    static void _setTopicStatistics() { _constructMqttTopic(MQTT_TOPIC_STATISTICS, _mqttTopicStatistics, sizeof(_mqttTopicStatistics)); }

    // TODO: use messagepack instead of JSON here for efficiency
    static void _circularBufferToJson(JsonDocument* jsonDocument, CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> &_payloadMeter) {
        logger.debug("Converting circular buffer to JSON...", TAG);
        
        // Additional safety check - if restart is in progress and enough time has passed, 
        // avoid operations that might conflict with cleanup
        if (restartConfiguration.isRequired && 
            (millis() - restartConfiguration.requiredAt) > (ESP32_RESTART_DELAY - 1000)) {
            logger.warning("Restart imminent, skipping circular buffer to JSON conversion", TAG);
            return;
        }
        
        // Log memory usage before JSON operations
        size_t freeHeap = ESP.getFreeHeap();
        size_t minFreeHeap = ESP.getMinFreeHeap();
        logger.debug("Memory before JSON: Free heap: %u bytes, Min free heap: %u bytes", TAG, freeHeap, minFreeHeap);
        
        if (freeHeap < JSON_SAFE_MIN_HEAP_SIZE) { // Less than safe threshold
            logger.warning("Low heap memory detected (%u bytes), JSON operations may fail", TAG, freeHeap);
        }
        
        JsonArray _jsonArray = jsonDocument->to<JsonArray>();
        
        ade7953.takePayloadMeterMutex();
        unsigned int _loops = 0;
        while (!_payloadMeter.isEmpty() && _loops < MAX_LOOP_ITERATIONS && _isSendPowerDataEnabled()) {
            _loops++;
            JsonObject _jsonObject = _jsonArray.add<JsonObject>();

            PayloadMeter _oldestPayloadMeter = _payloadMeter.shift();

            if (_oldestPayloadMeter.unixTime == 0) {
                logger.debug("Payload meter has zero unixTime, skipping...", TAG);
                continue;
            }

            if (!validateUnixTime(_oldestPayloadMeter.unixTime)) {
                logger.warning("Invalid unixTime in payload meter: %llu", TAG, _oldestPayloadMeter.unixTime);
                continue;
            }

            _jsonObject["unixTime"] = _oldestPayloadMeter.unixTime;
            _jsonObject["channel"] = _oldestPayloadMeter.channel;
            _jsonObject["activePower"] = _oldestPayloadMeter.activePower;
            _jsonObject["powerFactor"] = _oldestPayloadMeter.powerFactor;
        }
        ade7953.givePayloadMeterMutex();

        for (int i = 0; i < MULTIPLEXER_CHANNEL_COUNT; i++) {
            if (ade7953.channelData[i].active) {
                JsonObject _jsonObject = _jsonArray.add<JsonObject>();

                if (ade7953.meterValues[i].lastUnixTimeMilliseconds == 0) {
                    logger.debug("Meter values for channel %d have zero unixTime, skipping...", TAG, i);
                    continue;
                }

                if (!validateUnixTime(ade7953.meterValues[i].lastUnixTimeMilliseconds)) {
                    logger.warning("Invalid unixTime in meter values for channel %d: %llu", TAG, i, ade7953.meterValues[i].lastUnixTimeMilliseconds);
                    continue;
                }

                _jsonObject["unixTime"] = ade7953.meterValues[i].lastUnixTimeMilliseconds;
                _jsonObject["channel"] = i;
                _jsonObject["activeEnergyImported"] = ade7953.meterValues[i].activeEnergyImported;
                _jsonObject["activeEnergyExported"] = ade7953.meterValues[i].activeEnergyExported;
                _jsonObject["reactiveEnergyImported"] = ade7953.meterValues[i].reactiveEnergyImported;
                _jsonObject["reactiveEnergyExported"] = ade7953.meterValues[i].reactiveEnergyExported;
                _jsonObject["apparentEnergy"] = ade7953.meterValues[i].apparentEnergy;
            }
        }

        JsonObject _jsonObject = _jsonArray.add<JsonObject>();

        if (ade7953.meterValues[CHANNEL_0].lastUnixTimeMilliseconds == 0) {
            logger.debug("Meter values have zero unixTime, skipping...", TAG);
            return;
        }

        if (!validateUnixTime(ade7953.meterValues[CHANNEL_0].lastUnixTimeMilliseconds)) {
            logger.warning("Invalid unixTime in meter values: %llu", TAG, ade7953.meterValues[CHANNEL_0].lastUnixTimeMilliseconds);
            return;
        }    
        _jsonObject["unixTime"] = ade7953.meterValues[CHANNEL_0].lastUnixTimeMilliseconds;
        _jsonObject["voltage"] = ade7953.meterValues[CHANNEL_0].voltage;

        // Log memory usage after JSON operations
        size_t freeHeapAfter = ESP.getFreeHeap();
        logger.debug("Memory after JSON: Free heap: %u bytes (change: %d bytes)", TAG, freeHeapAfter, (int)(freeHeapAfter - freeHeap));

        logger.debug("Circular buffer converted to JSON", TAG);
    }

    static void _publishConnectivity(bool isOnline) {
        logger.debug("Publishing connectivity to MQTT...", TAG);

        JsonDocument _jsonDocument;
        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["connectivity"] = isOnline ? "online" : "offline";

        char connectivityMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, connectivityMessage, sizeof(connectivityMessage));

        if (_publishMessage(_mqttTopicConnectivity, connectivityMessage, true)) {_publishMqtt.connectivity = false;} // Publish with retain

        logger.debug("Connectivity published to MQTT", TAG);
    }

    static void _publishMeter() {
        logger.debug("Publishing meter data to MQTT...", TAG);

        JsonDocument _jsonDocument;
        _circularBufferToJson(&_jsonDocument, _payloadMeter);

        char meterMessage[JSON_MQTT_LARGE_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, meterMessage, sizeof(meterMessage));

        if (_publishMessage(_mqttTopicMeter, meterMessage)) {_publishMqtt.meter = false;}

        logger.debug("Meter data published to MQTT", TAG);
    }

    static void _publishStatus() {
        logger.debug("Publishing status to MQTT...", TAG);

        JsonDocument _jsonDocument;

        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["rssi"] = WiFi.RSSI();
        _jsonDocument["uptime"] = millis();
        _jsonDocument["freeHeap"] = ESP.getFreeHeap();
        _jsonDocument["freeSpiffs"] = SPIFFS.totalBytes() - SPIFFS.usedBytes();

        char statusMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, statusMessage, sizeof(statusMessage));

        if (_publishMessage(_mqttTopicStatus, statusMessage)) {_publishMqtt.status = false;}

        logger.debug("Status published to MQTT", TAG);
    }

    static void _publishMetadata() {
        logger.debug("Publishing metadata to MQTT...", TAG);

        JsonDocument _jsonDocument;

        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["firmwareBuildVersion"] = FIRMWARE_BUILD_VERSION;
        _jsonDocument["firmwareBuildDate"] = FIRMWARE_BUILD_DATE;

        char metadataMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, metadataMessage, sizeof(metadataMessage));

        if (_publishMessage(_mqttTopicMetadata, metadataMessage)) {_publishMqtt.metadata = false;}
        
        logger.debug("Metadata published to MQTT", TAG);
    }

    static void _publishChannel() {
        logger.debug("Publishing channel data to MQTT", TAG);

        JsonDocument _jsonChannelData;
        ade7953.channelDataToJson(_jsonChannelData);
        
        JsonDocument _jsonDocument;
        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["data"] = _jsonChannelData;

        char channelMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, channelMessage, sizeof(channelMessage));
     
        if (_publishMessage(_mqttTopicChannel, channelMessage)) {_publishMqtt.channel = false;}

        logger.debug("Channel data published to MQTT", TAG);
    }

    static void _publishCrash() {
        logger.debug("Publishing crash data to MQTT", TAG);

        CrashData _crashData;
        if (!CrashMonitor::getSavedCrashData(_crashData)) {
            logger.error("Error getting crash data", TAG);
            return;
        }

        JsonDocument _jsonDocumentCrash;
        if (!CrashMonitor::getJsonReport(_jsonDocumentCrash)) {
            logger.error("Error creating JSON report", TAG);
            return;
        }

        JsonDocument _jsonDocument;
        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["crashData"] = _jsonDocumentCrash;

        char crashMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, crashMessage, sizeof(crashMessage));

        if (_publishMessage(_mqttTopicCrash, crashMessage)) {_publishMqtt.crash = false;}

        logger.debug("Crash data published to MQTT", TAG);
    }

    static void _publishStatistics() {
        logger.debug("Publishing statistics to MQTT", TAG);

        JsonDocument _jsonDocument;
        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        
        JsonDocument _jsonDocumentStatistics;
        statisticsToJson(statistics, _jsonDocumentStatistics);
        _jsonDocument["statistics"] = _jsonDocumentStatistics;

        char statisticsMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, statisticsMessage, sizeof(statisticsMessage));

        if (_publishMessage(_mqttTopicStatistics, statisticsMessage)) {_publishMqtt.statistics = false;}

        logger.debug("Statistics published to MQTT", TAG);
    }

    static bool _publishProvisioningRequest() {
        logger.debug("Publishing provisioning request to MQTT", TAG);

        JsonDocument _jsonDocument;

        _jsonDocument["unixTime"] = CustomTime::getUnixTimeMilliseconds();
        _jsonDocument["firmwareVersion"] = FIRMWARE_BUILD_VERSION;

        char provisioningRequestMessage[JSON_MQTT_BUFFER_SIZE];
        safeSerializeJson(_jsonDocument, provisioningRequestMessage, sizeof(provisioningRequestMessage));

        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_PROVISIONING_REQUEST, _topic, sizeof(_topic));

        return _publishMessage(_topic, provisioningRequestMessage);
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

        // Check memory and stack before publishing
        size_t currentFreeHeap = ESP.getFreeHeap();
        if (currentFreeHeap < JSON_SAFE_MIN_HEAP_SIZE) {
            logger.warning("Critical memory situation during publish (%u bytes free), potential crash risk", TAG, currentFreeHeap);
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
        if (
            ((_payloadMeter.size() > (MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS * 0.90)) && _isSendPowerDataEnabled()) || 
            (millis() - _lastMillisMeterPublished) > MQTT_MAX_INTERVAL_METER_PUBLISH
        ) { // Either buffer is full (and we are sending power data) or time has passed
            logger.debug("Setting flag to publish %d meter data points", TAG, _payloadMeter.size());

            _publishMqtt.meter = true;
            
            _lastMillisMeterPublished = millis();
        }
    }

    static void _checkIfPublishStatusNeeded() {
        if ((millis() - _lastMillisStatusPublished) > MQTT_MAX_INTERVAL_STATUS_PUBLISH) {
            logger.debug("Setting flag to publish status", TAG);
            
            _publishMqtt.status = true;
            
            _lastMillisStatusPublished = millis();
        }
    }

    static void _checkIfPublishStatisticsNeeded() {
        if ((millis() - _lastMillisStatisticsPublished) > MQTT_MAX_INTERVAL_STATISTICS_PUBLISH) {
            logger.debug("Setting flag to publish statistics", TAG);
            
            _publishMqtt.statistics = true;
            
            _lastMillisStatisticsPublished = millis();
        }
    }

    static void _checkPublishMqtt() {
        if (_publishMqtt.connectivity) {_publishConnectivity();}
        if (_publishMqtt.meter) {_publishMeter();}
        if (_publishMqtt.status) {_publishStatus();}
        if (_publishMqtt.metadata) {_publishMetadata();}
        if (_publishMqtt.channel) {_publishChannel();}
        if (_publishMqtt.crash) {_publishCrash();}
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
        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE, _topic, sizeof(_topic));
        
        if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to firmware update topic", TAG);
        }
    }

    static void _subscribeRestart() {
        logger.debug("Subscribing to restart topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_RESTART);
        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_RESTART, _topic, sizeof(_topic));
        
        if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to restart topic", TAG);
        }
    }

    static void _subscribeEraseCertificates() {
        logger.debug("Subscribing to erase certificates topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES);
        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES, _topic, sizeof(_topic));
        
        if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to erase certificates topic", TAG);
        }
    }

    static void _subscribeEnableDebugLogging() { 
        logger.debug("Subscribing to enable debug logging topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING);
        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING, _topic, sizeof(_topic));
        
        if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
            logger.warning("Failed to subscribe to enable debug logging topic", TAG);
        }
    }

    static void _subscribeProvisioningResponse() {
        logger.debug("Subscribing to provisioning response topic: %s", TAG, MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE);
        char _topic[MQTT_TOPIC_BUFFER_SIZE];
        _constructMqttTopic(MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE, _topic, sizeof(_topic));
        
        if (!_clientMqtt.subscribe(_topic, MQTT_TOPIC_SUBSCRIBE_QOS)) {
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
        PayloadMeter meterEntry;
        
        // Transfer data from queue to circular buffer for batch processing
        while (xQueueReceive(_meterQueue, &meterEntry, 0) == pdTRUE) {
            if (!_payloadMeter.isFull()) {
                _payloadMeter.push(meterEntry);
            } else {
                // Buffer is full, trigger immediate publish
                _publishMqtt.meter = true;
                break;
            }
        }
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
    
    // Helper functions for preferences
    static void _loadPreferences()
    {
        logger.debug("Loading MQTT preferences...", TAG);
        
        Preferences prefs;
        prefs.begin(MQTT_PREFERENCES_NAMESPACE, true); // Read-only
        
        _cloudServicesEnabled = prefs.getBool(MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY, DEFAULT_IS_CLOUD_SERVICES_ENABLED);
        _sendPowerDataEnabled = prefs.getBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, true);
        
        prefs.end();
        
        logger.info("Cloud services enabled: %s, Send power data enabled: %s", TAG, 
                   _cloudServicesEnabled ? "true" : "false",
                   _sendPowerDataEnabled ? "true" : "false");
    }
    
    static bool _isCloudServicesEnabled()
    {
        return _cloudServicesEnabled;
    }
    
    static bool _isSendPowerDataEnabled()
    {
        return _sendPowerDataEnabled;
    }
    
    static void _setSendPowerDataEnabled(bool enabled)
    {
        logger.debug("Setting send power data enabled to %s", TAG, enabled ? "true" : "false");
        
        // Update state variable
        _sendPowerDataEnabled = enabled;
        
        // Save to preferences
        Preferences prefs;
        prefs.begin(MQTT_PREFERENCES_NAMESPACE, false); // Read-write
        prefs.putBool(MQTT_PREFERENCES_SEND_POWER_DATA_KEY, enabled);
        prefs.end();
        
        logger.info("Send power data %s", TAG, enabled ? "enabled" : "disabled");
    }

}