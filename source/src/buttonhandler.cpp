#include "buttonhandler.h"

static const char *TAG = "buttonhandler";

namespace ButtonHandler
{
    // Static state variables
    static int _buttonPin = INVALID_PIN; // Default pin
    static TaskHandle_t _buttonTaskHandle = NULL;
    static SemaphoreHandle_t _buttonSemaphore = NULL;

    // Button state
    static volatile unsigned long long _buttonPressStartTime = ZERO_START_TIME;
    static volatile bool _buttonPressed = false;
    static ButtonPressType _currentPressType = ButtonPressType::NONE;
    static bool _operationInProgress = false;
    static char _operationName[STATUS_BUFFER_SIZE] = "";
    static unsigned long long _operationTimestamp = ZERO_START_TIME;

    // Getter public methods
    ButtonPressType getCurrentPressType() {return _currentPressType;};
    bool isOperationInProgress() {return _operationInProgress;};
    void getOperationName(char* buffer, size_t bufferSize) {snprintf(buffer, bufferSize, "%s", _operationName);};
    unsigned long long getOperationTimestamp() {return _operationTimestamp;};

    // Private function declarations
    static void _buttonISR();
    static void _buttonTask(void *parameter);
    static void _processButtonPress(unsigned long long pressDuration);
    static void _updateVisualFeedback(unsigned long long pressDuration);

    // Operation handlers
    static void _handleRestart();
    static void _handlePasswordReset();
    static void _handleWifiReset();
    static void _handleFactoryReset();

    void begin(int buttonPin)
    {
        _buttonPin = buttonPin;
        logger.debug("Initializing interrupt-driven button handler on GPIO%d", TAG, _buttonPin);

        // Setup GPIO with pull-up
        pinMode(_buttonPin, INPUT_PULLUP);

        // Create binary semaphore for ISR communication
        _buttonSemaphore = xSemaphoreCreateBinary();
        if (_buttonSemaphore == NULL)
        {
            logger.error("Failed to create button semaphore", TAG);
            return;
        }

        // Create button handling task with higher stack size for safety
        if (xTaskCreate(_buttonTask, BUTTON_TASK_NAME, BUTTON_TASK_STACK_SIZE, NULL, BUTTON_TASK_PRIORITY, &_buttonTaskHandle) != pdPASS)
        {
            logger.error("Failed to create button task", TAG);
            return;
        }

        // Setup interrupt on both edges (press and release)
        attachInterrupt(digitalPinToInterrupt(_buttonPin), _buttonISR, CHANGE);

        logger.debug("Button handler ready - interrupt-driven with task processing", TAG);
    }

    void stop() 
    {
        logger.debug("Stopping button handler", TAG);

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
        TickType_t lastFeedbackUpdate = ZERO_START_TIME;

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
                    unsigned long long pressDuration = millis64() - _buttonPressStartTime;
                    logger.debug("Button released after %llu ms", TAG, pressDuration);

                    _processButtonPress(pressDuration);
                    _buttonPressStartTime = ZERO_START_TIME;

                    // Clear visual feedback
                    Led::clearPattern(Led::PRIO_URGENT);
                }
                else if (_buttonPressed)
                {
                    // Button was pressed - start visual feedback
                    logger.debug("Button pressed", TAG);

                    Led::setBrightness(max(Led::getBrightness(), (unsigned int)1));
                    Led::setWhite(Led::PRIO_URGENT);
                }
            // Here it means it is still being pressed
            } else if (_buttonPressed && _buttonPressStartTime > ZERO_START_TIME) {
                // Timeout occurred while button is pressed - update visual feedback
                unsigned long long pressDuration = millis64() - _buttonPressStartTime;
                _updateVisualFeedback(pressDuration);
            }
        }

        vTaskDelete(NULL);
    }

    static void _processButtonPress(unsigned long long pressDuration)
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
            logger.debug("Button press duration %llu ms - no action", TAG, pressDuration);
        }
    }

    static void _updateVisualFeedback(unsigned long long pressDuration) // FIXME: the previous color does not get cleared since they are the same priority
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
        logger.info("Restart initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "Restart");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        Led::setCyan(Led::PRIO_URGENT);

        setRestartSystem(TAG, "Restart via button");

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handlePasswordReset()
    {
        logger.info("Password reset to default initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "Password Reset");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        Led::setYellow(Led::PRIO_URGENT);

        if (CustomServer::resetWebPassword()) // Implement actual password reset logic
        {
            // Update authentication middleware with new password
            CustomServer::updateAuthPassword();
            
            logger.info("Password reset to default successfully", TAG);
            Led::blinkGreenSlow(Led::PRIO_URGENT, 2000ULL);
        }
        else
        {
            logger.error("Failed to reset password to default", TAG);
            Led::blinkRedFast(Led::PRIO_CRITICAL, 2000ULL);
        }

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handleWifiReset()
    {
        logger.info("WiFi reset initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "WiFi Reset");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        Led::setOrange(Led::PRIO_URGENT);

        CustomWifi::resetWifi(); // This will restart the device

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handleFactoryReset()
    {
        logger.info("Factory reset initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "Factory Reset");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        factoryReset(); // This will also restart the device

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    void clearCurrentOperationName()
    {
        snprintf(_operationName, sizeof(_operationName), "");
        _operationTimestamp = ZERO_START_TIME;
    }
}