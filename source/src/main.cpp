/*
EnergyMe - Home
Copyright (C) 2025 Jibril Sharafi
*/

#include <Arduino.h>
#include <AdvancedLogger.h>
#include <LittleFS.h>

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

Statistics statistics; // Move both to utils and use getter to get and set them
char DEVICE_ID[DEVICE_ID_BUFFER_SIZE];

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

  // Need to call this once and at begin to ensure PSRAM is used for all mbedtls (both for OTA and MQTT connection. and maybe InfluxDB)
  mbedtls_platform_set_calloc_free(ota_calloc_psram, ota_free_psram);

  Serial.println("Setting up LED...");
  Led::begin(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);
  Serial.println("LED setup done");

  Led::setWhite(Led::PRIO_NORMAL);

  if (!isFirstBootDone())
  {
    setFirstBootDone();
    createAllNamespaces();
    LOG_INFO("First boot setup complete. Welcome aboard!");
  }

  if (!LittleFS.begin(true)) // Ensure the partition name is "spiffs" in partitions.csv (even when using LittleFS). Setting the partition label to "littlefs" caused issues
  {
    Serial.println("LittleFS initialization failed!");
    ESP.restart();
    return;
  }

  Led::setYellow(Led::PRIO_NORMAL);
  AdvancedLogger::begin(LOG_PATH);
  LOG_DEBUG("AdvancedLogger initialized with log path: %s", LOG_PATH);
  
  LOG_DEBUG("Setting up callbacks for AdvancedLogger...");
  AdvancedLogger::setCallback(CustomLog::callbackMultiple);
  LOG_DEBUG("Callbacks for AdvancedLogger set up successfully");

  LOG_INFO("Guess who's back, back again! EnergyMe - Home is starting up...");
  LOG_INFO("Build version: %s | Build date: %s %s | Device ID: %s", FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME, DEVICE_ID);
  
  LOG_DEBUG("Setting up crash monitor...");
  CrashMonitor::begin();
  LOG_INFO("Crash monitor setup done");

  printDeviceStatusStatic();

  Led::setPurple(Led::PRIO_NORMAL);
  LOG_DEBUG("Setting up multiplexer...");
  Multiplexer::begin(
      MULTIPLEXER_S0_PIN,
      MULTIPLEXER_S1_PIN,
      MULTIPLEXER_S2_PIN,
      MULTIPLEXER_S3_PIN);
  LOG_INFO("Multiplexer setup done");

  LOG_DEBUG("Setting up button handler...");
  ButtonHandler::begin(BUTTON_GPIO0_PIN);
  LOG_INFO("Button handler setup done");

  LOG_DEBUG("Setting up ADE7953...");
  if (Ade7953::begin(
      ADE7953_SS_PIN,
      ADE7953_SCK_PIN,
      ADE7953_MISO_PIN,
      ADE7953_MOSI_PIN,
      ADE7953_RESET_PIN,
      ADE7953_INTERRUPT_PIN)
    ) {
      LOG_INFO("ADE7953 setup done");
  } else {
      LOG_ERROR("ADE7953 initialization failed! This is a big issue mate..");
  }

  Led::setBlue(Led::PRIO_NORMAL);
  LOG_DEBUG("Setting up WiFi...");
  CustomWifi::begin();
  LOG_INFO("WiFi setup done");

  while (!CustomWifi::isFullyConnected()) // TODO: maybe we can move this and everything related to wifi connection to the wifi itself and handle it async
  {
    LOG_DEBUG("Waiting for full WiFi connection...");
    delay(1000);
  }

  // Add UDP logging setup after WiFi
  LOG_DEBUG("Setting up UDP logging...");
  CustomLog::begin();
  LOG_INFO("UDP logging setup done");

  LOG_DEBUG("Syncing time...");
  if (CustomTime::begin()) LOG_INFO("Initial time sync successful");
  else LOG_ERROR("Initial time sync failed! Will retry later.");

  LOG_DEBUG("Setting up server...");
  CustomServer::begin();
  LOG_INFO("Server setup done");

  LOG_DEBUG("Setting up Modbus TCP...");
  ModbusTcp::begin();
  LOG_INFO("Modbus TCP setup done");

  #ifdef HAS_SECRETS
  LOG_DEBUG("Setting up MQTT client...");
  Mqtt::begin();
  LOG_INFO("MQTT client setup done");
  #endif

  LOG_DEBUG("Setting up Custom MQTT client...");
  CustomMqtt::begin();
  LOG_INFO("Custom MQTT client setup done");

  LOG_DEBUG("Setting up InfluxDB client...");
  InfluxDbClient::begin();
  LOG_INFO("InfluxDB client setup done");

  LOG_DEBUG("Starting maintenance task...");
  startMaintenanceTask();
  LOG_INFO("Maintenance task started");

  Led::setGreen(Led::PRIO_NORMAL);
  printStatistics();
  printDeviceStatusDynamic();
  LOG_INFO("Setup done! Let's get this energetic party started!");

  // Since in the loop there is nothing we care about, let's just kill the main task to gain some heap
  delay(1000);
  vTaskDelete(NULL);
}

void loop()
{
  // Oh yes, it took a incredible amount of time but finally we have a loop in which "nothing" happens
  // This is because all of the tasks are running in their own FreeRTOS tasks
  // Much better than the old way of having everything in the main loop blocking
  // This will never run, but we leave the delay for safety
  vTaskDelay(portMAX_DELAY);
}