#pragma once

// Note: all the durations hereafter are in milliseconds unless specified otherwise

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "11"
#define FIRMWARE_BUILD_VERSION_PATCH "00"
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

// Authentication
#define PREFERENCES_NAMESPACE_AUTH "auth_ns" 
#define PREFERENCES_KEY_PASSWORD "password"
#define WEBSERVER_DEFAULT_USERNAME "admin"
#define WEBSERVER_DEFAULT_PASSWORD "energyme"
#define WEBSERVER_REALM "EnergyMe-Home"
#define MAX_PASSWORD_LENGTH 64
#define MIN_PASSWORD_LENGTH 4
#define AUTH_SESSION_TIMEOUT (24 * 60 * 60 * 1000) // 24 hours in milliseconds
#define AUTH_TOKEN_LENGTH 32
#define MAX_CONCURRENT_SESSIONS 10 // Maximum number of concurrent login sessions

// Preferences namespaces for configuration storage
#define PREFERENCES_NAMESPACE_GENERAL "general_ns"
#define PREFERENCES_NAMESPACE_ADE7953 "ade7953_ns"
#define PREFERENCES_NAMESPACE_CALIBRATION "calibration_ns"
#define PREFERENCES_NAMESPACE_CHANNELS "channels_ns"
#define PREFERENCES_NAMESPACE_MQTT "mqtt_ns"
#define PREFERENCES_NAMESPACE_INFLUXDB "influxdb_ns"
#define PREFERENCES_NAMESPACE_BUTTON "button_ns"
#define PREFERENCES_NAMESPACE_WIFI "wifi_ns"
#define PREFERENCES_NAMESPACE_TIME "time_ns"
#define PREFERENCES_NAMESPACE_CRASHMONITOR "crashmonitor_ns"
#define PREFERENCES_NAMESPACE_CERTIFICATES "certificates_ns"
#define PREFERENCES_NAMESPACE_LED "led_ns"

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
#define PREF_KEY_MQTT_ENABLED "enabled"
#define PREF_KEY_MQTT_SERVER "server"
#define PREF_KEY_MQTT_PORT "port"
#define PREF_KEY_MQTT_USERNAME "username"
#define PREF_KEY_MQTT_PASSWORD "password"
#define PREF_KEY_MQTT_CLIENT_ID "clientId"
#define PREF_KEY_MQTT_TOPIC_PREFIX "topicPrefix"
#define PREF_KEY_MQTT_PUBLISH_INTERVAL "publishInterval"
#define PREF_KEY_MQTT_USE_CREDENTIALS "useCredentials"
#define PREF_KEY_MQTT_TOPIC "topic"
#define PREF_KEY_MQTT_FREQUENCY "frequency"

// Preferences keys for channel configuration (per channel, format: "ch<N>_<property>")
#define PREF_KEY_CHANNEL_ACTIVE_FMT "ch%d_active"
#define PREF_KEY_CHANNEL_REVERSE_FMT "ch%d_reverse"
#define PREF_KEY_CHANNEL_LABEL_FMT "ch%d_label"
#define PREF_KEY_CHANNEL_PHASE_FMT "ch%d_phase"
#define PREF_KEY_CHANNEL_CALIB_LABEL_FMT "ch%d_calibLabel"

// Rate limiting for DoS protection
#define MAX_LOGIN_ATTEMPTS 10 // Maximum failed login attempts before IP is blocked
#define LOGIN_BLOCK_DURATION (15 * 60 * 1000) // 15 minutes in milliseconds
#define RATE_LIMIT_CLEANUP_INTERVAL (5 * 60 * 1000) // 5 minutes - cleanup old entries
#define MAX_TRACKED_IPS 20 // Maximum number of IP addresses to track (memory constraint)

// Serial
#define SERIAL_BAUDRATE 115200 // Most common baudrate for ESP32

// While loops
#define MAX_LOOP_ITERATIONS 1000 // The maximum number of iterations for a while loop

// Main
#define MAINTENANCE_CHECK_INTERVAL (10 * 1000) // Interval to check main parameters, to avoid overloading the loop

// Logger
#define LOG_PATH "/logger/log.txt"
#define LOG_CONFIG_PATH "/logger/config.txt"
#define LOG_TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"

