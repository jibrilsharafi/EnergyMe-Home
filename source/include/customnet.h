// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include "custometh.h"
#include "customwifi.h"

// Interface-agnostic network readiness. Every service gated on "the network is
// up" (MQTT, NTP, InfluxDB, telemetry, log, health check, Modbus) asks HERE,
// not CustomWifi, so Ethernet counts the moment it exists. On Home the Ethernet
// side is permanently false and these reduce exactly to the CustomWifi answers.

namespace CustomNet
{
    // A station-side interface is fully usable: Ethernet serviceable, or STA
    // associated with an address and past the lwIP settle window. With
    // requireInternet, additionally proves upstream reachability over whichever
    // interface holds the default route.
    bool isFullyConnected(bool requireInternet = false);

    // "Can anyone reach this device": a station-side interface is up OR the
    // recovery SoftAP is serving. This is what the health check and the boot
    // wait gate on - a device serving on the AP is working as intended.
    bool isNetworkServiceable();
}
