#pragma once

#include <AdvancedLogger.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
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

#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100 // Timeout for acquiring SPI mutex to prevent deadlocks

// Task
#define ADE7953_METER_READING_TASK_NAME "ade7953_task" // The name of the ADE7953 task
#define ADE7953_METER_READING_TASK_STACK_SIZE (16 * 1024) // The stack size for the ADE7953 task
#define ADE7953_METER_READING_TASK_PRIORITY 2 // The priority for the ADE7953 task

#define ADE7953_ENERGY_SAVE_TASK_NAME "energy_save_task" // The name of the energy save task
#define ADE7953_ENERGY_SAVE_TASK_STACK_SIZE (4 * 1024) // The stack size for the energy save task
#define ADE7953_ENERGY_SAVE_TASK_PRIORITY 1 // The priority for the energy save task

#define ADE7953_HOURLY_CSV_SAVE_TASK_NAME "hourly_csv_task" // The name of the hourly CSV save task
#define ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE (4 * 1024) // The stack size for the hourly CSV save task
#define ADE7953_HOURLY_CSV_SAVE_TASK_PRIORITY 1 // The priority for the hourly CSV save task

#define ENERGY_SAVE_THRESHOLD 1000.0f // Threshold for saving energy data (in Wh) and in any case not more frequent than every 5 minutes

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

// Constant hardware-fixed values
// This is a hardcoded value since the voltage divider implemented (in v5 is 990 kOhm to 1 kOhm) yields this volts per LSB constant
// The computation is as follows:
// The maximum value of register VRMS is 9032007 (24-bit unsigned) with full scale inputs (0.5V absolute, 0.3536V rms).
// The voltage divider ratio is 1000/(990000+1000) =0.001009
// The maximum RMS voltage in input is 0.3536 / 0.001009 = 350.4 V
// The LSB per volt is therefore 9032007 / 350.4 = 25779
// For embedded systems, multiplications are better than divisions, so we use a float constant which is VOLT_PER_LSB = 1 / LSB_PER_VOLT
#define VOLT_PER_LSB 0.0000387922

// Configuration Preferences Keys
#define CONFIG_SAMPLE_TIME_KEY "sample_time"
#define CONFIG_AV_GAIN_KEY "av_gain"
#define CONFIG_AI_GAIN_KEY "ai_gain"
#define CONFIG_BI_GAIN_KEY "bi_gain"
#define CONFIG_AIRMS_OS_KEY "airms_os"
#define CONFIG_BIRMS_OS_KEY "birms_os"
#define CONFIG_AW_GAIN_KEY "aw_gain"
#define CONFIG_BW_GAIN_KEY "bw_gain"
#define CONFIG_AWATT_OS_KEY "awatt_os"
#define CONFIG_BWATT_OS_KEY "bwatt_os"
#define CONFIG_AVAR_GAIN_KEY "avar_gain"
#define CONFIG_BVAR_GAIN_KEY "bvar_gain"
#define CONFIG_AVAR_OS_KEY "avar_os"
#define CONFIG_BVAR_OS_KEY "bvar_os"
#define CONFIG_AVA_GAIN_KEY "ava_gain"
#define CONFIG_BVA_GAIN_KEY "bva_gain"
#define CONFIG_AVA_OS_KEY "ava_os"
#define CONFIG_BVA_OS_KEY "bva_os"
#define CONFIG_PHCAL_A_KEY "phcal_a"
#define CONFIG_PHCAL_B_KEY "phcal_b"

// Energy Preferences Keys (max 15 chars)
#define ENERGY_ACTIVE_IMP_KEY "ch%d_actImp"    // Format: ch17_actImp (11 chars)
#define ENERGY_ACTIVE_EXP_KEY "ch%d_actExp"    // Format: ch17_actExp (11 chars)
#define ENERGY_REACTIVE_IMP_KEY "ch%d_reactImp" // Format: ch17_reactImp (13 chars)
#define ENERGY_REACTIVE_EXP_KEY "ch%d_reactExp" // Format: ch17_reactExp (13 chars)
#define ENERGY_APPARENT_KEY "ch%d_apparent"   // Format: ch17_apparent (13 chars)

// Saving date
#define SAVE_ENERGY_INTERVAL (5 * 60 * 1000) // Time between each energy save to preferences. Do not increase the frequency to avoid wearing the flash memory 
#define HOURLY_CSV_SAVE_TOLERANCE_MS (2 * 60 * 1000) // Tolerance window around the hour mark for CSV saves (2 minutes) 
#define DAILY_ENERGY_CSV_HEADER "timestamp,channel,label,phase,active_imported,active_exported,reactive_imported,reactive_exported,apparent"
#define DAILY_ENERGY_CSV_DIGITS 1 // Since the energy is in Wh, it is useless to go below 0.1 Wh

