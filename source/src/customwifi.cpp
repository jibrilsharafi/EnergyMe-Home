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
  // Logs show "Idle Status" (WL_IDLE_STATUS) when IP is 0.0.0.0, where WiFi.isConnected() would be false.
  if (!WiFi.isConnected() || WiFi.localIP() == IPAddress(0,0,0,0)) {
    _logger.warning("WiFi connection lost or IP not assigned. Current status: %d. Attempting reconnection...", TAG, WiFi.status());
    _led.setBlue();
    
    // Try simple reconnect first
    if (WiFi.reconnect()) { // This attempts to reconnect using the last known credentials.
      _logger.info("WiFi.reconnect() call initiated. Waiting for connection and IP address...", TAG);
      
      unsigned long _reconnectWaitStart = millis();
      bool _fullyConnected = false;
      // Wait for a short period for the connection to establish and an IP to be assigned.
      while (millis() - _reconnectWaitStart < 5000) { // Wait up to 5 seconds
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
          _logger.info("Successfully reconnected to WiFi: %s, IP: %s", TAG, WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
          setupMdns();
          printWifiStatus(); // Should now show correct IP and status
          _failedConnectionAttempts = 0; // Reset counter on successful full reconnection
          _lastConnectionAttemptTime = millis(); // Update timestamp for stable connection tracking
          _fullyConnected = true;
          _led.unblock(); // Unblock LED after successful connection
          break;
        }
        delay(250); // Check status periodically
      }

      if (!_fullyConnected) {
        _logger.warning("WiFi.reconnect() initiated but connection not fully established (no IP or not WL_CONNECTED) within timeout. Current status: %d, IP: %s. Falling back to _connectToWifi().", TAG, WiFi.status(), WiFi.localIP().toString().c_str());
        _connectToWifi(); // Fallback to the more robust WiFiManager method
      }
    } else {
      // If WiFi.reconnect() itself returns false (e.g., no credentials, immediate failure)
      _logger.warning("WiFi.reconnect() call failed immediately. Using _connectToWifi().", TAG);
      _connectToWifi(); // Fallback to the more robust WiFiManager method
    }
  }
}

bool CustomWifi::_connectToWifi() // FIXME: if a reset occurs, we still get _portalWasExpected to true, leading to no restert and incorrect webserver binding
{
  _logger.debug("Connecting to WiFi (using WiFiManager)...", TAG);

  _led.block();
  _led.setBlue(true);
  
  _lastConnectionAttemptTime = millis(); // Record the start of this connection attempt
  _failedConnectionAttempts++;

  // Check if credentials exist *before* calling autoConnect.
  bool _portalWasExpected = !(_wifiManager.getWiFiIsSaved());
  if (_portalWasExpected) {
    _logger.info("No saved credentials found. WiFiManager portal may start.", TAG);
  } else {
     _logger.info("Attempting connection with saved credentials", TAG);
  }

  // This will block until connected or portal times out/exits
  String hostname = WIFI_CONFIG_PORTAL_SSID " - " + getDeviceId();
  bool success = _wifiManager.autoConnect(hostname.c_str());

  if (success) {
    _logger.info("Connected to WiFi: %s", TAG, WiFi.SSID().c_str());
    
    // Reset failed attempts counter on success
    _failedConnectionAttempts = 0;
    _lastConnectionAttemptTime = millis(); // Update timestamp on successful connection

    if (_portalWasExpected) {
        // If the portal was expected and connection succeeded, 
        // it means new credentials were entered via the portal.
        // Restart the device to ensure the portal server is fully closed 
        // and the main server can bind to port 80 cleanly.
        _logger.warning("New WiFi credentials saved via portal. Restarting device...", TAG);
        _led.setCyan(true); // Indicate successful save before restart
        delay(1000); // Short delay to allow log message to potentially send
        ESP.restart();
        // Code execution stops here due to restart
    } else {
        // Connected using existing credentials, proceed normally
        setupMdns();
        printWifiStatus();
        _led.unblock();
        return true;
    }
  } else {
    // Connection or portal failed/timed out
    _led.setRed(true);
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

void CustomWifi::getWifiStatus(JsonDocument &jsonDocument)
{
  jsonDocument["macAddress"] = WiFi.macAddress();
  jsonDocument["localIp"] = WiFi.localIP().toString();
  jsonDocument["subnetMask"] = WiFi.subnetMask().toString();
  jsonDocument["gatewayIp"] = WiFi.gatewayIP().toString();
  jsonDocument["dnsIp"] = WiFi.dnsIP().toString();
  wl_status_t _status = WiFi.status();
  jsonDocument["status"] = WL_NO_SHIELD == _status ? "No Shield Available" : WL_IDLE_STATUS == _status   ? "Idle Status"
                                                                         : WL_NO_SSID_AVAIL == _status   ? "No SSID Available"
                                                                         : WL_SCAN_COMPLETED == _status  ? "Scan Completed"
                                                                         : WL_CONNECTED == _status       ? "Connected"
                                                                         : WL_CONNECT_FAILED == _status  ? "Connection Failed"
                                                                         : WL_CONNECTION_LOST == _status ? "Connection Lost"
                                                                         : WL_DISCONNECTED == _status    ? "Disconnected"
                                                                                                         : "Unknown Status";
  jsonDocument["ssid"] = WiFi.SSID();
  jsonDocument["bssid"] = WiFi.BSSIDstr();
  jsonDocument["rssi"] = WiFi.RSSI();
}

void CustomWifi::printWifiStatus()
{
  JsonDocument _jsonDocument;
  getWifiStatus(_jsonDocument);

  _logger.info(
      "MAC: %s | IP: %s | Status: %s | SSID: %s | RSSI: %s",
      TAG,
      _jsonDocument["macAddress"].as<String>().c_str(),
      _jsonDocument["localIp"].as<String>().c_str(),
      _jsonDocument["status"].as<String>().c_str(),
      _jsonDocument["ssid"].as<String>().c_str(),
      _jsonDocument["rssi"].as<String>().c_str());
}