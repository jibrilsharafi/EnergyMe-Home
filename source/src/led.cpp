#include "led.h"

Led::Led(int redPin, int greenPin, int bluePin, int brightness) {
    _redPin = redPin;
    _greenPin = greenPin;
    _bluePin = bluePin;
    _brightness = brightness;
}

void Led::begin() {
    pinMode(_redPin, OUTPUT);
    pinMode(_greenPin, OUTPUT);
    pinMode(_bluePin, OUTPUT);

    ledcSetup(1, LED_FREQUENCY, LED_RESOLUTION);
    ledcSetup(2, LED_FREQUENCY, LED_RESOLUTION);
    ledcSetup(3, LED_FREQUENCY, LED_RESOLUTION);

    ledcAttachPin(_redPin, _redPinChannel);
    ledcAttachPin(_greenPin, _greenPinChannel);
    ledcAttachPin(_bluePin, _bluePinChannel);

    setOff();
}

void Led::setBrightness(int brightness) {
    _brightness = min(max(brightness, 0), LED_MAX_BRIGHTNESS);
    _setPwm();
}

int Led::getBrightness() {
    return _brightness;
}

void Led::setRed(bool force) {
    _setColor(255, 0, 0, force);
}

void Led::setGreen(bool force) {
    _setColor(0, 255, 0, force);
}

void Led::setBlue(bool force) {
    _setColor(0, 0, 255, force);
}

void Led::setYellow(bool force) {
    _setColor(255, 255, 0, force);
}

void Led::setPurple(bool force) {
    _setColor(255, 0, 255, force);
}

void Led::setCyan(bool force) {
    _setColor(0, 255, 255, force);
}

void Led::setOrange(bool force) {
    _setColor(255, 165, 0, force);
}

void Led::setWhite(bool force) {
    _setColor(255, 255, 255, force);
}

void Led::setOff(bool force) {
    _setColor(0, 0, 0, force);
}

void Led::_setColor(int red, int green, int blue, bool force) {
    if (_isBlocked && !force){
        return;
    }
    _redValue = red;
    _greenValue = green;
    _blueValue = blue;

    _setPwm();
}

void Led::_setPwm() {
    ledcWrite(_redPinChannel, int(_redValue * _brightness / LED_MAX_BRIGHTNESS));
    ledcWrite(_greenPinChannel, int(_greenValue * _brightness / LED_MAX_BRIGHTNESS));
    ledcWrite(_bluePinChannel, int(_blueValue * _brightness / LED_MAX_BRIGHTNESS));
}

void Led::block() {
    _isBlocked = true;
}

void Led::unblock() {
    _isBlocked = false;
}