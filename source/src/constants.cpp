#include "constants.h"

// General
// --------------------

// Firmware info
const char* FIRMWARE_VERSION = "0.3.21";
const char* FIRMWARE_DATE = "2024-05-06";

// File path
const char* METADATA_JSON_PATH = "/metadata.json";
const char* GENERAL_CONFIGURATION_JSON_PATH = "/config/general.json";
const char* CONFIGURATION_ADE7953_JSON_PATH = "/config/ade7953.json";
const char* CALIBRATION_JSON_PATH = "/config/calibration.json";
const char* CHANNEL_DATA_JSON_PATH = "/config/channel.json";
const char* LOGGER_JSON_PATH = "/config/logger.json";
const char* ENERGY_JSON_PATH = "/energy.json";
const char* FACTORY_PATH = "/fct";

// Serial
const int SERIAL_BAUDRATE = 921600; // Fastest baudrate for ESP32

// Logger
// 0:VERBOSE, 1: DEBUG, 2: INFO, 3: WARNING, 4: ERROR, 5: FATAL
const char* LOG_TXT_PATH = "/log.txt";
const char* LOG_COPY_TXT_PATH = "/log_copy.txt";
const int LOG_FILE_MAX_LENGTH = 1000; // In lines
const int DEFAULT_LOG_PRINT_LEVEL = CUSTOM_LOG_LEVEL_DEBUG;
const int DEFAULT_LOG_SAVE_LEVEL = CUSTOM_LOG_LEVEL_INFO;

// Time
const char* NTP_SERVER = "pool.ntp.org";
const int TIME_SYNC_INTERVAL = 3600; // 1 hour
const int GMT_OFFSET = 3600; // 1 hour
const int DAYLIGHT_OFFSET = 0;

// LED
const int LED_RED_PIN = 38;
const int LED_GREEN_PIN = 39;
const int LED_BLUE_PIN = 37;
const int LED_DEFAULT_BRIGHTNESS = 70;
const int LED_MAX_BRIGHTNESS = 255;
const int LED_FREQUENCY = 5000;
const int LED_RESOLUTION = 8;

// WiFi
const int WIFI_CONFIG_PORTAL_TIMEOUT = 180; // 3 minutes
const char* WIFI_CONFIG_PORTAL_SSID = "EnergyMe";

// MDNS
const char* MDNS_HOSTNAME = "energyme";

// Cloud services
const bool DEFAULT_IS_CLOUD_SERVICES_ENABLED = false;
const int MAX_INTERVAL_PAYLOAD = 60000; // 60 seconds

// MQTT
const int MQTT_MAX_CONNECTION_ATTEMPT = 5; // The maximum number of attempts to connect to the MQTT broker
const int MQTT_OVERRIDE_KEEPALIVE = 600; // The default value is 15 seconds, which is too low for the AWS IoT MQTT broker
const int MQTT_STATUS_PUBLISH_INTERVAL = 3600; // In seconds
const int MQTT_MIN_CONNECTION_INTERVAL = 60000; // In milliseconds, representing the minimum interval between two connection attempts

// Conversion factors
const float BYTE_TO_KILOBYTE = 1 / 1024.0;
const float MILLIS_TO_HOURS = 1 / 3600000.0;

// Saving data
const int ENERGY_SAVE_INTERVAL = 3600; // Time between each energy save (in seconds) to the SPIFFS

// ESP32 status
const int MINIMUM_FREE_HEAP_SIZE = 10000; // Below this value (in bytes), the ESP32 will restart
const int MINIMUM_FREE_SPIFFS_SIZE = 1000000; // Below this value (in bytes), the ESP32 will clear the logs

// Multiplexer
// --------------------
const int MULTIPLEXER_S0_PIN = 36; 
const int MULTIPLEXER_S1_PIN = 35;
const int MULTIPLEXER_S2_PIN = 45;
const int MULTIPLEXER_S3_PIN = 48;

// ADE7953
// --------------------

// Hardware pins
const int SS_PIN = 11;
const int SCK_PIN = 14;
const int MISO_PIN = 13;
const int MOSI_PIN = 12;
const int ADE7953_RESET_PIN = 9;

// Default values for ADE7953 registers
const int DEFAULT_EXPECTED_AP_NOLOAD_REGISTER = 0x00E419; // Default expected value for AP_NOLOAD_32 
const int DEFAULT_X_NOLOAD_REGISTER = 0x00C832; // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 20000:1, meaning that the no-load threshold is 0.005% of the full-scale value
const int DEFAULT_DISNOLOAD_REGISTER = 0x00; // 0x00 = 0b00000000 (disable all no-load detection)
const int DEFAULT_LCYCMODE_REGISTER = 0xFF; // 0xFF = 0b11111111 (enable accumulation mode for all channels)
const int DEFAULT_LINECYC_REGISTER = 50; // Set the number of half line cycles to accumulate before interrupting
const int DEFAULT_PGA_REGISTER = 0x000; // PGA gain = 1
const int DEFAULT_CONFIG_REGISTER = 0b1000000000000100; // Enable bit 2, and 15 (keep HPF enabled, keep COMM_LOCK disabled)

// Default calibration values
const int DEFAULT_AWGAIN = 0x400000; // Default AWGAIN value
const int DEFAULT_AWATTOS = 0x00; // Default AWATTOS value
const int DEFAULT_AVARGAIN = 0x400000; // Default AVARGAIN value
const int DEFAULT_AVAROS = 0x00; // Default AVAROS value
const int DEFAULT_AVAGAIN = 0x400000; // Default AVAGAIN value
const int DEFAULT_AVAOS = 0x00; // Default AVAOS value
const int DEFAULT_AIGAIN = 0x400000; // Default AIGAIN value
const int DEFAULT_AIRMSOS = 0xFD12; // Default AIRMSOS value
const int DEFAULT_BIGAIN = 0x41BCA1; // Default BIGAIN value - Modified to match channel A
const int DEFAULT_BIRMSOS = 0xFD12; // Default BIRMSOS value
const int DEFAULT_PHCALA = 0x00; // Default PHCALA value
const int DEFAULT_PHCALB = 0x00; // Default PHCALB value

// Fixed conversion values
const float POWER_FACTOR_CONVERSION_FACTOR = 1.0 / 32768.0; // PF/LSB

// Sample time
const int SAMPLE_TIME = 100; // 100 cycles = 1 second for 50Hz