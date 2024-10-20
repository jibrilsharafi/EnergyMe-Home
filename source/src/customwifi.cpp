#include "customwifi.h"

CustomWifi::CustomWifi(
    AdvancedLogger &logger) : _logger(logger) {}

bool CustomWifi::begin()
{
  _logger.debug("Setting up WiFi...", "setupWifi");

  _wifiManager.setConfigPortalTimeout(WIFI_CONFIG_PORTAL_TIMEOUT);

  if (_connectToWifi())
  {
    return true;
  }

  _logger.info("Connected to WiFi", "customwifi::setupWifi");
  return true;
}

bool CustomWifi::_connectToWifi()
{
  _logger.debug("Connecting to WiFi...", "customwifi::_connectToWifi");

  if (!_wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID))
  {
    _logger.warning("Failed to connect and hit timeout", "customwifi::_connectToWifi");
    return false;
  }

  printWifiStatus();
  _logger.info("Connected to WiFi", "customwifi::_connectToWifi");
  return true;
}

void CustomWifi::loop()
{
  if ((millis() - _lastMillisWifiLoop) > WIFI_LOOP_INTERVAL)
  {
    _lastMillisWifiLoop = millis();

    if (!WiFi.isConnected())
    {
      _logger.warning("WiFi connection lost. Reconnecting...", "customwifi::wifiLoop");

      if (!_connectToWifi())
      {
        setRestartEsp32("customwifi::wifiLoop", "Failed to reconnect to WiFi and hit timeout");
      }
    }
  }
}

void CustomWifi::resetWifi()
{
  _logger.warning("Resetting WiFi...", "resetWifi");

  _wifiManager.resetSettings();

  setRestartEsp32("customwifi::resetWifi", "WiFi reset (erase credentials). Will restart ESP32 in AP mode");
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
