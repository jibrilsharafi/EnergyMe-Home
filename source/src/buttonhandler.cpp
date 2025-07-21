#include "buttonhandler.h"
#include <stdexcept>
#include <cstdarg>

static const char *TAG = "buttonhandler";

ButtonHandler::ButtonHandler(
    int buttonPin,
    AdvancedLogger &logger) : _buttonPin(buttonPin),
                              _logger(logger),
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
    Led::block();
    Led::setBrightness(max(Led::getBrightness(), 1)); // Show a faint light even if it is off
    Led::setWhite(true);                              // Initial press indication
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

    Led::unblock(); // Unblock LED for other operations
}

void ButtonHandler::_processButtonPressType()
{
    switch (_currentPressType)
    {
    case ButtonPressType::SINGLE_SHORT:
        snprintf(_operationName, sizeof(_operationName), "Restart");
        _handleRestart();
        break;
    case ButtonPressType::SINGLE_MEDIUM:
        snprintf(_operationName, sizeof(_operationName), "Password Reset");
        _handlePasswordReset();
        break;
    case ButtonPressType::SINGLE_LONG:
        snprintf(_operationName, sizeof(_operationName), "WiFi Reset");
        _handleWifiReset();
        break;
    case ButtonPressType::SINGLE_VERY_LONG:
        snprintf(_operationName, sizeof(_operationName), "Factory Reset");
        _handleFactoryReset();
        break;
    default:
        break;
    }
}

void ButtonHandler::_handleRestart()
{
    _logger.info("Restart initiated", TAG);
    snprintf(_operationName, sizeof(_operationName), "Restart");
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations and set feedback color
    Led::block();
    Led::setCyan(true);

    setRestartEsp32(TAG, "Restart via button");

    // Anything below here is useless like a chocolate teapot
    // -----------------------------------------------------
    Led::unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handlePasswordReset()
{
    _logger.debug("Password reset to default initiated", TAG);
    snprintf(_operationName, sizeof(_operationName), "Password Reset");
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    Led::block();
    Led::setYellow(true);

    // Reset password to default
    if (setAuthPassword(DEFAULT_WEB_PASSWORD))
    {
        _logger.warning("Password reset to default successfully", TAG);

        // Success - Green - 3 slow blinks green
        for (int i = 0; i < 3; ++i)
        {
            Led::setGreen(true);
            delay(500); // Brief success indication
            Led::setOff(true);
            delay(500);
        }
    }
    else
    {
        _logger.error("Failed to reset password to default", TAG);

        // Failure - Red - 3 slow blinks red
        for (int i = 0; i < 3; ++i)
        {
            Led::setRed(true); // Indicate failure
            delay(500);
            Led::setOff(true);
            delay(500);
        }
    }

    // Unblock LED
    Led::setOff(true);
    Led::unblock();
    delay(1000); // Ensure proper feedback to the user

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleWifiReset()
{
    _logger.info("WiFi reset initiated", TAG);
    snprintf(_operationName, sizeof(_operationName), "WiFi Reset");
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    Led::block();
    Led::setOrange(true);

    CustomWifi::resetWifi(); // Erase WiFi credentials and restart

    // Anything below here is useless like being an adult in a candy store
    // -----------------------------------------------------
    _logger.info("WiFi reset completed successfully", TAG);

    // Unblock LED
    Led::unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleFactoryReset()
{
    _logger.info("Factory reset initiated", TAG);
    snprintf(_operationName, sizeof(_operationName), "Factory Reset");
    _operationTimestamp = CustomTime::getUnixTime();
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();

    // Block LED from other operations
    Led::block();
    Led::setRed(true);

    factoryReset(); // Format and restart

    // Anything below here is useless like the R in Marlboro
    // -----------------------------------------------------
    _logger.info("Factory reset initiated - device will restart", TAG);

    // Unblock LED (won't matter due to restart)
    Led::unblock();

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
        Led::setWhite(true); // Indicate maximum press time exceeded
        return;
    }
    if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME)
    {
        Led::setRed(true); // Factory reset - most dangerous (red)
    }
    else if (pressDuration >= BUTTON_LONG_PRESS_TIME)
    {
        Led::setOrange(true); // WiFi reset - moderately dangerous (orange)
    }
    else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME)
    {
        Led::setYellow(true); // Password reset - caution (yellow)
    }
    else if (pressDuration >= BUTTON_SHORT_PRESS_TIME)
    {
        Led::setCyan(true); // Restart - safest operation (green)
    }
    else
    {
        Led::setWhite(true); // Initial press indication
    }
}

bool ButtonHandler::_readButton()
{
    return digitalRead(_buttonPin);
}

void ButtonHandler::clearCurrentOperationName()
{
    sniprintf(_operationName, sizeof(_operationName), "None");
    _operationTimestamp = 0;
    _saveLastOperationToNVS();
    _saveOperationTimestampToNVS();
}

void ButtonHandler::_saveLastOperationToNVS()
{
    _preferences.putString("lastOperation", _operationName);
    _logger.debug("Saved last operation to NVS: %s", TAG, _operationName);
}

void ButtonHandler::_loadLastOperationFromNVS()
{
    _preferences.getString("lastOperation", _operationName, sizeof(_operationName));
    if (strlen(_operationName) > 0) 
    {
        _logger.info("Loaded last button operation from NVS: %s", TAG, _operationName);
    }
    else
    {
        _logger.info("No previous button operation found in NVS", TAG);
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
        char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
        CustomTime::timestampFromUnix(_operationTimestamp, timestampBuffer);
        _logger.info("Loaded operation timestamp from NVS: %s", TAG, timestampBuffer);
    }
}