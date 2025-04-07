#pragma once

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "07"
#define FIRMWARE_BUILD_VERSION_PATCH "12"
#define FIRMWARE_BUILD_VERSION FIRMWARE_BUILD_VERSION_MAJOR "." FIRMWARE_BUILD_VERSION_MINOR "." FIRMWARE_BUILD_VERSION_PATCH

#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// Project info
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

// File path
#define GENERAL_CONFIGURATION_JSON_PATH "/config/general.json"
#define CONFIGURATION_ADE7953_JSON_PATH "/config/ade7953.json"
#define CALIBRATION_JSON_PATH "/config/calibration.json"
#define CHANNEL_DATA_JSON_PATH "/config/channel.json"
#define CUSTOM_MQTT_CONFIGURATION_JSON_PATH "/config/custommqtt.json"
#define ENERGY_JSON_PATH "/energy.json"
#define DAILY_ENERGY_JSON_PATH "/daily-energy.json"
#define FW_UPDATE_INFO_JSON_PATH "/fw-update-info.json"
#define FW_UPDATE_STATUS_JSON_PATH "/fw-update-status.json"

// Crash monitor
#define PREFERENCES_NAMESPACE_CRASHMONITOR "crashmonitor"
#define PREFERENCES_DATA_KEY "crashdata"
#define CRASH_SIGNATURE 0xDEADBEEF
#define MAX_BREADCRUMBS 32
#define WATCHDOG_TIMER 30000
#define PREFERENCES_FIRMWARE_STATUS_KEY "fw_status"
#define ROLLBACK_TESTING_TIMEOUT 60000 // Interval in which the firmware is being tested. If the ESP32 reboots unexpectedly, the firmware will be rolled back
#define MAX_CRASH_COUNT 10
#define CRASH_COUNTER_TIMEOUT 60000

// Serial
#define SERIAL_BAUDRATE 115200 // Most common baudrate for ESP32

// While loops
#define MAX_LOOP_ITERATIONS 1000 // The maximum number of iterations for a while loop

// Logger
#define LOG_PATH "/logger/log.txt"
#define LOG_CONFIG_PATH "/logger/config.txt"
#define LOG_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define LOG_BUFFER_SIZE 30 // Callback queue size
#define LOG_JSON_BUFFER_SIZE 1024
#define LOG_TOPIC_SIZE 64

// Time
#define NTP_SERVER "pool.ntp.org"
#define TIME_SYNC_INTERVAL 3600 // Sync time every hour
#define DEFAULT_GMT_OFFSET 0
#define DEFAULT_DST_OFFSET 0
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// Webserver
#define WEBSERVER_PORT 80

// LED
// NOTE: pins on the schematic are mixed up. Hereafter the correct pin numbers
#define LED_RED_PIN 39
#define LED_GREEN_PIN 40
#define LED_BLUE_PIN 38
#define DEFAULT_LED_BRIGHTNESS 191 // 75% of the maximum brightness
#define LED_MAX_BRIGHTNESS 255
#define LED_FREQUENCY 5000
#define LED_RESOLUTION 8

// WiFi
#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_LOOP_INTERVAL 1000 // In milliseconds
#define WIFI_CONNECT_TIMEOUT 60 // In seconds
#define WIFI_PORTAL_TIMEOUT 180 // In seconds
#define WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS 5 // Maximum WiFi reconnection attempts before restart
#define WIFI_RECONNECT_DELAY_BASE 5000 // Base delay for exponential backoff (in milliseconds)
#define WIFI_STABLE_CONNECTION 300000 // Duration of uninterrupted WiFi connection to reset the reconnection counter (in milliseconds)

// MDNS
#define MDNS_HOSTNAME "energyme"

