#pragma once

#include <Arduino.h>

#include "constants.h"

class Led {
    public:
        // Constructor. redPin, greenPin, bluePin are the pins connected to the LED
        Led(int redPin, int greenPin, int bluePin, int brightness);
        // Initialize the LED
        void begin();

        // Set the brightness of the LED (0-255)
        void setBrightness(int brightness);
        int getBrightness();

        void setRed(bool force = false);
        void setGreen(bool force = false);
        void setBlue(bool force = false);
        void setYellow(bool force = false);
        void setPurple(bool force = false);
        void setCyan(bool force = false);
        void setOrange(bool force = false);
        void setWhite(bool force = false);
        
        void setOff(bool force = false);

        void block();
        void unblock();

    private:
        int _redValue = 0;
        int _greenValue = 0;
        int _blueValue = 0;

        int _redPin;
        int _greenPin;
        int _bluePin;
        
        int _brightness;

        int _redPinChannel = 1;
        int _greenPinChannel = 2;
        int _bluePinChannel = 3;

        bool _isBlocked = false;
        
        // Set the color of the LED to the specified RGB triplet
        void _setColor(int red, int green, int blue, bool force = false);
        // Set the PWM values of the LED
        void _setPwm();
};