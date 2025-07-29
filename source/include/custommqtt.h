#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <Preferences.h>
#include <WiFi.h>

#include "ade7953.h"
#include "constants.h"
#include "customtime.h"
#include "customwifi.h"
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


struct CustomMqttConfiguration { // TODO: deprecate this
    bool enabled;
    char server[SERVER_NAME_BUFFER_SIZE];
    int port;
    char clientid[CLIENT_ID_BUFFER_SIZE];
    char topic[MQTT_TOPIC_BUFFER_SIZE];
    int frequency;
    bool useCredentials;
    char username[USERNAME_BUFFER_SIZE];
    char password[PASSWORD_BUFFER_SIZE];
    char lastConnectionStatus[STATUS_BUFFER_SIZE];
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

class CustomMqtt
{
public:
    CustomMqtt(
        Ade7953 &ade7953,
        AdvancedLogger &logger,
        PubSubClient &customClientMqtt,
        CustomMqttConfiguration &customMqttConfiguration
    );

    void begin();
    void loop();

    bool setConfiguration(JsonDocument &jsonDocument);

private:
    void _setDefaultConfiguration();
    void _setConfigurationFromSpiffs();
    void _saveConfigurationToSpiffs();

    void _disable();

    bool _connectMqtt();

    void _publishMeter();

    bool _publishMessage(const char *topic, const char *message);

    bool _validateJsonConfiguration(JsonDocument &jsonDocument);

    Ade7953 &_ade7953;
    AdvancedLogger &_logger;
    PubSubClient &_customClientMqtt;
    CustomMqttConfiguration &_customMqttConfiguration;

    bool _isSetupDone = false;

    unsigned long _lastMillisMqttLoop = 0;
    unsigned long _lastMillisMqttFailed = 0;
    unsigned long _mqttConnectionAttempt = 0;
    unsigned long _nextMqttConnectionAttemptMillis = 0;

    unsigned long _lastMillisMeterPublish = 0;
};