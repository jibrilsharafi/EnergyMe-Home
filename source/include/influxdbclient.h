#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <base64.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

#include "ade7953.h"
#include "globals.h"
#include "customtime.h"
#include "customwifi.h"

#define PREFERENCES_NAMESPACE_INFLUXDB "influxdb_ns"
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
#define INFLUXDB_FREQUENCY_DEFAULT 15  // In seconds
#define INFLUXDB_USE_SSL_DEFAULT false
#define INFLUXDB_TASK_NAME "influxdb_task"
#define INFLUXDB_TASK_STACK_SIZE 8192
#define INFLUXDB_TASK_PRIORITY 1

extern Ade7953 ade7953;

struct InfluxDbConfiguration {
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    int version;  // 1 or 2
    
    // v1 fields
    char database[DATABASE_NAME_BUFFER_SIZE];
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    
    // v2 fields
    char organization[ORGANIZATION_BUFFER_SIZE];
    char bucket[BUCKET_NAME_BUFFER_SIZE];
    char token[TOKEN_BUFFER_SIZE];
    
    char measurement[MEASUREMENT_BUFFER_SIZE];
    int frequencySeconds;
    bool useSSL;
    char lastConnectionStatus[STATUS_BUFFER_SIZE];
    char lastConnectionAttemptTimestamp[TIMESTAMP_STRING_BUFFER_SIZE];

    InfluxDbConfiguration()
        : enabled(INFLUXDB_ENABLED_DEFAULT), 
          port(INFLUXDB_PORT_DEFAULT),
          version(INFLUXDB_VERSION_DEFAULT),
          frequencySeconds(INFLUXDB_FREQUENCY_DEFAULT),
          useSSL(INFLUXDB_USE_SSL_DEFAULT) {
      snprintf(server, sizeof(server), "%s", INFLUXDB_SERVER_DEFAULT);
      snprintf(database, sizeof(database), "%s", INFLUXDB_DATABASE_DEFAULT);
      snprintf(username, sizeof(username), "%s", INFLUXDB_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", INFLUXDB_PASSWORD_DEFAULT);
      snprintf(organization, sizeof(organization), "%s", INFLUXDB_ORGANIZATION_DEFAULT);
      snprintf(bucket, sizeof(bucket), "%s", INFLUXDB_BUCKET_DEFAULT);
      snprintf(token, sizeof(token), "%s", INFLUXDB_TOKEN_DEFAULT);
      snprintf(measurement, sizeof(measurement), "%s", INFLUXDB_MEASUREMENT_DEFAULT);
      snprintf(lastConnectionStatus, sizeof(lastConnectionStatus), "Never attempted");
      snprintf(lastConnectionAttemptTimestamp, sizeof(lastConnectionAttemptTimestamp), "Never attempted");
    }
};

namespace InfluxDbClient
{
    void begin();
    void stop();

    bool setConfiguration(JsonDocument &jsonDocument);
}