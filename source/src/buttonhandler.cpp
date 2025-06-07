#include "buttonhandler.h"
#include <stdexcept>
#include <cstdarg>

static const char *TAG = "buttonhandler";

ButtonHandler::ButtonHandler(
    int buttonPin,
    AdvancedLogger &logger,
    Led &led,
    CustomWifi &customWifi) : _buttonPin(buttonPin),
                              _logger(logger),
                              _led(led),
                              _customWifi(customWifi),
                              _buttonPressed(false),
                              _lastButtonState(false),
                              _buttonPressStartTime(0),
                              _lastDebounceTime(0),
                              _currentPressType(ButtonPressType::NONE),
                              _operationName(""),
                              _operationInProgress(false),
                              _operationTimestamp(0)
{
}

void ButtonHandler::begin()
{
    pinMode(_buttonPin, INPUT_PULLUP);

    // Initialize NVS and load last operation if any
    _preferences.begin("buttonhandler", false);
    _loadLastOperationFromNVS();
    _loadOperationTimestampFromNVS();

    _logger.debug("Button handler initialized on GPIO%d", TAG, _buttonPin);
}

void ButtonHandler::loop()
{
    _updateButtonState();
    _updatePressVisualFeedback();

    // Handle ongoing operations
    if (_operationInProgress)
    {
        _processButtonPressType();
    }
}

void ButtonHandler::_updateButtonState()
{
    bool currentButtonState = !_readButton(); // Inverted because of pull-up
    unsigned long currentTime = millis();

    // Debounce logic
    if (currentButtonState != _lastButtonState)
    {
        _lastDebounceTime = currentTime;
    }

    if ((currentTime - _lastDebounceTime) > BUTTON_DEBOUNCE_TIME)
    {
        if (currentButtonState != _buttonPressed)
        {
            _buttonPressed = currentButtonState;

            if (_buttonPressed)
            {
                _handleButtonPress();
            }
            else
            {
                _handleButtonRelease();
            }
        }
    }

    _lastButtonState = currentButtonState;
}

void ButtonHandler::_handleButtonPress()
{
    _buttonPressStartTime = millis();
    _logger.debug("Button pressed", TAG);

    // Start immediate visual feedback
    _led.block();
    _led.setBrightness(max(_led.getBrightness(), 1)); // Show a faint light even if it is off
    _led.setWhite(true);                              // Initial press indication
}

void ButtonHandler::_handleButtonRelease()
{
    unsigned long pressDuration = millis() - _buttonPressStartTime;

    _logger.debug("Button released after %lu ms", TAG, pressDuration);

    // Determine press type based on duration
    if (pressDuration >= BUTTON_SHORT_PRESS_TIME && pressDuration < BUTTON_MEDIUM_PRESS_TIME)
    {
        _currentPressType = ButtonPressType::SINGLE_SHORT;
        _operationInProgress = true;
    }
    else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME && pressDuration < BUTTON_LONG_PRESS_TIME)
    {
        _currentPressType = ButtonPressType::SINGLE_MEDIUM;
        _operationInProgress = true;
    }
    else if (pressDuration >= BUTTON_LONG_PRESS_TIME && pressDuration < BUTTON_VERY_LONG_PRESS_TIME)
    {
        _currentPressType = ButtonPressType::SINGLE_LONG;
        _operationInProgress = true;
    }
    else if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME && pressDuration < BUTTON_MAX_PRESS_TIME)
    {
        _currentPressType = ButtonPressType::SINGLE_VERY_LONG;
        _operationInProgress = true;
    }
    else
    {
        _currentPressType = ButtonPressType::NONE;
        _operationInProgress = false;
    }

    _led.unblock(); // Unblock LED for other operations
}

void ButtonHandler::_processButtonPressType()
{
    switch (_currentPressType)
    {
    case ButtonPressType::SINGLE_SHORT:
        _operationName = "Restart";
        _handleRestart();
        break;
    case ButtonPressType::SINGLE_MEDIUM:
        _operationName = "Password Reset";
        _handlePasswordReset();
        break;
    case ButtonPressType::SINGLE_LONG:
        _operationName = "WiFi Reset";
        _handleWifiReset();
        break;
    case ButtonPressType::SINGLE_VERY_LONG:
        _operationName = "Factory Reset";
        _handleFactoryReset();
        break;
    default:
        break;
    }
}

