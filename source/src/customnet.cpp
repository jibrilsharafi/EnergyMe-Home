// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "customnet.h"

#include <WiFi.h>

namespace CustomNet
{
    // Plain TCP connect over whatever interface holds the default route. The
    // CustomWifi probe cannot be reused when Ethernet carries the traffic: it
    // fails early on WiFi-specific pre-checks (STA association, WiFi gateway and
    // DNS) that are all legitimately absent on an Ethernet-only device.
    static bool _testConnectivityOverDefaultRoute()
    {
        WiFiClient client;
        client.setTimeout(CONNECTIVITY_TEST_TIMEOUT_MS);
        if (!client.connect(CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT)) {
            LOG_DEBUG("Connectivity test failed: cannot reach %s:%d over the default route",
                      CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT);
            return false;
        }
        client.stop();
        return true;
    }

    bool isFullyConnected(bool requireInternet)
    {
        bool ethUp = CustomEth::isServiceable();
        bool wifiUp = CustomWifi::isFullyConnected(false);
        if (!ethUp && !wifiUp) return false;
        if (!requireInternet) return true;
        // WiFi-only: keep the richer WiFi probe (gateway/DNS diagnostics) exactly
        // as before this change. With Ethernet up, probe the default route directly.
        return ethUp ? _testConnectivityOverDefaultRoute() : CustomWifi::testConnectivity();
    }

    bool isNetworkServiceable()
    {
        return isFullyConnected(false) || CustomWifi::isApServing();
    }
}
