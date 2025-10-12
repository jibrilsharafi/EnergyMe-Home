#pragma once

// Note: all the durations hereafter are in milliseconds unless specified otherwise

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "0"
#define FIRMWARE_BUILD_VERSION_MINOR "12"
#define FIRMWARE_BUILD_VERSION_PATCH "40"
#define FIRMWARE_BUILD_VERSION FIRMWARE_BUILD_VERSION_MAJOR "." FIRMWARE_BUILD_VERSION_MINOR "." FIRMWARE_BUILD_VERSION_PATCH

#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Product info
#define COMPANY_NAME "EnergyMe"
#define PRODUCT_NAME "Home"
#define FULL_PRODUCT_NAME "EnergyMe - Home"
#define PRODUCT_DESCRIPTION "An open-source energy monitoring system for home use, capable of monitoring up to 17 circuits."
#define GITHUB_URL "https://github.com/jibrilsharafi/EnergyMe-Home"
#define GITHUB_API_RELEASES_URL "https://api.github.com/repos/jibrilsharafi/EnergyMe-Home/releases/latest"
#define AUTHOR "Jibril Sharafi"
#define AUTHOR_EMAIL "jibril.sharafi@gmail.com"

// Serial
#define SERIAL_BAUDRATE 115200 // Most common baudrate for ESP32

// While loops
#define MAX_LOOP_ITERATIONS 1000 // The maximum number of iterations for any while loop to avoid infinite loops

// Preferences namespaces are here to enable a full wipe from utils when factory resetting
#define PREFERENCES_NAMESPACE_GENERAL "general_ns"
#define PREFERENCES_NAMESPACE_ADE7953 "ade7953_ns"
#define PREFERENCES_NAMESPACE_ENERGY "energy_ns"
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
#define PREFERENCES_NAMESPACE_AUTH "auth_ns" 

// Logger
#define LOG_PATH "/log.txt"
#define MAX_LOG_LINES 1000
#define MAXIMUM_LOG_FILE_SIZE (200 * 1024)

// Buffer Sizes for String Operations
// =================================
// Only constants which are used in multiple files are defined here
#define PREFERENCES_KEY_BUFFER_SIZE 15  // Maximum allowed by Preferences API
#define VERSION_BUFFER_SIZE 16          // For version strings (e.g., 1.0.0)
#define DEVICE_ID_BUFFER_SIZE 16        // For device ID (increased slightly for safety)
#define IP_ADDRESS_BUFFER_SIZE 16       // For IPv4 addresses (e.g., "192.168.1.1")
#define MAC_ADDRESS_BUFFER_SIZE 18      // For MAC addresses (e.g., "00:1A:2B:3C:4D:5E")
#define TIMESTAMP_ISO_BUFFER_SIZE 25    // For ISO UTC timestamps (formatted as "YYYY-MM-DDTHH:MM:SS.sssZ")
#define SHORT_NAME_BUFFER_SIZE 32       // For short names (e.g., channel names, labels)
#define MD5_BUFFER_SIZE 33              // 32 characters + null terminator
#define USERNAME_BUFFER_SIZE 64         // For usernames (e.g., WiFi SSID, MQTT username)
#define PASSWORD_BUFFER_SIZE 64         // For passwords (e.g., WiFi password, MQTT password)
#define SHORT_STATUS_BUFFER_SIZE 64     // Generic short status messages. Smaller than status so we can use %s of SHORT_STATUS in STATUS
#define NAME_BUFFER_SIZE 64             // For generic names (device, user, etc.)
#define MQTT_TOPIC_BUFFER_SIZE 128      // For MQTT topics
#define URL_BUFFER_SIZE 256             // For URLs
#define FULL_URL_BUFFER_SIZE 512        // For URLs with query parameters and similars
#define STATUS_BUFFER_SIZE 128          // Generic status messages (e.g., connection status, error messages)

#define STREAM_UTILS_MQTT_PACKET_SIZE 256 // Buffering stream packets instead of sending byte per byte is way more efficient

// Timeouts and intervals
#define TASK_STOPPING_TIMEOUT (3 * 1000)
#define TASK_STOPPING_CHECK_INTERVAL 100
#define CONFIG_MUTEX_TIMEOUT_MS (1 * 1000) // Generic timeout for configuration mutexes. Long timeouts cause wdt crash (like in async tcp)

// Channel configuration
#define MULTIPLEXER_CHANNEL_COUNT 16
#define CHANNEL_COUNT (MULTIPLEXER_CHANNEL_COUNT + 1) // All the 16 of the multiplexer + 1 directly going to channel A of ADE7953 

// Server used ports (here to ensure no conflicts)
#define MODBUS_TCP_PORT 502
#define WEBSERVER_PORT 80

// Useful constants
#define MAGIC_WORD_RTC 0xDEADBEEF // This is crucial to ensure that the RTC variables used have sensible values or it is just some garbage after reboot