void ButtonHandler::_handleRestart()
{
    _logger.info("Restart initiated", TAG);
    _operationName = "Restart";
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations and set feedback color
    _led.block();
    _led.setCyan(true);

    setRestartEsp32(TAG, "Restart via button");

    // Anything below here is useless like a chocolate teapot
    // -----------------------------------------------------
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handlePasswordReset()
{
    _logger.debug("Password reset to default initiated", TAG);
    _operationName = "Password Reset";
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    _led.block();
    _led.setYellow(true);

    // Reset password to default
    if (setAuthPassword(DEFAULT_WEB_PASSWORD))
    {
        _logger.warning("Password reset to default successfully", TAG);

        // Success - Green - 3 slow blinks green
        for (int i = 0; i < 3; ++i)
        {
            _led.setGreen(true);
            delay(500); // Brief success indication
            _led.setOff(true);
            delay(500);
        }
    }
    else
    {
        _logger.error("Failed to reset password to default", TAG);

        // Failure - Red - 3 slow blinks red
        for (int i = 0; i < 3; ++i)
        {
            _led.setRed(true); // Indicate failure
            delay(500);
            _led.setOff(true);
            delay(500);
        }
    }

    // Unblock LED
    _led.setOff(true);
    _led.unblock();
    delay(1000); // Ensure proper feedback to the user

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleWifiReset()
{
    _logger.info("WiFi reset initiated", TAG);
    _operationName = "WiFi Reset";
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    _led.block();
    _led.setOrange(true);

    _customWifi.resetWifi(); // Erase WiFi credentials and restart

    // Anything below here is useless like being an adult in a candy store
    // -----------------------------------------------------
    _logger.info("WiFi reset completed successfully", TAG);

    // Unblock LED
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleFactoryReset()
{
    _logger.info("Factory reset initiated", TAG);
    _operationName = "Factory Reset";
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    _led.block();
    _led.setRed(true);

    factoryReset(); // Format and restart

    // Anything below here is useless like the R in Marlboro
    // -----------------------------------------------------
    _logger.info("Factory reset initiated - device will restart", TAG);

    // Unblock LED (won't matter due to restart)
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_updatePressVisualFeedback()
{
    if (!_buttonPressed || _operationInProgress)
    {
        return;
    }

    unsigned long pressDuration = millis() - _buttonPressStartTime;

    // Provide immediate visual feedback based on current press duration
    if (pressDuration >= BUTTON_MAX_PRESS_TIME)
    {
        _led.setWhite(true); // Indicate maximum press time exceeded
        return;
    }
    if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME)
    {
        _led.setRed(true); // Factory reset - most dangerous (red)
    }
    else if (pressDuration >= BUTTON_LONG_PRESS_TIME)
    {
        _led.setOrange(true); // WiFi reset - moderately dangerous (orange)
    }
    else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME)
    {
        _led.setYellow(true); // Password reset - caution (yellow)
    }
    else if (pressDuration >= BUTTON_SHORT_PRESS_TIME)
    {
        _led.setCyan(true); // Restart - safest operation (green)
    }
    else
    {
        _led.setWhite(true); // Initial press indication
    }
}

bool ButtonHandler::_readButton()
{
    return digitalRead(_buttonPin);
}

void ButtonHandler::clearCurrentOperationName()
{
    _operationName = "";
    _operationTimestamp = 0;
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();
}

void ButtonHandler::_saveLastOperationToNVS()
{
    _preferences.putString("lastOperation", _operationName);
    _logger.debug("Saved last operation to NVS: %s", TAG, _operationName.c_str());
}

void ButtonHandler::_loadLastOperationFromNVS()
{
    _operationName = _preferences.getString("lastOperation", "");
    if (!_operationName.isEmpty())
    {
        _logger.info("Loaded last button operation from NVS: %s", TAG, _operationName.c_str());
    }
}

void ButtonHandler::_saveOperationTimestampToNVS()
{
    _preferences.putULong("lastOpTimestamp", _operationTimestamp);
    _logger.debug("Saved operation timestamp to NVS: %lu", TAG, _operationTimestamp);
}

void ButtonHandler::_loadOperationTimestampFromNVS()
{
    _operationTimestamp = _preferences.getULong("lastOpTimestamp", 0);
    if (_operationTimestamp > 0)
    {
        String timestampStr = CustomTime::timestampFromUnix(_operationTimestamp);
        _logger.info("Loaded operation timestamp from NVS: %s", TAG, timestampStr.c_str());
    }
}