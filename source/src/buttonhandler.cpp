#include "buttonhandler.h"

static const char *TAG = "buttonhandler";

namespace ButtonHandler
{
    // Static state variables
    static int _buttonPin = INVALID_PIN; // Default pin
    static Preferences _preferences;
    static TaskHandle_t _buttonTaskHandle = NULL;
    static SemaphoreHandle_t _buttonSemaphore = NULL;

    // Button state
    static volatile unsigned long _buttonPressStartTime = ZERO_START_TIME;
    static volatile bool _buttonPressed = false;
    static ButtonPressType _currentPressType = ButtonPressType::NONE;
    static bool _operationInProgress = false;
    static char _operationName[BUTTON_HANDLER_OPERATION_BUFFER_SIZE] = "";
    static unsigned long _operationTimestamp = 0;

    // Getter public methods
    ButtonPressType getCurrentPressType() {return _currentPressType;};
    bool isOperationInProgress() {return _operationInProgress;};
    void getCurrentOperationName(char* buffer, size_t bufferSize) {snprintf(buffer, bufferSize, "%s", _operationName);};
    unsigned long getCurrentOperationTimestamp() {return _operationTimestamp;};

    // Private function declarations
    static void _buttonISR();
    static void _buttonTask(void *parameter);
    static void _processButtonPress(unsigned long pressDuration);
    static void _updateVisualFeedback(unsigned long pressDuration);

    // Operation handlers
    static void _handleRestart();
    static void _handlePasswordReset();
    static void _handleWifiReset();
    static void _handleFactoryReset();

    // NVS persistence
    static void _loadOperationData();
    static void _saveOperationData();

    void begin(
        int buttonPin)
    {
        _buttonPin = buttonPin;
        logger.info("Initializing interrupt-driven button handler on GPIO%d", TAG, _buttonPin);

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

        // Initialize NVS and load last operation
        _preferences.begin(PREFERENCES_NAMESPACE_BUTTON, false);
        _loadOperationData();

        logger.info("Button handler ready - interrupt-driven with task processing", TAG);
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
            _buttonPressStartTime = millis();

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
        TickType_t lastFeedbackUpdate = 0;

        while (true)
        {
            // Wait for button event or timeout for visual feedback updates
            if (xSemaphoreTake(_buttonSemaphore, feedbackUpdateInterval))
            {
                // Button event occurred
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_TIME)); // Simple debounce

                if (!_buttonPressed && _buttonPressStartTime > ZERO_START_TIME)
                {
                    // Button was released - process the press
                    unsigned long pressDuration = millis() - _buttonPressStartTime;
                    logger.debug("Button released after %lu ms", TAG, pressDuration);

                    _processButtonPress(pressDuration);
                    _buttonPressStartTime = ZERO_START_TIME;

                    // Clear visual feedback
                    Led::unblock();
                }
                else if (_buttonPressed)
                {
                    // Button was pressed - start visual feedback
                    logger.debug("Button pressed", TAG);

                    Led::block();
                    Led::setBrightness(max(Led::getBrightness(), 1));
                    Led::setWhite(true);
                }
            }
            // Here it means it is still being pressed
            else if (_buttonPressed && _buttonPressStartTime > ZERO_START_TIME)
            {
                // Timeout occurred while button is pressed - update visual feedback
                unsigned long pressDuration = millis() - _buttonPressStartTime;
                _updateVisualFeedback(pressDuration);
            }
        }

        vTaskDelete(NULL);
    }

    static void _processButtonPress(unsigned long pressDuration)
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
            logger.debug("Button press duration %lu ms - no action", TAG, pressDuration);
        }
    }

    static void _updateVisualFeedback(unsigned long pressDuration)
    {
        // Provide immediate visual feedback based on current press duration
        if (pressDuration >= BUTTON_MAX_PRESS_TIME)
        {
            Led::setWhite(true); // Maximum press time exceeded
        }
        else if (pressDuration >= BUTTON_VERY_LONG_PRESS_TIME)
        {
            Led::setRed(true); // Factory reset - most dangerous
        }
        else if (pressDuration >= BUTTON_LONG_PRESS_TIME)
        {
            Led::setOrange(true); // WiFi reset
        }
        else if (pressDuration >= BUTTON_MEDIUM_PRESS_TIME)
        {
            Led::setYellow(true); // Password reset
        }
        else if (pressDuration >= BUTTON_SHORT_PRESS_TIME)
        {
            Led::setCyan(true); // Restart
        }
        else
        {
            Led::setWhite(true); // Initial press
        }
    }

    static void _handleRestart()
    {
        logger.info("Restart initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "Restart");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        _saveOperationData();

        Led::block();
        Led::setCyan(true);

        setRestartEsp32(TAG, "Restart via button");

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handlePasswordReset()
    {
        logger.info("Password reset to default initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "Password Reset");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        _saveOperationData();

        Led::block();
        Led::setYellow(true);

        if (true) // TODO: Implement actual password reset logic
        {
            logger.info("Password reset to default successfully", TAG);

            // Success feedback - 3 green blinks
            for (int i = 0; i < 3; i++)
            {
                Led::setGreen(true);
                vTaskDelay(pdMS_TO_TICKS(500));
                Led::setOff(true);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        else
        {
            logger.error("Failed to reset password to default", TAG);

            // Error feedback - 3 red blinks
            for (int i = 0; i < 3; i++)
            {
                Led::setRed(true);
                vTaskDelay(pdMS_TO_TICKS(500));
                Led::setOff(true);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }

        Led::setOff(true);
        Led::unblock();

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    static void _handleWifiReset()
    {
        logger.info("WiFi reset initiated via button", TAG);
        snprintf(_operationName, sizeof(_operationName), "WiFi Reset");
        _operationTimestamp = CustomTime::getUnixTime();
        _operationInProgress = true;

        _saveOperationData();

        Led::block();
        Led::setOrange(true);

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

        _saveOperationData();

        Led::block();
        Led::setRed(true);

        factoryReset(); // This will restart the device

        _operationInProgress = false;
        _currentPressType = ButtonPressType::NONE;
    }

    void clearCurrentOperationName()
    {
        snprintf(_operationName, sizeof(_operationName), "");
        _operationTimestamp = ZERO_START_TIME;
        _saveOperationData();
    }

    // NVS persistence functions
    static void _saveOperationData()
    {
        _preferences.putString(PREFERENCES_LAST_OPERATION_KEY, _operationName);
        _preferences.putULong(PREFERENCES_LAST_OPERATION_TIMESTAMP_KEY, _operationTimestamp);
        logger.debug("Saved operation data to NVS: %s at %lu", TAG, _operationName, _operationTimestamp);
    }

    static void _loadOperationData()
    {
        _preferences.getString(PREFERENCES_LAST_OPERATION_KEY, _operationName, sizeof(_operationName));
        _operationTimestamp = _preferences.getULong(PREFERENCES_LAST_OPERATION_TIMESTAMP_KEY, 0);

        if (strlen(_operationName) > 0)
        {
            logger.info("Loaded last button operation from NVS: %s", TAG, _operationName);
            
            if (_operationTimestamp > ZERO_START_TIME)
            {
                char timestampBuffer[TIMESTAMP_BUFFER_SIZE];
                CustomTime::timestampFromUnix(_operationTimestamp, timestampBuffer, sizeof(timestampBuffer));
                logger.info("Operation timestamp: %s", TAG, timestampBuffer);
            }
        }
        else
        {
            logger.debug("No previous button operation found in NVS", TAG);
        }
    }

}