// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "mqtt_energy_publish_gate.h"

namespace MqttEnergyPublishGate {

bool shouldPublishNow(uint64_t nowUnixSecond, uint64_t targetBoundaryUnixSecond,
                       uint32_t deadlineSeconds, bool allChannelsFreshSinceBoundary) {
    if (nowUnixSecond < targetBoundaryUnixSecond) return false;
    if (allChannelsFreshSinceBoundary) return true;
    return nowUnixSecond >= targetBoundaryUnixSecond + deadlineSeconds;
}

}  // namespace MqttEnergyPublishGate
