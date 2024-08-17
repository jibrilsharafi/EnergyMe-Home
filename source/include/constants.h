#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "AdvancedLogger.h"

// Definitions
// --------------------

// Project info
#define COMPANY_NAME "EnergyMe"
#define FULL_PRODUCT_NAME "EnergyMe - Home"
#define PRODUCT_NAME "Home"
#define PRODUCT_DESCRIPTION "A open-source energy monitoring system for home use, capable of monitoring up to 17 circuits."
#define PRODUCT_URL "https://energyme.net"
#define GITHUB_URL "https://github.com/jibrilsharafi/EnergyMe-Home"
#define AUTHOR "Jibril Sharafi"
#define AUTHOR_EMAIL "jibril.sharafi@gmail.com"
#define COPYRIGHT_YEAR "2024"
#define COPYRIGHT_HOLDER "Jibril Sharafi"
#define LICENSE "MIT License"
#define LICENSE_URL "https://opensource.org/licenses/MIT"

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "04"
#define FIRMWARE_BUILD_VERSION_PATCH "15"
#define FIRMWARE_BUILD_VERSION FIRMWARE_BUILD_VERSION_MAJOR "." FIRMWARE_BUILD_VERSION_MINOR "." FIRMWARE_BUILD_VERSION_PATCH

#define FIRMWARE_BUILD_DATE_YEAR "2024"
#define FIRMWARE_BUILD_DATE_MONTH "08"
#define FIRMWARE_BUILD_DATE_DAY "17"
#define FIRMWARE_BUILD_DATE FIRMWARE_BUILD_DATE_YEAR "-" FIRMWARE_BUILD_DATE_MONTH "-" FIRMWARE_BUILD_DATE_DAY

// Measurements
#define VOLTAGE_MEASUREMENT 1
#define CURRENT_MEASUREMENT 2
#define ACTIVE_POWER_MEASUREMENT 3
#define REACTIVE_POWER_MEASUREMENT 4
#define APPARENT_POWER_MEASUREMENT 5
#define POWER_FACTOR_MEASUREMENT 6

// ADE7953
#define CHANNEL_A 0
#define CHANNEL_B 1
#define DEFAULT_NUMBER_CALIBRATION_VALUES 2 // This cannot be defined as a constant because it is used for array initialization
#define MAXIMUM_NUMBER_CALIBRATION_VALUES 10 // This cannot be defined as a constant because it is used for array initialization
#define MAX_SAMPLES_AVERAGE_MEASUREMENT 1000 // The maximum number of samples to average the measurements. 1000 should be 1 data point per ms, per second
#define MAX_DURATION_AVERAGE_MEASUREMENT 1000 // Milliseconds, meaning that the maximum duration of the average measurement is 1 second

// Multiplexer
#define MULTIPLEXER_CHANNEL_COUNT 16 // This cannot be defined as a constant because it is used for array initialization

// MQTT Payload
#define MAX_NUMBER_POINTS_PAYLOAD 30 // The maximum number of points that can be sent in a single payload. 60 is about 5kB
#define MQTT_PAYLOAD_LIMIT 5000 // Every 5000 bytes is a single message
#define MAX_MQTT_TOPIC_LENGTH 300 // The maximum length of a MQTT topic

// URL Utilities
#define PUBLIC_IP_ENDPOINT "http://checkip.amazonaws.com/"
#define PUBLIC_LOCATION_ENDPOINT "http://ip-api.com/json/"
#define PUBLIC_TIMEZONE_ENDPOINT "http://api.geonames.org/timezoneJSON?"
#define PUBLIC_TIMEZONE_USERNAME "energymehome"

// Constants
// --------------------

// File path
extern const char* METADATA_JSON_PATH;
extern const char* GENERAL_CONFIGURATION_JSON_PATH;
extern const char* CONFIGURATION_ADE7953_JSON_PATH;
extern const char* CALIBRATION_JSON_PATH;
extern const char* CHANNEL_DATA_JSON_PATH;
extern const char* LOGGER_JSON_PATH;
extern const char* ENERGY_JSON_PATH;
extern const char* DAILY_ENERGY_JSON_PATH;
extern const char* FACTORY_PATH;
extern const char* FIRMWARE_UPDATE_INFO_PATH;
extern const char* FIRMWARE_UPDATE_STATUS_PATH;

// Serial
extern const int SERIAL_BAUDRATE; // Fastest baudrate for ESP32

// Logger
extern const char* LOG_PATH;
extern const char* LOG_CONFIG_PATH;
extern const char* LOG_TIMESTAMP_FORMAT;
extern const int LOG_FILE_MAX_LENGTH;
extern const LogLevel DEFAULT_LOG_PRINT_LEVEL;
extern const LogLevel DEFAULT_LOG_SAVE_LEVEL;

