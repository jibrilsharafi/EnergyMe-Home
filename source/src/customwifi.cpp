#include "customwifi.h"

static const char *TAG = "customwifi";

CustomWifi::CustomWifi(
    AdvancedLogger &logger, Led &led) : _logger(logger), _led(led) {}

bool CustomWifi::begin()
{
  _logger.debug("Setting up WiFi...", TAG);

  _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
  _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  // _wifiManager.setHttpPort(WIFI_CONFIG_PORTAL_PORT); // Keep portal on default port 80

  // Connect to WiFi or start portal
  if (!_connectToWifi()) {
    // Handle the case where connection/portal failed critically after retries
    // Depending on _connectToWifi's logic, it might have already restarted.
    // If not, you might want to add a fallback or error state here.
    _logger.fatal("Initial WiFi connection failed after retries.", TAG);
    // Perhaps blink an error pattern or halt.
    // For now, we assume _connectToWifi handles restarts on persistent failure.
    return false; 
  }

  // If we get here, we're connected (either initially or after portal restart)
  _led.unblock();
  _logger.debug("WiFi setup done", TAG);
  return true;
}

void CustomWifi::loop()
{
  // Only check periodically to reduce overhead
  if (millis() - _lastMillisWifiLoop < WIFI_LOOP_INTERVAL) return;  
  _lastMillisWifiLoop = millis();
  
  // If already connected and IP is valid, reset failure counter and return
  if (WiFi.isConnected() && WiFi.localIP() != IPAddress(0,0,0,0)) {
    if (_failedConnectionAttempts > 0 && 
        (millis() - _lastConnectionAttemptTime) > WIFI_STABLE_CONNECTION) { 
      _failedConnectionAttempts = 0;
      _logger.debug("WiFi connection stable, resetting failure counter", TAG);
    }
    return;
  }

  // If disconnected or IP is not yet assigned.
  // WiFi.isConnected() checks if WiFi.status() == WL_CONNECTED.
  // Logs show "Idle Status" (WL_IDLE_STATUS) when IP is 0.0f.0.0f, where WiFi.isConnected() would be false.
  if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0,0,0,0)) {
    _logger.warning("WiFi connection lost or IP not assigned. Current status: %d. Attempting reconnection...", TAG, WiFi.status());
    _led.setBlue();
    
    // Try simple reconnect first
    if (WiFi.reconnect()) { // This attempts to reconnect using the last known credentials.
      _logger.info("WiFi.reconnect() call initiated. Waiting for connection and IP address...", TAG);
      
      unsigned long _reconnectWaitStart = millis();
      bool _fullyConnected = false;
      // Wait for a short period for the connection to establish and an IP to be assigned.

      unsigned int _loops = 0;
      while (millis() - _reconnectWaitStart < 5000 && _loops < MAX_LOOP_ITERATIONS) { // Wait up to 5 seconds
        _loops++;
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
          _logger.info("Successfully reconnected to WiFi: %s, IP: %s", TAG, WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
          setupMdns();
          printWifiInfo(); // Should now show correct IP and status
          _failedConnectionAttempts = 0; // Reset counter on successful full reconnection
          _lastConnectionAttemptTime = millis(); // Update timestamp for stable connection tracking
          _fullyConnected = true;
          _led.unblock(); // Unblock LED after successful connection
          statistics.wifiConnection++; // Increment successful connection counter
          break;
        }
        delay(250); // Check status periodically
      }

      if (!_fullyConnected) {
        _logger.warning("WiFi.reconnect() initiated but connection not fully established (no IP or not WL_CONNECTED) within timeout. Current status: %d, IP: %s. Falling back to _connectToWifi().", TAG, WiFi.status(), WiFi.localIP().toString().c_str());
        statistics.wifiConnectionError++; // Increment error counter
        _connectToWifi(); // Fallback to the more robust WiFiManager method
      }
    } else {
      // If WiFi.reconnect() itself returns false (e.g., no credentials, immediate failure)
      _logger.warning("WiFi.reconnect() call failed immediately. Using _connectToWifi().", TAG);
      statistics.wifiConnectionError++; // Increment error counter
      _connectToWifi(); // Fallback to the more robust WiFiManager method
    }
  }
}

