#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "ade7953.h"
#include "binaries.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
#include "customlog.h"
#include "globals.h"
#include "structs.h"
#include "utils.h"

#define MQTT_TASK_STACK_SIZE (16 * 1024)
#define MQTT_TASK_PRIORITY 3
#define MQTT_LOG_QUEUE_SIZE 200 // Callback queue size - (high, only for development. Make lower evenually)
#define MQTT_METER_QUEUE_SIZE 50
#if HAS_SECRETS
#define DEFAULT_CLOUD_SERVICES_ENABLED true // If we compile with secrets, we might as well just directly connect
#define DEFAULT_SEND_POWER_DATA_ENABLED true // Send all the data by default
#define DEFAULT_DEBUG_LOGS_ENABLED false // Do not send debug logs by default
#else
#define DEFAULT_CLOUD_SERVICES_ENABLED false
#define DEFAULT_SEND_POWER_DATA_ENABLED false
#define DEFAULT_DEBUG_LOGS_ENABLED false
#endif
#define MQTT_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define MQTT_MAX_INTERVAL_STATUS_PUBLISH (60 * 60 * 1000) // The interval between two status publish
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (60 * 60 * 1000) // The interval between two statistics publish
#define MQTT_MAX_INTERVAL_CRASH_MONITOR_PUBLISH (60 * 60 * 1000) // The interval between two status publish
#define MQTT_CLAIM_MAX_CONNECTION_ATTEMPT 10 // The maximum number of attempts to connect to AWS IoT Core MQTT broker for claiming certificates
#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)
#define MQTT_MIN_CONNECTION_INTERVAL (5 * 1000) // Minimum interval between two connection attempts
#define MQTT_INITIAL_RECONNECT_INTERVAL (5 * 1000) // Initial interval for MQTT reconnection attempts
#define MQTT_MAX_RECONNECT_INTERVAL (5 * 60 * 1000) // Maximum interval for MQTT reconnection attempts
#define MQTT_RECONNECT_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks
#define MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS 100 // The maximum number of points that can be sent in a single payload. Going higher than about 150 leads to unstable connections
#define MQTT_PAYLOAD_LIMIT (16 * 1024) // Increase the base limit of 256 bytes. Increasing this over 32768 bytes will lead to unstable connections
#define MQTT_PROVISIONING_TIMEOUT (60 * 1000) // The timeout for the provisioning response
#define MQTT_PROVISIONING_LOOP_CHECK (1 * 1000) // Interval between two certificates check on memory
#define MQTT_DEBUG_LOGGING_DEFAULT_DURATION (3 * 60 * 1000) 
#define MQTT_DEBUG_LOGGING_MAX_DURATION (60 * 60 * 1000)

#define MQTT_PREFERENCES_NAMESPACE "mqtt_ns"
#define MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY "enableCloudServices"
#define MQTT_PREFERENCES_SEND_POWER_DATA_KEY "sendPowerData"

// Cloud services
// --------------------
// Basic ingest functionality
#define AWS_TOPIC "$aws"
#define MQTT_BASIC_INGEST AWS_TOPIC "/rules"

// Certificates path
#define PREFS_KEY_CERTIFICATE "certificate"
#define PREFS_KEY_PRIVATE_KEY "private_key"
#define KEY_SIZE 256

// EnergyMe - Home | Custom MQTT topics
// Base topics
#define MQTT_TOPIC_1 "energyme"
#define MQTT_TOPIC_2 "home"

// Publish topics
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_STATUS "status"
#define MQTT_TOPIC_METADATA "metadata"
#define MQTT_TOPIC_CHANNEL "channel"
#define MQTT_TOPIC_MONITOR "monitor"
#define MQTT_TOPIC_LOG "log"
#define MQTT_TOPIC_CONNECTIVITY "connectivity"
#define MQTT_TOPIC_STATISTICS "statistics"
#define MQTT_TOPIC_PROVISIONING_REQUEST "provisioning/request"

// Subscribe topics
#define MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE "firmware-update"
#define MQTT_TOPIC_SUBSCRIBE_RESTART "restart"
#define MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE "provisioning/response"
#define MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES "erase-certificates"
#define MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION "set-general-configuration"
#define MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING "enable-debug-logging" 
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// MQTT will
#define MQTT_WILL_QOS 1
#define MQTT_WILL_RETAIN true
#define MQTT_WILL_MESSAGE "{\"connectivity\":\"unexpected_offline\"}"

// AWS IoT Core endpoint
#define AWS_IOT_CORE_PORT 8883

#define MAGIC_WORD_RTC 0xDEADBEEF // This is crucial to ensure that the RTC data has sensible values or it is just some garbage after reboot

struct DebugFlagsRtc {
    bool enableMqttDebugLogging;
    unsigned long mqttDebugLoggingDurationMillis;
    unsigned long mqttDebugLoggingEndTimeMillis;
    unsigned int signature;
    // Since this struct will be used in an RTC_NOINIT_ATTR, we cannot initialize it in the constructor
};

struct PublishMqtt
{
  bool connectivity;
  bool meter;
  bool status;
  bool metadata;
  bool channel;
  bool statistics;

  // Set default to true to publish everything on first connection (except meter which needs to gather data first)
  PublishMqtt() : connectivity(true), meter(false), status(true), metadata(true), channel(true), statistics(true) {}
};

namespace Mqtt
{
    void begin();
    void stop();

    // Configuration methods
    void setCloudServicesEnabled(bool enabled);
    bool isCloudServicesEnabled();
    void getFirmwareUpdatesVersion(char* buffer, size_t bufferSize);
    void getFirmwareUpdatesUrl(char* buffer, size_t bufferSize);
    
    // Public methods for requesting MQTT publications
    void requestConnectivityPublish();
    void requestMeterPublish();
    void requestStatusPublish();
    void requestMetadataPublish();
    void requestChannelPublish();
    void requestStatisticsPublish();
    
    // Public methods for pushing data to queues
    void pushLog(const char* timestamp, unsigned long millisEsp, const char* level, unsigned int coreId, const char* function, const char* message);
    void pushMeter(const PayloadMeter& payload);
}