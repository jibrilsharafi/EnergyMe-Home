#include "customwifi.h"

WiFiManager wifiManager;

bool setupWifi() {
  logger.log("Setting up WiFi...", "setupWifi", CUSTOM_LOG_LEVEL_DEBUG);

  wifiManager.setConfigPortalTimeout(WIFI_CONFIG_PORTAL_TIMEOUT);
  
  if (!wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID)) {
    logger.log("Failed to connect and hit timeout", "customwifi::setupWifi", CUSTOM_LOG_LEVEL_ERROR);
    return false;
  }
  
  printWifiStatus();
  logger.log("Connected to WiFi", "customwifi::setupWifi", CUSTOM_LOG_LEVEL_INFO);
  return true;
}

void checkWifi() {
  if (!WiFi.isConnected()) {
    logger.log("WiFi connection lost. Reconnecting...", "customwifi::checkWifi", CUSTOM_LOG_LEVEL_WARNING);
    if (!setupWifi()) {
      restartEsp32("customwifi::checkWifi", "Failed to connect to WiFi and hit timeout");
    }
  }
}

void resetWifi() {
  logger.log("Resetting WiFi...", "resetWifi", CUSTOM_LOG_LEVEL_WARNING);
  wifiManager.resetSettings();
  restartEsp32("customwifi::resetWifi", "WiFi reset (erase credentials). Will restart ESP32 in AP mode");
}

bool setupMdns() {
  logger.log("Setting up mDNS...", "setupMdns", CUSTOM_LOG_LEVEL_DEBUG);
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    logger.log("Error setting up mDNS responder!", "customwifi::setupMdns", CUSTOM_LOG_LEVEL_ERROR);
    return false;
  }
  MDNS.addService("http", "tcp", 80);
  return true;
}

// Get the status of the WiFi connection (SSID, IP, etc.)
JsonDocument getWifiStatus() {
  JsonDocument _jsonDocument;

  _jsonDocument["macAddress"] = WiFi.macAddress();
  _jsonDocument["localIp"] = WiFi.localIP().toString();
  _jsonDocument["subnetMask"] = WiFi.subnetMask().toString();
  _jsonDocument["gatewayIp"] = WiFi.gatewayIP().toString();
  _jsonDocument["dnsIp"] = WiFi.dnsIP().toString();
  wl_status_t _status = WiFi.status();
  _jsonDocument["status"] = WL_NO_SHIELD == _status ? "No Shield Available" :
                    WL_IDLE_STATUS == _status ? "Idle Status" :
                    WL_NO_SSID_AVAIL == _status ? "No SSID Available" :
                    WL_SCAN_COMPLETED == _status ? "Scan Completed" :
                    WL_CONNECTED == _status ? "Connected" :
                    WL_CONNECT_FAILED == _status ? "Connection Failed" :
                    WL_CONNECTION_LOST == _status ? "Connection Lost" :
                    WL_DISCONNECTED == _status ? "Disconnected" :
                    "Unknown Status";
  _jsonDocument["ssid"] = WiFi.SSID();
  _jsonDocument["bssid"] = WiFi.BSSIDstr();
  _jsonDocument["rssi"] = WiFi.RSSI();

  return _jsonDocument;
}

void printWifiStatus() {
  JsonDocument _jsonDocument = getWifiStatus();

  logger.log(
    (
      "MAC: " + _jsonDocument["macAddress"].as<String>() + " | " +
      "IP: " + _jsonDocument["localIp"].as<String>() + " | " +
      "Status: " + _jsonDocument["status"].as<String>() + " | " +
      "SSID: " + _jsonDocument["ssid"].as<String>() + " | " +
      "RSSI: " + _jsonDocument["rssi"].as<String>()
    ).c_str(),
    "customwifi::printWifiStatus",
    CUSTOM_LOG_LEVEL_DEBUG
  );
}
