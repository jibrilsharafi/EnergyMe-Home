#pragma once

// Note: all the durations hereafter are in milliseconds unless specified otherwise

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "11"
#define FIRMWARE_BUILD_VERSION_PATCH "04"
#define FIRMWARE_BUILD_VERSION FIRMWARE_BUILD_VERSION_MAJOR "." FIRMWARE_BUILD_VERSION_MINOR "." FIRMWARE_BUILD_VERSION_PATCH

#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Product info
#define COMPANY_NAME "EnergyMe"
#define PRODUCT_NAME "Home"
#define FULL_PRODUCT_NAME "EnergyMe - Home"
#define PRODUCT_DESCRIPTION "An open-source energy monitoring system for home use, capable of monitoring up to 17 circuits."
#define GITHUB_URL "https://github.com/jibrilsharafi/EnergyMe-Home"
#define AUTHOR "Jibril Sharafi"
#define AUTHOR_EMAIL "jibril.sharafi@gmail.com"

// URL Utilities
#define PUBLIC_LOCATION_ENDPOINT "http://ip-api.com/json/"
#define PUBLIC_TIMEZONE_ENDPOINT "http://api.geonames.org/timezoneJSON?"
#define PUBLIC_TIMEZONE_USERNAME "energymehome"

// Serial
#define SERIAL_BAUDRATE 115200 // Most common baudrate for ESP32

// While loops
#define MAX_LOOP_ITERATIONS 10000 // The maximum number of iterations for a while loop

// Time utilities
#define MINIMUM_UNIX_TIME 1000000000 // In seconds, corresponds to 2001
#define MINIMUM_UNIX_TIME_MILLISECONDS 1000000000000 // Corresponds to 2001
#define MAXIMUM_UNIX_TIME 4102444800 // In seconds, corresponds to 2100
#define MAXIMUM_UNIX_TIME_MILLISECONDS 4102444800000 // Corresponds to 2100

// File path
#define CONFIGURATION_ADE7953_JSON_PATH "/config/ade7953.json"
#define CALIBRATION_JSON_PATH "/config/calibration.json"
#define CHANNEL_DATA_JSON_PATH "/config/channel.json"
#define ENERGY_JSON_PATH "/energy.json"
#define DAILY_ENERGY_JSON_PATH "/daily-energy.json"

// Preferences namespaces for configuration storage
#define PREFERENCES_NAMESPACE_ADE7953 "ade7953_ns"
#define PREFERENCES_NAMESPACE_CALIBRATION "calibration_ns"
#define PREFERENCES_NAMESPACE_CHANNELS "channels_ns"
#define PREFERENCES_NAMESPACE_MQTT "mqtt_ns"
#define PREFERENCES_NAMESPACE_CUSTOM_MQTT "custom_mqtt_ns"
#define PREFERENCES_NAMESPACE_INFLUXDB "influxdb_ns"
#define PREFERENCES_NAMESPACE_BUTTON "button_ns"
#define PREFERENCES_NAMESPACE_WIFI "wifi_ns"
#define PREFERENCES_NAMESPACE_TIME "time_ns"
#define PREFERENCES_NAMESPACE_CRASHMONITOR "crashmonitor_ns"
#define PREFERENCES_NAMESPACE_CERTIFICATES "certificates_ns"
#define PREFERENCES_NAMESPACE_LED "led_ns"
#define PREFERENCES_NAMESPACE_FIRMWARE_UPDATES "fw_updates_ns"

#define PREF_KEY_FW_UPDATES_URL "url"
#define PREF_KEY_FW_UPDATES_VERSION "version"

// Preferences keys for general configuration
#define PREF_KEY_TIMEZONE "timezone"
#define PREF_KEY_LOCATION_LAT "locationLat"
#define PREF_KEY_LOCATION_LON "locationLon"
#define PREF_KEY_SEND_POWER_DATA "sendPowerData"
#define PREF_KEY_LOG_LEVEL_PRINT "logLevelPrint"
#define PREF_KEY_LOG_LEVEL_SAVE "logLevelSave"

