---
applyTo: '**'
---
Provide project context and coding guidelines that AI should follow when generating code, answering questions, or reviewing changes.

0. **Agent instructions**:
    - Always be as concise as possible unless the user asks for more details.
    - Don't be condescending or overly verbose.
    - Always stick to the request, and avoid at all costs creating new files unless asked to. Proposals are accepted.
    - When running `pio` or `platformio` commands, use the full path.
    - Never create markdown files or documentation unless explicitly requested.

1. **Project Context**:
    - EnergyMe-Home is an open-source ESP32-based energy monitoring system using the Arduino framework with PlatformIO
    - Monitors up to 17 circuits (1 direct + 16 multiplexed) via ADE7953 energy meter IC
    - Primary interfaces: Web UI, MQTT, InfluxDB, Modbus TCP
    - Uses Preferences for configuration storage and LittleFS for historical data (compressed CSV)
    - Most processing is handled by the ADE7953 IC - ESP32S3 mainly handles communication and data routing

2. **Coding Philosophy**:
    - **Favor simplicity over complexity** - this is not a performance-critical system
    - **Readable code over clever optimizations** - maintainability is key for open-source projects
    - **Arduino/embedded conventions** - use standard Arduino libraries and patterns where possible
    - **Defensive programming** - validate inputs and handle errors gracefully, but don't over-engineer

3. **Core Programming Practices**:
    **Memory Management:**
    - Prefer stack allocation over dynamic allocation (avoid at all costs `String`, use `char[]` buffers)
    - IMPORTANT: use only `snprintf()` whenever possible (and never `sprintf()` or `strncpy()`), also for string concatenation
    - Use named constants for buffer sizes (e.g., `URL_BUFFER_SIZE`, `LINE_PROTOCOL_BUFFER_SIZE`)
    - Use `sizeof(buffer)` instead of hardcoded sizes in function calls
    - Only optimize memory usage in frequently called or critical functions (understand from context)

    **Error Handling:**
    - Never use try-catch blocks - they are not supported in the Arduino framework
    - Log errors and warnings appropriately using the logger
    - Return early on invalid inputs (fail-fast principle)
    - Provide meaningful error messages for debugging
    - Don't crash the system - graceful degradation is preferred

    **Code Organization:**
    - Use existing project patterns and conventions
    - Keep functions focused and reasonably sized
    - Follow existing naming conventions in the codebase
    - Add comments for business logic, not obvious code

4. **Constants and Configuration**:
    - Define all constants in `constants.h` for global values or in respective file headers for module-specific values
    - Use constants for buffer sizes, timeouts, pin assignments, and configuration defaults
    - Group related constants with clear naming prefixes (e.g., `WIFI_`, `MQTT_`, `ADE7953_`)

5. **Naming Conventions and Logging Guidelines**:
    **Naming Conventions:**
    - **Variables**: camelCase (e.g., `wifiConnected`, `energyMeterData`)
    - **Functions**: camelCase (e.g., `getConfiguration()`, `startWifiTask()`)
    - **Constants**: UPPER_SNAKE_CASE (e.g., `WIFI_TIMEOUT_MS`, `BUFFER_SIZE`)
    - **Private functions and variables**: Prefix with underscore (e.g., `_validateConfiguration()`, `_handleWifiEvent()`, `_configMutex`)
    - **Class/Namespace**: PascalCase (e.g., `CustomWifi`, `Ade7953`)

    **Logging Guidelines:**
    - Use the AdvancedLogger library for all logging
    - Log levels: 
    - **LOG_FATAL**: Unrecoverable errors, system crashes, immediate restart or factory reset needed
    - **LOG_ERROR**: System failures, critical errors that affect functionality
    - **LOG_WARN**: Recoverable issues, configuration problems, retry attempts
    - **LOG_INFO**: Important system events (startup, connections, major state changes)
    - **LOG_DEBUG**: Detailed flow information, useful for troubleshooting
    - **LOG_VERBOSE**: Very detailed information (SPI communications, internal state changes)
    - Use descriptive messages with context: `LOG_ERROR("WiFi connection failed after %d attempts", retryCount)`
    - Do not include module or function names in log messages - the logger automatically includes this information

6. **TODO and Code Comments Standards**:
    - Use standardized comment tags for better code tracking and organization:
      ```cpp
      // TODO: Basic improvement or feature (orange)
      // FIXME: Something broken that needs fixing (red)
      // HACK: Temporary workaround that should be cleaned up (red italic)
      ```
    - Keep TODO descriptions concise but descriptive
    - Remove TODO comments when the task is completed

7. **JSON Management**:
    - Always validate JSON structure before accessing fields
    - Use `JsonDocument` everywhere with the SpiRamAllocator like so:
      ```cpp
      SpiRamAllocator allocator;
      JsonDocument doc(&allocator);
      ```
    - Pass streams directly to `deserializeJson()` for efficiency (e.g., `deserializeJson(doc, file)` or `deserializeJson(doc, http.getStream())`)
    - Initialize all configuration fields with sensible defaults
    - Log JSON parsing errors clearly
    - Don't worry about JSON document size - plenty of PSRAM available
    - **ArduinoJson validation**: ONLY use `is<type>()` to validate field types (e.g., `json["field"].is<bool>()`)
    - **Deprecated methods**: Never use `containsKey()` - it's deprecated in ArduinoJson
    - **Null checks**: Only use `isNull()` at JSON document level to ensure non-empty JSON, not for individual fields

