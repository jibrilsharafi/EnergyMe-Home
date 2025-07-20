#include "led.h"

namespace Led {
    // Static variables to maintain state
    static int _redPin = -1;
    static int _greenPin = -1;
    static int _bluePin = -1;
    static int _redValue = 0;
    static int _greenValue = 0;
    static int _blueValue = 0;
    static int _brightness = DEFAULT_LED_BRIGHTNESS;
    static bool _isBlocked = false;
    
    // Private helper functions
    static void _setColor(int red, int green, int blue, bool force = false);
    static void _setPwm();

    void begin(int redPin, int greenPin, int bluePin) {
        _redPin = redPin;
        _greenPin = greenPin;
        _bluePin = bluePin;

        pinMode(_redPin, OUTPUT);
        pinMode(_greenPin, OUTPUT);
        pinMode(_bluePin, OUTPUT);

        ledcAttach(_redPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_greenPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_bluePin, LED_FREQUENCY, LED_RESOLUTION);

        setOff();
    }

    void setBrightness(int brightness) {
        _brightness = min(max(brightness, 0), LED_MAX_BRIGHTNESS);
        _setPwm();
    }

    int getBrightness() {
        return _brightness;
    }

    void setRed(bool force) {
        _setColor(255, 0, 0, force);
    }

    void setGreen(bool force) {
        _setColor(0, 255, 0, force);
    }

    void setBlue(bool force) {
        _setColor(0, 0, 255, force);
    }

    void setYellow(bool force) {
        _setColor(255, 255, 0, force);
    }

    void setPurple(bool force) {
        _setColor(255, 0, 255, force);
    }

    void setCyan(bool force) {
        _setColor(0, 255, 255, force);
    }

    void setOrange(bool force) {
        _setColor(255, 128, 0, force);
    }

    void setWhite(bool force) {
        _setColor(255, 255, 255, force);
    }

    void setOff(bool force) {
        _setColor(0, 0, 0, force);
    }

    void block() {
        _isBlocked = true;
    }

    void unblock() {
        _isBlocked = false;
    }

    static void _setColor(int red, int green, int blue, bool force) {
        if (_isBlocked && !force) {
            return;
        }
        _redValue = red;
        _greenValue = green;
        _blueValue = blue;

        _setPwm();
    }

    static void _setPwm() {
        ledcWrite(_redPin, int(_redValue * _brightness / LED_MAX_BRIGHTNESS));
        ledcWrite(_greenPin, int(_greenValue * _brightness / LED_MAX_BRIGHTNESS));
        ledcWrite(_bluePin, int(_blueValue * _brightness / LED_MAX_BRIGHTNESS));
    }
}