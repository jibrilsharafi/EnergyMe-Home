# EnergyMe - Home | Source Code

**Firmware Version:** v0.12.37  
**Hardware Revision:** v5  
**Platform:** ESP32-S3 with Arduino 3.x Framework

## Technical Overview

EnergyMe-Home is a modern, task-based energy monitoring system built on FreeRTOS, designed for maximum stability and performance. The system interfaces an ESP32-S3 microcontroller with an ADE7953 energy measurement IC via SPI, capable of monitoring:

- **17 Total Channels**: 1 direct channel (channel 0) + 16 multiplexed channels
- **Real-time Measurements**: Voltage, current, active/reactive/apparent power, power factor, and energy accumulation
- **Phase Support**: Single-phase measurements per channel, configurable for three-phase systems
- **High Accuracy**: Advanced calibration with no-load threshold detection and phase compensation

## Architecture Overview

### Task-Based Design (FreeRTOS)

The firmware has been completely redesigned around FreeRTOS tasks for improved stability and performance:

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
- **Consecutive Crash Tracking**: Automatic firmware rollback after 3 consecutive crashes
- **Core Dump Support**: Complete crash analysis with backtrace decoding
- **Breadcrumb System**: Track execution flow before crashes

#### Memory Management
- **PSRAM Optimization**: Efficient use of 2MB PSRAM on ESP32-S3
- **Heap Monitoring**: Automatic restart when heap drops below safe levels
- **Stack Analysis**: Per-task stack usage monitoring via TaskInfo

## Core Components

### Energy Measurement (ADE7953)
- **Precision IC**: Analog Devices ADE7953 single-phase energy measurement
- **Dual Channels**: Channel A (direct) and Channel B (multiplexed)
- **Advanced Features**: Line cycle accumulation, phase calibration, no-load detection
- **Real-time Processing**: Non-blocking measurement loops with FreeRTOS tasks

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
- **Dual Clients**: Local MQTT + AWS IoT Core integration
- **Secure Publishing**: TLS/SSL support with certificate authentication
- **Configurable Topics**: Flexible topic structure for data organization
- **Quality of Service**: Configurable QoS levels with retry logic

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
- **`ButtonHandler`**: Hardware button interface with task-based event processing
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
- **`/log.txt`**: System logs with rotation (max 1000 lines)
- **Energy Data**: Accumulation files for active/reactive/apparent energy
- **Daily Statistics**: Historical energy consumption data
- **CSV Exports**: Hourly energy data in CSV format for analysis

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
  - Power Factor with phase compensation
  - Energy accumulation (Wh, VARh, VAh)
- **Sampling Rate**: Channel 0 sampled every 200ms, others depend on active channels
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
- **Memory Usage**: Optimized PSRAM allocation, heap monitoring
- **Task Scheduling**: FreeRTOS with priority-based scheduling
- **Update Rates**:
  - Web Interface: 1-5 second updates
  - MQTT Publishing: Configurable (5-60 seconds typical)
  - Modbus TCP: Real-time on demand
  - InfluxDB: Batched writes with configurable intervals

## Communication Interfaces

### RESTful API
Comprehensive REST API with Swagger documentation (`/swagger.html`):

**Authentication Endpoints:**
- `POST /rest/auth/login` - Secure user authentication
- `POST /rest/auth/logout` - Session termination  
- `GET /rest/auth/status` - Authentication status
- `POST /rest/auth/change-password` - Password management

**System Management:**
- Device info, project info, WiFi status
- Firmware updates with MD5 verification
- Crash data analysis and core dump access
- System restart, factory reset, WiFi reset
- File management (list, retrieve, delete)

**Energy Data:**
- Real-time meter readings (all channels or individual)
- Historical energy data and statistics
- Direct ADE7953 register access for diagnostics
- CSV export functionality

**Configuration:**
- System settings (general, ADE7953, channels)
- Calibration parameters and validation
- MQTT and InfluxDB configuration
- Network and authentication settings

### MQTT Integration