// MQTT
#define DEFAULT_IS_CLOUD_SERVICES_ENABLED false
#define MAX_INTERVAL_METER_PUBLISH 30000 // The maximum interval between two meter payloads (in milliseconds)
#define MAX_INTERVAL_STATUS_PUBLISH 3600000 // The interval between two status publish (in milliseconds)
#define MAX_INTERVAL_CRASH_MONITOR_PUBLISH 3600000 // The interval between two status publish (in milliseconds)
#define MQTT_CLAIM_MAX_CONNECTION_ATTEMPT 10 // The maximum number of attempts to connect to AWS IoT Core MQTT broker for claiming certificates
#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)
#define MQTT_MIN_CONNECTION_INTERVAL 5000 // Minimum interval between two connection attempts (in milliseconds)
#define MQTT_INITIAL_RECONNECT_INTERVAL 5000 // Initial interval for MQTT reconnection attempts (milliseconds)
#define MQTT_MAX_RECONNECT_INTERVAL 300000 // Maximum interval for MQTT reconnection attempts (milliseconds)
#define MQTT_RECONNECT_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks (in milliseconds)
#define PAYLOAD_METER_MAX_NUMBER_POINTS 150 // The maximum number of points that can be sent in a single payload. Going higher than about 150 leads to unstable connections
#define MQTT_PAYLOAD_LIMIT 32768 // Increase the base limit of 256 bytes. Increasing this over 32768 bytes will lead to unstable connections
#define MQTT_MAX_TOPIC_LENGTH 256 // The maximum length of a MQTT topic
#define MQTT_PROVISIONING_TIMEOUT 60000 // The timeout for the provisioning response (in milliseconds)
#define MQTT_PROVISIONING_LOOP_CHECK 1000 // Interval between two certificates check on memory (in milliseconds)

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
#define MQTT_CUSTOM_INITIAL_RECONNECT_INTERVAL 5000 // Initial interval for custom MQTT reconnection attempts (milliseconds)
#define MQTT_CUSTOM_MAX_RECONNECT_INTERVAL 300000 // Maximum interval for custom MQTT reconnection attempts (milliseconds)
#define MQTT_CUSTOM_RECONNECT_MULTIPLIER 2 // Multiplier for custom MQTT exponential backoff
#define MQTT_CUSTOM_LOOP_INTERVAL 100 // Interval between two MQTT loop checks (in milliseconds)
#define MQTT_CUSTOM_MIN_CONNECTION_INTERVAL 10000 // Minimum interval between two connection attempts (in milliseconds)
#define MQTT_CUSTOM_PAYLOAD_LIMIT 8192 // Increase the base limit of 256 bytes

// Saving date
#define SAVE_ENERGY_INTERVAL 360000 // Time between each energy save (in milliseconds) to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

// ESP32 status
#define MINIMUM_FREE_HEAP_SIZE 10000 // Below this value (in bytes), the ESP32 will restart
#define MINIMUM_FREE_SPIFFS_SIZE 10000 // Below this value (in bytes), the ESP32 will clear the log
#define ESP32_RESTART_DELAY 1000 // The delay before restarting the ESP32 (in milliseconds) after a restart request, needed to allow the ESP32 to finish the current operations

// Multiplexer
// --------------------
#define MULTIPLEXER_S0_PIN 10
#define MULTIPLEXER_S1_PIN 11
#define MULTIPLEXER_S2_PIN 3
#define MULTIPLEXER_S3_PIN 9
#define MULTIPLEXER_CHANNEL_COUNT 16 // This cannot be defined as a constant because it is used for array initialization
#define CHANNEL_COUNT MULTIPLEXER_CHANNEL_COUNT + 1 // The number of channels being 1 (general) + 16 (multiplexer)

// ADE7953
// --------------------
// Hardware pins
#define SS_PIN 48
#define SCK_PIN 36
#define MISO_PIN 35
#define MOSI_PIN 45
#define ADE7953_RESET_PIN 21
#define ADE7953_INTERRUPT_PIN 37 // TODO: Implement the interrupt pin

