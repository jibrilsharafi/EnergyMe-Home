#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer
#include <CircularBuffer.hpp>

// Project includes
#include "ade7953.h"
#include "buttonhandler.h"
#include "constants.h"
#include "crashmonitor.h"
#include "customwifi.h" // Needs to be defined before customserver.h due to conflict between WiFiManager and ESPAsyncWebServer
#include "customserver.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "custommqtt.h"
#include "influxdbclient.h"
#include "multiplexer.h"
#include "structs.h"
#include "utils.h"

// Global variables
// --------------------

static const char *TAG = "main";

RestartConfiguration restartConfiguration;
PublishMqtt publishMqtt;

MainFlags mainFlags;

GeneralConfiguration generalConfiguration;
CustomMqttConfiguration customMqttConfiguration;
InfluxDbConfiguration influxDbConfiguration;
RTC_NOINIT_ATTR CrashData crashData;
RTC_NOINIT_ATTR DebugFlagsRtc debugFlagsRtc;

WiFiClientSecure net = WiFiClientSecure();
PubSubClient clientMqtt(net);

WiFiClient customNet;
PubSubClient customClientMqtt(customNet);

CircularBuffer<PayloadMeter, MQTT_PAYLOAD_METER_MAX_NUMBER_POINTS> payloadMeter;

AsyncWebServer server(WEBSERVER_PORT);

// Callback variables
CircularBuffer<LogJson, LOG_BUFFER_SIZE> logBuffer;
char jsonBuffer[LOG_JSON_BUFFER_SIZE];  // Pre-allocated buffer
String deviceId;      // Pre-allocated buffer

// Interrupt handling
// --------------------
volatile bool ade7953InterruptFlag = false;

// Interrupt Service Routine for ADE7953
void IRAM_ATTR ade7953ISR() {
    ade7953InterruptFlag = true;
}

// Utils variables
unsigned long long _linecycUnix = 0;   // Used to track the Unix time when the linecycle ended (for MQTT payloads)

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

ButtonHandler buttonHandler(
  BUTTON_GPIO0_PIN,
  logger,
  led,
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
  customTime
);

InfluxDbClient influxDbClient(
  ade7953,
  logger,
  influxDbConfiguration,
  customTime
);

Mqtt mqtt(
  ade7953,
  logger,
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
  customWifi,
  customMqtt,
  influxDbClient,
  buttonHandler
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
    if (
      (strcmp(level, "debug") == 0 && !debugFlagsRtc.enableMqttDebugLogging) || // Only send debug logs if MQTT debug logging is enabled
      (strcmp(level, "verbose") == 0) // Never send verbose logs via MQTT
    ) {return; }

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

    // If not connected to WiFi and MQTT, return (log is still stored in circular buffer for later) 
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
        snprintf(
          topic, 
          sizeof(topic), 
          "%s/%s/%s/%s/%s", 
          MQTT_TOPIC_1, 
          MQTT_TOPIC_2, 
          deviceId.c_str(), 
          MQTT_TOPIC_LOG, 
        _log.level);

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

    logger.info("Booting...", TAG);  
    logger.info("EnergyMe - Home | Build version: %s | Build date: %s %s", TAG, FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);

    logger.info("Setting up crash monitor...", TAG);
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

    led.setCyan();

    TRACE
    logger.info("Checking for missing files...", TAG);
    auto missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        led.setOrange();
        logger.warning("Missing files detected. Creating default files for missing files...", TAG);
        
        TRACE
        createDefaultFilesForMissingFiles(missingFiles);

        logger.info("Default files created for missing files", TAG);
    } else {
        logger.info("No missing files detected", TAG);
    }

    TRACE
    logger.info("Fetching general configuration from SPIFFS...", TAG);
    if (!setGeneralConfigurationFromSpiffs()) {
        logger.warning("Failed to load configuration from SPIFFS. Using default values.", TAG);
    } else {
        logger.info("Configuration loaded from SPIFFS", TAG);
    }

    led.setPurple();
    
    TRACE
    logger.info("Setting up multiplexer...", TAG);
    multiplexer.begin();
    logger.info("Multiplexer setup done", TAG);

    TRACE
    logger.info("Setting up ADE7953...", TAG);
    attachInterrupt(digitalPinToInterrupt(ADE7953_INTERRUPT_PIN), ade7953ISR, FALLING); // This has to be done before ade7953.begin() 
    if (!ade7953.begin()) {
      logger.fatal("ADE7953 initialization failed!", TAG);
    } else {
      logger.info("ADE7953 setup done", TAG);
    }

    led.setBlue();    
    
    TRACE
    logger.info("Setting up WiFi...", TAG);
    customWifi.begin();
    logger.info("WiFi setup done", TAG);

    TRACE
    logger.info("Setting up button handler...", TAG);
    buttonHandler.begin();
    logger.info("Button handler setup done", TAG);

    TRACE
    logger.info("Syncing time...", TAG);
    updateTimezone();
    if (!customTime.begin()) {
      logger.error("Initial time sync failed! Will retry later.", TAG);
    } else {
        logger.info("Time synced", TAG);
    }
    
    TRACE
    logger.info("Setting up server...", TAG);
    customServer.begin();
    logger.info("Server setup done", TAG);

    TRACE
    logger.info("Setting up Modbus TCP...", TAG);
    modbusTcp.begin();
    logger.info("Modbus TCP setup done", TAG);

    led.setGreen();

    TRACE
    logger.info("Setup done", TAG);
}

