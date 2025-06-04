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
                              _operationInProgress(false),
                              _ledFeedbackActive(false),
                              _lastLedToggle(0),
                              _ledState(false)
{
}

void ButtonHandler::begin()
{
    pinMode(_buttonPin, INPUT_PULLUP);
    _logger.info("Button handler initialized on GPIO%d", TAG, _buttonPin);
}

void ButtonHandler::loop()
{
    _updateButtonState();
    _updateLedFeedback(); // TODO: remove this and related as the operations are so quick (and have their own led control) that no blink is actually needed
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
    _led.block();       // Block LED from other operations
    _led.setBlue(true); // Initial press indication
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
    if (_operationInProgress)
    {
        _startLedFeedback(_currentPressType);
    }
}

void ButtonHandler::_processButtonPressType()
{
    switch (_currentPressType)
    {
    case ButtonPressType::SINGLE_SHORT:
        _handleRestart();
        break;
    case ButtonPressType::SINGLE_MEDIUM:
        _handlePasswordReset();
        break;
    case ButtonPressType::SINGLE_LONG:
        _handleWifiReset();
        break;
    case ButtonPressType::SINGLE_VERY_LONG:
        _handleFactoryReset();
        break;
    default:
        break;
    }
}

void ButtonHandler::_handleRestart()
{
    _logger.info("Restart initiated", TAG);

    // Block LED from other operations and set feedback color
    _led.block();
    _led.setCyan(true);

    setRestartEsp32(TAG, "Restart via button");

    // Unblock LED before restart (won't matter much due to restart)
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handlePasswordReset()
{
    _logger.debug("Password reset to default initiated", TAG);

    // Block LED from other operations
    _led.block();
    _led.setPurple(true);

    // Reset password to default
    if (setAuthPassword(DEFAULT_WEB_PASSWORD))
    {
        _logger.warning("Password reset to default successfully", TAG);
        // Success - Green
        _led.setGreen(true);
        delay(1000); // Brief success indication
    }
    else
    {
        _logger.error("Failed to reset password to default", TAG);
        _led.setRed(true);
        delay(1000); // Brief error indication
    }

    // Unblock LED
    _led.setOff(true);
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleWifiReset()
{
    _logger.info("WiFi reset initiated", TAG);

    // Block LED from other operations
    _led.block();
    _led.setOrange(true);

    _customWifi.resetWifi(); // Erase WiFi credentials and restart

    // Anything below here is useless like being an adult in a candy store
    // -----------------------------------------------------
    _logger.info("WiFi reset completed successfully", TAG);

    // Success - Green
    _led.setGreen(true);
    delay(1000); // Brief success indication

    // Unblock LED
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleFactoryReset()
{
    _logger.info("Factory reset initiated", TAG);

    // Block LED from other operations
    _led.block();
    _led.setWhite(true);

    factoryReset(); // Format and restart

    // Anything below here is useless like the R in Marlboro
    // -----------------------------------------------------
    _logger.info("Factory reset initiated - device will restart", TAG);

    // Success indication - won't be seen due to restart
    _led.setGreen(true);

    // Unblock LED (won't matter due to restart)
    _led.unblock();

    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_startLedFeedback(ButtonPressType pressType)
{
    _ledFeedbackActive = true;
    _lastLedToggle = millis();
    _ledState = false;
    _led.setOff(true); // Start with LED off
    _led.block();      // Block LED from other operations
}

void ButtonHandler::_updateLedFeedback()
{
    if (!_ledFeedbackActive || !_operationInProgress)
    {
        return;
    }

    unsigned long currentTime = millis();
    unsigned long interval = BUTTON_FEEDBACK_BLINK_SLOW;

    // Adjust blink speed based on operation type
    switch (_currentPressType)
    {
    case ButtonPressType::SINGLE_MEDIUM:
    case ButtonPressType::SINGLE_LONG:
        interval = BUTTON_FEEDBACK_BLINK_SLOW;
        break;
    case ButtonPressType::SINGLE_VERY_LONG:
        interval = BUTTON_FEEDBACK_BLINK_FAST;
        break;
    default:
        interval = BUTTON_FEEDBACK_BLINK_SLOW;
        break;
    }

    _blinkLed(interval);
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
        _led.setBlue(true); // Initial press indication
    }
}

void ButtonHandler::_stopLedFeedback()
{
    _ledFeedbackActive = false;
    _led.setOff(true);
    _led.unblock(); // Unblock LED for other operations
}

void ButtonHandler::_blinkLed(unsigned long interval)
{
    unsigned long currentTime = millis();

    if (currentTime - _lastLedToggle >= interval)
    {
        _ledState = !_ledState;
        _lastLedToggle = currentTime;

        if (_ledState)
        {
            _led.setYellow(true); // Default feedback color
        }
        else
        {
            _led.setOff(true);
        }
    }
}

bool ButtonHandler::_readButton()
{
    return digitalRead(_buttonPin);
}