// Preferences keys for ADE7953 configuration
#define PREF_KEY_SAMPLE_TIME "sampleTime"
#define PREF_KEY_A_V_GAIN "aVGain"
#define PREF_KEY_A_I_GAIN "aIGain"
#define PREF_KEY_B_I_GAIN "bIGain"
#define PREF_KEY_A_IRMS_OS "aIRmsOs"
#define PREF_KEY_B_IRMS_OS "bIRmsOs"
#define PREF_KEY_A_W_GAIN "aWGain"
#define PREF_KEY_B_W_GAIN "bWGain"
#define PREF_KEY_A_WATT_OS "aWattOs"
#define PREF_KEY_B_WATT_OS "bWattOs"
#define PREF_KEY_A_VAR_GAIN "aVarGain"
#define PREF_KEY_B_VAR_GAIN "bVarGain"
#define PREF_KEY_A_VAR_OS "aVarOs"
#define PREF_KEY_B_VAR_OS "bVarOs"
#define PREF_KEY_A_VA_GAIN "aVaGain"
#define PREF_KEY_B_VA_GAIN "bVaGain"
#define PREF_KEY_A_VA_OS "aVaOs"
#define PREF_KEY_B_VA_OS "bVaOs"

// Preferences keys for MQTT configuration
#define PREF_KEY_CUSTOM_MQTT_ENABLED "enabled"
#define PREF_KEY_CUSTOM_MQTT_SERVER "server"
#define PREF_KEY_CUSTOM_MQTT_PORT "port"
#define PREF_KEY_CUSTOM_MQTT_USERNAME "username"
#define PREF_KEY_CUSTOM_MQTT_PASSWORD "password"
#define PREF_KEY_CUSTOM_MQTT_CLIENT_ID "clientId"
#define PREF_KEY_CUSTOM_MQTT_TOPIC_PREFIX "topicPrefix"
#define PREF_KEY_CUSTOM_MQTT_PUBLISH_INTERVAL "publishInterval"
#define PREF_KEY_CUSTOM_MQTT_USE_CREDENTIALS "useCredentials"
#define PREF_KEY_CUSTOM_MQTT_TOPIC "topic"
#define PREF_KEY_CUSTOM_MQTT_FREQUENCY "frequency"

// MQTT
#define PREF_KEY_MQTT_CLOUD_SERVICES "cloud_services"
#define PREF_KEY_MQTT_SEND_POWER_DATA "send_power_data"

// Preferences keys for channel configuration (per channel, format: "ch<N>_<property>")
#define PREF_KEY_CHANNEL_ACTIVE_FMT "ch%d_active"
#define PREF_KEY_CHANNEL_REVERSE_FMT "ch%d_reverse"
#define PREF_KEY_CHANNEL_LABEL_FMT "ch%d_label"
#define PREF_KEY_CHANNEL_PHASE_FMT "ch%d_phase"
#define PREF_KEY_CHANNEL_CALIB_LABEL_FMT "ch%d_calibLabel"

// Logger
#define LOG_PATH "/logger/log.txt"
#define LOG_CONFIG_PATH "/logger/config.txt"
#define LOG_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// Buffer Sizes for String Operations
// =================================
#define DEVICE_ID_BUFFER_SIZE 16      // For device ID (increased slightly for safety)
#define TIMESTAMP_STRING_BUFFER_SIZE 32
#define MD5_BUFFER_SIZE 33 // 32 characters + null terminator
#define USERNAME_BUFFER_SIZE 64
#define PASSWORD_BUFFER_SIZE 64
#define NAME_BUFFER_SIZE 64 // For generic names (device, user, etc.)
#define MQTT_TOPIC_BUFFER_SIZE 64
#define SERVER_NAME_BUFFER_SIZE 128
#define STATUS_BUFFER_SIZE 128 // Generic status messages (e.g., connection status, error messages)

// System restart thresholds
#define MINIMUM_FREE_HEAP_SIZE 1000 // Below this value (in bytes), the system will restart. This value can get very low due to the presence of the PSRAM to support
#define MINIMUM_FREE_PSRAM_SIZE 10000 // Below this value (in bytes), the system will restart
#define MINIMUM_FREE_SPIFFS_SIZE 10000 // Below this value (in bytes), the system will clear the log
#define SYSTEM_RESTART_DELAY (2 * 1000) // The delay before restarting the system after a restart request, needed to allow the system to finish the current operations
#define MINIMUM_FREE_HEAP_OTA 20000 // Below this, the OTA is rejected (a bit unsafe, this could block OTA)
#define MINIMUM_FIRMWARE_SIZE 100000 // Minimum firmware size in bytes (100KB) - prevents empty/invalid uploads

// Multiplexer
// --------------------
#define MULTIPLEXER_CHANNEL_COUNT 16

// Server used ports (here to ensure no conflicts)
#define MODBUS_TCP_PORT 502 // The default port for Modbus TCP
#define WEBSERVER_PORT 80