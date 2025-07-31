// The LED functionality uses a FreeRTOS task to handle patterns asynchronously
// with support for different priorities and patterns.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "constants.h"
#include "utils.h"

#define PREFERENCES_BRIGHTNESS_KEY "brightness"

#define INVALID_PIN -1 // Used for initialization of pins
#define DEFAULT_LED_BRIGHTNESS 191 // 75% of the maximum brightness
#define LED_RESOLUTION 8 // Resolution for PWM, 8 bits (0-255)
#define LED_MAX_BRIGHTNESS 255 // 8-bit PWM
#define LED_FREQUENCY 5000 // Frequency for PWM, in Hz. Quite standard

// LED Task configuration
#define LED_TASK_NAME "led_task"
#define LED_TASK_STACK_SIZE 4096
#define LED_TASK_PRIORITY 1
#define LED_QUEUE_SIZE 10
#define LED_TASK_DELAY_MS 50

// LED Pattern types
enum class LedPattern {
    SOLID,          // Solid color
    BLINK_SLOW,     // 1 second on, 1 second off
    BLINK_FAST,     // 250ms on, 250ms off
    PULSE,          // Smooth fade in/out
    DOUBLE_BLINK,   // Two quick blinks, then pause
    OFF             // LED off
};

// Priority levels (higher number = higher priority)  
typedef unsigned char LedPriority;

namespace Led {
    // Priority constants
    const LedPriority PRIO_NORMAL = 1;     // Normal operation status
    const LedPriority PRIO_MEDIUM = 5;     // Network/connection status  
    const LedPriority PRIO_URGENT = 10;    // Updates, errors, critical states
    const LedPriority PRIO_CRITICAL = 15;  // Override everything

    // Color structure
    struct Color {
        unsigned char red;
        unsigned char green;
        unsigned char blue;
        
        Color(unsigned char r = 0, unsigned char g = 0, unsigned char b = 0) : red(r), green(g), blue(b) {}
    };

    // Predefined colors
    namespace Colors {
        const Color RED(255, 0, 0);
        const Color GREEN(0, 255, 0);
        const Color BLUE(0, 0, 255);
        const Color YELLOW(255, 255, 0);
        const Color PURPLE(255, 0, 255);
        const Color CYAN(0, 255, 255);
        const Color ORANGE(255, 128, 0);
        const Color WHITE(255, 255, 255);
        const Color OFF(0, 0, 0);
    }

    void begin(int redPin, int greenPin, int bluePin);
    void end();
    void resetToDefaults();

    void setBrightness(int brightness);
    int getBrightness();

    // Pattern control functions
    void setPattern(LedPattern pattern, Color color, LedPriority priority = 1, unsigned long long durationMs = 0);
    void clearPattern(LedPriority priority);
    void clearAllPatterns();

    // Convenience functions for common patterns
    void setRed(LedPriority priority = 1);
    void setGreen(LedPriority priority = 1);
    void setBlue(LedPriority priority = 1);
    void setYellow(LedPriority priority = 1);
    void setPurple(LedPriority priority = 1);
    void setCyan(LedPriority priority = 1);
    void setOrange(LedPriority priority = 1);
    void setWhite(LedPriority priority = 1);
    void setOff(LedPriority priority = 1);

    // Pattern convenience functions
    void blinkOrangeFast(LedPriority priority = 5, unsigned long long durationMs = 0);
    void blinkRed(LedPriority priority = 5, unsigned long long durationMs = 0);
    void blinkBlueSlow(LedPriority priority = 1, unsigned long long durationMs = 0);
    void blinkBlueFast(LedPriority priority = 1, unsigned long long durationMs = 0);
    void blinkGreenSlow(LedPriority priority = 1, unsigned long long durationMs = 0);
    void blinkGreenFast(LedPriority priority = 1, unsigned long long durationMs = 0);
    void pulseBlue(LedPriority priority = 1, unsigned long long durationMs = 0);
    void blinkPurpleSlow(LedPriority priority = 5, unsigned long long durationMs = 0);
    void blinkPurpleFast(LedPriority priority = 5, unsigned long long durationMs = 0);
    void doubleBlinkYellow(LedPriority priority = 10, unsigned long long durationMs = 0);
}