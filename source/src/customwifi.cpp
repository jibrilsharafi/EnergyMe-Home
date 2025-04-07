#include "customwifi.h"

const char *TAG = "customwifi";

CustomWifi::CustomWifi(
    AdvancedLogger &logger, Led &led) : _logger(logger), _led(led) {}

bool CustomWifi::begin()
{
  _logger.debug("Setting up WiFi...", TAG);

  _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
  _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  
  // Connect to WiFi or start portal
  _connectToWifi();

  // If we get here, we're connected
  _led.unblock();
  _logger.debug("WiFi setup done", TAG);
  return true;
}

void CustomWifi::loop()
{
  // Only check periodically to reduce overhead
  if (millis() - _lastMillisWifiLoop < WIFI_LOOP_INTERVAL) return;  

  _lastMillisWifiLoop = millis();
  
  // If already connected, nothing to do
  if (WiFi.isConnected()) {
    // Reset counter when we have a stable connection
    if (_failedConnectionAttempts > 0 && 
        (millis() - _lastConnectionAttemptTime) > 300000) { // 5 minutes stable connection
      _failedConnectionAttempts = 0;
      _logger.debug("WiFi connection stable for 5 minutes, resetting failure counter", TAG);
    }
    return;
  }

  // If disconnected, attempt to reconnect
  _logger.warning("WiFi connection lost. Reconnecting...", TAG);
  _led.setBlue();
  
  // Try simple reconnect first
  if (!WiFi.reconnect()) {
    // If we could not directly reconnect with WiFi.reconnect, try with exponential backoff
    _connectToWifi();
  } else {
    _logger.info("Successfully reconnected to WiFi", TAG);
  }
}

bool CustomWifi::_connectToWifi()
{
  _logger.info("Connecting to WiFi...", TAG);

  _led.block();
  _led.setBlue(true);
  
  // Track connection attempt
  _lastConnectionAttemptTime = millis();
  _failedConnectionAttempts++;

  // This will block until connected or portal times out
  if (_wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID)) {
    _logger.info("Connected to WiFi: %s", TAG, WiFi.SSID().c_str());
    setupMdns();
    printWifiStatus();
    _led.unblock();
    
    // Reset failed attempts counter on success
    _failedConnectionAttempts = 0;
    return true;
  } else {
    _led.setRed(true);
    _logger.warning("Failed to connect to WiFi. Attempt %d of %d", TAG, 
                   _failedConnectionAttempts, WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS);
                   
    // Only restart if we've hit the maximum attempts
    if (_failedConnectionAttempts >= WIFI_MAX_CONSECUTIVE_RECONNECT_ATTEMPTS) {
      _logger.error("Maximum reconnection attempts reached. Restarting device...", TAG);
      
      setRestartEsp32(TAG, "Maximum WiFi reconnection attempts reached");
    } else {
      // Calculate backoff delay with exponential increase
      unsigned long backoffDelay = WIFI_RECONNECT_DELAY_BASE * (1 << (_failedConnectionAttempts - 1));
      _logger.info("Will retry connecting in %lu ms", TAG, backoffDelay);
      _led.unblock(); // Allow LED to show other states
      _led.setOrange();
      delay(backoffDelay);
    }
    
    return false;
  }
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