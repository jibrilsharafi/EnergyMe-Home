// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "customwifi.h"

namespace CustomWifi
{
  // WiFi connection task variables
  static TaskHandle_t _wifiTaskHandle = NULL;
  
  // Counters
  static uint64_t _lastReconnectAttempt = 0;
  static int32_t _reconnectAttempts = 0; // Increased every disconnection, reset on stable (few minutes) connection
  static uint64_t _lastWifiConnectedMillis = 0; // Timestamp when WiFi was last fully connected (for lwIP stabilization)

  // WiFi event notification values for task communication
  static const uint32_t WIFI_EVENT_CONNECTED = 1;
  static const uint32_t WIFI_EVENT_GOT_IP = 2;
  static const uint32_t WIFI_EVENT_DISCONNECTED = 3;
  static const uint32_t WIFI_EVENT_SHUTDOWN = 4;
  static const uint32_t WIFI_EVENT_FORCE_RECONNECT = 5;

  // Task state management
  static bool _taskShouldRun = false;
  static bool _eventsEnabled = false;


  // Private helper functions
  static void _onWiFiEvent(WiFiEvent_t event);
  static void _wifiConnectionTask(void *parameter);
  static void _setupWiFiManager(WiFiManager &wifiManager);
  static void _handleSuccessfulConnection();
  static bool _setupMdns();
  static void _cleanup();
  static void _startWifiTask();
  static void _stopWifiTask();
  static bool _testConnectivity();
  static void _forceReconnectInternal();

