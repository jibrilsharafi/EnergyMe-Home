# EnergyMe - Home | Source Code

**Note:** This source code is for hardware revision **v5** and represents a significantly more stable version of the firmware.

## Technical Overview

The system consists of an ESP32 microcontroller interfacing with an ADE7953 energy measurement IC via SPI, capable of monitoring:

- 1 direct channel (channel 0)
- 16 multiplexed channels
- Supports both single-phase and three-phase measurements
- Real-time monitoring of voltage, current, power (active/reactive/apparent), power factor, and energy

## Core Components

### ADE7953

Handles energy measurements with features like:

- No-load threshold detection
- Phase calibration
- Multiple measurement modes (voltage, current, power, energy)
- Line cycle accumulation mode for improved accuracy

### Additional Components

- **Multiplexer**: Manages channel switching for the 16 secondary channels
- **SPIFFS**: Stores configuration and calibration data
- **Web Interface**: Provides configuration, monitoring, and firmware update capabilities
- **Authentication System**: Token-based security with HTTP-only cookies, password hashing, and comprehensive endpoint protection
- **MQTT**: Enables remote data logging and device management (including optional AWS IoT Core integration)
- **InfluxDB Client**: Native time-series database integration with support for both v1.x and v2.x, buffering, and automatic retry logic
- **Modbus TCP**: Industrial protocol support
- **Crash Reporting**: Utilizes ESP32's RTC memory for post-crash diagnostics.

## Code Structure and Design

This section provides an overview of the source code organization and the design principles behind the EnergyMe-Home firmware.

### Directory Layout (within `source/`)

- `src/`: Contains the main application logic (`main.cpp`) and implementations of custom classes.
- `include/`: Header files for all custom classes and definitions (e.g., `constants.h`, `structs.h`).
- `html/`, `css/`: Web interface files (HTML, CSS). JavaScript, if used externally, would typically reside in a `js/` directory.
- `config/`: Default or example configuration files (e.g., `calibration.json`, `channel.json`).
- `resources/`: Non-code assets like images or, in this project, the `swagger.yaml` file.

### Key Software Modules

The firmware is built around several key modules/classes, typically found in `include/` and `src/`:

- `Ade7953`: Manages all interactions with the ADE7953 energy measurement IC.
- `Multiplexer`: Controls the analog multiplexers to switch between CT channels.
- `CustomServer`: Handles HTTP requests, serves web pages, and manages Restful API endpoints.
- `Authentication System`: Provides secure login, token management, password hashing, and session control.
- `CustomMQTT`: Manages MQTT broker connections, data formatting, and publishing (supports local and AWS IoT).
- `InfluxDBClient`: Handles connections to InfluxDB databases with support for both v1.x and v2.x, SSL/TLS, buffering, retry logic, and line protocol formatting.
- `ModbusTCP`: Implements the Modbus TCP server, defining registers and handling client requests.
- `CustomWifi`: Manages Wi-Fi connectivity, including the captive portal for initial setup.
- `CrashMonitor`: Implements the crash reporting feature using RTC memory.
- `Led`: Controls status LEDs.
- **Configuration Management**: Loads, saves, and applies system settings from JSON files on SPIFFS.
- **Data Logging/Storage**: Handles storage and retrieval of energy data.

### Design Principles

- **Modularity**: Code is organized into distinct classes/modules for maintainability and reusability.
- **Asynchronous Operations**: Leveraged for responsive network operations and sensor readings.
- **Configuration-driven**: System behavior is largely controlled by configuration files.
- **Robustness**: Features like crash reporting and MD5-checked firmware updates enhance stability.

## Secrets Management

The codebase includes support for integrating with AWS IoT Core and for local data encryption. This involves the use of several secret files, typically stored in a `secrets/` directory (which should be added to `.gitignore` if you create it):

```text
secrets/ (if used)
  ├── ca.pem            # AWS IoT Root CA (for AWS IoT integration)
  ├── certclaim.pem     # Device certificate for AWS IoT (for AWS IoT integration)
  ├── privateclaim.pem  # Device private key (for AWS IoT integration)
  ├── endpoint.txt      # AWS IoT endpoint (for AWS IoT integration)
  ├── rulemeter.txt     # AWS IoT rule configuration (for AWS IoT integration)
  └── encryptionkey.txt # Local data encryption key
```

**Important Note on Open-Source Distribution:**

- **AWS IoT Core Secrets**: The files `ca.pem`, `certclaim.pem`, `privateclaim.pem`, `endpoint.txt`, and `rulemeter.txt` are required **only** if you intend to use the AWS IoT Core integration. These files are typically part of privately compiled and distributed versions of the firmware and are **not included in this open-source repository**.
- **Functionality Without AWS Secrets**: The open-source firmware is designed to function fully without these AWS-specific secret files. If these files are not present, the AWS IoT Core integration features will be disabled, but all other functionalities, including local MQTT, web server, Modbus, etc., will operate normally.

## Data Storage

