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

// Preferences namespaces are here to enable a full wipe from utils when factory resetting
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

// Logger
#define LOG_PATH "/logger/log.txt"
#define LOG_CONFIG_PATH "/logger/config.txt"
#define LOG_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// Buffer Sizes for String Operations
// =================================
// Only constants which are used in multiple files are defined here
#define DEVICE_ID_BUFFER_SIZE 16      // For device ID (increased slightly for safety)
#define IP_ADDRESS_BUFFER_SIZE 16 // For IPv4 addresses (e.g., "192.168.1.1")
#define MAC_ADDRESS_BUFFER_SIZE 18 // For MAC addresses (e.g., "00:1A:2B:3C:4D:5E")
#define TIMESTAMP_STRING_BUFFER_SIZE 20 // For timestamps (formatted as "YYYY-MM-DD HH:MM:SS")
#define MD5_BUFFER_SIZE 33 // 32 characters + null terminator
#define USERNAME_BUFFER_SIZE 64
#define PASSWORD_BUFFER_SIZE 64
#define NAME_BUFFER_SIZE 64 // For generic names (device, user, etc.)
#define MQTT_TOPIC_BUFFER_SIZE 64
#define SERVER_NAME_BUFFER_SIZE 128
#define STATUS_BUFFER_SIZE 128 // Generic status messages (e.g., connection status, error messages)
#define VERSION_BUFFER_SIZE 32

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