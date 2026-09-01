// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "custometh.h"

#include <ETH.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <lwip/dns.h>

#include "wifi_provisioning.h"

namespace CustomEth
{
    static TaskHeartbeat _heartbeat;
    static TaskHandle_t _ethTaskHandle = NULL;
    static volatile bool _stopRequested = false;

    static SemaphoreHandle_t _configMutex = NULL;
    static EthConfiguration _configuration;

    // Arbitration state. Written from the Network event task (ETH events), the
    // WiFi task (notifyStaState) and the eth task (applySwitch): every access
    // copies under the mutex and acts on the copy, per the FreeRTOS rules.
    static SemaphoreHandle_t _ctxMutex = NULL;
    static InterfaceArbitration::Context _arbCtx;

    static bool _enabled = false;          // Profile has Ethernet and begin() ran
    static bool _staticApplied = false;    // Static config actually applied this boot (not skipped by backstop)
    static bool _bootFailCounted = false;  // Backstop counter incremented this boot (at first link-up)
    static bool _backstopCleared = false;  // Counter cleared this boot after the stable window
    static uint64_t _serviceableSinceMs = 0; // For the lwIP settle delay and the backstop stable window; 0 while not serviceable

    #define ETH_MAX_INTERFACE_CALLBACKS 6
    static InterfaceChangeCallback _callbacks[ETH_MAX_INTERFACE_CALLBACKS] = {};
    static size_t _callbackCount = 0;

    static SPIClass* _spi = nullptr;

