

#include "customlog.h"

static const char *TAG = "customlog";

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
    static bool _isQueueInitialized = false;

    static void _callbackMqtt();
    static void _callbackUdp();
    static bool _initializeQueue();

    static bool _initializeQueue() // Cannot use logger here to avoid circular dependency
    {
        if (_isQueueInitialized) {
            return true;
        }

        // Allocate queue storage in PSRAM
        size_t queueStorageSize = LOG_BUFFER_SIZE * sizeof(LogJson);
        _udpLogQueueStorage = (uint8_t*)heap_caps_malloc(queueStorageSize, MALLOC_CAP_SPIRAM);
        
        if (_udpLogQueueStorage == nullptr) {
            Serial.printf("[ERROR] Failed to allocate PSRAM for UDP log queue (%d bytes) | Free PSRAM: %d bytes\n", queueStorageSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            return false;
        }

        // Create FreeRTOS static queue using PSRAM buffer
        _udpLogQueue = xQueueCreateStatic(LOG_BUFFER_SIZE, sizeof(LogJson), _udpLogQueueStorage, &_udpLogQueueStruct);
        if (_udpLogQueue == nullptr) {
            Serial.printf("[ERROR] Failed to create UDP log queue\n");
            heap_caps_free(_udpLogQueueStorage);
            _udpLogQueueStorage = nullptr;
            return false;
        }

        _isQueueInitialized = true;
        Serial.printf("[DEBUG] UDP log queue initialized with PSRAM buffer (%d bytes) | Free PSRAM: %d bytes\n", queueStorageSize, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return true;
    }

    void begin()
    {
        if (_isUdpInitialized) {
            logger.debug("UDP logging already initialized", TAG);
            return;
        }

        // Initialize queue if not already done
        if (!_initializeQueue()) {
            logger.error("Failed to initialize UDP log queue", TAG);
            return;
        }

        // Initialize UDP destination IP from configuration
        _udpDestinationIp.fromString(DEFAULT_UDP_LOG_DESTINATION_IP);
        
        _udpClient.begin(UDP_LOG_PORT);
        _isUdpInitialized = true;
        
        size_t queueStorageSize = LOG_BUFFER_SIZE * sizeof(LogJson);
        logger.debug("UDP logging configured - destination: %s:%d, PSRAM buffer: %zu bytes", TAG,
                    _udpDestinationIp.toString().c_str(), UDP_LOG_PORT, queueStorageSize);
    }

    void stop()
    {
        if (_isUdpInitialized) {
            _udpClient.stop();
            _isUdpInitialized = false;
            logger.debug("UDP client stopped", TAG);
        }
        
        // Clean up the queue and PSRAM buffer
        if (_isQueueInitialized) {
            _udpLogQueue = nullptr;
            
            // Free PSRAM buffer
            if (_udpLogQueueStorage != nullptr) {
                heap_caps_free(_udpLogQueueStorage);
                _udpLogQueueStorage = nullptr;
            }
            
            _isQueueInitialized = false;
            logger.debug("UDP logging queue stopped, PSRAM freed", TAG);
        }
    }

    static void _callbackMqtt(
        const char* timestamp,
        uint64_t millisEsp,
        const char* level,
        uint32_t coreId,
        const char* function,
        const char* message
    )
    {
        #if HAS_SECRETS
        Mqtt::pushLog(timestamp, millisEsp, level, coreId, function, message);
        #endif
    }

    static void _callbackUdp(
        const char* timestamp,
        uint64_t millisEsp,
        const char* level,
        uint32_t coreId,
        const char* function,
        const char* message
    )
    {
        if (!DEFAULT_IS_UDP_LOGGING_ENABLED) return;
        if (strcmp(level, "verbose") == 0) return; // Never send verbose logs via UDP
        
        // Initialize queue on first use if not already done
        if (!_isQueueInitialized) {
            if (!_initializeQueue()) {
                return; // Failed to initialize, drop this log
            }
        }
        
        // Create log entry
        LogJson logEntry(timestamp, millisEsp, level, coreId, function, message);
        
        // Try to send to queue (non-blocking)
        if (xQueueSend(_udpLogQueue, &logEntry, 0) != pdTRUE) {
            // Queue is full, could optionally remove oldest item and try again
            // For now, just drop the log entry
        }

        // If not connected to WiFi or no valid IP or UDP not initialized, return (log is still stored in queue for later)
        if (!_isUdpInitialized) return;
        if (!CustomWifi::isFullyConnected()) return;

        uint32_t loops = 0;
        LogJson log;
        
        while (uxQueueMessagesWaiting(_udpLogQueue) > 0 && loops < MAX_LOOP_ITERATIONS) {
            loops++;

            // Receive from queue (non-blocking)
            if (xQueueReceive(_udpLogQueue, &log, 0) != pdTRUE) {
                break; // No more items in queue
            }

            // Format as simplified syslog message
            snprintf(_udpBuffer, sizeof(_udpBuffer),
                "<%d>%s %s[%llu]: [%s][Core%u] %s: %s",
                UDP_LOG_SERVERITY_FACILITY, // Facility.Severity (local0.info)
                log.timestamp,
                DEVICE_ID,
                log.millisEsp,
                log.level,
                log.coreId,
                log.function,
                log.message);
            
            if (!_udpClient.beginPacket(_udpDestinationIp, UDP_LOG_PORT)) {
                // Put the log back in the queue (front of queue)
                xQueueSendToFront(_udpLogQueue, &log, 0);
                break;
            }
            
            size_t bytesWritten = _udpClient.write((const uint8_t*)_udpBuffer, strlen(_udpBuffer));
            if (bytesWritten == 0) {
                xQueueSendToFront(_udpLogQueue, &log, 0);
                _udpClient.endPacket(); // Clean up the packet
                break;
            }
            
            if (!_udpClient.endPacket()) {
                xQueueSendToFront(_udpLogQueue, &log, 0);
                break;
            }
            
            // Small delay between UDP packets to avoid overwhelming the stack
            if (uxQueueMessagesWaiting(_udpLogQueue) > 0) {
                delay(DELAY_SEND_UDP);
            }
        }
    }

    void callbackMultiple(
        const char* timestamp,
        uint64_t millisEsp,
        const char* level,
        uint32_t coreId,
        const char* function,
        const char* message
    )
    {
        _callbackUdp(timestamp, millisEsp, level, coreId, function, message);
        _callbackMqtt(timestamp, millisEsp, level, coreId, function, message);
    }

}