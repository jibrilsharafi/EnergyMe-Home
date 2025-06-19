#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFiManager.h> // Needs to be defined on top due to conflict between WiFiManager and ESPAsyncWebServer
#include <CircularBuffer.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <WiFiUdp.h>

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
Statistics statistics;

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
char baseMqttTopicLogs[LOG_TOPIC_SIZE];
char fullMqttTopic[LOG_TOPIC_SIZE];
String deviceId;

// UDP logging variables
WiFiUDP udpClient;
IPAddress broadcastIP;
char udpBuffer[UDP_LOG_BUFFER_SIZE];  // Separate buffer for UDP
bool isUdpLoggingEnabled = DEFAULT_IS_UDP_LOGGING_ENABLED;

// Interrupt handling
// --------------------
volatile unsigned long ade7953TotalInterrupts = 0;
volatile unsigned long ade7953LastInterruptTime = 0;

// Utils variables
unsigned long long _linecycUnix = 0;   // Used to track the Unix time when the linecycle ended (for MQTT payloads)
unsigned long lastMaintenanceCheck = 0;

// FreeRTOS task variables
TaskHandle_t meterReadingTaskHandle = NULL;
SemaphoreHandle_t payloadMeterMutex = NULL;
SemaphoreHandle_t ade7953InterruptSemaphore = NULL;

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
// Tasks
// --------------------

// Interrupt Service Routine for ADE7953
void IRAM_ATTR ade7953ISR() {
  
  // Check if semaphore exists to prevent crashes during shutdown/startup
  if (ade7953InterruptSemaphore == NULL) {
    return; // Exit early if semaphore doesn't exist
  }
  
  mainFlags.currentChannel = ade7953.findNextActiveChannel(mainFlags.currentChannel);
  multiplexer.setChannel(max(mainFlags.currentChannel - 1, 0));

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  
  statistics.ade7953TotalInterrupts++;
  ade7953LastInterruptTime = millis();
    
  // Give binary semaphore from ISR - this will wake up the waiting task
  // If semaphore is already given, this call will be ignored (no queuing needed)
  xSemaphoreGiveFromISR(ade7953InterruptSemaphore, &xHigherPriorityTaskWoken);
  
  // Request context switch if a higher priority task was woken
  if (xHigherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

void meterReadingTask(void *parameter)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (true)
  {

    // Wait for interrupt semaphore with timeout
    // Check if semaphore still exists to prevent crashes during shutdown
    if (
        ade7953InterruptSemaphore != NULL &&
        xSemaphoreTake(ade7953InterruptSemaphore, pdMS_TO_TICKS(ADE7953_INTERRUPT_TIMEOUT_MS)) == pdTRUE)
    {
      _linecycUnix = customTime.getUnixTimeMilliseconds(); // Update the linecyc Unix time

      if (
          millis() > MINIMUM_TIME_BEFORE_VALID_METER &&
          millis() - ade7953LastInterruptTime >= ade7953.getSampleTime())
      {
        logger.warning("ADE7953 interrupt not handled within the sample time. We are %lu ms late (sample time is %lu ms).",
                       TAG,
                       millis() - ade7953LastInterruptTime,
                       ade7953.getSampleTime());
      }

      ade7953.handleInterrupt();

      led.setGreen();

      if (mainFlags.currentChannel != -1)
      { // -1 indicates that no channel is active
        if (ade7953.readMeterValues(mainFlags.currentChannel, _linecycUnix))
        {
          if (customTime.isTimeSynched() && millis() > MINIMUM_TIME_BEFORE_VALID_METER)
          { // Wait after the first time boot
            PAYLOAD_METER_LOCK();
            payloadMeter.push(
                PayloadMeter(
                    mainFlags.currentChannel,
                    _linecycUnix,
                    ade7953.meterValues[mainFlags.currentChannel].activePower,
                    ade7953.meterValues[mainFlags.currentChannel].powerFactor));
            PAYLOAD_METER_UNLOCK();
          }

          printMeterValues(&ade7953.meterValues[mainFlags.currentChannel], &ade7953.channelData[mainFlags.currentChannel]);
        }
      }

      // We always read the first channel as it is in a separate channel in the ADE7953 and is not impacted by the switching of the multiplexer
      if (ade7953.readMeterValues(CHANNEL_0, _linecycUnix))
      {
        if (customTime.isTimeSynched() && millis() > MINIMUM_TIME_BEFORE_VALID_METER)
        {
          PAYLOAD_METER_LOCK();
          payloadMeter.push(
              PayloadMeter(
                  CHANNEL_0,
                  _linecycUnix,
                  ade7953.meterValues[CHANNEL_0].activePower,
                  ade7953.meterValues[CHANNEL_0].powerFactor));
          PAYLOAD_METER_UNLOCK();
        }
        printMeterValues(&ade7953.meterValues[CHANNEL_0], &ade7953.channelData[CHANNEL_0]);
      }
    }
    else
    {
      // Check if semaphore is NULL (during shutdown) and exit gracefully
      if (ade7953InterruptSemaphore == NULL)
      {
        logger.debug("Semaphore is NULL, task exiting...", TAG);
        break;
      }

      // Timeout occurred - this means no interrupt was received within the timeout period
      // This is normal behavior when no energy flow is detected or during low activity periods
      // Use vTaskDelayUntil for precise timing to prevent drift
      vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }

    led.setOff(); // Turn off the LED after processing the interrupt
  }
}

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
    ) return;

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
    if (clientMqtt.state() != MQTT_CONNECTED) return;

    unsigned int _loops = 0;
    while (!logBuffer.isEmpty() && _loops < MAX_LOOP_ITERATIONS) {
        _loops++;

        LogJson _log = logBuffer.shift();
        size_t totalLength = strlen(_log.timestamp) + strlen(_log.function) + strlen(_log.message) + 100; // Leave overhead
        if (totalLength > sizeof(jsonBuffer)) continue;

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

        snprintf(
          fullMqttTopic, 
          sizeof(fullMqttTopic), 
          "%s/%s", 
          baseMqttTopicLogs, 
          _log.level
        );

        if (!clientMqtt.publish(fullMqttTopic, jsonBuffer)) {
            Serial.printf("MQTT publish failed to %s. Error: %d\n", 
                fullMqttTopic, clientMqtt.state());
            logBuffer.push(_log);
            break;
        }
    }
}

