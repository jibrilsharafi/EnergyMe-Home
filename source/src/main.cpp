#include <Arduino.h>

#include <WiFiManager.h>
#include <ESPmDNS.h>

#include <FS.h>
#include <SPIFFS.h>

#include <ArduinoJson.h>

// Custom libraries
#include "customserver.h"
#include "constants.h"
#include "customwifi.h"
#include "led.h"
#include "multiplexer.h"
#include "ade7953.h"
#include "mqtt.h"
#include "structs.h"
#include "utils.h"
#include "global.h"

// Global variables
int currentChannel = 0;
int previousChannel = 0;
bool firstLinecyc = false;

bool isFirstSetup = false;

long lastMillisChange = 0;
GeneralConfiguration generalConfiguration;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

CircularBuffer<data::PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> payloadMeter;

// Custom classes

CustomTime customTime(
  NTP_SERVER,
  TIME_SYNC_INTERVAL
);

Logger logger;

Led led(
  LED_RED_PIN, 
  LED_GREEN_PIN, 
  LED_BLUE_PIN, 
  LED_DEFAULT_BRIGHTNESS
);

Multiplexer multiplexer(
  MULTIPLEXER_S0_PIN,
  MULTIPLEXER_S1_PIN,
  MULTIPLEXER_S2_PIN,
  MULTIPLEXER_S3_PIN
);

Ade7953 ade7953(
  SS_PIN,
  SCK_PIN,
  MISO_PIN,
  MOSI_PIN,
  ADE7953_RESET_PIN
);

// Main functions
// --------------------

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.printf("\n\n\n\n\n !!! BOOTING !!! \n\n\n");
  Serial.printf("\n\n\n EnergyMe - Home \n\n\n\n\n");

  logger.logOnly("Booting...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  logger.logOnly(("Build version: " + String(FIRMWARE_VERSION)).c_str(), "main::setup", CUSTOM_LOG_LEVEL_INFO);
  logger.logOnly(("Build date: " + String(FIRMWARE_DATE)).c_str(), "main::setup", CUSTOM_LOG_LEVEL_INFO);

  logger.logOnly("Setting up LED...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  led.begin();
  logger.logOnly("LED setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);

  led.setCyan();

  logger.logOnly("Setting up SPIFFS...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (!SPIFFS.begin(true)) {
    logger.logOnly("An Error has occurred while mounting SPIFFS", "main::setup", CUSTOM_LOG_LEVEL_FATAL);
  } else {
    logger.log("Booting...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
    logger.log(("Build version: " + String(FIRMWARE_VERSION)).c_str(), "main::setup", CUSTOM_LOG_LEVEL_INFO);
    logger.log(("Build date: " + String(FIRMWARE_DATE)).c_str(), "main::setup", CUSTOM_LOG_LEVEL_INFO);
    
    logger.log("SPIFFS mounted successfully", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }
  
  logger.log("Setting up logger...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  logger.begin();
  logger.log("Logger setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  
  isFirstSetup = checkIfFirstSetup();
  if (isFirstSetup) {
    logger.log("First setup detected", "main::setup", CUSTOM_LOG_LEVEL_WARNING);
  }

  logger.log("Fetching configuration from SPIFFS...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (!setGeneralConfigurationFromSpiffs()) {
    logger.log("Failed to load configuration from SPIFFS. Using default values.", "main::setup", CUSTOM_LOG_LEVEL_WARNING);
    setDefaultGeneralConfiguration();
  } else {
    logger.log("Configuration loaded from SPIFFS", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }

  led.setPurple();
  
  logger.log("Setting up multiplexer...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  multiplexer.begin();
  logger.log("Multiplexer setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  
  logger.log("Setting up ADE7953...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (!ade7953.begin()) {
    logger.log("ADE7953 initialization failed!", "main::setup", CUSTOM_LOG_LEVEL_FATAL);
  } else {
    logger.log("ADE7953 setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }
  

  led.setBlue();

  logger.log("Setting up WiFi...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (!setupWifi()) {
    restartEsp32("main::setup", "Failed to connect to WiFi and hit timeout");
  } else {
    logger.log("WiFi setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }
  
  logger.log("Setting up mDNS...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (!setupMdns()) {
    logger.log("Failed to setup mDNS", "main::setup", CUSTOM_LOG_LEVEL_ERROR);
  } else {
    logger.log("mDNS setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }
  
  logger.log("Syncing time...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  updateTimezone();
  if (!customTime.begin()) {
    logger.log("Time sync failed!", "main::setup", CUSTOM_LOG_LEVEL_ERROR);
  } else {
    logger.log("Time synced", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }
  
  logger.log("Setting up server...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  setupServer();
  logger.log("Server setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);

  logger.log("Setting up MQTT...", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  if (generalConfiguration.isCloudServicesEnabled) {
    if (!setupMqtt()) {
      logger.log("MQTT initialization failed!", "main::setup", CUSTOM_LOG_LEVEL_ERROR);
    } else {
      logger.log("MQTT setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
    }
  } else {
    logger.log("Cloud services not enabled", "main::setup", CUSTOM_LOG_LEVEL_INFO);
  }

  if (isFirstSetup) {
    logFirstSetupComplete();
    logger.log("First setup complete", "main::setup", CUSTOM_LOG_LEVEL_WARNING);
  }

  led.setGreen();
  logger.log("Setup done", "main::setup", CUSTOM_LOG_LEVEL_INFO);
}

void loop() {
  checkWifi();

  if (generalConfiguration.isCloudServicesEnabled) {
    mqttLoop();
  }
  
  if (ade7953.isLinecycFinished()) {
    ade7953.readMeterValues(currentChannel);
    
    previousChannel = currentChannel;
    currentChannel = ade7953.findNextActiveChannel(currentChannel);
    multiplexer.setChannel(max(currentChannel-1, 0));
    
    printMeterValues(ade7953.meterValues[previousChannel], ade7953.channelData[previousChannel].label.c_str());

    payloadMeter.push(data::PayloadMeter(
      previousChannel,
      customTime.getUnixTime(),
      ade7953.meterValues[previousChannel].activePower,
      ade7953.meterValues[previousChannel].powerFactor
    ));
    
    led.setGreen();
  }

  if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
    restartEsp32("main::loop", "Heap memory has degraded below safe minimum");
  }

  // If memory is below a certain level, clear the logs
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
    logger.clearLog();
  }
  
  ade7953.loop();

  led.setOff();
}