#pragma once

#include <Arduino.h>

#include "structs.h"

namespace Multiplexer {
    void begin(
        int32_t s0Pin,
        int32_t s1Pin,
        int32_t s2Pin,
        int32_t s3Pin
    );
    // No need to stop anything here since once it executes at the beginning, there is no other use for this
    
    void setChannel(uint32_t channel);
}