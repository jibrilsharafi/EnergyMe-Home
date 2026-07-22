// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free decision for whether the energy MQTT snapshot should
// publish yet once the wall-clock boundary has been reached (see
// mqtt.cpp::_checkIfPublishEnergyNeeded). Kept separate from mqtt.cpp so the
// wait/deadline arithmetic is host-testable (see test/test_mqtt_energy_publish_gate).
//
// The publish is held past the boundary until every active channel's last
// reading is at or after it (so no point in the snapshot is actually
// leftover data from before the boundary), capped by a deadline so a single
// starved channel can never block the topic indefinitely.

namespace MqttEnergyPublishGate {

bool shouldPublishNow(uint64_t nowUnixSecond, uint64_t targetBoundaryUnixSecond,
                       uint32_t deadlineSeconds, bool allChannelsFreshSinceBoundary);

}  // namespace MqttEnergyPublishGate
