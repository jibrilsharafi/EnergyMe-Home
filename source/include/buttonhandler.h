// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>

// #include "constants.h"
#include "led.h"
#include "customwifi.h"
#include "customserver.h"
#include "utils.h"

#define BUTTON_TASK_NAME "button_task"
// 3 KB crashed a bench device: LOG_DEBUG("Button pressed") on every press pushes
// vsnprintf's 512-byte message buffer plus newlib's internal vfprintf stack usage
// on top of the task's own frames, and the resulting overflow panicked inside
// _svfprintf_r (see the crash archived 2026-08-08, task=button_task). Matches
// every other logging task in the firmware (4-8 KB); only the LED task goes
// smaller, and only because it deliberately never logs.
#define BUTTON_TASK_STACK_SIZE (4 * 1024)
#define BUTTON_TASK_PRIORITY 2

// Timing constants
#define BUTTON_DEBOUNCE_TIME 50
#define BUTTON_SHORT_PRESS_TIME (2 * 1000)
#define BUTTON_MEDIUM_PRESS_TIME (5 * 1000)
#define BUTTON_LONG_PRESS_TIME (10 * 1000)
#define BUTTON_VERY_LONG_PRESS_TIME (15 * 1000)
#define BUTTON_MAX_PRESS_TIME (20 * 1000)

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
    void begin(uint8_t buttonPin);
    void stop();
    
    // Task information
    TaskInfo getTaskInfo();
}
