# EnergyMe-Home: PCB & Firmware Datasheet

Technical specifications for the open-source *EnergyMe-Home* board (hardware **v6.1**) and its firmware. This datasheet covers the PCB and firmware only - the open-source deliverable. Enclosure, supplied current transformers, packaging, and regulatory conformity belong to the assembled commercial product and are out of scope here.

For the circuit design and PCB layout, see the [Hardware reference](README.md). For installation and use, see the [Setup & Usage Guide](../manual/README.md).

---

## 1. Identification

| | |
| --- | --- |
| **Project** | EnergyMe-Home (open-source energy monitor) |
| **Hardware revision** | v6.1 |
| **Open-source certification** | OSHWA `IT000025` |
| **License (firmware)** | GPL-3.0 |
| **License (hardware)** | CERN-OHL-S-2.0 |
| **Repository** | <https://github.com/jibrilsharafi/EnergyMe-Home> |

---

## 2. PCB

| Parameter | Value |
| --- | --- |
| Layers | 4 |
| Dimensions | 87 x 50 mm |
| Height with components | ~15 mm |
| Mounting | Designed for a 3-module DIN-rail enclosure |
| CT inputs | 16 x 3.5 mm stereo jack (PJ-3200) |
| Line input | 2-pin screw terminal, 300 V / 3 A |
| Programming header | 2x4 2.54 mm (3V3, GND, EN, IO0, TX, RX, D+, D-) |
| Expansion headers | 2x, for branch CT channels beyond the base 8 |

Layer stack-up: L1 signal/components, L2 ground plane, L3 3.3 V power plane, L4 signal. Full PCB design files (schematics, gerbers, BOM, pick-and-place) are available under [`hardware/pcb/`](pcb/).

---

## 3. Electrical

| Parameter | Value |
| --- | --- |
| Supply voltage | 100-240 V AC |
| Supply frequency | 50 / 60 Hz |
| Board power consumption | ~1 W typical (~200 mA at 3.3 V) |
| Safety class | Class II (double insulation) |
| Internal fuse | Slow-blow (time-lag), 500 mA |
| Galvanic isolation (voltage measurement) | Yes, via ZMPT107-1 voltage transformer |
| Measurement channels | 16 (1 direct + 15 multiplexed) |
| Channel numbering | 0 (main) ... 15 (branches) |
| Voltage measurement range | Single-phase, nominal 100-240 V AC |
| Current measurement | Via external voltage-output CT, up to CT-rated current |
| CT input safe range | <= 0.5 V at the CT secondary (333 mV nominal) |
| Measurement accuracy | < 1 % typical with calibrated voltage-output CTs |
| Energy accumulation | kWh per channel, with CSV logging |

### 3.1 Supported electrical systems

The metering chain monitors single-phase and (as derived single-phase per leg) three-phase systems:

| System | Voltage | Notes |
| --- | --- | --- |
| Single phase | 230 V (L + N) | Europe, Asia, Africa, Oceania |
| Split phase | 120 / 240 V (L1 + L2 + N) | North America residential |
| Split phase | 120 / 240 V (L1 + L2, no N) | Older electrical systems |
| Three phase | 400 / 230 V (3L + N) | Commercial, derived voltages |
| Three phase | 208 / 120 V (3L + N) | North America commercial, derived voltages |

Three-phase systems are monitored as derived single-phase voltages per leg, each on its own channel; the board is a single-phase metering platform, not a true polyphase meter.

---

## 4. Radio

| Parameter | Value |
| --- | --- |
| Wireless standard | Wi-Fi IEEE 802.11 b/g/n |
| Frequency band | 2.412-2.472 GHz |
| Maximum transmit power | <= 20 dBm (100 mW) ERP |
| Antenna | Internal PCB antenna (ESP32-S3 module) |

---

## 5. Key components

| Block | Component |
| --- | --- |
| Microcontroller | Espressif ESP32-S3-WROOM-1-N16R2 (dual-core, Wi-Fi 2.4 GHz, 16 MB Flash, 2 MB PSRAM) |
| Metering IC | Analog Devices ADE7953 (single-phase, dual-channel, 24-bit), SPI at 2 MHz with data-ready IRQ |
| Metering IC clock | 3.58 MHz active SMD oscillator (RO03579043) |
| Analog multiplexer | NXP 74HC4067PW (16-channel, TSSOP-24) |
| Voltage divider | 1000:1 (1 MOhm / 1 kOhm) for AC line reference |
| Voltage transformer | ZMPT107-1 (full galvanic isolation between mains and measurement circuit) |
| Power supply | HLK-PM03 AC/DC (100-240 V AC -> 3.3 V DC, 1 A max) |

---

## 6. CT interface

The board accepts **voltage-output** current transformers (no on-board burden resistors):

| Parameter | Value |
| --- | --- |
| CT type | Voltage-output (333 mV) split-core, clamp-on |
| Maximum CT output | 333 mV nominal (<= 0.5 V absolute at the secondary) |
| Connector | 16 x PJ-3200 3.5 mm stereo jack (1 direct + 15 multiplexed) |
| Input conditioning | Low-pass filter 1 kOhm / 33 nF; 1 MOhm pulldown to guarantee 0 V with no CT connected |
| Orientation | Auto-detected in firmware (clip direction does not matter) |

CTs are not part of the board; any voltage-output CT within the input range works. Non-standard CTs require a one-time calibration step in the web interface (see [Setup §4.6](../manual/02-setup.md#46-ct-calibration-only-if-using-non-standard-cts)).

---

## 7. Firmware

| Aspect | Value |
| --- | --- |
| Language | C++ |
| Framework | Arduino 3.x (PlatformIO) |
| RTOS | FreeRTOS (task-based architecture) |
| License | GPL-3.0 |

### 7.1 Features

- Energy monitoring per channel (real-time power, RMS voltage and current, power factor, frequency, kWh accumulation)
- Web interface for configuration, monitoring, and integration setup
- Token-based authentication with password protection
- Captive portal for first-time Wi-Fi setup; mDNS service discovery (`energyme.local`)
- Over-the-air firmware updates with MD5 verification and rollback
- Crash recovery with safe-mode operation
- 10+ years of hourly local data logging with CSV export
- Per-channel waveform capture (voltage and current) from the web UI

---

## 8. Communication and integration

| Protocol | Notes |
| --- | --- |
| **REST API** | Full Swagger-documented API for data and configuration ([swagger.yaml](../source/resources/swagger.yaml)) |
| **MQTT** | Dual-client: AWS IoT Core + user-defined broker with TLS and authentication |
| **Modbus TCP** | Industrial protocol for SCADA / PLC integration |
| **InfluxDB** | Native client, v1.x and v2.x, with TLS and buffering |
| **mDNS** | Service discovery (`energyme.local`) |
| **Home Assistant** | Dedicated custom integration: [homeassistant-energyme](https://github.com/jibrilsharafi/homeassistant-energyme) |

---

## 9. References

- [Hardware reference and circuit design](README.md)
- [PCB design files](pcb/)
- [Firmware source](../source/README.md)
- [Setup & Usage Guide](../manual/README.md)
