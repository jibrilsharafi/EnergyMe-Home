// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#include "mqtt_grid_schedule.h"

namespace MqttGridSchedule {

uint64_t nextAlignedBoundarySeconds(uint64_t nowUnixSecond, uint32_t alignSeconds) {
    if (alignSeconds == 0) return nowUnixSecond;
    return ((nowUnixSecond / alignSeconds) + 1) * alignSeconds;
}

}  // namespace MqttGridSchedule
