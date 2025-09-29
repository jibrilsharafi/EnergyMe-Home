# EnergyMe-Home Hardware Documentation

This document provides detailed hardware specifications and technical information for the EnergyMe-Home PCB design. For general project information, software features, and integrations, see the [main README](../README.md).

## Hardware Specifications

**Board Information:**

- **Hardware Revision:** v5
- **PCB Layers:** 4 layers
- **Board Dimensions:** 87 mm x 50 mm. Height with components around 15 mm
- **Power Consumption:** ~100 mA typical (< 1W AC consumption)

**Key Components:**

- **Main Microcontroller:** ESP32-S3 (dual-core, WiFi 2.4 GHz, 16MB Flash, 2MB PSRAM)
- **Energy Measurement IC:** Analog Devices ADE7953 (single-phase)
- **Analog Multiplexer:** 74HC4067PW (16-channel)
- **CT Inputs:** 17 total (1 direct + 16 multiplexed via 3.5mm jacks)
- **Voltage Reference:** 1000:1 divider (1 MΩ / 1kΩ)
- **Power Supply:** Onboard AC/DC converter (100-240 Vac to 3.3 Vdc, 1A max)

## Circuit Design Overview

### 1. Power Supply Unit

Discrete AC/DC converter providing 3.3V DC from universal AC input (100-240 VAC, 50-60 Hz).

![Power Supply](https://image.easyeda.com/oshwhub/pullImage/fddd17b65fa04d2abfbcce1412394c06.png)

### 2. Microcontroller (ESP32-S3)

Central processing unit managing all digital logic, SPI communication with ADE7953, multiplexer control, and wireless connectivity.

![ESP32-S3](https://image.easyeda.com/oshwhub/pullImage/dea4a95df7204d5f9eb930a6aa5e4354.png)

### 3. Energy Measurement (ADE7953)

High-precision energy metering IC with dual current channels:

- **Channel A:** Direct CT input for main circuit monitoring
- **Voltage Input:** AC mains reference via 1000:1 voltage divider
- **Channel B:** Multiplexed input from 16 branch circuits

![ADE7953](https://image.easyeda.com/oshwhub/pullImage/7b4b21cd9ede44e0ab6cbf68409c70e8.png)

### 4. Analog Multiplexing

Single 74HC4067PW multiplexer routes one of 16 CT signals to ADE7953 Channel B. ESP32-S3 controls select lines (S0-S3) for sequential measurement.

![Multiplexer](https://image.easyeda.com/oshwhub/pullImage/9f0c683d254a458f843b96fa37b7b6af.png)

### 5. CT Interface

All 17 CT inputs use 3.5mm stereo jacks with direct connection through low-pass filters (1 kΩ / 33 nF). **Maximum CT output: 333 mV**.

![CT Interface](https://image.easyeda.com/oshwhub/pullImage/c47671faf1c3473cab564f7056ce818e.png)

## Design Files & Resources

- **EasyEDA Project:** [Multiple Channel Energy Meter](https://oshwlab.com/jabrillo/multiple-channel-energy-meter)
- **Schematics:** Available in `Schematics/` directory
- **BOM:** `Schematics/BOM.csv`
- **Gerber Files:** Multiple board variants in `Schematics/`
- **Component Datasheets:** Available in `Components/` directory

## Safety & Assembly Notes

⚠️ **ELECTRICAL SAFETY WARNING**

This device interfaces with AC mains voltage. Construction and installation must only be performed by qualified individuals with proper electrical safety knowledge. Improper handling can result in serious injury or death.

**Assembly Requirements:**

- SMD soldering skills and appropriate tools (hot air station, fine-tip iron)
- Use voltage-output CTs rated for 333mV maximum output
- Follow proper electrical isolation and safety procedures

## Hardware Images

**Bare PCB:**

![Bare PCB](https://image.easyeda.com/oshwhub/pullImage/06165d5bc11443768b389e65da0b750a.jpg)

**Assembled Board:**

![Assembled PCB](https://image.easyeda.com/oshwhub/pullImage/54320e42416844e980a43cc4ebc63200.jpg)

**Enclosure:**

![Enclosure](https://image.easyeda.com/oshwhub/pullImage/08c4acf57e38402a88879df4a71796b8.jpg)

**Installation:**

![Installation](https://image.easyeda.com/oshwhub/pullImage/8211ebaea22a4962865c3cb88da6bc68.jpg)
