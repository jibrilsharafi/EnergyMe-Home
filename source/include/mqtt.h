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

// High-priority payload, checked ahead of the log queue and shadow publish in
// _handleConnectedState() - see Mqtt::pushAlarm(). Alarms are rare (issue raise
// edges only); sized generously anyway since PSRAM is cheap.
#define MQTT_ALARM_QUEUE_SIZE (4 * 1024) // Size in bytes to allocate to PSRAM
#define MQTT_ALARM_TYPE_BUFFER_SIZE 32 // Short machine-readable identifier, e.g. "zero_crossing_timeout" - not a free-text message
#define MQTT_ALARM_EVENT_ID_BUFFER_SIZE 17 // 16-char lowercase-hex token (see generateHexToken, utils.h) + null terminator

// AWS IoT Jobs OTA constants
#define OTA_TASK_NAME "ota_task"
#define OTA_TASK_STACK_SIZE (12 * 1024) // Has to be big to allow for the presigned S3 URL to be handled
#define OTA_TASK_PRIORITY 5
#define OTA_HTTPS_BUFFER_SIZE_TX (2 * 1024)
#define OTA_PRESIGNED_URL_BUFFER_SIZE (4 * 1024) // The presigned S3 URL can be very long
#define OTA_SIGNATURE_HASH_CHUNK_SIZE 4096 // Chunk size for streaming the downloaded partition through SHA-256 before signature verification
#define OTA_PARTITION_SCRUB_SIZE 4096 // One flash sector: erasing the image header is enough to make a rejected image fail esp_image_verify
#define MQTT_OTA_SIZE_REPORT_UPDATE (128 * 1024)

// OTA download retry schedule. The presigned S3 URL is minted when the device
// picks up the job and lives 60 min, so the whole schedule has to fit inside
// that from the moment the download starts. Delays are 2, 4, 8 and 15 min
// (the fourth doubling is clamped), i.e. 29 min of waiting; with the attempts
// themselves the worst case lands near 44 min, leaving ~15 min of margin.
// There is deliberately no elapsed-time guard on the total; see _otaTask for
// how a URL that has expired mid-schedule is detected and cut short instead.
#define OTA_DOWNLOAD_MAX_ATTEMPTS 5
#define OTA_DOWNLOAD_RETRY_INITIAL_INTERVAL (2 * 60 * 1000)
#define OTA_DOWNLOAD_RETRY_MAX_INTERVAL (15 * 60 * 1000)
#define OTA_DOWNLOAD_RETRY_MULTIPLIER 2
// Widest value is "<bytes>/<bytes>"; AWS caps a statusDetails value at 1024.
#define OTA_STATUS_DETAIL_VALUE_BUFFER_SIZE 24

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
// Bytes the "coreDump" key, its quotes and the separating comma add on top of
// the already-measured metadata, when sizing a crash message against the
// publish limit
#define CRASH_PUBLISH_COREDUMP_FIELD_OVERHEAD sizeof(",\"coreDump\":\"\"")

#define DEFAULT_CLOUD_SERVICES_ENABLED false // Always off by default, and enabled only explicitly by the user
#define DEFAULT_SEND_POWER_DATA_ENABLED true // Send all the data by default
#define DEFAULT_SEND_GRID_DATA_ENABLED false // Off by default; the cloud enables it per device
#define DEFAULT_MQTT_LOG_LEVEL_INT 2 // Default minimum log level for MQTT publishing (INFO = 2)

#define MQTT_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define MQTT_GRID_PUBLISH_ALIGN_SECONDS 60 // Grid batches publish aligned to wall-clock minute boundaries, not a relative interval
#define MQTT_ENERGY_PUBLISH_ALIGN_SECONDS 60 // Energy snapshots publish aligned to wall-clock minute boundaries, not a relative interval
// Max wait past the boundary for every active channel to cross it before publishing anyway (see
// MqttEnergyPublishGate). Tied to CHANNEL_MAX_GAP_MS (ade7953.h) - the WDRR watchdog's own guarantee
// on the longest gap between reads of any channel - rather than a separate literal, so the two never
// drift apart: this deadline can never fire before the scheduler's own worst-case has had a chance to catch up.
#define MQTT_ENERGY_PUBLISH_DEADLINE_SECONDS (CHANNEL_MAX_GAP_MS / 1000)
#ifdef ENV_DEV
// In dev: send system_dynamic and statistics every minute so post-mortem
// telemetry has the resolution needed to investigate behavior.
#define MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH (60 * 1000)
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (60 * 1000)
#else
#define MQTT_MAX_INTERVAL_SYSTEM_DYNAMIC_PUBLISH (60 * 60 * 1000)  // 1 hour since the data does not change frequently (and sent on reboot/reconnection anyway)
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (6 * 60 * 60 * 1000)  // 6 hours since they are cumulative counters (and sent on reboot/reconnection anyway)
#endif

