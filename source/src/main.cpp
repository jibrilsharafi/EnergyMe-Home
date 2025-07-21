#include <Arduino.h>
#include <SPIFFS.h>
#include <CircularBuffer.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <WiFiUdp.h>

// Project includes
// Initialization before everything
#include "constants.h"
#include "structs.h"
#include "utils.h"
#include "pins.h"

#include "ade7953.h"
#include "buttonhandler.h"
#include "crashmonitor.h"
#include "customwifi.h" // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "customserver.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "influxdbclient.h"
#include "multiplexer.h"

// Global variables
// --------------------

static const char *TAG = "main";

RestartConfiguration restartConfiguration;

MainFlags mainFlags;
Statistics statistics;

RTC_NOINIT_ATTR DebugFlagsRtc debugFlagsRtc;

// AsyncWebServer server(WEBSERVER_PORT);

// Global device ID - defined here, declared as extern in constants.h
char DEVICE_ID[DEVICE_ID_BUFFER_SIZE];

// Callback variables

// MQTT logging variables
CircularBuffer<LogJson, LOG_BUFFER_SIZE> logBuffer;
char jsonBuffer[LOG_JSON_BUFFER_SIZE];  // Pre-allocated buffer
char baseMqttTopicLogs[LOG_TOPIC_SIZE];
char fullMqttTopic[LOG_TOPIC_SIZE];

// UDP logging variables
CircularBuffer<LogJson, LOG_BUFFER_SIZE> udpLogBuffer;
WiFiUDP udpClient;
IPAddress udpDestinationIp; // Will be set from configuration
char udpBuffer[UDP_LOG_BUFFER_SIZE];  // Separate buffer for UDP

// Utils variables
unsigned long long _linecycUnix = 0;   // Used to track the Unix time when the linecycle ended (for MQTT payloads)
unsigned long lastMaintenanceCheck = 0;

// Classes instances
// --------------------

AdvancedLogger logger(
  LOG_PATH,
  LOG_CONFIG_PATH,
  LOG_TIMESTAMP_FORMAT
);

Ade7953 ade7953(
  ADE7953_SS_PIN,
  ADE7953_SCK_PIN,
  ADE7953_MISO_PIN,
  ADE7953_MOSI_PIN,
  ADE7953_RESET_PIN,
  ADE7953_INTERRUPT_PIN,
  logger,
  mainFlags
);

// CustomServer customServer(
//   server,
//   logger,
//   led,
//   ade7953,
//   customMqtt,
//   influxDbClient,
//   buttonHandler
// );

// Main functions
// Tasks
// --------------------

// --------------------
// UDP Logging Configuration

void setupUdpLogging() {
    // Initialize UDP destination IP from configuration
    udpDestinationIp.fromString(DEFAULT_UDP_LOG_DESTINATION_IP);
    
    udpClient.begin(UDP_LOG_PORT);
    logger.info("UDP logging configured - destination: %s:%d", TAG, 
                udpDestinationIp.toString().c_str(), UDP_LOG_PORT);
}

// --------------------
// Callback 

void callbackLogToMqtt(
    const char* timestamp,
    unsigned long millisEsp,
    const char* level,
    unsigned int coreId,
    const char* function,
    const char* message
) {
    // Use the new queue-based approach
    Mqtt::pushLog(timestamp, millisEsp, level, coreId, function, message);
}