// Default configuration values
#define DEFAULT_CONFIG_SAMPLE_TIME 1000
#define DEFAULT_CONFIG_AV_GAIN 0
#define DEFAULT_CONFIG_AI_GAIN 0
#define DEFAULT_CONFIG_BI_GAIN 0
#define DEFAULT_CONFIG_AIRMS_OS 0
#define DEFAULT_CONFIG_BIRMS_OS 0
#define DEFAULT_CONFIG_AW_GAIN 0
#define DEFAULT_CONFIG_BW_GAIN 0
#define DEFAULT_CONFIG_AWATT_OS 0
#define DEFAULT_CONFIG_BWATT_OS 0
#define DEFAULT_CONFIG_AVAR_GAIN 0
#define DEFAULT_CONFIG_BVAR_GAIN 0
#define DEFAULT_CONFIG_AVAR_OS 0
#define DEFAULT_CONFIG_BVAR_OS 0
#define DEFAULT_CONFIG_AVA_GAIN 0
#define DEFAULT_CONFIG_BVA_GAIN 0
#define DEFAULT_CONFIG_AVA_OS 0
#define DEFAULT_CONFIG_BVA_OS 0
#define DEFAULT_CONFIG_PHCAL_A 0
#define DEFAULT_CONFIG_PHCAL_B 0

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
#define VALIDATE_VOLTAGE_MIN 50.0f
#define VALIDATE_VOLTAGE_MAX 300.0f
#define VALIDATE_CURRENT_MIN -300.0f
#define VALIDATE_CURRENT_MAX 300.0f
#define VALIDATE_POWER_MIN -100000.0f
#define VALIDATE_POWER_MAX 100000.0f
#define VALIDATE_POWER_FACTOR_MIN -1.0f
#define VALIDATE_POWER_FACTOR_MAX 1.0f
#define VALIDATE_GRID_FREQUENCY_MIN 45.0f
#define VALIDATE_GRID_FREQUENCY_MAX 65.0f

// Guardrails and thresholds
#define MAXIMUM_POWER_FACTOR_CLAMP 1.05f // Values above 1 but below this are still accepted (rounding errors and similar)
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.01f // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used
#define MINIMUM_POWER_FACTOR 0.05f // Measuring such low power factors is virtually impossible with such CTs
#define MAX_CONSECUTIVE_ZEROS_BEFORE_LEGITIMATE 100 // Threshold to transition to a legitimate zero state for channel 0
#define ADE7953_MIN_LINECYC 10UL // Below this the readings are unstable (200 ms)
#define ADE7953_MAX_LINECYC 1000UL // Above this too much time passes (20 seconds)

#define INVALID_SPI_READ_WRITE 0xDEADDEAD // Custom, used to indicate an invalid SPI read/write operation

// ADE7953 Smart Failure Detection
#define ADE7953_MAX_FAILURES_BEFORE_RESTART 100
#define ADE7953_FAILURE_RESET_TIMEOUT_MS (1 * 60 * 1000)

// Check for incorrect readings
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE 100.0f // Absolute difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE 0.20f // Relative difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded

// Channel Preferences Keys
#define CHANNEL_ACTIVE_KEY "active_%u"
#define CHANNEL_REVERSE_KEY "reverse_%u"
#define CHANNEL_LABEL_KEY "label_%u"
#define CHANNEL_PHASE_KEY "phase_%u"
#define CHANNEL_CALIBRATION_LABEL_KEY "cal_label_%u"

// Default channel values
#define DEFAULT_CHANNEL_ACTIVE false
#define DEFAULT_CHANNEL_0_ACTIVE true // Channel 0 must always be active
#define DEFAULT_CHANNEL_REVERSE false
#define DEFAULT_CHANNEL_PHASE PHASE_1
#define DEFAULT_CHANNEL_0_LABEL "Channel 0"
#define DEFAULT_CHANNEL_LABEL_FORMAT "Channel %u"
#define DEFAULT_CHANNEL_0_CALIBRATION_LABEL "SCT-013-50A-333mV"
#define DEFAULT_CHANNEL_CALIBRATION_LABEL "SCT-013-30A-333mV"

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


enum Phase : uint32_t { // Not a class so that we can directly use it in JSON serialization
    PHASE_1 = 1,
    PHASE_2 = 2,
    PHASE_3 = 3,
};

enum class Ade7953Channel{
    A,
    B,
};

// We don't have an enum for 17 channels since having them as unsigned int is more flexible

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
  uint64_t lastUnixTimeMilliseconds;
  uint64_t lastMillis;

  MeterValues()
    : voltage(230.0), current(0.0f), activePower(0.0f), reactivePower(0.0f), apparentPower(0.0f), powerFactor(0.0f),
      activeEnergyImported(0.0f), activeEnergyExported(0.0f), reactiveEnergyImported(0.0f), 
      reactiveEnergyExported(0.0f), apparentEnergy(0.0f), lastUnixTimeMilliseconds(0), lastMillis(0) {}
};

struct EnergyValues // Simpler structure for optimizing energy saved to storage
{
  float activeEnergyImported;
  float activeEnergyExported;
  float reactiveEnergyImported;
  float reactiveEnergyExported;
  float apparentEnergy;
  uint64_t lastUnixTimeMilliseconds; // Last time the values were updated in milliseconds since epoch