void loop() {
    TRACE
    if (mainFlags.blockLoop) return;

    TRACE
    customTime.loop();

    TRACE
    crashMonitor.crashCounterLoop();

    TRACE
    crashMonitor.firmwareTestingLoop();
      
    TRACE
    customWifi.loop();
    
    TRACE
    buttonHandler.loop();
    
    TRACE
    mqtt.loop();
    
    TRACE
    customMqtt.loop();
    
    TRACE
    influxDbClient.loop();

    TRACE
    ade7953.loop();
    
    TRACE
    if (ade7953InterruptFlag) {
      _linecycUnix = customTime.getUnixTimeMilliseconds(); // Update the linecyc Unix time

      ade7953InterruptFlag = false;
      ade7953.handleInterrupt();
  
      led.setGreen();

      // Since there is a settling time after the multiplexer is switched, 
      // we let one cycle pass before we start reading the values
      if (mainFlags.isFirstLinecyc) {
          mainFlags.isFirstLinecyc = false;
          ade7953.purgeEnergyRegister(mainFlags.currentChannel);
      } else {
          mainFlags.isFirstLinecyc = true;

          if (mainFlags.currentChannel != -1) { // -1 indicates that no channel is active
            TRACE
            if (ade7953.readMeterValues(mainFlags.currentChannel, _linecycUnix)) {
              if (customTime.isTimeSynched() && millis() > MINIMUM_TIME_BEFORE_VALID_METER) { // Wait after the first time boot
                TRACE
                payloadMeter.push(
                  PayloadMeter(
                    mainFlags.currentChannel,
                    _linecycUnix,
                    ade7953.meterValues[mainFlags.currentChannel].activePower,
                    ade7953.meterValues[mainFlags.currentChannel].powerFactor
                  )
                );
              }
              
              printMeterValues(ade7953.meterValues[mainFlags.currentChannel], ade7953.channelData[mainFlags.currentChannel].label.c_str());
            }
          }

          TRACE
          mainFlags.currentChannel = ade7953.findNextActiveChannel(mainFlags.currentChannel);
          multiplexer.setChannel(max(mainFlags.currentChannel-1, 0));
      }

      // We always read the first channel as it is in a separate channel in the ADE7953 and is not impacted by the switching of the multiplexer
      TRACE
      if (ade7953.readMeterValues(0, _linecycUnix)) {
        if (customTime.isTimeSynched() && millis() > MINIMUM_TIME_BEFORE_VALID_METER) {
          TRACE
          payloadMeter.push(
            PayloadMeter(
              0,
              _linecycUnix,
              ade7953.meterValues[0].activePower,
              ade7953.meterValues[0].powerFactor
            )
          );
        }
        printMeterValues(ade7953.meterValues[0], ade7953.channelData[0].label.c_str());
      }
    }

    TRACE
    if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        printDeviceStatus();
        logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
        setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
    }

    // If memory is below a certain level, clear the log
    TRACE
    if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        printDeviceStatus();
        logger.clearLog();
        logger.warning("Log cleared due to low memory", TAG);
    }

    TRACE
    checkIfRestartEsp32Required();
    
    TRACE
    if (debugFlagsRtc.enableMqttDebugLogging && millis() >= debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
        logger.info("MQTT debug logging period ended.", TAG);

        debugFlagsRtc.enableMqttDebugLogging = false;
        debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
        debugFlagsRtc.signature = 0; 
    }

    TRACE
    led.setOff();
}