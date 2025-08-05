#pragma once

// Hardware revision: EnergyMe - Home v5 (02-12-2024)

// RGB LED pins
#define LED_RED_PIN 39
#define LED_GREEN_PIN 40
#define LED_BLUE_PIN 38

// General use button pin (used also to set the chip to flash mode)
#define BUTTON_GPIO0_PIN 0

// Multiplexer pins
#define MULTIPLEXER_S0_PIN 10
#define MULTIPLEXER_S1_PIN 11
#define MULTIPLEXER_S2_PIN 3
#define MULTIPLEXER_S3_PIN 9

// ADE7953 pins
#define ADE7953_SS_PIN 48
#define ADE7953_SCK_PIN 36
#define ADE7953_MISO_PIN 35
#define ADE7953_MOSI_PIN 45
#define ADE7953_RESET_PIN 21
#define ADE7953_INTERRUPT_PIN 37

// Voltage divider resistors to directly feed the scaled down mains voltage to the ADE7953
#define VOLTAGE_DIVIDER_R1 990000.0f // Actually 3x330 kOhm resistors in series
#define VOLTAGE_DIVIDER_R2 1000.0f // 1 kOhm resistor