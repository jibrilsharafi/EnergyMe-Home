#include "led.h"

namespace Led {
    // Hardware pins
    static int _redPin = INVALID_PIN;
    static int _greenPin = INVALID_PIN;
    static int _bluePin = INVALID_PIN;
    static int _brightness = DEFAULT_LED_BRIGHTNESS;
    
    // Task handles and queue
    static TaskHandle_t _ledTaskHandle = nullptr;
    static QueueHandle_t _ledQueue = nullptr;
    
    // LED command structure for queue
    struct LedCommand {
        LedPattern pattern;
        Color color;
        LedPriority priority;
        unsigned long long durationMs;  // 0 = indefinite
        unsigned long long timestamp;   // When command was issued
    };
    
    // Current active state
    struct LedState {
        LedPattern currentPattern = LED_PATTERN_OFF;
        Color currentColor = Colors::OFF;
        LedPriority currentPriority = 1; // PRIO_NORMAL
        unsigned long long patternStartTime = 0;
        unsigned long long patternDuration = 0;
        unsigned long long cycleStartTime = 0;
        bool isActive = false;
    };
    
    static LedState _state;
    
    // Private helper functions
    static void _setHardwareColor(const Color& color);
    static void _ledTask(void* parameter);
    static void _processPattern();
    static unsigned char _calculateBrightness(unsigned char value, float factor = 1.0f);
    static bool _loadConfiguration();
    static void _saveConfiguration();

    void begin(int redPin, int greenPin, int bluePin) {
        _redPin = redPin;
        _greenPin = greenPin;
        _bluePin = bluePin;

        // Initialize hardware
        pinMode(_redPin, OUTPUT);
        pinMode(_greenPin, OUTPUT);
        pinMode(_bluePin, OUTPUT);

        ledcAttach(_redPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_greenPin, LED_FREQUENCY, LED_RESOLUTION);
        ledcAttach(_bluePin, LED_FREQUENCY, LED_RESOLUTION);

        _loadConfiguration();

        // Create queue for LED commands
        _ledQueue = xQueueCreate(LED_QUEUE_SIZE, sizeof(LedCommand));
        if (_ledQueue == nullptr) {
            return; // Failed to create queue
        }

        // Create LED task
        BaseType_t result = xTaskCreate(
            _ledTask,
            LED_TASK_NAME,
            LED_TASK_STACK_SIZE,
            nullptr,
            LED_TASK_PRIORITY,
            &_ledTaskHandle
        );

        if (result != pdPASS) {
            vQueueDelete(_ledQueue);
            _ledQueue = nullptr;
            return; // Failed to create task
        }

        // Start with LED off
        setOff();
    }

    void end() {
        if (_ledTaskHandle != nullptr) {
            vTaskDelete(_ledTaskHandle);
            _ledTaskHandle = nullptr;
        }
        
        if (_ledQueue != nullptr) {
            vQueueDelete(_ledQueue);
            _ledQueue = nullptr;
        }
        
        // Turn off LED
        _setHardwareColor(Colors::OFF);
    }

    void resetToDefaults() {
        _brightness = DEFAULT_LED_BRIGHTNESS;
        _saveConfiguration();
    }

