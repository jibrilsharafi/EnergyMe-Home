#include "customwifi.h"

static const char *TAG = "customwifi";

namespace CustomWifi
{
  // Static variables - minimal state
  static WiFiManager _wifiManager;
  static TaskHandle_t _wifiTaskHandle = NULL;
  static bool _isInitialConnection = true;
  static unsigned long _lastReconnectAttempt = 0;
  static int _reconnectAttempts = 0;

  // WiFi event notification values for task communication
  static const uint32_t WIFI_EVENT_CONNECTED = 1;
  static const uint32_t WIFI_EVENT_GOT_IP = 2;
  static const uint32_t WIFI_EVENT_DISCONNECTED = 3;

  // Private helper functions
  static void _onWiFiEvent(WiFiEvent_t event);
  static void _wifiConnectionTask(void *parameter);
  static void _setupWiFiManager();
  static void _handleSuccessfulConnection();
  static bool _setupMdns();
  static WifiInfo _getWifiInfo();

  bool begin()
  {
    logger.info("Setting up WiFi with event-driven architecture...", TAG);

    // Setup WiFi event handling
    WiFi.onEvent(_onWiFiEvent);

    // Enable auto-reconnect and persistence
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    // Setup WiFiManager with optimal settings
    _setupWiFiManager();

    // Create WiFi connection task
    xTaskCreate(_wifiConnectionTask, WIFI_TASK_NAME, WIFI_TASK_STACK_SIZE, NULL, WIFI_TASK_PRIORITY, &_wifiTaskHandle);

    logger.info("WiFi setup complete - event-driven mode enabled", TAG);
    return true;
  }

  bool isFullyConnected() // Also check IP to ensure full connectivity
  {
    // Check if WiFi is connected and has an IP address
    return WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  }

  static void _setupWiFiManager()
  {
    _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
    _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
    _wifiManager.setConnectRetries(WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS); // Let WiFiManager handle initial retries

    // Callback when portal starts
    _wifiManager.setAPCallback([](WiFiManager *wm)
                               {
                                 logger.info("WiFi configuration portal started: %s", TAG, wm->getConfigPortalSSID().c_str());
                                 Led::block();
                                 Led::setPurple(true); // Distinct color for portal mode
                               });

    // Callback when config is saved
    _wifiManager.setSaveConfigCallback([]()
                                       {
            logger.warning("WiFi credentials saved via portal - restarting...", TAG);
            Led::setCyan(true);
            delay(1000);
            ESP.restart(); });
  }

  static void _onWiFiEvent(WiFiEvent_t event)
  {
    // Keep WiFi event handlers minimal to avoid stack overflow
    // All logging is deferred to the WiFi task
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_START:
      // Station started - no action needed
      break;

    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Led::setBlue();
      _reconnectAttempts = 0;
      // Defer logging to task
      if (_wifiTaskHandle)
        xTaskNotify(_wifiTaskHandle, WIFI_EVENT_CONNECTED, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      // Defer all operations to task - avoid any function calls that might log
      if (_wifiTaskHandle)
        xTaskNotify(_wifiTaskHandle, WIFI_EVENT_GOT_IP, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Led::setBlue();
      statistics.wifiConnectionError++;

      // Notify task to handle fallback if needed
      if (_wifiTaskHandle)
        xTaskNotify(_wifiTaskHandle, WIFI_EVENT_DISCONNECTED, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      // Auth mode changed - no immediate action needed
      break;

    default:
      break;
    }
  }

  static void _handleSuccessfulConnection()
  {
    _isInitialConnection = false;
    _reconnectAttempts = 0;
    _lastReconnectAttempt = 0;

    // Setup mDNS
    _setupMdns();

    // Print connection info
    printWifiInfo();

    // LED indication
    Led::unblock();
    Led::setGreen();

    logger.info("WiFi fully connected and operational", TAG);
  }

  static void _wifiConnectionTask(void *parameter)
  {
    uint32_t notificationValue;

    // Initial connection attempt
    Led::setBlue();
    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);

    if (!_wifiManager.autoConnect(hostname))
    {
      logger.error("Initial WiFi connection failed - restarting", TAG);
      Led::setRed(true);
      delay(2000);
      ESP.restart();
    }

    // Main task loop - handles fallback scenarios and deferred logging
    while (true)
    {
      // Wait for notification from event handler or timeout
      if (xTaskNotifyWait(0, ULONG_MAX, &notificationValue, pdMS_TO_TICKS(30000)))
      {
        // Handle deferred operations from WiFi events (safe context)
        // NOTE: Logging temporarily disabled due to AdvancedLogger stack issues
        switch (notificationValue)
        {
        case WIFI_EVENT_CONNECTED:
          logger.info("WiFi connected to: %s", TAG, WiFi.SSID().c_str());
          continue; // No further action needed

        case WIFI_EVENT_GOT_IP:
          logger.info("WiFi got IP: %s", TAG, WiFi.localIP().toString().c_str());
          // Handle successful connection operations safely in task context
          _handleSuccessfulConnection();
          continue; // No further action needed

        case WIFI_EVENT_DISCONNECTED:
          logger.warning("WiFi disconnected - auto-reconnect will handle", TAG);
          // Fall through to handle disconnection
          break;

        default:
          // Legacy notification or timeout - treat as disconnection check
          break;
        }

        // Handle disconnection (only for WIFI_EVENT_DISCONNECTED or legacy notifications)
        if (notificationValue == WIFI_EVENT_DISCONNECTED || notificationValue == 1)
        {
          // Wait a bit for auto-reconnect to work
          vTaskDelay(pdMS_TO_TICKS(5000));

          // Check if still disconnected
          if (!WiFi.isConnected())
          {
            _reconnectAttempts++;
            _lastReconnectAttempt = millis();

            logger.warning("Auto-reconnect failed, attempt %d", TAG, _reconnectAttempts);
            Led::setOrange();

            // After several failures, try WiFiManager as fallback
            if (_reconnectAttempts >= WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS)
            {
              logger.error("Multiple reconnection failures - starting portal", TAG);

              // Try WiFiManager portal
              Led::block();
              Led::setPurple(true);

              if (!_wifiManager.startConfigPortal(hostname))
              {
                logger.fatal("Portal failed - restarting device", TAG);
                Led::setRed(true);
                delay(2000);
                ESP.restart();
              }
              // If portal succeeds, device will restart automatically
            }
          }
        }
      }

      // Periodic health check (every 30 seconds)
      if (WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0))
      {
        // Reset failure counter on sustained connection
        if (_isInitialConnection || _reconnectAttempts > 0)
        {
          logger.debug("WiFi connection stable - resetting counters", TAG);
          _reconnectAttempts = 0;
        }
      }
    }

    vTaskDelete(NULL);
  }

