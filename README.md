# <img src="resources/logo.png" width="32" height="32" align="center"> EnergyMe-Home

<!-- All the usual badges (license, stars, release, etc.) -->
![Online](https://img.shields.io/badge/dynamic/json?url=https://4uemnt5duad63ldajxymghlv3e0yvwnw.lambda-url.eu-central-1.on.aws/&query=$.total_active&style=for-the-badge&label=EnergyMe-Home%20Active&color=green&logo=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAACXBIWXMAAAHYAAAB2AH6XKZyAAAD+ElEQVR42r2Xa0xTVxzAyRaBrcRlU4bZK5hhhiPZJGaguEUZzrgikOHI1kQT34+YGD/4SExM6r23two2tS8KrSCtpbWYKEQUFFEuglBa2tSm1kd8EASiH4wYTTRK/95zg8U+bnvv9fHh96Ef2t/vnHvO6blJAJAkhH8u65anH699VdihsaLPQhH0pVJKXT7DVjMxzWSEz8wGKKbU6z5awIo2o3S2/cgrJH/DDHomxL3KeR88YHlbfUOmzRRE0kiyTunHV5+rEn2wgKXtNc0ZlqNIxkpBu673vQesuStNnd9a7UeCRKSYjVDWrdrw3gL+7jDl5p80P2MEHMk6qX+ScFABZaYU4JO4AaWt9Vvzmo4HRaa6KQFHyqjDeyJ/zzYmz7SNkTtto6TjX+8BKHETK1kDVpyrU82xm4LJvMRT5J/VXUO/0zQsz7KOkLtpqdM2KgdE/X05fN2DQ5GHtMQMEJ9tOJ3RyCw2QXxpNUBpl+qpdVR+HQkjKfcSkNKFQZ6T8EUFFLcf3fettYGXcHqjARa262CdQwkHb1SBdQSJYqMeIiGNwpiA7H7iUViA5NKhmd/YDS+5in87o4PdXgWY7x+YFCRmoYsZPUP6ZWwiLKCgTdvJRTy3uRqwQBWLhJ1dN0kkDiGiMAgFlFBVs9MsxmA8cTJNaZcaLMwU88M4TEJ6DxKHg84YJqCwQ1uXSL7NpeQpnmLRIIGEUZT5FN8zAT+d0j+IF/Bfj0qwfPt1GZLFROKTZTAB6fbal+x7WgtWgfLKeyRM744tF9EwayCpqenTVBZ5msUA2jsHBckNw3L4rhfJYjOzZ3IX5LcczmAbvfiiWpDcRC/UnH4ciViZ0088ZgLQSkw2xw5Q3KrkLW+k5QWh/c7OIid+JbQN6eMzagvOstfwlltplroTyxFiDy4NBWQ3V49HBKDjlbd8mYebHJ2CFX5pWihg8XktFRmw8oqGsxwdTn9yHDnij0GiM+xCsn5AUZQSsQ5WObScF1yei7v8q248KLlKZkfdiH5u0Y+9HbDGmXgG0H/7LwM4ZzniLzehiXklE3eqJW9fQNa64m9BzRAJP/bxk//qwIfj3glzWqpH3gRscLPPAHabpPfxfl7yrD58HB29cQMqutXFqZNrYSNLwJaADEQ8xIgcB/6wwl85i9OtOLdVfxMFbPJoop73kkGCl/gLCoMij6x1vqt2GudrefklxQLRMSNs9k4FyO+Q8EMvzln8OU3uADZU4Sd+F/ReUHhB07flqh6OjZAg8REg4iKlmOf8fIlHdnGtH8t/pxeTHb4jxXtv6CY2BnCY66gMY57zECxwK4OLPcoXy7yqRyU+VeB/v+rEnnuKAiEvp68BSJih/3uMeCMAAAAASUVORK5CYII=)

[![GitHub release](https://img.shields.io/github/release/jibrilsharafi/EnergyMe-Home.svg)](https://github.com/jibrilsharafi/EnergyMe-Home/releases)
[![GitHub stars](https://img.shields.io/github/stars/jibrilsharafi/EnergyMe-Home.svg?style=social&label=Stars)](https://github.com/jibrilsharafi/EnergyMe-Home/stargazers)
[![GitHub issues](https://img.shields.io/github/issues/jibrilsharafi/EnergyMe-Home.svg)](https://github.com/jibrilsharafi/EnergyMe-Home/issues)
[![GitHub license](https://img.shields.io/github/license/jibrilsharafi/EnergyMe-Home.svg)](https://github.com/jibrilsharafi/EnergyMe-Home/blob/main/LICENSE)

<a href="https://certification.oshwa.org/it000025.html"><img src="resources/certification-mark-IT000025-wide.svg" alt="OSHWA Certified Open Source Hardware IT000025" height="40" style="background:#ffffff;"></a>
<a href="https://www.tindie.com/stores/energyme/?ref=offsite_badges&utm_source=sellers_jabrillo&utm_medium=badges&utm_campaign=badge_small"><img src="https://d2ss6ovg47m0r5.cloudfront.net/badges/tindie-smalls.png" alt="I sell on Tindie" height="42"></a>

![Homepage](resources/homepage.png)

EnergyMe-Home is an open-source energy monitoring system for home use, capable of monitoring up to 17 circuits. It integrates with any platform through REST API, MQTT, Modbus TCP, and InfluxDB.

Built for makers and DIY enthusiasts who want to track their home's energy consumption. The hardware uses ESP32-S3 and ADE7953 energy measurement IC, while the firmware is written in C++ with PlatformIO and Arduino framework. You can build and customize it yourself - all hardware designs and software are open-source.

## Supported Electrical Systems

EnergyMe-Home is compatible with various electrical systems worldwide:

| System Type | Voltage | Configuration | Supported |
|-------------|---------|---------------|-----------|
| **Single Phase** | 230V | L + N | ✅ Europe, Asia, Africa, Oceania |
| **Split Phase** | 120V/240V | L1 + L2 + N | ✅ North America residential |
| **Split Phase** | 120V/240V | L1 + L2 (no N) | ✅ Old electrical systems |
| **Three Phase** | 400V/230V | 3L + N | ❕ Europe commercial/industrial (derived voltages) |
| **Three Phase** | 208V/120V | 3L + N | ❕ North America commercial (derived voltages) |

**Current Transformers Supported:**

- 333 mV output CTs (+- 500 mV absolute)
- 3.5 mm jack connectors for easy plug-and-play

**Monitoring Capacity:**

- Up to **17 circuits** simultaneously
- 1× main circuit (typically 50A CT), sampled at high frequency for accurate total energy measurement
- 16× branch circuits (typically 30A CTs), multiplexed for individual monitoring

⚠️ **Important**: Installation requires working with your main electrical panel. Always consult a qualified electrician if you're not experienced with electrical installations. The user is responsible for ensuring compliance with local electrical codes and regulations, and assumes all risks associated with the installation and use of this device.

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

The firmware is built with C++ using the *PlatformIO* ecosystem and *Arduino 3.x framework*, with a **task-based architecture using FreeRTOS**.

**Key Features:**

- **Energy Monitoring**: ADE7953 driver with energy accumulation and CSV logging
- **Web Interface**: Dashboard for monitoring and system configuration
- **Authentication**: Token-based security with password protection
- **Integration Options**: REST API, MQTT, InfluxDB, and Modbus TCP
- **Crash Recovery**: Automatic recovery and firmware rollback on failures
- **WiFi Setup**: Captive portal for configuration and mDNS support (`energyme.local`)
- **OTA Updates**: Firmware updates with MD5 verification and rollback
- **Waveform Analyzer**: Capture high-resolution voltage & current waveforms per channel from the web UI

For detailed architecture, implementation details, and API documentation, see [`source/README.md`](source/README.md).

## Integration

EnergyMe-Home offers multiple integration options:

- **REST API**: Complete Swagger-documented API for all data and configuration ([swagger.yaml](source/resources/swagger.yaml))
- **MQTT**: Publish energy data to any MQTT broker with optional authentication
- **Modbus TCP**: Industrial protocol for SCADA systems integration
- **InfluxDB**: Support for both v1.x and v2.x with SSL/TLS and buffering
- **Home Assistant**: Dedicated custom integration ([homeassistant-energyme](https://github.com/jibrilsharafi/homeassistant-energyme))

For detailed integration guides and implementation details, see the [documentation](documentation/README.md).

## Home Assistant Integration

EnergyMe-Home integrates with Home Assistant through a dedicated custom integration.

![Home Assistant Integration](resources/homeassistant_integration.png)

- **Setup**: Requires device IP address (automatic discovery via mDNS coming soon)
- **Data**: All energy measurements and system information as Home Assistant entities
- **Access**: Monitor all circuits and aggregate data within Home Assistant

Get started at [homeassistant-energyme](https://github.com/jibrilsharafi/homeassistant-energyme).

## Getting Started

1. **Order the PCB**: Download the design files from the [Schematics folder](documentation/Schematics/) and order from your preferred PCB manufacturer
2. **Populate the board**: Solder all components using the BOM in `documentation/Schematics`
3. **Flash the firmware**: Connect a USB-to-UART adapter to the UART pins and flash using PlatformIO
4. **Configure WiFi**: Power on the device and connect to the captive portal to set up WiFi credentials
5. **Start monitoring**: Access the web interface at `http://energyme.local` (default credentials: *admin*/*energyme*)

For detailed build instructions and troubleshooting, see the [documentation](documentation/README.md).

## Privacy & Telemetry

Optional anonymous usage statistics sent once per boot: hashed device ID, firmware version, country/city (from IP). Helps count active installations. No personal data or usage patterns collected. Disable in `platformio.ini`: set `ENABLE_OPEN_SOURCE_TELEMETRY=0`.

## Contributing

Contributions are welcome! Please read the [contributing guidelines](CONTRIBUTING.md) for more information.

## License

This project contains both software and hardware components:

- **Software**: Licensed under the [GNU General Public License v3.0](LICENSE) (GPL-3.0)
- **Hardware**: Licensed under the [CERN Open Hardware Licence Version 2 - Permissive](LICENSE-HARDWARE) (CERN-OHL-P-2.0)

See the respective license files for full details.
