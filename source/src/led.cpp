#include "led.h"

// TODO: we could introduce the logger here (when it will
// be static) but it is not essential since this is a 
// simple LED control class
// TODO: add task for async usage
namespace Led {
    // Static variables to maintain state
    static int _redPin = INVALID_PIN;
    static int _greenPin = INVALID_PIN;
    static int _bluePin = INVALID_PIN;
    static int _redValue = 0;
    static int _greenValue = 0;
    static int _blueValue = 0;
    static int _brightness = DEFAULT_LED_BRIGHTNESS;
    static bool _isBlocked = false;
    
    // Private helper functions
    static void _setColor(int red, int green, int blue, bool force = false);
    static void _setPwm();

    static bool _loadConfiguration();
    static void _saveConfiguration();

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

        _loadConfiguration();

        setOff();
    }

    void resetToDefaults() {
        _brightness = DEFAULT_LED_BRIGHTNESS;
        _saveConfiguration();
    }

    bool _loadConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, true)) {
            // If preferences can't be opened, fallback to default
            _brightness = DEFAULT_LED_BRIGHTNESS;
            _saveConfiguration();
            _setPwm();
            return false;
        }

        _brightness = preferences.getInt(PREFERENCES_BRIGHTNESS_KEY, DEFAULT_LED_BRIGHTNESS);
        preferences.end();
        
        // Validate loaded value is within acceptable range
        _brightness = min(max(_brightness, 0), LED_MAX_BRIGHTNESS);
        
        // Update PWM with loaded brightness
        _setPwm();
        return true;
    }

    void _saveConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, false)) {
            return;
        }
        preferences.putInt(PREFERENCES_BRIGHTNESS_KEY, _brightness);
        preferences.end();
    }

    void setBrightness(int brightness) {
        _brightness = min(max(brightness, 0), LED_MAX_BRIGHTNESS);
        _setPwm();
        _saveConfiguration();
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
        // Validate pins are set before using them
        if (_redPin == INVALID_PIN || _greenPin == INVALID_PIN || _bluePin == INVALID_PIN) {
            return;
        }
        
        ledcWrite(_redPin, int(_redValue * _brightness / LED_MAX_BRIGHTNESS));
        ledcWrite(_greenPin, int(_greenValue * _brightness / LED_MAX_BRIGHTNESS));
        ledcWrite(_bluePin, int(_blueValue * _brightness / LED_MAX_BRIGHTNESS));
    }
}