Configuration data is stored in JSON files:

- **calibration.json**: Measurement calibration values
- **channel.json**: Channel configuration
- **energy.json**: Energy accumulation data
- **daily-energy.json**: Daily energy statistics
- **influxdb.json**: InfluxDB connection and configuration settings

## Measurement Process

- Direct channel (0) reads voltage as reference
- Multiplexed channels (1-16) use voltage from channel 0
- Current measurement per channel
- Power calculations:
  - Active power from energy accumulation
  - Reactive power calculated from apparent and active power
  - Power factor compensation for three-phase loads

## Detailed Communication Interfaces

This section provides more specific details about the various communication interfaces supported by EnergyMe-Home.

### Restful API

The system exposes a comprehensive Restful API for configuration, data retrieval, and system control. All API responses are in JSON format.

- **Swagger Documentation**: The primary source for API details is `source/resources/swagger.yaml`, viewable with Swagger UI tools.
- **Base URL**: API endpoints are generally under `/rest/` (e.g., `http://<device_ip>/rest/...`). Utility endpoints like firmware update (`/do-update`) or Wi-Fi setup (`/wifisave` in AP mode) may have dedicated paths.
- **Authentication**: Most protected endpoints require authentication via Bearer token or HTTP-only cookie. Authentication endpoints include:
  - `POST /rest/auth/login` - User authentication
  - `POST /rest/auth/logout` - Session termination
  - `GET /rest/auth/status` - Authentication status check
  - `POST /rest/auth/change-password` - Password modification
- **Key Capabilities:**
  - **System Management**: Device status, project/device info, Wi-Fi status, firmware updates (info, status, execution with MD5 check), crash data/logs, restart, factory reset, Wi-Fi reset.
  - **Meter Readings**: All meter values, single channel data, specific values (e.g., active power), direct ADE7953 register access.
  - **Configuration**: Get/Set for general settings, ADE7953 parameters, channels, custom MQTT, InfluxDB configuration, calibration.
  - **InfluxDB Management**: Get/Set InfluxDB configuration, connection status, write statistics, connection testing.
  - **File Management**: List, retrieve, delete files on the device.

### MQTT Interface

The `CustomMQTT` module in EnergyMe-Home handles publishing data to an MQTT broker. It supports standard MQTT for local communication and includes optional integration with AWS IoT Core (contingent on the presence of necessary secrets, as detailed in the Secrets Management section).

- **Topic Structure**: The base topic is configurable via the `CustomMQTT` settings. Data is published to subtopics indicating its nature (e.g., real-time measurements, status). Refer to `custommqtt.h` and its implementation (`custommqtt.cpp`) for specifics on topic construction.
- **Data Payload**: Typically JSON, with measurements, timestamps, and channel identifiers, as formatted by `CustomMQTT`.
- **Authentication (managed by `CustomMQTT`)**:
  - Standard MQTT: Supports unprotected and username/password authentication, configurable through the system.
  - AWS IoT Core: If enabled by providing the necessary secrets, `CustomMQTT` will use X.509 certificates for authentication.
- **QoS (Quality of Service)**: `CustomMQTT` uses QoS 0 or 1 for data publishing. Details can be found by examining its usage of the `PubSubClient` library within `custommqtt.cpp`.
- **Will Message**: The `CustomMQTT` module can be configured to set a Will Message, notifying the broker if the device disconnects unexpectedly.

### Modbus TCP Server

The device acts as a Modbus TCP server for integration with SCADA systems and industrial equipment.

- **Port**: Configurable (default: 502).
- **Function Codes Supported**: Primarily "Read Holding Registers" (FC03). Refer to `modbustcp.h` and implementation for exact codes.
- **Register Mapping**:
  - Registers are 16-bit. Floating-point values span two registers.
  - Includes: system information, per-channel data (Voltage, Current, Powers, PF, Energy), and aggregated data.
  - Specific addresses and organization are in `modbustcp.h` and its `.cpp` file.
- **Data Types**: Integers or IEEE754 floating-point numbers. Byte order follows Modbus standards.

### InfluxDB Interface

The InfluxDB client provides seamless integration with InfluxDB time-series databases for long-term data storage and analysis.

- **API Endpoints**:
  - `GET /rest/get-influxdb-configuration` - Retrieve current InfluxDB configuration
  - `POST /rest/set-influxdb-configuration` - Update InfluxDB configuration settings
  - `GET /rest/influxdb-status` - Get connection status and write statistics (if implemented)
  - `POST /rest/influxdb-test` - Test connection with current settings (if implemented)
- **Configuration Parameters**:
  - **Server Settings**: Host, port, database/organization name, SSL/TLS settings
  - **Authentication**: Username/password (v1.x) or API token (v2.x)
  - **Data Settings**: Measurement names, write frequency, buffer settings
  - **Advanced**: Retry logic, timeout values, batch sizes
