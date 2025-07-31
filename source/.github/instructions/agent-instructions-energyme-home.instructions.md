---
applyTo: '**'
---
Provide project context and coding guidelines that AI should follow when generating code, answering questions, or reviewing changes.

0. **Agent instructions**:
    - Always be as concise as possible unless the user asks for more details.
    - Don't be condescending or overly verbose.
    - Always stick to the request, and avoid at all costs creating new files unless asked to. Proposals are accepted.
    - Never ask to run `pio` or `platformio` commands as they won't work directly in the terminal. Assume the user will run them manually.

1. **Project Context**:
    - EnergyMe-Home is an open-source ESP32-based energy monitoring system using the Arduino framework with PlatformIO
    - Monitors up to 17 circuits (1 direct + 16 multiplexed) via ADE7953 energy meter IC
    - Primary interfaces: Web UI, MQTT, InfluxDB, Modbus TCP
    - Uses SPIFFS for configuration storage (JSON files)
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

6. **JSON and Configuration**:
    - Always validate JSON structure before accessing fields
    - Use `JsonDocument` everywhere - automatically uses 2MB PSRAM on ESP32-S3
    - Pass streams directly to `deserializeJson()` for efficiency (e.g., `deserializeJson(doc, file)` or `deserializeJson(doc, http.getStream())`)
    - Initialize all configuration fields with sensible defaults
    - Log JSON parsing errors clearly
    - Don't worry about JSON document size - plenty of PSRAM available

7. **Data storage**:
    - Use Preferences wherever possible for configuration storage
    - Use SPIFFS (to update in the future to LittleFS) for historical data storage

8. **FreeRTOS Task Management**:
    - Use the standard task lifecycle pattern with task notifications for graceful shutdown
    - Always check task handles before operations to prevent race conditions
    - Implement timeout protection when stopping tasks to prevent system hangs
    - Let tasks self-cleanup by setting handle to NULL and calling vTaskDelete(NULL)
    - Use non-blocking task notification checks (ulTaskNotifyTake with timeout 0) in task loops
    - Standard pattern:
      ```cpp
      TaskHandle_t taskHandle = NULL;
      bool taskShouldRun = false;
      
      void myTask(void* parameter) {
          taskShouldRun = true;
          while (taskShouldRun) {
              // Task work here
              
              // Check for stop notification (non-blocking)
              uint32_t notificationValue = ulTaskNotifyTake(pdFALSE, 0);
              if (notificationValue > 0) {
                  taskShouldRun = false;
                  break;
              }
              vTaskDelay(pdMS_TO_TICKS(100)); // Adjust delay as needed
          }
          taskHandle = NULL;
          vTaskDelete(NULL);
      }
      
      void startTask() {
          if (taskHandle == NULL) {
              xTaskCreate(myTask, "TaskName", 4096, NULL, 1, &taskHandle);
          }
      }
      
      void stopTask() {
          if (taskHandle != NULL) {
              xTaskNotifyGive(taskHandle);
              // Wait with timeout for clean shutdown
              int timeout = 1000;
              while (taskHandle != NULL && timeout > 0) {
                  vTaskDelay(pdMS_TO_TICKS(10));
                  timeout -= 10;
              }
              // Force cleanup if needed
              if (taskHandle != NULL) {
                  vTaskDelete(taskHandle);
                  taskHandle = NULL;
              }
          }
      }
      ```