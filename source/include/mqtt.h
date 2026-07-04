// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <StreamUtils.h>
#include <WiFiClientSecure.h>
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "ade7953.h"
#include "awsconfig.h"
#include "constants.h"
#include "factory_keys.h"
#include "customtime.h"
#include "customwifi.h"
#include "customlog.h"
#include "globals.h"
#include "structs.h"
#include "utils.h"

#define MQTT_TASK_NAME "mqtt_task"
#define MQTT_TASK_STACK_SIZE (7 * 1024) // Around 6 kB usage
#define MQTT_TASK_PRIORITY 1 // Below AsyncTCP/web (priority 3) so user-facing requests preempt cloud publishing

#define MQTT_LOG_QUEUE_SIZE (256 * 1024) // Generous log size (in bytes) thanks to PSRAM
#define MQTT_METER_QUEUE_SIZE (64 * 1024) // Size in bytes to allocate to PSRAM
#define MQTT_GRID_QUEUE_SIZE (64 * 1024) // Size in bytes to allocate to PSRAM (~34 min of 500 ms points); overflow drops oldest
#define QUEUE_WAIT_TIMEOUT 100 // Amount of milliseconds to wait if the queue is full or busy

// AWS IoT Jobs OTA constants
#define OTA_TASK_NAME "ota_task"
#define OTA_TASK_STACK_SIZE (12 * 1024) // Has to be big to allow for the presigned S3 URL to be handled
#define OTA_TASK_PRIORITY 5
#define OTA_STATUS_CHECK_INTERVAL (1 * 1000)
#define OTA_HTTPS_BUFFER_SIZE_TX (2 * 1024)
#define OTA_PRESIGNED_URL_BUFFER_SIZE (4 * 1024) // The presigned S3 URL can be very long
#define MQTT_OTA_SIZE_REPORT_UPDATE (128 * 1024)

// OTA validation constants
#define OTA_VALIDATION_TASK_NAME "ota_validation_task"
#define OTA_VALIDATION_TASK_STACK_SIZE (6 * 1024)
#define OTA_VALIDATION_TASK_PRIORITY 2
#define OTA_VALIDATION_TIMEOUT (5 * 60 * 1000) // Stable operation before marking OTA as successful
#define OTA_VALIDATION_CHECK_INTERVAL (10 * 1000) // Check periodically during validation period
#define MQTT_PREFERENCES_OTA_JOB_ID_KEY "ota_job_id"
#define MQTT_PREFERENCES_OTA_PENDING_KEY "ota_pending"
#define MQTT_PREFERENCES_OTA_EXPECTED_SHA256_KEY "ota_sha256" // Expected firmware SHA256 for validation

// MQTT buffer sizes - all moved to PSRAM for better memory utilization
// 9 KB sized for the worst-case inbound shadow /update/delta (changed desired
// fields + per-field metadata) now that /update/accepted is intentionally not
// subscribed. This static PubSubClient RX/TX buffer lives in internal RAM and
// adds pressure at the TLS handshake (see the 2.0.0 esp-aes alloc memory) -
// verify free internal heap after connect on real hardware.
#define MQTT_BUFFER_SIZE (9 * 1024)
#define MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE (32 * 1024) // PSRAM buffer for MQTT subscribe messages (reduced for efficiency)
#define CERTIFICATE_BUFFER_SIZE (16 * 1024)   // PSRAM buffer for certificate storage (was 4KB)
#define MINIMUM_CERTIFICATE_LENGTH 128 // Minimum length for valid certificates (to avoid empty strings)
#define CORE_DUMP_CHUNK_SIZE (4 * 1024) // Do not exceed 4kB to avoid stability issues

#define DEFAULT_CLOUD_SERVICES_ENABLED false // Always off by default, and enabled only explicitly by the user
#define DEFAULT_SEND_POWER_DATA_ENABLED true // Send all the data by default
#define DEFAULT_SEND_GRID_DATA_ENABLED false // Off by default; the cloud enables it per device
#define DEFAULT_MQTT_LOG_LEVEL_INT 2 // Default minimum log level for MQTT publishing (INFO = 2)

#define MQTT_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define MQTT_MAX_INTERVAL_GRID_PUBLISH (60 * 1000) // Batch interval for grid points (no need for fast updates)
#ifdef ENV_DEV
// In dev: send system_dynamic and statistics every minute so post-mortem
// telemetry has the resolution needed to investigate behavior.
#define MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH (60 * 1000)
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (60 * 1000)
#else
#define MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH (60 * 60 * 1000)  // 1 hour since the data does not change frequently (and sent on reboot/reconnection anyway)
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (6 * 60 * 60 * 1000)  // 6 hours since they are cumulative counters (and sent on reboot/reconnection anyway)
#endif

#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)

#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks
#define MQTT_METER_ESTIMATED_PER_ENTRY 35 // Estimated size in bytes of each meter entry (unix ms, channel, active power, pf)
#define MQTT_GRID_FREQUENCY_PAYLOAD_DECIMALS 4 // 0.1 mHz: preserves the ~0.8 mHz EMA resolution
#define MQTT_GRID_VOLTAGE_PAYLOAD_DECIMALS 1
#define MQTT_METER_ESTIMATED_ENERGY_VOLTAGE_OVERHEAD_BYTES 500 // Estimated overhead in bytes for energy and voltage data in the payload
#define AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE (5 * 1024) // This is the minimum billable size for AWS IoT Core, so it makes little sense to send smaller payloads
#define AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT (128 * 1024) // Limit of AWS
#define MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER 0.95 // Multiplier to avoid reaching exactly the limit

