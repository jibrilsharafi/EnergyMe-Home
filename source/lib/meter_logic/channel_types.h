// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <cstdint>

// Shared channel-configuration types, deliberately free of any Arduino / FreeRTOS
// dependency so that the pure measurement and scheduling logic in meter_logic can
// be compiled and unit-tested on the host (see lib/meter_logic + test/).
//
// ChannelRole lives here (not in ade7953.h) so the host build never has to pull in
// Arduino.h just to know what "GRID" or "LOAD" means.

// Plain enum (not enum class) so it serializes directly to/from JSON as an integer.
enum ChannelRole : uint8_t {
    CHANNEL_ROLE_LOAD     = 0, // Default - regular load/consumption (negatives clamped to 0)
    CHANNEL_ROLE_GRID     = 1, // Grid meter (+ import, - export)
    CHANNEL_ROLE_PV       = 2, // PV/Solar production (+ generation, negatives clamped to 0)
    CHANNEL_ROLE_BATTERY  = 3, // Battery (+ discharge, - charge)
    CHANNEL_ROLE_INVERTER = 4, // Hybrid inverter: PV + battery DC-coupled, AC output (negatives allowed)
    CHANNEL_ROLE_COUNT    = 5  // Total number of roles
};

#define DEFAULT_CHANNEL_ROLE CHANNEL_ROLE_LOAD
#define DEFAULT_CHANNEL_0_ROLE CHANNEL_ROLE_GRID // Channel 0 is typically the grid meter
