// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "customnet.h"

namespace CustomNet
{
    bool isFullyConnected(bool requireInternet)
    {
        bool ethUp = CustomEth::isServiceable();
        bool wifiUp = CustomWifi::isFullyConnected(false);
        if (!ethUp && !wifiUp) return false;
        if (!requireInternet) return true;
        // WiFi-only: keep the richer WiFi probe (gateway/DNS diagnostics) exactly
        // as before this change. With Ethernet up that probe fails early on
        // WiFi-specific pre-checks that are legitimately absent, so probe the
        // default route directly instead.
        return ethUp ? probeTcp(CONNECTIVITY_TEST_IP, CONNECTIVITY_TEST_PORT, CONNECTIVITY_TEST_TIMEOUT_MS)
                     : CustomWifi::testConnectivity();
    }

    bool isNetworkServiceable()
    {
        return isFullyConnected(false) || CustomWifi::isApServing();
    }
}