// Buffer Sizes for String Operations
// =================================

// Small buffers (8-32 bytes)
#define DEVICE_ID_BUFFER_SIZE 16      // For device IDs (increased slightly for safety)
#define VERSION_BUFFER_SIZE 16        // For firmware version strings (major.minor.patch format)
#define TOKEN_SHORT_KEY_BUFFER_SIZE 16 // For auth tokens "t" + 8-char hex + null terminator = 10 chars max, 16 for safety
#define DATE_BUFFER_SIZE 11           // For date strings (YYYY-MM-DD + null terminator)
#define CLIENT_ID_BUFFER_SIZE 32      // For MQTT client IDs
#define DATABASE_NAME_BUFFER_SIZE 32  // For database names
#define ORGANIZATION_BUFFER_SIZE 32   // For organization names
#define MEASUREMENT_BUFFER_SIZE 32    // For measurement names
#define TIMESTAMP_STRING_BUFFER_SIZE 32 // For timestamp strings
#define MD5_BUFFER_SIZE 33            // For MD5 hashes (32 chars + null terminator)

// Medium buffers (64-128 bytes)
#define LABEL_BUFFER_SIZE 64          // For sanitized channel labels
#define CALIBRATION_LABEL_BUFFER_SIZE 64 // For calibration labels
#define FUNCTION_NAME_BUFFER_SIZE 64  // For function names in restart configuration
#define SERVER_NAME_BUFFER_SIZE 64    // For MQTT/InfluxDB server names
#define MQTT_TOPIC_BUFFER_SIZE 64          // For MQTT topics
#define USERNAME_BUFFER_SIZE 64       // For usernames
#define PASSWORD_BUFFER_SIZE 64       // For passwords
#define BUCKET_NAME_BUFFER_SIZE 64    // For bucket names
#define FIRMWARE_STATUS_BUFFER_SIZE 64 // For firmware status messages
#define ENCRYPTION_KEY_BUFFER_SIZE 64 // For encryption keys (preshared key + device ID)
#define AUTH_PASSWORD_BUFFER_SIZE 64  // For authentication passwords
#define CHARS_TOKEN_BUFFER_SIZE 64    // For characters in token strings
#define TOKEN_FULL_KEY_BUFFER_SIZE 64 // Full auth tokens
#define BUTTON_HANDLER_OPERATION_BUFFER_SIZE 64 // For button handler operations
#define CHANNEL_LABEL_BUFFER_SIZE 128 // For channel labels (let's be generous come on!)
#define TOKEN_BUFFER_SIZE 128         // For authentication tokens
#define COUNTRY_BUFFER_SIZE 128       // For country names
#define CITY_BUFFER_SIZE 128          // For city names
#define AUTH_HEADER_BUFFER_SIZE 128   // For HTTP authorization headers
#define STATUS_BUFFER_SIZE 128         // For connection status messages

// Large buffers (256-512 bytes)
#define REASON_BUFFER_SIZE 128        // For restart reasons
#define URL_BUFFER_SIZE 128           // For HTTP URLs
#define ENCODED_CREDENTIALS_BUFFER_SIZE 256 // For base64 encoded credentials
#define FILENAME_BUFFER_SIZE 256      // For file names in the filesystem
#define LINE_PROTOCOL_BUFFER_SIZE 512 // For InfluxDB line protocol strings
#define DECRYPTION_BUFFER_SIZE 512    // For decrypted data (certificates, tokens, etc.)
#define JSON_STRING_PRINT_BUFFER_SIZE 512 // For JSON strings (print only, needed usually for debugging - Avoid being too large to prevent stack overflow)
#define PUBLIC_LOCATION_PAYLOAD_BUFFER_SIZE 512 // For public location API payloads
#define HTTP_RESPONSE_BUFFER_SIZE 512 // For HTTP response strings
#define JSON_RESPONSE_BUFFER_SIZE 2048 // For JSON response strings (reduced from 512B)
#define FULLURL_BUFFER_SIZE 512       // For full URL with query parameters
#define MQTT_SUBSCRIBE_MESSAGE_BUFFER_SIZE 512 // For MQTT subscribe messages (reduced from 1KB)
#define JSON_MQTT_BUFFER_SIZE 512     // For MQTT JSON payloads

