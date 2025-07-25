

#include "customlog.h"

static const char *TAG = "logcallback";

namespace CustomLog
{
    // Static variables - all internal to this module
    static CircularBuffer<LogJson, LOG_BUFFER_SIZE> _udpLogBuffer;
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
        if (!DEFAULT_IS_UDP_LOGGING_ENABLED || !_isUdpInitialized) return;
        if (strcmp(level, "verbose") == 0) return; // Never send verbose logs via UDP
        
        // Skip UDP logging if heap is critically low to prevent death spiral
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) return;

        _udpLogBuffer.push(
            LogJson(
                timestamp,
                millisEsp,
                level,
                coreId,
                function,
                message
            )
        );

        // If not connected to WiFi or no valid IP, return (log is still stored in circular buffer for later)
        if (!CustomWifi::isFullyConnected()) return;

        unsigned int _loops = 0;
        while (!_udpLogBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
            _loops++;

            LogJson _log = _udpLogBuffer.shift();

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
                _udpLogBuffer.push(_log);
                break;
            }
            
            size_t bytesWritten = _udpClient.write((const uint8_t*)_udpBuffer, strlen(_udpBuffer));
            if (bytesWritten == 0) {
                _udpLogBuffer.push(_log);
                _udpClient.endPacket(); // Clean up the packet
                break;
            }
            
            if (!_udpClient.endPacket()) {
                _udpLogBuffer.push(_log);
                break;
            }
            
            // Small delay between UDP packets to avoid overwhelming the stack
            if (!_udpLogBuffer.isEmpty()) {
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