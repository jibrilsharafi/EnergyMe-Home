#include "customwifi.h"

static const char *TAG = "customwifi";

namespace CustomWifi
{
  // Static variables - minimal state
  static WiFiManager _wifiManager;
  static TaskHandle_t _wifiTaskHandle = NULL;
  static unsigned long long _lastReconnectAttempt = 0;
  static int _reconnectAttempts = 0; // Increased every disconnection, reset on stable (few minutes) connection

  // WiFi event notification values for task communication
  static const unsigned long WIFI_EVENT_CONNECTED = 1;
  static const unsigned long WIFI_EVENT_GOT_IP = 2;
  static const unsigned long WIFI_EVENT_DISCONNECTED = 3;
  static const unsigned long WIFI_EVENT_SHUTDOWN = 4;

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

  bool begin()
  {
    if (_wifiTaskHandle != NULL)
    {
      logger.debug("WiFi task is already running", TAG);
      return true;
    }

    logger.debug("Starting WiFi task", TAG);

    // Enable auto-reconnect and persistence
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // Setup WiFiManager with optimal settings
    _setupWiFiManager();

    // Create WiFi connection task
    BaseType_t result = xTaskCreate(_wifiConnectionTask, WIFI_TASK_NAME, WIFI_TASK_STACK_SIZE, NULL, WIFI_TASK_PRIORITY, &_wifiTaskHandle);
    
    if (result != pdPASS) { 
      logger.error("Failed to create WiFi task", TAG);
      return false;
    }

    return true;
  }

  bool isFullyConnected() // Also check IP to ensure full connectivity
  {
    // Check if WiFi is connected and has an IP address
    return WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }

  static void _setupWiFiManager()
  {
    logger.debug("Setting up the WiFiManager...", TAG);

    _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
    _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
    _wifiManager.setConnectRetries(WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS); // Let WiFiManager handle initial retries

        // Callback when portal starts
    _wifiManager.setAPCallback([](WiFiManager *wm) {
                                logger.info("WiFi configuration portal started: %s", TAG, wm->getConfigPortalSSID().c_str());
                                Led::blinkBlueFast(); 
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
      xTaskNotify(_wifiTaskHandle, (unsigned long)event, eSetValueWithOverwrite);
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
    unsigned long notificationValue;
    _taskShouldRun = true;

    // Initial connection attempt
    Led::pulseBlue(Led::PRIO_MEDIUM);
    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);

    Led::blinkBlueFast(Led::PRIO_MEDIUM);
    if (!_wifiManager.autoConnect(hostname))
    {
      logger.error("Initial WiFi connection failed - restarting", TAG);
      Led::doubleBlinkYellow(Led::PRIO_CRITICAL);
      delay(1000);
      // Do not wait for any further operations, since the WiFi is critical and this is the first connection attempt
      ESP.restart();
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

          // Wait a bit for auto-reconnect to work
          delay(5000);

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
                logger.fatal("Portal failed - restarting device", TAG);
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

      // Periodic health check (only if we should still be running)
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

    if (
        MDNS.begin(MDNS_HOSTNAME) &&
        MDNS.addService("http", "tcp", WEBSERVER_PORT) &&
        MDNS.addService("modbus", "tcp", MODBUS_TCP_PORT))
    {
      logger.info("mDNS setup done", TAG);
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

  void stop()
  {
    logger.debug("Stopping WiFi task", TAG);
    
    if (_wifiTaskHandle != NULL) {
      // Send shutdown notification using the special shutdown event
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_SHUTDOWN, eSetValueWithOverwrite);
      
      // Wait with timeout for clean shutdown
      int timeout = 2000; // 2 seconds
      while (_wifiTaskHandle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout -= 10;
      }
      
      // Force cleanup if needed
      if (_wifiTaskHandle != NULL) {
        logger.warning("Force stopping WiFi task", TAG);
        vTaskDelete(_wifiTaskHandle);
        _wifiTaskHandle = NULL;
      } else {
        logger.debug("WiFi task stopped successfully", TAG);
      }
    } else {
      logger.debug("WiFi task was not running", TAG);
    }
  }
}