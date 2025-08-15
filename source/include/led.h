// The LED functionality uses a FreeRTOS task to handle patterns asynchronously
// with support for different priorities and patterns.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

#include "constants.h"
#include "utils.h"
#include "structs.h"

#define PREFERENCES_BRIGHTNESS_KEY "brightness"

#define INVALID_PIN 255 // Used for initialization of pins
#define DEFAULT_LED_BRIGHTNESS_PERCENT 75 // Default brightness percentage
#define LED_RESOLUTION 8 // Resolution for PWM, 8 bits (0-255)
#define LED_MAX_BRIGHTNESS_PERCENT 100 // Maximum brightness percentage
#define LED_FREQUENCY 5000 // Frequency for PWM, in Hz. Quite standard

// LED Task configuration
#define LED_TASK_NAME "led_task"
#define LED_TASK_STACK_SIZE (2 * 1024) // No need for 4 kB since there is no logger usage
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
typedef uint8_t LedPriority;

namespace Led {
    // Priority constants
    const LedPriority PRIO_NORMAL = 1;     // Normal operation status
    const LedPriority PRIO_MEDIUM = 5;     // Network/connection status  
    const LedPriority PRIO_URGENT = 10;    // Updates, errors, critical states
    const LedPriority PRIO_CRITICAL = 15;  // Override everything

    // Color structure
    struct Color {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        
        Color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) : red(r), green(g), blue(b) {}
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

    void begin(uint8_t redPin, uint8_t greenPin, uint8_t bluePin);
    void stop();

    void resetToDefaults();

    void setBrightness(uint8_t brightness);
    uint8_t getBrightness();
    inline bool isBrightnessValid(uint8_t brightness) { return brightness <= LED_MAX_BRIGHTNESS_PERCENT; }

    // Pattern control functions
    void setPattern(LedPattern pattern, Color color, LedPriority priority = 1, uint64_t durationMs = 0);
    void clearPattern(LedPriority priority);
    void clearAllPatterns();

    // Convenience functions
    inline void setRed(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::RED, priority); }
    inline void setGreen(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::GREEN, priority); }
    inline void setBlue(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::BLUE, priority); }
    inline void setYellow(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::YELLOW, priority); }
    inline void setPurple(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::PURPLE, priority); }
    inline void setCyan(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::CYAN, priority); }   
    inline void setOrange(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::ORANGE, priority); }
    inline void setWhite(LedPriority priority = 1) { setPattern(LedPattern::SOLID, Colors::WHITE, priority); }
    inline void setOff(LedPriority priority = 1) { setPattern(LedPattern::OFF, Colors::OFF, priority); }

    // Pattern convenience functions
    inline void blinkOrangeFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::ORANGE, priority, durationMs); }
    inline void blinkRedSlow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_SLOW, Colors::RED, priority, durationMs); }
    inline void blinkRedFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::RED, priority, durationMs); }
    inline void blinkBlueSlow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_SLOW, Colors::BLUE, priority, durationMs); }
    inline void blinkBlueFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::BLUE, priority, durationMs); }
    inline void pulseBlue(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::PULSE, Colors::BLUE, priority, durationMs); }
    inline void blinkGreenSlow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_SLOW, Colors::GREEN, priority, durationMs); }
    inline void blinkGreenFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::GREEN, priority, durationMs); }
    inline void blinkPurpleSlow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_SLOW, Colors::PURPLE, priority, durationMs); }
    inline void blinkPurpleFast(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::BLINK_FAST, Colors::PURPLE, priority, durationMs); }
    inline void doubleBlinkYellow(LedPriority priority = 1, uint64_t durationMs = 0) { setPattern(LedPattern::DOUBLE_BLINK, Colors::YELLOW, priority, durationMs); }

    // Task information
    TaskInfo getTaskInfo();
}