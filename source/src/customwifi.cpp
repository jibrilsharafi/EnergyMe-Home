#include "customwifi.h"

static const char *TAG = "customwifi";

namespace CustomWifi
{
  // Static variables - minimal state
  static WiFiManager _wifiManager;
  static TaskHandle_t _wifiTaskHandle = NULL;
  static uint64_t _lastReconnectAttempt = 0;
  static int32_t _reconnectAttempts = 0; // Increased every disconnection, reset on stable (few minutes) connection

  // WiFi event notification values for task communication
  static const uint32_t WIFI_EVENT_CONNECTED = 1;
  static const uint32_t WIFI_EVENT_GOT_IP = 2;
  static const uint32_t WIFI_EVENT_DISCONNECTED = 3;
  static const uint32_t WIFI_EVENT_SHUTDOWN = 4;

  // Task state management
  static bool _taskShouldRun = false;
  static bool _eventsEnabled = false;

  // Private helper functions
  static void _onWiFiEvent(WiFiEvent_t event);
  static void _wifiConnectionTask(void *parameter);
  static void _setupWiFiManager();
  static void _handleSuccessfulConnection();
  static bool _setupMdns();
  static void _cleanup();
  static void _startWifiTask();
  static void _stopWifiTask();

  bool begin()
  {
    if (_wifiTaskHandle != NULL)
    {
      logger.debug("WiFi task is already running", TAG);
      return true;
    }

    logger.debug("Starting WiFi...", TAG);

    // Configure WiFi for better authentication reliability
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Set WiFi mode explicitly and disable power saving to prevent handshake issues
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable WiFi sleep to prevent handshake timeouts

    // Setup WiFiManager with optimal settings
    _setupWiFiManager();

    // Start WiFi connection task
    _startWifiTask();
    
    return _wifiTaskHandle != NULL;
  }

  void stop()
  {
    _stopWifiTask();

    // Disconnect WiFi and clean up
    if (WiFi.isConnected())
    {
      logger.debug("Disconnecting WiFi...", TAG);
      WiFi.disconnect(true);
      delay(1000); // Allow time for disconnection
      _cleanup();
    }
  }

  bool isFullyConnected() // Also check IP to ensure full connectivity
  {
    // Check if WiFi is connected and has an IP address
    return WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }

