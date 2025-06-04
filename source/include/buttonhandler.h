#pragma once

#include <Arduino.h>
#include <AdvancedLogger.h>

#include "constants.h"
#include "led.h"
#include "customwifi.h"
#include "utils.h"

/**
 * Button press types for different functionalities
 */
enum class ButtonPressType {
    NONE,
    SINGLE_SHORT,     // Restart
    SINGLE_MEDIUM,    // Password reset to default  
    SINGLE_LONG,      // WiFi reset
    SINGLE_VERY_LONG  // Factory reset
};

/**
 * Button handler class for managing critical functionality control
 * via GPIO0 button with comprehensive LED feedback system
 */
class ButtonHandler {
public:
    /**
     * Constructor
     * @param buttonPin GPIO pin number for the button
     * @param logger Reference to logger instance
     * @param led Reference to LED instance for feedback
     * @param customWifi Reference to WiFi manager instance
     */
    ButtonHandler(
        int buttonPin,
        AdvancedLogger& logger,
        Led& led,
        CustomWifi& customWifi
    );

    /**
     * Initialize the button handler
     * Sets up GPIO pin and internal state
     */
    void begin();

    /**
     * Main loop function - should be called frequently
     * Handles button press detection and timing
     */
    void loop();

    /**
     * Get current button press type being processed
     * @return Current button press type
     */
    ButtonPressType getCurrentPressType() const { return _currentPressType; }

    /**
     * Check if button operation is currently in progress
     * @return true if operation is active
     */
    bool isOperationInProgress() const { return _operationInProgress; }

private:
    // Hardware
    int _buttonPin;
      // Dependencies
    AdvancedLogger& _logger;
    Led& _led;
    CustomWifi& _customWifi;
    
    // Button state tracking
    bool _buttonPressed;
    bool _lastButtonState;
    unsigned long _buttonPressStartTime;
    unsigned long _lastDebounceTime;
    
    // Operation state
    ButtonPressType _currentPressType;
    bool _operationInProgress;
    
    // LED feedback state
    bool _ledFeedbackActive;
    unsigned long _lastLedToggle;
    bool _ledState;
      // Private methods
    void _updateButtonState();
    void _handleButtonPress();
    void _handleButtonRelease();
    void _processButtonPressType();
    void _updatePressVisualFeedback();
    
    // Operation handlers
    void _handleRestart();
    void _handlePasswordReset();
    void _handleWifiReset();
    void _handleFactoryReset();
    
    // LED feedback methods
    void _startLedFeedback(ButtonPressType pressType);
    void _updateLedFeedback();
    void _stopLedFeedback();
    void _blinkLed(unsigned long interval);
    
    // Utility methods
    bool _readButton();
};