#### AWS IoT Core (Optional)
- **X.509 Certificate Authentication**: Secure device authentication
- **Topic Structure**: Configurable with device-specific naming
- **SSL/TLS**: Encrypted data transmission
- **Will Messages**: Connection status monitoring

#### Local MQTT Broker
- **Flexible Authentication**: None, username/password, or certificate-based
- **QoS Support**: Configurable quality of service levels (0, 1, 2)
- **Retained Messages**: Status persistence across connections
- **JSON Payloads**: Structured data with timestamps and metadata

**Topics Example:**
```
energyme/[device-id]/status          # Device status
energyme/[device-id]/measurements    # Real-time readings  
energyme/[device-id]/energy         # Energy accumulation
energyme/[device-id]/diagnostics    # System diagnostics
```

### InfluxDB Time-Series Database

**Version Support:**
- **InfluxDB v1.x**: Username/password authentication
- **InfluxDB v2.x**: API token authentication

**Features:**
- **Line Protocol**: Automatic data formatting
- **Batch Writes**: Configurable batch sizes for efficiency
- **SSL/TLS**: Secure connections with certificate validation
- **Retry Logic**: Exponential backoff on connection failures
- **Buffer Management**: Memory-efficient data queuing

**Configuration:**
```json
{
  "enabled": true,
  "host": "influxdb.local",
  "port": 8086,
  "database": "energyme",
  "measurement": "power_data",
  "batchSize": 10,
  "writeInterval": 30
}
```

### Modbus TCP Server

**Industrial Standard Implementation:**
- **Port**: 502 (configurable)
- **Function Codes**: Read Holding Registers (FC03), Read Input Registers (FC04)
- **Register Types**: 16-bit integers, IEEE754 floats (2 registers)
- **Real-time Data**: Direct access to current measurements

**Register Mapping:**
```
0x0000-0x001F: System Information
0x0020-0x003F: Channel 0 (Direct) Measurements  
0x0040-0x005F: Channel 1 Measurements
...
0x0200-0x021F: Channel 16 Measurements
0x0300-0x031F: Aggregated Data (Total Power, Energy)
0x0400-0x041F: System Status and Diagnostics
```

## Security & Authentication

### Comprehensive Security Model
- **Token-Based Authentication**: JWT-style tokens with configurable expiration
- **HTTP-Only Cookies**: XSS attack prevention
- **BCrypt Password Hashing**: Strong password protection with salt
- **Session Management**: Automatic token refresh and expiration
- **Endpoint Protection**: Critical operations require authentication

### Default Credentials
- **Username**: `admin`
- **Password**: `energyme`

⚠️ **Security Notice**: Change default credentials immediately after first login

### Protected Operations
- System configuration changes
- Firmware updates and system restart
- Factory reset operations
- Log access and crash data
- Calibration modifications
- File system operations

## Secrets Management (Optional AWS Integration)

AWS IoT Core integration requires additional secret files:

```
secrets/
├── ca.pem              # AWS IoT Root CA certificate
├── certclaim.pem       # Device certificate
├── privateclaim.pem    # Device private key  
├── endpoint.txt        # AWS IoT endpoint URL
├── rulemeter.txt       # AWS IoT rule configuration
└── encryptionkey.txt   # Local data encryption key
```

**Note**: These files are only required for AWS IoT Core integration. The system operates fully without them using local MQTT brokers.

## Development & Build Information

### Platform Configuration
- **Framework**: Arduino 3.x on ESP32-S3
- **Build System**: PlatformIO with custom platform fork
- **Memory Configuration**: QIO QSPI for 2MB PSRAM access
- **Partition Table**: Custom 16MB layout optimized for OTA updates

