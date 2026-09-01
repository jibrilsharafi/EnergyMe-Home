// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AdvancedLogger.h>
#include <ETH.h>

#include "constants.h"
#include "globals.h"
#include "hardware_profile.h"
#include "interface_arbitration.h"
#include "structs.h"
#include "utils.h"

// W5500 SPI Ethernet (Home Pro). Owns the dedicated SPI bus, the lwIP netif,
// the persisted IP configuration (eth_ns) and the interface arbitration between
// Ethernet and WiFi STA. On products whose profile declares no Ethernet, begin()
// is a no-op and every predicate reports "not serviceable" - Home behavior is
// untouched.

#define ETH_TASK_NAME "eth_task"
#define ETH_TASK_STACK_SIZE (6 * 1024) // Logs + Network API calls; sized like the other logging tasks (4-8 KB)
#define ETH_TASK_PRIORITY 5
#define ETH_TASK_TICK_MS 1000          // Arbitration/backstop evaluation cadence

#define ETH_PHY_ADDR_W5500 1
#define ETH_SPI_FREQ_MHZ 25            // W5500 tolerates more, but 25 MHz matches the module crystal and is proven territory

// The static config must survive long enough after link-up to be declared good.
// Same philosophy as the WiFi static-IP backstop: clear the boot-fail counter
// only once the interface has proven itself, not at apply time.
#define ETH_STATIC_STABLE_CLEAR_MS (60 * 1000)
#define ETH_STATIC_MAX_BOOT_FAILS 3

// lwIP settle time after the interface obtains an address, mirroring
// WIFI_LWIP_STABILIZATION_DELAY: services connecting in the same tick the
// address appears have crashed on WiFi before; assume ETH deserves the same.
#define ETH_LWIP_STABILIZATION_DELAY (1 * 1000)

// Early-boot DHCP grace: with the link up but no address yet, the recovery AP
// raise is held back this long from boot so a normally-leasing network never
// sees an AP blip on a zero-touch first boot. After the window, link-without-
// address counts as unreachable and the AP may rise.
#define ETH_LINK_DHCP_GRACE_MS (15 * 1000)

// Preferences keys (eth_ns). Max 15 chars each. Namespace is created lazily on
// first write so it never exists on a Home device; factory reset and config
// backup handle it generically (clear-if-present / include-if-present).
#define ETH_CONFIG_USE_STATIC_KEY "useStatic"
#define ETH_CONFIG_IP_KEY "ip"
#define ETH_CONFIG_GATEWAY_KEY "gateway"
#define ETH_CONFIG_SUBNET_KEY "subnet"
#define ETH_CONFIG_DNS1_KEY "dns1"
#define ETH_CONFIG_DNS2_KEY "dns2"
#define ETH_CONFIG_STATIC_FAILS_KEY "staticFails"

#define PREFERENCES_NAMESPACE_ETH "eth_ns"

// Ethernet IP configuration. Stored in NVS (eth_ns), applied at boot.
// Addresses stored as strings for easy validation, like WifiConfiguration.
struct EthConfiguration {
    bool useStaticIp;
    char ip[IP_ADDRESS_BUFFER_SIZE];
    char gateway[IP_ADDRESS_BUFFER_SIZE];
    char subnet[IP_ADDRESS_BUFFER_SIZE];
    char dns1[IP_ADDRESS_BUFFER_SIZE];
    char dns2[IP_ADDRESS_BUFFER_SIZE];

    EthConfiguration() : useStaticIp(false) {
        ip[0] = '\0';
        gateway[0] = '\0';
        subnet[0] = '\0';
        dns1[0] = '\0';
        dns2[0] = '\0';
    }
};

namespace CustomEth
{
    // Brings up the W5500 and the eth task. Returns true on products without
    // Ethernet (nothing to do) and false only on an actual bring-up failure.
    bool begin();
    void stop();

    // Profile says this product has Ethernet and begin() brought it up.
    bool isEnabled();

    bool isLinkUp();

    // Link up + valid address (DHCP lease or applied static) + lwIP settle time.
    bool isServiceable();

    // The interface currently holding the default route. WIFI_STA/NONE on Home.
    InterfaceArbitration::Interface activeInterface();

    // Feed from CustomWifi on every STA connect/disconnect so arbitration sees
    // both sides. Safe to call on any product; a no-op before begin() on Pro.
    void notifyStaState(bool connected);

    // Called after the default route moved to a different interface. Consumers
    // (MQTT, InfluxDB, time sync) register to drop-and-reconnect their sessions
    // instead of waiting out TCP keepalive on a dead path.
    typedef void (*InterfaceChangeCallback)(InterfaceArbitration::Interface newActive);
    bool onInterfaceChange(InterfaceChangeCallback callback);

    // Configuration management - direct struct operations
    bool getConfiguration(EthConfiguration &config);
    bool setConfiguration(const EthConfiguration &config); // Persists to NVS; caller restarts to apply
    bool resetConfiguration();

    // Configuration management - JSON operations
    bool getConfigurationAsJson(JsonDocument &jsonDocument);
    bool setConfigurationFromJson(JsonDocument &jsonDocument, bool partial = false);
    void configurationToJson(const EthConfiguration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, EthConfiguration &config, bool partial = false);

    // Status: link, addressing mode, ip/gateway/subnet/dns, MAC, active interface.
    void getStatusAsJson(JsonDocument &jsonDocument);

    // Task information
    TaskInfo getTaskInfo();
}
