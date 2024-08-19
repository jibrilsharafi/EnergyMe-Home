#include "constants.h"

// General
// --------------------

// File path
const char* FIRST_SETUP_PATH = "/first-setup.json";
const char* GENERAL_CONFIGURATION_JSON_PATH = "/config/general.json";
const char* CONFIGURATION_ADE7953_JSON_PATH = "/config/ade7953.json";
const char* CALIBRATION_JSON_PATH = "/config/calibration.json";
const char* CHANNEL_DATA_JSON_PATH = "/config/channel.json";
const char* ENERGY_JSON_PATH = "/energy.json";
const char* DAILY_ENERGY_JSON_PATH = "/daily-energy.json";
const char* FIRMWARE_UPDATE_INFO_PATH = "/fw-update-info.json";
const char* FIRMWARE_UPDATE_STATUS_PATH = "/fw-update-status.json";

// Serial
const int SERIAL_BAUDRATE = 115200; // Most common baudrate for ESP32

// Logger
const char* LOG_PATH = "/AdvancedLogger/log.txt";
const char* LOG_CONFIG_PATH = "/AdvancedLogger/config.txt";
const char* LOG_TIMESTAMP_FORMAT = "%Y-%m-%d %H:%M:%S";
const int LOG_FILE_MAX_LENGTH = 1000; // In lines

// Time
const char* NTP_SERVER = "pool.ntp.org";
const int TIME_SYNC_INTERVAL = 3600; // Sync time every hour
const int DEFAULT_GMT_OFFSET = 0;
const int DEFAULT_DST_OFFSET = 0;

// LED
const int LED_RED_PIN = 38;
const int LED_GREEN_PIN = 39;
const int LED_BLUE_PIN = 37;
const int DEFAULT_LED_BRIGHTNESS = 191; // 75% of the maximum brightness
const int LED_MAX_BRIGHTNESS = 255;
const int LED_FREQUENCY = 5000;
const int LED_RESOLUTION = 8;

// WiFi
const int WIFI_CONFIG_PORTAL_TIMEOUT = 300; // 5 minutes
const char* WIFI_CONFIG_PORTAL_SSID = "EnergyMe";
const int WIFI_LOOP_INTERVAL = 15000;

// MDNS
const char* MDNS_HOSTNAME = "energyme";

// Cloud services
const bool DEFAULT_IS_CLOUD_SERVICES_ENABLED = false;
const int MAX_INTERVAL_PAYLOAD = 30000; // 30 seconds - The maximum interval between two meter payloads

// MQTT
const int MQTT_MAX_CONNECTION_ATTEMPT = 5; // The maximum number of attempts to connect to the MQTT broker
const int MQTT_OVERRIDE_KEEPALIVE = 600; // The default value is 15 seconds, which is too low for the AWS IoT MQTT broker
const int MQTT_STATUS_PUBLISH_INTERVAL = 3600; // Time between each status publish (in seconds)
const int MQTT_MIN_CONNECTION_INTERVAL = 10000; // In milliseconds, representing the minimum interval between two connection attempts
const int MQTT_LOOP_INTERVAL = 1000;

// Conversion factors
const float BYTE_TO_KILOBYTE = 1 / 1024.0;
const float MILLIS_TO_HOURS = 1 / 3600000.0;

// Saving date
const int ENERGY_SAVE_INTERVAL = 900; // Time between each energy save (in seconds) to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

// ESP32 status
const int MINIMUM_FREE_HEAP_SIZE = 10000; // Below this value (in bytes), the ESP32 will restart
const int MINIMUM_FREE_SPIFFS_SIZE = 100000; // Below this value (in bytes), the ESP32 will clear the logs

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
const long DEFAULT_EXPECTED_AP_NOLOAD_REGISTER = 0x00E419; // Default expected value for AP_NOLOAD_32 
const long DEFAULT_X_NOLOAD_REGISTER = 0x00C832; // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 20000:1, meaning that the no-load threshold is 0.005% of the full-scale value
const long DEFAULT_DISNOLOAD_REGISTER = 0; // 0x00 = 0b00000000 (disable all no-load detection)
const long DEFAULT_LCYCMODE_REGISTER = 0xFF; // 0xFF = 0b11111111 (enable accumulation mode for all channels)
const long DEFAULT_LINECYC_REGISTER = 50; // Set the number of half line cycles to accumulate before interrupting
const long DEFAULT_PGA_REGISTER = 0; // PGA gain = 1
const long DEFAULT_CONFIG_REGISTER = 0b1000000000000100; // Enable bit 2, and 15 (keep HPF enabled, keep COMM_LOCK disabled)
const long DEFAULT_AWGAIN = 4194304; // Default AWGAIN value
const long DEFAULT_AWATTOS = 0; // Default AWATTOS value
const long DEFAULT_AVARGAIN = 4194304; // Default AVARGAIN value
const long DEFAULT_AVAROS = 0; // Default AVAROS value
const long DEFAULT_AVAGAIN = 4194304; // Default AVAGAIN value
const long DEFAULT_AVAOS = 0; // Default AVAOS value
const long DEFAULT_AIGAIN = 4194304; // Default AIGAIN value
const long DEFAULT_AIRMSOS = -400; // Default AIRMSOS value
const long DEFAULT_BIGAIN = 4295713; // Default BIGAIN value - Modified to match channel A
const long DEFAULT_BIRMSOS = -750; // Default BIRMSOS value
const long DEFAULT_PHCALA = 200; // Default PHCALA value
const long DEFAULT_PHCALB = 200; // Default PHCALB value

// Fixed conversion values
const float POWER_FACTOR_CONVERSION_FACTOR = 1.0 / 32768.0; // PF/LSB

// Sample time
const int DEFAULT_SAMPLE_CYCLES = 100; // 100 cycles = 1 second for 50Hz

// Validate values
const float VALIDATE_VOLTAGE_MIN = 150.0;
const float VALIDATE_VOLTAGE_MAX = 300.0;
const float VALIDATE_CURRENT_MIN = -100.0;
const float VALIDATE_CURRENT_MAX = 100.0;
const float VALIDATE_POWER_MIN = -100000.0;
const float VALIDATE_POWER_MAX = 100000.0;
const float VALIDATE_POWER_FACTOR_MIN = -1.0;
const float VALIDATE_POWER_FACTOR_MAX = 1.0;

// Modbus TCP
// --------------------
const int MODBUS_TCP_PORT = 502;
const int MODBUS_TCP_MAX_CLIENTS = 3;
const int MODBUS_TCP_TIMEOUT = 10000;
const int MODBUS_TCP_SERVER_ID = 1;