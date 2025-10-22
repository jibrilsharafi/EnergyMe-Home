// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

#include "structs.h"

#define INVALID_PIN 255

namespace Multiplexer {
    void begin(
        uint8_t s0Pin,
        uint8_t s1Pin,
        uint8_t s2Pin,
        uint8_t s3Pin
    );
    // No need to stop anything here since once it executes at the beginning, there is no other use for this
    
    void setChannel(uint8_t channel);
}