#define MQTT_INITIAL_RETRY_INTERVAL (15 * 1000) // Base delay for exponential backoff in milliseconds
#define MQTT_MAX_RETRY_INTERVAL (60 * 60 * 1000) // Maximum delay for exponential backoff in milliseconds
#define MQTT_RETRY_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_MAX_CONNECTION_ATTEMPTS 10 // Maximum number of connection attempts before restarting the device. High since we don't want a reboot cycle
#define MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY "en_cloud"
#define MQTT_PREFERENCES_SEND_POWER_DATA_KEY "send_power"
#define MQTT_PREFERENCES_SEND_GRID_DATA_KEY "send_grid"
#define MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY "log_level_int"
#define MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY "transient_log" // active transient (VERBOSE/DEBUG) level, persisted so a debug session survives a reboot; absent = none

// MQTT topic suffixes (application-level; see awsconfig.h for the namespace prefix)
// Publish topics. system/static + channel retired (-> info + channels shadows);
// configurable state lives in shadows, telemetry topics carry runtime data only.
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_GRID "grid"
#define MQTT_TOPIC_SYSTEM_DYNAMIC "system/dynamic"
#define MQTT_TOPIC_STATISTICS "statistics"
#define MQTT_TOPIC_CRASH "crash"
#define MQTT_TOPIC_LOG "log"
// Subscribe topics. The legacy `command` topic is retired (-> IoT Commands +
// system shadow); only AWS IoT Jobs (OTA) and shadow/command reserved topics remain.
#define MQTT_TOPIC_SUBSCRIBE_JOBS "jobs"
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// AWS IoT Commands (transient operations): reject any request older than this.
#define COMMAND_MAX_AGE_SECONDS 300 // 5 minutes

// AWS IoT Commands subscription gate (compile-time feature flag).
// The device subscribes to "$aws/commands/things/<id>/executions/+/request/json"
// - '+' is the execution-id wildcard only; the payload-format segment is the
// concrete "json", never a '+'. An earlier build used '+' at the payload-format
// position, an unsupported reserved-topic subscribe that made the broker drop the
// session with CLIENT_ERROR (~1.2 s reconnect storm, observed on dev .174). The
// corrected topic is safe to subscribe even before a dispatcher exists. For the
// device to RECEIVE commands, the cloud must create them with contentType
// application/json (so AWS publishes to .../request/json, matching this subscribe).
#define MQTT_IOT_COMMANDS_SUBSCRIBE_ENABLED true

struct PublishMqtt
{
  bool meter;
  bool grid;
  bool systemDynamic;
  bool statistics;
  bool crash;
  bool requestOta;

  PublishMqtt() :
    meter(false), // Need to fill queue first
    grid(false),  // Need to fill queue first
    systemDynamic(true),
    statistics(true),
    crash(false), // May not be present
    requestOta(true) {} // Always require on connection
};

namespace Mqtt
{
    void begin();
    void stop();

    // Cloud services methods
    void setCloudServicesEnabled(bool enabled);
    bool isCloudServicesEnabled();
    bool isConnected(); // Connection fact for the issue registry (updated once per task loop)
    
    // Public methods for requesting MQTT publications
    void requestChannelPublish();
    void requestCrashPublish();
    
    // Public methods for pushing data to queues
    void pushLog(const LogEntry& entry);
    void pushMeter(const PayloadMeter& payload);
    void pushGrid(const PayloadGridPoint& point);

    TaskInfo getMqttTaskInfo();
    TaskInfo getMqttOtaTaskInfo();

    // Shadow module helpers: subscribe/publish on reserved $aws/things/<id>/...
    // topics (the shadow module builds the "shadow/name/<name>/..." suffix).
    bool subscribeReservedThings(const char* finalTopic);
    bool publishReservedThings(JsonDocument& jsonDocument, const char* finalTopic, bool retain = false);

    // Config accessors used by the system shadow (04).
    int  getMqttLogLevel();                  // current runtime level int (0..5)
    void setMqttLogLevel(const char* level); // persisted baseline (validates the name)
    void setRuntimeLogLevel(int level);      // runtime only - NOT persisted (transient verbose)
    // Transient (VERBOSE/DEBUG) reboot-restore marker. Persisted separately from
    // the baseline so a debug session keeps boot-time logs across a reboot.
    void saveTransientLogLevel(int level);   // persist active transient level (no-op if unchanged)
    void clearTransientLogLevel();           // clear the marker (revert / persistent set)
    int  getTransientLogLevel();             // persisted transient level, or -1 if none
    bool getSendPowerData();
    void setSendPowerData(bool enabled);     // persisted
    bool getSendGridData();
    void setSendGridData(bool enabled);      // persisted; cloud-settable via the system shadow

#ifdef ENV_DEV
    // Dev-only: inject a synthetic IoT Command execution through the real handler
    // (staged from the caller's task, dispatched on the MQTT task). Never in prod.
    void injectCommandExecution(const char* executionId, const char* payload);
#endif
}