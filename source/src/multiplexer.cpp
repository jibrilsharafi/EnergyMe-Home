#include "multiplexer.h"

namespace Multiplexer {
    static int _s0Pin = -1;
    static int _s1Pin = -1;
    static int _s2Pin = -1;
    static int _s3Pin = -1;

    void begin(int s0Pin, int s1Pin, int s2Pin, int s3Pin) {
        _s0Pin = s0Pin;
        _s1Pin = s1Pin;
        _s2Pin = s2Pin;
        _s3Pin = s3Pin;

        pinMode(_s0Pin, OUTPUT);
        pinMode(_s1Pin, OUTPUT);
        pinMode(_s2Pin, OUTPUT);
        pinMode(_s3Pin, OUTPUT);

        // At boot, set the multiplexer to channel 1 (channel 0 of the multiplexer)
        // To avoid incorrect first readings 
        setChannel(0);
    }

    void setChannel(unsigned int channel) {
        if (channel < 0 || channel > MULTIPLEXER_CHANNEL_COUNT-1) {
            return;
        }

        // Truth table for the multiplexer
        // Enable pin (E) is always 0 at the hardware level
        // S0 S1 S2 S3  E CHANNEL
        //  X  X  X  X  1    None
        //  0  0  0  0  0       0
        //  1  0  0  0  0       1
        //  0  1  0  0  0       2
        //  1  1  0  0  0       3
        //  0  0  1  0  0       4
        //  1  0  1  0  0       5
        //  0  1  1  0  0       6
        //  1  1  1  0  0       7
        //  0  0  0  1  0       8
        //  1  0  0  1  0       9
        //  0  1  0  1  0       10
        //  1  1  0  1  0       11
        //  0  0  1  1  0       12
        //  1  0  1  1  0       13
        //  0  1  1  1  0       14
        //  1  1  1  1  0       15
        digitalWrite(_s0Pin, channel & 0x01);
        digitalWrite(_s1Pin, channel & 0x02);
        digitalWrite(_s2Pin, channel & 0x04);
        digitalWrite(_s3Pin, channel & 0x08);
    }
}