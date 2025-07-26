#pragma once

#include <Arduino.h>
#include <Preferences.h>

// #include "constants.h"
#include "led.h"
#include "customwifi.h"
#include "customtime.h"
#include "utils.h"

#define BUTTON_TASK_NAME "button_task"
#define BUTTON_TASK_STACK_SIZE 6144
#define BUTTON_TASK_PRIORITY 2

#define PREFERENCES_LAST_OPERATION_KEY "last_operation"
#define PREFERENCES_LAST_OPERATION_TIMESTAMP_KEY "last_op_timestamp" // Cannot be too long

// Timing constants
#define BUTTON_DEBOUNCE_TIME 50 // Debounce time in milliseconds
#define BUTTON_SHORT_PRESS_TIME 2000 // Short press duration (Restart)
#define BUTTON_MEDIUM_PRESS_TIME 6000 // Medium press duration (Password reset)
#define BUTTON_LONG_PRESS_TIME 10000 // Long press duration (WiFi reset)
#define BUTTON_VERY_LONG_PRESS_TIME 15000 // Very long press duration (Factory reset)
#define BUTTON_MAX_PRESS_TIME 20000 // Maximum press duration

#define ZERO_START_TIME 0 // Used to indicate no button press has started

enum class ButtonPressType
{
  NONE,
  SINGLE_SHORT,    // Restart
  SINGLE_MEDIUM,   // Password reset to default
  SINGLE_LONG,     // WiFi reset
  SINGLE_VERY_LONG // Factory reset
};

namespace ButtonHandler {
    void begin(int buttonPin);

    ButtonPressType getCurrentPressType();
    bool isOperationInProgress();
    void getCurrentOperationName(char* buffer, size_t bufferSize);
    unsigned long getCurrentOperationTimestamp();
    void clearCurrentOperationName();
}
