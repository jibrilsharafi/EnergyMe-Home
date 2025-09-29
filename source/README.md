# EnergyMe - Home | Source Code

**Platform:** ESP32-S3 with Arduino 3.x Framework

*Note: Hardware revision can be found in `include/pins.h`*

## Technical Overview

EnergyMe-Home is a task-based energy monitoring system built on FreeRTOS. The system interfaces an ESP32-S3 microcontroller with an ADE7953 energy measurement IC via SPI, capable of monitoring:

- **17 Total Channels**: 1 direct channel (channel 0) + 16 multiplexed channels
- **Real-time Measurements**: Voltage, current, active/reactive/apparent power, power factor, and energy accumulation
- **Phase Support**: Single-phase measurements per channel. For three-phase systems, assumes 120-degree phase shift between phases with same voltage reference (see `ade7953.cpp` for implementation details)
- **High Accuracy**: Advanced calibration with no-load threshold detection

## Architecture Overview

### Task-Based Design (FreeRTOS)

The firmware is designed around FreeRTOS tasks for stability and performance:

```cpp
void setup() {
    // Initialize all components
    // Start maintenance task
    startMaintenanceTask();
    
    // Main task deletes itself after setup
    vTaskDelete(NULL);
}

void loop() {
    // All work is done in dedicated tasks
    vTaskDelay(portMAX_DELAY);
}
```

**Active Tasks:**
- **Maintenance Task**: System health monitoring, memory management
- **ADE7953 Tasks**: Meter reading, energy saving, CSV logging
- **Network Tasks**: WiFi management, MQTT clients, web server
- **System Tasks**: Crash monitoring, LED control, button handling
- **Storage Tasks**: InfluxDB client, UDP logging

### Advanced Stability Features

#### Crash Monitor System
- **RTC Memory Persistence**: Crash data survives reboots
- **Consecutive Crash/Reset Tracking**: Automatic firmware rollback after 3 consecutive crashes or 10 consecutive resets, otherwise factory reset
- **Core Dump Support**: Complete crash analysis with ESP32 core dump functionality

#### Memory Management
- **PSRAM Optimization**: Used to enlarge queues and buffers
- **Heap Monitoring**: Automatic restart when heap drops below safe levels
- **Stack Analysis**: Per-task stack usage monitoring via TaskInfo

## Core Components

### Energy Measurement (ADE7953)
- **Precision IC**: Analog Devices ADE7953 single-phase energy measurement
- **Dual Channels**: Channel A (direct) and Channel B (multiplexed)
- **Advanced Features**: Line cycle accumulation, no-load detection
- **Task-Based Processing**: Non-blocking measurement loops with FreeRTOS tasks

### Multiplexer Control
- **16-Channel Switching**: 74HC4067PW analog multiplexer
- **Sequential Scanning**: Automatic channel switching for continuous monitoring
- **GPIO Control**: ESP32-S3 manages select lines (S0-S3)

### Web Interface & API
- **Modern UI**: Responsive design with real-time updates
- **RESTful API**: Comprehensive endpoints with Swagger documentation
- **Authentication**: Secure token-based system with HTTP-only cookies
- **Pages Available**:
  - Dashboard (`index.html`)
  - System Information (`info.html`)
  - Configuration Management (`configuration.html`)
  - Channel Setup (`channel.html`) 
  - Calibration Tools (`calibration.html`)
  - ADE7953 Tester (`ade7953-tester.html`)
  - Firmware Updates (`update.html`)
  - System Logs (`log.html`)
  - API Documentation (`swagger.html`)

### Communication Interfaces

#### MQTT Support
- **Dual Clients**: 
  - **MQTT**: AWS IoT Core integration (requires secrets, available with purchased devices)
  - **CustomMqtt**: Local/personal MQTT broker client for custom integrations
- **Secure Publishing**: TLS/SSL support with certificate authentication
- **Quality of Service**: QoS on subscribe operations

#### InfluxDB Integration
- **Native Client**: Direct time-series database integration
- **Version Support**: Both InfluxDB v1.x and v2.x
- **Buffering**: Optimized batch writes with automatic retry
- **SSL/TLS**: Secure connections with certificate validation

#### Modbus TCP Server
- **Industrial Protocol**: Standard Modbus TCP implementation
- **Register Mapping**: Comprehensive register layout for all measurements
- **Real-time Data**: Direct access to current readings and accumulated energy
- **SCADA Integration**: Compatible with industrial monitoring systems

## Code Structure and Design

### Directory Layout

