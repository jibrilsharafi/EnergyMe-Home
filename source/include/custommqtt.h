#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <Preferences.h>
#include <WiFiClient.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
#include "globals.h"
#include "structs.h"
#include "utils.h"

// Custom MQTT
#define DEFAULT_IS_CUSTOM_MQTT_ENABLED false
#define MQTT_CUSTOM_SERVER_DEFAULT "test.mosquitto.org"
#define MQTT_CUSTOM_PORT_DEFAULT 1883
#define MQTT_CUSTOM_CLIENTID_DEFAULT "energyme-home"
#define MQTT_CUSTOM_TOPIC_DEFAULT "topic"
#define MQTT_CUSTOM_FREQUENCY_DEFAULT 15 // In seconds
#define MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT false
#define MQTT_CUSTOM_USERNAME_DEFAULT "username"
#define MQTT_CUSTOM_PASSWORD_DEFAULT "password"
#define MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL (5 * 1000) // Initial interval for custom MQTT reconnection attempts
#define MQTT_CUSTOM_MAX_RECONNECT_INTERVAL (5 * 60 * 1000) // Maximum interval for custom MQTT reconnection attempts
#define MQTT_CUSTOM_RECONNECT_MULTIPLIER 2 // Multiplier for custom MQTT exponential backoff
#define MQTT_CUSTOM_LOOP_INTERVAL 100 // Interval between two MQTT loop checks
#define MQTT_CUSTOM_MIN_CONNECTION_INTERVAL (10 * 1000) // Minimum interval between two connection attempts
#define MQTT_CUSTOM_PAYLOAD_LIMIT 512 // Increase the base limit of 256 bytes

#define CUSTOM_MQTT_ENABLED_KEY "enabled"
#define CUSTOM_MQTT_SERVER_KEY "server"
#define CUSTOM_MQTT_PORT_KEY "port"
#define CUSTOM_MQTT_USERNAME_KEY "username"
#define CUSTOM_MQTT_PASSWORD_KEY "password"
#define CUSTOM_MQTT_CLIENT_ID_KEY "clientId"
#define CUSTOM_MQTT_TOPIC_PREFIX_KEY "topicPrefix"
#define CUSTOM_MQTT_PUBLISH_INTERVAL_KEY "publishInterval"
#define CUSTOM_MQTT_USE_CREDENTIALS_KEY "useCredentials"
#define CUSTOM_MQTT_TOPIC_KEY "topic"
#define CUSTOM_MQTT_FREQUENCY_KEY "frequency"

struct CustomMqttConfiguration { // TODO: deprecate this
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    char clientid[DEVICE_ID_BUFFER_SIZE];
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    int frequency;
    bool useCredentials;
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    char lastConnectionStatus[NAME_BUFFER_SIZE];
    char lastConnectionAttemptTimestamp[TIMESTAMP_STRING_BUFFER_SIZE];

    CustomMqttConfiguration() 
        : enabled(DEFAULT_IS_CUSTOM_MQTT_ENABLED), 
          port(MQTT_CUSTOM_PORT_DEFAULT),
          frequency(MQTT_CUSTOM_FREQUENCY_DEFAULT),
          useCredentials(MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT) {
      snprintf(server, sizeof(server), "%s", MQTT_CUSTOM_SERVER_DEFAULT);
      snprintf(clientid, sizeof(clientid), "%s", MQTT_CUSTOM_CLIENTID_DEFAULT);
      snprintf(topic, sizeof(topic), "%s", MQTT_CUSTOM_TOPIC_DEFAULT);
      snprintf(username, sizeof(username), "%s", MQTT_CUSTOM_USERNAME_DEFAULT);
      snprintf(password, sizeof(password), "%s", MQTT_CUSTOM_PASSWORD_DEFAULT);
      snprintf(lastConnectionStatus, sizeof(lastConnectionStatus), "Never attempted");
      snprintf(lastConnectionAttemptTimestamp, sizeof(lastConnectionAttemptTimestamp), "Never attempted");
    }
};

namespace CustomMqtt
{
    void begin();
    void stop();

    bool setConfiguration(CustomMqttConfiguration &config);
    bool setConfigurationFromJson(JsonDocument &jsonDocument);
    void getConfiguration(CustomMqttConfiguration &config);

    bool configurationToJson(CustomMqttConfiguration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, CustomMqttConfiguration &config);
}