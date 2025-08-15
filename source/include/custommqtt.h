#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <StreamUtils.h>
#include <WiFiClient.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
#include "globals.h"
#include "utils.h"
#include "structs.h"
#include "utils.h"

// Custom MQTT configuration constants
#define DEFAULT_IS_CUSTOM_MQTT_ENABLED false
#define MQTT_CUSTOM_SERVER_DEFAULT "test.mosquitto.org"
#define MQTT_CUSTOM_PORT_DEFAULT 1883
#define MQTT_CUSTOM_CLIENTID_DEFAULT "energyme-home"
#define MQTT_CUSTOM_TOPIC_DEFAULT "energyme"
#define MQTT_CUSTOM_FREQUENCY_SECONDS_DEFAULT 15
#define MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT false
#define MQTT_CUSTOM_USERNAME_DEFAULT "username"
#define MQTT_CUSTOM_PASSWORD_DEFAULT "password"

// Custom MQTT task constants
#define CUSTOM_MQTT_TASK_NAME "custom_mqtt_task"
#define CUSTOM_MQTT_TASK_STACK_SIZE (6 * 1024) // Must be bigger than the payload limit. In general never exceeded 4 kB usage
#define CUSTOM_MQTT_TASK_PRIORITY 1
#define CUSTOM_MQTT_TASK_CHECK_INTERVAL (1 * 1000) // Cannot send mqtt messages faster than this (reducing it crashes the system)

// Reconnection strategy constants
#define MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL (5 * 1000)
#define MQTT_CUSTOM_MAX_RECONNECT_INTERVAL (5 * 60 * 1000)
#define MQTT_CUSTOM_RECONNECT_MULTIPLIER 2
#define MQTT_CUSTOM_MAX_RECONNECT_ATTEMPTS 10
#define MQTT_CUSTOM_MAX_FAILED_MESSAGE_PUBLISH_ATTEMPTS 10

// Preferences keys for persistent storage
#define CUSTOM_MQTT_ENABLED_KEY "enabled"
#define CUSTOM_MQTT_SERVER_KEY "server"
#define CUSTOM_MQTT_PORT_KEY "port"
#define CUSTOM_MQTT_USERNAME_KEY "username"
#define CUSTOM_MQTT_PASSWORD_KEY "password"
#define CUSTOM_MQTT_CLIENT_ID_KEY "clientId"
#define CUSTOM_MQTT_TOPIC_PREFIX_KEY "topicPrefix"
#define CUSTOM_MQTT_PUBLISH_INTERVAL_KEY "publInterval"
#define CUSTOM_MQTT_USE_CREDENTIALS_KEY "useCred"
#define CUSTOM_MQTT_TOPIC_KEY "topic"
#define CUSTOM_MQTT_FREQUENCY_KEY "frequency"

struct CustomMqttConfiguration {
    bool enabled;
    char server[URL_BUFFER_SIZE];
    uint16_t port;
    char clientid[NAME_BUFFER_SIZE];
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    uint32_t frequencySeconds;
    bool useCredentials;
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];

    // Constructor sets sensible defaults
    CustomMqttConfiguration() 
        : enabled(DEFAULT_IS_CUSTOM_MQTT_ENABLED), 
          port(MQTT_CUSTOM_PORT_DEFAULT),
          frequencySeconds(MQTT_CUSTOM_FREQUENCY_SECONDS_DEFAULT),
          useCredentials(MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT) {
      snprintf(server, sizeof(server), "%s", MQTT_CUSTOM_SERVER_DEFAULT);
      snprintf(clientid, sizeof(clientid), "%s", MQTT_CUSTOM_CLIENTID_DEFAULT);
      snprintf(topic, sizeof(topic), "%s", MQTT_CUSTOM_TOPIC_DEFAULT);
      snprintf(username, sizeof(username), "%s", MQTT_CUSTOM_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", MQTT_CUSTOM_PASSWORD_DEFAULT);
    }
};

namespace CustomMqtt
{
    // Lifecycle management
    void begin();
    void stop();

    // Configuration management - direct struct operations
    bool getConfiguration(CustomMqttConfiguration &config);
    bool setConfiguration(const CustomMqttConfiguration &config);
    bool resetConfiguration();
    
    // Configuration management - JSON operations
    bool getConfigurationAsJson(JsonDocument &jsonDocument);
    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial = false);
    void configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config, bool partial = false);

    // Runtime status
    bool getRuntimeStatus(char *statusBuffer, size_t statusSize, char *timestampBuffer, size_t timestampSize);

    // Task information
    TaskInfo getTaskInfo();
}