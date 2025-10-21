# EnergyMe - Home | Source Code

**Platform:** ESP32-S3 with Arduino 3.x Framework  
**Hardware Revision:** See `include/pins.h`

## System Overview

Task-based energy monitoring system built on FreeRTOS. ESP32-S3 interfaces with ADE7953 energy measurement IC via SPI.

**Monitoring Capabilities:**

- **17 channels**: 1 direct (channel 0) + 16 multiplexed
- **Measurements**: Voltage, current, active/reactive/apparent power, power factor, energy accumulation
- Single-phase per channel (three-phase assumes 120° shift, same voltage reference - see `ade7953.cpp`)
- Calibration with no-load threshold detection

## Architecture

### FreeRTOS Task-Based Design

```cpp
void setup() {
    // Initialize components
    // Start maintenance task
    startMaintenanceTask();
    
    // Delete main task after setup
    vTaskDelete(NULL);
}

void loop() {
    // All work done in dedicated tasks
    vTaskDelay(portMAX_DELAY);
}
```

**Active Tasks:**

- **Maintenance**: System health, memory management
- **ADE7953**: Meter reading, energy saving, CSV logging
- **Network**: WiFi, MQTT clients, web server
- **System**: Crash monitoring, LED control, button handling
- **Storage**: InfluxDB client, UDP logging

### Stability Features

**Crash Monitor - Two-Tier Restart Protection:**

The system implements a hierarchical protection against restart loops to ensure device remains accessible:

#### Tier 1: Safe Mode (Rapid Loop Protection)

- **Trigger**: 5+ restarts within 60 seconds each (20+ in dev mode)
- **Action**:
  - Blocks all restart attempts for 5-minute minimum uptime
  - **All systems remain operational**: WiFi, OTA, web interface, ADE7953, APIs
  - Visual indicator: Purple LED
- **Recovery**: Auto-clears after 30 minutes of stable operation or next restart > 60s uptime
- **Use Case**: Any rapid restart loop condition (software bugs, config issues, etc.)

#### Tier 2: Rollback/Factory Reset (Persistent Failure Recovery)

- **Trigger**: 3+ consecutive crashes OR 10+ consecutive resets (10/30 in dev mode)
- **Action**:
  1. Attempts firmware rollback (if available, once per boot cycle)
  2. Performs factory reset if rollback unavailable or failed
- **Recovery**: Counters reset after 180 seconds of stable operation
- **Use Case**: Bad firmware updates or persistent hardware failures

#### How They Work Together

- Safe mode activates first for rapid loops (prevents OTA lockout, keeps device accessible)
- Crash/reset tracking continues in background
- If failures persist in safe mode → triggers rollback/factory reset
- Independent counter management (timing-based vs stability-based)

#### Additional Features

- RTC memory persistence (survives reboots)
- ESP32 core dump support with backtrace decoding
- Automatic MQTT crash reporting (if configured)

**Memory Management:**

- PSRAM for queues and buffers
- Automatic restart on low heap
- Per-task stack monitoring via TaskInfo

## Core Components

**ADE7953 Driver:**

- Analog Devices ADE7953 single-phase energy measurement IC
- Dual channels: A (direct) and B (multiplexed)
- Line cycle accumulation, no-load detection
- Task-based non-blocking measurement loops

**Multiplexer:**

- 74HC4067PW 16-channel analog multiplexer
- Sequential scanning for continuous monitoring
- GPIO control via ESP32-S3 (S0-S3 select lines)

**Web Interface:**

- Responsive UI with real-time updates
- RESTful API with Swagger documentation
- Token-based authentication with HTTP-only cookies
- Pages: Dashboard, System Info, Configuration, Channel Setup, Calibration, ADE7953 Tester, Firmware Updates, Logs, API Docs

**Communication:**

- **MQTT**: Dual clients (AWS IoT Core + local broker support)
- **InfluxDB**: Native client for v1.x and v2.x with SSL/TLS and batching
- **Modbus TCP**: Industrial protocol for SCADA integration and other systems

## Code Structure

```text
source/
├── src/                   # Implementation files
│   ├── main.cpp           # Application entry point
│   ├── ade7953.cpp        # Energy IC driver
│   ├── crashmonitor.cpp   # Crash detection and recovery
│   ├── customserver.cpp   # Web server and API
│   ├── custommqtt.cpp     # MQTT clients
│   ├── influxdbclient.cpp # Time-series database client
│   └── ...
├── include/               # Headers
│   ├── constants.h        # System constants
│   ├── structs.h          # Data structures
│   ├── globals.h          # Global declarations
│   └── ...
├── html/                  # Web pages
├── css/                   # Stylesheets
├── js/                    # Client scripts
└── resources/             # Static assets
```

