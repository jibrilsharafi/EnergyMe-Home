#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer
#include <CircularBuffer.hpp>

// Project includes
#include "ade7953.h"
#include "constants.h"
#include "crashmonitor.h"
#include "customwifi.h" // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "customserver.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "custommqtt.h"
#include "multiplexer.h"
#include "structs.h"
#include "utils.h"

// Global variables
// --------------------

RestartConfiguration restartConfiguration;
PublishMqtt publishMqtt;

MainFlags mainFlags;

GeneralConfiguration generalConfiguration;
CustomMqttConfiguration customMqttConfiguration;
RTC_NOINIT_ATTR CrashData crashData;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

WiFiClient customNet;
PubSubClient customClientMqtt(customNet);

CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;

AsyncWebServer server(WEBSERVER_PORT);

// Callback variables
CircularBuffer<LogJson, LOG_BUFFER_SIZE> logBuffer;
char jsonBuffer[LOG_JSON_BUFFER_SIZE];  // Pre-allocated buffer
String deviceId;      // Pre-allocated buffer

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

Led led(
  LED_RED_PIN, 
  LED_GREEN_PIN, 
  LED_BLUE_PIN, 
  DEFAULT_LED_BRIGHTNESS
);

Multiplexer multiplexer(
  MULTIPLEXER_S0_PIN,
  MULTIPLEXER_S1_PIN,
  MULTIPLEXER_S2_PIN,
  MULTIPLEXER_S3_PIN
);

CustomWifi customWifi(
  logger,
  led
);

CustomTime customTime(
  NTP_SERVER,
  TIME_SYNC_INTERVAL,
  TIMESTAMP_FORMAT,
  generalConfiguration,
  logger
);

Ade7953 ade7953(
  SS_PIN,
  SCK_PIN,
  MISO_PIN,
  MOSI_PIN,
  ADE7953_RESET_PIN,
  logger,
  customTime,
  mainFlags
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
  mainFlags
);

Mqtt mqtt(
  ade7953,
  logger,
  customTime,
  clientMqtt,
  net,
  publishMqtt,
  payloadMeter
);

CustomServer customServer(
  server,
  logger,
  led,
  ade7953,
  customTime,
  customWifi,
  customMqtt
);

// Main functions
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
    if (strcmp(level, "debug") == 0 || strcmp(level, "verbose") == 0) return;

    if (deviceId == "") {
        deviceId = WiFi.macAddress();
        deviceId.replace(":", "");
    }

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

    if (WiFi.status() != WL_CONNECTED) return;
    if (!clientMqtt.connected()) return; 

    unsigned int _loops = 0;
    while (!logBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;

        LogJson _log = logBuffer.shift();
        size_t totalLength = strlen(_log.timestamp) + strlen(_log.function) + strlen(_log.message) + 100;
        if (totalLength > sizeof(jsonBuffer)) {
            Serial.println("Log message too long for buffer");
            continue;
        }

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

        char topic[LOG_TOPIC_SIZE];
        snprintf(topic, sizeof(topic), "energyme/home/%s/log/%s", deviceId.c_str(), _log.level);

        if (!clientMqtt.publish(topic, jsonBuffer)) {
            Serial.printf("MQTT publish failed to %s. Error: %d\n", 
                topic, clientMqtt.state());
            logBuffer.push(_log);
            break;
        }
    }
}

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    Serial.printf("EnergyMe - Home\n____________________\n\n");
    Serial.println("Booting...");
    Serial.printf("Build version: %s\n", FIRMWARE_BUILD_VERSION);
    Serial.printf("Build date: %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    
    Serial.println("Setting up LED...");
    led.begin();
    Serial.println("LED setup done");
    led.setYellow(); // Indicate we're in early boot/crash check

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
        ESP.restart();
        return;
    }
    led.setWhite();
    
    logger.begin();
    logger.setCallback(callbackLogToMqtt);

    logger.info("Booting...", "main::setup");  
    logger.info("EnergyMe - Home | Build version: %s | Build date: %s %s", "main::setup", FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);

    
    logger.info("Setting up crash monitor...", "main::setup");
    crashMonitor.begin();
    logger.info("Crash monitor setup done", "main::setup");

    led.setCyan();

    TRACE
    logger.info("Checking for missing files...", "main::setup");
    auto missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        led.setOrange();
        logger.warning("Missing files detected. Creating default files for missing files...", "main::setup");
        
        TRACE
        createDefaultFilesForMissingFiles(missingFiles);

        logger.info("Default files created for missing files", "main::setup");
    } else {
        logger.info("No missing files detected", "main::setup");
    }

    TRACE
    logger.info("Fetching general configuration from SPIFFS...", "main::setup");
    if (!setGeneralConfigurationFromSpiffs()) {
        logger.warning("Failed to load configuration from SPIFFS. Using default values.", "main::setup");
    } else {
        logger.info("Configuration loaded from SPIFFS", "main::setup");
    }

    led.setPurple();
    
    TRACE
    logger.info("Setting up multiplexer...", "main::setup");
    multiplexer.begin();
    logger.info("Multiplexer setup done", "main::setup");
    
    TRACE
    logger.info("Setting up ADE7953...", "main::setup");
    if (!ade7953.begin()) {
        logger.fatal("ADE7953 initialization failed!", "main::setup");
    } else {
        logger.info("ADE7953 setup done", "main::setup");
    }

    led.setBlue();

    TRACE
    logger.info("Setting up WiFi...", "main::setup");
    customWifi.begin();
    logger.info("WiFi setup done", "main::setup");

    TRACE
    logger.info("Syncing time...", "main::setup");
    updateTimezone();
    if (!customTime.begin()) {
        logger.error("Time sync failed!", "main::setup");
    } else {
        logger.info("Time synced", "main::setup");
    }
    
    TRACE
    logger.info("Setting up server...", "main::setup");
    customServer.begin();
    logger.info("Server setup done", "main::setup");

    TRACE
    logger.info("Setting up Modbus TCP...", "main::setup");
    modbusTcp.begin();
    logger.info("Modbus TCP setup done", "main::setup");

    led.setGreen();

    TRACE
    logger.info("Setup done", "main::setup");
}

