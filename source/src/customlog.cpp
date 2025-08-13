#include "customlog.h"

namespace CustomLog
{
    // Static variables - all internal to this module
    static QueueHandle_t _udpLogQueue = nullptr;
    static StaticQueue_t _udpLogQueueStruct;
    static uint8_t* _udpLogQueueStorage = nullptr;
    static WiFiUDP _udpClient;
    static IPAddress _udpDestinationIp;
    static char _udpBuffer[UDP_LOG_BUFFER_SIZE];
    static bool _isUdpInitialized = false;

    // Task state
    static TaskHandle_t _udpTaskHandle = NULL;
    static bool _udpTaskShouldRun = false;

    static void _callbackMqtt(const LogEntry& entry);
    static void _callbackUdp(const LogEntry& entry);

    static bool _initializeQueue();
    static void _startTask();
    static void _stopTask();
    static void _udpTask(void* parameter);

    void begin()
    {
        if (_isUdpInitialized) {
            LOG_DEBUG("UDP logging already initialized");
            return;
        }

        // Initialize queue if not already done
        if (!_initializeQueue()) {
            LOG_ERROR("Failed to initialize UDP log queue");
            return;
        }

        // Initialize UDP destination IP from configuration
        _udpDestinationIp.fromString(DEFAULT_UDP_LOG_DESTINATION_IP);
        
        _udpClient.begin(UDP_LOG_PORT);
        _isUdpInitialized = true;

        LOG_DEBUG("UDP logging configured - destination: %s:%d, PSRAM buffer: %zu bytes",
                    _udpDestinationIp.toString().c_str(), UDP_LOG_PORT, LOG_QUEUE_SIZE);

        // Start async task
        _startTask();
    }

    void stop()
    {
        _stopTask();

        if (_isUdpInitialized) {
            _udpClient.stop();
            _isUdpInitialized = false;
            LOG_DEBUG("UDP client stopped");
        }
        
        // Clean up the queue and PSRAM buffer
        if (_udpLogQueueStorage != nullptr) {
            _udpLogQueue = nullptr;
            
            // Free PSRAM buffer
            if (_udpLogQueueStorage != nullptr) {
                heap_caps_free(_udpLogQueueStorage);
                _udpLogQueueStorage = nullptr;
            }

            LOG_DEBUG("UDP logging queue stopped, PSRAM freed");
        }
    }

    void callbackMultiple(const LogEntry& entry)
    {
        _callbackUdp(entry);
        _callbackMqtt(entry);
    }