  void resetWifi()
  {
    logger.warning("Resetting WiFi credentials and restarting...", TAG);
    Led::setRed(true);
    _wifiManager.resetSettings();
    delay(1000);
    ESP.restart();
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
        MDNS.addService("mqtt", "tcp", MQTT_CUSTOM_PORT_DEFAULT) &&
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

  WifiInfo _getWifiInfo()
  {
    WifiInfo wifiInfo = {};

    snprintf(wifiInfo.macAddress, sizeof(wifiInfo.macAddress), "%s", WiFi.macAddress().c_str());
    snprintf(wifiInfo.localIp, sizeof(wifiInfo.localIp), "%s", WiFi.localIP().toString().c_str());
    snprintf(wifiInfo.subnetMask, sizeof(wifiInfo.subnetMask), "%s", WiFi.subnetMask().toString().c_str());
    snprintf(wifiInfo.gatewayIp, sizeof(wifiInfo.gatewayIp), "%s", WiFi.gatewayIP().toString().c_str());
    snprintf(wifiInfo.dnsIp, sizeof(wifiInfo.dnsIp), "%s", WiFi.dnsIP().toString().c_str());

    // Simplified status mapping
    wl_status_t status = WiFi.status();
    const char *statusStr = (status == WL_CONNECTED)         ? "Connected"
                            : (status == WL_DISCONNECTED)    ? "Disconnected"
                            : (status == WL_CONNECTION_LOST) ? "Connection Lost"
                            : (status == WL_CONNECT_FAILED)  ? "Connection Failed"
                            : (status == WL_IDLE_STATUS)     ? "Idle"
                                                             : "Unknown";
    snprintf(wifiInfo.status, sizeof(wifiInfo.status), "%s", statusStr);

    snprintf(wifiInfo.ssid, sizeof(wifiInfo.ssid), "%s", WiFi.SSID().c_str());
    snprintf(wifiInfo.bssid, sizeof(wifiInfo.bssid), "%s", WiFi.BSSIDstr().c_str());
    wifiInfo.rssi = WiFi.RSSI();

    return wifiInfo;
  }

  void getWifiInfoJson(JsonDocument &jsonDocument)
  {
    WifiInfo wifiInfo = _getWifiInfo();

    jsonDocument["macAddress"] = wifiInfo.macAddress;
    jsonDocument["localIp"] = wifiInfo.localIp;
    jsonDocument["subnetMask"] = wifiInfo.subnetMask;
    jsonDocument["gatewayIp"] = wifiInfo.gatewayIp;
    jsonDocument["dnsIp"] = wifiInfo.dnsIp;
    jsonDocument["status"] = wifiInfo.status;
    jsonDocument["ssid"] = wifiInfo.ssid;
    jsonDocument["bssid"] = wifiInfo.bssid;
    jsonDocument["rssi"] = wifiInfo.rssi;
  }

  void printWifiInfo()
  {
    WifiInfo wifiInfo = _getWifiInfo();
    logger.info("WiFi - SSID: %s | IP: %s | Status: %s | RSSI: %d dBm", TAG,
                wifiInfo.ssid, wifiInfo.localIp, wifiInfo.status, wifiInfo.rssi);
  }
}