  EnergyValues()
    : activeEnergyImported(0.0f), activeEnergyExported(0.0f), reactiveEnergyImported(0.0f),
      reactiveEnergyExported(0.0f), apparentEnergy(0.0f), lastUnixTimeMilliseconds(0) {}
};

struct CalibrationValues
{
  char label[NAME_BUFFER_SIZE];
  float aLsb;
  float wLsb;
  float varLsb;
  float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CalibrationValues()
    : aLsb(1.0), wLsb(1.0), varLsb(1.0), vaLsb(1.0), whLsb(1.0), varhLsb(1.0), vahLsb(1.0) {
      snprintf(label, sizeof(label), "Calibration");
    }
};

struct ChannelData
{
  int32_t index;
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

// ADE7953 Configuration structure
struct Ade7953Configuration
{
  int32_t aVGain;
  int32_t aIGain;
  int32_t bIGain;
  int32_t aIRmsOs;
  int32_t bIRmsOs;
  int32_t aWGain;
  int32_t bWGain;
  int32_t aWattOs;
  int32_t bWattOs;
  int32_t aVarGain;
  int32_t bVarGain;
  int32_t aVarOs;
  int32_t bVarOs;
  int32_t aVaGain;
  int32_t bVaGain;
  int32_t aVaOs;
  int32_t bVaOs;
  int32_t phCalA;
  int32_t phCalB;

  Ade7953Configuration()
    : aVGain(DEFAULT_CONFIG_AV_GAIN), aIGain(DEFAULT_CONFIG_AI_GAIN), bIGain(DEFAULT_CONFIG_BI_GAIN),
      aIRmsOs(DEFAULT_CONFIG_AIRMS_OS), bIRmsOs(DEFAULT_CONFIG_BIRMS_OS),
      aWGain(DEFAULT_CONFIG_AW_GAIN), bWGain(DEFAULT_CONFIG_BW_GAIN),
      aWattOs(DEFAULT_CONFIG_AWATT_OS), bWattOs(DEFAULT_CONFIG_BWATT_OS),
      aVarGain(DEFAULT_CONFIG_AVAR_GAIN), bVarGain(DEFAULT_CONFIG_BVAR_GAIN),
      aVarOs(DEFAULT_CONFIG_AVAR_OS), bVarOs(DEFAULT_CONFIG_BVAR_OS),
      aVaGain(DEFAULT_CONFIG_AVA_GAIN), bVaGain(DEFAULT_CONFIG_BVA_GAIN),
      aVaOs(DEFAULT_CONFIG_AVA_OS), bVaOs(DEFAULT_CONFIG_BVA_OS),
      phCalA(DEFAULT_CONFIG_PHCAL_A), phCalB(DEFAULT_CONFIG_PHCAL_B) {}
};

namespace Ade7953
{
    // Core lifecycle management
    bool begin(
        uint32_t ssPin,
        uint32_t sckPin,
        uint32_t misoPin,
        uint32_t mosiPin,
        uint32_t resetPin,
        uint32_t interruptPin);
    void stop();

    // Hardware communication (exposed for advanced use)
    int32_t readRegister(int32_t registerAddress, int32_t nBits, bool signedData, bool isVerificationRequired = true);
    void writeRegister(int32_t registerAddress, int32_t nBits, int32_t data, bool isVerificationRequired = true);

    // Task control
    void pauseMeterReadingTask();
    void resumeMeterReadingTask();

    // Channel and meter data access
    bool isChannelActive(uint32_t channelIndex);
    void getChannelData(ChannelData &channelData, uint32_t channelIndex);
    void getMeterValues(MeterValues &meterValues, uint32_t channelIndex);

    // Aggregated power calculations
    float getAggregatedActivePower(bool includeChannel0 = true);
    float getAggregatedReactivePower(bool includeChannel0 = true);
    float getAggregatedApparentPower(bool includeChannel0 = true);
    float getAggregatedPowerFactor(bool includeChannel0 = true);

    // System parameters
    uint32_t getSampleTime();
    bool setSampleTime(uint32_t sampleTime);
    
    float getGridFrequency();

    // Configuration management
    void getConfiguration(Ade7953Configuration &config);
    bool setConfiguration(const Ade7953Configuration &config);
    bool configurationToJson(const Ade7953Configuration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(JsonDocument &jsonDocument, Ade7953Configuration &config, bool partial = false);

    // Calibration management
    void setDefaultCalibrationValues();
    bool setCalibrationValues(JsonDocument &jsonDocument);

    // Channel data management
    void setDefaultChannelData();
    bool setChannelData(JsonDocument &jsonDocument);
    bool setSingleChannelData(uint32_t channelIndex, bool active, bool reverse, const char* label, Phase phase, const char* calibrationLabel);
    void channelDataToJson(JsonDocument &jsonDocument);

    // Energy data management
    void resetEnergyValues();
    bool setEnergyValues(JsonDocument &jsonDocument);

    // Data output and visualization
    void singleMeterValuesToJson(JsonDocument &jsonDocument, uint32_t channel);
    void fullMeterValuesToJson(JsonDocument &jsonDocument);
};