#pragma once


// System includes
#include <Arduino.h>
#include <WiFiUdp.h>
#include <CircularBuffer.hpp>

// Project includes
#include "mqtt.h"
#include "customwifi.h"
#include "structs.h"
#include "constants.h"
#include "utils.h"

// Constants for UDP logging
#define UDP_LOG_SERVERITY_FACILITY 16 // Standard syslog facility for local0.info
#define UDP_LOG_PORT 514 // Standard syslog port
#define UDP_LOG_BUFFER_SIZE 256 // Smaller buffer for UDP packets
#define DEFAULT_IS_UDP_LOGGING_ENABLED true
#define DEFAULT_UDP_LOG_DESTINATION_IP "239.255.255.250" // Multicast IP for UDP logging
#define LOG_BUFFER_SIZE 20 // Callback queue size
#define LOG_CALLBACK_LEVEL_SIZE 8 // Size for log level (e.g., "info", "error")
#define LOG_CALLBACK_FUNCTION_SIZE 32 // Size for function name
#define LOG_CALLBACK_MESSAGE_SIZE 128 // Size for log message

struct LogJson {
    char timestamp[TIMESTAMP_BUFFER_SIZE];
    unsigned long millisEsp;
    char level[LOG_CALLBACK_LEVEL_SIZE];
    unsigned int coreId;
    char function[LOG_CALLBACK_FUNCTION_SIZE];
    char message[LOG_CALLBACK_MESSAGE_SIZE];

    LogJson()
        : millisEsp(0), coreId(0) {
        snprintf(timestamp, sizeof(timestamp), "%s", "");
        snprintf(level, sizeof(level), "%s", "");
        snprintf(function, sizeof(function), "%s", "");
        snprintf(message, sizeof(message), "%s", "");
    }

    LogJson(const char* timestampIn, unsigned long millisEspIn, const char* levelIn, unsigned int coreIdIn, const char* functionIn, const char* messageIn)
        : millisEsp(millisEspIn), coreId(coreIdIn) {
        snprintf(timestamp, sizeof(timestamp), "%s", timestampIn ? timestampIn : "");
        snprintf(level, sizeof(level), "%s", levelIn ? levelIn : "");
        snprintf(function, sizeof(function), "%s", functionIn ? functionIn : "");
        snprintf(message, sizeof(message), "%s", messageIn ? messageIn : "");
    }
};

namespace CustomLog
{
    void setupUdp();
    void stopUdp();
    
    void callbackMultiple(
        const char* timestamp,
        unsigned long millisEsp,
        const char* level,
        unsigned int coreId,
        const char* function,
        const char* message
    );
}