**Key Modules:**

- **System**: `CrashMonitor`, `Led`, `ButtonHandler`, `CustomTime`
- **Energy**: `Ade7953`, `Multiplexer`
- **Network**: `CustomWifi`, `CustomServer`, `Mqtt`, `CustomMqtt`, `InfluxDbClient`, `ModbusTcp`
- **Storage**: `Preferences API`, `LittleFS`

**Design Principles:**

- Task-based operations with FreeRTOS
- Crash resilience with automatic recovery
- PSRAM utilization and stack monitoring
- Token-based authentication
- Non-blocking operations

## Configuration & Storage

**Preferences (ESP32 NVS):**

- `general_ns`: System settings, device configuration
- `ade7953_ns`: Energy IC parameters
- `calibration_ns`: Measurement calibration values
- `channels_ns`: Per-channel configuration
- `mqtt_ns` / `custom_mqtt_ns`: MQTT broker settings
- `influxdb_ns`: InfluxDB configuration
- `auth_ns`: Authentication credentials
- `wifi_ns`: Network configuration
- `crashmonitor_ns`: Crash recovery settings

**LittleFS Files:**

- `/log.txt`: System logs with rotation (max 1000 lines)
- Energy data: Accumulation files for active/reactive/apparent energy
- CSV exports: Hourly data zipped daily (up to 10 years storage)

**Task Monitoring:**

All tasks provide `TaskInfo` structures with stack usage metrics:

- Allocated stack size
- Minimum free stack observed
- Free/used percentage

Monitored tasks: MQTT clients, web server, ADE7953 operations, crash monitor, LED control, maintenance, WiFi, UDP logging, InfluxDB.

## System Specifications

**Monitoring:**

- 17 circuits: 1 direct + 16 multiplexed
- Parameters: RMS voltage/current, active/reactive/apparent power, power factor, energy accumulation
- Sampling: Channel 0 every 200ms, others every 400ms minimum (depends on active channels)
- Accuracy: Typically ±1% with proper CT calibration
- Voltage range: 90-265V RMS (universal AC input)
- Current range: Configurable via CT (max CT output: 333mV)

**Hardware:**

- ESP32-S3: Dual-core, 16MB Flash, 2MB PSRAM
- Communication: SPI to ADE7953, I2C available
- Power: <1W via onboard AC/DC converter

**Performance:**

- Update rates: Web (1s), MQTT (5-60s configurable), Modbus TCP (on demand), InfluxDB (batched, configurable)
- **Modbus TCP**: ~200 polls/sec, ~5ms avg response, 100% reliability (1000 consecutive reads). All ~200 registers are readable in ~1s.
- **REST API**: ~150ms avg response across 30+ endpoints, 100% success rate (300 requests)

## API & Integration

**REST API:**  
Swagger documentation at `/swagger.html` covers authentication, system management, energy data access, and configuration.

**MQTT:**  

- AWS IoT Core (requires secrets)
- Local broker support with configurable authentication
- TLS/SSL with certificates

**InfluxDB:**  
Both v1.x and v2.x support, line protocol, batch writes, SSL/TLS, retry logic.

**Modbus TCP:**  
FC03/FC04 function codes, register mapping for system info and measurements.

## Security

**Authentication:**  
Token-based sessions with automatic expiration, BCrypt password hashing, HTTP-only cookies.

**Default Credentials:**  
Username: `admin` | Password: `energyme`

⚠️ Change immediately after first login.

## AWS IoT Integration (Optional)

Requires files in `secrets/` directory:

- `ca.pem`, `certclaim.pem`, `privateclaim.pem` (X.509 certificates)
- `endpoint.txt`, `rulemeter.txt` (AWS IoT config)
- `encryptionkey.txt` (local encryption key)

System works without these using local MQTT brokers via CustomMqtt.

## Development

**Build:**  
PlatformIO with Arduino 3.x framework on ESP32-S3.

**Dependencies:**  
AdvancedLogger, ArduinoJson, StreamUtils, ESPAsyncWebServer, PubSubClient, WiFiManager, eModbus

**Tools:**  
Static analysis (cppcheck, clang-tidy), ESP32 core debugging, PSRAM optimization

## Diagnostics

**Crash Analysis:**  
Core dumps, RTC memory persistence, automatic recovery/rollback

**Monitoring:**  
Task stack usage, heap/PSRAM tracking, network status

**Debug Access:**  
Serial (115200 baud), UDP logging, web interface, REST API
