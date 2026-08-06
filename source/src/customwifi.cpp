// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "customwifi.h"
#include "taskprofiler.h"

namespace CustomWifi
{
  static TaskHeartbeat _heartbeat;

  // WiFi connection task variables
  static TaskHandle_t _wifiTaskHandle = NULL;
  
  // Counters
  static uint64_t _lastReconnectAttempt = 0;
  static int32_t _reconnectAttempts = 0; // Increased every disconnection, reset on stable (few minutes) connection
  // Deadline replacing the old blocking delay() after a disconnect: the task must stay
  // responsive during the grace window. 0 means disarmed.
  static uint64_t _disconnectDeadlineMs = 0;
  // The notify wait is shortened while a deadline is armed, so "wait timed out" no longer
  // implies "a full periodic interval elapsed". Gate the periodic check on its own clock.
  static uint64_t _lastPeriodicCheckMs = 0;
  static uint64_t _lastWifiConnectedMillis = 0; // Timestamp when WiFi was last fully connected (for lwIP stabilization)
  static IPAddress _lastMdnsIp; // Last IP for which mDNS was initialized; used to skip rebuild when IP unchanged
  static bool _mdnsInitialized = false;

  // WiFi event notification values for task communication
  static const uint32_t WIFI_EVENT_CONNECTED = 1;
  static const uint32_t WIFI_EVENT_GOT_IP = 2;
  static const uint32_t WIFI_EVENT_DISCONNECTED = 3;
  static const uint32_t WIFI_EVENT_SHUTDOWN = 4;
  static const uint32_t WIFI_EVENT_FORCE_RECONNECT = 5;
  static const uint32_t WIFI_EVENT_NEW_CREDENTIALS = 6;
  static const uint32_t WIFI_EVENT_AP_START = 7;
  static const uint32_t WIFI_EVENT_AP_STACONNECTED = 8;
  static const uint32_t WIFI_EVENT_AP_STADISCONNECTED = 9;

  // Task state management
  static bool _taskShouldRun = false;
  static bool _eventsEnabled = false;
  
  // New credentials storage for async switching
  // To be safe, we should create mutexes around these since they are accessed via a public method
  // But since they are seldom written and read, we can avoid the complexity for now
  static char _pendingSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static char _pendingPassword[WIFI_PASSWORD_BUFFER_SIZE] = {0};
  static bool _hasPendingCredentials = false;

  // Outcome of the last credential write. setCredentials() returns the moment the request
  // is queued, so the HTTP response is sent before the write is even attempted; the status
  // endpoint reports this so a failed store is visible instead of silently discarded.
  // Lock-free single-word read, same discipline as _publishedState.
  static volatile bool _lastCredentialWriteFailed = false;

  // Diagnostic info for fallback portal
  static char _lastAttemptedSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static uint8_t _lastDisconnectReason = 0;
  static char _lastDisconnectSSID[WIFI_SSID_BUFFER_SIZE] = {0};
  static char _lastDisconnectBSSID[MAC_ADDRESS_BUFFER_SIZE] = {0};
  static int8_t _lastDisconnectRSSI = 0;

  // Network configuration (static IP)
  static WifiConfiguration _configuration;
  static SemaphoreHandle_t _configMutex = nullptr;

  // Provisioning state machine (pure logic in lib/wifi_provisioning, unit-tested there).
  // Owned by the WiFi task: only the task mutates _provisioning.
  static WifiProvisioning::Context _provisioning;
  // Lock-free snapshot of _provisioning.state for readers on other tasks. The Phase 4
  // auth filter runs on the AsyncTCP task for every request and must not take a mutex,
  // so the task publishes the state here after every transition. A uint8_t-backed enum
  // is written atomically on this target; readers only ever compare it for equality.
  static volatile WifiProvisioning::State _publishedState = WifiProvisioning::State::STA_CONNECTING;

  // STA connect attempt in flight. 0 means no attempt is being timed. The timeout is
  // extended after a power reset so a household blip, which also reboots the router,
  // does not count failures and raise a SoftAP while the router is still booting (D11).
  static uint64_t _connectDeadlineMs = 0;
  static uint32_t _connectTimeoutMs = 0;

  // SoftAP + captive DNS state. Only the WiFi task touches these.
  static DNSServer _dnsServer;
  static bool _dnsRunning = false;
  static bool _apRaised = false;
  // Written from the AsyncTCP task by startScan(), read there too. The WiFi task never touches
  // it, so the only writer is the request path and no synchronisation is needed for it alone.
  static uint64_t _lastScanStartedMs = 0;

  // Host-order snapshot of the SoftAP address, 0 when no AP is up. Published here so the
  // auth carve-out filter can compare against it without calling WiFi.softAPIP(), which is an
  // esp_netif_get_ip_info() call and runs on the AsyncTCP task for every request to every path.
  // Host order rather than the raw IPAddress dword for the reason _toHostOrder() documents.
  // A uint32_t is written atomically on this target and readers only compare it for equality.
  static volatile uint32_t _apAddressHostOrder = 0;

  // Private helper functions
  static void _onWiFiEvent(WiFiEvent_t event);
  static void _onWiFiEventWithInfo(WiFiEvent_t event, WiFiEventInfo_t info);
  static const char* _getDisconnectReasonString(uint8_t reason);
  static void _wifiConnectionTask(void *parameter);
  static void _startStaAttempt();
  static void _serviceConnectDeadline();
  static void _serviceApLifecycle();
  static void _reconcileApWithState();
  static bool _raiseAp();
  static void _tearDownAp();
  static void _serviceDns();
  static void _readStoredSsid(char *out, size_t outSize);
  static void _handleSuccessfulConnection();
  static bool _setupMdns();
  static void _cleanup();
  static void _startWifiTask();
  static void _stopWifiTask();
  static bool _testConnectivity();
  static void _forceReconnectInternal();
  static void _serviceDisconnectDeadline();
  static bool _hasStoredCredentials();
  static void _feedProvisioning(WifiProvisioning::Event event);
  static bool _isPowerReset();
  static void _sendOpenSourceTelemetry();
  static void _resolveApPassword(char* out, size_t outSize);
  static uint32_t _toHostOrder(const IPAddress &address);
  static IPAddress _fromHostOrder(uint32_t value);
  static bool _telemetrySent = false; // Ensures telemetry is sent only once per boot
  static bool _powerResetGraceUsed = false; // The extended post-power-cut timeout is for the first attempt only

