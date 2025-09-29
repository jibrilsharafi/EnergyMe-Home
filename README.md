# EnergyMe-Home

![Homepage](resources/homepage.png)

EnergyMe-Home is an open-source energy monitoring system designed for home use, capable of monitoring up to 17 circuits and integratable with any platform thanks to its Restful API, MQTT, and Modbus TCP support. Moreover, a cloud platform is available for free to store and visualize your data.

## Introduction

Welcome to ***EnergyMe-Home***, fellow energy-enthusiast! This project is all about making energy monitoring easy, affordable, and accessible for everyone. As a maker, I wanted to create a fully open-source energy meter that anyone can build and customize. *EnergyMe-Home* can **monitor up to 17 circuits at once**, giving you detailed insights into your home's energy consumption.

By combining hardware and software, this project empowers you to take control of your energy usage and simultaneously save on costs. Whether you're a DIY enthusiast, an energy-conscious homeowner, or just curious about IoT and energy monitoring, EnergyMe-Home is here to help you get started.

## Hardware

![PCB](resources/PCB%20top%20view.jpg)

The hardware (currently at **v5**) consists of both the PCB design and the components used to build the energy monitoring system.

The key components include:

- ESP32-S3: the brain of the project
- ADE7953: single-phase energy measurement IC
- Multiplexers: used to monitor multiple circuits at once
- 3.5 mm jack connectors: used to easily connect current transformers

PCB schematics and BOMs are available in the `documentation/Schematics` directory, while datasheets for key components are in the `documentation/Components` directory. Additional hardware specifications and technical details can be found in the [`documentation/README.md`](documentation/README.md).

The project is published on *EasyEDA* for easy access to the PCB design files. You can find the project on [EasyEDA OSHWLab](https://oshwlab.com/jabrillo/multiple-channel-energy-meter).

## Software

The software is written in C++ using the *PlatformIO* ecosystem and *Arduino 3.x framework*. The firmware is built on a **modern task-based architecture using FreeRTOS** for maximum stability and performance. All source code is available in the [`source`](source) directory with comprehensive documentation.

**Key Architecture Features:**
- **FreeRTOS Task-Based Design**: All major operations run in dedicated tasks for improved stability and resource management
- **Advanced Crash Recovery**: RTC memory-based crash monitoring with automatic firmware rollback
- **Memory Optimization**: PSRAM utilization and intelligent heap management
- **Task Monitoring**: Real-time stack usage and performance monitoring for all system tasks

**Core Software Features:**

- **ADE7953** driver with task-based meter reading, energy accumulation, and CSV logging
- **Comprehensive Authentication System** with token-based security, HTTP-only cookies, password hashing, session management, and protected endpoints
- **Advanced Crash Monitoring** system leveraging RTC memory of ESP32 with breadcrumb tracking and core dump analysis
- **Dual MQTT Clients** for both local brokers and AWS IoT Core integration with secure publishing
- **InfluxDB Integration** for seamless time-series data storage with support for both v1.x and v2.x, SSL/TLS, buffering, and automatic retry logic
- **Async Web Server** for real-time monitoring, historical data, firmware updates (with MD5 integrity check), and system configuration
- **Captive Wi-Fi Portal** for easy initial Wi-Fi configuration with connection monitoring
- **Multi-Priority LED Control** for visual status indication with task-based management
- **Modbus TCP Server** for serving data to industrial SCADA systems
- **16-Channel Multiplexer** control for sequential circuit monitoring
- **mDNS Support** for local network discovery (`energyme.local`)
- **Enhanced Stability Features** including consecutive crash counting, memory management, and automatic recovery
- **UDP Network Logging** with real-time log streaming
- **Secure Firmware Updates** with MD5 hash verification and automatic rollback on failure
- **Secrets Management** for secure credential handling (optional AWS IoT integration)
- **Preferences-Based Storage** with organized namespaces for configuration data
- **Comprehensive Calibration Tools** including web interface and direct ADE7953 register access

**Performance & Reliability:**
- Non-blocking operations with configurable update intervals
- Stack usage monitoring for all FreeRTOS tasks
- Automatic restart on low memory conditions
- Core dump analysis for debugging crashes
- Maintenance task for system health monitoring

Detailed implementation information and API documentation are available in the [`source/README.md`](source/README.md).

## Integration

### Restful API

All the data collected by the energy monitoring system, as well as the configuration settings, can be accessed through the Restful API.

![Swagger](resources/swagger.png)

A complete swagger documentation of the Restful API is available in the [`swagger.yaml`](source/resources/swagger.yaml) file.

### MQTT

The energy monitoring system can also publish data to an MQTT broker (unprotected and protected with username and password). Implementation details are documented in [`source/include/custommqtt.h`](source/include/custommqtt.h).

### Modbus TCP

The energy monitoring system can also act as a Modbus TCP server, exposing many registers for reading the data (single channel and aggregated data). Implementation details are documented in [`source/include/modbustcp.h`](source/include/modbustcp.h).

### InfluxDB Integration

EnergyMe-Home now includes native InfluxDB integration for seamless time-series data storage and visualization. The system supports both InfluxDB v1.x and v2.x with the following features:

- **Universal InfluxDB Support**: Compatible with both InfluxDB v1.x (username/password) and v2.x (token-based authentication)
- **SSL/TLS Security**: Secure connections with configurable SSL certificate validation
- **Intelligent Buffering**: Local data buffering with configurable batch sizes and automatic retry logic
- **Connection Management**: Automatic reconnection with exponential backoff for enhanced reliability
- **Flexible Configuration**: Configurable measurement names, field mapping, and write frequency
- **Real-time Monitoring**: Web interface shows connection status and write statistics
- **RESTful API**: Complete API endpoints for InfluxDB configuration and management

The InfluxDB client automatically formats energy data using the line protocol and handles both real-time measurements and energy accumulations. Configuration is available through the web interface with real-time status monitoring and connection testing. Implementation details are documented in [`source/include/influxdbclient.h`](source/include/influxdbclient.h).

## Security & Authentication

EnergyMe-Home features a comprehensive authentication system to secure access to the web interface and protect critical system operations. The system provides enterprise-grade security while maintaining ease of use.

### Key Security Features

- **Token-Based Authentication**: Secure session management with automatic expiration
- **Password Protection**: Strong password hashing and secure credential storage
- **Protected Operations**: All critical system functions require authentication
- **Session Security**: HTTP-only cookies and automatic session renewal
- **Vulnerability Protection**: Built-in protection against common web security threats

### Default Credentials

- **Username**: `admin`
- **Password**: `energyme`

**Important**: Change the default credentials immediately after first login for security.

### Getting Started

1. Navigate to your device's IP address
2. Use the default credentials to log in
3. Change the default password when prompted
4. Access all system features through the authenticated web interface

The authentication system is fully integrated with the REST API and supports secure session management across all device features. For detailed technical implementation information, see the [source code documentation](source/README.md#security--authentication).

### Home Assistant Integration

EnergyMe-Home integrates with Home Assistant through a dedicated custom integration for convenient energy monitoring directly in your dashboard.

![Home Assistant Integration](resources/Home%20Assistant%20integration.png)

- **Simple Setup**: Only requires the device IP address (automatic discovery via mDNS coming soon)
- **Real-time Data**: All energy measurements and system information as Home Assistant entities
- **Complete Access**: Monitor all circuits and aggregate data within Home Assistant

Get started at [homeassistant-energyme](https://github.com/jibrilsharafi/homeassistant-energyme).

## Contributing

Contributions are welcome! Please read the [contributing guidelines](CONTRIBUTING.md) for more information.

## License

This project is licensed under the terms of the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.
