// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include <Arduino.h>
#include <AdvancedLogger.h>
#include <LittleFS.h>
#include <driver/gpio.h>
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
#include <hal/usb_serial_jtag_ll.h>
#endif

// Project includes
// Initialization before everything
#include "constants.h"
#include "structs.h"
#include "utils.h"
#include "hardware_profile.h"

#include "ade7953.h"
#include "buttonhandler.h"
#include "crashmonitor.h"
#include "customwifi.h"
#include "custometh.h"
#include "customnet.h"
#include "customserver.h"
#include "led.h"
#include "modbustcp.h"
#include "mqtt.h"
#include "custommqtt.h"
#include "influxdbclient.h"
#include "issueregistry.h"
#include "multiplexer.h"
#include "customlog.h"
#include "taskprofiler.h"

// Global variables
// --------------------

Statistics statistics; // Move both to utils and use getter to get and set them
char DEVICE_ID[DEVICE_ID_BUFFER_SIZE];

// gpio_install_isr_service() runs esp_intr_alloc() + a poisoned malloc on the 1024-byte ipcN
// stack (prebuilt sdkconfig, not tunable); an interrupt frame pushed at that depth trips the
// stack canary ("Stack canary watchpoint triggered (ipc1)", seen on the Pro bench while a host
// drained the USB backlog). Install once while the core is quiet, with the USB-Serial-JTAG IRQ
// masked at the peripheral. CPU interrupts must stay enabled: the IPC call blocks. This only
// lowers the probability (tick/IPI remain). Later attachInterrupt()/ETH.begin() see "already
// installed" and skip the IPC.
static esp_err_t installGpioIsrServiceEarly()
{
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  const uint32_t usbIntrEna = usb_serial_jtag_ll_get_intr_ena_status();
  usb_serial_jtag_ll_disable_intr_mask(usbIntrEna);
#endif
  const esp_err_t err = gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  usb_serial_jtag_ll_ena_intr_mask(usbIntrEna);
#endif
  return err;
}

#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
// Weak core hook: the first thing loopTask runs, BEFORE the core's pre-setup chip report
// (dev builds, CORE_DEBUG_LEVEL >= 4). Setting the TX timeout in setup() is too late for
// that report, hence here. Bench numbers: see SERIAL_TX_TIMEOUT_MS in constants.h.
uint64_t getArduinoSetupWaitTime_ms()
{
  Serial.setTxTimeoutMs(SERIAL_TX_TIMEOUT_MS);
  return 0;
}
#endif

static uint32_t taskStackMinFree(const char *taskName)
{
  TaskHandle_t handle = xTaskGetHandle(taskName);
  if (handle == NULL) return 0;
  return (uint32_t)uxTaskGetStackHighWaterMark(handle);
}

