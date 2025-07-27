// The LED functionality has been changed from an object to a namespace
// to simplify usage and avoid unnecessary object instantiation, since 
// it is (almost) stateless and only one LED will be used at a time anyway.

#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "constants.h"

#define PREFERENCES_BRIGHTNESS_KEY "brightness"

#define INVALID_PIN -1 // Used for initialization of pins
#define DEFAULT_LED_BRIGHTNESS 191 // 75% of the maximum brightness
#define LED_RESOLUTION 8 // Resolution for PWM, 8 bits (0-255)
#define LED_MAX_BRIGHTNESS 255 // 8-bit PWM
#define LED_FREQUENCY 5000 // Frequency for PWM, in Hz. Quite standard

namespace Led {
    void begin(int redPin, int greenPin, int bluePin);
    void resetToDefaults();

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
}