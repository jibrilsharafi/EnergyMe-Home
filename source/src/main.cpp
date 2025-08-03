/*
EnergyMe - Home
Copyright (C) 2025 Jibril Sharafi
*/

#include <Arduino.h>
#include <SPIFFS.h>

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
#include "custommqtt.h"
#include "influxdbclient.h"
#include "multiplexer.h"
#include "customlog.h"

// Global variables
// --------------------

static const char *TAG = "main";

Statistics statistics;
char DEVICE_ID[DEVICE_ID_BUFFER_SIZE];

// Classes instances
// --------------------

AdvancedLogger logger(
    LOG_PATH,
    LOG_CONFIG_PATH,
    LOG_TIMESTAMP_FORMAT);

void setup()
{
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

  Led::setWhite(Led::PRIO_NORMAL);
  // Disable watchdog during SPIFFS begin because if the formatting is required, it can take a while
  // And we don't want to continuously print an error to Serial
  // TODO: actually do this (code 705)
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS initialization failed!");
    ESP.restart();
    return;
  }

  Led::setYellow(Led::PRIO_NORMAL);
  logger.begin();
  logger.setCallback(CustomLog::callbackMultiple);

  logger.info("Guess who's back, back again! EnergyMe - Home is starting up...", TAG);
  logger.info("Build version: %s | Build date: %s %s | Device ID: %s", TAG, FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME, DEVICE_ID);
  printDeviceStatusStatic();
  
  logger.debug("Setting up crash monitor...", TAG);
  CrashMonitor::begin();
  logger.info("Crash monitor setup done", TAG);

  Led::setPurple(Led::PRIO_NORMAL);
  logger.debug("Setting up multiplexer...", TAG);
  Multiplexer::begin(
      MULTIPLEXER_S0_PIN,
      MULTIPLEXER_S1_PIN,
      MULTIPLEXER_S2_PIN,
      MULTIPLEXER_S3_PIN);
  logger.info("Multiplexer setup done", TAG);

  logger.debug("Setting up button handler...", TAG);
  ButtonHandler::begin(BUTTON_GPIO0_PIN);
  logger.info("Button handler setup done", TAG);

  logger.debug("Setting up ADE7953...", TAG);
  if (Ade7953::begin(
      ADE7953_SS_PIN,
      ADE7953_SCK_PIN,
      ADE7953_MISO_PIN,
      ADE7953_MOSI_PIN,
      ADE7953_RESET_PIN,
      ADE7953_INTERRUPT_PIN)
    ) {
      logger.info("ADE7953 setup done", TAG);
  } else {
      logger.error("ADE7953 initialization failed! This is a big issue mate..", TAG);
  }

  Led::setBlue(Led::PRIO_NORMAL);
  logger.debug("Setting up WiFi...", TAG);
  CustomWifi::begin();
  logger.info("WiFi setup done", TAG);

  while (!CustomWifi::isFullyConnected())
  {
    logger.debug("Waiting for full WiFi connection...", TAG);
    delay(1000);
  }

  // Add UDP logging setup after WiFi
  logger.debug("Setting up UDP logging...", TAG);
  CustomLog::begin();
  logger.info("UDP logging setup done", TAG);

  logger.debug("Syncing time...", TAG);
  if (CustomTime::begin())
  {
    char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
    CustomTime::getTimestamp(timestampBuffer, sizeof(timestampBuffer));
    logger.info("Initial time sync successful. Current timestamp: %s", TAG, timestampBuffer);
  }
  else
  {
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

  logger.debug("Setting up Custom MQTT client...", TAG);
  CustomMqtt::begin();
  logger.info("Custom MQTT client setup done", TAG);

  logger.debug("Setting up InfluxDB client...", TAG);
  InfluxDbClient::begin();
  logger.info("InfluxDB client setup done", TAG);

  logger.debug("Starting maintenance task...", TAG);
  startMaintenanceTask();
  logger.info("Maintenance task started", TAG);

  Led::setGreen(Led::PRIO_NORMAL);
  printDeviceStatusDynamic();
  logger.info("Setup done! Let's get this energetic party started!", TAG);
}

void loop()
{
  // Oh yes, it took a int32_t time but finally we have a loop in which "nothing" happens
  // This is because all of the tasks are running in their own FreeRTOS tasks
  // Much better than the old way of having everything in the main loop blocking
}