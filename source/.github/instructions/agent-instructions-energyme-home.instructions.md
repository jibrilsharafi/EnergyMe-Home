---
applyTo: '**'
---
Provide project context and coding guidelines that AI should follow when generating code, answering questions, or reviewing changes.

0. **Agent instructions**:
    - Always be as concise as possible unless the user asks for more details.
    - Don't be condescending or overly verbose.
    - Always stick to the request, and avoid at all costs creating new files unless asked to. Proposals are accepted.
    - Never ask to run `pio` or `platformio` commands as they won't work directly in the terminal. Assume the user will run them manually.
    - Never create markdown files or documentation unless explicitly requested.

1. **Project Context**:
    - EnergyMe-Home is an open-source ESP32-based energy monitoring system using the Arduino framework with PlatformIO
    - Monitors up to 17 circuits (1 direct + 16 multiplexed) via ADE7953 energy meter IC
    - Primary interfaces: Web UI, MQTT, InfluxDB, Modbus TCP
    - Uses LittleFS for configuration storage (JSON files)
    - Most processing is handled by the ADE7953 IC - ESP32 mainly handles communication and data routing

2. **Coding Philosophy**:
    - **Favor simplicity over complexity** - this is not a performance-critical system
    - **Readable code over clever optimizations** - maintainability is key for open-source projects
    - **Arduino/embedded conventions** - use standard Arduino libraries and patterns where possible
    - **Defensive programming** - validate inputs and handle errors gracefully, but don't over-engineer

3. **Memory Management**:
    - Prefer stack allocation over dynamic allocation (avoid `String`, use `char[]` buffers)
    - IMPORTANT: use only `snprintf()` whenever possible (and never `sprintf()` or `strncpy()`), also for string concatenation
    - Use named constants for buffer sizes (e.g., `URL_BUFFER_SIZE`, `LINE_PROTOCOL_BUFFER_SIZE`)
    - Use `sizeof(buffer)` instead of hardcoded sizes in function calls
    - Define buffer sizes as constants (in `constants.h`) for consistency
    - Only optimize memory usage in frequently called or critical functions (understand from context)

4. **Error Handling**:
    - Never use try-catch blocks - they are not supported in the Arduino framework
    - Log errors and warnings appropriately using the logger
    - Return early on invalid inputs (fail-fast principle)
    - Provide meaningful error messages for debugging
    - Don't crash the system - graceful degradation is preferred

5. **Code Organization**:
    - Use existing project patterns and conventions
    - Keep functions focused and reasonably sized
    - Use constants from `constants.h` for configuration values
    - Follow existing naming conventions in the codebase
    - Add comments for business logic, not obvious code

6. **JSON and Configuration Management**:
    - Always validate JSON structure before accessing fields
    - Use `JsonDocument` everywhere - automatically uses 2MB PSRAM on ESP32-S3
    - Pass streams directly to `deserializeJson()` for efficiency (e.g., `deserializeJson(doc, file)` or `deserializeJson(doc, http.getStream())`)
    - Initialize all configuration fields with sensible defaults
    - Log JSON parsing errors clearly
    - Don't worry about JSON document size - plenty of PSRAM available
    - **ArduinoJson validation**: ONLY use `is<type>()` to validate field types (e.g., `json["field"].is<bool>()`)
    - **Deprecated methods**: Never use `containsKey()` - it's deprecated in ArduinoJson
    - **Null checks**: Only use `isNull()` at JSON document level to ensure non-empty JSON, not for individual fields

    **Configuration Management Standard Pattern**:
    - **Configuration struct**: Always include constructor with default values using constants from `constants.h`
    - **Public API**: Standard functions for all configuration modules:
      ```cpp
      void getConfiguration(ConfigStruct &config);                   // Get current configuration
      bool setConfiguration(ConfigStruct &config);                    // Set configuration
      bool configurationToJson(ConfigStruct &config, JsonDocument &jsonDocument);     // Struct to JSON
      bool configurationFromJson(JsonDocument &jsonDocument, ConfigStruct &config, bool partial = false);   // Full (or partial) JSON to struct
      ```
    - **Private validation functions**: Use consistent naming and validation approach:
      ```cpp
      static bool _validateJsonConfiguration(JsonDocument &jsonDocument, bool partial = false); // Single validation function
      ```
    - **Validation logic**: 
      - When `partial = false`: Validate ALL required fields are present and correct type
      - When `partial = true`: Validate at least ONE field is present and valid type, return `true` immediately when first valid field is found
      - Use `JsonDocument &jsonDocument` parameter style consistently
    - **JSON processing**: 
      - `configurationFromJson()` uses full (or partial) validation (`_validateJsonConfiguration(doc, false)`)
    - **Error handling**: Log specific field validation errors in validation functions, not in calling functions
    - **Thread Safety**: Configuration modules with tasks must use semaphores for thread-safe access:
      ```cpp
      static SemaphoreHandle_t _configMutex = nullptr;  // Created in begin()
      #define CONFIG_MUTEX_TIMEOUT_MS 5000               // Timeout constant in constants.h
      ```
      - `setConfiguration()` must acquire mutex with timeout before modifying shared state
      - `getConfiguration()` should also use mutex for consistency (though less critical)
      - Always release mutex in all code paths (success and error cases)
      - Log timeout errors clearly for debugging

