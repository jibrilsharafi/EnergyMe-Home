# EnergyMe Home: Datasheet

Technical specifications for *EnergyMe Home*. For installation and use, see the [Installation Manual](../manual/README.md). For regulatory information, see [Safety & Compliance](../manual/safety-and-compliance.md).

---

## 1. Identification

| | |
| --- | --- |
| **Product** | EnergyMe Home |
| **Product code** | EM-HOME-1.0-KIT-STANDARD |
| **Hardware revision** | v6.1 |
| **Open-source certification** | OSHWA `IT000025` |
| **Manufacturer** | EnergyMe S.r.l. — Milano, Italy |

---

## 2. Mechanical

| Parameter | Value |
| --- | --- |
| Form factor | DIN-rail mount, EN 60715 / DIN 43880 |
| DIN modules occupied | 3 |
| Dimensions (W × H × D) | ~53 × 90 × 65 mm |
| Weight | 110 g |
| Case material | ABS, light grey |
| IP rating | IP20 (indoor closed panel only) |
| Mounting | Spring-clip onto DIN rail |
| Power terminals | Screw terminals, internal, with removable cover |
| CT inputs | 16 × 3.5 mm stereo jack (PJ-3133-5A) |

### 2.1 Current transformers (supplied)

| Parameter | Value |
| --- | --- |
| Type | Split-core, clamp-on |
| Rated current | 30 A |
| Output | 0.333 V at rated current (voltage output, burden integrated) |
| Cable length | 1 m |
| Connector | 3.5 mm stereo jack |
| Dimensions | 41 × 27 × 24 mm |
| Weight | 50 g |

Higher-rated CTs (75 A, 150 A) are available on request for high-current circuits. See [Setup §4.6](../manual/02-setup.md#46-ct-calibration-only-if-using-non-standard-cts) for calibration of non-standard CTs.

---

## 3. Electrical

| Parameter | Value |
| --- | --- |
| Supply voltage | 100–240 V AC |
| Supply frequency | 50 / 60 Hz |
| Power consumption | < 2 W typical |
| Safety class | Class II (double insulation) |
| Internal fuse | Fast-blow, 500 mA |
| Galvanic isolation (voltage measurement) | Yes, via ZMPT107-1 voltage transformer |
| Number of measurement channels | 16 (1 direct + 15 multiplexed) |
| Channel numbering | 0 (main) … 15 (branches) |
| Voltage measurement range | Single-phase, nominal 100–240 V AC |
| Current measurement | Via external CT, up to CT-rated current |
| CT input safe range | ≤ 0.5 V at the CT secondary |
| Measurement accuracy | < 1 % typical with supplied 30 A CTs (factory-calibrated) |
| Energy accumulation | kWh, per channel, with CSV logging |

---

## 4. Radio

| Parameter | Value |
| --- | --- |
| Wireless standard | Wi-Fi IEEE 802.11 b/g/n |
| Frequency band | 2.412 – 2.472 GHz |
| Maximum transmit power | ≤ 20 dBm (100 mW) ERP |
| Antenna | Internal PCB antenna |

The device is configured at the factory for the European frequency range. For use outside the EU, verify compliance with local radio regulations before powering the device.

---

## 5. Hardware

| Block | Component |
| --- | --- |
| Microcontroller | Espressif ESP32-S3-WROOM-1-N16R2 (dual-core, Wi-Fi 2.4 GHz, 16 MB Flash, 2 MB PSRAM) |
| Metering IC | Analog Devices ADE7953 (single-phase, dual-channel) |
| Metering IC clock | 3.58 MHz crystal oscillator (S1C35800ZWJAC) |
| Analog multiplexer | NXP 74HC4067PW (16 channel, TSSOP-24) |
| Voltage divider | 1000:1 (1 MΩ / 1 kΩ) for AC line reference |
| Voltage transformer | ZMPT107-1 (full galvanic isolation between mains and measurement circuit) |
| Power supply | HLK-PM03 AC/DC (100–240 V AC → 3.3 V DC, 1 A max) |
| Enclosure | 3M XTS Modulbox XTS, DIN-rail compatible |

### 5.1 PCB

| Parameter | Value |
| --- | --- |
| PCB layers | 4 |
| PCB dimensions | 87 × 50 mm |
| PCB height with components | ~15 mm |

Full PCB design files (schematics, gerber, BOM, pick-and-place) are available under [`hardware/pcb/`](pcb/).

---

## 6. Environmental

| Parameter | Operating | Storage |
| --- | --- | --- |
| Temperature | 0 °C to +50 °C | -10 °C to +60 °C |
| Relative humidity | ≤ 80 %, non-condensing | ≤ 80 %, non-condensing |
| Altitude | ≤ 2000 m | — |
| Pollution degree | 2 | — |
| Installation environment | Indoor, closed electrical panel | Dry, protected from sunlight |

---

## 7. Software

| Aspect | Value |
| --- | --- |
| Firmware language | C++ |
| Framework | Arduino 3.x (PlatformIO) |
| RTOS | FreeRTOS (task-based architecture) |
| License (firmware) | GPL-3.0 |
| License (hardware) | CERN-OHL-S v2 |

### 7.1 Features

- Energy monitoring per channel (real-time power, RMS voltage and current, power factor, frequency, kWh accumulation)
- Web interface for configuration, monitoring, and integration setup
- Token-based authentication with password protection
- Captive portal for first-time Wi-Fi setup; mDNS service discovery (`energyme.local`)
- Over-the-air firmware updates with MD5 verification and rollback
- Crash recovery with safe-mode operation
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

## 9. Package contents

| # | Item | Qty |
| --- | --- | --- |
| 1 | EnergyMe Home device, pre-assembled, with L/N wires pre-connected | 1 |
| 2 | CT clamp, 30 A, 1 m cable, 3.5 mm jack | 4 |
| 3 | "Let's get started" sticker (QR code, inside the lid) | 1 |
| 4 | Declaration of Conformity (printed or via QR) | 1 |

Additional CTs are available as an expansion kit (any combination of 30 A, 75 A, or 150 A clamps, up to 15 branch channels).

---

## 10. Compliance

The device complies with the following EU directives. See the [Declaration of Conformity](../manual/safety-and-compliance.md#3-declaration-of-conformity) for the full list of harmonized standards applied.

- **2014/35/EU** — Low Voltage Directive (LVD)
- **2014/30/EU** — Electromagnetic Compatibility (EMC)
- **2014/53/EU** — Radio Equipment Directive (RED)
- **2011/65/EU** — RoHS, as amended by **2015/863**
- **2012/19/EU** — WEEE
- **Regulation (EU) 2023/988** — General Product Safety Regulation (GPSR)

---

## 11. References

- [Installation Manual](../manual/README.md)
- [Safety & Compliance Information](../manual/safety-and-compliance.md)
- [Hardware reference](README.md)
- [PCB design files](pcb/)
- [Firmware source](../source/README.md)
- [Product page](https://www.energyme.net/product-home-en)