void callbackLogToUdp(
    const char* timestamp,
    unsigned long millisEsp,
    const char* level,
    unsigned int coreId,
    const char* function,
    const char* message
) {
    if (!DEFAULT_IS_UDP_LOGGING_ENABLED) return;
    if (strcmp(level, "verbose") == 0) return; // Never send verbose logs via UDP
    
    // Skip UDP logging if heap is critically low to prevent death spiral
    if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) return;

    udpLogBuffer.push(
        LogJson(
            timestamp,
            millisEsp,
            level,
            coreId,
            function,
            message
        )
    );

    // If not connected to WiFi or no valid IP, return (log is still stored in circular buffer for later)
    if (CustomWifi::isFullyConnected() == false) return;

    unsigned int _loops = 0;
    while (!udpLogBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;

        LogJson _log = udpLogBuffer.shift();

        // Format as simplified syslog message
        snprintf(udpBuffer, sizeof(udpBuffer),
            "<%d>%s %s[%lu]: [%s][Core%u] %s: %s", // TODO: Make this constants or give meaningful names
            16, // Facility.Severity (local0.info)
            _log.timestamp,
            DEVICE_ID,
            _log.millisEsp,
            _log.level,
            _log.coreId,
            _log.function,
            _log.message);
        
        if (!udpClient.beginPacket(udpDestinationIp, UDP_LOG_PORT)) {
            udpLogBuffer.push(_log);
            break;
        }
        
        size_t bytesWritten = udpClient.write((const uint8_t*)udpBuffer, strlen(udpBuffer));
        if (bytesWritten == 0) {
            udpLogBuffer.push(_log);
            udpClient.endPacket(); // Clean up the packet
            break;
        }
        
        if (!udpClient.endPacket()) {
            udpLogBuffer.push(_log);
            break;
        }
        
        // Small delay between UDP packets to avoid overwhelming the stack
        if (!udpLogBuffer.isEmpty()) {
            delayMicroseconds(100); // 0.1ms delay
        }
    }
}

