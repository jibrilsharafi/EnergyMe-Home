#include "customwifi.h"

CustomWifi::CustomWifi(
    AdvancedLogger &logger, Led &led) : _logger(logger), _led(led) {}

bool CustomWifi::begin()
{
  _logger.debug("Setting up WiFi...", "customwifi::setupWifi");

  _wifiManager.setConfigPortalBlocking(false);
  _wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT);
  _wifiManager.setConfigPortalTimeoutCallback([this]() {
    _logger.warning("WiFi configuration portal timeout. Restarting ESP32 in AP mode", "customwifi::setupWifi");
    ESP.restart();
  });
  _connectToWifi();

  _logger.debug("WiFi setup done", "customwifi::setupWifi");
  return true;
}

void CustomWifi::loop()
{
  if (_wifiManager.getConfigPortalActive()) { // If it is still in the captive portal, we need to manually process the WiFiManager
    _led.block();
    _led.setBlue(true);
    _led.setBrightness(max(_led.getBrightness(), 1));
    
    // This outputs true when the user inputs the correct credentials
    if (_wifiManager.process()) {
      _logger.warning("WiFi connected. Restarting...", "customwifi::loop");
      ESP.restart();
    }
    
    _led.setOff(true);
    _led.unblock();
    return;
  }

  if (millis() - _lastMillisWifiLoop < WIFI_LOOP_INTERVAL) return;  

  _lastMillisWifiLoop = millis();
  if (WiFi.status() == WL_CONNECTED) return;

  _logger.warning("WiFi connection lost. Reconnecting...", "customwifi::wifiLoop");
  _connectToWifi();
}

bool CustomWifi::_connectToWifi()
{
  _logger.info("Connecting to WiFi...", "customwifi::_connectToWifi");

  _led.block();
  _led.setBlue(true);
  if (_wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID)) {
    _logger.info("Connected to WiFi", "customwifi::_connectToWifi");
    setupMdns();
    _led.unblock();
    return true;
  } else {
    _logger.info("WiFi captive portal set up", "customwifi::_connectToWifi");
    return false;
  }
}

void CustomWifi::resetWifi()
{
  _logger.warning("Erasing WiFi credentials and restarting...", "resetWifi");

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
      "customwifi::printWifiStatus",
      _jsonDocument["macAddress"].as<String>().c_str(),
      _jsonDocument["localIp"].as<String>().c_str(),
      _jsonDocument["status"].as<String>().c_str(),
      _jsonDocument["ssid"].as<String>().c_str(),
      _jsonDocument["rssi"].as<String>().c_str());
}
