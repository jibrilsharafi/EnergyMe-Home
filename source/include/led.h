#ifndef LED_H
#define LED_H

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

        void setRed(bool blocking = false);
        void setGreen(bool blocking = false);
        void setBlue(bool blocking = false);
        void setYellow(bool blocking = false);
        void setPurple(bool blocking = false);
        void setCyan(bool blocking = false);
        void setOrange(bool blocking = false);
        void setWhite(bool blocking = false);
        
        void setOff(bool blocking = false);

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
        void _setColor(int red, int green, int blue, bool blocking = false);
        // Set the PWM values of the LED
        void _setPwm();
};

#endif