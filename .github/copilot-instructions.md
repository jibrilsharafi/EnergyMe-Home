# GitHub Copilot Instructions for EnergyMe-Home

## Project Overview

EnergyMe-Home is an open-source energy monitoring system for home use, capable of monitoring up to 17 circuits. It's an embedded firmware project built for ESP32-S3 microcontrollers with the following key characteristics:

- **Hardware**: ESP32-S3-N16R2 with 16MB Flash and 2MB PSRAM
- **Framework**: Arduino 3.x framework with PlatformIO build system
- **Architecture**: FreeRTOS task-based architecture
- **Energy IC**: ADE7953 single-phase energy measurement IC
- **Integration**: REST API, MQTT, Modbus TCP, and InfluxDB support

## Project Structure

- `/source` - Main firmware source code (C++ with Arduino framework)
  - `src/` - Core implementation files
  - `include/` - Header files
  - `html/`, `css/`, `js/` - Web interface files (embedded in firmware)
  - `platformio.ini` - Build configuration
- `/documentation` - Hardware schematics, component datasheets
- `/examples` - Example configurations and integrations
- `/install_docs` - Installation and setup guides

## Build System & Environments

This project uses **PlatformIO** with four build environments:

1. **esp32s3-dev** - Development with secrets embedded (for internal testing)
2. **esp32s3-dev-nosecrets** - Development without secrets (for open-source development)
3. **esp32s3-prod** - Production build with secrets
4. **esp32s3-prod-nosecrets** - Production build for public releases

### Building the Project

```bash
# Install PlatformIO if not already installed
pip install platformio

# Build for open-source development (no secrets required)
cd source
pio run -e esp32s3-dev-nosecrets

# Clean build
pio run -t clean -e esp32s3-dev-nosecrets

# Static analysis
pio check -e esp32s3-dev-nosecrets
```

### Important Build Notes

- Always use the `-nosecrets` environments for open-source contributions
- The project requires specific ESP32-S3 configurations (PSRAM, flash mode, partition table)
- Web interface files (HTML/CSS/JS) are embedded at compile time using `board_build.embed_txtfiles`
- Do NOT modify `platformio.ini` unless absolutely necessary - the ESP32-S3-N16R2 configuration is fragile

## Coding Standards & Conventions

### General Guidelines

- **Language**: C++ (Arduino framework)
- **Style**: Follow existing code style in the repository
- **Comments**: Use clear, concise comments; avoid stating the obvious
- **Warnings**: The project enables strict compiler warnings (`-Wall -Wextra -Wformat -Wformat-security -Wconversion`)
- **Memory**: Be mindful of heap usage; use PSRAM for large buffers via `ps_malloc()`

### Embedded Systems Best Practices

1. **Memory Management**
   - ESP32-S3 has limited heap memory; avoid large stack allocations
   - Use PSRAM for queues, buffers, and large data structures
   - Always check return values from memory allocation functions
   - Be aware of stack sizes for FreeRTOS tasks

2. **Task-Based Architecture**
   - All work is done in FreeRTOS tasks, not in `loop()`
   - Use appropriate task priorities and stack sizes
   - Use proper synchronization primitives (mutexes, semaphores)
   - Avoid blocking operations in critical tasks

3. **Hardware Interfacing**
   - SPI is used for ADE7953 communication
   - GPIO pins are defined in `include/pins.h`
   - Always check hardware availability before accessing peripherals
   - Use proper error handling for hardware failures

4. **Networking**
   - WiFi management uses WiFiManager library with captive portal
   - Async web server (ESPAsyncWebServer) for non-blocking HTTP operations
   - MQTT uses PubSubClient library
   - Always handle connection failures gracefully

5. **Crash Recovery**
   - The system has sophisticated crash monitoring and recovery
   - Safe mode activates after rapid restart loops
   - Firmware rollback available for bad updates
   - Do not interfere with crash recovery mechanisms

### Code Quality