// Setup
#define ADE7953_RESET_LOW_DURATION 200 // The duration for the reset pin to be low (in milliseconds)
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500 // In milliseconds

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 
#define DEFAULT_X_NOLOAD_REGISTER 0x00E419 // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 10000:1, meaning that the no-load threshold is 0.01% of the full-scale value
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (enable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0xFF // 0xFF 0b11111111 (enable accumulation mode for all channels)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000000001100 // Enable bit 2, bit 3 (line accumulation for PF), and 15 (keep HPF enabled, keep COMM_LOCK disabled)
#define DEFAULT_GAIN 4194304 // 0x400000 (default gain for the ADE7953)
#define DEFAULT_OFFSET 0 // 0x000000 (default offset for the ADE7953)
#define DEFAULT_PHCAL 10 // 0.02°/LSB, indicating a phase calibration of 0.2° which is needed for CTs
#define DEFAULT_SAMPLE_TIME 200 // 200 ms is the minimum time required to settle the ADE7953 channel readings (needed as the multiplexer constantly switches)

// Fixed conversion values
#define POWER_FACTOR_CONVERSION_FACTOR 1.0 / 32768.0 // PF/LSB
#define ANGLE_CONVERSION_FACTOR 360.0 * 50.0 / 223000.0 // 0.0807 °/LSB
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.003 // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used

// Validate values
#define VALIDATE_VOLTAGE_MIN 50.0 // Any voltage below this value is discarded
#define VALIDATE_VOLTAGE_MAX 300.0  // Any voltage above this value is discarded
#define VALIDATE_CURRENT_MIN -100.0 // Any current below this value is discarded
#define VALIDATE_CURRENT_MAX 100.0 // Any current above this value is discarded
#define VALIDATE_POWER_MIN -100000.0 // Any power below this value is discarded
#define VALIDATE_POWER_MAX 100000.0 // Any power above this value is discarded
#define VALIDATE_POWER_FACTOR_MIN -1.0 // Any power factor below this value is discarded
#define VALIDATE_POWER_FACTOR_MAX 1.0 // Any power factor above this value is discarded

// Modbus TCP
// --------------------
#define MODBUS_TCP_PORT 502 // The default port for Modbus TCP
#define MODBUS_TCP_MAX_CLIENTS 3 // The maximum number of clients that can connect to the Modbus TCP server
#define MODBUS_TCP_TIMEOUT 10000 // The timeout for the Modbus TCP server (in milliseconds)
#define MODBUS_TCP_SERVER_ID 1 // The Modbus TCP server ID

// Cloud services
// --------------------
// Basic ingest functionality
#define AWS_TOPIC "$aws"
#define MQTT_BASIC_INGEST AWS_TOPIC "/rules"

// Certificates path
#define PREFERENCES_NAMESPACE_CERTIFICATES "certificates"
#define PREFS_KEY_CERTIFICATE "certificate"
#define PREFS_KEY_PRIVATE_KEY "private_key"
#define CERTIFICATE_LENGTH 2048
#define KEY_SIZE 256

// EnergyMe - Home | Custom MQTT topics
// Base topics
#define MQTT_TOPIC_1 "energyme"
#define MQTT_TOPIC_2 "home"

// Publish topics
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_STATUS "status"
#define MQTT_TOPIC_METADATA "metadata"
#define MQTT_TOPIC_CHANNEL "channel"
#define MQTT_TOPIC_CRASH "crash"
#define MQTT_TOPIC_MONITOR "monitor"
#define MQTT_TOPIC_GENERAL_CONFIGURATION "general-configuration"
#define MQTT_TOPIC_CONNECTIVITY "connectivity"
#define MQTT_TOPIC_PROVISIONING_REQUEST "provisioning/request"

// Subscribe topics
#define MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE "firmware-update"
#define MQTT_TOPIC_SUBSCRIBE_RESTART "restart"
#define MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE "provisioning/response"
#define MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES "erase-certificates"
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// MQTT will
#define MQTT_WILL_QOS 1
#define MQTT_WILL_RETAIN true
#define MQTT_WILL_MESSAGE "{\"connectivity\":\"unexpected_offline\"}"

// AWS IoT Core endpoint
#define AWS_IOT_CORE_PORT 8883