7. **Data storage**:
    - Use Preferences wherever possible for configuration storage
    - Use LittleFS (to update in the future to LittleFS) for historical data storage

8. **Timestamp and Time Handling**:
    - **Data types**: Always use `uint64_t` for timestamps, millis, and time intervals to avoid rollover issues
    - **Rollover prevention**: `uint32_t` rolls over every 49.7 days and hits 2038 problem; `uint64_t` prevents both
    - **Storage format**: Always store timestamps as Unix seconds (or milliseconds for time-critical data)
    - **Unix seconds**: Use for general events, configuration, logging, system events
    - **Unix milliseconds**: Use only for time-critical measurements (energy meter readings, precise timing)
    - **Return format**: Always return timestamps in UTC format unless explicitly required to be local
    - **Display format**: Convert to local time only for user interface display
    - **API format**: Use ISO 8601 UTC format (`YYYY-MM-DDTHH:MM:SS.sssZ`) for external APIs
    - **Storage efficiency**: Numeric timestamps (8 bytes) vs formatted strings (20-25 bytes)
    - **Time arithmetic**: Store as numbers to enable easy duration calculations and sorting
    - **Printf formatting**: Use `%llu` for `uint64_t` values in logging and string formatting

9. **FreeRTOS Task Management**:
    - Use the standard task lifecycle pattern with task notifications for graceful shutdown
    - Always check task handles before operations to prevent race conditions
    - Implement timeout protection when stopping tasks to prevent system hangs
    - Let tasks self-cleanup by setting handle to NULL and calling vTaskDelete(NULL)
    - **For task shutdown notifications**: Use blocking `ulTaskNotifyTake(pdTRUE, timeout)` - it's as CPU-efficient as `vTaskDelay()` but provides immediate shutdown response
    - **Only use non-blocking pattern** if you need sub-second shutdown response or must do other work during delays
    - Have a single (private) function to start and stop tasks, while the public methods should be related to begin and stop
    - Standard pattern:
      ```cpp
      TaskHandle_t taskHandle = NULL;
      bool taskShouldRun = false;
      
      void myTask(void* parameter) {
          LOG_DEBUG("Task X started");

          taskShouldRun = true;
          while (taskShouldRun) {
              // Task work here
              
              // Wait for stop notification with timeout (blocking) - zero CPU usage while waiting
              uint32_t notificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TASK_INTERVAL_MS));
              if (notificationValue > 0) {
                  taskShouldRun = false;
                  break;
              }
          }

          LOG_DEBUG("Task X stopping");
          taskHandle = NULL;
          vTaskDelete(NULL);
      }
      
      void startTask() {
          if (taskHandle == NULL) {
              LOG_DEBUG("Starting task X");
              xTaskCreate(myTask, X_TASK_NAME, X_TASK_STACK_SIZE, NULL, X_TASK_PRIORITY, &taskHandle);
          } else {
              LOG_DEBUG("Task X is already running");
          }
      }
      
      void stopTask() {
        // Anything else needed
        stopTaskGracefully(&_XTaskHandle, "X task"); // Public function in utils.h
        // Anything else needed
      }
      ```
      ```

10. **Coding style**:
    - For simple ifs with a single statement, use the one-line format:
      ```cpp
      if (!someBoolean) return;
      ```
    - Use `delay(X)` instead of `vTaskDelay(pdMS_TO_TICKS(X))` since they map to the same underlying FreeRTOS function and `delay()` is more readable in this context.