// Pace for draining the crash archive, one record per cycle. Also the retry
// pace: a transient publish failure costs one interval, not every crash publish
// until the next reboot.
#define MQTT_MAX_INTERVAL_CRASH_PUBLISH (60 * 1000)
// Records a single cycle will step over before giving up. Bounds the loop when
// every record at the head of the archive is unpublishable.
#define CRASH_PUBLISH_MAX_ATTEMPTS_PER_CYCLE 4

#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)

#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks

// Notification bit alongside TASK_NOTIFY_SHUTDOWN_BIT (constants.h) that wakes
// the MQTT task immediately instead of waiting up to MQTT_LOOP_INTERVAL - see
// requestImmediatePublish() and its caller pushAlarm().
#define MQTT_NOTIFY_WAKE_BIT (1 << 1)
#define MQTT_METER_ESTIMATED_PER_ENTRY 35 // Estimated size in bytes of each meter entry (unix ms, channel, active power, pf)
#define MQTT_METER_QUEUE_ALMOST_FULL_RATIO 0.9 // Force a publish attempt once the meter queue is this fraction full, regardless of the byte/interval trigger, so pushMeter() doesn't silently drop the oldest entry
#define MQTT_GRID_FREQUENCY_PAYLOAD_DECIMALS 4 // 0.1 mHz: preserves the ~0.8 mHz EMA resolution
#define MQTT_GRID_VOLTAGE_PAYLOAD_DECIMALS 1
#define AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE (5 * 1024) // This is the minimum billable size for AWS IoT Core, so it makes little sense to send smaller payloads
#define AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT (128 * 1024) // Limit of AWS
#define MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER 0.95 // Multiplier to avoid reaching exactly the limit

// Meter (power-only) publish cadence: shadow-configurable via the `system`
// shadow (meter_publish_threshold_bytes / meter_publish_max_interval_ms), so
// cadence can be dialed down for near-real-time viewing or up for
// cost-efficient batching without a reflash. Key names are unchanged from
// before the meter/energy topic split (see split-meter-energy-topic); their
// scope is now power points only - voltage and per-channel energy counters
// publish separately on their own wall-clock-aligned cadence (see
// MQTT_ENERGY_PUBLISH_ALIGN_SECONDS). Defaults match today's fixed behavior;
// bounds guard against a bad/forgotten write causing a publish storm or an
// oversized payload (see openspec/changes/configurable-meter-publish-rate).
#define MQTT_METER_PUBLISH_THRESHOLD_BYTES_DEFAULT AWS_IOT_CORE_MQTT_PAYLOAD_MINIMUM_BILLABLE
#define MQTT_METER_PUBLISH_THRESHOLD_BYTES_MIN 256 // Small enough to allow near-real-time publishing, big enough to be a meaningful trigger
#define MQTT_METER_PUBLISH_THRESHOLD_BYTES_MAX ((uint32_t)(AWS_IOT_CORE_MQTT_PAYLOAD_LIMIT * MQTT_METER_PAYLOAD_THRESHOLD_MULTIPLIER))
#define MQTT_METER_PUBLISH_MAX_INTERVAL_MS_DEFAULT MQTT_MAX_INTERVAL_METER_PUBLISH
#define MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MIN MQTT_LOOP_INTERVAL // Below the task's own poll cadence, a smaller value has no effect
#define MQTT_METER_PUBLISH_MAX_INTERVAL_MS_MAX (24UL * 60 * 60 * 1000) // 24 h ceiling - a long interval is just "batch a lot"

#define MQTT_INITIAL_RETRY_INTERVAL (15 * 1000) // Base delay for exponential backoff in milliseconds
#define MQTT_MAX_RETRY_INTERVAL (60 * 60 * 1000) // Maximum delay for exponential backoff in milliseconds
#define MQTT_RETRY_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_MAX_CONNECTION_ATTEMPTS 10 // Maximum number of connection attempts before restarting the device. High since we don't want a reboot cycle
#define MQTT_PREFERENCES_IS_CLOUD_SERVICES_ENABLED_KEY "en_cloud"
#define MQTT_PREFERENCES_SEND_POWER_DATA_KEY "send_power"
#define MQTT_PREFERENCES_SEND_GRID_DATA_KEY "send_grid"
#define MQTT_PREFERENCES_METER_PUBLISH_THRESHOLD_KEY "meter_pub_thr"
#define MQTT_PREFERENCES_METER_PUBLISH_INTERVAL_KEY "meter_pub_int"
#define MQTT_PREFERENCES_MQTT_LOG_LEVEL_KEY "log_level_int"
#define MQTT_PREFERENCES_TRANSIENT_LOG_LEVEL_KEY "transient_log" // active transient (VERBOSE/DEBUG) level, persisted so a debug session survives a reboot; absent = none