```
source/
├── src/                    # Implementation files
│   ├── main.cpp           # Main application entry point
│   ├── ade7953.cpp        # Energy measurement IC driver
│   ├── crashmonitor.cpp   # Crash detection and recovery
│   ├── customserver.cpp   # Web server and API
│   ├── custommqtt.cpp     # MQTT client implementations
│   ├── influxdbclient.cpp # Time-series database client
│   ├── utils.cpp          # System utilities and helpers
│   └── ...                # Other component implementations
├── include/               # Header files
│   ├── constants.h        # System constants and configuration
│   ├── structs.h          # Data structures and types
│   ├── globals.h          # Global variable declarations
│   └── ...                # Component headers
├── html/                  # Web interface pages
├── css/                   # Stylesheets
├── js/                    # JavaScript client code
├── resources/             # Static assets (favicon, swagger spec)
└── utils/                 # Development tools and scripts
```

### Key Software Modules

#### System Core
- **`CrashMonitor`**: Advanced crash detection with RTC memory persistence, automatic recovery
- **`Led`**: Multi-priority LED status indication with FreeRTOS task management
- **`ButtonHandler`**: Hardware button interface with task-based event processing (password reset, WiFi recovery, etc.)
- **`CustomTime`**: Network time synchronization with timezone management

#### Energy Measurement
- **`Ade7953`**: Complete ADE7953 IC driver with task-based meter reading, energy accumulation, and CSV logging
- **`Multiplexer`**: 16-channel analog multiplexer control for sequential circuit monitoring

#### Networking & Communication  
- **`CustomWifi`**: WiFi management with captive portal and connection monitoring task
- **`CustomServer`**: Async web server with authentication, API endpoints, and OTA updates
- **`Mqtt`** / **`CustomMqtt`**: Dual MQTT clients (AWS IoT + local broker) with secure publishing
- **`InfluxDbClient`**: Native InfluxDB integration with batching and retry logic
- **`ModbusTcp`**: Industrial Modbus TCP server with comprehensive register mapping

#### Storage & Configuration
- **Preferences API**: Secure credential storage across multiple namespaces
- **LittleFS**: Configuration files, energy data, and system logs
- **Task-Safe Access**: Mutex-protected configuration operations

### Design Principles

- **Task-Based Architecture**: All operations run in dedicated FreeRTOS tasks for better resource management
- **Crash Resilience**: Comprehensive error handling with automatic recovery and rollback capabilities  
- **Memory Optimization**: PSRAM utilization, stack monitoring, and efficient buffer management
- **Security First**: Token-based authentication, encrypted communications, secure credential storage
- **Real-time Performance**: Non-blocking operations with configurable update intervals

## Configuration & Data Management

### Configuration Storage
Settings are stored using ESP32 Preferences API across organized namespaces:

- **`general_ns`**: System-wide settings, device configuration
- **`ade7953_ns`**: Energy measurement IC parameters  
- **`calibration_ns`**: Measurement calibration values
- **`channels_ns`**: Per-channel configuration and labels
- **`mqtt_ns`** / **`custom_mqtt_ns`**: MQTT broker settings
- **`influxdb_ns`**: Time-series database configuration
- **`auth_ns`**: Authentication credentials and tokens
- **`wifi_ns`**: Network configuration
- **`crashmonitor_ns`**: Crash recovery settings

### Data Files (LittleFS)
- **`/log.txt`**: System logs with rotation (max 1000 lines) via AdvancedLogger library
- **Energy Data**: Accumulation files for active/reactive/apparent energy
- **CSV Exports**: Hourly energy data zipped daily to save memory, allowing up to 10 years of hourly data storage

### Task Information Monitoring
All tasks provide comprehensive monitoring via `TaskInfo` structures:

```cpp
struct TaskInfo {
    uint32_t allocatedStack;      // Total stack size
    uint32_t minimumFreeStack;    // Minimum free stack observed
    float freePercentage;         // Stack utilization metrics
    float usedPercentage;
};
```

**Monitored Tasks:**
- MQTT clients (AWS IoT, Custom MQTT)
- Web server (health check, OTA timeout)
- ADE7953 operations (meter reading, energy saving, CSV logging)
- System services (crash monitor, LED control, maintenance)
- Network services (WiFi management, UDP logging, InfluxDB)

## System Specifications

### Measurement Capabilities
- **17 Monitored Circuits**: 1 direct + 16 multiplexed channels
- **Electrical Parameters**: 
  - RMS Voltage and Current
  - Active Power (W), Reactive Power (VAR), Apparent Power (VA)
  - Power Factor
  - Energy accumulation (Wh, VARh, VAh)
