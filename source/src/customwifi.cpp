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

  // Diagnostic info for fallback portal
  static char _lastAttemptedSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static uint8_t _lastDisconnectReason = 0;
  static char _lastDisconnectSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static char _lastDisconnectBSSID[MAC_ADDRESS_BUFFER_SIZE] = {0};
  static int8_t _lastDisconnectRSSI = 0;

  // Private helper functions
  static void _onWiFiEvent(WiFiEvent_t event);
  static void _onWiFiEventWithInfo(WiFiEvent_t event, WiFiEventInfo_t info);
  static const char* _getDisconnectReasonString(uint8_t reason);
  static void _wifiConnectionTask(void *parameter);
  static void _setupWiFiManager(WiFiManager &wifiManager);
  static void _setupDiagnosticEndpoint(WiFiManager &wifiManager);
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

    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_HOSTNAME_PREFIX, DEVICE_ID);
    WiFi.setHostname(hostname); // Allow for easier identification in the router/network client list
    // TODO: add these functionalities (via config) to allow for channel and power settings
    // WiFi.setChannel(6);       // Optional - if user configured a specific channel
    // WiFi.setTxPower(WIFI_POWER_20dBm);  // Optional - regional compliance

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

    // Setup diagnostic endpoint for troubleshooting during fallback
    _setupDiagnosticEndpoint(wifiManager);

    LOG_DEBUG("WiFiManager set up");
  }

  static void _setupDiagnosticEndpoint(WiFiManager& wifiManager)
  {
    // Add diagnostic endpoint accessible during config portal fallback
    // This uses WiFiManager's synchronous WebServer (not AsyncWebServer)
    wifiManager.setWebServerCallback([&wifiManager]() {
      wifiManager.server->on("/diagnostic", HTTP_GET, [&wifiManager]() {
        // TODO: can we avoid this terrible mess of writing an HTML page like this?
        // TODO: can we have API calls here to show "real time data"?
        String page = F("<!DOCTYPE html><html><head>");
        page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        page += F("<title>EnergyMe Diagnostic</title>");
        // Use WiFiManager-compatible light theme
        page += F("<style>");
        page += F("body{font-family:Verdana,sans-serif;margin:0;padding:20px;background:#f5f5f5;color:#333}");
        page += F("h1{color:#1fa3ec;border-bottom:2px solid #1fa3ec;padding-bottom:10px;margin-top:0}");
        page += F("h2{color:#1fa3ec;margin:10px 0}");
        page += F(".section{background:#fff;padding:15px;margin:15px 0;border-radius:4px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}");
        page += F("pre{background:#1a1a2e;color:#eee;padding:10px;overflow-x:auto;font-size:11px;max-height:300px;overflow-y:auto;border-radius:4px;white-space:pre-wrap;word-wrap:break-word}");
        page += F(".info{display:grid;grid-template-columns:1fr 1fr;gap:8px}");
        page += F(".info-item{background:#f9f9f9;padding:8px;border-radius:4px;border:1px solid #eee}");
        page += F(".label{color:#888;font-size:11px;text-transform:uppercase}.value{font-weight:bold;color:#1fa3ec;word-break:break-all}");
        page += F("button{background:#1fa3ec;color:#fff;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;font-size:14px}");
        page += F("button:hover{background:#0e7ac4}");
        page += F(".warn{color:#d9534f}");
        page += F("</style>");
        page += F("<script>");
        page += F("function downloadLogs(){");
        page += F("const logs=document.getElementById('logs-content').innerText;");
        page += F("const blob=new Blob([logs],{type:'text/plain'});");
        page += F("const url=URL.createObjectURL(blob);");
        page += F("const a=document.createElement('a');");
        page += F("a.href=url;");
        page += F("a.download='energyme_diagnostic_'+new Date().toISOString().slice(0,10)+'.log';");
        page += F("document.body.appendChild(a);");
        page += F("a.click();");
        page += F("document.body.removeChild(a);");
        page += F("URL.revokeObjectURL(url);");
        page += F("}");
        page += F("</script>");
        page += F("</head><body>");
        
        page += F("<h1>&#128295; EnergyMe Diagnostic</h1>");
        
        // System Information Section
        // --------------------------
        page += F("<div class='section'><h2>System Information</h2><div class='info'>");
        
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s", FIRMWARE_BUILD_VERSION);
        page += F("<div class='info-item'><div class='label'>Firmware</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");

        snprintf(buffer, sizeof(buffer), "%s", ESP.getSketchMD5().c_str());
        page += F("<div class='info-item'><div class='label'>Sketch MD5</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");

        snprintf(buffer, sizeof(buffer), "%s %s", FIRMWARE_BUILD_DATE, FIRMWARE_BUILD_TIME);
        page += F("<div class='info-item'><div class='label'>Build Time</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        snprintf(buffer, sizeof(buffer), "%s", DEVICE_ID);
        page += F("<div class='info-item'><div class='label'>Device ID</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        snprintf(buffer, sizeof(buffer), "%lu bytes", ESP.getFreeHeap());
        page += F("<div class='info-item'><div class='label'>Free Heap</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        snprintf(buffer, sizeof(buffer), "%lu bytes", ESP.getFreePsram());
        page += F("<div class='info-item'><div class='label'>Free PSRAM</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        uint64_t uptimeMs = millis64();
        uint32_t uptimeSec = static_cast<uint32_t>(uptimeMs / 1000);
        uint32_t hours = uptimeSec / 3600;
        uint32_t minutes = (uptimeSec % 3600) / 60;
        uint32_t seconds = uptimeSec % 60;
        snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
        page += F("<div class='info-item'><div class='label'>Uptime</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        snprintf(buffer, sizeof(buffer), "%s", getResetReasonString(esp_reset_reason()));
        page += F("<div class='info-item'><div class='label'>Last Reset Reason</div><div class='value'>");
        page += buffer;
        page += F("</div></div>");
        
        page += F("</div></div>");
        
        // WiFi Information Section
        // ------------------------
        page += F("<div class='section'><h2>WiFi Status</h2><div class='info'>");
        
        page += F("<div class='info-item'><div class='label'>Connection Status</div><div class='value'>");
        page += wifiManager.getWLStatusString();
        page += F("</div></div>");
        
        page += F("<div class='info-item'><div class='label'>Saved SSID</div><div class='value'>");
        page += wifiManager.getWiFiSSID(true);
        page += F("</div></div>");
        
        page += F("<div class='info-item'><div class='label'>Last Attempted SSID</div><div class='value'>");
        page += (strlen(_lastAttemptedSSID) > 0) ? _lastAttemptedSSID : "(none)";
        page += F("</div></div>");
        
        // Show last disconnect reason captured during connection attempt
        page += F("<div class='info-item'><div class='label'>Disconnect Reason</div><div class='value'>");
        if (_lastDisconnectReason != 0) {
          snprintf(buffer, sizeof(buffer), "<span class='warn'>%d (%s)</span>", 
                   _lastDisconnectReason, _getDisconnectReasonString(_lastDisconnectReason));
          page += buffer;
        } else {
          page += "(no disconnect recorded)";
        }
        page += F("</div></div>");
        
        // Show SSID from disconnect event (what the ESP was trying to connect to)
        page += F("<div class='info-item'><div class='label'>Disconnect SSID</div><div class='value'>");
        page += (strlen(_lastDisconnectSSID) > 0) ? _lastDisconnectSSID : "(none)";
        page += F("</div></div>");
        
        // Show BSSID from disconnect event
        page += F("<div class='info-item'><div class='label'>Disconnect BSSID</div><div class='value'>");
        if (strlen(_lastDisconnectBSSID) > 0) {
          page += _lastDisconnectBSSID;
        } else {
          page += "(none)";
        }
        page += F("</div></div>");
        
        // Show RSSI at disconnect time
        page += F("<div class='info-item'><div class='label'>Disconnect RSSI</div><div class='value'>");
        if (_lastDisconnectReason != 0) {
          snprintf(buffer, sizeof(buffer), "%d dBm", _lastDisconnectRSSI);
          page += buffer;
        } else {
          page += "(none)";
        }
        page += F("</div></div>");
        
        // Get MAC address from ESP chip (works even in AP mode)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        page += F("<div class='info-item'><div class='label'>Device MAC</div><div class='value'>");
        snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        page += buffer;
        page += F("</div></div>");
        
        page += F("</div></div>");
        
        // Recent Logs Section
        page += F("<div class='section'><h2>Recent Logs</h2>");
        page += F("<div style='margin-bottom:10px'>");
        page += F("<button onclick='downloadLogs()'>⬇Download Logs</button>");
        page += F("</div>");
        page += F("<pre id='logs-content'>");
        
        if (LittleFS.exists(LOG_PATH)) {
          File logFile = LittleFS.open(LOG_PATH, "r");
          if (logFile) {
            size_t fileSize = logFile.size();
            // Read last 4KB of logs to avoid memory issues
            const size_t maxLogSize = MAX_LOG_SIZE_DIAGNOSTIC_FALLBACK_PAGE;
            if (fileSize > maxLogSize) {
              logFile.seek(fileSize - maxLogSize);
              // Skip to next newline to avoid partial line
              while (logFile.available() && logFile.read() != '\n') {}
            }
            while (logFile.available()) {
              int c = logFile.read();
              if (c < 0) break; // EOF or error
              // Escape HTML special characters
              if (c == '<') page += "&lt;";
              else if (c == '>') page += "&gt;";
              else if (c == '&') page += "&amp;";
              else page += static_cast<char>(c);
            }
            logFile.close();
          } else {
            page += F("(Could not open log file)");
          }
        } else {
          page += F("(No log file found)");
        }
        
        page += F("</pre></div>");
        
        // Navigation
        page += F("<div style='text-align:center;margin-top:20px'>");
        page += F("<a href='/'><button>&#8592; Back to WiFi Setup</button></a>");
        page += F("</div>");
        
        page += F("</body></html>");
        
        wifiManager.server->send(200, "text/html", page);
      });
    });

    // Add diagnostic button to the WiFiManager menu
    const char* menu[] = {"wifi", "info", "custom", "sep", "update", "exit"};
    wifiManager.setMenu(menu, 6);
    wifiManager.setCustomMenuHTML("<form action='/diagnostic' method='get'><button type='submit'>&#128295; Diagnostic</button></form>");
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

  // Early event handler to capture disconnect reason during initial connection
  // This runs BEFORE autoConnect so we can capture why connection failed
  static void _onWiFiEventWithInfo(WiFiEvent_t event, WiFiEventInfo_t info)
  {
    // DO NOT USE ANY LOGGING HERE to avoid weird crashes (this is a callback.. I don't know why but it seems unsafe)
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      _lastDisconnectReason = info.wifi_sta_disconnected.reason;
      _lastDisconnectRSSI = info.wifi_sta_disconnected.rssi;
      snprintf(_lastDisconnectSSID, sizeof(_lastDisconnectSSID), "%s", info.wifi_sta_disconnected.ssid);
      snprintf((char*)_lastDisconnectBSSID, sizeof(_lastDisconnectBSSID), "%02X:%02X:%02X:%02X:%02X:%02X",
               info.wifi_sta_disconnected.bssid[0], info.wifi_sta_disconnected.bssid[1],
               info.wifi_sta_disconnected.bssid[2], info.wifi_sta_disconnected.bssid[3],
               info.wifi_sta_disconnected.bssid[4], info.wifi_sta_disconnected.bssid[5]);
    }
  }

  static const char* _getDisconnectReasonString(uint8_t reason)
  {
    switch (reason) {
      case 1:   return "UNSPECIFIED";
      case 2:   return "AUTH_EXPIRE";
      case 3:   return "AUTH_LEAVE";
      case 4:   return "DISASSOC_INACTIVITY";
      case 5:   return "ASSOC_TOOMANY";
      case 6:   return "CLASS2_FRAME_NONAUTH";
      case 7:   return "CLASS3_FRAME_NONASSOC";
      case 8:   return "ASSOC_LEAVE";
      case 9:   return "ASSOC_NOT_AUTHED";
      case 10:  return "DISASSOC_PWRCAP_BAD";
      case 11:  return "DISASSOC_SUPCHAN_BAD";
      case 12:  return "BSS_TRANSITION";
      case 13:  return "IE_INVALID";
      case 14:  return "MIC_FAILURE";
      case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
      case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
      case 17:  return "IE_IN_4WAY_DIFFERS";
      case 18:  return "GROUP_CIPHER_INVALID";
      case 19:  return "PAIRWISE_CIPHER_INVALID";
      case 20:  return "AKMP_INVALID";
      case 21:  return "UNSUPP_RSN_IE_VERSION";
      case 22:  return "INVALID_RSN_IE_CAP";
      case 23:  return "802_1X_AUTH_FAILED";
      case 24:  return "CIPHER_SUITE_REJECTED";
      case 200: return "BEACON_TIMEOUT";
      case 201: return "NO_AP_FOUND";
      case 202: return "AUTH_FAIL";
      case 203: return "ASSOC_FAIL";
      case 204: return "HANDSHAKE_TIMEOUT";
      case 205: return "CONNECTION_FAIL";
      case 206: return "AP_TSF_RESET";
      case 207: return "ROAMING";
      case 208: return "ASSOC_COMEBACK_TIME_TOO_LONG";
      case 209: return "SA_QUERY_TIMEOUT";
      case 210: return "NO_AP_COMPATIBLE_SECURITY";
      case 211: return "NO_AP_AUTHMODE_THRESHOLD";
      case 212: return "NO_AP_RSSI_THRESHOLD";
      default:  return "UNKNOWN";
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

    // Store the saved SSID for diagnostic purposes before attempting connection
    String savedSSID = wifiManager->getWiFiSSID(true);
    if (savedSSID.length() > 0) {
      snprintf(_lastAttemptedSSID, sizeof(_lastAttemptedSSID), "%s", savedSSID.c_str());
    }

    // Register early event handler to capture disconnect reason during initial connection
    // This must be BEFORE autoConnect so we can capture why connection fails
    WiFi.onEvent(_onWiFiEventWithInfo);

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
      LOG_WARNING("Connectivity test failed early: WiFi not connected");
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
      LOG_DEBUG("Connectivity test failed: cannot reach %s:%d (no internet)", 
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