// Very large buffers (1KB+)
#define ENCRYPTED_DATA_BUFFER_SIZE 1024 // For encrypted data storage (reduced from 2KB)
#define DECRYPTION_WORKING_BUFFER_SIZE 1024 // Working buffer for decryption operations (reduced from 2KB)
#define JSON_MQTT_LARGE_BUFFER_SIZE 2048 // For larger JSON payloads in MQTT messages
#define AWS_IOT_CORE_CERT_BUFFER_SIZE 2048 // For AWS IoT Core certificate
#define AWS_IOT_CORE_PRIVATE_KEY_BUFFER_SIZE 2048 // For AWS IoT Core private key
#define PAYLOAD_BUFFER_SIZE 1024 // For InfluxDB payload (reduced from 16KB to 4KB)

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

// Saving date
#define SAVE_ENERGY_INTERVAL (6 * 60 * 1000) // Time between each energy save to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

// ESP32 status
#define MINIMUM_FREE_HEAP_SIZE 5000 // Below this value (in bytes), the ESP32 will restart
#define MINIMUM_FREE_SPIFFS_SIZE 100000 // Below this value (in bytes), the ESP32 will clear the log
#define ESP32_RESTART_DELAY (2 * 1000) // The delay before restarting the ESP32 after a restart request, needed to allow the ESP32 to finish the current operations
#define MINIMUM_FREE_HEAP_OTA 20000 // Below this, the OTA is rejected (a bit unsafe, this could block OTA)

// Multiplexer
// --------------------
#define MULTIPLEXER_CHANNEL_COUNT 16

// ADE7953
// --------------------
#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100 // Timeout for acquiring SPI mutex to prevent deadlocks

// Task
#define ADE7953_METER_READING_TASK_NAME "ade7953_task" // The name of the ADE7953 task
#define ADE7953_METER_READING_TASK_STACK_SIZE (8 * 1024) // The stack size for the ADE7953 task
#define ADE7953_METER_READING_TASK_PRIORITY 2 // The priority for the ADE7953 task

// Memory safety for JSON operations
#define JSON_SAFE_MIN_HEAP_SIZE (16 * 1024) // Minimum free heap required for JSON operations

// Interrupt handling
#define ADE7953_INTERRUPT_TIMEOUT_MS 1000 // Timeout for waiting on interrupt semaphore (in ms)

