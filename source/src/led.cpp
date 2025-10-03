#include "led.h"

namespace Led
{
    // Hardware pins
    static uint8_t _redPin = INVALID_PIN;
    static uint8_t _greenPin = INVALID_PIN;
    static uint8_t _bluePin = INVALID_PIN;
    static uint8_t _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;

    // Task handles and queue
    static TaskHandle_t _ledTaskHandle = nullptr;
    static QueueHandle_t _ledQueue = nullptr;
    static bool _ledTaskShouldRun = false;

    // LED command structure for queue
    struct LedCommand
    {
        LedPattern pattern;
        Color color;
        LedPriority priority;
        uint64_t durationMs; // 0 = indefinite
        uint64_t timestamp;  // When command was issued
    };

    // Current active state
    struct LedState
    {
        LedPattern currentPattern = LedPattern::OFF;
        Color currentColor = Colors::OFF;
        LedPriority currentPriority = 1; // PRIO_NORMAL
        uint64_t patternStartTime = 0;
        uint64_t patternDuration = 0;
        uint64_t cycleStartTime = 0;
        bool isActive = false;
    };

    static LedState _state;

    // Private helper functions
    static void _setHardwareColor(const Color &color);
    static void _ledTask(void *parameter);
    static void _processPattern();
    static uint8_t _calculateBrightness(uint8_t value, float factor = 1.0f);
    static bool _loadConfiguration();
    static void _saveConfiguration();

    void begin(uint8_t redPin, uint8_t greenPin, uint8_t bluePin)
    {
        if (_ledTaskHandle != nullptr) { return; }

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
        if (_ledQueue == nullptr) { return; } // Failed to create queue

        // Create LED task
        LOG_DEBUG("Starting LED task with %d bytes stack in internal RAM", LED_TASK_STACK_SIZE);
        
        BaseType_t result = xTaskCreate(
            _ledTask,
            LED_TASK_NAME,
            LED_TASK_STACK_SIZE,
            nullptr,
            LED_TASK_PRIORITY,
            &_ledTaskHandle);

        if (result != pdPASS)
        {
            LOG_ERROR("Failed to create LED task");
            vQueueDelete(_ledQueue);
            _ledQueue = nullptr;
            _ledTaskHandle = NULL;
            return; // Failed to create task
        }

        // Start with LED off
        setOff();
    }

    void stop()
    {
        stopTaskGracefully(&_ledTaskHandle, "LED task");

        if (_ledQueue != nullptr)
        {
            vQueueDelete(_ledQueue);
            _ledQueue = nullptr;
        }

        // Turn off LED
        _setHardwareColor(Colors::OFF);
    }