void callbackLogMultiple(
    const char* timestamp,
    unsigned long millisEsp,
    const char* level,
    unsigned int coreId,
    const char* function,
    const char* message
) {
  callbackLogToUdp(timestamp, millisEsp, level, coreId, function, message);
  callbackLogToMqtt(timestamp, millisEsp, level, coreId, function, message);
}

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    Serial.printf("EnergyMe - Home\n____________________\n\n");
    Serial.println("Booting...");
    Serial.printf("Build version: %s\n", FIRMWARE_BUILD_VERSION);
    Serial.printf("Build date: %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    
    // Initialize global device ID
    getDeviceId(DEVICE_ID, sizeof(DEVICE_ID));
    Serial.printf("Device ID: %s\n", DEVICE_ID);

    Serial.println("Setting up LED...");
    Led::begin(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);
    Serial.println("LED setup done");
    Led::setYellow(); // Indicate we're in early boot/crash check

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
        ESP.restart();
        return;
    }
    Led::setWhite();
    
    logger.begin();
    logger.setCallback(callbackLogMultiple);

    logger.info("Guess who's back, back again! EnergyMe - Home is starting up...", TAG);
    logger.info("Build version: %s | Build date: %s %s | Device ID: %s", TAG, FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME, DEVICE_ID);

    logger.debug("Setting up crash monitor...", TAG);
    CrashMonitor::begin();
    logger.info("Crash monitor setup done", TAG);

    if (CrashMonitor::hasCoreDump()) {
        size_t dumpSize = CrashMonitor::getCoreDumpSize();
        logger.warning("Core dump available: %d bytes", "main", dumpSize);
        
        // Clear it after processing
        CrashMonitor::clearCoreDump();
    }

    logger.debug(
        "Checking RTC debug flags. Signature: 0x%X, Enabled: %d, Duration: %lu, EndTimeMillis: %lu", 
        TAG, 
        debugFlagsRtc.signature, 
        debugFlagsRtc.enableMqttDebugLogging, 
        debugFlagsRtc.mqttDebugLoggingDurationMillis, 
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis
    );
    if (debugFlagsRtc.signature == DEBUG_FLAGS_RTC_SIGNATURE && debugFlagsRtc.enableMqttDebugLogging) {
        logger.info("Resuming MQTT debug logging from RTC for %lu ms.", TAG, debugFlagsRtc.mqttDebugLoggingDurationMillis);
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = millis() + debugFlagsRtc.mqttDebugLoggingDurationMillis;
    } else {
        logger.debug("No valid RTC debug flags found. Resetting to default values.", TAG);
        debugFlagsRtc.enableMqttDebugLogging = false;
        debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
        debugFlagsRtc.signature = 0;
    }

    Led::setCyan();

    logger.debug("Checking for missing files...", TAG);
    auto missingFiles = checkMissingFiles();
    if (missingFiles.empty()) {
      logger.info("No missing files detected", TAG);
    } else {
      Led::setOrange();
      logger.info("Missing files detected (first setup? Welcome to EnergyMe - Home!!!). Creating default files for missing files...", TAG);      
      createDefaultFilesForMissingFiles(missingFiles);
  
      logger.info("Default files created for missing files", TAG);
    }
    
    Led::setPurple();
    
    logger.debug("Setting up multiplexer...", TAG);
    Multiplexer::begin(
      MULTIPLEXER_S0_PIN,
      MULTIPLEXER_S1_PIN,
      MULTIPLEXER_S2_PIN,
      MULTIPLEXER_S3_PIN
    );
    logger.info("Multiplexer setup done", TAG);
    
    logger.debug("Setting up button handler...", TAG);
    ButtonHandler::begin(BUTTON_GPIO0_PIN);
    logger.info("Button handler setup done", TAG);
    
    logger.debug("Setting up ADE7953...", TAG);
    if (ade7953.begin()) {
      logger.info("ADE7953 setup done", TAG);
    } else {
      logger.fatal("ADE7953 initialization failed! This is a big issue mate..", TAG);
    }

    Led::setBlue();  
    logger.debug("Setting up WiFi...", TAG);
    CustomWifi::begin();
    logger.info("WiFi setup done", TAG);

    while (!CustomWifi::isFullyConnected()) { // TODO: make better?
        logger.info("Waiting for full WiFi connection...", TAG);
        delay(1000);
    }

    // Add UDP logging setup after WiFi
    logger.debug("Setting up UDP logging...", TAG);
    setupUdpLogging();
    logger.info("UDP logging setup done", TAG);
    
    logger.debug("Syncing time...", TAG);
    if (CustomTime::begin()) {
      int gmtOffset, dstOffset;
      getPublicTimezone(&gmtOffset, &dstOffset);
      CustomTime::setOffset(
        gmtOffset, 
        dstOffset
      );
      
      char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
      CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
      logger.info("Initial time sync successful. Current timestamp: %s", TAG, timestampBuffer);
    } else {
      logger.error("Initial time sync failed! Will retry later.", TAG);
    }
    
    
    logger.debug("Setting up server...", TAG);
    // customServer.begin();
    server_begin();
    logger.info("Server setup done", TAG);
    
    logger.debug("Setting up Modbus TCP...", TAG);
    ModbusTcp::begin();
    logger.info("Modbus TCP setup done", TAG);

    logger.debug("Setting up MQTT client...", TAG);
    Mqtt::begin();
    logger.info("MQTT client setup done", TAG);

    logger.debug("Setting up InfluxDB client...", TAG);
    InfluxDbClient::begin();
    logger.info("InfluxDB client setup done", TAG);

    Led::setGreen();
    logger.info("Setup done! Let's get this energetic party started!", TAG);
}

void loop() {
    if (mainFlags.blockLoop) return;
 
    CrashMonitor::crashCounterLoop();
    CrashMonitor::firmwareTestingLoop();

    ade7953.loop();
    
    if (millis() - lastMaintenanceCheck >= MAINTENANCE_CHECK_INTERVAL) {      
      lastMaintenanceCheck = millis();      
      updateStatistics();
      printStatistics();
      printDeviceStatus();

      if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
        setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
      }

      // If memory is below a certain level, clear the log 
      if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        logger.clearLog();
        logger.warning("Log cleared due to low memory", TAG);
      }
      
      if (debugFlagsRtc.enableMqttDebugLogging && millis() >= debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
        logger.info("MQTT debug logging period ended.", TAG);

        debugFlagsRtc.enableMqttDebugLogging = false;
        debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
        debugFlagsRtc.signature = 0; 
      }
    }

    // TODO: add a periodic check doing a GET request on the is-alive API to ensure HTTP is working properly

    
    checkIfRestartEsp32Required();
}