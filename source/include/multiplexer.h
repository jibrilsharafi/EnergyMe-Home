#pragma once

#include <Arduino.h>

#include "structs.h"

namespace Multiplexer {
    void begin(
        int s0Pin,
        int s1Pin,
        int s2Pin,
        int s3Pin
    );
    // No need to stop anything here since once it executes at the beginning, there is no other use for this
    
    void setChannel(unsigned int channel);
}