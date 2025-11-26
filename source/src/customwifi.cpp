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
  static const uint32_t WIFI_EVENT_NEW_CREDENTIALS = 6;

  // Task state management
  static bool _taskShouldRun = false;
  static bool _eventsEnabled = false;
  
  // New credentials storage for async switching
  // To be safe, we should create mutexes around these since they are accessed via a public method
  // But since they are seldom written and read, we can avoid the complexity for now
  static char _pendingSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static char _pendingPassword[WIFI_PASSWORD_BUFFER_SIZE] = {0};
  static bool _hasPendingCredentials = false;


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
  static bool _isPowerReset();
  static void _sendOpenSourceTelemetry();
  static bool _telemetrySent = false; // Ensures telemetry is sent only once per boot

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

  bool isFullyConnected(bool requireInternet) // NOTE: as this can be quite "heavy", use it only where actually needed 
  {
    // Check if WiFi is connected and has an IP address (it can happen that WiFi is connected but no IP assigned)
    if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0, 0, 0, 0)) return false;

    // Ensure lwIP network stack has had time to stabilize after connection
    // This prevents DNS/UDP crashes when services try to connect too quickly
    if (_lastWifiConnectedMillis > 0 && (millis64() - _lastWifiConnectedMillis) < WIFI_LWIP_STABILIZATION_DELAY) return false;

    if (requireInternet) return _testConnectivity();
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

    // Check if this is a power reset - router likely rebooting
    bool isPowerReset = _isPowerReset();
    uint32_t connectTimeout = isPowerReset ? WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS : WIFI_CONNECT_TIMEOUT_SECONDS;
    
    if (isPowerReset) {
      LOG_INFO("Power reset detected - using extended WiFi timeout (%d seconds) to allow router to reboot", connectTimeout);
    }

    wifiManager.setConnectTimeout(connectTimeout);
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
    _sendOpenSourceTelemetry(); // Non-blocking short POST (guarded by compile-time flag)
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

        case WIFI_EVENT_NEW_CREDENTIALS:
          if (_hasPendingCredentials)
          {
            LOG_INFO("Processing new WiFi credentials for SSID: %s", _pendingSSID);
            
            // Save new credentials to NVS using esp_wifi_set_config() directly
            // This stores credentials WITHOUT triggering a connection attempt,
            // which avoids heap corruption when restarting immediately after
            wifi_config_t wifi_config = {};
            snprintf((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", _pendingSSID);
            snprintf((char*)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", _pendingPassword);
            
            esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (err != ESP_OK) {
              LOG_ERROR("Failed to save WiFi credentials: %s", esp_err_to_name(err));
              memset(_pendingSSID, 0, sizeof(_pendingSSID));
              memset(_pendingPassword, 0, sizeof(_pendingPassword));
              _hasPendingCredentials = false;
              continue;
            }
            
            // Clear pending credentials from memory
            memset(_pendingSSID, 0, sizeof(_pendingSSID));
            memset(_pendingPassword, 0, sizeof(_pendingPassword));
            _hasPendingCredentials = false;
            
            // Request system restart to apply new WiFi credentials
            LOG_INFO("New credentials saved to NVS, restarting system");
            setRestartSystem("Restart to apply new WiFi credentials");
          }
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
        if (_taskShouldRun)
        {
          if (isFullyConnected())
          {   
            // Test internet connectivity but don't force reconnection if it fails
            // WiFi is connected to local network - internet may simply be unavailable
            // This allows the device to operate in local-only mode (static IP, isolated networks)
            if (!_testConnectivity()) {
              LOG_DEBUG("Internet connectivity unavailable - device operating in local-only mode");
            }
          
            // Reset failure counter on sustained connection
            if (_reconnectAttempts > 0 && millis64() - _lastReconnectAttempt > WIFI_STABLE_CONNECTION_DURATION)
            {
              LOG_DEBUG("WiFi connection stable - resetting counters");
              _reconnectAttempts = 0;
            }
          }
          else
          {
            // WiFi not connected or no IP - force reconnection
            LOG_WARNING("Periodic check: WiFi not fully connected - forcing reconnection");
            _forceReconnectInternal();
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

  bool setCredentials(const char* ssid, const char* password)
  {
    // Validate inputs
    if (!ssid || strlen(ssid) == 0)
    {
      LOG_ERROR("Invalid SSID provided");
      return false;
    }

    if (strlen(ssid) >= WIFI_SSID_BUFFER_SIZE)
    {
      LOG_ERROR("SSID exceeds maximum length of %d characters", WIFI_SSID_BUFFER_SIZE);
      return false;
    }

    if (!password)
    {
      LOG_ERROR("Password cannot be NULL");
      return false;
    }

    if (strlen(password) >= WIFI_PASSWORD_BUFFER_SIZE)
    {
      LOG_ERROR("Password exceeds maximum length of %d characters", WIFI_PASSWORD_BUFFER_SIZE);
      return false;
    }

    if (strlen(password) == 0)
    {
      LOG_WARNING("Empty password provided for SSID: %s (open network)", ssid);
    }

    // Check if WiFi task is running
    if (_wifiTaskHandle == NULL)
    {
      LOG_ERROR("Cannot set credentials - WiFi task not running");
      return false;
    }

    LOG_INFO("Queueing new WiFi credentials for SSID: %s", ssid);
    
    // Store credentials for WiFi task to process
    snprintf(_pendingSSID, sizeof(_pendingSSID), "%s", ssid);
    snprintf(_pendingPassword, sizeof(_pendingPassword), "%s", password);
    _hasPendingCredentials = true;
    
    // Notify WiFi task to process new credentials
    xTaskNotify(_wifiTaskHandle, WIFI_EVENT_NEW_CREDENTIALS, eSetValueWithOverwrite);
    
    // Return immediately - actual connection happens asynchronously in WiFi task
    // This prevents blocking the web server and avoids conflicts with event handlers
    return true;
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
    // Check basic WiFi connectivity (without internet test to avoid recursion)
    if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      LOG_DEBUG("Connectivity test failed early: WiFi not connected");
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
    // This should be true even before DNS resolution
    IPAddress dns1 = WiFi.dnsIP(0);
    IPAddress dns2 = WiFi.dnsIP(1);
    if (dns1 == IPAddress(0, 0, 0, 0) && dns2 == IPAddress(0, 0, 0, 0)) {
      LOG_WARNING("Connectivity test failed: no DNS servers available");
      statistics.wifiConnectionError++;
      return false;
    }

    // Simple TCP connect to Google Public DNS (8.8.8.8:53) - lightweight internet connectivity test
    // Uses IP address to avoid DNS lookup, port 53 is rarely blocked by firewalls
    WiFiClient client;
    client.setTimeout(CONNECTIVITY_TEST_TIMEOUT_MS);
    
    if (!client.connect(CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT)) {
      LOG_WARNING("Connectivity test failed: cannot reach %s:%d (no internet)", 
                  CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT);
      statistics.wifiConnectionError++;
      return false;
    }
    
    // Connection successful - internet is reachable
    client.stop();
    
    // Use char buffers to avoid dynamic string allocation in logs and potential crashes
    char gatewayStr[IP_ADDRESS_BUFFER_SIZE];
    snprintf(gatewayStr, sizeof(gatewayStr), "%d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
    char dns1Str[IP_ADDRESS_BUFFER_SIZE];
    snprintf(dns1Str, sizeof(dns1Str), "%d.%d.%d.%d", dns1[0], dns1[1], dns1[2], dns1[3]);
    LOG_DEBUG("Connectivity test passed - Gateway: %s, DNS: %s, Internet: %s:%d reachable", 
              gatewayStr,
              dns1Str,
              CONNECTIVITY_TEST_IP,
              CONNECTIVITY_TEST_PORT);
    return true;
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

  static bool _isPowerReset()
  {
    // Check if the reset reason indicates a power-related event
    // ESP_RST_POWERON: Power on reset (cold boot)
    // ESP_RST_BROWNOUT: Brownout reset (power supply voltage dropped below minimum)
    esp_reset_reason_t resetReason = esp_reset_reason();
    return resetReason == ESP_RST_POWERON || resetReason == ESP_RST_BROWNOUT;
  }

  static void _sendOpenSourceTelemetry()
  {
#ifdef ENABLE_OPEN_SOURCE_TELEMETRY
    if (_telemetrySent) return;

    // Basic preconditions: WiFi connected with IP
    if (!isFullyConnected(true)) {
      LOG_DEBUG("Skipping telemetry - WiFi not fully connected");
      return;
    }

    // Prepare JSON payload using PSRAM allocator
    SpiRamAllocator allocator;
    JsonDocument doc(&allocator);

    // Hash the device identifier (privacy: avoid sending raw ID)
    unsigned char hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, reinterpret_cast<const unsigned char*>(DEVICE_ID), strlen((const char*)DEVICE_ID));
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    
    char hashedDeviceId[65]; // 64 hex chars + null
    for (int i = 0; i < 32; i++) snprintf(&hashedDeviceId[i * 2], 3, "%02x", hash[i]);
    
    doc["device_id"] = hashedDeviceId; // Replace plain ID with hashed value
    doc["firmware_version"] = FIRMWARE_BUILD_VERSION;
    doc["sketch_md5"] = ESP.getSketchMD5();

    char jsonBuffer[TELEMETRY_JSON_BUFFER_SIZE];
    size_t jsonSize = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    if (jsonSize == 0 || jsonSize >= sizeof(jsonBuffer)) {
      LOG_WARNING("Telemetry JSON invalid or too large"); 
      return; 
    }

    WiFiClientSecure client;
    client.setTimeout(TELEMETRY_TIMEOUT_MS);
    client.setCACert(AWS_IOT_CORE_CA_CERT); // Use Amazon Root CA 1 for secure connection

    if (!client.connect(TELEMETRY_URL, TELEMETRY_PORT)) {
      LOG_WARNING("Telemetry connection failed");
      return;
    }

    // Build HTTP request headers
    char header[TELEMETRY_JSON_BUFFER_SIZE];
    int headerLen = snprintf(header, sizeof(header),
                             "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: EnergyMe-Home/%s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                             TELEMETRY_PATH,
                             TELEMETRY_URL,
                             FIRMWARE_BUILD_VERSION,
                             (unsigned)jsonSize);
    if (headerLen <= 0 || headerLen >= (int)sizeof(header)) {
      client.stop(); 
      LOG_WARNING("Telemetry header build failed"); 
      return; 
    }

    // Send request
    client.write(reinterpret_cast<const uint8_t*>(header), headerLen);
    client.write(reinterpret_cast<const uint8_t*>(jsonBuffer), jsonSize);

    // Lightweight response handling (discard data, avoid dynamic allocations)
    // While we could avoid this, it is safer to ensure the server closes the connection properly
    uint64_t start = millis64();
    while (client.connected() && (millis64() - start) < TELEMETRY_TIMEOUT_MS)
    {
      while (client.available()) client.read();
      delay(10);
    }
    client.stop();

    _telemetrySent = true; // Set to true regardless of success to avoid repeated attempts. This info is not critical.
    LOG_INFO("Open source telemetry sent");
#else
    LOG_DEBUG("Open source telemetry disabled (compile-time)");
#endif
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