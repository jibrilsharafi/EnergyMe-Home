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
#include "modbustcp.h"

// Global variables
int currentChannel = 0;
int previousChannel = 0;

bool isFirstSetup = false;

GeneralConfiguration generalConfiguration;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

CircularBuffer<data::PayloadMeter, MAX_NUMBER_POINTS_PAYLOAD> payloadMeter;

ModbusTcp modbusTcp(
  MODBUS_TCP_PORT, 
  MODBUS_TCP_SERVER_ID, 
  MODBUS_TCP_MAX_CLIENTS, 
  MODBUS_TCP_TIMEOUT
);

// Custom classes

CustomTime customTime(
  NTP_SERVER,
  TIME_SYNC_INTERVAL
);

AdvancedLogger logger(
  LOG_PATH,
  LOG_CONFIG_PATH,
  LOG_TIMESTAMP_FORMAT
);

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

  log_i("Booting...");
  log_i("EnergyMe - Home");

  log_i("Build version: %s", FIRMWARE_VERSION);
  log_i("Build date: %s", FIRMWARE_DATE);

  log_i("Setting up LED...");
  led.begin();
  log_i("LED setup done");

  led.setCyan();

  log_i("Setting up SPIFFS...");
  if (!SPIFFS.begin(true)) {
    log_e("An Error has occurred while mounting SPIFFS");
    // TODO: add here a function to check the presence of all the required files in the SPIFFS

    // FIXME: remove this temporary fix
    // Check if FIRMWARE_UPDATE_INFO_PATH exists. If it is, check if the content is not null (""). If it is, substitute it with {}
    // Check if FIRMWARE_UPDATE_INFO_PATH exists. If it doesn't, create it and initialize with {}
    File file;
    if (!SPIFFS.exists(FIRMWARE_UPDATE_INFO_PATH)) {
        file = SPIFFS.open(FIRMWARE_UPDATE_INFO_PATH, FILE_WRITE);
        if (!file) {
            log_e("Failed to create firmware update info file");
        } else {
            file.print("{}");
            log_w("Firmware update info file did not exist. Created and initialized with default value");
            file.close();
        }
    }

      if (!SPIFFS.exists(FIRMWARE_UPDATE_STATUS_PATH)) {
        file = SPIFFS.open(FIRMWARE_UPDATE_STATUS_PATH, FILE_WRITE);
        if (!file) {
            log_e("Failed to create firmware update info file");
        } else {
            file.print("{}");
            log_w("Firmware update info file did not exist. Created and initialized with default value");
            file.close();
        }
    }
  } else {
    log_i("SPIFFS setup done");
  }
  
  // Nothing is logged before this point as the logger is not yet initialized
  log_i("Setting up logger...");
  logger.begin();
  log_i("Logger setup done");
  
  logger.info("Booting...", "main::setup");  
  logger.info("EnergyMe - Home", "main::setup");
  logger.info("Build version: %s", "main::setup", FIRMWARE_VERSION);
  logger.info("Build date: %s", "main::setup", FIRMWARE_DATE);
  
  isFirstSetup = checkIfFirstSetup();
  if (isFirstSetup) {
    logger.warning("First setup detected", "main::setup");
  }

  logger.info("Fetching configuration from SPIFFS...", "main::setup");
  if (!setGeneralConfigurationFromSpiffs()) {
    logger.warning("Failed to load configuration from SPIFFS. Using default values.", "main::setup");
    setDefaultGeneralConfiguration();
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
  if (!setupWifi()) {
    restartEsp32("main::setup", "Failed to connect to WiFi and hit timeout");
  } else {
    logger.info("WiFi setup done", "main::setup");
  }
  
  logger.info("Setting up mDNS...", "main::setup");
  if (!setupMdns()) {
    logger.error("Failed to setup mDNS", "main::setup");
  } else {
    logger.info("mDNS setup done", "main::setup");
  }
  
  logger.info("Syncing time...", "main::setup");
  updateTimezone();
  if (!customTime.begin()) {
    logger.error("Time sync failed!", "main::setup");
  } else {
    logger.info("Time synced", "main::setup");
  }
  
  logger.info("Setting up server...", "main::setup");
  setupServer();
  logger.info("Server setup done", "main::setup");

  logger.info("Setting up Modbus TCP...", "main::setup");
  modbusTcp.begin();
  logger.info("Modbus TCP setup done", "main::setup");

  logger.info("Setting up MQTT...", "main::setup");
  if (generalConfiguration.isCloudServicesEnabled) {
    if (!setupMqtt()) {
      logger.error("MQTT initialization failed!", "main::setup");
    } else {
      logger.info("MQTT setup done", "main::setup");
    }
  } else {
    logger.info("Cloud services not enabled", "main::setup");
  }

  if (isFirstSetup) {
    logFirstSetupComplete();
    logger.warning("First setup complete", "main::setup");
  }

  led.setGreen();
  logger.info("Setup done", "main::setup");
}

void loop() {
  checkWifi();

  if (generalConfiguration.isCloudServicesEnabled) {
    mqttLoop();
  }
  
  if (ade7953.isLinecycFinished()) {
    led.setGreen();

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