// Time
extern const char* NTP_SERVER;
extern const int TIME_SYNC_INTERVAL; // 1 hour
extern const int DEFAULT_GMT_OFFSET; // 1 hour
extern const int DEFAULT_DST_OFFSET;

// LED
extern const int LED_RED_PIN;
extern const int LED_GREEN_PIN;
extern const int LED_BLUE_PIN;
extern const int LED_DEFAULT_BRIGHTNESS;
extern const int LED_MAX_BRIGHTNESS;
extern const int LED_FREQUENCY;
extern const int LED_RESOLUTION;

// WiFi
extern const int WIFI_CONFIG_PORTAL_TIMEOUT; // 3 minutes
extern const char* WIFI_CONFIG_PORTAL_SSID;

// MDNS
extern const char* MDNS_HOSTNAME;

// Cloud services
extern const bool DEFAULT_IS_CLOUD_SERVICES_ENABLED;
extern const int MAX_INTERVAL_PAYLOAD; // The maximum interval between two payloads

// MQTT
extern const int MQTT_MAX_CONNECTION_ATTEMPT; // The maximum number of attempts to connect to the MQTT broker
extern const int MQTT_OVERRIDE_KEEPALIVE; // The default value is 15 seconds, which is too low for the AWS IoT MQTT broker
extern const int MQTT_STATUS_PUBLISH_INTERVAL; // In seconds
extern const int MQTT_MIN_CONNECTION_INTERVAL; // In milliseconds, representing the minimum interval between two connection attempts
extern const int MQTT_LOOP_INTERVAL; // In milliseconds, representing the interval between two MQTT loop calls (needed for the subscribe function - https://github.com/knolleary/pubsubclient/issues/163)

// Conversion factors
extern const float BYTE_TO_KILOBYTE; 
extern const float MILLIS_TO_HOURS;

// Saving data
extern const int ENERGY_SAVE_INTERVAL; // In seconds

// ESP32 status
extern const int MINIMUM_FREE_HEAP_SIZE; // Below this value, the ESP32 will restart
extern const int MINIMUM_FREE_SPIFFS_SIZE; // Below this value, the ESP32 will clear the logs

// Multiplexer
// --------------------
extern const int MULTIPLEXER_S0_PIN;
extern const int MULTIPLEXER_S1_PIN;
extern const int MULTIPLEXER_S2_PIN;
extern const int MULTIPLEXER_S3_PIN;

// ADE7953
// --------------------

// Hardware pins
extern const int SS_PIN;
extern const int SCK_PIN;
extern const int MISO_PIN;
extern const int MOSI_PIN;
extern const int ADE7953_RESET_PIN;

// Default values for ADE7953 registers
extern const int DEFAULT_EXPECTED_AP_NOLOAD_REGISTER;
extern const int DEFAULT_X_NOLOAD_REGISTER;
extern const int DEFAULT_DISNOLOAD_REGISTER;
extern const int DEFAULT_CONFIG_REGISTER;
extern const int DEFAULT_LCYCMODE_REGISTER;
extern const int DEFAULT_LINECYC_REGISTER;
extern const int DEFAULT_PGA_REGISTER;
extern const int DEFAULT_CONFIG_REGISTER;

// Default calibration values
extern const int DEFAULT_AWGAIN;
extern const int DEFAULT_AWATTOS;
extern const int DEFAULT_AVARGAIN;
extern const int DEFAULT_AVAROS;
extern const int DEFAULT_AVAGAIN;
extern const int DEFAULT_AVAOS;
extern const int DEFAULT_AIGAIN;
extern const int DEFAULT_AIRMSOS;
extern const int DEFAULT_BIGAIN;
extern const int DEFAULT_BIRMSOS;
extern const int DEFAULT_PHCALA;
extern const int DEFAULT_PHCALB;

// Fixed conversion values
extern const float POWER_FACTOR_CONVERSION_FACTOR; // PF/LSB

// Sample time
extern const int SAMPLE_CYCLES;

// Validate values
extern const float VALIDATE_VOLTAGE_MIN;
extern const float VALIDATE_VOLTAGE_MAX;
extern const float VALIDATE_CURRENT_MIN;
extern const float VALIDATE_CURRENT_MAX;
extern const float VALIDATE_POWER_MIN;
extern const float VALIDATE_POWER_MAX;
extern const float VALIDATE_POWER_FACTOR_MIN;
extern const float VALIDATE_POWER_FACTOR_MAX;

// Modbus TCP
// --------------------
extern const int MODBUS_TCP_PORT;
extern const int MODBUS_TCP_MAX_CLIENTS;
extern const int MODBUS_TCP_TIMEOUT; // In ms
extern const int MODBUS_TCP_SERVER_ID;

#endif