#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer

// Project includes
#include "ade7953.h"
#include "constants.h"
#include "customwifi.h" // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "customserver.h"
#include "global.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "multiplexer.h"
#include "structs.h"
#include "utils.h"

// Global variables
int currentChannel = 0;
int previousChannel = 0;

GeneralConfiguration generalConfiguration;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);
AsyncWebServer server(80);

CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;

AdvancedLogger logger(
  LOG_PATH,
  LOG_CONFIG_PATH,
  LOG_TIMESTAMP_FORMAT
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
  logger
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
  customTime
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

CustomServer customServer(
  logger,
  led,
  ade7953,
  customTime,
  customWifi
);

Mqtt mqtt(
  ade7953,
  logger,
  customTime
);

// Main functions
// --------------------

void setup() {
  Serial.begin(SERIAL_BAUDRATE);

  Serial.println("Booting...");
  Serial.println("EnergyMe - Home");

  Serial.printf("Build version: %s\n", FIRMWARE_BUILD_VERSION);
  Serial.printf("Build date: %s\n", FIRMWARE_BUILD_DATE);

  Serial.println("Setting up LED...");
  led.begin();
  Serial.println("LED setup done");

  led.setCyan();

  Serial.println("Setting up SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
  } else {
    Serial.println("SPIFFS setup done");
  }
  
  // Nothing is logged before this point as the logger is not yet initialized
  Serial.println("Setting up logger...");
  logger.begin();
  logger.setPrintLevel(LogLevel::DEBUG);
  logger.setMaxLogLines(LOG_FILE_MAX_LENGTH);
  logger.info("Logger setup done", "main::setup");
  
  logger.info("Booting...", "main::setup");  
  logger.info("EnergyMe - Home", "main::setup");
  logger.info("Build version: %s", "main::setup", FIRMWARE_BUILD_VERSION);
  logger.info("Build date: %s", "main::setup", FIRMWARE_BUILD_DATE);
  
  if (checkIfFirstSetup() || checkAllFiles()) {
    logger.warning("First setup detected or not all files are present. Creating default files...", "main::setup");
    createDefaultFiles();
    logger.info("Default files created", "main::setup");
  }

  logger.info("Fetching configuration from SPIFFS...", "main::setup");
  setDefaultGeneralConfiguration(); // Start with default values
  if (!setGeneralConfigurationFromSpiffs()) {
    logger.warning("Failed to load configuration from SPIFFS. Using default values.", "main::setup");
  } else {
    logger.info("Configuration loaded from SPIFFS", "main::setup");
  }

  led.setPurple();
  
  logger.info("Setting up multiplexer...", "main::setup");
  multiplexer.begin();
  logger.info("Multiplexer setup done", "main::setup");
  
  logger.info("Setting up ADE7953...", "main::setup");
  if (!ade7953.begin()) {
    logger.fatal("ADE7953 initialization failed!", "main::setup");
  } else {
    logger.info("ADE7953 setup done", "main::setup");
  }
  
  led.setBlue();

  logger.info("Setting up WiFi...", "main::setup");
  if (!customWifi.begin()) {
    restartEsp32("main::setup", "Failed to connect to WiFi and hit timeout");
  } else {
    logger.info("WiFi setup done", "main::setup");
  }
  
  logger.info("Syncing time...", "main::setup");
  updateTimezone();
  if (!customTime.begin()) {
    logger.error("Time sync failed!", "main::setup");
  } else {
    logger.info("Time synced", "main::setup");
  }
  
  logger.info("Setting up server...", "main::setup");
  customServer.begin();
  logger.info("Server setup done", "main::setup");

  logger.info("Setting up Modbus TCP...", "main::setup");
  modbusTcp.begin();
  logger.info("Modbus TCP setup done", "main::setup");

  logger.info("Setting up MQTT...", "main::setup");
  if (generalConfiguration.isCloudServicesEnabled) {
    if (!mqtt.begin()) {
      logger.error("MQTT initialization failed!", "main::setup");
    } else {
      logger.info("MQTT setup done", "main::setup");
    }
  } else {
    logger.info("Cloud services not enabled", "main::setup");
  }

  led.setGreen();
  logger.info("Setup done", "main::setup");
}

void loop() {
  customWifi.loop();
  mqtt.loop();
  ade7953.loop();
  
  if (ade7953.isLinecycFinished()) {
    led.setGreen();

    ade7953.readMeterValues(currentChannel);
    
    previousChannel = currentChannel;
    currentChannel = ade7953.findNextActiveChannel(currentChannel);
    multiplexer.setChannel(max(currentChannel-1, 0));
    
    printMeterValues(ade7953.meterValues[previousChannel], ade7953.channelData[previousChannel].label.c_str());

    payloadMeter.push(PayloadMeter(
      previousChannel,
      customTime.getUnixTime(),
      ade7953.meterValues[previousChannel].activePower,
      ade7953.meterValues[previousChannel].powerFactor
    )); 
  }

  if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
    restartEsp32("main::loop", "Heap memory has degraded below safe minimum");
  }

  // If memory is below a certain level, clear the logs
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
    logger.clearLog();
    logger.warning("Logs cleared due to low memory", "main::loop");
  }
  
  led.setOff();
}