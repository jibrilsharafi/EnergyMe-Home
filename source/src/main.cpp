#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer

// Testing the RTC memory to store the crash counter
#include <esp_system.h>
#include <rom/rtc.h>

// Define structure for RTC memory
RTC_DATA_ATTR struct { //FIXME: this does not really store data across reboots
    uint32_t breadcrumbs[8];  // Stores last 8 checkpoint IDs
    uint8_t currentIndex;     // Current position in circular buffer
    uint32_t crashCount;      // Number of crashes detected
} rtcData;

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
bool isCrashCounterReset = false;

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

const char* getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN: return "Unknown reset";
        case ESP_RST_POWERON: return "Power-on reset";
        case ESP_RST_EXT: return "External pin reset";
        case ESP_RST_SW: return "Software reset";
        case ESP_RST_PANIC: return "Exception/Panic reset";
        case ESP_RST_INT_WDT: return "Interrupt watchdog reset";
        case ESP_RST_TASK_WDT: return "Task watchdog reset";
        case ESP_RST_WDT: return "Other watchdog reset";
        case ESP_RST_DEEPSLEEP: return "Deep sleep reset";
        case ESP_RST_BROWNOUT: return "Brownout reset";
        case ESP_RST_SDIO: return "SDIO reset";
        default: return "Unknown";
    }
}

void setupBreadcrumbs() {
    esp_reset_reason_t _resetReason = esp_reset_reason();
    
    // Initialize on first boot or power-on
    if (_resetReason == ESP_RST_POWERON) {
        memset(&rtcData, 0, sizeof(rtcData));
        logger.info("Power loss detected. Resetting breadcrumbs.", "main::setupBreadcrumbs");
        return;
    }
    
    // Check for crash conditions
    if (_resetReason == ESP_RST_PANIC ||       // Exception/Panic
        _resetReason == ESP_RST_INT_WDT ||     // Interrupt watchdog
        _resetReason == ESP_RST_TASK_WDT ||    // Task watchdog
        _resetReason == ESP_RST_WDT ||         // Other watchdog
        _resetReason == ESP_RST_BROWNOUT) {    // Brownout
        
        rtcData.crashCount++;
        logger.warning("Crash detected! Type: %s (%d) | Crash count: %d", "main::setupBreadcrumbs", getResetReasonString(_resetReason), _resetReason, rtcData.crashCount);
        
        logger.info("Last breadcrumbs (most recent first):", "main::setupBreadcrumbs");
        for (int i = 0; i < 8; i++) {
            uint8_t index = (rtcData.currentIndex - i - 1) & 0x07;
            if (rtcData.breadcrumbs[index] != 0) {
                logger.info("Breadcrumb %d: 0x%04X", "main::setupBreadcrumbs", i, rtcData.breadcrumbs[index]);
            }
        }
    }
}


