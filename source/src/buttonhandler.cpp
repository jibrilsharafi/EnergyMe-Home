#include "buttonhandler.h"

namespace ButtonHandler
{
    // Static state variables
    static uint8_t _buttonPin = INVALID_PIN; // Default pin
    static TaskHandle_t _buttonTaskHandle = NULL;
    static SemaphoreHandle_t _buttonSemaphore = NULL;

    // Button state
    static volatile uint64_t _buttonPressStartTime = ZERO_START_TIME;
    static volatile bool _buttonPressed = false;
    static ButtonPressType _currentPressType = ButtonPressType::NONE;
    static bool _operationInProgress = false;


    // Private function declarations
    static void _buttonISR();
    static void _buttonTask(void *parameter);
    static void _processButtonPress(uint64_t pressDuration);
    static void _updateVisualFeedback(uint64_t pressDuration);

    // Operation handlers
    static void _handleRestart();
    static void _handlePasswordReset();
    static void _handleWifiReset();
    static void _handleFactoryReset();

    void begin(uint8_t buttonPin)
    {
        _buttonPin = buttonPin;
        LOG_DEBUG("Initializing interrupt-driven button handler on GPIO%d", _buttonPin);

        // Setup GPIO with pull-up
        pinMode(_buttonPin, INPUT_PULLUP);

        // Create binary semaphore for ISR communication
        _buttonSemaphore = xSemaphoreCreateBinary();
        if (_buttonSemaphore == NULL)
        {
            LOG_ERROR("Failed to create button semaphore");
            return;
        }

        // Create button handling task with internal RAM stack (performs Preferences operations)
        LOG_DEBUG("Starting button task with %d bytes stack in internal RAM (performs Preferences operations)", BUTTON_TASK_STACK_SIZE);

        BaseType_t result = xTaskCreate(
            _buttonTask,
            BUTTON_TASK_NAME,
            BUTTON_TASK_STACK_SIZE,
            NULL,
            BUTTON_TASK_PRIORITY,
            &_buttonTaskHandle);

        if (result != pdPASS)
        {
            LOG_ERROR("Failed to create button task");
            vSemaphoreDelete(_buttonSemaphore);
            _buttonSemaphore = NULL;
            return;
        }

        // Setup interrupt on both edges (press and release)
        attachInterrupt(digitalPinToInterrupt(_buttonPin), _buttonISR, CHANGE);

        LOG_DEBUG("Button handler ready - interrupt-driven with task processing");
    }

    void stop() 
    {
        LOG_DEBUG("Stopping button handler");

        // Detach interrupt
        detachInterrupt(_buttonPin);

        // Stop task gracefully
        stopTaskGracefully(&_buttonTaskHandle, "Button task");

        // Delete semaphore
        if (_buttonSemaphore != NULL)
        {
            vSemaphoreDelete(_buttonSemaphore);
            _buttonSemaphore = NULL;
        }

        _buttonPin = INVALID_PIN; // Reset pin
    }

    static void IRAM_ATTR _buttonISR()
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        // Read current button state (inverted due to pull-up)
        // Need to read because the ISR is triggered on both edges
        // to handle both press and release events
        bool buttonState = !digitalRead(_buttonPin);

        if (buttonState && !_buttonPressed)
        {
            // Button press detected
            _buttonPressed = true;
            _buttonPressStartTime = millis64();

            // Notify task of button press
            xSemaphoreGiveFromISR(_buttonSemaphore, &xHigherPriorityTaskWoken);
        }
        else if (!buttonState && _buttonPressed)
        {
            // Button release detected
            _buttonPressed = false;

            // Notify task of button release
            xSemaphoreGiveFromISR(_buttonSemaphore, &xHigherPriorityTaskWoken);
        }

