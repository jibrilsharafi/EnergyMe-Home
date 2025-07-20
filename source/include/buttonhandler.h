#pragma once

#include <Arduino.h>
#include <AdvancedLogger.h>
#include <Preferences.h>

#include "constants.h"
#include "led.h"
#include "customwifi.h"
#include "customtime.h"
#include "utils.h"

/**
 * Button press types for different functionalities
 */
enum class ButtonPressType
{
  NONE,
  SINGLE_SHORT,    // Restart
  SINGLE_MEDIUM,   // Password reset to default
  SINGLE_LONG,     // WiFi reset
  SINGLE_VERY_LONG // Factory reset
};

/**
 * Button handler class for managing critical functionality control
 * via GPIO0 button with immediate visual feedback during press
 */
class ButtonHandler
{ // TODO: add this to the readme
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
      AdvancedLogger &logger,
      CustomWifi &customWifi);

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

  /**
   * Get the name of the current operation being processed
   * @return Name of the operation
   */
  const char* getCurrentOperationName() const { return _operationName; }

  /**
   * Get the Unix timestamp of the last operation
   * @return Unix timestamp when the last operation was performed
   */
  unsigned long getCurrentOperationTimestamp() const { return _operationTimestamp; }

  /**
   * Clear the current operation name
   */
  void clearCurrentOperationName();

private:
  // Hardware
  int _buttonPin;

  // Dependencies
  AdvancedLogger &_logger;
  CustomWifi &_customWifi;
  Preferences _preferences;

  // Button state tracking
  bool _buttonPressed;
  bool _lastButtonState;
  unsigned long _buttonPressStartTime;
  unsigned long _lastDebounceTime;
  // Operation state
  ButtonPressType _currentPressType;
  char _operationName[BUTTON_HANDLER_OPERATION_BUFFER_SIZE];
  bool _operationInProgress;
  unsigned long _operationTimestamp;

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

  // Utility methods
  bool _readButton();
  // NVS methods for persistence
  void _saveLastOperationToNVS();
  void _loadLastOperationFromNVS();
  void _saveOperationTimestampToNVS();
  void _loadOperationTimestampFromNVS();
};
