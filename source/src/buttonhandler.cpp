#include "buttonhandler.h"
#include <stdexcept>
#include <cstdarg>

static const char* TAG = "buttonhandler";

ButtonHandler::ButtonHandler(
    int buttonPin,
    AdvancedLogger& logger,
    Led& led,
    CustomWifi& customWifi
) : _buttonPin(buttonPin),
    _logger(logger),
    _led(led),
    _customWifi(customWifi),
    _buttonPressed(false),
    _lastButtonState(false),
    _buttonPressStartTime(0),
    _buttonReleaseTime(0),
    _lastDebounceTime(0),
    _pressCount(0),
    _firstPressTime(0),
    _lastPressTime(0),
    _currentPressType(ButtonPressType::NONE),
    _operationInProgress(false),
    _operationStartTime(0),
    _ledFeedbackActive(false),
    _lastLedToggle(0),
    _ledState(false)
{
}

void ButtonHandler::begin() {
    pinMode(_buttonPin, INPUT_PULLUP);
    _logger.info("Button handler initialized on GPIO%d",TAG, _buttonPin);
    
    // Brief LED feedback to indicate button handler is ready
    _led.setGreen();
}

void ButtonHandler::loop() {
    _updateButtonState();
    _updateLedFeedback();
    
    // Handle multi-press timeout logic
    _checkMultiPressTimeout();
    
    // Handle ongoing operations
    if (_operationInProgress) {
        _processButtonPressType();
    }
}

void ButtonHandler::_updateButtonState() {
    bool currentButtonState = !_readButton(); // Inverted because of pull-up
    unsigned long currentTime = millis();
    
    // Debounce logic
    if (currentButtonState != _lastButtonState) {
        _lastDebounceTime = currentTime;
    }
    
    if ((currentTime - _lastDebounceTime) > BUTTON_DEBOUNCE_TIME) {
        if (currentButtonState != _buttonPressed) {
            _buttonPressed = currentButtonState;
            
            if (_buttonPressed) {
                _handleButtonPress();
            } else {
                _handleButtonRelease();
            }
        }
    }
    
    _lastButtonState = currentButtonState;
}

void ButtonHandler::_handleButtonPress() {
    _buttonPressStartTime = millis();
    _logger.info("Button pressed", TAG);
    
    // Start LED feedback for press indication
    _led.setBlue();
}

void ButtonHandler::_handleButtonRelease() {
    unsigned long pressDuration = millis() - _buttonPressStartTime;
    _buttonReleaseTime = millis();
    
    _logger.info("Button released after %lu ms", TAG, pressDuration);
    
    // Determine press type based on duration
    if (pressDuration < BUTTON_SHORT_PRESS_TIME) {
        _detectMultiPress();
    } else if (pressDuration < BUTTON_MEDIUM_PRESS_TIME) {
        _currentPressType = ButtonPressType::SINGLE_MEDIUM;
        _operationInProgress = true;
        _operationStartTime = millis();
    } else if (pressDuration < BUTTON_LONG_PRESS_TIME) {
        _currentPressType = ButtonPressType::SINGLE_LONG;
        _operationInProgress = true;
        _operationStartTime = millis();
    } else if (pressDuration < BUTTON_VERY_LONG_PRESS_TIME) {
        _currentPressType = ButtonPressType::SINGLE_VERY_LONG;
        _operationInProgress = true;
        _operationStartTime = millis();
    } else {
        _currentPressType = ButtonPressType::SINGLE_FORCE;
        _operationInProgress = true;
        _operationStartTime = millis();
    }
    
    if (_operationInProgress) {
        _startLedFeedback(_currentPressType);
    }
}

void ButtonHandler::_detectMultiPress() {
    unsigned long currentTime = millis();
    
    if (_pressCount == 0 || (currentTime - _firstPressTime) > BUTTON_TRIPLE_PRESS_INTERVAL * 3) {
        // First press or timeout - start new sequence
        _pressCount = 1;
        _firstPressTime = currentTime;
        _lastPressTime = currentTime;
        return; // Wait for more presses or timeout
    } else {
        _pressCount++;
        _lastPressTime = currentTime;
        
        // Process immediately if we have 3 or more presses
        if (_pressCount >= 3) {
            _currentPressType = ButtonPressType::TRIPLE_PRESS;
            _operationInProgress = true;
            _operationStartTime = millis();
            _resetPressDetection();
        }
    }
}

void ButtonHandler::_processButtonPressType() {
    switch (_currentPressType) {
        case ButtonPressType::SINGLE_SHORT:
            _handleWifiStatusIndication();
            break;
        case ButtonPressType::SINGLE_MEDIUM:
            _handleWifiReset();
            break;
        case ButtonPressType::SINGLE_LONG:
            _handlePasswordReset();
            break;
        case ButtonPressType::SINGLE_VERY_LONG:
            _handleFactoryReset();
            break;
        case ButtonPressType::SINGLE_FORCE:
            _handleForceRestart();
            break;
        case ButtonPressType::DOUBLE_PRESS:
            _handleDeviceInfo();
            break;
        case ButtonPressType::TRIPLE_PRESS:
            _handleFirmwareInfo();
            break;
        default:
            break;
    }
}