    static bool _initializeQueue() // Cannot use logger here to avoid recursion
    {
        if (_udpLogQueueStorage != nullptr) return true;

        // Allocate queue storage in PSRAM
        uint32_t queueLength = LOG_QUEUE_SIZE / sizeof(LogEntry);
        size_t realQueueSize = queueLength * sizeof(LogEntry);
        _udpLogQueueStorage = (uint8_t*)heap_caps_malloc(realQueueSize, MALLOC_CAP_SPIRAM);
        
        if (_udpLogQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for UDP log queue (%d bytes) | Free PSRAM: %d bytes\n", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            return false;
        }

        // Create FreeRTOS static queue using PSRAM buffer
        _udpLogQueue = xQueueCreateStatic(queueLength, sizeof(LogEntry), _udpLogQueueStorage, &_udpLogQueueStruct);
        if (_udpLogQueue == nullptr) {
            Serial.printf("[ERROR] Failed to create UDP log queue\n");
            heap_caps_free(_udpLogQueueStorage);
            _udpLogQueueStorage = nullptr;
            return false;
        }

        Serial.printf("[DEBUG] UDP log queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", realQueueSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    static void _callbackMqtt(const LogEntry& entry)
    {
        #ifdef HAS_SECRETS
        Mqtt::pushLog(entry);
        #endif
    }

    static void _callbackUdp(const LogEntry& entry)
    {
        if (!DEFAULT_IS_UDP_LOGGING_ENABLED) return;
        if (entry.level == LogLevel::VERBOSE) return; // Never send verbose logs via UDP
        if (!_initializeQueue()) return; // Failed to initialize, drop this log
        
        if (xQueueSend(_udpLogQueue, &entry, 0) != pdTRUE) { // Send to queue with no wait to avoid blocking
            // Queue full -> drop
        }
    }

    static void _startTask()
    {
        if (_udpTaskHandle != NULL) {
            LOG_DEBUG("UDP log task already running");
            return;
        }

        LOG_DEBUG("Starting UDP log task");
        BaseType_t result = xTaskCreate(
            _udpTask,
            UDP_LOG_TASK_NAME,
            UDP_LOG_TASK_STACK_SIZE,
            NULL,
            UDP_LOG_TASK_PRIORITY,
            &_udpTaskHandle
        );

        if (result != pdPASS) {
            LOG_ERROR("Failed to create UDP log task");
            _udpTaskHandle = NULL;
        }
    }

    static void _stopTask()
    {
        stopTaskGracefully(&_udpTaskHandle, "UDP log task");
    }

    static void _udpTask(void* parameter) // No logging to avoid recursion
    {
        _udpTaskShouldRun = true;

        while (_udpTaskShouldRun) {
            if (!_initializeQueue()) continue;

            // Block waiting for a log entry, but wake up periodically to allow shutdown
            LogEntry entry; // non-const for xQueueReceive
            if (xQueueReceive(_udpLogQueue, &entry, pdMS_TO_TICKS(UDP_LOG_LOOP_INTERVAL)) != pdTRUE) {
                // Timeout: check for stop notification
                if (ulTaskNotifyTake(pdTRUE, 0) > 0) { _udpTaskShouldRun = false; break; }
                continue;
            }

            // If not connected or UDP not initialized, requeue to front and wait
            if (!_isUdpInitialized || !CustomWifi::isFullyConnected()) {
                xQueueSendToFront(_udpLogQueue, &entry, 0);
                delay(DELAY_SEND_UDP);
                continue;
            }

            // Prepare timestamp
            char timestamp[TIMESTAMP_ISO_BUFFER_SIZE];
            AdvancedLogger::getTimestampIsoUtcFromUnixTimeMilliseconds(
                entry.unixTimeMilliseconds,
                timestamp,
                sizeof(timestamp)
            );

            // Format message
            snprintf(_udpBuffer, sizeof(_udpBuffer),
                "<%d>%s %s[%llu]: [%s][Core%d] %s[%s]: %s",
                UDP_LOG_SERVERITY_FACILITY,
                timestamp,
                DEVICE_ID,
                entry.millis,
                AdvancedLogger::logLevelToString(entry.level),
                entry.coreId,
                entry.file,
                entry.function,
                entry.message);
            
            if (!_udpClient.beginPacket(_udpDestinationIp, UDP_LOG_PORT)) {
                // Put the log back in the queue (front of queue)
                // if (_udpLogQueue) xQueueSendToFront(_udpLogQueue, &entry, 0);
                delay(DELAY_SEND_UDP);
                continue;
            }
            
            size_t bytesWritten = _udpClient.write((const uint8_t*)_udpBuffer, strlen(_udpBuffer));
            if (bytesWritten == 0) {
                // if (_udpLogQueue) xQueueSendToFront(_udpLogQueue, &entry, 0);
                _udpClient.endPacket(); // Clean up the packet
                delay(DELAY_SEND_UDP);
                continue;
            }
            
            if (!_udpClient.endPacket()) {
                // if (_udpLogQueue) xQueueSendToFront(_udpLogQueue, &entry, 0);
                delay(DELAY_SEND_UDP);
                continue;
            }
        }

        // Removed LOG_DEBUG here to avoid recursive logging
        _udpTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    TaskInfo getTaskInfo()
    {
        if (_udpTaskHandle != NULL) {
            return TaskInfo(UDP_LOG_TASK_STACK_SIZE, uxTaskGetStackHighWaterMark(_udpTaskHandle));
        } else {
            return TaskInfo(); // Return empty/default TaskInfo if task is not running
        }
    }
}