void loop() {
    TRACE
    if (mainFlags.blockLoop) return;

    TRACE
    crashMonitor.crashCounterLoop();
    TRACE
    crashMonitor.firmwareTestingLoop();
    
    TRACE
    customWifi.loop();
    
    TRACE
    mqtt.loop();
    
    TRACE
    customMqtt.loop();
    
    TRACE
    ade7953.loop();

    TRACE
    if (ade7953.isLinecycFinished()) {
    
        led.setGreen();

        // Since there is a settling time after the multiplexer is switched, 
        // we let one cycle pass before we start reading the values
        if (mainFlags.isfirstLinecyc) {
            mainFlags.isfirstLinecyc = false;
        } else {
            mainFlags.isfirstLinecyc = true;

            if (mainFlags.currentChannel != -1) { // -1 indicates that no channel is active
              TRACE
              ade7953.readMeterValues(mainFlags.currentChannel);

              multiplexer.setChannel(max(mainFlags.currentChannel-1, 0));

              TRACE
              payloadMeter.push(
              PayloadMeter(
                  mainFlags.currentChannel,
                  customTime.getUnixTimeMilliseconds(),
                  ade7953.meterValues[mainFlags.currentChannel].activePower,
                  ade7953.meterValues[mainFlags.currentChannel].powerFactor
                  )
              );
              
              printMeterValues(ade7953.meterValues[mainFlags.currentChannel], ade7953.channelData[mainFlags.currentChannel].label.c_str());
            }
        
            mainFlags.currentChannel = ade7953.findNextActiveChannel(mainFlags.currentChannel);
        }

        // We always read the first channel as it is in a separate channel and is not impacted by the switching of the multiplexer
        TRACE
        ade7953.readMeterValues(0);
        payloadMeter.push(
            PayloadMeter(
                0,
                customTime.getUnixTimeMilliseconds(),
                ade7953.meterValues[0].activePower,
                ade7953.meterValues[0].powerFactor
            )
            );
        printMeterValues(ade7953.meterValues[0], ade7953.channelData[0].label.c_str());
    }

    TRACE
    if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        printDeviceStatus();
        setRestartEsp32("main::loop", "Heap memory has degraded below safe minimum");
    }

    // If memory is below a certain level, clear the log
    TRACE
    if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        printDeviceStatus();
        logger.clearLog();
        logger.warning("Log cleared due to low memory", "main::loop");
    }

    TRACE
    checkIfRestartEsp32Required();
    
    TRACE
    led.setOff();
}