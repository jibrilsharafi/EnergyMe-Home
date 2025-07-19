#pragma once

// Note: all the durations hereafter are in milliseconds unless specified otherwise

// Hardware revision: EnergyMe - Home v5 (02-12-2024)

// Firmware info
#define FIRMWARE_BUILD_VERSION_MAJOR "00"
#define FIRMWARE_BUILD_VERSION_MINOR "10"
#define FIRMWARE_BUILD_VERSION_PATCH "06"
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
#define MINIMUM_TIME_BEFORE_VALID_METER 15000 // Before this do not buffer or upload meter values (avoid incorrect readings just after boot)

// File path
#define GENERAL_CONFIGURATION_JSON_PATH "/config/general.json"
#define CONFIGURATION_ADE7953_JSON_PATH "/config/ade7953.json"
#define CALIBRATION_JSON_PATH "/config/calibration.json"
#define CHANNEL_DATA_JSON_PATH "/config/channel.json"
#define CUSTOM_MQTT_CONFIGURATION_JSON_PATH "/config/custommqtt.json"
#define INFLUXDB_CONFIGURATION_JSON_PATH "/config/influxdb.json"
#define ENERGY_JSON_PATH "/energy.json"
#define DAILY_ENERGY_JSON_PATH "/daily-energy.json"
#define FW_UPDATE_INFO_JSON_PATH "/fw-update-info.json"
#define FW_UPDATE_STATUS_JSON_PATH "/fw-update-status.json"

// Crash monitor
#define PREFERENCES_NAMESPACE_CRASHMONITOR "crashmonitor"
#define PREFERENCES_DATA_KEY "crashdata"
#define CRASH_SIGNATURE 0xDEADBEEF
#define MAX_BREADCRUMBS 8
#define WATCHDOG_TIMER (30 * 1000) // If the esp_task_wdt_reset() is not called within this time, the ESP32 panics
#define PREFERENCES_FIRMWARE_STATUS_KEY "fw_status"
#define ROLLBACK_TESTING_TIMEOUT (60 * 1000) // Interval in which the firmware is being tested. If the ESP32 reboots unexpectedly, the firmware will be rolled back
#define MAX_CRASH_COUNT 5 // Maximum amount of consecutive crashes before triggering a rollback
#define CRASH_COUNTER_TIMEOUT (180 * 1000) // Timeout for the crash counter to reset

// Authentication
#define PREFERENCES_NAMESPACE_AUTH "auth"
#define PREFERENCES_KEY_PASSWORD "password"
#define DEFAULT_WEB_PASSWORD "energyme"
#define DEFAULT_WEB_USERNAME "admin"
#define MAX_PASSWORD_LENGTH 64
#define MIN_PASSWORD_LENGTH 4
#define AUTH_SESSION_TIMEOUT (24 * 60 * 60 * 1000) // 24 hours in milliseconds
#define AUTH_TOKEN_LENGTH 32
#define MAX_CONCURRENT_SESSIONS 10 // Maximum number of concurrent login sessions

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
#define LOG_BUFFER_SIZE 50 // Callback queue size
#define LOG_JSON_BUFFER_SIZE 1024
#define LOG_TOPIC_SIZE 64

// UDP Logging
#define UDP_LOG_PORT 514 // Standard syslog port
#define UDP_LOG_BUFFER_SIZE 512 // Smaller buffer for UDP packets
#define DEFAULT_IS_UDP_LOGGING_ENABLED true

// Time
#define NTP_SERVER "pool.ntp.org"
#define TIME_SYNC_INTERVAL (60 * 60) // Sync time every hour (in seconds)
#define DEFAULT_GMT_OFFSET 0
#define DEFAULT_DST_OFFSET 0
#define TIME_SYNC_RETRY_INTERVAL (60 * 1000) // Retry sync if failed
#define TIMESTAMP_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIMESTAMP_BUFFER_SIZE 32  // Size needed for TIMESTAMP_FORMAT (19 chars + null terminator) plus some extra space for safety