#ifdef ENV_DEV
// Dev-only boot heap ledger: internal free heap after each setup stage, so internal RAM
// can be attributed per module from the boot log (UDP logs carry the boot lines too).
static void logHeapLedger(const char *stage)
{
  LOG_DEBUG("Heap ledger | %s | internal free %lu | largest block %lu", stage,
            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
#else
static inline void logHeapLedger(const char *) {}
#endif

void setup()
{
  const esp_err_t gpioIsrErr = installGpioIsrServiceEarly();

  Serial.begin(SERIAL_BAUDRATE);
  Serial.printf(FULL_PRODUCT_NAME "\n____________________\n\n");
  Serial.println("Booting...");
  Serial.printf("Build version: %s\n", FIRMWARE_BUILD_VERSION);
  Serial.printf("Build date: %s %s\n", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);

  // Initialize global device ID
  getDeviceId(DEVICE_ID, sizeof(DEVICE_ID));
  Serial.printf("Device ID: %s\n", DEVICE_ID);

  // Must be first: reads eFuse, selects hardware profile (pins, voltage ratios, mux map),
  // sets globalHwProfile and globalCommunityMode before any hardware is initialized.
  initHardwareProfile();

  // Register per-core idle hooks before any task is created so the baseline is
  // measured against a still-quiet system. Failure here is non-fatal (CPU% will
  // report 0; heartbeats and stack info still work).
  TaskProfiler::begin();

  // Need to call this once and at begin to ensure PSRAM is used for all mbedtls (both for OTA and MQTT connection. and maybe InfluxDB)
  mbedtls_platform_set_calloc_free(ota_calloc_psram, ota_free_psram);

  Serial.println("Setting up LED...");
  Led::begin(globalHwProfile->ledRedPin, globalHwProfile->ledGreenPin, globalHwProfile->ledBluePin);
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
    ESP.restart(); // No reason to live if we cannot mount the filesystem
    return;
  }

  // Check for pending configuration restore (BEFORE services start = natural "blocked state")
  if (isNvsRestorePending()) {
    LOG_DEBUG("Pending configuration restore detected. Starting restore process...");
    performNvsRestore();
    LOG_INFO("Configuration restore process completed");
  }

  Led::setYellow(Led::PRIO_NORMAL);
  AdvancedLogger::begin(LOG_PATH);
  LOG_DEBUG("AdvancedLogger initialized with log path: %s", LOG_PATH);
  
  LOG_DEBUG("Setting up callbacks for AdvancedLogger...");
  AdvancedLogger::setCallback(CustomLog::callbackMultiple);
  // VERBOSE never goes to the callbacks (UDP, MQTT): at hundreds of lines per second it would only
  // fill the log queue. It can be read on the serial console at best, through the print level.
  AdvancedLogger::setCallbackLevel(LogLevel::DEBUG);
  LOG_DEBUG("Callbacks for AdvancedLogger set up successfully");
  logHeapLedger("led + littlefs + logger");

  LOG_INFO("Guess who's back, back again! " FULL_PRODUCT_NAME " is starting up...");
  LOG_INFO(
    "Build version: %s %s(MD5: %s) | Build date: %s %s | Device ID: %s", 
    FIRMWARE_BUILD_VERSION,
    #ifdef ENV_DEV 
    "(dev) ",
    #else
    "",
    #endif
    ESP.getSketchMD5().c_str(), 
    FIRMWARE_BUILD_DATE, 
    FIRMWARE_BUILD_TIME, 
    DEVICE_ID
  ); // When changing this, be careful as it is used as parsing method to validate firmware version and environment; always align with manufacturing repo
  
  Led::setOrange(Led::PRIO_NORMAL);
  LOG_DEBUG("Setting up crash monitor...");
  CrashMonitor::begin();
  LOG_DEBUG("Crash monitor setup done");
  logHeapLedger("crash monitor");
  LOG_DEBUG("GPIO ISR service: %s | ipc0/ipc1 stack min free: %lu/%lu bytes",
            esp_err_to_name(gpioIsrErr),
            (unsigned long)taskStackMinFree("ipc0"),
            (unsigned long)taskStackMinFree("ipc1"));

  printDeviceStatusStatic();

  Led::setPurple(Led::PRIO_NORMAL);
  LOG_DEBUG("Setting up multiplexer...");
  Multiplexer::begin(
      globalHwProfile->muxS0Pin,
      globalHwProfile->muxS1Pin,
      globalHwProfile->muxS2Pin,
      globalHwProfile->muxS3Pin);
  LOG_DEBUG("Multiplexer setup done");
  logHeapLedger("multiplexer");

  LOG_DEBUG("Setting up button handler...");
  ButtonHandler::begin(globalHwProfile->buttonPin);
  LOG_DEBUG("Button handler setup done");
  logHeapLedger("button");

  LOG_DEBUG("Setting up ADE7953...");
  if (
    Ade7953::begin(
      globalHwProfile->ade7953SsPin,
      globalHwProfile->ade7953SckPin,
      globalHwProfile->ade7953MisoPin,
      globalHwProfile->ade7953MosiPin,
      globalHwProfile->ade7953ResetPin,
      globalHwProfile->ade7953InterruptPin
    )
  ) LOG_DEBUG("ADE7953 setup done");
  else LOG_ERROR("ADE7953 initialization failed! This is a big issue mate..");
  logHeapLedger("ade7953");

  Led::setBlue(Led::PRIO_NORMAL);
  CustomEth::registerEvents(); // Ahead of the first WiFi event, see custometh.h. No-op without Ethernet
  LOG_DEBUG("Setting up WiFi...");
  CustomWifi::begin();
  LOG_DEBUG("WiFi setup done");
  logHeapLedger("wifi");

  // Callbacks registered BEFORE begin() so the callback array is complete before the
  // eth task exists - no writer/iterator race. On failover the established TLS/TCP
  // sessions are bound to the dead interface's address: drop them so they
  // reconnect on the new route, and resync NTP against a server reachable
  // through it. Harmless on Home (the callbacks never fire without Ethernet).
  CustomEth::onInterfaceChange([](InterfaceArbitration::Interface) { Mqtt::requestReconnect(); });
  CustomEth::onInterfaceChange([](InterfaceArbitration::Interface) { CustomMqtt::requestReconnect(); });
  CustomEth::onInterfaceChange([](InterfaceArbitration::Interface) { CustomTime::requestResync(); });

  // No-op on products without Ethernet. On Pro this brings up the W5500 and the
  // interface arbitration; a cabled device typically has a lease before the WiFi
  // association finishes, so the wait below clears on the wire.
  LOG_DEBUG("Setting up Ethernet...");
  if (CustomEth::begin()) LOG_DEBUG("Ethernet setup done");
  else LOG_ERROR("Ethernet initialization failed! Continuing on WiFi only");
  logHeapLedger("ethernet");

  // Wait until the device is reachable by SOMETHING: STA connected, or the SoftAP raised
  // and serving. Waiting on isFullyConnected() here would spin forever on a device with no
  // valid credentials, so CustomServer::begin() below would never run and provisioning
  // would be unreachable - the AP would be up with nothing listening on it (D7).
  // Safe only because the health check now gates on isNetworkServiceable() too; on
  // isFullyConnected() it would fail every 30 s with the AP up and restart the device.
  //
  // Bounded, because "neither interface" is a reachable state, not a transient one: if every
  // candidate subnet collides, _raiseAp() fails closed and there is no AP to wait for. An
  // unbounded wait there is the worst possible outcome - no STA, no AP, and no web server on
  // any interface, so nothing to diagnose or fix it with. Give up and carry on instead: every
  // downstream service gates on isFullyConnected() by itself, and starting the server anyway
  // means it is already listening the moment any interface appears.
  uint64_t networkWaitStartMs = millis64();
  while (!CustomNet::isNetworkServiceable() &&
         (millis64() - networkWaitStartMs) < SETUP_NETWORK_WAIT_TIMEOUT_MS)
  {
    LOG_DEBUG("Waiting for a network interface or SoftAP...");
    delay(1000);
  }

  if (!CustomNet::isNetworkServiceable())
  {
    LOG_ERROR("No station-side link and no SoftAP after %llu s - continuing boot anyway. The device is "
              "unreachable until one of them comes up; the network tasks keep retrying",
              (uint64_t)(SETUP_NETWORK_WAIT_TIMEOUT_MS / 1000ULL));
  }

  // Add custom logging setup after WiFi
  LOG_DEBUG("Setting up custom logging...");
  CustomLog::begin();
  LOG_DEBUG("Custom logging setup done");
  logHeapLedger("network wait + custom logging");

  LOG_DEBUG("Syncing time...");
  if (CustomTime::begin()) LOG_INFO("Initial time sync successful");
  else LOG_ERROR("Initial time sync failed! Will retry later.");
  logHeapLedger("time");

  // Before the web server: the /api/v1/system/issues endpoint reads the registry
  // mutex, so the registry must exist before requests can arrive (else early polls
  // hit a NULL mutex -> HTTP 500). Deps are ready here: hw profile (initHardwareProfile)
  // and ADE7953 (channel facts) init earlier; time is up for issue timestamps. The
  // task's cloud/influx checks read safe default flags until those modules begin().
  LOG_DEBUG("Setting up issue registry...");
  IssueRegistry::begin();
  LOG_DEBUG("Issue registry setup done");
  logHeapLedger("issue registry");

  LOG_DEBUG("Setting up server...");
  CustomServer::begin();
  LOG_DEBUG("Server setup done");
  logHeapLedger("web server");

  // Only once there is a station link. Modbus TCP is unauthenticated and binds every
  // interface, so starting it on an AP-only boot would serve meter data to anyone in radio
  // range of the provisioning SoftAP. The health-check task starts it when STA comes up.
  LOG_DEBUG("Setting up Modbus TCP...");
  ModbusTcp::syncWithNetwork(CustomNet::isFullyConnected(), CustomWifi::isApServing());
  LOG_DEBUG("Modbus TCP setup done");
  logHeapLedger("modbus tcp");

  if (!globalCommunityMode) {
    LOG_DEBUG("Setting up MQTT client...");
    Mqtt::begin();
    LOG_DEBUG("MQTT client setup done");
  }

  LOG_DEBUG("Setting up Custom MQTT client...");
  CustomMqtt::begin();
  LOG_DEBUG("Custom MQTT client setup done");
  logHeapLedger("cloud mqtt + custom mqtt");

  LOG_DEBUG("Setting up InfluxDB client...");
  InfluxDbClient::begin();
  LOG_DEBUG("InfluxDB client setup done");
  logHeapLedger("influxdb");

  LOG_DEBUG("Starting maintenance task...");
  startMaintenanceTask();
  LOG_DEBUG("Maintenance task started");
  logHeapLedger("maintenance");

  // Visual indicator for safe mode (restart protection active)
  if (CrashMonitor::isInSafeMode()) {
    Led::setPurple(Led::PRIO_CRITICAL); // Purple = safe mode (restart protection)
  } else {
    Led::setGreen(Led::PRIO_NORMAL);
  }
  
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