  bool begin()
  {
    if (_wifiTaskHandle != NULL)
    {
      LOG_DEBUG("WiFi task is already running");
      return true;
    }

    LOG_DEBUG("Starting WiFi...");

    // Configure WiFi for better authentication reliability
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Set WiFi mode explicitly and disable power saving to prevent handshake issues
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Disable WiFi sleep to prevent handshake timeouts

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
      LOG_DEBUG("Disconnecting WiFi...");
      WiFi.disconnect(true);
      delay(1000); // Allow time for disconnection
      _cleanup();
    }
  }

  bool isFullyConnected() // Also check IP to ensure full connectivity
  {
    // Check if WiFi is connected and has an IP address (it can happen that WiFi is connected but no IP assigned)
    if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0, 0, 0, 0)) return false;

    // Ensure lwIP network stack has had time to stabilize after connection
    // This prevents DNS/UDP crashes when services try to connect too quickly
    if (_lastWifiConnectedMillis > 0 && (millis64() - _lastWifiConnectedMillis) < WIFI_LWIP_STABILIZATION_DELAY) return false;

    return true;
  }

  bool testConnectivity()
  {
    return _testConnectivity();
  }

  void forceReconnect()
  {
    if (_wifiTaskHandle != NULL) {
      LOG_WARNING("Forcing WiFi reconnection...");
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_FORCE_RECONNECT, eSetValueWithOverwrite);
    } else {
      LOG_WARNING("Cannot force reconnect - WiFi task not running");
    }
  }

  static void _setupWiFiManager(WiFiManager& wifiManager)
  {
    LOG_DEBUG("Setting up the WiFiManager...");

    wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SECONDS);
    wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_SECONDS);
    wifiManager.setConnectRetries(WIFI_INITIAL_MAX_RECONNECT_ATTEMPTS); // Let WiFiManager handle initial retries
    
    // Additional WiFi settings to improve handshake reliability
    wifiManager.setCleanConnect(true);    // Clean previous connection attempts
    wifiManager.setBreakAfterConfig(true); // Exit after successful config
    wifiManager.setRemoveDuplicateAPs(true); // Remove duplicate AP entries

        // Callback when portal starts
    wifiManager.setAPCallback([](WiFiManager *wm) {
                                LOG_INFO("WiFi configuration portal started: %s", wm->getConfigPortalSSID().c_str());
                                Led::blinkBlueFast(Led::PRIO_MEDIUM);
                              });

    // Callback when config is saved
    wifiManager.setSaveConfigCallback([]() {
            LOG_INFO("WiFi credentials saved via portal - restarting...");
            Led::setPattern(
              LedPattern::BLINK_FAST,
              Led::Colors::CYAN,
              Led::PRIO_CRITICAL,
              3000ULL
            );
            // Maybe with some smart management we could avoid the restart..
            // But we know that a reboot always solves any issues, so we leave it here
            // to ensure we start fresh
            setRestartSystem("Restart after WiFi config save");
          });

    LOG_DEBUG("WiFiManager set up");
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
    // Note: printDeviceStatusDynamic() removed to avoid flash I/O from PSRAM task

    Led::clearPattern(Led::PRIO_MEDIUM); // Ensure that if we are connected again, we don't keep the blue pattern
    Led::setGreen(Led::PRIO_NORMAL); // Hack: to ensure we get back to green light, we set it here even though a proper LED manager would handle priorities better
    LOG_INFO("WiFi fully connected and operational");
  }

  static void _wifiConnectionTask(void *parameter)
  {
    LOG_DEBUG("WiFi task started");
    uint32_t notificationValue;
    _taskShouldRun = true;

    // Create WiFiManager on heap to save stack space
    WiFiManager* wifiManager = new WiFiManager();
    if (!wifiManager) {
      LOG_ERROR("Failed to allocate WiFiManager");
      _taskShouldRun = false;
      _cleanup();
      _wifiTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }
    _setupWiFiManager(*wifiManager);

    // Initial connection attempt
    Led::pulseBlue(Led::PRIO_MEDIUM);
    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);

    // Try initial connection with retries for handshake timeouts
    LOG_DEBUG("Attempt WiFi connection");
      
    if (!wifiManager->autoConnect(hostname)) { // HACK: actually handle this in such a way where we retry constantly, but without restarting the device. Closing the task has little utility
      LOG_WARNING("WiFi connection failed, exiting wifi task");
      Led::blinkRedFast(Led::PRIO_URGENT);
      _taskShouldRun = false;
      setRestartSystem("Restart after WiFi connection failure");
      _cleanup();
      delete wifiManager; // Clean up before exit
      _wifiTaskHandle = NULL;
      vTaskDelete(NULL);
      return;
    }

    // Clean up WiFiManager after successful connection - no longer needed
    delete wifiManager;
    wifiManager = nullptr;

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
          LOG_DEBUG("WiFi connected to: %s", WiFi.SSID().c_str());
          continue; // No further action needed

        case WIFI_EVENT_GOT_IP:
          LOG_DEBUG("WiFi got IP: %s", WiFi.localIP().toString().c_str());
          statistics.wifiConnection++; // It is here we know the wifi connection went through (and the one which is called on reconnections)
          _lastWifiConnectedMillis = millis64(); // Track connection time for lwIP stabilization
          // Handle successful connection operations safely in task context
          _handleSuccessfulConnection();
          continue; // No further action needed

        case WIFI_EVENT_FORCE_RECONNECT:
          _forceReconnectInternal();
          continue; // No further action needed

        case WIFI_EVENT_DISCONNECTED:
          statistics.wifiConnectionError++;
          Led::pulseBlue(Led::PRIO_MEDIUM);
          LOG_WARNING("WiFi disconnected - auto-reconnect will handle");
          _lastWifiConnectedMillis = 0; // Reset stabilization timer on disconnect

          // Wait a bit for auto-reconnect (enabled by default) to work
          delay(WIFI_DISCONNECT_DELAY);

          // Check if still disconnected
          if (!isFullyConnected())
          {
            _reconnectAttempts++;
            _lastReconnectAttempt = millis64();

            LOG_WARNING("Auto-reconnect failed, attempt %d", _reconnectAttempts);

            // After several failures, try WiFiManager as fallback
            if (_reconnectAttempts >= WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS)
            {
              LOG_ERROR("Multiple reconnection failures - starting portal");

              // Create WiFiManager on heap for portal operation
              WiFiManager* portalManager = new WiFiManager();
              if (!portalManager) {
                LOG_ERROR("Failed to allocate WiFiManager for portal");
                setRestartSystem("Restart after WiFiManager allocation failure");
                break;
              }
              _setupWiFiManager(*portalManager);

              // Try WiFiManager portal
              // TODO: this eventually will need to be async or similar since we lose meter 
              // readings in the meanwhile (and infinite loop of portal - reboots)
              if (!portalManager->startConfigPortal(hostname))
              {
                LOG_ERROR("Portal failed - restarting device");
                Led::blinkRedFast(Led::PRIO_URGENT);
                setRestartSystem("Restart after portal failure");
              }
              // Clean up WiFiManager after portal operation
              delete portalManager;
              // If portal succeeds, device will restart automatically
            }
          }
          break;

        default:
          // Handle unknown WiFi events for debugging
          if (notificationValue >= 100) { // WiFi events are >= 100
            LOG_DEBUG("Unknown WiFi event received: %lu", notificationValue);
          } else {
            // Legacy notification or timeout - treat as disconnection check
            LOG_DEBUG("WiFi periodic check or timeout");
          }
          break;
        }
      }
      else
      {
        // Timeout occurred - perform periodic health check
        if (_taskShouldRun && isFullyConnected())
        {   
          if (!_testConnectivity()) {
            LOG_WARNING("Connectivity test failed - forcing reconnection");
            _forceReconnectInternal();
          }
        
          // Reset failure counter on sustained connection
          if (_reconnectAttempts > 0 && millis64() - _lastReconnectAttempt > WIFI_STABLE_CONNECTION_DURATION)
          {
            LOG_DEBUG("WiFi connection stable - resetting counters");
            _reconnectAttempts = 0;
          }
        }
      }
    }

    // Cleanup before task exit
    _cleanup();
    
    LOG_DEBUG("WiFi task stopping");
    _wifiTaskHandle = NULL;
    vTaskDelete(NULL);
  }

  void resetWifi()
  {
    LOG_WARNING("Resetting WiFi credentials and restarting...");
    Led::blinkOrangeFast(Led::PRIO_CRITICAL);
    
    // Create WiFiManager on heap temporarily to reset settings
    WiFiManager* wifiManager = new WiFiManager();
    if (wifiManager) {
      wifiManager->resetSettings();
      delete wifiManager;
    }
    
    setRestartSystem("Restart after WiFi reset");
  }

  bool _setupMdns()
  {
    LOG_DEBUG("Setting up mDNS...");

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

      LOG_INFO("mDNS setup done: %s.local", MDNS_HOSTNAME);
      return true;
    }
    else
    {
      LOG_WARNING("Error setting up mDNS");
      return false;
    }
  }

  static void _cleanup()
  {
    LOG_DEBUG("Cleaning up WiFi resources...");
    
    // Disable event handling first
    _eventsEnabled = false;
    
    // Remove WiFi event handler to prevent crashes during shutdown
    WiFi.removeEvent(_onWiFiEvent);
    
    // Stop mDNS
    MDNS.end();
    
    LOG_DEBUG("WiFi cleanup completed");
  }

  static bool _testConnectivity()
  {
    if (!isFullyConnected()) {
      LOG_DEBUG("Connectivity test failed: not fully connected");
      return false;
    }

    // Check if we have a valid gateway IP
    IPAddress gateway = WiFi.gatewayIP();
    if (gateway == IPAddress(0, 0, 0, 0)) {
      LOG_WARNING("Connectivity test failed: no gateway IP available");
      statistics.wifiConnectionError++;
      return false;
    }

    // Check if we have valid DNS servers
    IPAddress dns1 = WiFi.dnsIP(0);
    IPAddress dns2 = WiFi.dnsIP(1);
    if (dns1 == IPAddress(0, 0, 0, 0) && dns2 == IPAddress(0, 0, 0, 0)) {
      LOG_WARNING("Connectivity test failed: no DNS servers available");
      statistics.wifiConnectionError++;
      return false;
    }

    // Try a simple HTTP request to test actual internet connectivity
    WiFiClient client;
    client.setTimeout(CONNECTIVITY_TEST_TIMEOUT_MS);
    
    if (!client.connect(CONNECTIVITY_TEST_HOST, CONNECTIVITY_TEST_PORT)) {
      LOG_WARNING("Connectivity test failed: cannot connect to %s", CONNECTIVITY_TEST_HOST);
      statistics.wifiConnectionError++;
      return false;
    }
    
    client.print("HEAD / HTTP/1.1\r\nHost: ");
    client.print(CONNECTIVITY_TEST_HOST);
    client.print("\r\nConnection: close\r\n\r\n");
    
    // Wait for response
    uint64_t startTime = millis64();
    while (client.connected() && (millis64() - startTime) < CONNECTIVITY_TEST_TIMEOUT_MS) {
      if (client.available()) {
        String response = client.readStringUntil('\n');
        client.stop();
        if (response.startsWith("HTTP/1.1")) {
          LOG_DEBUG("Connectivity test passed - Gateway: %s, DNS: %s, Host tested: %s:%d", 
                    gateway.toString().c_str(), 
                    dns1.toString().c_str(),
                    CONNECTIVITY_TEST_HOST,
                    CONNECTIVITY_TEST_PORT);
          return true;
        }
        break;
      }
      delay(10);
    }
    
    client.stop();
    LOG_WARNING("Connectivity test failed: no valid HTTP response from %s", CONNECTIVITY_TEST_HOST);
    statistics.wifiConnectionError++;
    return false;
  }

  static void _forceReconnectInternal()
  {
    LOG_WARNING("Performing forced WiFi reconnection...");
    
    // Disconnect and reconnect
    WiFi.disconnect(false); // Don't erase credentials
    delay(WIFI_FORCE_RECONNECT_DELAY);
    
    // Trigger reconnection
    WiFi.reconnect();
    
    _reconnectAttempts++;
    _lastReconnectAttempt = millis64();
    statistics.wifiConnectionError++;
    
    LOG_INFO("Forced reconnection initiated (attempt %d)", _reconnectAttempts);
  }

  static void _startWifiTask()
  {
    if (_wifiTaskHandle) { LOG_DEBUG("WiFi task is already running"); return; }
    LOG_DEBUG("Starting WiFi task with %d bytes stack in internal RAM (performs TCP network operations)", WIFI_TASK_STACK_SIZE);

    BaseType_t result = xTaskCreate(
        _wifiConnectionTask,
        WIFI_TASK_NAME,
        WIFI_TASK_STACK_SIZE,
        nullptr,
        WIFI_TASK_PRIORITY,
        &_wifiTaskHandle);
    if (result != pdPASS) { LOG_ERROR("Failed to create WiFi task"); }
  }

  static void _stopWifiTask()
  {
    if (_wifiTaskHandle == NULL)
    {
      LOG_DEBUG("WiFi task was not running");
      return;
    }

    LOG_DEBUG("Stopping WiFi task");

    // Send shutdown notification using the special shutdown event (cannot use standard stopTaskGracefully)
    xTaskNotify(_wifiTaskHandle, WIFI_EVENT_SHUTDOWN, eSetValueWithOverwrite);

    // Wait with timeout for clean shutdown using standard pattern
    uint64_t startTime = millis64();
    
    while (_wifiTaskHandle != NULL && (millis64() - startTime) < TASK_STOPPING_TIMEOUT)
    {
      delay(TASK_STOPPING_CHECK_INTERVAL);
    }

    // Force cleanup if needed
    if (_wifiTaskHandle != NULL)
    {
      LOG_WARNING("Force stopping WiFi task after timeout");
      vTaskDelete(_wifiTaskHandle);
      _wifiTaskHandle = NULL;
    }
    else
    {
      LOG_DEBUG("WiFi task stopped gracefully");
    }

    WiFi.disconnect(true);
  }

  TaskInfo getTaskInfo()
  {
    return getTaskInfoSafely(_wifiTaskHandle, WIFI_TASK_STACK_SIZE);
  }
}