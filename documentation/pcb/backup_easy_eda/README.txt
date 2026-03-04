![homepage.png](https://image.easyeda.com/oshwhub/pullImage/26d75bd30a36437f8a8b44cba028fae0.png)

# Project introduction
EnergyMe-Home is an open-source hardware project for a multi-channel energy monitoring system, designed using EasyEDA. This board forms the core of a system capable of detailed electricity consumption analysis for up to 17 circuits. The design prioritizes flexibility, robust measurement capabilities using the ADE7953 IC, and extensive connectivity options managed by an ESP32-S3 microcontroller. All corresponding open-source firmware, detailed documentation, and further project information can be found on GitHub: [https://github.com/jibrilsharafi/EnergyMe-Home](https://github.com/jibrilsharafi/EnergyMe-Home).

# Project function
This hardware design enables the following core functionalities:

*   **Simultaneous Multi-Circuit Energy Measurement:** The PCB is designed to interface with one direct CT (Current Transformer) input and up to 16 additional CT inputs via an onboard analog multiplexing system, all feeding into the ADE7953 energy measurement IC.
*   **Accurate Electrical Parameter Acquisition:** Through the ADE7953 and supporting circuitry, the board is capable of capturing data for:
    *   Voltage RMS (from a single reference input)
    *   Current RMS for each of the 17 channels
    *   Active, Reactive, and Apparent Power
    *   Power Factor
    *   Accumulated Energy
*   **Flexible Connectivity:** The ESP32-S3 provides WiFi for network communication, enabling the firmware to implement:
    *   A web server for configuration and real-time data display.
    *   REST API for external system integration.
    *   MQTT client for data publishing (e.g., to local brokers or cloud platforms).
    *   InfluxDB client for direct time-series database logging.
    *   Modbus TCP server capabilities.
*   **User Interface &amp; Diagnostics:** Onboard RGB LED for status indication, and UART pins for programming and serial debugging.
*   **External CT Interfacing:** Standard 3.5mm jack connectors are provided for easy and secure connection of common current transformers.

# Project Parameters
*   **Main Microcontroller:** ESP32-S3
*   **Energy Measurement IC:** Analog Devices ADE7953
*   **Multiplexer ICs:** 74HC4067PW (16 channels analog multiplexer)
*   **Number of CT Channels:** 1 Direct (for ADE7953 Channel A Current Input), 16 Multiplexed (for ADE7953 Channel B Current Input)
*   **CT Input Type:** Designed for voltage-output CTs (e.g., 333mV at rated current) via 3.5mm audio jacks.
*   **Voltage Reference Input:** Direct connection via 1 MΩ / 1kΩ voltage divider.
*   **PCB Layers:** 4 layers
*   **Board Dimensions:** 87 mm x 50 mm. Height with components around 15 mm
*   **Power Supply:** Onboard AC/DC converter using the same supply as the voltage reference
*   **Communication Interfaces (Hardware):** SPI (for ADE7953), GPIOs (for multiplexer, LEDs), UART (Programming/Serial).
*   **Connectivity (Wireless):** WiFi 2.4 GHz via ESP32-S3.

# Principle analysis (Hardware description)
The EnergyMe-Home hardware is designed to accurately measure and process energy data from multiple circuits. The design can be broken down into the following key sections:

1.  **Power Supply Unit (PSU):**
    *   Discrete component that converts universal voltage (100-240 Vac | 50-60 Hz) to 3.3 Vdc (with up to 1 A output) required by the ESP32-S3, ADE7953, and other logic components. Standard consumption hovers around 100 mA, so AC consumption is            
How to use：

At editor, open the document via: Top menu - File - Open - EasyEDA... , and select the json file, then open it at the editor, you can save it into a project.


如何使用：

打开编辑器，通过：顶部菜单 - 文件 - 打开 - 立创EDA... ，选择 json 文件打开在编辑器，你可以保存文档进工程里面。