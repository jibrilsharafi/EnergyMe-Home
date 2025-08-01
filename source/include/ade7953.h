#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Ticker.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "ade7953registers.h"
#include "multiplexer.h"
#include "customtime.h"
#include "mqtt.h"
#include "binaries.h"
#include "constants.h"
#include "structs.h"
#include "utils.h"

// Saving date
#define SAVE_ENERGY_INTERVAL (6 * 60 * 1000) // Time between each energy save to the SPIFFS. Do not increase the frequency to avoid wearing the flash memory 

#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100 // Timeout for acquiring SPI mutex to prevent deadlocks

// Task
#define ADE7953_METER_READING_TASK_NAME "ade7953_task" // The name of the ADE7953 task
#define ADE7953_METER_READING_TASK_STACK_SIZE (8 * 1024) // The stack size for the ADE7953 task
#define ADE7953_METER_READING_TASK_PRIORITY 2 // The priority for the ADE7953 task

// Interrupt handling
#define ADE7953_INTERRUPT_TIMEOUT_MS 1000 // Timeout for waiting on interrupt semaphore (in ms)

// Setup
#define ADE7953_RESET_LOW_DURATION 200 // The duration for the reset pin to be low
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500 

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 
#define DEFAULT_X_NOLOAD_REGISTER 0x00E419 // Value for AP_NOLOAD_32, VAR_NOLOAD_32 and VA_NOLOAD_32 register. Represents a scale of 10000:1, meaning that the no-load threshold is 0.01% of the full-scale value
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (enable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0b01111111 // 0xFF 0b01111111 (enable accumulation mode for all channels, disable read with reset)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000100001100 // Enable bit 2, bit 3 (line accumulation for PF), 8 (CRC is enabled), and 15 (keep HPF enabled, keep COMM_LOCK disabled)
#define DEFAULT_GAIN 4194304 // 0x400000 (default gain for the ADE7953)
#define DEFAULT_OFFSET 0 // 0x000000 (default offset for the ADE7953)
#define DEFAULT_PHCAL 10 // 0.02°/LSB, indicating a phase calibration of 0.2° which is minimum needed for CTs
#define DEFAULT_IRQENA_REGISTER 0b001101000000000000000000 // Enable CYCEND interrupt (bit 18) and Reset (bit 20, mandatory) for line cycle end detection
#define MINIMUM_SAMPLE_TIME 200

// IRQSTATA / RSTIRQSTATA Register Bit Positions (Table 23, ADE7953 Datasheet)
#define IRQSTATA_AEHFA_BIT         0  // Active energy register half full (Current Channel A)
#define IRQSTATA_VAREHFA_BIT       1  // Reactive energy register half full (Current Channel A)
#define IRQSTATA_VAEHFA_BIT        2  // Apparent energy register half full (Current Channel A)
#define IRQSTATA_AEOFA_BIT         3  // Active energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAREOFA_BIT       4  // Reactive energy register overflow/underflow (Current Channel A)
#define IRQSTATA_VAEOFA_BIT        5  // Apparent energy register overflow/underflow (Current Channel A)
#define IRQSTATA_AP_NOLOADA_BIT    6  // Active power no-load detected (Current Channel A)
#define IRQSTATA_VAR_NOLOADA_BIT   7  // Reactive power no-load detected (Current Channel A)
#define IRQSTATA_VA_NOLOADA_BIT    8  // Apparent power no-load detected (Current Channel A)
#define IRQSTATA_APSIGN_A_BIT      9  // Sign of active energy changed (Current Channel A)
#define IRQSTATA_VARSIGN_A_BIT     10 // Sign of reactive energy changed (Current Channel A)
#define IRQSTATA_ZXTO_IA_BIT       11 // Zero crossing missing on Current Channel A
#define IRQSTATA_ZXIA_BIT          12 // Current Channel A zero crossing detected
#define IRQSTATA_OIA_BIT           13 // Current Channel A overcurrent threshold exceeded
#define IRQSTATA_ZXTO_BIT          14 // Zero crossing missing on voltage channel
#define IRQSTATA_ZXV_BIT           15 // Voltage channel zero crossing detected
#define IRQSTATA_OV_BIT            16 // Voltage peak overvoltage threshold exceeded
#define IRQSTATA_WSMP_BIT          17 // New waveform data acquired
#define IRQSTATA_CYCEND_BIT        18 // End of line cycle accumulation period
#define IRQSTATA_SAG_BIT           19 // Sag event occurred
#define IRQSTATA_RESET_BIT         20 // End of software or hardware reset
#define IRQSTATA_CRC_BIT           21 // Checksum has changed

// Fixed conversion values
#define POWER_FACTOR_CONVERSION_FACTOR 1.0f / 32768.0f // PF/LSB
#define ANGLE_CONVERSION_FACTOR 360.0f * 50.0f / 223000.0f // 0.0807 °/LSB
#define GRID_FREQUENCY_CONVERSION_FACTOR 223750.0f // Clock of the period measurement, in Hz. To be multiplied by the register value of 0x10E

// Validate values
#define VALIDATE_VOLTAGE_MIN 50.0f // Any voltage below this value is discarded
#define VALIDATE_VOLTAGE_MAX 300.0f  // Any voltage above this value is discarded
#define VALIDATE_CURRENT_MIN -300.0f // Any current below this value is discarded
#define VALIDATE_CURRENT_MAX 300.0f // Any current above this value is discarded
#define VALIDATE_POWER_MIN -100000.0f // Any power below this value is discarded
#define VALIDATE_POWER_MAX 100000.0f // Any power above this value is discarded
#define VALIDATE_POWER_FACTOR_MIN -1.0f // Any power factor below this value is discarded
#define VALIDATE_POWER_FACTOR_MAX 1.0f // Any power factor above this value is discarded
#define VALIDATE_GRID_FREQUENCY_MIN 45.0f // Minimum grid frequency in Hz
#define VALIDATE_GRID_FREQUENCY_MAX 65.0f // Maximum grid frequency in Hz

// Guardrails and thresholds
#define MAXIMUM_POWER_FACTOR_CLAMP 1.05f // Values above 1 but below this are still accepted
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.01f // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used
#define MINIMUM_POWER_FACTOR 0.05f // Measuring such low power factors is virtually impossible with such CTs
#define MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE 100 // Threshold to transition to a legitimate zero state for channel 0
#define ADE7953_MIN_LINECYC 10U // The minimum line cycle settable for the ADE7953. Must be unsigned
#define ADE7953_MAX_LINECYC 1000U // The maximum line cycle settable for the ADE7953. Must be unsigned

#define INVALID_SPI_READ_WRITE 0xDEADDEAD // Custom, used to indicate an invalid SPI read/write operation

// ADE7953 Smart Failure Detection
#define ADE7953_MAX_FAILURES_BEFORE_RESTART 100
#define ADE7953_FAILURE_RESET_TIMEOUT_MS (1 * 60 * 1000)

// Check for incorrect readings
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE 100.0f // Absolute difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE 0.20f // Relative difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded

// Preferences keys for ADE7953 configuration
#define PREF_KEY_SAMPLE_TIME "sampleTime"
#define PREF_KEY_A_V_GAIN "aVGain"
#define PREF_KEY_A_I_GAIN "aIGain"
#define PREF_KEY_B_I_GAIN "bIGain"
#define PREF_KEY_A_IRMS_OS "aIRmsOs"
#define PREF_KEY_B_IRMS_OS "bIRmsOs"
#define PREF_KEY_A_W_GAIN "aWGain"
#define PREF_KEY_B_W_GAIN "bWGain"
#define PREF_KEY_A_WATT_OS "aWattOs"
#define PREF_KEY_B_WATT_OS "bWattOs"
#define PREF_KEY_A_VAR_GAIN "aVarGain"
#define PREF_KEY_B_VAR_GAIN "bVarGain"
#define PREF_KEY_A_VAR_OS "aVarOs"
#define PREF_KEY_B_VAR_OS "bVarOs"
#define PREF_KEY_A_VA_GAIN "aVaGain"
#define PREF_KEY_B_VA_GAIN "bVaGain"
#define PREF_KEY_A_VA_OS "aVaOs"
#define PREF_KEY_B_VA_OS "bVaOs"

// Preferences keys for channel configuration (per channel, format: "ch<N>_<property>")
#define PREF_KEY_CHANNEL_ACTIVE_FMT "ch%d_active"
#define PREF_KEY_CHANNEL_REVERSE_FMT "ch%d_reverse"
#define PREF_KEY_CHANNEL_LABEL_FMT "ch%d_label"
#define PREF_KEY_CHANNEL_PHASE_FMT "ch%d_phase"
#define PREF_KEY_CHANNEL_CALIB_LABEL_FMT "ch%d_calibLabel"

#define BIT_8 8
#define BIT_16 16
#define BIT_24 24
#define BIT_32 32

#define INVALID_CHANNEL -1 // Invalid channel identifier, used to indicate no active channel

// Enumeration for different types of ADE7953 interrupts
enum class Ade7953InterruptType {
  NONE,           // No interrupt or unknown
  CYCEND,         // Line cycle end - normal meter reading
  RESET,          // Device reset detected
  CRC_CHANGE,     // CRC register change detected
  OTHER           // Other interrupts (SAG, etc.)
};


enum Phase : unsigned int { // Not a class so that we can directly use it in JSON serialization
    PHASE_1 = 1,
    PHASE_2 = 2,
    PHASE_3 = 3,
};

enum class Ade7953Channel{
    A,
    B,
};

enum class MeasurementType{
    VOLTAGE,
    CURRENT,
    ACTIVE_POWER,
    REACTIVE_POWER,
    APPARENT_POWER,
    POWER_FACTOR,
};



/*
 * Struct to hold the real-time meter values for a specific channel
  * Contains:
  * - voltage: Voltage in Volts
  * - current: Current in Amperes
  * - activePower: Active power in Watts
  * - reactivePower: Reactive power in VAR
  * - apparentPower: Apparent power in VA
  * - powerFactor: Power factor (-1 to 1, where negative values indicate capacitive load while positive values indicate inductive load)
  * - activeEnergyImported: Active energy imported in Wh
  * - activeEnergyExported: Active energy exported in Wh
  * - reactiveEnergyImported: Reactive energy imported in VArh
  * - reactiveEnergyExported: Reactive energy exported in VArh
  * - apparentEnergy: Apparent energy in VAh (only absolute value)
  * - lastUnixTimeMilliseconds: Last time the values were updated in milliseconds since epoch. Useful for absolute time tracking
 */
struct MeterValues
{
  float voltage;
  float current;
  float activePower;
  float reactivePower;
  float apparentPower;
  float powerFactor;
  float activeEnergyImported;
  float activeEnergyExported;
  float reactiveEnergyImported;
  float reactiveEnergyExported;
  float apparentEnergy;
  unsigned long long lastUnixTimeMilliseconds;
  unsigned long long lastMillis;

  MeterValues()
    : voltage(230.0), current(0.0f), activePower(0.0f), reactivePower(0.0f), apparentPower(0.0f), powerFactor(0.0f),
      activeEnergyImported(0.0f), activeEnergyExported(0.0f), reactiveEnergyImported(0.0f), 
      reactiveEnergyExported(0.0f), apparentEnergy(0.0f), lastUnixTimeMilliseconds(0), lastMillis(0) {}
};

struct CalibrationValues
{
  char label[NAME_BUFFER_SIZE];
  float vLsb;
  float aLsb;
  float wLsb;
  float varLsb;
  float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CalibrationValues()
    : vLsb(1.0), aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {
      snprintf(label, sizeof(label), "Calibration");
    }
};


struct ChannelData
{
  int index;
  bool active;
  bool reverse;
  char label[NAME_BUFFER_SIZE];
  Phase phase;
  CalibrationValues calibrationValues;

  ChannelData()
    : index(0), active(false), reverse(false), phase(PHASE_1), calibrationValues(CalibrationValues()) {
      snprintf(label, sizeof(label), "Channel");
    }
};

// Used to track consecutive zero energy readings for channel 0
struct ChannelState { // TODO: what the heck was i thinking with this?
    unsigned long consecutiveZeroCount = 0;
};

namespace Ade7953
{
    bool begin();
    void cleanup();
    void loop();
        
    unsigned int getSampleTime();

    long readRegister(long registerAddress, int nBits, bool signedData, bool isVerificationRequired = true);
    void writeRegister(long registerAddress, int nBits, long data, bool isVerificationRequired = true);

    float getAggregatedActivePower(bool includeChannel0 = true);
    float getAggregatedReactivePower(bool includeChannel0 = true);
    float getAggregatedApparentPower(bool includeChannel0 = true);
    float getAggregatedPowerFactor(bool includeChannel0 = true);

    float getGridFrequency();
    
    void resetEnergyValues();
    bool setEnergyValues(JsonDocument &jsonDocument);
    void saveEnergy();

    void setDefaultConfiguration();
    bool setConfiguration(JsonDocument &jsonDocument);

    void setDefaultCalibrationValues();
    bool setCalibrationValues(JsonDocument &jsonDocument);

    void setDefaultChannelData();
    bool setChannelData(JsonDocument &jsonDocument);
    void channelDataToJson(JsonDocument &jsonDocument);
    
    void singleMeterValuesToJson(JsonDocument &jsonDocument, unsigned int channel);
    void fullMeterValuesToJson(JsonDocument &jsonDocument);

    void printMeterValues(MeterValues* meterValues, ChannelData* channelData);

    MeterValues meterValues[CHANNEL_COUNT];
    ChannelData channelData[CHANNEL_COUNT];    
    
    void pauseMeterReadingTask();
    void resumeMeterReadingTask();
};