// Setup
#define ADE7953_RESET_LOW_DURATION 200 // The duration for the reset pin to be low
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500 

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 
#define DEFAULT_X_NOLOAD_REGISTER 0x00E419 // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 10000:1, meaning that the no-load threshold is 0.01% of the full-scale value
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (enable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0b01111111 // 0xFF 0b01111111 (enable accumulation mode for all channels, disable read with reset)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000100001100 // Enable bit 2, bit 3 (line accumulation for PF), 8 (CRC is enabled), and 15 (keep HPF enabled, keep COMM_LOCK disabled)
#define DEFAULT_GAIN 4194304 // 0x400000 (default gain for the ADE7953)
#define DEFAULT_OFFSET 0 // 0x000000 (default offset for the ADE7953)
#define DEFAULT_PHCAL 10 // 0.02°/LSB, indicating a phase calibration of 0.2° which is minimum needed for CTs
#define DEFAULT_IRQENA_REGISTER 0b001101000000000000000000 // Enable CYCEND interrupt (bit 18) and Reset (bit 20, mandatory) for line cycle end detection
#define MINIMUM_SAMPLE_TIME 200

// IRQSTATA / RSTIRQSTATA Register Bit Positions (Table 23, ADE7953 Datasheet)
#define IRQSTATA_AEHFA_BIT         0  // Active energy register half full (Current Channel A)
#define IRQSTATA_VAREHFA_BIT       1  // Reactive energy register half full (Current Channel A)
#define IRQSTATA_VAEHFA_BIT        2  // Apparent energy register half full (Current Channel A)
#define IRQSTATA_AEOFA_BIT         3  // Active energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAREOFA_BIT       4  // Reactive energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAEOFA_BIT        5  // Apparent energy register overflow/underflow (Current Channel A)
#define IRQSTATA_AP_NOLOADA_BIT    6  // Active power no-load detected (Current Channel A)
#define IRQSTATA_VAR_NOLOADA_BIT   7  // Reactive power no-load detected (Current Channel A)
#define IRQSTATA_VA_NOLOADA_BIT    8  // Apparent power no-load detected (Current Channel A)
#define IRQSTATA_APSIGN_A_BIT      9  // Sign of active energy changed (Current Channel A)
#define IRQSTATA_VARSIGN_A_BIT     10 // Sign of reactive energy changed (Current Channel A)
#define IRQSTATA_ZXTO_IA_BIT       11 // Zero crossing missing on Current Channel A
#define IRQSTATA_ZXIA_BIT          12 // Current Channel A zero crossing detected
#define IRQSTATA_OIA_BIT           13 // Current Channel A overcurrent threshold exceeded
#define IRQSTATA_ZXTO_BIT          14 // Zero crossing missing on voltage channel
#define IRQSTATA_ZXV_BIT           15 // Voltage channel zero crossing detected
#define IRQSTATA_OV_BIT            16 // Voltage peak overvoltage threshold exceeded
#define IRQSTATA_WSMP_BIT          17 // New waveform data acquired
#define IRQSTATA_CYCEND_BIT        18 // End of line cycle accumulation period
#define IRQSTATA_SAG_BIT           19 // Sag event occurred
#define IRQSTATA_RESET_BIT         20 // End of software or hardware reset
#define IRQSTATA_CRC_BIT           21 // Checksum has changed

// Fixed conversion values
#define POWER_FACTOR_CONVERSION_FACTOR 1.0f / 32768.0f // PF/LSB
#define ANGLE_CONVERSION_FACTOR 360.0f * 50.0f / 223000.0f // 0.0807 °/LSB
#define GRID_FREQUENCY_CONVERSION_FACTOR 223750.0f // Clock of the period measurement, in Hz. To be multiplied by the register value of 0x10E

// Validate values
#define VALIDATE_VOLTAGE_MIN 50.0f // Any voltage below this value is discarded
#define VALIDATE_VOLTAGE_MAX 300.0f  // Any voltage above this value is discarded
#define VALIDATE_CURRENT_MIN -300.0f // Any current below this value is discarded
#define VALIDATE_CURRENT_MAX 300.0f // Any current above this value is discarded
#define VALIDATE_POWER_MIN -100000.0f // Any power below this value is discarded
#define VALIDATE_POWER_MAX 100000.0f // Any power above this value is discarded
#define VALIDATE_POWER_FACTOR_MIN -1.0f // Any power factor below this value is discarded
#define VALIDATE_POWER_FACTOR_MAX 1.0f // Any power factor above this value is discarded
#define VALIDATE_GRID_FREQUENCY_MIN 45.0f // Minimum grid frequency in Hz
#define VALIDATE_GRID_FREQUENCY_MAX 65.0f // Maximum grid frequency in Hz

// Guardrails and thresholds
#define MAXIMUM_POWER_FACTOR_CLAMP 1.05f // Values above 1 but below this are still accepted
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.01f // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used
#define MINIMUM_POWER_FACTOR 0.05f // Measuring such low power factors is virtually impossible with such CTs
#define MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE 100 // Threshold to transition to a legitimate zero state for channel 0
#define ADE7953_MIN_LINECYC 10U // The minimum line cycle settable for the ADE7953. Must be unsigned
#define ADE7953_MAX_LINECYC 1000U // The maximum line cycle settable for the ADE7953. Must be unsigned

#define INVALID_SPI_READ_WRITE 0xDEADDEAD // Custom, used to indicate an invalid SPI read/write operation

// ADE7953 Smart Failure Detection
#define ADE7953_MAX_FAILURES_BEFORE_RESTART 100
#define ADE7953_FAILURE_RESET_TIMEOUT_MS (1 * 60 * 1000)

// Check for incorrect readings
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE 100.0f // Absolute difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE 0.20f // Relative difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded

// Server used ports (here to ensure no conflicts)
#define MODBUS_TCP_PORT 502 // The default port for Modbus TCP
#define WEBSERVER_PORT 80