void leaveBreadcrumb(int checkpoint) {
    // Store checkpoint in circular buffer
    rtcData.breadcrumbs[rtcData.currentIndex] = checkpoint;
    rtcData.currentIndex = (rtcData.currentIndex + 1) & 0x07;  // Keep within 0-7
}

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

  // TODO: delete this part as it is inherently handled by the crash counter
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

  setupBreadcrumbs();
  leaveBreadcrumb(0x1001);

  // Check if the device has crashed more than the maximum allowed times. If so, the device will rollback to the stable firmware
  logger.info("Checking integrity...", "main::setup");
  leaveBreadcrumb(0x1004);
  handleCrashCounter();
  leaveBreadcrumb(0x1005);
  handleFirmwareTesting();
  logger.info("Integrity check done", "main::setup");
  
  leaveBreadcrumb(0x1006);
  if (checkIfFirstSetup() || checkAllFiles()) {
    led.setOrange();

    logger.warning("First setup detected or not all files are present. Creating default files...", "main::setup");
    isFirstSetup = true;
    formatAndCreateDefaultFiles();
    logger.info("Default files created after format", "main::setup");
  }

  leaveBreadcrumb(0x1007);
  logger.info("Fetching general configuration from SPIFFS...", "main::setup");
  setDefaultGeneralConfiguration(); // Start with default values
  if (!setGeneralConfigurationFromSpiffs()) {
    logger.warning("Failed to load configuration from SPIFFS. Using default values.", "main::setup");
  } else {
    logger.info("Configuration loaded from SPIFFS", "main::setup");
  }

  led.setPurple();
  
  leaveBreadcrumb(0x1008);
  logger.info("Setting up multiplexer...", "main::setup");
  multiplexer.begin();
  logger.info("Multiplexer setup done", "main::setup");
  
  leaveBreadcrumb(0x1009);
  logger.info("Setting up ADE7953...", "main::setup");
  if (!ade7953.begin()) {
    logger.fatal("ADE7953 initialization failed!", "main::setup");
  } else {
    logger.info("ADE7953 setup done", "main::setup");
  }
  
  led.setBlue();

  leaveBreadcrumb(0x10010);
  logger.info("Setting up WiFi...", "main::setup");
  if (!customWifi.begin()) {
    setRestartEsp32("main::setup", "Failed to connect to WiFi and hit timeout");
  } else {
    logger.info("WiFi setup done", "main::setup");
  }

  // The mDNS has to be set up in the main setup function as it is required to be globally accessible
  leaveBreadcrumb(0x10011);
  logger.info("Setting up mDNS...", "main::setupMdns");
    if (!MDNS.begin(MDNS_HOSTNAME))
    {
      logger.error("Error setting up mDNS responder!", "main::setupMdns");
    }
  MDNS.addService("http", "tcp", 80);
  logger.info("mDNS setup done", "main::setupMdns");
  
  leaveBreadcrumb(0x10012);
  logger.info("Syncing time...", "main::setup");
  updateTimezone();
  if (!customTime.begin()) {
    logger.error("Time sync failed!", "main::setup");
  } else {
    logger.info("Time synced", "main::setup");
  }
  
  leaveBreadcrumb(0x10013);
  logger.info("Setting up server...", "main::setup");
  customServer.begin();
  logger.info("Server setup done", "main::setup");

  leaveBreadcrumb(0x10014);
  logger.info("Setting up Modbus TCP...", "main::setup");
  modbusTcp.begin();
  logger.info("Modbus TCP setup done", "main::setup");

  leaveBreadcrumb(0x10015);
  if (generalConfiguration.isCloudServicesEnabled) {
    logger.info("Setting up MQTT...", "main::setup");
    mqtt.begin();
    logger.info("MQTT setup done", "main::setup");
  } else {
    logger.info("Cloud services not enabled", "main::setup");
  }

  leaveBreadcrumb(0x10016);
  logger.info("Setting up custom MQTT...", "main::setup");
  customMqtt.setup();
  logger.info("Custom MQTT setup done", "main::setup");

  isFirstSetup = false;

  // Remove the format.txt file as the setup is done
  leaveBreadcrumb(0x10017);
  SPIFFS.remove("/format.txt");

  led.setGreen();

  leaveBreadcrumb(0x10018);
  logger.info("Setup done", "main::setup");
}

void loop() {
  leaveBreadcrumb(0x1100);
  customWifi.loop();
  leaveBreadcrumb(0x1101);
  mqtt.loop();
  leaveBreadcrumb(0x1102);
  customMqtt.loop();
  leaveBreadcrumb(0x1103);
  ade7953.loop();
  
  leaveBreadcrumb(0x1104);
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

  leaveBreadcrumb(0x1105);
  if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
    printDeviceStatus();
    setRestartEsp32("main::loop", "Heap memory has degraded below safe minimum");
  }

  // If memory is below a certain level, clear the log
  leaveBreadcrumb(0x1106);
  if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
    printDeviceStatus();
    logger.clearLog();
    logger.warning("Log cleared due to low memory", "main::loop");
  }

  leaveBreadcrumb(0x1107);
  firmwareTestingLoop();
  leaveBreadcrumb(0x1108);
  crashCounterLoop();
  leaveBreadcrumb(0x1109);
  checkIfRestartEsp32Required();
  
  leaveBreadcrumb(0x1110);
  led.setOff();
}