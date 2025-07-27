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
#include "customlog.h"

// Global variables
// --------------------

static const char *TAG = "main";

RestartConfiguration restartConfiguration;
Statistics statistics;

// Global device ID - defined here, declared as extern in constants.h
char DEVICE_ID[DEVICE_ID_BUFFER_SIZE];

// Utils variables
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
  logger
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
    logger.setCallback(CustomLog::callbackMultiple);

    logger.info("Guess who's back, back again! EnergyMe - Home is starting up...", TAG);
    logger.info("Build version: %s | Build date: %s %s | Device ID: %s", TAG, FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME, DEVICE_ID);

    logger.debug("Setting up crash monitor...", TAG);
    CrashMonitor::begin();
    logger.info("Crash monitor setup done", TAG);

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

    while (!CustomWifi::isFullyConnected()) {
        logger.debug("Waiting for full WiFi connection...", TAG);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Add UDP logging setup after WiFi
    logger.debug("Setting up UDP logging...", TAG);
    CustomLog::setupUdp();
    logger.info("UDP logging setup done", TAG);
    
    logger.debug("Syncing time...", TAG);
    if (CustomTime::begin()) {
      int gmtOffset, dstOffset;
      if (getPublicTimezone(&gmtOffset, &dstOffset)) {
        CustomTime::setOffset(
          gmtOffset, 
          dstOffset
        );

        char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
        logger.info("Initial time sync successful. Current timestamp: %s", TAG, timestampBuffer);
      } else {
        logger.warning("Time synced, but the timezone could not be fetched. It will be shown as UTC", TAG);
      }
    } else {
      logger.error("Initial time sync failed! Will retry later.", TAG);
    }
    
    logger.debug("Setting up server...", TAG);
    CustomServer::begin();
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
    printDeviceStatusStatic();
    
    logger.info("Setup done! Let's get this energetic party started!", TAG);
}

void loop() {

    ade7953.loop();
    
    if (millis() - lastMaintenanceCheck >= MAINTENANCE_CHECK_INTERVAL) {      
      lastMaintenanceCheck = millis();      
      updateStatistics();
      printStatistics();
      printDeviceStatusDynamic();

      if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
        setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
      }

      // If memory is below a certain level, clear the log 
      if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        logger.clearLog();
        logger.warning("Log cleared due to low memory", TAG);
      }
    }

    checkIfRestartEsp32Required();
}