#ifndef CONSTANTS_H
#define CONSTANTS_H

// Project info
#define COMPANY_NAME "EnergyMe"
#define PRODUCT_NAME "Home"
#define FULL_PRODUCT_NAME "EnergyMe - Home"
#define PRODUCT_DESCRIPTION "A open-source energy monitoring system for home use, capable of monitoring up to 17 circuits."
#define PRODUCT_URL "https://energyme.net"
#define GITHUB_URL "https://github.com/jibrilsharafi/EnergyMe-Home"
#define AUTHOR "Jibril Sharafi"
#define AUTHOR_EMAIL "jibril.sharafi@gmail.com"

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "05"
#define FIRMWARE_BUILD_VERSION_PATCH "06"
#define FIRMWARE_BUILD_VERSION FIRMWARE_BUILD_VERSION_MAJOR "." FIRMWARE_BUILD_VERSION_MINOR "." FIRMWARE_BUILD_VERSION_PATCH

#define FIRMWARE_BUILD_DATE_YEAR "2024"
#define FIRMWARE_BUILD_DATE_MONTH "10"
#define FIRMWARE_BUILD_DATE_DAY "23"
#define FIRMWARE_BUILD_DATE FIRMWARE_BUILD_DATE_YEAR "-" FIRMWARE_BUILD_DATE_MONTH "-" FIRMWARE_BUILD_DATE_DAY

// Measurements
#define VOLTAGE_MEASUREMENT 1
#define CURRENT_MEASUREMENT 2
#define ACTIVE_POWER_MEASUREMENT 3
#define REACTIVE_POWER_MEASUREMENT 4
#define APPARENT_POWER_MEASUREMENT 5
#define POWER_FACTOR_MEASUREMENT 6

// URL Utilities
#define PUBLIC_IP_ENDPOINT "http://checkip.amazonaws.com/"
#define PUBLIC_LOCATION_ENDPOINT "http://ip-api.com/json/"
#define PUBLIC_TIMEZONE_ENDPOINT "http://api.geonames.org/timezoneJSON?"
#define PUBLIC_TIMEZONE_USERNAME "energymehome"

// File path
#define FIRST_SETUP_JSON_PATH "/first-setup.json"
#define GENERAL_CONFIGURATION_JSON_PATH "/config/general.json"
#define CONFIGURATION_ADE7953_JSON_PATH "/config/ade7953.json"
#define CALIBRATION_JSON_PATH "/config/calibration.json"
#define CHANNEL_DATA_JSON_PATH "/config/channel.json"
#define CUSTOM_MQTT_CONFIGURATION_JSON_PATH "/config/custommqtt.json"
#define ENERGY_JSON_PATH "/energy.json"
#define DAILY_ENERGY_JSON_PATH "/daily-energy.json"
#define FW_UPDATE_INFO_JSON_PATH "/fw-update-info.json"
#define FW_UPDATE_STATUS_JSON_PATH "/fw-update-status.json"

// Rollback and crash
#define FW_ROLLBACK_TXT "/fw-rollback-status.txt"
#define STABLE_FIRMWARE "stable"
#define NEW_FIRMWARE_TO_BE_TESTED "new_firmware"
#define NEW_FIRMWARE_TESTING "testing"
#define ROLLBACK_TESTING_TIMEOUT 60000 // Interval in which the firmware is being tested. If the ESP32 reboots unexpectedly, the firmware will be rolled back
#define CRASH_COUNTER_TXT "/crash-counter.txt"
#define CRASH_DATA_JSON "/crash-data.json"
#define CRASH_TRACKER_SIGNATURE 0xDEADBEEF
#define CRASH_MONITOR_WATCHDOG_TIMEOUT 300 // The watchdog timeout for the crash monitor (in seconds)
#define MAX_CRASH_COUNT 10 // The maximum number of crashes before the firmware is rolled back and the SPIFFS is formatted
#define CRASH_COUNTER_TIMEOUT 60000 // Interval in which the crash counter is reset

// Serial
#define SERIAL_BAUDRATE 115200 // Most common baudrate for ESP32

// Logger
#define LOG_PATH "/AdvancedLogger/log.txt"
#define LOG_CONFIG_PATH "/AdvancedLogger/config.txt"
#define LOG_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// Time
#define NTP_SERVER "pool.ntp.org"
#define TIME_SYNC_INTERVAL 3600 // Sync time every hour
#define DEFAULT_GMT_OFFSET 0
#define DEFAULT_DST_OFFSET 0
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// LED
#define LED_RED_PIN 38
#define LED_GREEN_PIN 39
#define LED_BLUE_PIN 37
#define DEFAULT_LED_BRIGHTNESS 191 // 75% of the maximum brightness
#define LED_MAX_BRIGHTNESS 255
#define LED_FREQUENCY 5000
#define LED_RESOLUTION 8

// WiFi
#define WIFI_CONFIG_PORTAL_TIMEOUT 300 // In seconds
#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_LOOP_INTERVAL 5000 // In milliseconds

// MDNS
#define MDNS_HOSTNAME "energyme"