- **Data Format**: Automatic conversion to InfluxDB line protocol with configurable field mapping
- **Status Monitoring**: Real-time connection status and write statistics available through web interface
- **Error Handling**: Comprehensive error logging and automatic retry with exponential backoff

## System Specifications

This section outlines the key technical specifications and capabilities of the EnergyMe-Home system.

### Measurement Capabilities

- **Monitored Circuits**: 1 direct channel (typically main incomer) + 16 multiplexed channels.
- **Measured Electrical Parameters**:
  - Voltage (RMS)
  - Current (RMS)
  - Active Power (W)
  - Reactive Power (VAR)
  - Apparent Power (VA)
  - Power Factor
  - Active Energy (Wh/kWh)
  - Reactive Energy (VARh/kVARh)
  - Apparent Energy (VAh/kVAh)
- **Accuracy**: Dependent on CT sensor quality, proper calibration, and ADE7953 configuration.
- **Voltage Measurement Range**: universal AC voltage range (e.g., 90-265V AC RMS).
- **Current Measurement Range**: Dependent on selected Current Transformers (CTs), but output voltage should be always 333 mV or lower.
- **Sampling Rate**: The ADE7953 has high-frequency sampling (channel 0 is always sampled every 200 ms, while the others depend on the amount of active channels).
- **Phase Support**: Single-phase measurements per channel. Can be used in three-phase systems by monitoring each phase individually, by assuming a constant phase shift of 120 degrees between phases and same voltage reference.

### Hardware Interfaces

- **Microcontroller**: ESP32-S3.
- **Energy Measurement IC**: ADE7953 (communicates via SPI).
- **Multiplexer Control**: Via GPIOs from the ESP32-S3.
- **User Interface**: Onboard LEDs for status, web interface.

### Data Management

- **Local Storage**: SPIFFS (Serial Peripheral Interface Flash File System) on ESP32 flash.
- **Configuration Storage**: JSON files (e.g., `calibration.json`, `channel.json`).
- **Energy Data Storage**: JSON files (e.g., `energy.json`, `daily-energy.json`).
- **Data Update Rates**:
  - Web Interface: (e.g., Real-time data updated every 1-5 seconds).
  - MQTT Publishing: (e.g., Configurable, typically every 5-60 seconds).
  - Modbus TCP: Data available on poll.

### Power Supply

- **Input Voltage**: universal AC voltage range (e.g., 90-265V AC RMS).
- **Power Consumption**: less than 1VA

## Calibration

Calibration is a critical step to ensure accurate energy readings. The system allows for calibration of various parameters, which can be adjusted via the web interface.

Calibration parameters include:

- LSB values for voltage, current, power measurements
- Phase calibration for accurate power factor readings
- No-load thresholds to eliminate noise

## Crash Reporting

To aid in debugging and improve long-term stability, the system features a crash reporting mechanism.
If the ESP32 encounters a critical error and reboots, details about the crash (such as the call stack and error type) are stored in the RTC (Real-Time Clock) memory. This memory persists across reboots (but not power cycles).
The crash information can then be retrieved and viewed, typically through the web interface or via serial monitor, allowing developers to diagnose the cause of unexpected resets.
This system is invaluable for identifying and fixing bugs in the firmware.

## Security & Authentication

EnergyMe-Home implements a comprehensive authentication system to secure access to the web interface and protect critical system operations.

### Authentication Features

- **Token-Based Authentication**: Secure JWT-style token generation with configurable expiration
- **HTTP-Only Cookies**: Session tokens stored securely to prevent XSS attacks
- **Password Hashing**: Strong bcrypt-based password hashing with salt
- **Session Management**: Automatic session expiration and renewal
- **Protected Endpoints**: Critical API endpoints require authentication including:
  - System configuration changes
  - Factory reset operations
  - Firmware updates and system restart
  - Log access and management
  - Calibration modifications
  - File system operations

### Default Credentials

- **Username**: `admin`
- **Password**: `energyme`

**Important**: Change the default credentials immediately after first login for security.

### Implementation Details

Authentication implementation spans multiple modules:

- **`CustomServer`**: Handles login/logout endpoints, session validation, and endpoint protection
- **`utils.h/cpp`**: Contains core authentication functions (password hashing, token generation, validation)
- **`auth.js`**: Client-side authentication manager with automatic token handling
- **Configuration Files**: Secure storage of hashed passwords and session data

The system uses ESP32 Preferences for secure credential storage and implements comprehensive error handling with fallback mechanisms for enhanced reliability.

**Various notes**
- The PSRAM usage should not interfere heavily with Preferences, and in any case it should at worst slow it down, not crash the system. Given this assumption, the usage of Preferences is retained in the single modules and files, instead of being centralized in a single file.
- The ArduinoJson library uses by default the PSRAM when possible, thus already optimizing memory usage.
- The arduino framework converts delay to vTaskDelay anyway, so we use delay for cleaner APIs.
- The custom `millis64()` function is used in place of the standard `millis()` that returns a 32-bit value and thus overflows after only 49 days.