// Buffer Sizes for String Operations
#define URL_BUFFER_SIZE 256           // For HTTP URLs
#define LINE_PROTOCOL_BUFFER_SIZE 512 // For InfluxDB line protocol strings
#define LABEL_BUFFER_SIZE 64          // For sanitized channel labels
#define DEVICE_ID_BUFFER_SIZE 16      // For device IDs (increased slightly for safety)

// Webserver
#define WEBSERVER_PORT 80
#define API_UPDATE_THROTTLE_MS 500 // Minimum time between updates

// LED
// Hardware pins (check hardware revision)
#define LED_RED_PIN 39
#define LED_GREEN_PIN 40
#define LED_BLUE_PIN 38

#define DEFAULT_LED_BRIGHTNESS 191 // 75% of the maximum brightness
#define LED_MAX_BRIGHTNESS 255
#define LED_FREQUENCY 5000
#define LED_RESOLUTION 8

// Button
// Hardware pins (check hardware revision)
#define BUTTON_GPIO0_PIN 0 // GPIO0 button for critical functionality control

// Button timing constants
#define BUTTON_DEBOUNCE_TIME 50 // Debounce time in milliseconds
#define BUTTON_SHORT_PRESS_TIME 2000 // Short press duration (Restart)
#define BUTTON_MEDIUM_PRESS_TIME 6000 // Medium press duration (Password reset)
#define BUTTON_LONG_PRESS_TIME 10000 // Long press duration (WiFi reset)
#define BUTTON_VERY_LONG_PRESS_TIME 15000 // Very long press duration (Factory reset)
#define BUTTON_MAX_PRESS_TIME 20000 // Maximum press duration

// Button press patterns
#define BUTTON_DOUBLE_PRESS_INTERVAL 500 // Maximum time between presses for double press detection
#define BUTTON_TRIPLE_PRESS_INTERVAL 500 // Maximum time between presses for triple press detection

// WiFi
#define WIFI_CONFIG_PORTAL_SSID "EnergyMe"
#define WIFI_LOOP_INTERVAL (1 * 1000) 
#define WIFI_CONNECT_TIMEOUT 60 // In seconds
#define WIFI_PORTAL_TIMEOUT (3 * 60) // In seconds
#define WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS 5 // Maximum WiFi reconnection attempts before restart
#define WIFI_RECONNECT_DELAY_BASE (5 * 1000) // Base delay for exponential backoff
#define WIFI_STABLE_CONNECTION (5 * 60 * 1000) // Duration of uninterrupted WiFi connection to reset the reconnection counter

// MDNS
#define MDNS_HOSTNAME "energyme"

// MQTT
#if HAS_SECRETS
#define DEFAULT_IS_CLOUD_SERVICES_ENABLED true // If we compile with secrets, we might as well just directly connect
#else
#define DEFAULT_IS_CLOUD_SERVICES_ENABLED false
#endif
#define MQTT_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define MQTT_MAX_INTERVAL_STATUS_PUBLISH (60 * 60 * 1000) // The interval between two status publish
#define MQTT_MAX_INTERVAL_STATISTICS_PUBLISH (60 * 60 * 1000) // The interval between two statistics publish
#define MQTT_MAX_INTERVAL_CRASH_MONITOR_PUBLISH (60 * 60 * 1000) // The interval between two status publish
#define MQTT_CLAIM_MAX_CONNECTION_ATTEMPT 10 // The maximum number of attempts to connect to AWS IoT Core MQTT broker for claiming certificates
#define MQTT_OVERRIDE_KEEPALIVE 30 // 30 is the minimum value supported by AWS IoT Core (in seconds)
#define MQTT_MIN_CONNECTION_INTERVAL (5 * 1000) // Minimum interval between two connection attempts
#define MQTT_INITIAL_RECONNECT_INTERVAL (5 * 1000) // Initial interval for MQTT reconnection attempts
#define MQTT_MAX_RECONNECT_INTERVAL (5 * 60 * 1000) // Maximum interval for MQTT reconnection attempts
#define MQTT_RECONNECT_MULTIPLIER 2 // Multiplier for exponential backoff
#define MQTT_LOOP_INTERVAL 100 // Interval between two MQTT loop checks
#define MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS 150 // The maximum number of points that can be sent in a single payload. Going higher than about 150 leads to unstable connections
#define MQTT_PAYLOAD_LIMIT 32768 // Increase the base limit of 256 bytes. Increasing this over 32768 bytes will lead to unstable connections
#define MQTT_MAX_TOPIC_LENGTH 256 // The maximum length of a MQTT topic
#define MQTT_PROVISIONING_TIMEOUT (60 * 1000) // The timeout for the provisioning response
#define MQTT_PROVISIONING_LOOP_CHECK (1 * 1000) // Interval between two certificates check on memory
#define MQTT_DEBUG_LOGGING_DEFAULT_DURATION (3 * 60 * 1000) 
#define MQTT_DEBUG_LOGGING_MAX_DURATION (60 * 60 * 1000)
#define DEBUG_FLAGS_RTC_SIGNATURE 0xDEB6F1A6 // Used to verify the RTC data validity for MQTT debugging struct