// MQTT
#define DEFAULT_IS_CLOUD_SERVICES_ENABLED false
#define MAX_INTERVAL_METER_PUBLISH 30000 // The maximum interval between two meter payloads (in milliseconds)
#define MAX_INTERVAL_STATUS_PUBLISH 3600000 // The interval between two status publish (in milliseconds)
#define MQTT_MAX_CONNECTION_ATTEMPT 3 // The maximum number of attempts to connect to the MQTT server
#define MQTT_OVERRIDE_KEEPALIVE 30 // Minimum value supported by AWS IoT Core (in seconds) 
#define MQTT_MIN_CONNECTION_INTERVAL 5000 // Minimum interval between two connection attempts (in milliseconds)
#define MQTT_TEMPORARY_DISABLE_INTERVAL 3600000 // Interval between reconnect attempts after a failed connection (in milliseconds)
#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks (in milliseconds)
#define PAYLOAD_METER_MAX_NUMBER_POINTS 30 // The maximum number of points that can be sent in a single payload
#define MQTT_PAYLOAD_LIMIT 8192 // Increase the base limit of 256 bytes
#define MQTT_MAX_TOPIC_LENGTH 256 // The maximum length of a MQTT topic
#define MQTT_PROVISIONING_TIMEOUT 60000 // The timeout for the provisioning response (in milliseconds)
#define MQTT_PROVISIONING_LOOP_CHECK 1000 // Interval between two certificates check on memory (in milliseconds)

// Custom MQTT
#define DEFAULT_IS_CUSTOM_MQTT_ENABLED false
#define MQTT_CUSTOM_SERVER_DEFAULT "test.mosquitto.org"
#define MQTT_CUSTOM_PORT_DEFAULT 1883
#define MQTT_CUSTOM_CLIENTID_DEFAULT "energyme-home"
#define MQTT_CUSTOM_TOPIC_DEFAULT "topic"
#define MQTT_CUSTOM_FREQUENCY_DEFAULT 30000 // In milliseconds
#define MQTT_CUSTOM_USE_CREDENTIALS_DEFAULT false
#define MQTT_CUSTOM_USERNAME_DEFAULT "username"
#define MQTT_CUSTOM_PASSWORD_DEFAULT "password"
#define MQTT_CUSTOM_MAX_CONNECTION_ATTEMPT 5 // The maximum number of attempts to connect to the custom MQTT server
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
#define MULTIPLEXER_S0_PIN 36 
#define MULTIPLEXER_S1_PIN 35
#define MULTIPLEXER_S2_PIN 45
#define MULTIPLEXER_S3_PIN 48
#define MULTIPLEXER_CHANNEL_COUNT 16 // This cannot be defined as a constant because it is used for array initialization
#define CHANNEL_COUNT MULTIPLEXER_CHANNEL_COUNT + 1 // The number of channels being 1 (general) + 16 (multiplexer)

// ADE7953
// --------------------
// Hardware pins
#define SS_PIN 11 
#define SCK_PIN 14
#define MISO_PIN 13
#define MOSI_PIN 12
#define ADE7953_RESET_PIN 9

// Setup
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500 // In milliseconds

// Helper constants
#define CHANNEL_A 0
#define CHANNEL_B 1

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 
#define DEFAULT_X_NOLOAD_REGISTER 0x00C832 // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 20000:1, meaning that the no-load threshold is 0.005% of the full-scale value
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (disable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0xFF // 0xFF 0b11111111 (enable accumulation mode for all channels)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000000000100 // Enable bit 2, and 15 (keep HPF enabled, keep COMM_LOCK disabled)

// Fixed conversion values
#define POWER_FACTOR_CONVERSION_FACTOR 1.0 / 32768.0 // PF/LSB

// Sample time
#define DEFAULT_SAMPLE_CYCLES 100 // 100 cycles 1 second for 50Hz

// Validate values
#define VALIDATE_VOLTAGE_MIN 150.0 // Any voltage below this value is discarded
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
#define CERTIFICATE_PATH "/secret/cert.pem"
#define PRIVATE_KEY_PATH "/secret/private.pem"
#define CERTIFICATE_LENGTH 2048

// EnergyMe - Home | Custom MQTT topics
// Base topics
#define MQTT_TOPIC_1 "energyme"
#define MQTT_TOPIC_2 "home"

// Publish topics
#define MQTT_TOPIC_METER "meter"
#define MQTT_TOPIC_STATUS "status"
#define MQTT_TOPIC_METADATA "metadata"
#define MQTT_TOPIC_CHANNEL "channel"
#define MQTT_TOPIC_GENERAL_CONFIGURATION "general-configuration"
#define MQTT_TOPIC_CONNECTIVITY "connectivity"
#define MQTT_TOPIC_PROVISIONING_REQUEST "provisioning/request"

// Subscribe topics
#define MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE "firmware-update"
#define MQTT_TOPIC_SUBSCRIBE_RESTART "restart"
#define MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE "provisioning/response"
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// MQTT will
#define MQTT_WILL_QOS 1
#define MQTT_WILL_RETAIN true
#define MQTT_WILL_MESSAGE "{\"connectivity\":\"unexpected_offline\"}"

// AWS IoT Core endpoint
#define AWS_IOT_CORE_PORT 8883

#endif