bool CustomWifi::_connectToWifi()
{
  _logger.debug("Connecting to WiFi (using WiFiManager)...", TAG);

  _led.block();
  _led.setBlue(true);
  
  _lastConnectionAttemptTime = millis(); // Record the start of this connection attempt
  _failedConnectionAttempts++;
  
  // Reset portal tracking flag
  _portalWasStarted = false;
  
  // Set up callback to detect when portal starts
  _wifiManager.setAPCallback([this](WiFiManager* myWiFiManager) {
    _logger.info("WiFi configuration portal started", TAG);
    _portalWasStarted = true;
  });

  // This will block until connected or portal times out/exits
  char hostname[WIFI_SSID_BUFFER_SIZE];
  snprintf(hostname, sizeof(hostname), "%s - %s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);
  bool success = _wifiManager.autoConnect(hostname);

  if (success) {
    _logger.info("Connected to WiFi: %s", TAG, WiFi.SSID().c_str());
    
    // Reset failed attempts counter on success
    _failedConnectionAttempts = 0;
    _lastConnectionAttemptTime = millis(); // Update timestamp on successful connection
    statistics.wifiConnection++; // Increment successful connection counter

    if (_portalWasStarted) {
        // Portal was actually started and used during this session
        _logger.warning("WiFi credentials configured via portal. Restarting device...", TAG);
        _led.setCyan(true); // Indicate successful save before restart
        delay(1000); // Short delay to allow log message to potentially send
        ESP.restart();
        // Code execution stops here due to restart
    } else {
        // Connected using existing credentials without portal, proceed normally
        setupMdns();
        printWifiInfo();
        _led.unblock();
        return true;
    }
  } else {
    // Connection or portal failed/timed out
    _led.setRed(true);
    statistics.wifiConnectionError++; // Increment error counter
    _logger.warning("Failed to connect to WiFi or portal timed out. Attempt %d of %d", TAG, 
                   _failedConnectionAttempts, WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS);
                   
    // Only restart if we've hit the maximum *consecutive* attempts
    if (_failedConnectionAttempts >= WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS) {
      _logger.error("Maximum reconnection attempts reached. Restarting device...", TAG);
      setRestartEsp32(TAG, "Maximum WiFi reconnection attempts reached");
      // Restart is handled by setRestartEsp32 -> checkIfRestartEsp32Required in main loop
    } else {
      // Calculate backoff delay for next retry in loop() or next explicit call
      unsigned long backoffDelay = WIFI_RECONNECT_DELAY_BASE * (1 << (_failedConnectionAttempts - 1));
      _logger.info("Will retry connecting later (delay: %lu ms)", TAG, backoffDelay);
      // Note: The actual delay might happen implicitly before the next loop() check
      // or explicitly if _connectToWifi is called again immediately.
      // Consider adding a delay here if retries happen too quickly in your logic.
      // delay(backoffDelay); // Optional: uncomment if immediate retry is needed
    }
    _led.unblock(); // Allow LED to show other states (like Orange for retry pending)
    _led.setOrange(); 
    return false;
  }
  // Should not reach here due to restart or return statements above
  return false; 
}

void CustomWifi::resetWifi()
{
  _logger.warning("Erasing WiFi credentials and restarting...", TAG);
  _wifiManager.resetSettings();
  ESP.restart();
}

WifiInfo CustomWifi::getWifiInfo()
{
  WifiInfo wifiInfo;

  snprintf(wifiInfo.macAddress, sizeof(wifiInfo.macAddress), "%s", WiFi.macAddress().c_str());
  snprintf(wifiInfo.localIp, sizeof(wifiInfo.localIp), "%s", WiFi.localIP().toString().c_str());
  snprintf(wifiInfo.subnetMask, sizeof(wifiInfo.subnetMask), "%s", WiFi.subnetMask().toString().c_str());
  snprintf(wifiInfo.gatewayIp, sizeof(wifiInfo.gatewayIp), "%s", WiFi.gatewayIP().toString().c_str());
  snprintf(wifiInfo.dnsIp, sizeof(wifiInfo.dnsIp), "%s", WiFi.dnsIP().toString().c_str());

  wl_status_t status = WiFi.status();
  snprintf(wifiInfo.status, sizeof(wifiInfo.status), "%s", 
    (status == WL_NO_SHIELD) ? "No Shield Available"
    : (status == WL_IDLE_STATUS) ? "Idle Status"
    : (status == WL_NO_SSID_AVAIL) ? "No SSID Available"
    : (status == WL_SCAN_COMPLETED) ? "Scan Completed"
    : (status == WL_CONNECTED) ? "Connected"
    : (status == WL_CONNECT_FAILED) ? "Connection Failed"
    : (status == WL_CONNECTION_LOST) ? "Connection Lost"
    : (status == WL_DISCONNECTED) ? "Disconnected"
                                    : "Unknown Status");

  snprintf(wifiInfo.ssid, sizeof(wifiInfo.ssid), "%s", WiFi.SSID().c_str());
  snprintf(wifiInfo.bssid, sizeof(wifiInfo.bssid), "%s", WiFi.BSSIDstr().c_str());
  wifiInfo.rssi = WiFi.RSSI();

  return wifiInfo;
}

void CustomWifi::getWifiInfoJson(JsonDocument &jsonDocument)
{
  WifiInfo wifiInfo = getWifiInfo();

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

void CustomWifi::printWifiInfo()
{
  JsonDocument _jsonDocument;
  getWifiInfoJson(_jsonDocument);

  _logger.info(
      "MAC: %s | IP: %s | Status: %s | SSID: %s | RSSI: %d",
      TAG,
      _jsonDocument["macAddress"].as<const char*>(),
      _jsonDocument["localIp"].as<const char*>(),
      _jsonDocument["status"].as<const char*>(),
      _jsonDocument["ssid"].as<const char*>(),
      _jsonDocument["rssi"].as<int>()
    );
}