void ButtonHandler::_handleWifiStatusIndication() {
    _logger.info("WiFi status indication", TAG);
    
    // Show WiFi status via LED color
    if (WiFi.status() == WL_CONNECTED) {
        _led.setGreen(); // Connected - Green
    } else {
        _led.setRed(); // Disconnected - Red
    }
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleWifiReset() {
    _logger.info("WiFi reset initiated", TAG);
    
    // Orange during WiFi reset
    _led.setOrange();
    
    _customWifi.resetWifi();
    _logger.info("WiFi reset completed successfully", TAG);

    // Success - Green
    _led.setGreen();
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handlePasswordReset() {
    _logger.info("Password reset to default initiated", TAG);
    
    // Purple pulsing during password reset
    _led.setPurple();
    
    // Reset password to default
    if (setAuthPassword(DEFAULT_WEB_PASSWORD)) {
        _logger.info("Password reset to default successfully", TAG);

        // Success - Green
        _led.setGreen();
    } else {
        _logger.error(TAG, "Failed to reset password to default", TAG);
        _led.setRed();
    }
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleFactoryReset() {
    _logger.info("Factory reset initiated", TAG);
    
    // White during factory reset
    _led.setWhite();
    
    factoryReset();
    _logger.info("Factory reset initiated - device will restart", TAG);

    // Success indication - won't be seen due to restart
    _led.setGreen();
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleForceRestart() {
    _logger.info("Force restart initiated", TAG);
    
    // Cyan for warning
    _led.setCyan();
    
    _logger.warning(TAG, "Force restart initiated via button", TAG);
    setRestartEsp32(TAG, "Force restart via button");
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleDeviceInfo() {
    _logger.info("Device info display", TAG);
    
    // Show device info via LED pattern
    // Blue-Green-Blue pattern for device info
    int colors[] = {0, 1, 0}; // Blue, Green, Blue (using LED color constants)
    _showColorPattern(colors, 3, 800);
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_handleFirmwareInfo() {
    _logger.info("Firmware info display", TAG);
    
    // Show firmware info via LED pattern
    // Yellow-Purple-Yellow pattern for firmware info
    int colors[] = {2, 3, 2}; // Yellow, Purple, Yellow
    _showColorPattern(colors, 3, 800);
    
    _operationInProgress = false;
    _currentPressType = ButtonPressType::NONE;
}

void ButtonHandler::_startLedFeedback(ButtonPressType pressType) {
    _ledFeedbackActive = true;
    _lastLedToggle = millis();
    _ledState = false;
}

void ButtonHandler::_updateLedFeedback() {
    if (!_ledFeedbackActive || !_operationInProgress) {
        return;
    }
    
    unsigned long currentTime = millis();
    unsigned long interval = BUTTON_FEEDBACK_BLINK_SLOW;
    
    // Adjust blink speed based on operation type
    switch (_currentPressType) {
        case ButtonPressType::SINGLE_MEDIUM:
        case ButtonPressType::SINGLE_LONG:
            interval = BUTTON_FEEDBACK_BLINK_SLOW;
            break;
        case ButtonPressType::SINGLE_VERY_LONG:
        case ButtonPressType::SINGLE_FORCE:
            interval = BUTTON_FEEDBACK_BLINK_FAST;
            break;
        default:
            interval = BUTTON_FEEDBACK_BLINK_SLOW;
            break;
    }
    
    _blinkLed(interval);
}

void ButtonHandler::_stopLedFeedback() {
    _ledFeedbackActive = false;
    _led.setOff();
}

void ButtonHandler::_blinkLed(unsigned long interval) {
    unsigned long currentTime = millis();
    
    if (currentTime - _lastLedToggle >= interval) {
        _ledState = !_ledState;
        _lastLedToggle = currentTime;
        
        if (_ledState) {
            _led.setYellow(); // Default feedback color
        } else {
            _led.setOff();
        }
    }
}

void ButtonHandler::_pulseLed(int color, unsigned long duration) {
    // Simple pulse implementation without delay
    switch (color) {
        case 0: _led.setBlue(); break;
        case 1: _led.setGreen(); break;
        case 2: _led.setYellow(); break;
        case 3: _led.setPurple(); break;
        case 4: _led.setRed(); break;
        case 5: _led.setCyan(); break;
        case 6: _led.setWhite(); break;
        default: _led.setOff(); break;
    }
}

void ButtonHandler::_showColorPattern(int* colors, int colorCount, unsigned long interval) {
    // Simplified - just show the first color
    if (colorCount > 0) {
        _pulseLed(colors[0], interval);
    }
}

bool ButtonHandler::_readButton() {
    return digitalRead(_buttonPin);
}

void ButtonHandler::_checkMultiPressTimeout() {
    if (_pressCount > 0 && !_operationInProgress) {
        unsigned long currentTime = millis();
        unsigned long timeoutInterval;
        
        if (_pressCount == 1) {
            timeoutInterval = BUTTON_DOUBLE_PRESS_INTERVAL;
        } else if (_pressCount == 2) {
            timeoutInterval = BUTTON_TRIPLE_PRESS_INTERVAL;
        } else {
            return; // Already handled in _detectMultiPress
        }
        
        if ((currentTime - _lastPressTime) >= timeoutInterval) {
            // Timeout reached, process the current press count
            if (_pressCount == 1) {
                _currentPressType = ButtonPressType::SINGLE_SHORT;
            } else if (_pressCount == 2) {
                _currentPressType = ButtonPressType::DOUBLE_PRESS;
            }
            
            _operationInProgress = true;
            _operationStartTime = currentTime;
            _resetPressDetection();
            _startLedFeedback(_currentPressType);
        }
    }
}

void ButtonHandler::_resetPressDetection() {
    _pressCount = 0;
    _firstPressTime = 0;
    _lastPressTime = 0;
}