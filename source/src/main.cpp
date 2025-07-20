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

#include "ade7953.h"
#include "buttonhandler.h"
#include "crashmonitor.h"
#include "customwifi.h" // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "customserver.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "custommqtt.h"
#include "influxdbclient.h"
#include "multiplexer.h"

// Global variables
// --------------------

static const char *TAG = "main";

RestartConfiguration restartConfiguration;
PublishMqtt publishMqtt;

MainFlags mainFlags;
Statistics statistics;

GeneralConfiguration generalConfiguration;
CustomMqttConfiguration customMqttConfiguration;
InfluxDbConfiguration influxDbConfiguration;
RTC_NOINIT_ATTR CrashData crashData;
RTC_NOINIT_ATTR DebugFlagsRtc debugFlagsRtc;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

WiFiClient customNet;
PubSubClient customClientMqtt(customNet);

CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;  // TODO: freertos queue or/and ade7953 variable?

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
IPAddress broadcastIP;
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

CrashMonitor crashMonitor(
  logger
);

Multiplexer multiplexer(
  MULTIPLEXER_S0_PIN,
  MULTIPLEXER_S1_PIN,
  MULTIPLEXER_S2_PIN,
  MULTIPLEXER_S3_PIN
);

CustomWifi customWifi(
  logger
);

ButtonHandler buttonHandler(
  BUTTON_GPIO0_PIN,
  logger,
  customWifi
);

CustomTime customTime(
  NTP_SERVER,
  TIME_SYNC_INTERVAL,
  generalConfiguration,
  logger
);

Ade7953 ade7953(
  SS_PIN,
  SCK_PIN,
  MISO_PIN,
  MOSI_PIN,
  ADE7953_RESET_PIN,
  ADE7953_INTERRUPT_PIN,
  logger,
  mainFlags,
  customTime,
  multiplexer,
  payloadMeter
);

ModbusTcp modbusTcp(
  MODBUS_TCP_PORT, 
  MODBUS_TCP_SERVER_ID, 
  MODBUS_TCP_MAX_CLIENTS, 
  MODBUS_TCP_TIMEOUT,
  logger,
  ade7953,
  customTime
);

CustomMqtt customMqtt(
  ade7953,
  logger,
  customClientMqtt,
  customMqttConfiguration,
  customTime
);

InfluxDbClient influxDbClient(
  ade7953,
  logger,
  influxDbConfiguration,
  customTime
);

Mqtt mqtt( // TODO: Add semaphore for the payload meter (or better make it a queue in freertos)
  ade7953,
  logger,
  clientMqtt,
  net,
  publishMqtt,
  payloadMeter,
  restartConfiguration
);

// CustomServer customServer(
//   server,
//   logger,
//   led,
//   ade7953,
//   customWifi,
//   customMqtt,
//   influxDbClient,
//   buttonHandler
// );

