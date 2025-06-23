#include "multiplexer.h"

Multiplexer::Multiplexer(int s0, int s1, int s2, int s3)
{
    _s0 = s0;
    _s1 = s1;
    _s2 = s2;
    _s3 = s3;
}

void Multiplexer::begin()
{
    pinMode(_s0, OUTPUT);
    pinMode(_s1, OUTPUT);
    pinMode(_s2, OUTPUT);
    pinMode(_s3, OUTPUT);

    // At boot, set the multiplexer to channel 1 (channel 0 of the multiplexer)
    // To avoid incorrect first readings 
    setChannel(CHANNEL_0);
}

void Multiplexer::setChannel(ChannelNumber channel)
{
    digitalWrite(_s0, channel & 0x01);
    digitalWrite(_s1, channel & 0x02);
    digitalWrite(_s2, channel & 0x04);
    digitalWrite(_s3, channel & 0x08);
}