// MQTT topic suffixes (application-level; see awsconfig.h for the namespace prefix)
// Publish topics. system/static + channel retired (-> info + channels shadows);
// configurable state lives in shadows, telemetry topics carry runtime data only.
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_GRID "grid"
#define MQTT_TOPIC_ENERGY "energy"
#define MQTT_TOPIC_SYSTEM_DYNAMIC "system/dynamic"
#define MQTT_TOPIC_STATISTICS "statistics"
#define MQTT_TOPIC_CRASH "crash"
#define MQTT_TOPIC_LOG "log"
#define MQTT_TOPIC_ALARM "alarm" // Routed via its own rule (AWS_IOT_CORE_RULE_ALARM, awsconfig.h); requires that rule to exist server-side
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
  bool energy;
  bool systemDynamic;
  bool statistics;
  bool crash;
  bool requestOta;

  PublishMqtt() :
    meter(false), // Need to fill queue first
    grid(false),  // Need to fill queue first
    energy(false), // Scheduled on first aligned boundary after connect
    systemDynamic(true),
    statistics(true),
    crash(false), // May not be present
    requestOta(true) {} // Always require on connection
};

// Wire format for Mqtt::pushAlarm() - a fixed-size POD so it can pass through a
// FreeRTOS queue with no dynamic allocation. The published JSON also carries a
// power/pf/voltage snapshot, fetched at publish time (_publishAlarm) rather than
// carried here.
struct AlarmEntry
{
    char eventId[MQTT_ALARM_EVENT_ID_BUFFER_SIZE]; // unique per event, lets the cloud dedupe/correlate
    char type[MQTT_ALARM_TYPE_BUFFER_SIZE]; // e.g. "zero_crossing_timeout" - lets future alarm types share this same wire shape
    uint64_t unixTimeMs;

    AlarmEntry() : unixTimeMs(0) { eventId[0] = '\0'; type[0] = '\0'; }
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

    // Wake the MQTT task now instead of waiting up to MQTT_LOOP_INTERVAL for its
    // next loop - used by pushAlarm() below. Only rings the bell (xTaskNotify, no
    // data) - safe from any task, no-op before begin() or after stop().
    void requestImmediatePublish();
    
    // Public methods for pushing data to queues
    void pushLog(const LogEntry& entry);
    void pushMeter(const PayloadMeter& payload);
    void pushGrid(const PayloadGridPoint& point);

    // Queues a high-priority alarm and wakes the MQTT task (requestImmediatePublish).
    // Published ahead of the log queue and shadow report. Safe from any task.
    void pushAlarm(const AlarmEntry& entry);

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
    void clearOtaPendingState();             // drop the pending-OTA validation record once its job has a terminal status

    bool getSendPowerData();
    void setSendPowerData(bool enabled);     // persisted
    bool getSendGridData();
    void setSendGridData(bool enabled);      // persisted; cloud-settable via the system shadow
    uint32_t getMeterPublishThresholdBytes();
    void setMeterPublishThresholdBytes(uint32_t bytes);     // persisted; clamped to [MIN,MAX]; cloud-settable via the system shadow
    uint32_t getMeterPublishMaxIntervalMs();
    void setMeterPublishMaxIntervalMs(uint32_t intervalMs); // persisted; clamped to [MIN,MAX]; cloud-settable via the system shadow

#ifdef ENV_DEV
    // Dev-only: inject a synthetic IoT Command execution through the real handler
    // (staged from the caller's task, dispatched on the MQTT task). Never in prod.
    void injectCommandExecution(const char* executionId, const char* payload);

    // Dev-only: inject a synthetic AWS IoT job execution document (as delivered on
    // jobs/notify-next) through the real validate-and-handle path, so a fake or
    // unreachable firmware URL can drive the OTA download retry schedule on the
    // bench without minting a real job. Never in prod.
    void injectJobExecution(const char* payload);
#endif
}