        // Wake higher priority task if needed
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    static void _buttonTask(void *parameter)
    {
        TickType_t feedbackUpdateInterval = pdMS_TO_TICKS(100); // Update visual feedback every 100ms

        // This task should "never" be stopped, and to avoid over-complicating due to the semaphore, we don't stick to the standard approach
        while (true)
        {
            // Wait for button event or timeout for visual feedback updates
            if (xSemaphoreTake(_buttonSemaphore, feedbackUpdateInterval)) {
                // Button event occurred
                delay(BUTTON_DEBOUNCE_TIME); // Simple debounce

                if (!_buttonPressed && _buttonPressStartTime > ZERO_START_TIME)
                {
                    // Button was released - process the press
                    uint64_t pressDuration = millis64() - _buttonPressStartTime;
                    LOG_DEBUG("Button released after %llu ms", pressDuration);

                    _processButtonPress(pressDuration);
                    _buttonPressStartTime = ZERO_START_TIME;

                    // Clear visual feedback
                    Led::clearPattern(Led::PRIO_URGENT);
                }
                else if (_buttonPressed)
                {
                    // Button was pressed - start visual feedback
                    LOG_DEBUG("Button pressed");

                    Led::setBrightness(max(Led::getBrightness(), (uint8_t)1));
                    Led::setWhite(Led::PRIO_URGENT);
                }
            // Here it means it is still being pressed
            } else if (_buttonPressed && _buttonPressStartTime > ZERO_START_TIME) {
                // Timeout occurred while button is pressed - update visual feedback
                uint64_t pressDuration = millis64() - _buttonPressStartTime;
                _updateVisualFeedback(pressDuration);
            }
        }

        LOG_DEBUG("Button task stopping");
        _buttonTaskHandle = NULL;
        vTaskDelete(NULL);
    }

    static void _processButtonPress(uint64_t pressDuration)
    {
        // Determine press type based on duration
        if (pressDuration >= BUTTON_SHORT_PRESS_TIME && pressDuration < BUTTON_MEDIUM_PRESS_TIME)
        {
            _currentPressType = ButtonPressType::SINGLE_SHORT;
            _handleRestart();
        }
        else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME && pressDuration < BUTTON_LONG_PRESS_TIME)
        {
            _currentPressType = ButtonPressType::SINGLE_MEDIUM;
            _handlePasswordReset();
        }
        else if (pressDuration >= BUTTON_LONG_PRESS_TIME && pressDuration < BUTTON_VERY_LONG_PRESS_TIME)
        {
            _currentPressType = ButtonPressType::SINGLE_LONG;
            _handleWifiReset();
        }
        else if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME && pressDuration < BUTTON_MAX_PRESS_TIME)
        {
            _currentPressType = ButtonPressType::SINGLE_VERY_LONG;
            _handleFactoryReset();
        }
        else
        {
            _currentPressType = ButtonPressType::NONE;
            LOG_DEBUG("Button press duration %llu ms - no action", pressDuration);
        }
    }

    static void _updateVisualFeedback(uint64_t pressDuration)
    {
        // Provide immediate visual feedback based on current press duration
        if (pressDuration >= BUTTON_MAX_PRESS_TIME)
        {
            Led::setWhite(Led::PRIO_URGENT); // Maximum press time exceeded
        }
        else if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME)
        {
            Led::setRed(Led::PRIO_URGENT); // Factory reset - most dangerous
        }
        else if (pressDuration >= BUTTON_LONG_PRESS_TIME)
        {
            Led::setOrange(Led::PRIO_URGENT); // WiFi reset
        }
        else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME)
        {
            Led::setYellow(Led::PRIO_URGENT); // Password reset
        }
        else if (pressDuration >= BUTTON_SHORT_PRESS_TIME)
        {
            Led::setCyan(Led::PRIO_URGENT); // Restart
        }
        else
        {
            Led::setWhite(Led::PRIO_URGENT); // Initial press
        }
    }

    static void _handleRestart()
    {
        LOG_INFO("Restart initiated via button");
        _operationInProgress = true;

        Led::setCyan(Led::PRIO_URGENT);

        setRestartSystem("Restart via button");

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handlePasswordReset()
    {
        LOG_INFO("Password reset to default initiated via button");
        _operationInProgress = true;

        Led::setYellow(Led::PRIO_URGENT);

        if (CustomServer::resetWebPassword()) // Implement actual password reset logic
        {
            // Update authentication middleware with new password
            CustomServer::updateAuthPasswordWithOneFromPreferences();
            
            LOG_INFO("Password reset to default successfully");
            Led::blinkGreenSlow(Led::PRIO_URGENT, 2000ULL);
        }
        else
        {
            LOG_ERROR("Failed to reset password to default");
            Led::blinkRedFast(Led::PRIO_CRITICAL, 2000ULL);
        }

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handleWifiReset()
    {
        LOG_INFO("WiFi reset initiated via button");
        _operationInProgress = true;

        Led::setOrange(Led::PRIO_URGENT);

        CustomWifi::resetWifi(); // This will restart the device

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handleFactoryReset()
    {
        LOG_INFO("Factory reset initiated via button");
        _operationInProgress = true;

        setRestartSystem("Factory reset via button", true);

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    TaskInfo getTaskInfo()
    {
        return getTaskInfoSafely(_buttonTaskHandle, BUTTON_TASK_STACK_SIZE);
    }
}