    bool _loadConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, true)) {
            _brightness = DEFAULT_LED_BRIGHTNESS;
            _saveConfiguration();
            return false;
        }

        _brightness = preferences.getInt(PREFERENCES_BRIGHTNESS_KEY, DEFAULT_LED_BRIGHTNESS);
        preferences.end();
        
        // Validate loaded value is within acceptable range
        _brightness = min(max(_brightness, 0), LED_MAX_BRIGHTNESS);
        return true;
    }

    void _saveConfiguration() {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, false)) {
            return;
        }
        preferences.putInt(PREFERENCES_BRIGHTNESS_KEY, _brightness);
        preferences.end();
    }

    void setBrightness(int brightness) {
        _brightness = min(max(brightness, 0), LED_MAX_BRIGHTNESS);
        _saveConfiguration();
    }

    int getBrightness() {
        return _brightness;
    }

    void setPattern(LedPattern pattern, Color color, LedPriority priority, unsigned long durationMs) {
        if (_ledQueue == nullptr) {
            return;
        }
        
        LedCommand command = {
            pattern,
            color,
            priority,
            durationMs,
            millis64()
        };
        
        // Try to send command to queue (non-blocking)
        xQueueSend(_ledQueue, &command, 0);
    }

    void clearPattern(LedPriority priority) {
        if (_ledQueue == nullptr) {
            return;
        }
        
        // Send OFF command with specified priority
        LedCommand command = {
            LED_PATTERN_OFF,
            Colors::OFF,
            priority,
            0,
            millis64()
        };
        
        xQueueSend(_ledQueue, &command, 0);
    }

    void clearAllPatterns() {
        if (_ledQueue == nullptr) {
            return;
        }
        
        // Clear the queue
        xQueueReset(_ledQueue);
        
        // Send OFF command with critical priority
        setPattern(LED_PATTERN_OFF, Colors::OFF, 15); // PRIO_CRITICAL
    }

    // Convenience functions
    void setRed(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::RED, priority);
    }

    void setGreen(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::GREEN, priority);
    }

    void setBlue(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::BLUE, priority);
    }

    void setYellow(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::YELLOW, priority);
    }

    void setPurple(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::PURPLE, priority);
    }

    void setCyan(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::CYAN, priority);
    }

    void setOrange(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::ORANGE, priority);
    }

    void setWhite(LedPriority priority) {
        setPattern(LED_PATTERN_SOLID, Colors::WHITE, priority);
    }

    void setOff(LedPriority priority) {
        setPattern(LED_PATTERN_OFF, Colors::OFF, priority);
    }

    // Pattern convenience functions
    void blinkRed(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_BLINK_FAST, Colors::RED, priority, durationMs);
    }

    void blinkGreenSlow(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_BLINK_SLOW, Colors::GREEN, priority, durationMs);
    }
    
    void blinkGreenFast(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_BLINK_FAST, Colors::GREEN, priority, durationMs);
    }

    void pulseBlue(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_PULSE, Colors::BLUE, priority, durationMs);
    }

    void blinkPurpleSlow(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_BLINK_SLOW, Colors::PURPLE, priority, durationMs);
    }

    void doubleBlinkYellow(LedPriority priority, unsigned long long durationMs) {
        setPattern(LED_PATTERN_DOUBLE_BLINK, Colors::YELLOW, priority, durationMs);
    }

    // Private implementation functions
    static void _setHardwareColor(const Color& color) {
        // Validate pins are set before using them
        if (_redPin == INVALID_PIN || _greenPin == INVALID_PIN || _bluePin == INVALID_PIN) {
            return;
        }
        
        ledcWrite(_redPin, _calculateBrightness(color.red));
        ledcWrite(_greenPin, _calculateBrightness(color.green));
        ledcWrite(_bluePin, _calculateBrightness(color.blue));
    }

    static unsigned char _calculateBrightness(unsigned char value, float factor) {
        return (unsigned char)(value * _brightness * factor / LED_MAX_BRIGHTNESS);
    }

    static void _ledTask(void* parameter) {
        LedCommand command;
        unsigned long long currentTime;
        
        while (true) {
            // Check for new commands in queue
            if (xQueueReceive(_ledQueue, &command, pdMS_TO_TICKS(LED_TASK_DELAY_MS)) == pdTRUE) {
                currentTime = millis64();
                
                // Only accept commands with equal or higher priority
                if (command.priority >= _state.currentPriority || !_state.isActive) {
                    // Check if this is a duration-based override or permanent change
                    bool shouldUpdate = false;
                    
                    if (command.priority > _state.currentPriority) {
                        // Higher priority always overrides
                        shouldUpdate = true;
                    } else if (command.priority == _state.currentPriority) {
                        // Same priority: update if current pattern has expired or is OFF
                        if (!_state.isActive || _state.currentPattern == LED_PATTERN_OFF) {
                            shouldUpdate = true;
                        } else if (_state.patternDuration > 0 && 
                                  (currentTime - _state.patternStartTime) >= _state.patternDuration) {
                            shouldUpdate = true;
                        }
                    }
                    
                    if (shouldUpdate) {
                        _state.currentPattern = command.pattern;
                        _state.currentColor = command.color;
                        _state.currentPriority = command.priority;
                        _state.patternStartTime = currentTime;
                        _state.patternDuration = command.durationMs;
                        _state.cycleStartTime = currentTime;
                        _state.isActive = (command.pattern != LED_PATTERN_OFF);
                    }
                }
            }
            
            // Check if current pattern has expired
            currentTime = millis64();
            if (_state.isActive && _state.patternDuration > 0 && 
                (currentTime - _state.patternStartTime) >= _state.patternDuration) {
                _state.currentPattern = LED_PATTERN_OFF;
                _state.currentColor = Colors::OFF;
                _state.currentPriority = PRIO_NORMAL;
                _state.isActive = false;
            }
            
            // Process current pattern
            _processPattern();
            
            delay(LED_TASK_DELAY_MS);
        }
    }

    static void _processPattern() {
        unsigned long long currentTime = millis64();
        unsigned long long elapsed = currentTime - _state.cycleStartTime;
        Color outputColor = _state.currentColor;
        bool shouldOutput = true;
        
        switch (_state.currentPattern) {
            case LED_PATTERN_SOLID:
                // Always on with current color
                break;
                
            case LED_PATTERN_OFF:
                outputColor = Colors::OFF;
                break;
                
            case LED_PATTERN_BLINK_SLOW:
                // 1 second on, 1 second off
                shouldOutput = ((elapsed / 1000) % 2) == 0;
                if (!shouldOutput) outputColor = Colors::OFF;
                break;
                
            case LED_PATTERN_BLINK_FAST:
                // 250ms on, 250ms off
                shouldOutput = ((elapsed / 250) % 2) == 0;
                if (!shouldOutput) outputColor = Colors::OFF;
                break;
                
            case LED_PATTERN_PULSE: {
                // Smooth fade in/out over 2 seconds
                unsigned long cycle = elapsed % 2000; // 2 second cycle
                float factor;
                if (cycle < 1000) {
                    // Fade in
                    factor = (float)cycle / 1000.0f;
                } else {
                    // Fade out
                    factor = 1.0f - ((float)(cycle - 1000) / 1000.0f);
                }
                outputColor.red = _calculateBrightness(outputColor.red, factor);
                outputColor.green = _calculateBrightness(outputColor.green, factor);
                outputColor.blue = _calculateBrightness(outputColor.blue, factor);
                break;
            }
                
            case LED_PATTERN_DOUBLE_BLINK: {
                // Two quick blinks (100ms on, 100ms off, 100ms on, 100ms off), then 800ms pause
                unsigned long cycle = elapsed % 1200; // 1.2 second cycle
                if (cycle < 100 || (cycle >= 200 && cycle < 300)) {
                    // On periods
                    shouldOutput = true;
                } else if (cycle < 400) {
                    // Off periods between blinks
                    shouldOutput = false;
                    outputColor = Colors::OFF;
                } else {
                    // Long pause period
                    shouldOutput = false;
                    outputColor = Colors::OFF;
                }
                break;
            }
        }
        
        _setHardwareColor(outputColor);
    }
}