### Dependencies
```ini
# Core Libraries
jijio/AdvancedLogger@2.0.0           # Enhanced logging system
bblanchon/ArduinoJson@7.4.2          # JSON processing
bblanchon/StreamUtils@1.9.0          # Stream buffering

# Network & Communication  
esp32async/AsyncTCP@3.4.8            # Async TCP foundation
esp32async/ESPAsyncWebServer@3.8.1   # Web server
knolleary/PubSubClient@2.8.0         # MQTT client
tzapu/WiFiManager@2.0.17             # WiFi configuration
miq19/eModbus@1.7.4                  # Modbus TCP

# Utilities
tobozo/ESP32-targz@^1.2.9            # Archive support
```

### Build Flags & Optimization
```ini
# Memory & Performance
-DBOARD_HAS_PSRAM                    # Enable PSRAM support
-DADVANCED_LOGGER_ALLOCABLE_HEAP_SIZE=8912  # Logger heap limit

# Environment Configuration  
-DHAS_SECRETS                        # Enable AWS IoT integration
-DENV_DEV                           # Development environment

# Debug Configuration
-g3 -O0                             # Maximum debug info, no optimization
-DCORE_DEBUG_LEVEL=4                # Verbose ESP32 debugging
-DDEBUG_ESP_CORE                    # Core debug messages
```

### Static Analysis & Quality
- **Tools**: cppcheck, clang-tidy integration
- **Compiler Warnings**: Wall, Wextra, format security checks
- **Memory Analysis**: Stack usage files (.su), heap monitoring

## Calibration & Setup

### Initial Configuration
1. **First Boot**: Captive portal for WiFi setup
2. **Authentication**: Change default admin credentials  
3. **Channel Configuration**: Set labels and enable active channels
4. **Calibration**: Use built-in calibration tools or ADE7953 tester

### Calibration Parameters
- **LSB Values**: Voltage, current, and power scaling factors
- **Phase Calibration**: Power factor accuracy compensation
- **No-Load Thresholds**: Noise elimination settings
- **Offset Corrections**: Zero-point adjustments for each measurement type

### Calibration Tools
- **Web Interface**: Real-time calibration with immediate feedback
- **ADE7953 Tester**: Direct register access for advanced calibration
- **CSV Export**: Historical data analysis for calibration validation

## Troubleshooting & Diagnostics

### Crash Analysis
- **Core Dumps**: Complete crash analysis with backtrace
- **RTC Memory**: Persistent crash data across reboots  
- **Breadcrumb System**: Execution tracking before crashes
- **Recovery Actions**: Automatic firmware rollback after 3 consecutive crashes

### System Monitoring
- **Task Stack Usage**: Real-time monitoring of all FreeRTOS tasks
- **Memory Health**: Heap, PSRAM, and NVS usage tracking
- **Network Status**: WiFi connection quality and diagnostics
- **Performance Metrics**: CPU temperature, free memory, uptime

### Debug Interfaces
- **Serial Monitor**: 115200 baud with exception decoder
- **UDP Logging**: Network-based log streaming
- **Web Interface**: Live system status and logs
- **API Access**: Programmatic diagnostics via REST endpoints

## Implementation Notes

### Memory Management
- **PSRAM Usage**: ArduinoJson automatically uses PSRAM when available
- **Heap Monitoring**: Automatic restart when heap drops below 10KB
- **Stack Analysis**: Per-task stack usage tracking prevents overflow
- **NVS Optimization**: Organized namespaces for efficient storage

### Task Scheduling
- **FreeRTOS Integration**: All major operations run in dedicated tasks
- **Priority Management**: LED control uses priority-based color assignment
- **Graceful Shutdown**: Tasks can be stopped cleanly with timeout handling
- **Resource Management**: Mutexes protect shared configuration data

### Network Optimization
- **Buffered Streams**: StreamUtils improves MQTT throughput
- **Connection Pooling**: Persistent connections where possible
- **SSL/TLS**: Hardware-accelerated encryption for secure connections
- **Retry Logic**: Exponential backoff for failed network operations

**Technical Notes:**
- Custom `millis64()` function prevents 49-day overflow issues
- Preferences API used across modules for distributed storage
- Arduino framework `delay()` automatically converts to `vTaskDelay()`
- PSRAM activation optimized for mbedtls (OTA and MQTT security)