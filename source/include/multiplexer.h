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
    
    void setChannel(unsigned int channel);
}