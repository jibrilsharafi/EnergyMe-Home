#pragma once

#include <Arduino.h>

#include "structs.h"

class Multiplexer
{
public:
    Multiplexer(int s0, int s1, int s2, int s3);
    void begin();

    void setChannel(ChannelNumber channel);

private:
    int _s0;
    int _s1;
    int _s2;
    int _s3;
};