// General Configuration Defaults
#define DEFAULT_SEND_POWER_DATA true // Default for sending active power and power factor data

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
#define MQTT_CUSTOM_PAYLOAD_LIMIT 8192 // Increase the base limit of 256 bytes

// InfluxDB Configuration Defaults
#define DEFAULT_IS_INFLUXDB_ENABLED false
#define INFLUXDB_SERVER_DEFAULT "localhost"
#define INFLUXDB_PORT_DEFAULT 8086
#define INFLUXDB_VERSION_DEFAULT 2
#define INFLUXDB_DATABASE_DEFAULT "energyme-home"
#define INFLUXDB_USERNAME_DEFAULT ""
#define INFLUXDB_PASSWORD_DEFAULT ""
#define INFLUXDB_ORGANIZATION_DEFAULT "my-org"
#define INFLUXDB_BUCKET_DEFAULT "energyme-home"
#define INFLUXDB_TOKEN_DEFAULT ""
#define INFLUXDB_MEASUREMENT_DEFAULT "meter"
#define INFLUXDB_FREQUENCY_DEFAULT 15  // In seconds
#define INFLUXDB_BUFFER_SIZE 50 // The number of points to buffer before sending to InfluxDB
#define INFLUXDB_USE_SSL_DEFAULT false
#define INFLUXDB_LOOP_INTERVAL 100 // Interval between two InfluxDB loop checks
#define INFLUXDB_MAX_INTERVAL_METER_PUBLISH (60 * 1000) // The maximum interval between two meter payloads
#define INFLUXDB_FREQUENCY_UPLOAD_MULTIPLIER 10 // This is to upload the data only every 10x the frequency set in the InfluxDB configuration, to avoid doing too many HTTP requests and slowing down the ESP32
#define INFLUXDB_INITIAL_RECONNECT_INTERVAL (30 * 1000) // Initial interval for InfluxDB reconnection attempts
#define INFLUXDB_MAX_RECONNECT_INTERVAL (15 * 60 * 1000) // Maximum interval for InfluxDB reconnection attempts
#define INFLUXDB_RECONNECT_MULTIPLIER 2 // Multiplier for InfluxDB exponential backoff
#define INFLUXDB_MIN_CONNECTION_INTERVAL (30 * 1000) // Minimum interval between two connection attempts
#define INFLUXDB_MAX_CONNECTION_ATTEMPTS 10 // Maximum number of failed attempts before disabling InfluxDB

// Saving date
#define SAVE_ENERGY_INTERVAL (6 * 60 * 1000) // Time between each energy save to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

// ESP32 status
#define MINIMUM_FREE_HEAP_SIZE 10000 // Below this value (in bytes), the ESP32 will restart
#define MINIMUM_FREE_SPIFFS_SIZE 100000 // Below this value (in bytes), the ESP32 will clear the log
#define ESP32_RESTART_DELAY (2 * 1000) // The delay before restarting the ESP32 after a restart request, needed to allow the ESP32 to finish the current operations
#define MINIMUM_FREE_HEAP_OTA 20000 // Below this, the OTA is rejected (a bit unsafe, this could block OTA)

