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
    SINGLE_SHORT,     // 1 press < 1s: WiFi status indication
    SINGLE_MEDIUM,    // 1 press 1-3s: WiFi reset
    SINGLE_LONG,      // 1 press 3-10s: Password reset to default
    SINGLE_VERY_LONG, // 1 press 10-15s: Factory reset
    SINGLE_FORCE,     // 1 press > 15s: Force restart
    DOUBLE_PRESS,     // 2 presses quickly: Show device info via LED
    TRIPLE_PRESS      // 3 presses quickly: Show firmware info via LED
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
    unsigned long _buttonReleaseTime;
    unsigned long _lastDebounceTime;
    
    // Multi-press detection
    int _pressCount;
    unsigned long _firstPressTime;
    unsigned long _lastPressTime;
    
    // Operation state
    ButtonPressType _currentPressType;
    bool _operationInProgress;
    unsigned long _operationStartTime;
    
    // LED feedback state
    bool _ledFeedbackActive;
    unsigned long _lastLedToggle;
    bool _ledState;
    
    // Private methods
    void _updateButtonState();
    void _handleButtonPress();
    void _handleButtonRelease();    void _processButtonPressType();
    void _detectMultiPress();
    void _checkMultiPressTimeout();
    
    // Operation handlers
    void _handleWifiStatusIndication();
    void _handleWifiReset();
    void _handlePasswordReset();
    void _handleFactoryReset();
    void _handleForceRestart();
    void _handleDeviceInfo();
    void _handleFirmwareInfo();
    
    // LED feedback methods
    void _startLedFeedback(ButtonPressType pressType);
    void _updateLedFeedback();
    void _stopLedFeedback();
    void _blinkLed(unsigned long interval);
    void _pulseLed(int color, unsigned long duration);
    void _showColorPattern(int* colors, int colorCount, unsigned long interval);
      // Utility methods
    bool _readButton();
    void _resetPressDetection();
};