  static void _setupWiFiManager()
  {
    logger.debug("Setting up the WiFiManager...", TAG);

    _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SECONDS);
    _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SECONDS);
    _wifiManager.setConnectRetries(WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS); // Let WiFiManager handle initial retries
    
    // Additional WiFi settings to improve handshake reliability
    _wifiManager.setCleanConnect(true);    // Clean previous connection attempts
    _wifiManager.setBreakAfterConfig(true); // Exit after successful config
    _wifiManager.setRemoveDuplicateAPs(true); // Remove duplicate AP entries

        // Callback when portal starts
    _wifiManager.setAPCallback([](WiFiManager *wm) {
                                logger.info("WiFi configuration portal started: %s", TAG, wm->getConfigPortalSSID().c_str());
                                Led::blinkBlueFast(Led::PRIO_MEDIUM);
                              });

    // Callback when config is saved
    _wifiManager.setSaveConfigCallback([]() {
            logger.info("WiFi credentials saved via portal - restarting...", TAG);
            Led::setPattern(
              LedPattern::BLINK_FAST,
              Led::Colors::CYAN,
              Led::PRIO_CRITICAL,
              3000ULL
            );
            // Maybe with some smart management we could avoid the restart..
            // But we know that a reboot always solves any issues, so we leave it here
            // to ensure we start fresh
            setRestartSystem(TAG, "Restart after WiFi config save");
          });

    logger.debug("WiFiManager set up", TAG);
  }

  static void _onWiFiEvent(WiFiEvent_t event)
  {
    // Safety check - only process events if we're supposed to be running
    if (!_eventsEnabled || !_taskShouldRun) {
      return;
    }

    // Additional safety check for task handle validity
    if (_wifiTaskHandle == NULL) {
      return;
    }

    // Here we cannot do ANYTHING to avoid issues. Only notify the task,
    // which will handle all operations in a safe context.
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_START:
      // Station started - no action needed
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      // Defer logging to task
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_CONNECTED, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      // Defer all operations to task - avoid any function calls that might log
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_GOT_IP, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // Notify task to handle fallback if needed
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_DISCONNECTED, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      // Auth mode changed - no immediate action needed
      break;

    default:
      // Forward unknown events to task for logging/debugging
      xTaskNotify(_wifiTaskHandle, (uint32_t)event, eSetValueWithOverwrite);
      break;
    }
  }

  static void _handleSuccessfulConnection()
  {
    _lastReconnectAttempt = 0;

    _setupMdns();
    printDeviceStatusDynamic();

    logger.info("WiFi fully connected and operational", TAG);
  }

  static void _wifiConnectionTask(void *parameter)
  {
    logger.debug("WiFi task started", TAG);
    uint32_t notificationValue;
    _taskShouldRun = true;

    // Initial connection attempt
    Led::pulseBlue(Led::PRIO_MEDIUM);
    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);

    Led::blinkBlueSlow(Led::PRIO_MEDIUM);
    
    // Try initial connection with retries for handshake timeouts
    logger.debug("Attempt WiFi connection", TAG);
      
    if (!_wifiManager.autoConnect(hostname)) {
      logger.warning("WiFi connection failed, exiting wifi task", TAG);
      Led::blinkRedFast(Led::PRIO_URGENT);
      _taskShouldRun = false;
      _cleanup();
      _wifiTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }

    Led::clearPattern(Led::PRIO_MEDIUM);
    
    // If we reach here, we are connected
    _handleSuccessfulConnection();

    // Setup WiFi event handling - Only after full connection as during setup would crash sometimes probably due to the notifications
    _eventsEnabled = true;
    WiFi.onEvent(_onWiFiEvent);

    // Main task loop - handles fallback scenarios and deferred logging
    while (_taskShouldRun)
    {
      // Wait for notification from event handler or timeout
      if (xTaskNotifyWait(0, ULONG_MAX, &notificationValue, pdMS_TO_TICKS(WIFI_PERIODIC_CHECK_INTERVAL)))
      {
        // Check if this is a stop notification (we use a special value for shutdown)
        if (notificationValue == WIFI_EVENT_SHUTDOWN)
        {
          _taskShouldRun = false;
          break;
        }

        // Handle deferred operations from WiFi events (safe context)
        switch (notificationValue)
        {
        case WIFI_EVENT_CONNECTED:
          statistics.wifiConnection++;
          logger.debug("WiFi connected to: %s", TAG, WiFi.SSID().c_str());
          continue; // No further action needed

        case WIFI_EVENT_GOT_IP:
          logger.debug("WiFi got IP: %s", TAG, WiFi.localIP().toString().c_str());
          // Handle successful connection operations safely in task context
          _handleSuccessfulConnection();
          continue; // No further action needed

        case WIFI_EVENT_DISCONNECTED:
          statistics.wifiConnectionError++;
          Led::blinkBlueSlow(Led::PRIO_MEDIUM);
          logger.warning("WiFi disconnected - auto-reconnect will handle", TAG);

          // Wait a bit for auto-reconnect (enabled by default) to work
          delay(WIFI_DISCONNECT_DELAY);

          // Check if still disconnected
          if (!isFullyConnected())
          {
            _reconnectAttempts++;
            _lastReconnectAttempt = millis64();

            logger.warning("Auto-reconnect failed, attempt %d", TAG, _reconnectAttempts);

            // After several failures, try WiFiManager as fallback
            if (_reconnectAttempts >= WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS)
            {
              logger.error("Multiple reconnection failures - starting portal", TAG);

              // Try WiFiManager portal
              // TODO: this eventually will need to be async or similar since we lose meter 
              // readings in the meanwhile (and infinite loop of portal - reboots)
              if (!_wifiManager.startConfigPortal(hostname))
              {
                logger.error("Portal failed - restarting device", TAG);
                Led::blinkRedFast(Led::PRIO_URGENT);
                setRestartSystem(TAG, "Restart after portal failure");
              }
              // If portal succeeds, device will restart automatically
            }
          }
          break;

        default:
          // Handle unknown WiFi events for debugging
          if (notificationValue >= 100) { // WiFi events are >= 100
            logger.debug("Unknown WiFi event received: %lu", TAG, notificationValue);
          } else {
            // Legacy notification or timeout - treat as disconnection check
            logger.debug("WiFi periodic check or timeout", TAG);
          }
          break;
        }
      }
      else
      {
        // Timeout occurred - perform periodic health check
        if (_taskShouldRun && isFullyConnected())
        {
          // Reset failure counter on sustained connection
          if (_reconnectAttempts > 0 && millis64() - _lastReconnectAttempt > WIFI_STABLE_CONNECTION_DURATION)
          {
            logger.debug("WiFi connection stable - resetting counters", TAG);
            _reconnectAttempts = 0;
          }
        }
      }
    }

    // Cleanup before task exit
    _cleanup();
    
    logger.debug("WiFi task stopping", TAG);
    _wifiTaskHandle = NULL;
    vTaskDelete(NULL);
  }

  void resetWifi()
  {
    logger.warning("Resetting WiFi credentials and restarting...", TAG);
    Led::blinkOrangeFast(Led::PRIO_CRITICAL);
    _wifiManager.resetSettings();
    setRestartSystem(TAG, "Restart after WiFi reset");
  }

  bool _setupMdns()
  {
    logger.debug("Setting up mDNS...", TAG);

    // Ensure mDNS is stopped before starting
    MDNS.end();
    delay(100);

    // I would like to check for same MDNS_HOSTNAME on the newtork, but it seems that
    // I cannot do this with consistency. Let's just start the mDNS on the network and
    // hope for no other devices with the same name.

    if (MDNS.begin(MDNS_HOSTNAME) &&
        MDNS.addService("http", "tcp", WEBSERVER_PORT) &&
        MDNS.addService("modbus", "tcp", MODBUS_TCP_PORT))
    {
      // Add standard service discovery information
      MDNS.addServiceTxt("http", "tcp", "device_id", static_cast<const char *>(DEVICE_ID));
      MDNS.addServiceTxt("http", "tcp", "vendor", COMPANY_NAME);
      MDNS.addServiceTxt("http", "tcp", "model", PRODUCT_NAME);
      MDNS.addServiceTxt("http", "tcp", "version", FIRMWARE_BUILD_VERSION);
      MDNS.addServiceTxt("http", "tcp", "path", "/");
      MDNS.addServiceTxt("http", "tcp", "auth", "required");
      MDNS.addServiceTxt("http", "tcp", "ssl", "false");
      
      // Modbus service information
      MDNS.addServiceTxt("modbus", "tcp", "device_id", static_cast<const char *>(DEVICE_ID));
      MDNS.addServiceTxt("modbus", "tcp", "vendor", COMPANY_NAME);
      MDNS.addServiceTxt("modbus", "tcp", "model", PRODUCT_NAME);
      MDNS.addServiceTxt("modbus", "tcp", "version", FIRMWARE_BUILD_VERSION);
      MDNS.addServiceTxt("modbus", "tcp", "channels", "17");

      logger.info("mDNS setup done: %s.local", TAG, MDNS_HOSTNAME);
      return true;
    }
    else
    {
      logger.warning("Error setting up mDNS", TAG);
      return false;
    }
  }

  static void _cleanup()
  {
    logger.debug("Cleaning up WiFi resources...", TAG);
    
    // Disable event handling first
    _eventsEnabled = false;
    
    // Remove WiFi event handler to prevent crashes during shutdown
    WiFi.removeEvent(_onWiFiEvent);
    
    // Stop mDNS
    MDNS.end();
    
    logger.debug("WiFi cleanup completed", TAG);
  }

  static void _startWifiTask()
  {
    if (_wifiTaskHandle == NULL)
    {
      logger.debug("Starting WiFi task", TAG);
      BaseType_t result = xTaskCreate(
        _wifiConnectionTask,
        WIFI_TASK_NAME,
        WIFI_TASK_STACK_SIZE,
        NULL,
        WIFI_TASK_PRIORITY,
        &_wifiTaskHandle);

      if (result != pdPASS) { logger.error("Failed to create WiFi task", TAG); }
    }
    else
    {
      logger.debug("WiFi task is already running", TAG);
    }
  }

  static void _stopWifiTask()
  {
    if (_wifiTaskHandle == NULL)
    {
      logger.debug("WiFi task was not running", TAG);
      return;
    }

    logger.debug("Stopping WiFi task", TAG);

    // Send shutdown notification using the special shutdown event (cannot use standard stopTaskGracefully)
    xTaskNotify(_wifiTaskHandle, WIFI_EVENT_SHUTDOWN, eSetValueWithOverwrite);

    // Wait with timeout for clean shutdown using standard pattern
    uint64_t startTime = millis64();
    
    while (_wifiTaskHandle != NULL && (millis64() - startTime) < TASK_STOPPING_TIMEOUT) // TODO: use graceful here from utils?
    {
      delay(TASK_STOPPING_CHECK_INTERVAL);
    }

    // Force cleanup if needed
    if (_wifiTaskHandle != NULL)
    {
      logger.warning("Force stopping WiFi task after timeout", TAG);
      vTaskDelete(_wifiTaskHandle);
      _wifiTaskHandle = NULL;
    }
    else
    {
      logger.debug("WiFi task stopped gracefully", TAG);
    }

    WiFi.disconnect(true);
  }
}