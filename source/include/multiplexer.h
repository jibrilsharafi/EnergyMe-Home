#ifndef MULTIPLEXER_H
#define MULTIPLEXER_H

#include <Arduino.h>

#include "constants.h"
#include "utils.h"

extern AdvancedLogger logger;

class Multiplexer {
    public:
        // Constructor. s0-s3 are the pins connected to the multiplexer
        Multiplexer(int s0, int s1, int s2, int s3);
        // Initialize the multiplexer
        void begin();

        // Set the channel of the multiplexer (0-15)
        void setChannel(int channel);

    private:
        int _s0;
        int _s1;
        int _s2;
        int _s3;
};

#endif