void callbackLogToUdp(
    const char* timestamp,
    unsigned long millisEsp,
    const char* level,
    unsigned int coreId,
    const char* function,
    const char* message
) {
    if (!isUdpLoggingEnabled) return;
    if (strcmp(level, "verbose") == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    
    // Format as simplified syslog message
    snprintf(udpBuffer, sizeof(udpBuffer),
        "<%d>%s %s[%lu]: [%s][Core%u] %s: %s",
        16, // Facility.Severity (local0.info)
        timestamp,
        deviceId.c_str(),
        millisEsp,
        level,
        coreId,
        function,
        message);
    
    udpClient.beginPacket(broadcastIP, UDP_LOG_PORT);
    udpClient.write((const uint8_t*)udpBuffer, strlen(udpBuffer));
    udpClient.endPacket();
}

void callbackLogMultiple(
    const char* timestamp,
    unsigned long millisEsp,
    const char* level,
    unsigned int coreId,
    const char* function,
    const char* message
) {
  callbackLogToUdp(timestamp, millisEsp, level, coreId, function, message);
  callbackLogToMqtt(timestamp, millisEsp, level, coreId, function, message);
}

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    Serial.printf("EnergyMe - Home\n____________________\n\n");
    Serial.println("Booting...");
    Serial.printf("Build version: %s\n", FIRMWARE_BUILD_VERSION);
    Serial.printf("Build date: %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
    deviceId = getDeviceId();
    Serial.printf("Device ID: %s\n", deviceId.c_str());

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
    logger.setCallback(callbackLogMultiple);

    logger.info("Guess who's back, back again! EnergyMe - Home is starting up...", TAG);
    logger.info("Build version: %s | Build date: %s %s | Device ID: %s", TAG, FIRMWARE_BUILD_VERSION, FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME, deviceId.c_str());

    logger.debug("Setting up crash monitor...", TAG);
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

    TRACE();
    logger.debug("Checking for missing files...", TAG);
    auto missingFiles = checkMissingFiles();
    if (!missingFiles.empty()) {
        led.setOrange();
        logger.info("Missing files detected (first setup? Welcome to EnergyMe - Home!!!). Creating default files for missing files...", TAG);

        TRACE();
        createDefaultFilesForMissingFiles(missingFiles);

        logger.info("Default files created for missing files", TAG);
    } else {
        logger.info("No missing files detected", TAG);
    }

    TRACE();
    logger.debug("Fetching general configuration from SPIFFS...", TAG);
    if (!setGeneralConfigurationFromSpiffs()) {
        logger.warning("Failed to load configuration from SPIFFS. Using default values.", TAG);
    } else {
        logger.info("Configuration loaded from SPIFFS", TAG);
    }

    led.setPurple();
    
    TRACE();
    logger.debug("Setting up multiplexer...", TAG);
    multiplexer.begin();
    logger.info("Multiplexer setup done", TAG);

    TRACE();
    logger.debug("Creating meter reading task...", TAG);
    ade7953InterruptSemaphore = xSemaphoreCreateBinary();
    if (ade7953InterruptSemaphore == NULL) {
        logger.error("Failed to create ADE7953 interrupt semaphore", TAG);
    } else {
        logger.debug("ADE7953 interrupt semaphore created successfully", TAG);
    }
    
    payloadMeterMutex = xSemaphoreCreateMutex();
    if (payloadMeterMutex == NULL) {
        logger.error("Failed to create payloadMeter mutex", TAG);
    } else {
        logger.debug("PayloadMeter mutex created successfully", TAG);
    }

    TRACE();
    logger.debug("Setting up ADE7953...", TAG);
    attachInterrupt(digitalPinToInterrupt(ADE7953_INTERRUPT_PIN), ade7953ISR, FALLING); // This has to be done before ade7953.begin() for some mysterious reason
    if (!ade7953.begin()) {
      logger.fatal("ADE7953 initialization failed! This is a big issue mate..", TAG);
    } else {
      xTaskCreate(
          meterReadingTask,          // Task function
          ADE7953_TASK_NAME,         // Task name
          ADE7953_TASK_STACK_SIZE,   // Stack size (bytes)
          NULL,                      // Task parameter
          ADE7953_TASK_PRIORITY,     // Priority (higher than normal)
          &meterReadingTaskHandle    // Task handle
      );
      if (meterReadingTaskHandle != NULL) {
          logger.info("Meter reading task created successfully", TAG);
      } else {
          logger.error("Failed to create meter reading task", TAG);
      }
      ade7953.handleInterrupt(); // Clear any bit that was set to ensure we are now ready to handle the interrupts coming
      logger.info("ADE7953 setup done", TAG);
    }

    led.setBlue();    
    
    TRACE();
    logger.debug("Setting up WiFi...", TAG);
    customWifi.begin();
    logger.info("WiFi setup done", TAG);

    // Add UDP logging setup after WiFi
    TRACE();
    logger.debug("Setting up UDP logging...", TAG);
    if (WiFi.status() == WL_CONNECTED) {
        broadcastIP = WiFi.localIP();
        broadcastIP[3] = 255; // Convert to broadcast address
        udpClient.begin(UDP_LOG_PORT);
        logger.info("UDP broadcast logging configured for %s:%d", TAG, broadcastIP.toString().c_str(), UDP_LOG_PORT);
    } else {
        logger.warning("UDP logging setup skipped - WiFi not connected", TAG);
    }

    TRACE();
    logger.debug("Setting up button handler...", TAG);
    buttonHandler.begin();
    logger.info("Button handler setup done", TAG);

    TRACE();
    logger.debug("Syncing time...", TAG);
    updateTimezone();
    if (!customTime.begin()) {
      logger.error("Initial time sync failed! Will retry later.", TAG);
    } else {
      logger.info("Time synced. We're on time pal!", TAG);
    }
    
    TRACE();
    logger.debug("Setting up server...", TAG);
    customServer.begin();
    logger.info("Server setup done", TAG);

    TRACE();
    logger.debug("Setting up Modbus TCP...", TAG);
    modbusTcp.begin();
    logger.info("Modbus TCP setup done", TAG);

    led.setGreen();

    // Generate the device ID for the callback (pre-allocation makes everything quicker)
    TRACE();
    snprintf(
      baseMqttTopicLogs, 
      sizeof(baseMqttTopicLogs), 
      "%s/%s/%s/%s", 
      MQTT_TOPIC_1, 
      MQTT_TOPIC_2, 
      deviceId.c_str(), 
      MQTT_TOPIC_LOG
    );
    logger.debug("Base MQTT topic for logs: %s", TAG, baseMqttTopicLogs);    

    TRACE();
    logger.info("Setup done! Let's get this energetic party started!", TAG);
}

void loop() {
    TRACE();
    if (mainFlags.blockLoop) return;

    // Main loop now only handles non-critical operations
    // Meter reading is handled by dedicated task
    TRACE();
    customTime.loop();

    TRACE();
    crashMonitor.crashCounterLoop();

    TRACE();
    crashMonitor.firmwareTestingLoop();
      
    TRACE();
    customWifi.loop();
    
    TRACE();
    buttonHandler.loop();
    
    TRACE();
    mqtt.loop();
    
    TRACE();
    customMqtt.loop();
    
    TRACE();
    influxDbClient.loop();

    TRACE();
    ade7953.loop();

    TRACE();
    if (millis() - lastMaintenanceCheck >= MAINTENANCE_CHECK_INTERVAL) {      
      TRACE();
      lastMaintenanceCheck = millis();      
      logger.debug("Total interrupts received: %lu | Total handled: %lu",
        TAG, 
        statistics.ade7953TotalInterrupts,
        statistics.ade7953TotalHandledInterrupts
      );

      TRACE();
      printDeviceStatus();

      TRACE();
      if(ESP.getFreeHeap() < MINIMUM_FREE_HEAP_SIZE){
        logger.fatal("Heap memory has degraded below safe minimum: %d bytes", TAG, ESP.getFreeHeap());
        setRestartEsp32(TAG, "Heap memory has degraded below safe minimum");
      }

      // If memory is below a certain level, clear the log
      TRACE();
      if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < MINIMUM_FREE_SPIFFS_SIZE) {
        logger.clearLog();
        logger.warning("Log cleared due to low memory", TAG);
      }
      
      TRACE();
      if (debugFlagsRtc.enableMqttDebugLogging && millis() >= debugFlagsRtc.mqttDebugLoggingEndTimeMillis) {
        logger.info("MQTT debug logging period ended.", TAG);

        debugFlagsRtc.enableMqttDebugLogging = false;
        debugFlagsRtc.mqttDebugLoggingDurationMillis = 0;
        debugFlagsRtc.mqttDebugLoggingEndTimeMillis = 0;
        debugFlagsRtc.signature = 0; 
      }
    }

    TRACE();
    checkIfRestartEsp32Required();
}