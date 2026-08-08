// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

#include "constants.h"
#include "utils.h"
#include "structs.h"
#include "led_state.h"

// Firmware adapter over LedState. Owns the pins, the render task, the layer table's
// mutex and the persisted brightness; every rule about which indication wins and
// what a pattern looks like lives in lib/led_state and is unit-tested on the host.

#define PREFERENCES_BRIGHTNESS_KEY "brightness"

#define INVALID_PIN 255 // Used for initialization of pins
#define DEFAULT_LED_BRIGHTNESS_PERCENT 75 // Default brightness percentage
#define LED_RESOLUTION 8 // Resolution for PWM, 8 bits (0-255)
#define LED_FREQUENCY 5000 // Frequency for PWM, in Hz. Quite standard

constexpr uint8_t LED_MAX_BRIGHTNESS_PERCENT = LedState::MAX_BRIGHTNESS_PERCENT;

// LED Task configuration
#define LED_TASK_NAME "led_task"
#define LED_TASK_STACK_SIZE (2 * 1024) // No need for 4 kB since there is no logger usage
#define LED_TASK_PRIORITY 1

// Render period. The shortest segment of any pattern is DOUBLE_BLINK's 100 ms, so a
// tick of 25 ms samples it four times. It also bounds how long a setPattern() call
// takes to appear, since there is no wake-on-write.
#define LED_TASK_DELAY_MS 25

// Critical sections are a struct copy - no I/O, no logging, no allocation - so this
// only ever expires if something is badly wrong, in which case dropping one
// indication beats blocking the caller.
#define LED_MUTEX_TIMEOUT_MS 50

using LedPattern = LedState::Pattern;

// Priority levels (higher number = higher priority)
typedef uint8_t LedPriority;

namespace Led {
    // Priority constants. Kept as the public spelling for the ~40 existing call
    // sites; layerForPriority() maps them onto LedState::Layer.
    //
    // There is deliberately no PRIO_* for the user layer. It is not part of this
    // scale - it sits between STATUS and NETWORK - and reaching it goes through the
    // Layer overload of setPattern(), which is what the API handlers use.
    const LedPriority PRIO_NORMAL = 1;      // Normal operation status
    const LedPriority PRIO_MEDIUM = 5;      // Network/connection status
    const LedPriority PRIO_URGENT = 10;     // Updates, errors, critical states
    const LedPriority PRIO_CRITICAL = 15;   // Override everything

    using Color = LedState::Rgb;
    namespace Colors = LedState::Colors;

    // What the LED is showing right now.
    struct Snapshot {
        // False when the layer table could not be read. Distinct from active.any on
        // purpose: "I could not look" must never be reported as "the LED is off".
        bool valid = false;
        LedState::Active active;
        bool isLit = false;
        uint8_t brightness = 0;
    };

    void begin(uint8_t redPin, uint8_t greenPin, uint8_t bluePin);

    void setBrightness(uint8_t brightness);
    uint8_t getBrightness();
    inline bool isBrightnessValid(uint8_t brightness) { return brightness <= LED_MAX_BRIGHTNESS_PERCENT; }

    // Writes one layer, replacing what it held. Never affects another layer.
    void setPattern(LedState::Layer layer, LedPattern pattern, Color color, uint64_t durationMs = 0);
    void setPattern(LedPattern pattern, Color color, LedPriority priority = 1, uint64_t durationMs = 0);

    // Frees a layer, revealing the highest-priority layer still occupied.
    void clearLayer(LedState::Layer layer);
    void clearPattern(LedPriority priority);

    Snapshot getState();

    // Convenience functions
    inline void setRed(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::RED, priority); }
    inline void setGreen(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::GREEN, priority); }
    inline void setBlue(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::BLUE, priority); }
    inline void setYellow(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::YELLOW, priority); }
    inline void setPurple(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::PURPLE, priority); }
    inline void setCyan(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::CYAN, priority); }
    inline void setOrange(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::ORANGE, priority); }
    inline void setWhite(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::WHITE, priority); }

    // Pattern convenience functions
    inline void blinkOrangeFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::ORANGE, priority, durationMs); }
    inline void blinkRedFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::RED, priority, durationMs); }
    inline void blinkBlueFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::BLUE, priority, durationMs); }
    inline void pulseBlue(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::PULSE, Colors::BLUE, priority, durationMs); }
    inline void blinkGreenSlow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_SLOW, Colors::GREEN, priority, durationMs); }
    inline void blinkGreenFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::GREEN, priority, durationMs); }
    inline void blinkPurpleFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::PURPLE, priority, durationMs); }
    inline void doubleBlinkYellow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::DOUBLE_BLINK, Colors::YELLOW, priority, durationMs); }

    // Task information
    TaskInfo getTaskInfo();
}