    // Private helpers
    static void _onNetworkEvent(arduino_event_id_t event);
    static void _ethTask(void *parameter);
    static void _loadConfiguration();
    static void _saveConfigurationToPreferences(const EthConfiguration &config);
    static void _applyStaticConfiguration();
    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial);
    static bool _validateConfiguration(const EthConfiguration &config);
    static bool _isValidIpv4(const char* str, bool allowZero);
    static uint8_t _getStaticBootFails();
    static void _setStaticBootFails(uint8_t count);
    static void _updateEthState(bool linkUp, bool hasAddress);
    static void _evaluateArbitration();
    static void _applyDnsForActiveInterface(InterfaceArbitration::Interface active);
    static void _notifyEthTask();

    bool begin()
    {
        if (!globalHwProfile->hasEthernet) {
            LOG_DEBUG("Product has no Ethernet - CustomEth disabled");
            return true;
        }
        if (_ethTaskHandle != NULL) {
            LOG_DEBUG("Ethernet task is already running");
            return true;
        }

        LOG_DEBUG("Starting Ethernet (W5500)...");

        if (!createMutexIfNeeded(&_ctxMutex)) return false;
        InterfaceArbitration::init(_arbCtx, millis64());

        _loadConfiguration();

        // Register before ETH.begin() so no event can be missed. The handler runs
        // in the core's Network event task: it only copies state under the mutex
        // and pokes the eth task, which does the actual work.
        Network.onEvent(_onNetworkEvent);

        // Same hostname as the WiFi interface: one device, one name in the DHCP lease table.
        char hostname[64];
        snprintf(hostname, sizeof(hostname), "%s-%s", "energyme-home", DEVICE_ID);
        ETH.setHostname(hostname);

        // Dedicated SPI bus (the ADE7953 owns the default SPI): no bus sharing,
        // no cross-task SPI arbitration needed.
        _spi = new SPIClass(HSPI);
        _spi->begin(globalHwProfile->ethSckPin, globalHwProfile->ethMisoPin,
                    globalHwProfile->ethMosiPin, globalHwProfile->ethCsPin);

        if (!ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR_W5500,
                       globalHwProfile->ethCsPin, globalHwProfile->ethIrqPin,
                       globalHwProfile->ethRstPin, *_spi, ETH_SPI_FREQ_MHZ)) {
            LOG_ERROR("W5500 initialization failed - Ethernet unavailable this boot");
            return false;
        }

        _applyStaticConfiguration();

        _stopRequested = false;
        BaseType_t created = xTaskCreate(_ethTask, ETH_TASK_NAME, ETH_TASK_STACK_SIZE, NULL, ETH_TASK_PRIORITY, &_ethTaskHandle);
        if (created != pdPASS) {
            LOG_ERROR("Failed to create Ethernet task");
            _ethTaskHandle = NULL;
            return false;
        }

        _enabled = true;
        LOG_INFO("Ethernet started (CS=%u IRQ=%u RST=%u)", globalHwProfile->ethCsPin,
                 globalHwProfile->ethIrqPin, globalHwProfile->ethRstPin);
        return true;
    }

    void stop()
    {
        if (_ethTaskHandle == NULL) return;
        _stopRequested = true;
        _notifyEthTask();
    }

    bool isEnabled() { return _enabled; }

    bool isLinkUp()
    {
        if (!_enabled) return false;
        if (!acquireMutex(&_ctxMutex)) return false;
        bool up = _arbCtx.ethLinkUp;
        releaseMutex(&_ctxMutex);
        return up;
    }

    bool isServiceable()
    {
        if (!_enabled) return false;
        if (!acquireMutex(&_ctxMutex)) return false;
        bool serviceable = InterfaceArbitration::isEthServiceable(_arbCtx);
        uint64_t since = _serviceableSinceMs;
        releaseMutex(&_ctxMutex);
        if (!serviceable) return false;
        // Same reasoning as WIFI_LWIP_STABILIZATION_DELAY: give lwIP a moment
        // after the address appears before services pile onto the netif.
        return since != 0 && (millis64() - since) >= ETH_LWIP_STABILIZATION_DELAY;
    }

    InterfaceArbitration::Interface activeInterface()
    {
        if (_ctxMutex == NULL) return InterfaceArbitration::Interface::NONE;
        if (!acquireMutex(&_ctxMutex)) return InterfaceArbitration::Interface::NONE;
        InterfaceArbitration::Interface active = _arbCtx.active;
        releaseMutex(&_ctxMutex);
        return active;
    }

    void notifyStaState(bool connected)
    {
        if (_ctxMutex == NULL) return; // Home, or before begin(): nothing to arbitrate
        if (!acquireMutex(&_ctxMutex)) return;
        InterfaceArbitration::onStaState(_arbCtx, connected, millis64());
        releaseMutex(&_ctxMutex);
        _notifyEthTask();
    }

    bool onInterfaceChange(InterfaceChangeCallback callback)
    {
        if (callback == nullptr || _callbackCount >= ETH_MAX_INTERFACE_CALLBACKS) return false;
        _callbacks[_callbackCount++] = callback;
        return true;
    }

    // ------------------------------------------------------------------
    // Events and arbitration
    // ------------------------------------------------------------------

    static void _onNetworkEvent(arduino_event_id_t event)
    {
        switch (event) {
            case ARDUINO_EVENT_ETH_CONNECTED:
                // Backstop accounting: count the attempt at first link-up, so a
                // crash caused by the static config still accumulates, while a
                // cable-out boot (no link ever) stays neutral per the spec.
                if (_staticApplied && !_bootFailCounted) {
                    _bootFailCounted = true;
                    _setStaticBootFails((uint8_t)(_getStaticBootFails() + 1));
                }
                _updateEthState(true, ETH.hasIP());
                break;
            case ARDUINO_EVENT_ETH_GOT_IP:
                _updateEthState(true, true);
                break;
            case ARDUINO_EVENT_ETH_LOST_IP:
                _updateEthState(ETH.linkUp(), false);
                break;
            case ARDUINO_EVENT_ETH_DISCONNECTED:
            case ARDUINO_EVENT_ETH_STOP:
                _updateEthState(false, false);
                break;
            default:
                break;
        }
    }

    static void _updateEthState(bool linkUp, bool hasAddress)
    {
        if (!acquireMutex(&_ctxMutex)) return;
        bool wasServiceable = InterfaceArbitration::isEthServiceable(_arbCtx);
        InterfaceArbitration::onEthState(_arbCtx, linkUp, hasAddress, millis64());
        bool nowServiceable = InterfaceArbitration::isEthServiceable(_arbCtx);
        if (nowServiceable && !wasServiceable) _serviceableSinceMs = millis64();
        else if (!nowServiceable) _serviceableSinceMs = 0;
        releaseMutex(&_ctxMutex);
        _notifyEthTask();
    }

    static void _notifyEthTask()
    {
        if (_ethTaskHandle != NULL) xTaskNotifyGive(_ethTaskHandle);
    }

    static void _ethTask(void *parameter)
    {
        (void)parameter;
        LOG_DEBUG("Ethernet task started");

        uint32_t loops = 0;
        while (!_stopRequested && loops < MAX_LOOP_ITERATIONS * 1000000UL) {
            loops++;
            TASK_HEARTBEAT(_heartbeat);

            _evaluateArbitration();

            // Backstop clear: the static config has held the interface serviceable
            // past the crash/misconfig window, so it is not a boot-loop offender.
            if (_staticApplied && !_backstopCleared) {
                if (acquireMutex(&_ctxMutex)) {
                    uint64_t since = _serviceableSinceMs;
                    releaseMutex(&_ctxMutex);
                    if (since != 0 && (millis64() - since) >= ETH_STATIC_STABLE_CLEAR_MS) {
                        _setStaticBootFails(0);
                        _backstopCleared = true;
                        LOG_INFO("Static Ethernet IP stable - boot-fail backstop counter cleared");
                    }
                }
            }

            // Event-driven with a tick fallback: events poke us immediately, the
            // timeout re-evaluates timers (hold-down, stable window).
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ETH_TASK_TICK_MS));
        }

        LOG_DEBUG("Ethernet task stopping");
        _ethTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    static void _evaluateArbitration()
    {
        if (!acquireMutex(&_ctxMutex)) return;
        InterfaceArbitration::Decision decision = InterfaceArbitration::evaluate(_arbCtx, millis64());
        InterfaceArbitration::Interface previous = _arbCtx.active;
        if (decision.switchRequired) InterfaceArbitration::applySwitch(_arbCtx, decision.preferred);
        releaseMutex(&_ctxMutex);

        if (!decision.switchRequired) return;

        LOG_INFO("Default network interface: %s -> %s",
                 InterfaceArbitration::interfaceName(previous),
                 InterfaceArbitration::interfaceName(decision.preferred));

        // Route change first, then DNS, then the consumers drop their sessions
        // so they reconnect on the new path immediately.
        if (decision.preferred == InterfaceArbitration::Interface::ETHERNET) {
            Network.setDefaultInterface(ETH);
        } else if (decision.preferred == InterfaceArbitration::Interface::WIFI_STATION) {
            Network.setDefaultInterface(WiFi.STA);
        }

        _applyDnsForActiveInterface(decision.preferred);

        for (size_t i = 0; i < _callbackCount; i++) {
            if (_callbacks[i] != nullptr) _callbacks[i](decision.preferred);
        }
    }

    // lwIP's DNS server list is global, not per-netif: after a failover the
    // resolver keeps the OLD interface's servers (fatal when a static ETH DNS
    // sits on a now-unreachable segment). Re-apply the new interface's servers.
    static void _applyDnsForActiveInterface(InterfaceArbitration::Interface active)
    {
        IPAddress dns1(0, 0, 0, 0), dns2(0, 0, 0, 0);
        if (active == InterfaceArbitration::Interface::ETHERNET) {
            dns1 = ETH.dnsIP(0);
            dns2 = ETH.dnsIP(1);
        } else if (active == InterfaceArbitration::Interface::WIFI_STATION) {
            dns1 = WiFi.STA.dnsIP(0);
            dns2 = WiFi.STA.dnsIP(1);
        } else {
            return;
        }

        for (int i = 0; i < 2; i++) {
            IPAddress dns = (i == 0) ? dns1 : dns2;
            if (dns == IPAddress(0, 0, 0, 0)) continue;
            ip_addr_t addr;
            addr.type = IPADDR_TYPE_V4;
            addr.u_addr.ip4.addr = (uint32_t)dns;
            dns_setserver((u8_t)i, &addr);
        }
        LOG_DEBUG("DNS reapplied for %s: %s / %s", InterfaceArbitration::interfaceName(active),
                  dns1.toString().c_str(), dns2.toString().c_str());
    }

    // ------------------------------------------------------------------
    // Configuration (mirrors the CustomWifi config module pattern)
    // ------------------------------------------------------------------

    bool getConfiguration(EthConfiguration &config)
    {
        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for getConfiguration");
            return false;
        }
        config = _configuration;
        releaseMutex(&_configMutex);
        return true;
    }

    bool setConfiguration(const EthConfiguration &config)
    {
        if (!_validateConfiguration(config)) {
            LOG_WARNING("Refusing to set invalid Ethernet configuration");
            return false;
        }

        if (!createMutexIfNeeded(&_configMutex)) return false;
        if (!acquireMutex(&_configMutex)) {
            LOG_ERROR("Failed to acquire configuration mutex for setConfiguration");
            return false;
        }
        _configuration = config;
        releaseMutex(&_configMutex);

        _saveConfigurationToPreferences(config);

        LOG_DEBUG("Ethernet configuration set");
        return true;
    }

    bool resetConfiguration()
    {
        LOG_DEBUG("Resetting Ethernet configuration to default");
        EthConfiguration defaultConfig;
        if (!setConfiguration(defaultConfig)) {
            LOG_ERROR("Failed to reset Ethernet configuration");
            return false;
        }
        _setStaticBootFails(0); // Clean slate - default config is DHCP anyway
        LOG_INFO("Ethernet configuration reset to default");
        return true;
    }

    bool getConfigurationAsJson(JsonDocument &jsonDocument)
    {
        EthConfiguration config;
        if (!getConfiguration(config)) return false;
        configurationToJson(config, jsonDocument);
        return true;
    }

    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial)
    {
        EthConfiguration config;
        getConfiguration(config);

        if (!configurationFromJson(jsonDocument, config, partial)) {
            LOG_ERROR("Failed to set Ethernet configuration from JSON");
            return false;
        }

        if (!setConfiguration(config)) return false;

        // A user-saved config is the escape hatch: fresh boot attempts for the
        // (possibly new) static IP so the backstop doesn't keep it disabled.
        _setStaticBootFails(0);
        return true;
    }

    void configurationToJson(const EthConfiguration &config, JsonDocument &jsonDocument)
    {
        jsonDocument["useStaticIp"] = config.useStaticIp;
        jsonDocument["ip"] = JsonString(config.ip);
        jsonDocument["gateway"] = JsonString(config.gateway);
        jsonDocument["subnet"] = JsonString(config.subnet);
        jsonDocument["dns1"] = JsonString(config.dns1);
        jsonDocument["dns2"] = JsonString(config.dns2);
    }

    bool configurationFromJson(JsonDocument &jsonDocument, EthConfiguration &config, bool partial)
    {
        if (!_validateJsonConfiguration(jsonDocument, partial)) {
            LOG_WARNING("Invalid Ethernet configuration JSON");
            return false;
        }

        if (jsonDocument["useStaticIp"].is<bool>())    config.useStaticIp = jsonDocument["useStaticIp"].as<bool>();
        if (jsonDocument["ip"].is<const char*>())      snprintf(config.ip, sizeof(config.ip), "%s", jsonDocument["ip"].as<const char*>());
        if (jsonDocument["gateway"].is<const char*>()) snprintf(config.gateway, sizeof(config.gateway), "%s", jsonDocument["gateway"].as<const char*>());
        if (jsonDocument["subnet"].is<const char*>())  snprintf(config.subnet, sizeof(config.subnet), "%s", jsonDocument["subnet"].as<const char*>());
        if (jsonDocument["dns1"].is<const char*>())    snprintf(config.dns1, sizeof(config.dns1), "%s", jsonDocument["dns1"].as<const char*>());
        if (jsonDocument["dns2"].is<const char*>())    snprintf(config.dns2, sizeof(config.dns2), "%s", jsonDocument["dns2"].as<const char*>());

        return _validateConfiguration(config);
    }

    void getStatusAsJson(JsonDocument &jsonDocument)
    {
        jsonDocument["enabled"] = _enabled;
        jsonDocument["activeInterface"] = InterfaceArbitration::interfaceName(activeInterface());
        if (!_enabled) return;

        jsonDocument["linkUp"] = isLinkUp();
        jsonDocument["serviceable"] = isServiceable();
        jsonDocument["staticApplied"] = _staticApplied;
        jsonDocument["ip"] = ETH.localIP().toString();
        jsonDocument["gateway"] = ETH.gatewayIP().toString();
        jsonDocument["subnet"] = ETH.subnetMask().toString();
        jsonDocument["dns1"] = ETH.dnsIP(0).toString();
        jsonDocument["dns2"] = ETH.dnsIP(1).toString();
        jsonDocument["mac"] = ETH.macAddress();
    }

    static void _loadConfiguration()
    {
        LOG_DEBUG("Loading Ethernet configuration from Preferences...");

        EthConfiguration config; // Constructor sets defaults (DHCP)

        // Read-only open fails when eth_ns does not exist yet: that is the lazy-
        // creation contract (the namespace is only ever created by a write), so
        // fall through to defaults silently rather than materializing it here.
        Preferences preferences;
        if (preferences.begin(PREFERENCES_NAMESPACE_ETH, true)) {
            config.useStaticIp = preferences.getBool(ETH_CONFIG_USE_STATIC_KEY, false);
            snprintf(config.ip, sizeof(config.ip), "%s", preferences.getString(ETH_CONFIG_IP_KEY, "").c_str());
            snprintf(config.gateway, sizeof(config.gateway), "%s", preferences.getString(ETH_CONFIG_GATEWAY_KEY, "").c_str());
            snprintf(config.subnet, sizeof(config.subnet), "%s", preferences.getString(ETH_CONFIG_SUBNET_KEY, "").c_str());
            snprintf(config.dns1, sizeof(config.dns1), "%s", preferences.getString(ETH_CONFIG_DNS1_KEY, "").c_str());
            snprintf(config.dns2, sizeof(config.dns2), "%s", preferences.getString(ETH_CONFIG_DNS2_KEY, "").c_str());
            preferences.end();
        }

        if (!createMutexIfNeeded(&_configMutex)) return;
        if (acquireMutex(&_configMutex)) {
            if (_validateConfiguration(config)) {
                _configuration = config;
            } else {
                LOG_WARNING("Stored Ethernet configuration invalid - reverting to defaults (DHCP)");
                _configuration = EthConfiguration();
            }
            releaseMutex(&_configMutex);
        }

        LOG_DEBUG("Ethernet configuration loaded");
    }

    static void _saveConfigurationToPreferences(const EthConfiguration &config)
    {
        // First write creates eth_ns (lazy) - this never runs on a Home device
        // because the endpoints are product-gated and begin() no-ops there.
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ETH, false)) {
            LOG_ERROR("Failed to open Preferences namespace for Ethernet");
            return;
        }

        preferences.putBool(ETH_CONFIG_USE_STATIC_KEY, config.useStaticIp);
        preferences.putString(ETH_CONFIG_IP_KEY, config.ip);
        preferences.putString(ETH_CONFIG_GATEWAY_KEY, config.gateway);
        preferences.putString(ETH_CONFIG_SUBNET_KEY, config.subnet);
        preferences.putString(ETH_CONFIG_DNS1_KEY, config.dns1);
        preferences.putString(ETH_CONFIG_DNS2_KEY, config.dns2);

        preferences.end();
        LOG_DEBUG("Ethernet configuration saved to Preferences");
    }

    static void _applyStaticConfiguration()
    {
        EthConfiguration config;
        if (!getConfiguration(config)) return;

        _staticApplied = false;

        if (!config.useStaticIp) {
            LOG_DEBUG("Using DHCP for Ethernet IP configuration");
            return;
        }

        uint8_t bootFails = _getStaticBootFails();
        if (bootFails >= ETH_STATIC_MAX_BOOT_FAILS) {
            LOG_WARNING("Static Ethernet IP disabled after %u failed boots - using DHCP (config kept, save again to retry)", bootFails);
            return;
        }

        IPAddress ip, gateway, subnet, dns1, dns2;
        if (!(ip.fromString(config.ip) && gateway.fromString(config.gateway) && subnet.fromString(config.subnet))) {
            LOG_WARNING("Static Ethernet IP enabled but addresses invalid - using DHCP");
            return;
        }
        if (!dns1.fromString(config.dns1)) dns1 = gateway; // Routers commonly relay DNS
        dns2.fromString(config.dns2); // Optional - stays 0.0.0.0 when empty

        if (ETH.config(ip, gateway, subnet, dns1, dns2)) {
            _staticApplied = true;
            LOG_INFO("Static Ethernet IP configured: %s (gateway: %s, attempt %u)", config.ip, config.gateway, bootFails + 1);
        } else {
            LOG_ERROR("Failed to apply static Ethernet IP configuration - falling back to DHCP");
        }
    }

    static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial)
    {
        if (jsonDocument.isNull() || !jsonDocument.is<JsonObject>()) {
            LOG_WARNING("Invalid JSON document");
            return false;
        }

        if (partial) {
            if (jsonDocument["useStaticIp"].is<bool>())    return true;
            if (jsonDocument["ip"].is<const char*>())      return true;
            if (jsonDocument["gateway"].is<const char*>()) return true;
            if (jsonDocument["subnet"].is<const char*>())  return true;
            if (jsonDocument["dns1"].is<const char*>())    return true;
            if (jsonDocument["dns2"].is<const char*>())    return true;
            LOG_WARNING("No valid fields found in JSON document");
            return false;
        }

        if (!jsonDocument["useStaticIp"].is<bool>())    { LOG_WARNING("useStaticIp field is not a boolean"); return false; }
        if (!jsonDocument["ip"].is<const char*>())      { LOG_WARNING("ip field is not a string"); return false; }
        if (!jsonDocument["gateway"].is<const char*>()) { LOG_WARNING("gateway field is not a string"); return false; }
        if (!jsonDocument["subnet"].is<const char*>())  { LOG_WARNING("subnet field is not a string"); return false; }
        if (!jsonDocument["dns1"].is<const char*>())    { LOG_WARNING("dns1 field is not a string"); return false; }
        if (!jsonDocument["dns2"].is<const char*>())    { LOG_WARNING("dns2 field is not a string"); return false; }
        return true;
    }

    static bool _validateConfiguration(const EthConfiguration &config)
    {
        if (!config.useStaticIp) return true;

        if (!_isValidIpv4(config.ip, false))      { LOG_WARNING("Static IP enabled but 'ip' is invalid"); return false; }
        if (!_isValidIpv4(config.gateway, false)) { LOG_WARNING("Static IP enabled but 'gateway' is invalid"); return false; }
        if (!_isValidIpv4(config.subnet, false))  { LOG_WARNING("Static IP enabled but 'subnet' is invalid"); return false; }
        if (config.dns1[0] != '\0' && !_isValidIpv4(config.dns1, true)) { LOG_WARNING("Invalid 'dns1' address"); return false; }
        if (config.dns2[0] != '\0' && !_isValidIpv4(config.dns2, true)) { LOG_WARNING("Invalid 'dns2' address"); return false; }

        // A static address inside the recovery SoftAP's default subnet would route
        // ambiguously exactly when the AP is most needed. Only the default candidate
        // is rejected outright: the AP subnet selection avoids the other candidates
        // dynamically (it accounts for the ETH subnet like it does for STA).
        IPAddress ip;
        ip.fromString(config.ip);
        WifiProvisioning::Subnet apDefault = WifiProvisioning::candidateSubnet(0);
        uint32_t ipHost = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
        if (WifiProvisioning::subnetsOverlap(ipHost, apDefault.cidr, apDefault.address, apDefault.cidr)) {
            LOG_WARNING("Static IP %s overlaps the recovery access point subnet - rejected", config.ip);
            return false;
        }

        return true;
    }

    static bool _isValidIpv4(const char* str, bool allowZero)
    {
        if (str == nullptr || str[0] == '\0') return false;
        IPAddress addr;
        if (!addr.fromString(str)) return false;
        if (!allowZero && addr == IPAddress(0, 0, 0, 0)) return false;
        return true;
    }

    static uint8_t _getStaticBootFails()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ETH, true)) return 0;
        uint8_t count = preferences.getUChar(ETH_CONFIG_STATIC_FAILS_KEY, 0);
        preferences.end();
        return count;
    }

    static void _setStaticBootFails(uint8_t count)
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_ETH, false)) {
            LOG_ERROR("Failed to open Preferences to update static boot-fail counter");
            return;
        }
        preferences.putUChar(ETH_CONFIG_STATIC_FAILS_KEY, count);
        preferences.end();
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_ethTaskHandle, ETH_TASK_STACK_SIZE, &_heartbeat);
    }
}