    static void _ledTask(void *parameter) // FIXME: find a better way to handle LED colors. Now it does not behave correctly (maybe more like a state machine?)
    {
        LedCommand command;
        uint64_t currentTime;

        _ledTaskShouldRun = true;
        while (_ledTaskShouldRun)
        {

            // Check for new commands in queue with timeout
            if (xQueueReceive(_ledQueue, &command, pdMS_TO_TICKS(LED_TASK_DELAY_MS)) == pdTRUE)
            {
                currentTime = millis64();

                // Always process commands, but handle priority logic
                if (command.priority >= _state.currentPriority || !_state.isActive)
                {
                    // Higher or equal priority: execute immediately
                    _state.currentPattern = command.pattern;
                    _state.currentColor = command.color;
                    _state.currentPriority = command.priority;
                    _state.patternStartTime = currentTime;
                    _state.patternDuration = command.durationMs;
                    _state.cycleStartTime = currentTime;
                    _state.isActive = (command.pattern != LedPattern::OFF);
                }
                else
                {
                    // Lower priority: put back in queue for later
                    if (_ledQueue) xQueueSendToBack(_ledQueue, &command, 0);
                }
            }

            // Check if current pattern has expired
            currentTime = millis64();
            if (_state.isActive && _state.patternDuration > 0 &&
                (currentTime - _state.patternStartTime) >= _state.patternDuration)
            {
                _state.currentPattern = LedPattern::OFF;
                _state.currentColor = Colors::OFF;
                _state.currentPriority = PRIO_NORMAL;
                _state.isActive = false;
                
                // Process any queued commands immediately after expiration
                // This allows lower priority commands to execute
            }

            // Process current pattern
            _processPattern();

            // Wait for stop notification with timeout (blocking) - ensures proper yielding
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_TASK_DELAY_MS)) > 0)
            {
                _ledTaskShouldRun = false;
                break;
            }
        }

        _ledTaskHandle = nullptr;
        vTaskDelete(NULL);
    }

    void resetToDefaults()
    {
        _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;
        _saveConfiguration();
    }

    bool _loadConfiguration()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, true))
        {
            _brightness = DEFAULT_LED_BRIGHTNESS_PERCENT;
            _saveConfiguration();
            return false;
        }

        _brightness = preferences.getUChar(PREFERENCES_BRIGHTNESS_KEY, DEFAULT_LED_BRIGHTNESS_PERCENT);
        preferences.end();

        // Validate loaded value is within acceptable range
        _brightness = min(max(_brightness, (uint8_t)0), (uint8_t)LED_MAX_BRIGHTNESS_PERCENT);
        return true;
    }

    void _saveConfiguration()
    {
        Preferences preferences;
        if (!preferences.begin(PREFERENCES_NAMESPACE_LED, false)) { return; }
        preferences.putUChar(PREFERENCES_BRIGHTNESS_KEY, _brightness);
        preferences.end();
    }

    void setBrightness(uint8_t brightness)
    {
        _brightness = min(max(brightness, (uint8_t)0), (uint8_t)LED_MAX_BRIGHTNESS_PERCENT);
        _saveConfiguration();
    }

    uint8_t getBrightness() { return _brightness; }

    void setPattern(LedPattern pattern, Color color, LedPriority priority, uint64_t durationMs)
    {
        if (_ledQueue == nullptr) { return; }

        LedCommand command = {
            pattern,
            color,
            priority,
            durationMs,
            millis64()};

        // Try to send command to queue (non-blocking)
        xQueueSend(_ledQueue, &command, 0);
    }

    void clearPattern(LedPriority priority)
    {
        if (_ledQueue == nullptr) { return; }

        // Send OFF command with specified priority
        LedCommand command = {
            LedPattern::OFF,
            Colors::OFF,
            priority,
            0,
            millis64()};

        xQueueSend(_ledQueue, &command, 0);
    }

    void clearAllPatterns()
    {
        if (_ledQueue == nullptr) { return; }

        // Clear the queue
        xQueueReset(_ledQueue);

        // Send OFF command with critical priority
        setPattern(LedPattern::OFF, Colors::OFF, 15); // PRIO_CRITICAL
    }

    // Private implementation functions
    static void _setHardwareColor(const Color &color)
    {
        // Validate pins are set before using them
        if (_redPin == INVALID_PIN || _greenPin == INVALID_PIN || _bluePin == INVALID_PIN) { return; }

        ledcWrite(_redPin, _calculateBrightness(color.red));
        ledcWrite(_greenPin, _calculateBrightness(color.green));
        ledcWrite(_bluePin, _calculateBrightness(color.blue));
    }

    static uint8_t _calculateBrightness(uint8_t value, float factor)
    {
        return (uint8_t)(value * (float)_brightness * factor / (float)LED_MAX_BRIGHTNESS_PERCENT);
    }

    static void _processPattern()
    {
        uint64_t currentTime = millis64();
        uint64_t elapsed = currentTime - _state.cycleStartTime;
        Color outputColor = _state.currentColor;
        bool shouldOutput = true;

        switch (_state.currentPattern)
        {
        case LedPattern::SOLID:
            // Always on with current color
            break;

        case LedPattern::OFF:
            outputColor = Colors::OFF;
            break;

        case LedPattern::BLINK_SLOW:
            // 1 second on, 1 second off
            shouldOutput = ((elapsed / 1000) % 2) == 0;
            if (!shouldOutput)
                outputColor = Colors::OFF;
            break;

        case LedPattern::BLINK_FAST:
            // 250ms on, 250ms off
            shouldOutput = ((elapsed / 250) % 2) == 0;
            if (!shouldOutput)
                outputColor = Colors::OFF;
            break;

        case LedPattern::PULSE:
        {
            // Smooth fade in/out over 2 seconds
            uint64_t cycle = elapsed % 2000; // 2 second cycle
            float factor;
            if (cycle < 1000)
            {
                // Fade in
                factor = (float)cycle / 1000.0f;
            }
            else
            {
                // Fade out
                factor = 1.0f - ((float)(cycle - 1000) / 1000.0f);
            }
            outputColor.red = _calculateBrightness(outputColor.red, factor);
            outputColor.green = _calculateBrightness(outputColor.green, factor);
            outputColor.blue = _calculateBrightness(outputColor.blue, factor);
            break;
        }

        case LedPattern::DOUBLE_BLINK:
        {
            // Two quick blinks (100ms on, 100ms off, 100ms on, 100ms off), then 800ms pause
            uint64_t cycle = elapsed % 1200; // 1.2 second cycle
            if (cycle < 100 || (cycle >= 200 && cycle < 300))
            {
                // On periods
                shouldOutput = true;
            }
            else if (cycle < 400)
            {
                // Off periods between blinks
                shouldOutput = false;
                outputColor = Colors::OFF;
            }
            else
            {
                // Long pause period
                shouldOutput = false;
                outputColor = Colors::OFF;
            }
            break;
        }
        }

        _setHardwareColor(outputColor);
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_ledTaskHandle, LED_TASK_STACK_SIZE);
    }
}