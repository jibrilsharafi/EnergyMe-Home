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
#include "custommqtt.h"
#include "multiplexer.h"
#include "structs.h"
#include "utils.h"

// Global variables
RestartConfiguration restartConfiguration;
PublishMqtt publishMqtt;

bool isFirstSetup = false;
bool isFirmwareUpdate = false;

int currentChannel = 0;
int previousChannel = 0;

GeneralConfiguration generalConfiguration;
CustomMqttConfiguration customMqttConfiguration;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

WiFiClient customNet;
PubSubClient customClientMqtt(customNet);

CircularBuffer<PayloadMeter, PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;

AsyncWebServer server(80);

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

CustomMqtt customMqtt(
  ade7953,
  logger
);

Mqtt mqtt(
  ade7953,
  logger,
  customTime
);

CustomServer customServer(
  logger,
  led,
  ade7953,
  customTime,
  customWifi,
  customMqtt
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

  led.setWhite();

  Serial.println("Setting up SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
  } else {
    Serial.println("SPIFFS setup done");
  }

  led.setCyan();

  // Check if the device has crashed more than the maximum allowed times. If so, the device will rollback to the stable firmware
  handleCrashCounter();
  handleFirmwareTesting();

  if (SPIFFS.exists("/format.txt")) {
    led.setOrange();
    Serial.println("Format file detected. Formatting SPIFFS...");
    SPIFFS.remove("/format.txt");
    SPIFFS.format();
    Serial.println("SPIFFS formatted. Rebooting...");
    ESP.restart();
  }

  // Create format.txt file to format SPIFFS. If the setup is successful, the file will be removed
  // Otherwise, to prevent the device from getting stuck in a boot loop, the file will be checked
  // and the device will be formatted if the file is present
  File file = SPIFFS.open("/format.txt", FILE_WRITE);
  if (!file) {
    Serial.println("There was an error opening the file for writing");
  } else {
    file.println("");
    file.close();
  }
  
  // Nothing is logged before this point as the logger is not yet initialized
  Serial.println("Setting up logger...");
  logger.begin();
  logger.debug("Logger setup done", "main::setup");
  
  logger.info("Booting...", "main::setup");  

  logger.info("EnergyMe - Home | Build version: %s | Build date: %s", "main::setup", FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE);
  
  if (checkIfFirstSetup() || checkAllFiles()) {
    led.setOrange();

    logger.warning("First setup detected or not all files are present. Creating default files...", "main::setup");
    isFirstSetup = true;
    formatAndCreateDefaultFiles();
    logger.info("Default files created after format", "main::setup");
  }

  logger.info("Fetching general configuration from SPIFFS...", "main::setup");
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
    setRestartEsp32("main::setup", "Failed to connect to WiFi and hit timeout");
  } else {
    logger.info("WiFi setup done", "main::setup");
  }

  // The mDNS has to be set up in the main setup function as it is required to be globally accessible
  logger.info("Setting up mDNS...", "main::setupMdns");
    if (!MDNS.begin(MDNS_HOSTNAME))
    {
      logger.error("Error setting up mDNS responder!", "main::setupMdns");
    }
  MDNS.addService("http", "tcp", 80);
  logger.info("mDNS setup done", "main::setupMdns");
  
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

  if (generalConfiguration.isCloudServicesEnabled) {
    logger.info("Setting up MQTT...", "main::setup");
    mqtt.begin(getDeviceId());
    logger.info("MQTT setup done", "main::setup");
  } else {
    logger.info("Cloud services not enabled", "main::setup");
  }

  logger.info("Setting up custom MQTT...", "main::setup");
  customMqtt.setup();
  logger.info("Custom MQTT setup done", "main::setup");

  isFirstSetup = false;

  // Remove the format.txt file as the setup is done
  SPIFFS.remove("/format.txt");

  led.setGreen();
  logger.info("Setup done", "main::setup");
}

void loop() {
  customWifi.loop();
  mqtt.loop();
  customMqtt.loop();
  ade7953.loop();
  
  if (ade7953.isLinecycFinished()) {
    led.setGreen();

    ade7953.readMeterValues(currentChannel);
    
    previousChannel = currentChannel;
    currentChannel = ade7953.findNextActiveChannel(currentChannel);
    multiplexer.setChannel(max(currentChannel-1, 0));
    
    printMeterValues(ade7953.meterValues[previousChannel], ade7953.channelData[previousChannel].label.c_str());

    payloadMeter.push(
      PayloadMeter(
        previousChannel,
        customTime.getUnixTime(),
        ade7953.meterValues[previousChannel].activePower,
        ade7953.meterValues[previousChannel].powerFactor
        )
      );
  }

  if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
    printDeviceStatus();
    setRestartEsp32("main::loop", "Heap memory has degraded below safe minimum");
  }

  // If memory is below a certain level, clear the log
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
    printDeviceStatus();
    logger.clearLog();
    logger.warning("Log cleared due to low memory", "main::loop");
  }

  firmwareTestingLoop();
  checkIfRestartEsp32Required();
  
  led.setOff();
}