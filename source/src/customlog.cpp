

#include "customlog.h"

static const char *TAG = "logcallback";

namespace CustomLog
{
    // Static variables - all internal to this module
    static QueueHandle_t _udpLogQueue = nullptr;
    static WiFiUDP _udpClient;
    static IPAddress _udpDestinationIp;
    static char _udpBuffer[UDP_LOG_BUFFER_SIZE];
    static bool _isUdpInitialized = false;

    static void _callbackMqtt();
    static void _callbackUdp();

    void setupUdp()
    {
        if (_isUdpInitialized) {
            logger.debug("UDP logging already initialized", TAG);
            return;
        }

        // Create FreeRTOS queue for UDP logs
        _udpLogQueue = xQueueCreate(LOG_BUFFER_SIZE, sizeof(LogJson));
        if (_udpLogQueue == nullptr) {
            logger.error("Failed to create UDP log queue", TAG);
            return;
        }

        // Initialize UDP destination IP from configuration
        _udpDestinationIp.fromString(DEFAULT_UDP_LOG_DESTINATION_IP);
        
        _udpClient.begin(UDP_LOG_PORT);
        _isUdpInitialized = true;
        
        logger.info("UDP logging configured - destination: %s:%d", TAG, 
                    _udpDestinationIp.toString().c_str(), UDP_LOG_PORT);
    }

    void stopUdp()
    {
        if (_isUdpInitialized) {
            _udpClient.stop();
            _isUdpInitialized = false;
            
            // Clean up the queue
            if (_udpLogQueue != nullptr) {
                vQueueDelete(_udpLogQueue);
                _udpLogQueue = nullptr;
            }
            
            logger.debug("UDP logging stopped", TAG);
        }
    }

    void _callbackMqtt(
        const char* timestamp,
        unsigned long millisEsp,
        const char* level,
        unsigned int coreId,
        const char* function,
        const char* message
    )
    {
        // Use the new queue-based approach
        Mqtt::pushLog(timestamp, millisEsp, level, coreId, function, message);
    }

    void _callbackUdp(
        const char* timestamp,
        unsigned long millisEsp,
        const char* level,
        unsigned int coreId,
        const char* function,
        const char* message
    )
    {
        if (!DEFAULT_IS_UDP_LOGGING_ENABLED) return;
        if (strcmp(level, "verbose") == 0) return; // Never send verbose logs via UDP
        
        // Create log entry
        LogJson logEntry(timestamp, millisEsp, level, coreId, function, message);
        
        // Even though the initialization has not been done yet, we can still push the logs
        if (_udpLogQueue != nullptr) {
            // Try to send to queue (non-blocking)
            if (xQueueSend(_udpLogQueue, &logEntry, 0) != pdTRUE) {
                // Queue is full, could optionally remove oldest item and try again
                // For now, just drop the log entry
            }
        }

        if (!_isUdpInitialized) return;

        // If not connected to WiFi or no valid IP, return (log is still stored in queue for later)
        if (!CustomWifi::isFullyConnected()) return;

        unsigned int _loops = 0;
        LogJson _log;
        
        while (uxQueueMessagesWaiting(_udpLogQueue) > 0 && _loops < MAX_LOOP_ITERATIONS) {
            _loops++;

            // Receive from queue (non-blocking)
            if (xQueueReceive(_udpLogQueue, &_log, 0) != pdTRUE) {
                break; // No more items in queue
            }

            // Format as simplified syslog message
            snprintf(_udpBuffer, sizeof(_udpBuffer),
                "<%d>%s %s[%lu]: [%s][Core%u] %s: %s",
                UDP_LOG_SERVERITY_FACILITY, // Facility.Severity (local0.info)
                _log.timestamp,
                DEVICE_ID,
                _log.millisEsp,
                _log.level,
                _log.coreId,
                _log.function,
                _log.message);
            
            if (!_udpClient.beginPacket(_udpDestinationIp, UDP_LOG_PORT)) {
                // Put the log back in the queue (front of queue)
                xQueueSendToFront(_udpLogQueue, &_log, 0);
                break;
            }
            
            size_t bytesWritten = _udpClient.write((const unsigned char*)_udpBuffer, strlen(_udpBuffer));
            if (bytesWritten == 0) {
                xQueueSendToFront(_udpLogQueue, &_log, 0);
                _udpClient.endPacket(); // Clean up the packet
                break;
            }
            
            if (!_udpClient.endPacket()) {
                xQueueSendToFront(_udpLogQueue, &_log, 0);
                break;
            }
            
            // Small delay between UDP packets to avoid overwhelming the stack
            if (uxQueueMessagesWaiting(_udpLogQueue) > 0) {
                delayMicroseconds(100); // 0.1ms delay
            }
        }
    }

    void callbackMultiple(
        const char* timestamp,
        unsigned long millisEsp,
        const char* level,
        unsigned int coreId,
        const char* function,
        const char* message
    )
    {
        _callbackUdp(timestamp, millisEsp, level, coreId, function, message);
        _callbackMqtt(timestamp, millisEsp, level, coreId, function, message);
    }

}