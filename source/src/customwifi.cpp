#include "customwifi.h"

WiFiManager wifiManager;

unsigned long lastMillisWifiLoop = 0;

bool setupWifi() {
  logger.debug("Setting up WiFi...", "setupWifi");

  wifiManager.setConfigPortalTimeout(WIFI_CONFIG_PORTAL_TIMEOUT);
  
  if (!wifiManager.autoConnect(WIFI_CONFIG_PORTAL_SSID)) { //TODO: this could be made async
    logger.warning("Failed to connect and hit timeout", "customwifi::setupWifi");
    return false;
  }
  
  printWifiStatus();

  logger.info("Connected to WiFi", "customwifi::setupWifi");
  return true;
}

void wifiLoop() {
  if ((millis() - lastMillisWifiLoop) > WIFI_LOOP_INTERVAL) {
    lastMillisWifiLoop = millis();

    if (!WiFi.isConnected()) {
      logger.warning("WiFi connection lost. Reconnecting...", "customwifi::wifiLoop");

      if (!setupWifi()) {
        restartEsp32("customwifi::wifiLoop", "Failed to connect to WiFi and hit timeout");
      }
    }
  }
}

void resetWifi() {
  logger.warning("Resetting WiFi...", "resetWifi");
  wifiManager.resetSettings();
  restartEsp32("customwifi::resetWifi", "WiFi reset (erase credentials). Will restart ESP32 in AP mode");
}

bool setupMdns() {
  logger.debug("Setting up mDNS...", "setupMdns");
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    logger.error("Error setting up mDNS responder!", "customwifi::setupMdns");
    return false;
  }
  MDNS.addService("http", "tcp", 80);
  return true;
}

void getWifiStatus(JsonDocument& jsonDocument) {
  jsonDocument["macAddress"] = WiFi.macAddress();
  jsonDocument["localIp"] = WiFi.localIP().toString();
  jsonDocument["subnetMask"] = WiFi.subnetMask().toString();
  jsonDocument["gatewayIp"] = WiFi.gatewayIP().toString();
  jsonDocument["dnsIp"] = WiFi.dnsIP().toString();
  wl_status_t _status = WiFi.status();
  jsonDocument["status"] = WL_NO_SHIELD == _status ? "No Shield Available" :
                    WL_IDLE_STATUS == _status ? "Idle Status" :
                    WL_NO_SSID_AVAIL == _status ? "No SSID Available" :
                    WL_SCAN_COMPLETED == _status ? "Scan Completed" :
                    WL_CONNECTED == _status ? "Connected" :
                    WL_CONNECT_FAILED == _status ? "Connection Failed" :
                    WL_CONNECTION_LOST == _status ? "Connection Lost" :
                    WL_DISCONNECTED == _status ? "Disconnected" :
                    "Unknown Status";
  jsonDocument["ssid"] = WiFi.SSID();
  jsonDocument["bssid"] = WiFi.BSSIDstr();
  jsonDocument["rssi"] = WiFi.RSSI();
}

void printWifiStatus() {
  JsonDocument _jsonDocument;
  getWifiStatus(_jsonDocument);

  logger.debug(
    "MAC: %s | IP: %s | Status: %s | SSID: %s | RSSI: %s",
    "customwifi::printWifiStatus",
    _jsonDocument["macAddress"].as<String>().c_str(),
    _jsonDocument["localIp"].as<String>().c_str(),
    _jsonDocument["status"].as<String>().c_str(),
    _jsonDocument["ssid"].as<String>().c_str(),
    _jsonDocument["rssi"].as<String>().c_str()
  );
}
