#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <base64.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "ade7953.h"
#include "globals.h"
#include "customtime.h"
#include "customwifi.h"
#include "utils.h"

// Default configuration values
#define INFLUXDB_ENABLED_DEFAULT false
#define INFLUXDB_SERVER_DEFAULT "localhost"
#define INFLUXDB_PORT_DEFAULT 8086
#define INFLUXDB_VERSION_DEFAULT 2
#define INFLUXDB_DATABASE_DEFAULT "energyme-home"
#define INFLUXDB_USERNAME_DEFAULT ""
#define INFLUXDB_PASSWORD_DEFAULT ""
#define INFLUXDB_ORGANIZATION_DEFAULT "my-org"
#define INFLUXDB_BUCKET_DEFAULT "energyme-home"
#define INFLUXDB_TOKEN_DEFAULT ""
#define INFLUXDB_MEASUREMENT_DEFAULT "meter"
#define INFLUXDB_FREQUENCY_DEFAULT 15
#define INFLUXDB_USE_SSL_DEFAULT false

// Task configuration
#define INFLUXDB_TASK_NAME "influxdb_task"
#define INFLUXDB_TASK_STACK_SIZE (6 * 1024)  // Reduced from 8KB since buffers moved to PSRAM
#define INFLUXDB_TASK_PRIORITY 1
#define INFLUXDB_TASK_CHECK_INTERVAL 500 // Cannot send InfluxDB messages faster than this

// Helper constants
#define INFLUXDB_MINIMUM_FREQUENCY 1
#define INFLUXDB_MAXIMUM_FREQUENCY 3600

// Failure handling constants
#define INFLUXDB_INITIAL_RETRY_INTERVAL (30 * 1000)
#define INFLUXDB_MAX_RETRY_INTERVAL (10 * 60 * 1000)
#define INFLUXDB_RETRY_MULTIPLIER 2
#define INFLUXDB_MAX_CONSECUTIVE_FAILURES 10

// Preferences keys for persistent storage
#define INFLUXDB_ENABLED_KEY "enabled"
#define INFLUXDB_SERVER_KEY "server"
#define INFLUXDB_PORT_KEY "port"
#define INFLUXDB_VERSION_KEY "version"
#define INFLUXDB_DATABASE_KEY "database"
#define INFLUXDB_USERNAME_KEY "username"
#define INFLUXDB_PASSWORD_KEY "password"
#define INFLUXDB_ORGANIZATION_KEY "organization"
#define INFLUXDB_BUCKET_KEY "bucket"
#define INFLUXDB_TOKEN_KEY "token"
#define INFLUXDB_MEASUREMENT_KEY "measurement"
#define INFLUXDB_FREQUENCY_KEY "frequency"
#define INFLUXDB_USE_SSL_KEY "useSsl"

// Buffer sizes for various fields
#define TOKEN_BUFFER_SIZE 128
#define AUTH_HEADER_BUFFER_SIZE 256
#define LINE_PROTOCOL_BUFFER_SIZE 1024
#define PAYLOAD_BUFFER_SIZE (16 * 1024)  // Use PSRAM for larger buffer (16KB). Full buffer with 17 channels is about 7kB

struct InfluxDbConfiguration {
    bool enabled;
    char server[URL_BUFFER_SIZE];
    uint16_t port;
    uint8_t version;
    char database[NAME_BUFFER_SIZE];
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    char organization[SHORT_NAME_BUFFER_SIZE];
    char bucket[SHORT_NAME_BUFFER_SIZE];
    char token[TOKEN_BUFFER_SIZE];
    char measurement[NAME_BUFFER_SIZE];
    uint32_t frequencySeconds;
    bool useSsl;

    InfluxDbConfiguration()
        : enabled(INFLUXDB_ENABLED_DEFAULT), 
          port(INFLUXDB_PORT_DEFAULT),
          version(INFLUXDB_VERSION_DEFAULT),
          frequencySeconds(INFLUXDB_FREQUENCY_DEFAULT),
          useSsl(INFLUXDB_USE_SSL_DEFAULT) {
      snprintf(server, sizeof(server), "%s", INFLUXDB_SERVER_DEFAULT);
      snprintf(database, sizeof(database), "%s", INFLUXDB_DATABASE_DEFAULT);
      snprintf(username, sizeof(username), "%s", INFLUXDB_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", INFLUXDB_PASSWORD_DEFAULT);
      snprintf(organization, sizeof(organization), "%s", INFLUXDB_ORGANIZATION_DEFAULT);
      snprintf(bucket, sizeof(bucket), "%s", INFLUXDB_BUCKET_DEFAULT);
      snprintf(token, sizeof(token), "%s", INFLUXDB_TOKEN_DEFAULT);
      snprintf(measurement, sizeof(measurement), "%s", INFLUXDB_MEASUREMENT_DEFAULT);
    }
};

namespace InfluxDbClient
{
    void begin();
    void stop();

    // Manage configuration directly
    bool getConfiguration(InfluxDbConfiguration &config);
    bool setConfiguration(const InfluxDbConfiguration &config);
    bool resetConfiguration();
    
    // Manage configuration from JSON
    bool getConfigurationAsJson(JsonDocument &jsonDocument);
    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial = false);
    void configurationToJson(InfluxDbConfiguration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, InfluxDbConfiguration &config, bool partial = false);

    // Get runtime status
    bool getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize);

    // Task information
    TaskInfo getTaskInfo();
}