#pragma once


// System includes
#include <Arduino.h>
#include <WiFiUdp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Project includes
#include "mqtt.h"
#include "customwifi.h"
#include "structs.h"
#include "constants.h"
#include "utils.h"


#define UDP_LOG_SERVERITY_FACILITY 16 // Standard syslog facility for local0.info
#define UDP_LOG_PORT 514 // Standard syslog port
#define UDP_LOG_BUFFER_SIZE 1024 // Smaller buffer for UDP packets (not critical, but should be enough for most messages). Increased thanks to PSRAM
#define DEFAULT_UDP_LOG_DESTINATION_IP "239.255.255.250" // Multicast IP for UDP logging

#define LOG_QUEUE_SIZE (32 * 1024) // Callback queue size in bytes (length will be computed based on this). Can be set high thanks to PSRAM
#define LOG_CALLBACK_LEVEL_SIZE 8 // Size for log level (e.g., "info", "error")
#define LOG_CALLBACK_FUNCTION_SIZE 16 // Size for function name
#define LOG_ESPVPRINTF_CALLBACK_MESSAGE_SIZE 256 // Size for log message coming from ESP-IDF. They are small usually so 256 is enough

#define DELAY_SEND_UDP 10 // Millisecond delay between UDP sends to avoid flooding the network and starving

// Task configuration for async UDP sender
#define UDP_LOG_TASK_NAME "udp_log_task"
#define UDP_LOG_TASK_STACK_SIZE (4 * 1024)
#define UDP_LOG_TASK_PRIORITY 1
#define UDP_LOG_LOOP_INTERVAL 100

namespace CustomLog
{
    void begin();
    void stop();
    
    void callbackMultiple(const LogEntry& entry);

    // Task information
    TaskInfo getTaskInfo();
}