  // Network configuration helpers
  static void _loadConfiguration();
  static void _saveConfigurationToPreferences(const WifiConfiguration &config);
  static void _applyNetworkConfiguration();
  static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial);
  static bool _validateConfiguration(const WifiConfiguration &config);
  static bool _isValidIpv4(const char* str, bool allowZero);
  static void _serviceStaticIpHealth(bool internetReachable);
  static uint8_t _getStaticBootFails();
  static void _setStaticBootFails(uint8_t count);
  // Static-IP health state, reset each boot. _staticIpApplied: the static IP was actually applied
  // (not skipped by the backstop). _staticBackstopCleared: the boot-fail counter has been cleared
  // this boot. _staticRecoveryResolved: DHCP auto-recovery has confirmed internet or already fired.
  static bool _staticIpApplied = false;
  static bool _staticBackstopCleared = false;
  static bool _staticRecoveryResolved = false;
  static uint8_t _staticInternetFailStreak = 0;

  bool begin()
  {
    if (_wifiTaskHandle != NULL)
    {
      LOG_DEBUG("WiFi task is already running");
      return true;
    }

    LOG_DEBUG("Starting WiFi...");

    // This has to be before everything else to ensure the hostname is actually set
    char hostname[WIFI_SSID_BUFFER_SIZE];
    snprintf(hostname, sizeof(hostname), "%s-%s", WIFI_HOSTNAME_PREFIX, DEVICE_ID);
    WiFi.setHostname(hostname); // Allow for easier identification in the router/network client list

    // This loop owns the connect path, so Arduino must not also drive one. With
    // auto-reconnect on, STAClass re-calls esp_wifi_connect() the instant a disconnect
    // arrives, and ESP-IDF then refuses esp_wifi_set_config() with "sta is connecting,
    // cannot set config". On hardware that meant a device which could not associate could
    // not be given new credentials either: 12 consecutive submissions were accepted by the
    // API and discarded by the driver. It also made every _startStaAttempt() a no-op whose
    // 10 s deadline expired into a failure that never actually ran.
    WiFi.setAutoReconnect(false);
    WiFi.persistent(true);
    
    // APSTA from boot rather than switching modes later: a mode change on a live netif
    // drops the STA association, which is exactly what must not happen when the SoftAP is
    // raised to rescue a device that is still half-connected. Mode alone does not
    // broadcast anything - the AP only exists once softAP() is called.
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false); // Disable WiFi sleep to prevent handshake timeouts

    // Load persisted network configuration, applying defaults and writing them back to
    // NVS on first boot (migration). Then apply the static IP before the connection
    // attempt so it takes effect for the initial join.
    _loadConfiguration();
    _applyNetworkConfiguration();

    // Register every WiFi event handler once, here, before any event can be posted.
    // NetworkEvents never defines NETWORK_EVENTS_MUTEX, so _cbEventList is a plain
    // std::vector mutated without a lock while the arduino_events task iterates it.
    // Registering later (mid-connection) can reallocate the vector under that
    // iteration, which is a use-after-free and the most plausible cause of the
    // crash previously worked around by delaying registration.
    //
    // Registration is not the same as delivery: _onWiFiEvent gates on _eventsEnabled,
    // which stays false until the initial connection completes. Enabling it here too
    // would let eSetValueWithOverwrite leave a stale notification for the first
    // xTaskNotifyWait to consume.
    WiFi.onEvent(_onWiFiEventWithInfo);
    WiFi.onEvent(_onWiFiEvent);

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

  bool isApServing()
  {
    return WiFi.AP.started() && WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
  }

  bool isApAddress(const IPAddress &address)
  {
    uint32_t apAddress = _apAddressHostOrder;
    if (apAddress == 0) return false; // No AP up: nothing can match
    return _toHostOrder(address) == apAddress;
  }

  bool isNetworkServiceable()
  {
    return WifiProvisioning::isNetworkServiceable(isFullyConnected(), isApServing());
  }

  WifiProvisioning::State getProvisioningState()
  {
    return _publishedState;
  }

  // Network scan for the provisioning UI.
  //
  // Async on purpose: WiFi.scanNetworks() blocking would stall the WiFi task for seconds,
  // which is what this whole rework exists to avoid. Results are cached so a polling UI
  // does not restart a scan on every request - and, more importantly, so a scan is never
  // started while one is already running.
  //
  // Every start, read and free of the scan result buffer happens on the AsyncTCP task, which
  // is single threaded. That is what makes the buffer safe to touch without a mutex, and it
  // is why the buffer is NOT freed from the WiFi task on AP teardown: that would race a
  // getScanResultsAsJson() mid-iteration over results the driver had just freed.
  bool startScan()
  {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return true; // Already in flight

    // Never scan while an association attempt is in flight: esp_wifi_scan_start returns
    // ESP_ERR_WIFI_STATE during connect, and on some paths it aborts the attempt.
    // _connectDeadlineMs belongs to the WiFi task and is read here, on the AsyncTCP task,
    // without synchronisation. A uint64_t is two stores on this 32-bit core, so the read can
    // tear, but only the zero/nonzero distinction is used and both tearing outcomes are
    // already handled: a spurious nonzero refuses one scan, and a spurious zero lets a scan
    // through that the driver then rejects with ESP_ERR_WIFI_STATE.
    if (_connectDeadlineMs != 0) {
      LOG_DEBUG("Scan requested during an association attempt - deferring");
      return false;
    }

    int16_t result = WiFi.scanNetworks(true /* async */, true /* show hidden */,
                                       false /* passive */, WIFI_SCAN_MS_PER_CHANNEL);
    if (result == WIFI_SCAN_FAILED) {
      LOG_WARNING("Failed to start WiFi scan");
      return false;
    }

    _lastScanStartedMs = millis64();
    return true;
  }

  void getScanResultsAsJson(JsonDocument &jsonDocument, bool forceRescan)
  {
    int16_t count = WiFi.scanComplete();

    if (count == WIFI_SCAN_RUNNING) {
      jsonDocument["status"] = "running";
      jsonDocument["networks"].to<JsonArray>();
      return;
    }

    if (count == WIFI_SCAN_FAILED) {
      // No scan has ever completed, or the last one failed. Kick one off so the next poll
      // has something, and tell the client to come back.
      jsonDocument["status"] = startScan() ? "running" : "unavailable";
      jsonDocument["networks"].to<JsonArray>();
      return;
    }

    // A completed set is served from cache, which is what stops a polling client restarting a
    // scan on every request. Two things override that.
    //
    // `forceRescan` is the user pressing "Scan again". Without it that button re-renders the
    // same cached list and never touches the radio, which is exactly the moment a user is
    // pressing it because the network they want is missing.
    //
    // The TTL is about heap, not freshness: a completed set is a calloc() of _scanCount
    // wifi_ap_record_t (WiFiScan.cpp:126), so it lands in internal RAM and a dense apartment
    // block makes it several kilobytes, and nothing frees it until another scan starts, which
    // on a device that provisioned successfully is never.
    bool stale = (millis64() - _lastScanStartedMs) > WIFI_SCAN_RESULTS_TTL_MS;

    if (forceRescan || stale) {
      // scanNetworks() calls scanDelete() itself before it starts (WiFiScan.cpp:77), so
      // starting the scan is what retires the old buffer. Deliberately NOT freeing first: a
      // rescan that cannot start right now would otherwise throw away a list the user is
      // still looking at.
      if (startScan()) {
        jsonDocument["status"] = "running";
        jsonDocument["networks"].to<JsonArray>();
        return;
      }

      if (stale) {
        // Nothing is going to refresh this set and the driver holds it in internal RAM, so
        // give the heap back even though it costs the client one empty poll. Freeing here, on
        // the AsyncTCP task, is what keeps the buffer single threaded (see startScan).
        WiFi.scanDelete();
        jsonDocument["status"] = "unavailable";
        jsonDocument["networks"].to<JsonArray>();
        return;
      }

      // Forced but the radio is busy associating: serve the cached set rather than nothing.
    }

    jsonDocument["status"] = "complete";
    jsonDocument["ageMs"] = millis64() - _lastScanStartedMs;

    JsonArray networks = jsonDocument["networks"].to<JsonArray>();
    for (int16_t i = 0; i < count && i < WIFI_SCAN_MAX_RESULTS; i++) {
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = WiFi.SSID(i);
      network["rssi"] = WiFi.RSSI(i);
      network["channel"] = WiFi.channel(i);
      network["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
  }

  void getStoredSsid(char* out, size_t outSize)
  {
    if (out == nullptr || outSize == 0) return;
    _readStoredSsid(out, outSize);
  }

  bool lastCredentialWriteFailed()
  {
    return _lastCredentialWriteFailed;
  }

  void getDisconnectDiagnosticsAsJson(JsonDocument &jsonDocument)
  {
    jsonDocument["lastAttemptedSsid"] = _lastAttemptedSSID;
    jsonDocument["reasonCode"] = _lastDisconnectReason;
    jsonDocument["reason"] = _getDisconnectReasonString(_lastDisconnectReason);
    jsonDocument["disconnectSsid"] = _lastDisconnectSSID;
    jsonDocument["disconnectBssid"] = _lastDisconnectBSSID;
    jsonDocument["disconnectRssi"] = _lastDisconnectRSSI;
    jsonDocument["retryAttempts"] = _reconnectAttempts;
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

    // SoftAP events. Notify-only like the rest; the task decides what they mean.
    // ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED is deliberately left masked - it fires per
    // probe request and would flood the notification path.
    case ARDUINO_EVENT_WIFI_AP_START:
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_AP_START, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_AP_STACONNECTED, eSetValueWithOverwrite);
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      xTaskNotify(_wifiTaskHandle, WIFI_EVENT_AP_STADISCONNECTED, eSetValueWithOverwrite);
      break;

    default:
      // Forward unknown events to task for logging/debugging
      xTaskNotify(_wifiTaskHandle, (uint32_t)event, eSetValueWithOverwrite);
      break;
    }
  }

  // Captures the disconnect reason so the UI can explain a failed association. Registered
  // in begin(), before the first connect attempt, so an initial failure is captured too.
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

  // Consumed by getDisconnectDiagnosticsAsJson(), which is what replaced the reason-code
  // display on the removed WiFiManager /diagnostic page (D11).
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
    // Note: printDeviceStatusDynamic() removed to avoid flash I/O from this task

    Led::clearPattern(Led::PRIO_MEDIUM); // Ensure that if we are connected again, we don't keep the blue pattern
    Led::setGreen(Led::PRIO_NORMAL); // Hack: to ensure we get back to green light, we set it here even though a proper LED manager would handle priorities better
    LOG_INFO("WiFi fully connected and operational");

    // Static-IP health (boot-fail backstop clear + DHCP auto-recovery) is serviced from the periodic
    // check in the task loop, not here: it must run past the early crash window and after the restart
    // gate's minimum uptime, and we never reconfigure the live netif (that races lwIP).

    _sendOpenSourceTelemetry(); // Non-blocking short POST (guarded by compile-time flag)
  }

  static void _wifiConnectionTask(void *parameter)
  {
    LOG_DEBUG("WiFi task started");
    uint32_t notificationValue;

    // Seed the provisioning state machine from what the driver actually has stored.
    // Owned by this task from here on.
    bool hasCredentials = _hasStoredCredentials();
    WifiProvisioning::init(_provisioning, hasCredentials, millis64());
    _publishedState = _provisioning.state;
    LOG_INFO("Provisioning init: %s credentials, state %d",
             hasCredentials ? "found" : "no", (int)_provisioning.state);
    _taskShouldRun = true;

    Led::pulseBlue(Led::PRIO_MEDIUM);

    // Handlers are registered in begin(). Delivery opens here, before the first
    // WiFi.begin(), because the connect attempt is now driven by this loop: its events are
    // exactly what the loop needs, not stale noise from a blocking library call.
    _eventsEnabled = true;

    if (hasCredentials) {
      _startStaAttempt();
    } else {
      // Nothing to try. Record the decision, then let the same reconciliation path every
      // other raise goes through bring the radio up; the loop below keeps it bounded.
      LOG_INFO("No stored credentials - raising the SoftAP for provisioning");
      WifiProvisioning::raiseAp(_provisioning, millis64());
      _publishedState = _provisioning.state;
      _reconcileApWithState();
    }

    // Main task loop - handles fallback scenarios and deferred logging
    _lastPeriodicCheckMs = millis64();
    while (_taskShouldRun)
    {
      TASK_HEARTBEAT(_heartbeat);

      // Serviced at the top of the loop so it also runs on iterations reached by the
      // `continue`s in the switch below, which skip the periodic branch entirely.
      _serviceDisconnectDeadline();
      _serviceConnectDeadline();
      _serviceApLifecycle();

      // Never wait past an armed deadline, or a 15 s grace window would go unchecked
      // for up to a full 30 s periodic interval. Same for the association timeout, and
      // for the AP lifetime/grace timers while the AP is up.
      uint64_t nowMs = millis64();
      uint32_t waitMs = WIFI_PERIODIC_CHECK_INTERVAL;
      if (_disconnectDeadlineMs != 0)
      {
        uint64_t remainingMs = (_disconnectDeadlineMs > nowMs) ? (_disconnectDeadlineMs - nowMs) : 0;
        if (remainingMs < waitMs) waitMs = (uint32_t)remainingMs;
      }
      if (_connectDeadlineMs != 0)
      {
        uint64_t remainingMs = (_connectDeadlineMs > nowMs) ? (_connectDeadlineMs - nowMs) : 0;
        if (remainingMs < waitMs) waitMs = (uint32_t)remainingMs;
      }
      if (_apRaised && waitMs > WIFI_AP_LIFECYCLE_TICK_MS)
      {
        // The AP lifetime and grace windows are minutes long, so a coarse tick is enough
        // to bound them without waking the task needlessly.
        waitMs = WIFI_AP_LIFECYCLE_TICK_MS;
      }

      // Wait for notification from event handler or timeout
      if (xTaskNotifyWait(0, ULONG_MAX, &notificationValue, pdMS_TO_TICKS(waitMs)))
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
          // Both deadlines must be disarmed here. isFullyConnected() deliberately returns
          // false for WIFI_LWIP_STABILIZATION_DELAY after this point, so an association
          // that completes in the last second of its window would otherwise let
          // _serviceConnectDeadline() fire against a link that is actually up: it would
          // log a false timeout, feed a spurious STA_ATTEMPT_FAILED, and re-associate.
          _disconnectDeadlineMs = 0;
          _connectDeadlineMs = 0;
          _feedProvisioning(WifiProvisioning::Event::STA_CONNECTED);
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

            // Stop any association in flight FIRST. ESP-IDF rejects esp_wifi_set_config()
            // while the STA is connecting, and a device that cannot associate is retrying
            // almost continuously - so without this the write fails exactly when the user
            // is trying to correct the credentials, which is the whole point of AP_ASSIST.
            // Observed on hardware as 12/12 submissions refused with ESP_ERR_WIFI_CONN.
            _connectDeadlineMs = 0; // The attempt we are cancelling must not report a timeout
            esp_wifi_disconnect();

            esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (err != ESP_OK) {
              // One retry after a short settle: disconnect is asynchronous, so the driver
              // may still have been leaving the connecting state on the first try.
              LOG_WARNING("Credential write refused (%s), retrying after disconnect settles",
                          esp_err_to_name(err));
              vTaskDelay(pdMS_TO_TICKS(WIFI_CREDENTIAL_WRITE_RETRY_DELAY_MS));
              err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            }

            if (err != ESP_OK) {
              LOG_ERROR("Failed to save WiFi credentials: %s", esp_err_to_name(err));
              memset(_pendingSSID, 0, sizeof(_pendingSSID));
              memset(_pendingPassword, 0, sizeof(_pendingPassword));
              _hasPendingCredentials = false;
              // Record it so the caller can find out. setCredentials() returns as soon as
              // the request is queued, so the HTTP 200 has already gone out by now and this
              // is the only way the user learns the password was not stored.
              _lastCredentialWriteFailed = true;
              _startStaAttempt(); // Resume trying with whatever credentials we still have
              continue;
            }

            _lastCredentialWriteFailed = false;

            // Clear pending credentials from memory
            memset(_pendingSSID, 0, sizeof(_pendingSSID));
            memset(_pendingPassword, 0, sizeof(_pendingPassword));
            _hasPendingCredentials = false;

            // Apply without restarting. The restart existed because WiFiManager owned the
            // connect path and could not be re-driven; this loop can. Restarting mid-
            // provisioning would also drop the very AP the user submitted the form on.
            LOG_INFO("New credentials saved, associating without restart");
            _feedProvisioning(WifiProvisioning::Event::CREDENTIALS_SUBMITTED);
            _reconnectAttempts = 0;
            _disconnectDeadlineMs = 0;
            WiFi.disconnect(false);
            _startStaAttempt();
          }
          continue; // No further action needed

        case WIFI_EVENT_DISCONNECTED:
          statistics.wifiConnectionError++;
          // Not while the SoftAP is up. _ledTask accepts on priority >= current, so this
          // pulse and the AP's fast blink are last-write-wins at PRIO_MEDIUM, and a link
          // that keeps failing fires this event exactly while the AP is broadcasting - the
          // one time the AP indication has something to say. The AP owns the LED until it
          // comes down, and _tearDownAp() hands it back.
          if (!_apRaised) Led::pulseBlue(Led::PRIO_MEDIUM);
          LOG_WARNING("WiFi disconnected - auto-reconnect will handle");
          _lastWifiConnectedMillis = 0; // Reset stabilization timer on disconnect
          _feedProvisioning(WifiProvisioning::Event::STA_LOST);

          // Give auto-reconnect (enabled by default) a grace window, then evaluate.
          // Arm only when not already armed: re-arming on every disconnect would let a
          // link flapping faster than the window push the deadline out indefinitely, so
          // the evaluation below - and with it the portal fallback - would never run.
          // The old blocking delay() always completed and always evaluated exactly once
          // per window; this preserves that while keeping the task responsive.
          // Not while a connect attempt is in flight: that attempt already has its own
          // deadline, and the driver reports a failed FIRST association through this same
          // DISCONNECTED event (NO_AP_FOUND, AUTH_FAIL, HANDSHAKE_TIMEOUT). Arming both
          // would feed STA_ATTEMPT_FAILED twice for one failure, so apRaiseTriggers would
          // reach its threshold in roughly half the intended time and stop meaning
          // "consecutive genuine association failures".
          if (_disconnectDeadlineMs == 0 && _connectDeadlineMs == 0)
          {
            _disconnectDeadlineMs = millis64() + WIFI_DISCONNECT_DELAY;
          }
          break;

        case WIFI_EVENT_AP_START:
          LOG_DEBUG("SoftAP started on %s", WiFi.softAPIP().toString().c_str());
          continue;

        case WIFI_EVENT_AP_STACONNECTED:
          LOG_DEBUG("SoftAP client connected (%d now associated)", WiFi.softAPgetStationNum());
          continue;

        case WIFI_EVENT_AP_STADISCONNECTED:
          LOG_DEBUG("SoftAP client disconnected (%d still associated)", WiFi.softAPgetStationNum());
          // Nothing is watching any more, so the grace window has already delivered whatever
          // it was going to deliver. Only meaningful while the AP is up: the driver also
          // posts this event as the AP itself comes down, and feeding it then would hand the
          // state machine a teardown it has already performed.
          if (_apRaised && WiFi.softAPgetStationNum() == 0) {
            _feedProvisioning(WifiProvisioning::Event::AP_LAST_CLIENT_LEFT);
          }
          continue;

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
        // Timeout occurred. The wait may have been shortened by an armed deadline, so a
        // timeout no longer implies a full interval elapsed. Without this gate the branch
        // below would call _forceReconnectInternal() every few seconds while disconnected,
        // because that is exactly when the deadline is armed and isFullyConnected() false.
        if (_taskShouldRun && (millis64() - _lastPeriodicCheckMs >= WIFI_PERIODIC_CHECK_INTERVAL))
        {
          _lastPeriodicCheckMs = millis64();
          if (isFullyConnected())
          {
            // Test internet connectivity but don't force reconnection if it fails
            // WiFi is connected to local network - internet may simply be unavailable
            // This allows the device to operate in local-only mode (static IP, isolated networks)
            bool internetReachable = _testConnectivity();
            if (!internetReachable) {
              LOG_DEBUG("Internet connectivity unavailable - device operating in local-only mode");
            }

            // Manage the static-IP safety nets (backstop clear + DHCP auto-recovery)
            _serviceStaticIpHealth(internetReachable);

            // Reset failure counter on sustained connection
            if (_reconnectAttempts > 0 && millis64() - _lastReconnectAttempt > WIFI_STABLE_CONNECTION_DURATION)
            {
              LOG_DEBUG("WiFi connection stable - resetting counters");
              _reconnectAttempts = 0;
            }
          }
          else if (_provisioning.state == WifiProvisioning::State::UNPROVISIONED)
          {
            // No stored credentials: there is nothing to reconnect to. Forcing a reconnect
            // here would still increment _reconnectAttempts every 30 s (D8), poisoning the
            // very counter the AP-raise predicate reads, so the predicate would never settle
            // while the AP is up.
            LOG_DEBUG("Periodic check: unprovisioned, not forcing a reconnect");
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
    LOG_INFO("Resetting WiFi credentials and restarting...");
    Led::blinkOrangeFast(Led::PRIO_CRITICAL);

    // Also revert the network configuration (static IP) to defaults.
    // A WiFi reset is the connectivity-recovery path, so it must give a clean DHCP slate -
    // otherwise a bad static IP would survive the credential reset and keep the device
    // unreachable even after reconfiguring WiFi through the portal.
    resetConfiguration();

    // Erase the credentials the driver stores. This is the same store _hasStoredCredentials()
    // reads, so after this the device boots UNPROVISIONED and raises its SoftAP - the two
    // must agree, or a reset device would still read as provisioned.
    esp_err_t err = esp_wifi_restore();
    if (err != ESP_OK) {
      LOG_ERROR("Failed to erase stored WiFi credentials: %s", esp_err_to_name(err));
    }

    setRestartSystem("Restart after WiFi reset");
  }

  bool setCredentials(const char* ssid, const char* password)
  {
    // Validate inputs
    if (!ssid || !isStringLengthValid(ssid, 1, WIFI_SSID_BUFFER_SIZE - 1))
    {
      LOG_ERROR("Invalid SSID provided (must be 1-31 characters)");
      return false;
    }

    if (!password)
    {
      LOG_ERROR("Password cannot be NULL");
      return false;
    }

    if (!isStringLengthValid(password, 0, WIFI_PASSWORD_BUFFER_SIZE - 1))
    {
      LOG_ERROR("Password exceeds maximum length of %d characters", WIFI_PASSWORD_BUFFER_SIZE - 1);
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
    // Skip rebuild if responder is already running for the current IP. ESP-IDF mDNS
    // does periodic unsolicited re-announces (~120 s, RFC 6762) on its own, so peers
    // stay aware as long as the responder is alive. Most WiFi reconnects keep the
    // same DHCP lease; only rebuild when the IP actually changed.
    IPAddress currentIp = WiFi.localIP();
    if (_mdnsInitialized && currentIp == _lastMdnsIp) {
      LOG_DEBUG("mDNS already running for %s, skipping rebuild", currentIp.toString().c_str());
      return true;
    }

    LOG_DEBUG("Setting up mDNS...");

    // Ensure mDNS is stopped before starting
    if (_mdnsInitialized) {
      MDNS.end();
      delay(100);
    }

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
      MDNS.addServiceTxt("modbus", "tcp", "channels", "16"); // Cannot use constant since that is a number, not a string

      _lastMdnsIp = currentIp;
      _mdnsInitialized = true;
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
    
    // Disable event handling first. This is the whole shutdown barrier: _onWiFiEvent
    // returns immediately once _eventsEnabled is false, so no notification can reach a
    // task that is going away.
    _eventsEnabled = false;

    // Deliberately NOT calling WiFi.removeEvent() here. Erasing from _cbEventList while
    // the arduino_events task may be iterating it is the actual crash risk during
    // shutdown, and the flag above already stops delivery. See D10.


    // Stop mDNS
    MDNS.end();
    _mdnsInitialized = false;
    _lastMdnsIp = IPAddress();

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
      // Here we only log a debug since the internet connectivity is not a must-have
      // While before we used LOG_WARNING since the issue is WiFi related (critical)
      LOG_DEBUG("Connectivity test failed: cannot reach %s:%d (no internet)", 
                  CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT);
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

  // Reads the credentials the WiFi driver persists in its own NVS namespace. This is the
  // same store WiFi.begin() with no arguments connects from, so "provisioned" here means
  // exactly "WiFi.begin() has something to try".
  static bool _hasStoredCredentials()
  {
    wifi_config_t conf = {};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &conf);
    if (err != ESP_OK) {
      LOG_WARNING("Could not read stored WiFi config: %s", esp_err_to_name(err));
      return false;
    }
    return conf.sta.ssid[0] != '\0';
  }

  // Single funnel for provisioning transitions so the published snapshot can never drift
  // from the owned context. Task context only.
  static void _feedProvisioning(WifiProvisioning::Event event)
  {
    WifiProvisioning::State previous = _provisioning.state;
    WifiProvisioning::State current = WifiProvisioning::onEvent(_provisioning, event, millis64());
    _publishedState = current;

    if (current != previous) {
      LOG_INFO("Provisioning state %d -> %d", (int)previous, (int)current);
    }

    // Act on the decision immediately. onEvent() can decide an AP is needed (the move to
    // AP_ASSIST does exactly that), and waiting for the next loop tick to notice would
    // delay the one thing that makes an unreachable device reachable again.
    _reconcileApWithState();
  }

  // IPAddress <-> host-order conversion, done via octets on purpose.
  //
  // IPAddress stores its bytes in a union with a uint32_t and its (uint32_t) cast returns
  // that raw dword, so on this little-endian target the FIRST octet is the LOW byte. Using
  // the cast for arithmetic silently byte-reverses the address: IPAddress(0xAC1F2A01) is
  // 1.42.31.172, not 172.31.42.1, and a /24 mask comes out as 0.255.255.255. The candidate
  // table and every helper in lib/wifi_provisioning use MSB-first, so the two conventions
  // must be converted explicitly at this boundary rather than cast across it.
  static uint32_t _toHostOrder(const IPAddress& address)
  {
    return ((uint32_t)address[0] << 24) | ((uint32_t)address[1] << 16) |
           ((uint32_t)address[2] << 8)  | (uint32_t)address[3];
  }

  static IPAddress _fromHostOrder(uint32_t value)
  {
    return IPAddress((uint8_t)(value >> 24), (uint8_t)(value >> 16),
                     (uint8_t)(value >> 8),  (uint8_t)value);
  }

  // Reads the SSID the driver has stored, for diagnostics and for the UI to show what the
  // device is trying to join. Replaces WiFiManager::getWiFiSSID(true) (D11).
  static void _readStoredSsid(char* out, size_t outSize)
  {
    out[0] = '\0';
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return;
    snprintf(out, outSize, "%s", (const char*)conf.sta.ssid);
  }

  // Starts one non-blocking association attempt from the stored credentials. Replaces
  // WiFiManager::autoConnect(), which blocked the task for the whole attempt and, on
  // failure, restarted the device instead of letting it fall back to provisioning.
  static void _startStaAttempt()
  {
    // The extended timeout is a one-shot for the first attempt after a power cut, to let
    // a router that lost power at the same time finish booting. esp_reset_reason() is
    // fixed for the whole boot, so testing it per attempt kept every later retry at 5
    // minutes too - and since a mains-wired meter powers on from POWERON every time, that
    // made WIFI_CONNECT_TIMEOUT_SECONDS unreachable in the field and pushed AP_ASSIST out
    // to ~25 minutes instead of ~50 seconds.
    bool powerReset = _isPowerReset() && !_powerResetGraceUsed;
    _powerResetGraceUsed = true;

    _connectTimeoutMs = (powerReset ? WIFI_CONNECT_TIMEOUT_POWER_RESET_SECONDS
                                    : WIFI_CONNECT_TIMEOUT_SECONDS) * 1000UL;
    if (powerReset) {
      LOG_INFO("Power reset detected - extended WiFi timeout (%lu s) for the first attempt",
               (unsigned long)(_connectTimeoutMs / 1000UL));
    }

    _readStoredSsid(_lastAttemptedSSID, sizeof(_lastAttemptedSSID));
    LOG_INFO("Starting WiFi association attempt to '%s'", _lastAttemptedSSID);

    // WiFi.begin() calls esp_wifi_set_config() internally, which ESP-IDF refuses while a
    // previous association is still in flight. Clearing it first keeps every attempt real:
    // without this the call silently no-ops and the deadline below times out an attempt
    // that never started, which is what produced the phantom failures seen on hardware.
    esp_wifi_disconnect();

    if (!WiFi.begin()) { // No arguments: uses the credentials the driver has stored
      // Do not arm a deadline for an attempt that did not start; report it now so the
      // state machine counts a real failure rather than waiting out a fictional one.
      LOG_WARNING("WiFi.begin() refused - treating as an immediate association failure");
      _connectDeadlineMs = 0;
      _feedProvisioning(WifiProvisioning::Event::STA_ATTEMPT_FAILED);
      return;
    }

    _connectDeadlineMs = millis64() + _connectTimeoutMs;
  }

  // One association attempt gave up. Feed the state machine and either retry or let the
  // AP-raise predicate decide. Never restarts the device: failing to associate is now a
  // state (UNPROVISIONED / AP_ASSIST), not a fatal error.
  static void _serviceConnectDeadline()
  {
    if (_connectDeadlineMs == 0) return;
    if (millis64() < _connectDeadlineMs) return;

    _connectDeadlineMs = 0;

    if (isFullyConnected()) return; // Raced with a successful association

    LOG_WARNING("Association attempt timed out after %lu s", (unsigned long)(_connectTimeoutMs / 1000UL));
    _feedProvisioning(WifiProvisioning::Event::STA_ATTEMPT_FAILED);

    // Keep trying for as long as there is something to try. Raising the AP is no longer a
    // reason to stop: under APSTA both interfaces run at once, so the device can host the
    // portal and still rejoin by itself the moment the router comes back. Without
    // credentials there is nothing to attempt, and WiFi.begin() would just churn the radio.
    if (_provisioning.hasCredentials) {
      _startStaAttempt();
    }
  }

  // Raise the SoftAP on a subnet that cannot collide with the STA subnet. lwIP's ip4_route
  // returns the FIRST matching netif and netif_add prepends, so an AP raised after STA wins
  // every ambiguous match - an overlapping AP subnet silently blackholes LAN traffic (D5).
  static bool _raiseAp()
  {
    if (_apRaised) return true;

    // Compare against both the live lease and the configured static IP: a restored backup
    // can carry a static IP from a different LAN that is not currently in effect.
    bool staValid = WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0);
    uint32_t staAddr = staValid ? _toHostOrder(WiFi.localIP()) : 0;
    uint8_t staCidr = 24;
    if (staValid) {
      // cidrFromNetmask rejects a non-contiguous mask by returning 0, which is not a usable
      // prefix; fall back to /24 rather than comparing against a network that does not exist.
      uint8_t derived = WifiProvisioning::cidrFromNetmask(_toHostOrder(WiFi.subnetMask()));
      if (derived == 0) {
        LOG_WARNING("STA netmask %s is not a valid prefix - assuming /24 for overlap checks",
                    WiFi.subnetMask().toString().c_str());
      } else {
        staCidr = derived;
      }
    }

    // Through getConfiguration(), never off _configuration directly: this runs on the WiFi
    // task while the server task can be inside setConfiguration(), and the fields read below
    // are char arrays, so an unlocked read can catch a half-written address. A failure to
    // take the copy means the static IP cannot be considered at all, which is safe: the STA
    // comparison still applies and the candidate list still fails closed.
    WifiConfiguration config;
    bool haveConfig = getConfiguration(config);
    if (!haveConfig) {
      LOG_WARNING("Could not read the network configuration - ignoring the static IP for overlap checks");
    }

    bool staticValid = false;
    uint32_t staticAddr = 0;
    uint8_t staticCidr = 24;
    if (haveConfig && config.useStaticIp && config.ip[0] != '\0') {
      IPAddress parsed;
      if (parsed.fromString(config.ip)) {
        staticValid = true;
        staticAddr = _toHostOrder(parsed);
        IPAddress parsedMask;
        if (config.subnet[0] != '\0' && parsedMask.fromString(config.subnet)) {
          uint8_t derived = WifiProvisioning::cidrFromNetmask(_toHostOrder(parsedMask));
          if (derived != 0) staticCidr = derived;
        }
      }
    }

    WifiProvisioning::Subnet chosen;
    if (!WifiProvisioning::selectApSubnet(staValid, staAddr, staCidr,
                                          staticValid, staticAddr, staticCidr, chosen)) {
      LOG_ERROR("No non-overlapping SoftAP subnet available - not raising the AP");
      return false;
    }

    IPAddress apIp = _fromHostOrder(chosen.address);
    IPAddress apMask = _fromHostOrder(WifiProvisioning::netmaskFromCidr(chosen.cidr));

    char apSsid[WIFI_SSID_BUFFER_SIZE];
    snprintf(apSsid, sizeof(apSsid), "%s-%s", WIFI_CONFIG_PORTAL_SSID, DEVICE_ID);
    char apPassword[WIFI_PASSWORD_BUFFER_SIZE];
    _resolveApPassword(apPassword, sizeof(apPassword));

    // Documented order: config, then raise, then the DHCP captive-portal option (which
    // requires the AP to be started before it can be set).
    if (!WiFi.softAPConfig(apIp, apIp, apMask)) {
      LOG_ERROR("softAPConfig failed for %s/%u", apIp.toString().c_str(), chosen.cidr);
      return false;
    }
    if (!WiFi.softAP(apSsid, apPassword)) {
      LOG_ERROR("softAP failed for SSID %s", apSsid);
      return false;
    }

    WiFi.AP.enableDhcpCaptivePortal();

    _apRaised = true;

    // Publish the address the auth carve-out filter compares against. Set only after softAP()
    // has succeeded, so isApAddress() is never true for an AP that does not exist, and taken
    // from the address we configured rather than read back, which keeps it a plain store.
    _apAddressHostOrder = chosen.address;

    // Same signal the WiFiManager portal used to give (its setAPCallback), so the meaning
    // of a fast blue blink does not change for anyone who has seen it before: "I am waiting
    // for you to configure me". PRIO_MEDIUM per D14, so genuine faults still win.
    Led::blinkBlueFast(Led::PRIO_MEDIUM);

    LOG_INFO("SoftAP raised: %s on %s/%u", apSsid, apIp.toString().c_str(), chosen.cidr);
    return true;
  }

  static void _tearDownAp()
  {
    if (!_apRaised) return;

    // Stop DNS before the interface it answers for disappears. Done directly rather than
    // through _serviceDns(), which cannot act here: _apRaised is still true at this point.
    if (_dnsRunning) {
      _dnsServer.stop();
      _dnsRunning = false;
    }

    // Retire the published address before the interface goes, so the carve-out filter stops
    // matching on it no later than the AP stops existing.
    _apAddressHostOrder = 0;

    WiFi.softAPdisconnect(true);
    _apRaised = false;

    // Drop the provisioning blink and hand the LED back to whatever the STA side is doing.
    // clearPattern() alone is not enough: it also drops the disconnected pulse that
    // WIFI_EVENT_DISCONNECTED suppresses while the AP is up, which would leave a device that
    // is still searching for its network showing nothing at all.
    Led::clearPattern(Led::PRIO_MEDIUM);
    if (isFullyConnected()) Led::setGreen(Led::PRIO_NORMAL);
    else Led::pulseBlue(Led::PRIO_MEDIUM);

    LOG_INFO("SoftAP torn down");
  }

  // The catch-all DNS responder binds INADDR_ANY:53 and answers every name with a fixed
  // address regardless of arrival interface, so leaving it up once STA is connected makes
  // the device an open resolver on the customer's LAN. Bound to AP-up-and-STA-down (D4).
  static void _serviceDns()
  {
    bool wanted = _apRaised && WifiProvisioning::isDnsAllowed(_provisioning, WiFi.isConnected());

    if (wanted && !_dnsRunning) {
      // Always the three-argument form: the no-arg start() only sets the resolved address
      // when it is currently zero, so it would answer with a stale IP after the AP moves.
      if (_dnsServer.start(53, "*", WiFi.softAPIP())) {
        _dnsRunning = true;
        LOG_DEBUG("Captive DNS started on %s", WiFi.softAPIP().toString().c_str());
      } else {
        LOG_WARNING("Captive DNS failed to start");
      }
    } else if (!wanted && _dnsRunning) {
      _dnsServer.stop();
      _dnsRunning = false;
      LOG_DEBUG("Captive DNS stopped");
    }
  }

  // Brings the radio in line with what the state machine decided.
  //
  // The split matters: the pure library owns WHETHER an AP should exist (_provisioning
  // .apRaised), this file owns whether one actually does (_apRaised). Nothing else may
  // set either, or the two drift.
  //
  // Reconciling like this rather than polling shouldRaiseAp() is what makes AP_ASSIST
  // work at all. onEvent(STA_ATTEMPT_FAILED) sets Context.apRaised itself when it moves
  // to AP_ASSIST, and shouldRaiseAp() is documented UNPROVISIONED-only and returns false
  // once apRaised is set - so a predicate-polling caller would never raise the radio for
  // a device that lost its network. It would sit silent and unreachable, with main.cpp
  // blocked before CustomServer::begin(), recoverable only by physical access.
  static void _reconcileApWithState()
  {
    if (_provisioning.apRaised && !_apRaised) {
      // Retried on every tick if it fails: refusing to bring up the AP is exactly the
      // state that leaves the device unreachable, so it must not be a one-shot attempt.
      _raiseAp();
    } else if (!_provisioning.apRaised && _apRaised) {
      _tearDownAp();
    }
  }

  // Runs the decision predicates each loop, then makes the radio match. The only timed
  // window left is grace; the AP is otherwise up exactly while the device cannot be
  // reached over its own network, so this mostly just re-asserts that.
  static void _serviceApLifecycle()
  {
    uint64_t nowMs = millis64();

    if (_provisioning.apRaised && WifiProvisioning::shouldTearDownAp(_provisioning, nowMs)) {
      WifiProvisioning::tearDownAp(_provisioning, nowMs);
      _publishedState = _provisioning.state;
    } else if (WifiProvisioning::shouldRaiseAp(_provisioning, nowMs)) {
      // Covers both the first raise and any later one: if the AP is somehow down while the
      // device still cannot associate, this puts it back rather than leaving it dark.
      WifiProvisioning::raiseAp(_provisioning, nowMs);
      _publishedState = _provisioning.state;
    }

    _reconcileApWithState();
    _serviceDns();
  }

  // Evaluates a disconnect grace window once it expires. This is the non-blocking
  // replacement for the delay(WIFI_DISCONNECT_DELAY) that used to sit inline in the
  // WIFI_EVENT_DISCONNECTED case and stall the task for 15 s per failed attempt,
  // freezing AP client events, the DNS lifecycle and every timer along with it.
  static void _serviceDisconnectDeadline()
  {
    if (_disconnectDeadlineMs == 0) return;
    if (millis64() < _disconnectDeadlineMs) return;

    _disconnectDeadlineMs = 0; // Disarm first: exactly one evaluation per armed window

    // Auto-reconnect may have restored the link during the window
    if (isFullyConnected()) return;

    _reconnectAttempts++;
    _lastReconnectAttempt = millis64();
    _feedProvisioning(WifiProvisioning::Event::STA_ATTEMPT_FAILED);

    LOG_WARNING("Auto-reconnect failed, attempt %d", _reconnectAttempts);

    // No portal fallback any more. Repeated failures now feed the AP-raise predicate,
    // and _serviceApLifecycle() raises the SoftAP so the device can be re-provisioned
    // from its own web interface. It no longer restarts itself out of a bad network.
  }

  static bool _isPowerReset()
  {
    // Check if the reset reason indicates a power-related event
    // ESP_RST_POWERON: Power on reset (cold boot)
    // ESP_RST_BROWNOUT: Brownout reset (power supply voltage dropped below minimum)
    esp_reset_reason_t resetReason = esp_reset_reason();
    return resetReason == ESP_RST_POWERON || resetReason == ESP_RST_BROWNOUT;
  }

  // Load the SoftAP password used by the WiFi config portal. Prefers the factory-provisioned
  // value (NVS factory_ns::ap_password); falls back to DEVICE_ID, which is already part of
  // the portal SSID so users connecting to the AP can read it off the network name.
  // DEVICE_ID is 12 hex chars, which satisfies WPA2's 8-character minimum.
  static void _resolveApPassword(char* out, size_t outSize)
  {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';

    Preferences prefs;
    if (prefs.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
      prefs.getString(FACTORY_KEY_AP_PASSWORD, out, outSize);
      prefs.end();
    }

    if (strlen(out) < 8) {
      snprintf(out, outSize, "%s", DEVICE_ID);
    }
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
    
    doc["hashed_device_id"] = hashedDeviceId; // Replace plain ID with hashed value
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
    LOG_DEBUG("Starting WiFi task with %d bytes stack", WIFI_TASK_STACK_SIZE);

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

  // ============================================================================
  // Network configuration (static IP)
  // ============================================================================

  static bool _isValidIpv4(const char* str, bool allowZero)
  {
    if (str == nullptr || str[0] == '\0') return false;
    IPAddress addr;
    if (!addr.fromString(str)) return false;
    if (!allowZero && addr == IPAddress(0, 0, 0, 0)) return false;
    return true;
  }

  bool getConfiguration(WifiConfiguration &config)
  {
    if (!acquireMutex(&_configMutex)) {
      LOG_ERROR("Failed to acquire configuration mutex for getConfiguration");
      return false;
    }
    config = _configuration;
    releaseMutex(&_configMutex);
    return true;
  }

  bool setConfiguration(const WifiConfiguration &config)
  {
    if (!_validateConfiguration(config)) {
      LOG_WARNING("Refusing to set invalid network configuration");
      return false;
    }

    if (!createMutexIfNeeded(&_configMutex)) return false;
    if (!acquireMutex(&_configMutex)) {
      LOG_ERROR("Failed to acquire configuration mutex for setConfiguration");
      return false;
    }
    _configuration = config;
    releaseMutex(&_configMutex);

    // NVS write runs unlocked - copy was already taken under the mutex
    _saveConfigurationToPreferences(config);

    LOG_DEBUG("Network configuration set");
    return true;
  }

  bool resetConfiguration()
  {
    LOG_DEBUG("Resetting network configuration to default");
    WifiConfiguration defaultConfig;
    if (!setConfiguration(defaultConfig)) {
      LOG_ERROR("Failed to reset network configuration");
      return false;
    }
    _setStaticBootFails(0); // Clean slate - default config is DHCP anyway
    LOG_INFO("Network configuration reset to default");
    return true;
  }

  bool getConfigurationAsJson(JsonDocument &jsonDocument)
  {
    WifiConfiguration config;
    if (!getConfiguration(config)) return false;
    configurationToJson(config, jsonDocument);
    return true;
  }

  bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
  {
    WifiConfiguration config;
    getConfiguration(config);

    if (!configurationFromJson(jsonDocument, config, partial)) {
      LOG_ERROR("Failed to set network configuration from JSON");
      return false;
    }

    if (!setConfiguration(config)) return false;

    // A user-saved config is the escape hatch: give the (possibly new) static IP a fresh set of
    // boot attempts so the backstop doesn't keep it disabled from previous failures.
    _setStaticBootFails(0);
    return true;
  }

  void configurationToJson(const WifiConfiguration &config, JsonDocument &jsonDocument)
  {
    jsonDocument["useStaticIp"] = config.useStaticIp;
    jsonDocument["ip"] = JsonString(config.ip);
    jsonDocument["gateway"] = JsonString(config.gateway);
    jsonDocument["subnet"] = JsonString(config.subnet);
    jsonDocument["dns1"] = JsonString(config.dns1);
    jsonDocument["dns2"] = JsonString(config.dns2);
    jsonDocument["fallbackToDhcp"] = config.fallbackToDhcp;
  }

  bool configurationFromJson(JsonDocument &jsonDocument, WifiConfiguration &config, bool partial)
  {
    if (!_validateJsonConfiguration(jsonDocument, partial)) {
      LOG_WARNING("Invalid network configuration JSON");
      return false;
    }

    if (jsonDocument["useStaticIp"].is<bool>())     config.useStaticIp = jsonDocument["useStaticIp"].as<bool>();
    if (jsonDocument["ip"].is<const char*>())       snprintf(config.ip, sizeof(config.ip), "%s", jsonDocument["ip"].as<const char*>());
    if (jsonDocument["gateway"].is<const char*>())  snprintf(config.gateway, sizeof(config.gateway), "%s", jsonDocument["gateway"].as<const char*>());
    if (jsonDocument["subnet"].is<const char*>())   snprintf(config.subnet, sizeof(config.subnet), "%s", jsonDocument["subnet"].as<const char*>());
    if (jsonDocument["dns1"].is<const char*>())     snprintf(config.dns1, sizeof(config.dns1), "%s", jsonDocument["dns1"].as<const char*>());
    if (jsonDocument["dns2"].is<const char*>())     snprintf(config.dns2, sizeof(config.dns2), "%s", jsonDocument["dns2"].as<const char*>());
    if (jsonDocument["fallbackToDhcp"].is<bool>()) config.fallbackToDhcp = jsonDocument["fallbackToDhcp"].as<bool>();

    // Cross-field validation on the merged result (catches e.g. useStaticIp=true with no IP via PATCH)
    return _validateConfiguration(config);
  }

  static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial)
  {
    if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>()) {
      LOG_WARNING("Invalid JSON document");
      return false;
    }

    if (partial) {
      // At least one valid field must be present
      if (jsonDocument["useStaticIp"].is<bool>())    return true;
      if (jsonDocument["ip"].is<const char*>())      return true;
      if (jsonDocument["gateway"].is<const char*>()) return true;
      if (jsonDocument["subnet"].is<const char*>())  return true;
      if (jsonDocument["dns1"].is<const char*>())    return true;
      if (jsonDocument["dns2"].is<const char*>())    return true;
      if (jsonDocument["fallbackToDhcp"].is<bool>()) return true;
      LOG_WARNING("No valid fields found in JSON document");
      return false;
    }

    // Full validation - all fields must be present and of the right type
    if (!jsonDocument["useStaticIp"].is<bool>())    { LOG_WARNING("useStaticIp field is not a boolean"); return false; }
    if (!jsonDocument["ip"].is<const char*>())      { LOG_WARNING("ip field is not a string"); return false; }
    if (!jsonDocument["gateway"].is<const char*>()) { LOG_WARNING("gateway field is not a string"); return false; }
    if (!jsonDocument["subnet"].is<const char*>())  { LOG_WARNING("subnet field is not a string"); return false; }
    if (!jsonDocument["dns1"].is<const char*>())    { LOG_WARNING("dns1 field is not a string"); return false; }
    if (!jsonDocument["dns2"].is<const char*>())    { LOG_WARNING("dns2 field is not a string"); return false; }
    if (!jsonDocument["fallbackToDhcp"].is<bool>()) { LOG_WARNING("fallbackToDhcp field is not a boolean"); return false; }
    return true;
  }

  static bool _validateConfiguration(const WifiConfiguration &config)
  {
    if (config.useStaticIp) {
      // IP, gateway and subnet are mandatory and must be non-zero; DNS servers are optional
      if (!_isValidIpv4(config.ip, false))      { LOG_WARNING("Static IP enabled but 'ip' is invalid"); return false; }
      if (!_isValidIpv4(config.gateway, false)) { LOG_WARNING("Static IP enabled but 'gateway' is invalid"); return false; }
      if (!_isValidIpv4(config.subnet, false))  { LOG_WARNING("Static IP enabled but 'subnet' is invalid"); return false; }
      if (config.dns1[0] != '\0' && !_isValidIpv4(config.dns1, true)) { LOG_WARNING("Invalid 'dns1' address"); return false; }
      if (config.dns2[0] != '\0' && !_isValidIpv4(config.dns2, true)) { LOG_WARNING("Invalid 'dns2' address"); return false; }
    }

    return true;
  }

  static void _loadConfiguration()
  {
    LOG_DEBUG("Loading network configuration from Preferences...");

    WifiConfiguration config; // Constructor sets all defaults

    Preferences preferences;
    if (preferences.begin(PREFERENCES_NAMESPACE_WIFI, true)) {
      config.useStaticIp = preferences.getBool(WIFI_CONFIG_USE_STATIC_KEY, WIFI_USE_STATIC_IP_DEFAULT);
      snprintf(config.ip, sizeof(config.ip), "%s", preferences.getString(WIFI_CONFIG_IP_KEY, "").c_str());
      snprintf(config.gateway, sizeof(config.gateway), "%s", preferences.getString(WIFI_CONFIG_GATEWAY_KEY, "").c_str());
      snprintf(config.subnet, sizeof(config.subnet), "%s", preferences.getString(WIFI_CONFIG_SUBNET_KEY, "").c_str());
      snprintf(config.dns1, sizeof(config.dns1), "%s", preferences.getString(WIFI_CONFIG_DNS1_KEY, "").c_str());
      snprintf(config.dns2, sizeof(config.dns2), "%s", preferences.getString(WIFI_CONFIG_DNS2_KEY, "").c_str());
      config.fallbackToDhcp = preferences.getBool(WIFI_CONFIG_FALLBACK_KEY, WIFI_FALLBACK_TO_DHCP_DEFAULT);
      preferences.end();
    } else {
      LOG_ERROR("Failed to open Preferences namespace for WiFi. Using default configuration");
    }

    // setConfiguration persists the (possibly default) values back to NVS, so missing keys
    // are materialized on first boot - a clean migration with no separate version flag.
    // If the stored values fail validation (corruption / firmware downgrade), fall back to
    // defaults so the config subsystem (and its mutex) is always initialized.
    if (!setConfiguration(config)) {
      LOG_WARNING("Stored network configuration invalid - reverting to defaults");
      WifiConfiguration defaultConfig;
      setConfiguration(defaultConfig);
    }

    LOG_DEBUG("Network configuration loaded");
  }

  static void _saveConfigurationToPreferences(const WifiConfiguration &config)
  {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_WIFI, false)) {
      LOG_ERROR("Failed to open Preferences namespace for WiFi");
      return;
    }

    preferences.putBool(WIFI_CONFIG_USE_STATIC_KEY, config.useStaticIp);
    preferences.putString(WIFI_CONFIG_IP_KEY, config.ip);
    preferences.putString(WIFI_CONFIG_GATEWAY_KEY, config.gateway);
    preferences.putString(WIFI_CONFIG_SUBNET_KEY, config.subnet);
    preferences.putString(WIFI_CONFIG_DNS1_KEY, config.dns1);
    preferences.putString(WIFI_CONFIG_DNS2_KEY, config.dns2);
    preferences.putBool(WIFI_CONFIG_FALLBACK_KEY, config.fallbackToDhcp);

    preferences.end();
    LOG_DEBUG("Network configuration saved to Preferences");
  }

  // Apply the network configuration to the radio. Must run after WiFi.mode() and before the
  // connection attempt. Static IP is set via WiFi.config(); when static IP is disabled we
  // leave the netif on DHCP (its boot default).
  static void _applyNetworkConfiguration()
  {
    WifiConfiguration config;
    if (!getConfiguration(config)) return;

    _staticIpApplied = false;

    if (!config.useStaticIp) {
      LOG_DEBUG("Using DHCP for IP configuration");
      return;
    }

    // Backstop: if the last few boots with this static IP all failed, ignore it and come up on
    // DHCP so the device is always recoverable. The counter is cleared once static is confirmed
    // healthy (_serviceStaticIpHealth) or the user saves a new config (setConfigurationFromJson).
    uint8_t bootFails = _getStaticBootFails();
    if (bootFails >= WIFI_STATIC_IP_MAX_BOOT_FAILS) {
      LOG_WARNING("Static IP disabled after %u failed boots - using DHCP (config kept, save again to retry)", bootFails);
      return;
    }

    IPAddress ip, gateway, subnet, dns1, dns2;
    if (!(ip.fromString(config.ip) && gateway.fromString(config.gateway) && subnet.fromString(config.subnet))) {
      LOG_WARNING("Static IP enabled but addresses invalid - using DHCP");
      return;
    }

    // DNS is mandatory for the radio to resolve hostnames (AWS/MQTT/NTP). When the user leaves it
    // blank, default the primary DNS to the gateway (routers commonly relay DNS) rather than 0.0.0.0,
    // which would silently break name resolution.
    if (!dns1.fromString(config.dns1)) dns1 = gateway;
    dns2.fromString(config.dns2); // Optional - stays 0.0.0.0 when empty

    // Count this attempt BEFORE applying, so a crash or lock-out mid-boot still accumulates.
    _setStaticBootFails(bootFails + 1);

    if (WiFi.config(ip, gateway, subnet, dns1, dns2)) {
      _staticIpApplied = true;
      LOG_INFO("Static IP configured: %s (gateway: %s, attempt %u)", config.ip, config.gateway, bootFails + 1);
    } else {
      LOG_ERROR("Failed to apply static IP configuration - falling back to DHCP");
    }
  }

  // Persistent count of consecutive static-IP boots that did not reach healthy connectivity.
  // Backs the boot-fail backstop in _applyNetworkConfiguration. Stored in wifi_ns.
  static uint8_t _getStaticBootFails()
  {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_WIFI, true)) return 0;
    uint8_t count = preferences.getUChar(WIFI_CONFIG_STATIC_FAILS_KEY, 0);
    preferences.end();
    return count;
  }

  static void _setStaticBootFails(uint8_t count)
  {
    Preferences preferences;
    if (!preferences.begin(PREFERENCES_NAMESPACE_WIFI, false)) {
      LOG_ERROR("Failed to open Preferences to update static boot-fail counter");
      return;
    }
    preferences.putUChar(WIFI_CONFIG_STATIC_FAILS_KEY, count);
    preferences.end();
  }

  // Services the two static-IP safety nets from the periodic WiFi check (runs only when a static IP
  // was actually applied this boot). Recovery is always by restart-onto-DHCP, never by live netif
  // reconfiguration (WiFi.config(0,0,0) races lwIP and asserts in ip4_route during the IP-less window).
  static void _serviceStaticIpHealth(bool internetReachable)
  {
    if (!_staticIpApplied) return; // DHCP boot, or static was skipped by the backstop - nothing to manage

    // Backstop clear: reaching a periodic check still connected means the static IP is past the early
    // crash/assoc window and is not a boot-loop offender. Clear the counter (internet-independent, so a
    // valid LAN-only static IP is never disabled). Gated on _staticIpApplied, so the DHCP recovery boot
    // (where static was skipped) never clears it - that would re-arm a known-bad static IP and oscillate.
    if (!_staticBackstopCleared) {
      _setStaticBootFails(0);
      _staticBackstopCleared = true;
      LOG_INFO("Static IP stable - boot-fail backstop counter cleared");
    }

    // DHCP auto-recovery: only when the user opted in, and only until the first successful check
    // (so a later internet blip on a good static IP can never reboot the device off it).
    if (_staticRecoveryResolved) return;

    WifiConfiguration config;
    if (!getConfiguration(config) || !config.fallbackToDhcp) return;

    if (internetReachable) {
      _staticRecoveryResolved = true; // Confirmed good - disarm for the rest of this boot
      return;
    }

    _staticInternetFailStreak++;
    if (_staticInternetFailStreak < WIFI_STATIC_IP_FAIL_STREAK) {
      LOG_WARNING("Static IP has no internet (%u/%u) - will recover on DHCP if it persists",
                  _staticInternetFailStreak, WIFI_STATIC_IP_FAIL_STREAK);
      return;
    }

    // Persistently unreachable: force the backstop so the next boot comes up on DHCP, then restart once.
    // The restart gate's minimum uptime is already satisfied by the time the periodic check fires.
    _staticRecoveryResolved = true;
    LOG_WARNING("Static IP has no internet - forcing DHCP and restarting to recover (static config kept in NVS)");
    _setStaticBootFails(WIFI_STATIC_IP_MAX_BOOT_FAILS);
    setRestartSystem("Static IP unreachable - recovering on DHCP");
  }

  TaskInfo getTaskInfo()
  {
    return getTaskInfoSafely(_wifiTaskHandle, WIFI_TASK_STACK_SIZE, &_heartbeat);
  }
}