#ifdef HAS_SECRETS
#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <StreamUtils.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "mbedtls/sha256.h"

#include "ade7953.h"
#include "binaries.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
#include "customlog.h"
#include "globals.h"
#include "structs.h"
#include "utils.h"

#define MQTT_TASK_NAME "mqtt_task"
#define MQTT_TASK_STACK_SIZE (8 * 1024)          // Reduced from 16KB since buffers moved to PSRAM
#define MQTT_TASK_PRIORITY 3

#define MQTT_LOG_QUEUE_SIZE (64 * 1024) // Generous log size (in bytes) thanks to PSRAM
#define MQTT_METER_QUEUE_SIZE (32 * 1024) // Size in bytes to allocate to PSRAM
#define MQTT_METER_QUEUE_ALMOST_FULL_THRESHOLD 0.10 // Threshold for publishing
#define MQTT_METER_MAX_BATCHES 10 // Number of consecutive batches to publish before stopping to avoid infinite loop
#define QUEUE_WAIT_TIMEOUT 100 // Amount of milliseconds to wait if the queue is full or busy

// MQTT buffer sizes - all moved to PSRAM for better memory utilization
#define MQTT_BUFFER_SIZE (5 * 1024) // Needs to be at least 4 kB for the certificates
#define MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE (32 * 1024) // PSRAM buffer for MQTT subscribe messages (reduced for efficiency)
#define CERTIFICATE_BUFFER_SIZE (16 * 1024)   // PSRAM buffer for certificate storage (was 4KB)
#define MINIMUM_CERTIFICATE_LENGTH 128 // Minimum length for valid certificates (to avoid empty strings)
#define ENCRYPTION_KEY_BUFFER_SIZE 64 // For encryption keys (preshared key + device ID)
#define CORE_DUMP_CHUNK_SIZE (4 * 1024) // Do not exceed 4kB to avoid stability issues

#ifdef ENV_PROD
#define DEFAULT_CLOUD_SERVICES_ENABLED true // In prod, enable by default
#else
#define DEFAULT_CLOUD_SERVICES_ENABLED false // Leave manual connection
#endif
#define DEFAULT_SEND_POWER_DATA_ENABLED true // Send all the data by default
#define DEFAULT_MQTT_LOG_LEVEL_INT 2 // Default minimum log level for MQTT publishing (INFO = 2)

#define MQTT_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH (15 * 60 * 1000) // The maximum interval between two system dynamic payloads
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (15 * 60 * 1000) // The interval between two statistics publish

#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)

#define MQTT_CLAIM_MAX_CONNECTION_PUBLISH_ATTEMPT 10 // The maximum number of attempts to connect or publish to AWS IoT Core MQTT broker for claiming certificates
#define MQTT_CLAIM_INITIAL_RETRY_INTERVAL (5 * 1000) // Base delay for exponential backoff in milliseconds
#define MQTT_CLAIM_MAX_RETRY_INTERVAL (60 * 60 * 1000) // Maximum delay for exponential backoff in milliseconds
#define MQTT_CLAIM_RETRY_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_CLAIM_TIMEOUT (30 * 1000) // Timeout for claiming certificates (in milliseconds)

#define MQTT_INITIAL_RECONNECT_INTERVAL (5 * 1000) // Initial interval for MQTT reconnection attempts
#define MQTT_MAX_RECONNECT_INTERVAL (5 * 60 * 1000) // Maximum interval for MQTT reconnection attempts
#define MQTT_RECONNECT_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks
#define MQTT_CLAIMING_INTERVAL (1 * 1000) // Interval between two MQTT claiming checks
#define AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT (128 * 1024) // Limit of AWS

#define MQTT_INITIAL_RETRY_INTERVAL (5 * 1000) // Base delay for exponential backoff in milliseconds
#define MQTT_MAX_RETRY_INTERVAL (60 * 60 * 1000) // Maximum delay for exponential backoff in milliseconds
#define MQTT_RETRY_MULTIPLIER 2 // Multiplier for exponential backoff

#define MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY "en_cloud"
#define MQTT_PREFERENCES_SEND_POWER_DATA_KEY "send_power"
#define MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY "log_level_int"
#define MQTT_PREFERENCES_FW_UPDATE_URL_KEY "url"
#define MQTT_PREFERENCES_FW_UPDATE_VERSION_KEY "version"

// Cloud services
// --------------------
// Basic ingest functionality
#define AWS_TOPIC "$aws"
#define MQTT_BASIC_INGEST AWS_TOPIC "/rules"

// Certificates path
#define PREFS_KEY_CERTIFICATE "certificate"
#define PREFS_KEY_PRIVATE_KEY "private_key"

// EnergyMe - Home | Custom MQTT topics
// Base topics
#define MQTT_TOPIC_1 "energyme"
#define MQTT_TOPIC_2 "home"

// Publish topics
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_SYSTEM_STATIC "system/static"
#define MQTT_TOPIC_SYSTEM_DYNAMIC "system/dynamic"
#define MQTT_TOPIC_CHANNEL "channel"
#define MQTT_TOPIC_STATISTICS "statistics"
#define MQTT_TOPIC_CRASH "crash"
#define MQTT_TOPIC_LOG "log"
#define MQTT_TOPIC_PROVISIONING_REQUEST "provisioning/request"

// Subscribe topics
#define MQTT_TOPIC_SUBSCRIBE_COMMAND "command"
#define MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE "provisioning/response"
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// AWS IoT Core endpoint
#define AWS_IOT_CORE_PORT 8883

struct PublishMqtt
{
  bool meter;
  bool systemDynamic;
  bool systemStatic;
  bool channel;
  bool statistics;
  bool crash;

  PublishMqtt() : 
    meter(false), // Need to fill queue first
    systemDynamic(true), 
    systemStatic(true), 
    channel(true), 
    statistics(true), 
    crash(false) {} // May not be present
};

namespace Mqtt
{
    void begin();
    void stop();

    // Cloud services methods
    void setCloudServicesEnabled(bool enabled);
    bool isCloudServicesEnabled();

    // Firmware update methods
    void getFirmwareUpdateVersion(char* buffer, size_t bufferSize);
    void getFirmwareUpdateUrl(char* buffer, size_t bufferSize);
    bool isLatestFirmwareInstalled();
    
    // Public methods for requesting MQTT publications
    void requestChannelPublish();
    void requestCrashPublish();
    
    // Public methods for pushing data to queues
    void pushLog(const LogEntry& entry);
    void pushMeter(const PayloadMeter& payload);

    TaskInfo getTaskInfo();
}
#endif