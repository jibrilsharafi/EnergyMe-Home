#include "customwifi.h"

const char *TAG = "customwifi";

CustomWifi::CustomWifi(
    AdvancedLogger &logger, Led &led) : _logger(logger), _led(led) {}

bool CustomWifi::begin()
{
  _logger.debug("Setting up WiFi...", TAG);

  // Configure WiFiManager for blocking mode
  _wifiManager.setConfigPortalBlocking(true);
  _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
  
  // Set timeout callback to restart the device
  _wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  _wifiManager.setAPCallback([this](WiFiManager *wifiManager) {
    _logger.info("WiFi configuration portal started", TAG);
    _led.block();
    _led.setBlue(true);
    _led.setBrightness(max(_led.getBrightness(), 1));
  });
  
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
  if (WiFi.isConnected()) return;

  // If disconnected, attempt to reconnect
  _logger.warning("WiFi connection lost. Reconnecting...", TAG);
  if (!WiFi.reconnect()) {
    _logger.error("Failed to reconnect to WiFi. Restarting...", TAG);
    delay(1000); // Give time for logs to be sent
    ESP.restart();
  }
}

bool CustomWifi::_connectToWifi()
{
  _logger.info("Connecting to WiFi...", TAG);

  _led.block();
  _led.setBlue(true);
  
  // This will block until connected or portal times out
  if (_wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID)) {
    _logger.info("Connected to WiFi: %s", TAG, WiFi.SSID().c_str());
    setupMdns();
    printWifiStatus();
    _led.unblock();
    return true;
  } else {
    _logger.warning("Failed to connect to WiFi. Restarting device...", TAG);
    delay(1000); // Give time for logs to be sent
    ESP.restart();
    return false; // This line won't be reached due to restart
  }
}

// The rest of the methods remain unchanged
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