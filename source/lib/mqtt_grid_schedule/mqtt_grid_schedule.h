// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure, dependency-free wall-clock alignment math for the grid MQTT publish
// cadence (see mqtt.cpp::_checkIfPublishGridNeeded / _publishGrid). Kept
// separate from mqtt.cpp so the boundary arithmetic is host-testable
// (see test/test_mqtt_grid_schedule).

namespace MqttGridSchedule {

// Returns the next unix-second boundary strictly after `nowUnixSecond` that is
// a multiple of `alignSeconds` (e.g. alignSeconds=60 -> the next top-of-minute).
// Always resyncs from the current time rather than a prior deadline, so a long
// gap (e.g. a reconnect after an outage) lands on a real wall-clock boundary
// instead of drifting from a stale increment.
uint64_t nextAlignedBoundarySeconds(uint64_t nowUnixSecond, uint32_t alignSeconds);

}  // namespace MqttGridSchedule