// Multiplexer
// --------------------
// Hardware pins (check hardware revision)
#define MULTIPLEXER_S0_PIN 10
#define MULTIPLEXER_S1_PIN 11
#define MULTIPLEXER_S2_PIN 3
#define MULTIPLEXER_S3_PIN 9
#define MULTIPLEXER_CHANNEL_COUNT 16 // This cannot be defined as a constant because it is used for array initialization

// ADE7953
// --------------------
// Hardware pins (check hardware revision)
#define SS_PIN 48
#define SCK_PIN 36
#define MISO_PIN 35
#define MOSI_PIN 45
#define ADE7953_RESET_PIN 21
#define ADE7953_INTERRUPT_PIN 37
#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100 // Timeout for acquiring SPI mutex to prevent deadlocks

// Task
#define ADE7953_METER_READING_TASK_NAME "ade7953_task" // The name of the ADE7953 task
#define ADE7953_METER_READING_TASK_STACK_SIZE (16 * 1024) // The stack size for the ADE7953 task (increased from 8192 to prevent stack overflow in JSON operations)
#define ADE7953_METER_READING_TASK_PRIORITY 2 // The priority for the ADE7953 task

// Memory safety for JSON operations
#define JSON_SAFE_MIN_HEAP_SIZE (8 * 1024) // Minimum free heap required for JSON operations (8KB)
#define JSON_LOW_MEMORY_BATCH_LIMIT 250 // Reduced batch size when memory is low

// Interrupt handling
#define ADE7953_INTERRUPT_TIMEOUT_MS 1000 // Timeout for waiting on interrupt semaphore (in ms)

// Macros
#define PAYLOAD_METER_LOCK() do { if (_payloadMeterMutex != NULL) xSemaphoreTake(_payloadMeterMutex, pdMS_TO_TICKS(5000)); } while(0)
#define PAYLOAD_METER_UNLOCK() do { if (_payloadMeterMutex != NULL) xSemaphoreGive(_payloadMeterMutex); } while(0)

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

// Modbus TCP
// --------------------
#define MODBUS_TCP_PORT 502 // The default port for Modbus TCP
#define MODBUS_TCP_MAX_CLIENTS 3 // The maximum number of clients that can connect to the Modbus TCP server
#define MODBUS_TCP_TIMEOUT (10 * 1000) // The timeout for the Modbus TCP server
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
#define MQTT_TOPIC_LOG "log"
#define MQTT_TOPIC_GENERAL_CONFIGURATION "general-configuration"
#define MQTT_TOPIC_CONNECTIVITY "connectivity"
#define MQTT_TOPIC_STATISTICS "statistics"
#define MQTT_TOPIC_PROVISIONING_REQUEST "provisioning/request"

// Subscribe topics
#define MQTT_TOPIC_SUBSCRIBE_UPDATE_FIRMWARE "firmware-update"
#define MQTT_TOPIC_SUBSCRIBE_RESTART "restart"
#define MQTT_TOPIC_SUBSCRIBE_PROVISIONING_RESPONSE "provisioning/response"
#define MQTT_TOPIC_SUBSCRIBE_ERASE_CERTIFICATES "erase-certificates"
#define MQTT_TOPIC_SUBSCRIBE_SET_GENERAL_CONFIGURATION "set-general-configuration"
#define MQTT_TOPIC_SUBSCRIBE_ENABLE_DEBUG_LOGGING "enable-debug-logging" 
#define MQTT_TOPIC_SUBSCRIBE_QOS 1

// MQTT will
#define MQTT_WILL_QOS 1
#define MQTT_WILL_RETAIN true
#define MQTT_WILL_MESSAGE "{\"connectivity\":\"unexpected_offline\"}"

// AWS IoT Core endpoint
#define AWS_IOT_CORE_PORT 8883