- **Sampling Rate**: Channel 0 sampled every 200ms, others depend on number of active channels, minimum every 400ms (measurements are discarded every other cycle)
- **Accuracy**: Depends on CT quality and calibration (typically ±1% with proper setup)
- **Voltage Range**: Universal AC input (90-265V RMS)
- **Current Range**: Configurable via CT selection (max CT output: 333mV)

### Hardware Interface
- **Microcontroller**: ESP32-S3 (dual-core, 16MB Flash, 2MB PSRAM)
- **Communication**: SPI to ADE7953, I2C available
- **Memory**: 2MB PSRAM utilized for buffer optimization
- **Storage**: 16MB Flash with custom partition table
- **Power**: <1W consumption via onboard AC/DC converter

### Performance Characteristics
- **Memory Usage**: PSRAM utilization for buffer optimization, heap monitoring
- **Task Scheduling**: FreeRTOS with priority-based scheduling
- **Update Rates**:
  - Web Interface: 1 second updates
  - MQTT Publishing: Configurable (5-60 seconds typical)
  - Modbus TCP: Real-time on demand
  - InfluxDB: Batched writes with configurable intervals

## Communication Interfaces

### RESTful API
Comprehensive REST API with Swagger documentation (`/swagger.html`) for:
- Authentication and session management
- System management (device info, firmware updates, crash data)
- Energy data access (real-time readings, historical data, CSV export)
- Configuration management (system settings, calibration, network)

### MQTT Integration
- **MQTT**: AWS IoT Core integration (requires secrets)
- **CustomMqtt**: Local MQTT broker client with configurable authentication
- **SSL/TLS**: Encrypted connections with certificate support

### InfluxDB Time-Series Database
- **Version Support**: Both InfluxDB v1.x and v2.x
- **Features**: Line protocol formatting, batch writes, SSL/TLS, retry logic
- **Configuration**: Web interface with real-time status monitoring

### Modbus TCP Server
- **Industrial Standard**: FC03/FC04 function codes
- **Register Mapping**: System info, per-channel data, aggregated measurements
- **Real-time Access**: Direct measurement data for SCADA systems

## Security & Authentication

### Authentication System
- **Token-Based Authentication**: Session management with automatic expiration
- **Password Protection**: BCrypt hashing with secure credential storage
- **Protected Operations**: Critical system functions require authentication
- **Session Security**: HTTP-only cookies and automatic session renewal

### Default Credentials
- **Username**: `admin`
- **Password**: `energyme`

⚠️ **Security Notice**: Change default credentials immediately after first login

## AWS IoT Integration (Optional)

AWS IoT Core integration requires additional secret files in the `secrets/` directory:
- `ca.pem`, `certclaim.pem`, `privateclaim.pem` - X.509 certificates
- `endpoint.txt`, `rulemeter.txt` - AWS IoT configuration
- `encryptionkey.txt` - Local data encryption key

**Note**: These files are only required for AWS IoT Core integration. The system operates fully without them using CustomMqtt for local MQTT brokers.

## Development & Build

### Platform Configuration
- **Framework**: Arduino 3.x on ESP32-S3 with FreeRTOS
- **Build System**: PlatformIO with custom platform fork
- **Memory**: 16MB Flash, 2MB PSRAM with optimized partition table

### Key Dependencies
- AdvancedLogger, ArduinoJson, StreamUtils
- ESPAsyncWebServer, PubSubClient, WiFiManager
- eModbus for Modbus TCP functionality

### Build Features
- Static analysis (cppcheck, clang-tidy)
- Debug symbols and ESP32 core debugging
- PSRAM optimization for memory-intensive operations

## System Monitoring & Diagnostics

### Crash Analysis
- **Core Dumps**: ESP32 core dump functionality for complete crash analysis
- **RTC Memory**: Persistent crash data across reboots  
- **Recovery Actions**: Automatic firmware rollback or factory reset

### System Health
- **Task Monitoring**: Real-time stack usage for all FreeRTOS tasks
- **Memory Tracking**: Heap, PSRAM, and NVS usage monitoring
- **Network Status**: WiFi diagnostics and connection quality

### Debug Access
- **Serial Monitor**: 115200 baud with exception decoder
- **UDP Logging**: Network-based log streaming
- **Web Interface**: Live system status and logs
- **REST API**: Programmatic access to diagnostics