// Main functions
// Tasks
// --------------------

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
    if (
      (strcmp(level, "debug") == 0 && !debugFlagsRtc.enableMqttDebugLogging) || // Only send debug logs if MQTT debug logging is enabled
      (strcmp(level, "verbose") == 0) // Never send verbose logs via MQTT
    ) return;

    logBuffer.push(
      LogJson(
        timestamp,
        millisEsp,
        level,
        coreId,
        function,
        message
      )
    );

    // If not connected to WiFi and MQTT, return (log is still stored in circular buffer for later) 
    if (WiFi.status() != WL_CONNECTED) return;
    if (clientMqtt.state() != MQTT_CONNECTED) return;

    // Only generate the base MQTT topic if it does not exist yet
    if (baseMqttTopicLogs[0] == '\0') {
      snprintf(baseMqttTopicLogs, sizeof(baseMqttTopicLogs), 
               "%s/%s/%s/%s", MQTT_TOPIC_1, MQTT_TOPIC_2, DEVICE_ID, MQTT_TOPIC_LOG);
      logger.debug("Base MQTT topic for logs: %s", TAG, baseMqttTopicLogs);
    }

    unsigned int _loops = 0;
    while (!logBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;

        LogJson _log = logBuffer.shift();

        snprintf(jsonBuffer, sizeof(jsonBuffer),
            "{\"timestamp\":\"%s\","
            "\"millis\":%lu,"
            "\"core\":%u,"
            "\"function\":\"%s\","
            "\"message\":\"%s\"}",
            _log.timestamp,
            _log.millisEsp,
            _log.coreId,
            _log.function,
            _log.message);

        snprintf(
          fullMqttTopic, 
          sizeof(fullMqttTopic), 
          "%s/%s", 
          baseMqttTopicLogs, 
          _log.level
        );

        if (!clientMqtt.publish(fullMqttTopic, jsonBuffer)) {
            Serial.printf("MQTT publish failed to %s. Error: %d\n", 
                fullMqttTopic, clientMqtt.state());
            logBuffer.push(_log);
            statistics.mqttMessagesPublishedError++;
            break;
        }
    }
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

    // If not connected to WiFi, return (log is still stored in circular buffer for later)
    if (WiFi.status() != WL_CONNECTED) return;

    unsigned int _loops = 0;
    while (!udpLogBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;

        LogJson _log = udpLogBuffer.shift();
        
        // Additional heap check before attempting to send
        if (ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE) {
            // Put log back and stop trying
            udpLogBuffer.push(_log);
            break;
        }
        
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
        
        if (!udpClient.beginPacket(broadcastIP, UDP_LOG_PORT)) {
            // Failed to begin packet, put log back in buffer and break
            udpLogBuffer.push(_log);
            break;
        }
        
        size_t bytesWritten = udpClient.write((const uint8_t*)udpBuffer, strlen(udpBuffer));
        if (bytesWritten == 0 || !udpClient.endPacket()) {
            // Failed to send, put log back in buffer and break
            udpLogBuffer.push(_log);
            break;
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
    crashMonitor.begin();
    logger.info("Crash monitor setup done", TAG);

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

    TRACE();
    logger.debug("Checking for missing files...", TAG);
    auto missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        Led::setOrange();
        logger.info("Missing files detected (first setup? Welcome to EnergyMe - Home!!!). Creating default files for missing files...", TAG);

        TRACE();
        createDefaultFilesForMissingFiles(missingFiles);

        logger.info("Default files created for missing files", TAG);
    } else {
        logger.info("No missing files detected", TAG);
    }

    TRACE();
    logger.debug("Fetching general configuration from SPIFFS...", TAG);
    if (!setGeneralConfigurationFromSpiffs()) {
        logger.warning("Failed to load configuration from SPIFFS. Using default values.", TAG);
    } else {
        logger.info("Configuration loaded from SPIFFS", TAG);
    }

    Led::setPurple();
      TRACE();
    logger.debug("Setting up multiplexer...", TAG);
    multiplexer.begin();
    logger.info("Multiplexer setup done", TAG);

    TRACE();
    logger.debug("Setting up ADE7953...", TAG);
    if (!ade7953.begin()) {
      logger.fatal("ADE7953 initialization failed! This is a big issue mate..", TAG);
    } else {
      logger.info("ADE7953 setup done", TAG);
    }

    Led::setBlue();
    
    TRACE();
    logger.debug("Setting up WiFi...", TAG);
    customWifi.begin();
    logger.info("WiFi setup done", TAG);

    // Add UDP logging setup after WiFi
    TRACE();
    logger.debug("Setting up UDP logging...", TAG);
    if (WiFi.status() == WL_CONNECTED) {
        broadcastIP = WiFi.localIP();
        broadcastIP[3] = 255; // Convert to broadcast address
        udpClient.begin(UDP_LOG_PORT);
        logger.info("UDP broadcast logging configured for %s:%d", TAG, broadcastIP.toString().c_str(), UDP_LOG_PORT);
    } else {
        logger.warning("UDP logging setup skipped - WiFi not connected", TAG);
    }

    TRACE();
    logger.debug("Setting up button handler...", TAG);
    buttonHandler.begin();
    logger.info("Button handler setup done", TAG);

    TRACE();
    logger.debug("Syncing time...", TAG);
    updateTimezone();
    if (!customTime.begin()) {
      logger.error("Initial time sync failed! Will retry later.", TAG);
    } else {
      logger.info("Time synced. We're on time pal!", TAG);
    }
    
    TRACE();
    logger.debug("Setting up server...", TAG);
    // customServer.begin();
    server_begin();
    logger.info("Server setup done", TAG);

    TRACE();
    logger.debug("Setting up Modbus TCP...", TAG);
    modbusTcp.begin();
    logger.info("Modbus TCP setup done", TAG);

    Led::setGreen();

    TRACE();
    logger.info("Setup done! Let's get this energetic party started!", TAG);
}

void loop() {
    TRACE();
    if (mainFlags.blockLoop) return;

    // Main loop now only handles non-critical operations
    // Meter reading is handled by dedicated task
    TRACE();
    customTime.loop();

    TRACE();
    crashMonitor.crashCounterLoop();

    TRACE();
    crashMonitor.firmwareTestingLoop();
      
    TRACE();
    customWifi.loop();
    
    TRACE();
    buttonHandler.loop();
    
    TRACE();
    mqtt.loop();
    
    TRACE();
    customMqtt.loop();
    
    TRACE();
    influxDbClient.loop();

    TRACE();
    ade7953.loop();

    TRACE();
    if (millis() - lastMaintenanceCheck >= MAINTENANCE_CHECK_INTERVAL) {      
      lastMaintenanceCheck = millis();      
      
      TRACE();
      updateStatistics();

      TRACE();
      printStatistics();
      
      TRACE();
      printDeviceStatus();

      TRACE();
      if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
        setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
      }

      // If memory is below a certain level, clear the log
      TRACE();
      if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        logger.clearLog();
        logger.warning("Log cleared due to low memory", TAG);
      }
      
      TRACE();
      if (debugFlagsRtc.enableMqttDebugLogging && millis() >= debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
        logger.info("MQTT debug logging period ended.", TAG);

        debugFlagsRtc.enableMqttDebugLogging = false;
        debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
        debugFlagsRtc.signature = 0; 
      }
    }

    // TODO: add a periodic check doing a GET request on the is-alive API to ensure HTTP is working properly

    TRACE();
    checkIfRestartEsp32Required();
}