- Run static analysis before committing: `pio check`
- Fix all warnings in code you modify
- Test changes on actual hardware when possible (or at minimum, ensure compilation succeeds)
- For web interface changes, test in a browser after building and flashing

## Dependencies

All dependencies are pinned to specific versions in `platformio.ini`:

- **AdvancedLogger** (2.0.1) - Logging with PSRAM support
- **ArduinoJson** (7.4.2) - JSON processing
- **AsyncTCP** (3.4.8) & **ESPAsyncWebServer** (3.8.1) - Async web server
- **PubSubClient** (2.8.0) - MQTT client
- **WiFiManager** (2.0.17) - WiFi configuration
- **eModbus** (1.7.4) - Modbus TCP support
- **ESP32-targz** (1.2.9) - Archive extraction

**Do not update dependency versions** without thorough testing - embedded systems require stability.

## Testing

This project targets real hardware (ESP32-S3). Testing involves:

1. **Compilation**: Ensure code compiles without errors or warnings
2. **Static Analysis**: Run `pio check` for cppcheck and clangtidy
3. **Hardware Testing**: Flash to ESP32-S3 and verify functionality
4. **Integration Testing**: Test API endpoints, MQTT, web interface

There is no unit test framework currently set up. Focus on ensuring compilation succeeds and static analysis passes.

## Security Considerations

1. **Secrets Management**
   - Never commit secrets (certificates, keys, endpoints) to the repository
   - Use the `-nosecrets` build environments for open-source work
   - Secrets are provisioned at runtime in production builds without embedded secrets

2. **Web Security**
   - The web interface uses token-based authentication
   - Default credentials should be changed by users
   - Be mindful of CORS and XSS when modifying web interface

3. **Memory Safety**
   - Buffer overflows are critical in embedded systems
   - Always validate input sizes and array bounds
   - Use safe string functions (strncpy, snprintf, etc.)

4. **Network Security**
   - Support TLS/SSL for MQTT and InfluxDB connections
   - Validate certificates when appropriate
   - Handle authentication failures gracefully

## Common Tasks

### Modifying the Web Interface

1. Edit files in `source/html/`, `source/css/`, or `source/js/`
2. These files are embedded at compile time via `board_build.embed_txtfiles` in `platformio.ini`
3. Rebuild the firmware to include changes
4. Test in a browser after flashing

### Adding New API Endpoints

1. Modify `source/src/customserver.cpp` (or relevant file)
2. Follow the existing async endpoint pattern using ESPAsyncWebServer
3. Update the Swagger documentation in `source/resources/swagger.yaml`
4. Test with curl or Postman

### Working with Energy Measurements

1. ADE7953 driver is in `source/include/ade7953.h` and `source/src/ade7953.cpp`
2. Multiplexer control is in `source/include/multiplexer.h`
3. Energy data flows through tasks - review the task-based architecture
4. CSV logging happens in the ADE7953 task

## What to Avoid

1. **Do NOT** modify critical hardware configurations (PSRAM, flash mode, partition table) without deep knowledge
2. **Do NOT** add unnecessary dependencies - embedded systems must stay lean
3. **Do NOT** block FreeRTOS tasks with long-running operations
4. **Do NOT** introduce memory leaks - resources are limited
5. **Do NOT** remove crash recovery or safety mechanisms
6. **Do NOT** commit secrets, credentials, or device-specific configurations
7. **Do NOT** change the existing task architecture without thorough understanding

## Resources

- Main README: `/README.md`
- Source code documentation: `/source/README.md`
- Hardware documentation: `/documentation/README.md`
- Contributing guidelines: `/CONTRIBUTING.md`
- Swagger API documentation: `/source/resources/swagger.yaml`
- PlatformIO configuration: `/source/platformio.ini`

## When in Doubt

- Review existing code for patterns and conventions
- Check PlatformIO build output for warnings
- Run static analysis: `pio check`
- Ask for clarification if hardware-specific changes are needed
- Prefer minimal, targeted changes over large refactors
- Test compilation in the `-nosecrets` environment before submitting changes
