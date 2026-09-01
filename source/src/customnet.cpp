// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "customnet.h"

namespace CustomNet
{
    bool isFullyConnected(bool requireInternet)
    {
        bool stationUp = CustomEth::isServiceable() || CustomWifi::isFullyConnected(false);
        if (!stationUp) return false;
        // The connectivity probe is a plain TCP connect over the default route,
        // so it tests whichever interface is actually carrying traffic.
        if (requireInternet) return CustomWifi::testConnectivity();
        return true;
    }

    bool isNetworkServiceable()
    {
        return isFullyConnected(false) || CustomWifi::isApServing();
    }
}
