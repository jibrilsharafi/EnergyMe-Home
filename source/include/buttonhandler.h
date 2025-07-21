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

#define PREFERENCES_NAMESPACE_BUTTON "button_ns"
#define PREFERENCES_LAST_OPERATION_KEY "last_operation"
#define PREFERENCES_LAST_OPERATION_TIMESTAMP_KEY "last_operation_timestamp"

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