8. **Configuration Management Standard Pattern**:
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
      - Unless absolutely necessary, always use getter and setter functions to ensure consistency and avoid race conditions
      - Always release mutex in all code paths (success and error cases)
      - Log timeout errors clearly for debugging

9. **Data storage**:
    - Use Preferences wherever possible for configuration storage
    - Use LittleFS for historical data storage and logs (automatic with AdvancedLogger)

10. **Timestamp and Time Handling**:
    - **Data types**: Always use `uint64_t` for timestamps, millis, and time intervals to avoid rollover issues (`uint32_t` rolls over every 49.7 days and hits 2038 problem; `uint64_t` prevents both)
    - **Storage format**: Always store timestamps as Unix seconds (or milliseconds for time-critical data)
    - **Unix seconds**: Use for general events, configuration, logging, system events
    - **Unix milliseconds**: Use only for time-critical measurements (energy meter readings, precise timing)
    - **Return format**: Always return timestamps in UTC format unless explicitly required to be local
    - **Display format**: Convert to local time only for user interface display (web UI)
    - **API format**: Use ISO 8601 UTC format (`YYYY-MM-DDTHH:MM:SS.sssZ`) for external APIs
    - **Printf formatting**: Use `%llu` for `uint64_t` values in logging and string formatting

11. **FreeRTOS Task Management Principles**:
    - It is mandatory to use mutexes when getting or setting non-atomic variables. Always use getter and setter functions to ensure consistency
    - Use the standard task lifecycle pattern with task notifications for graceful shutdown
    - Always check task handles before operations to prevent race conditions
    - Implement timeout protection when stopping tasks to prevent system hangs
    - Let tasks self-cleanup by setting handle to NULL and calling vTaskDelete(NULL)
    - **For task shutdown notifications**: Use blocking `ulTaskNotifyTake(pdTRUE, timeout)` - it's as CPU-efficient as `vTaskDelay()` but provides immediate shutdown response
    - **Only use non-blocking pattern** if you need sub-second shutdown response or must do other work during delays
    - Have a single (private) function to start and stop tasks, while the public methods should be related to begin and stop

12. **FreeRTOS Task Implementation Patterns**:
    - **PSRAM vs Internal RAM Task Allocation**:
      - **Use PSRAM for tasks that DON'T do flash I/O** (WiFi, LED, Button Handler, Crash Monitor, ADE7953 meter reading)
      - **Use INTERNAL RAM for tasks that DO flash I/O** (NVS/Preferences, LittleFS file operations, OTA operations)
      - **Flash I/O operations include**: NVS/Preferences read/write, LittleFS file operations, OTA firmware writing, SPI flash operations
      - **Why**: When flash cache is disabled during flash operations, PSRAM becomes inaccessible, causing crashes
    - **PSRAM Task Pattern** (for non-flash I/O tasks):
      ```cpp
      TaskHandle_t taskHandle = NULL;
      bool taskShouldRun = false;
      StaticTask_t _taskBuffer;
      StackType_t *_taskStackPointer;
      
      void _startTask() {
          if (taskHandle != NULL) {
              LOG_DEBUG("Task already running");
              return;
          }

          LOG_DEBUG("Starting task with %d bytes stack in PSRAM", TASK_STACK_SIZE);

          _taskStackPointer = (StackType_t *)ps_malloc(TASK_STACK_SIZE);
          if (_taskStackPointer == NULL) {
              LOG_ERROR("Failed to allocate stack for task from PSRAM");
              return;
          }

          taskHandle = xTaskCreateStatic(
              myTask,
              TASK_NAME,
              TASK_STACK_SIZE,
              NULL,
              TASK_PRIORITY,
              _taskStackPointer,
              &_taskBuffer);

          if (!taskHandle) {
              LOG_ERROR("Failed to create task");
              free(_taskStackPointer);
              _taskStackPointer = nullptr;
          }
      }
      
      void stopTask() {
        stopTaskGracefully(&taskHandle, "task name");
        
        // Free PSRAM stack if task was stopped externally
        if (_taskStackPointer != nullptr)
        {
            free(_taskStackPointer);
            _taskStackPointer = nullptr;
        }
      }
      ```
    - **Internal RAM Task Pattern** (for flash I/O tasks):
      ```cpp
      TaskHandle_t taskHandle = NULL;
      bool taskShouldRun = false;
      
      void _startTask() {
          if (taskHandle != NULL) {
              LOG_DEBUG("Task already running");
              return;
          }

          LOG_DEBUG("Starting task with %d bytes stack in internal RAM (uses flash I/O)", TASK_STACK_SIZE);

          BaseType_t result = xTaskCreate(
              myTask,
              TASK_NAME,
              TASK_STACK_SIZE,
              NULL,
              TASK_PRIORITY,
              &taskHandle);

          if (result != pdPASS) {
              LOG_ERROR("Failed to create task");
              taskHandle = NULL;
          }
      }
      
      void stopTask() {
        stopTaskGracefully(&taskHandle, "task name");
      }
      ```

13. **Coding style**:
    - For simple ifs with a single statement, use the one-line format:
      ```cpp
      if (!someBoolean) return;
      ```
    - Use `delay(X)` instead of `vTaskDelay(pdMS_TO_TICKS(X))` since they map to the same underlying FreeRTOS function and `delay()` is more readable in this context.