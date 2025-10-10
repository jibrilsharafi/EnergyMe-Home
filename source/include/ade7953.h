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
#include "pins.h" // For the voltage divider

// SPI
#define ADE7953_SPI_FREQUENCY 2000000 // The maximum SPI frequency for the ADE7953 is 2MHz
#define ADE7953_SPI_MUTEX_TIMEOUT_MS 100
#define ADE7953_SPI_OPERATION_MUTEX_TIMEOUT_MS 200 // Longer than normal SPI since this accounts also for validation

// Tasks
#define ADE7953_METER_READING_TASK_NAME "ade7953_task"
#define ADE7953_METER_READING_TASK_STACK_SIZE (12 * 1024) // Fine, around 5 kB usage. Increased since we use PSRAM
#define ADE7953_METER_READING_TASK_PRIORITY 5

#define ADE7953_ENERGY_SAVE_TASK_NAME "energy_save_task"
#define ADE7953_ENERGY_SAVE_TASK_STACK_SIZE (5 * 1024) // Around 4.5 kB usage
#define ADE7953_ENERGY_SAVE_TASK_PRIORITY 1

#define ADE7953_HOURLY_CSV_SAVE_TASK_NAME "hourly_csv_task"
#define ADE7953_HOURLY_CSV_SAVE_TASK_STACK_SIZE (6 * 1024) // No more than 5 kB. A bit larger for safety
#define ADE7953_HOURLY_CSV_SAVE_TASK_PRIORITY 1

// ENERGY_SAVING
#define SAVE_ENERGY_INTERVAL (15 * 60 * 1000) // Time between each energy save to preferences. Do not increase the frequency to avoid wearing the flash memory. In any case, this is part of the requirement. The other part is ENERGY_SAVE_THRESHOLD 
#define ENERGY_CSV_PREFIX "/energy"
#define DAILY_ENERGY_CSV_HEADER "timestamp,channel,active_imported,active_exported"
#define DAILY_ENERGY_CSV_DIGITS 0 // Since the energy is in Wh, it is useless to go below 1 Wh, and we also save in space usage
#define ENERGY_SAVE_THRESHOLD 100.0f // Threshold for saving energy data (in Wh) and in any case not more frequent than SAVE_ENERGY_INTERVAL

// Interrupt handling
#define ADE7953_INTERRUPT_TIMEOUT_MS 1000ULL // If exceed this plus sample time, something is wrong as we are not receiving the interrupt

// Setup
#define ADE7953_RESET_LOW_DURATION 200 // The duration for the reset pin to be low (minimum is way lower, but this is a safe value)
#define ADE7953_MAX_VERIFY_COMMUNICATION_ATTEMPTS 5
#define ADE7953_VERIFY_COMMUNICATION_INTERVAL 500

// Default values for ADE7953 registers
#define UNLOCK_OPTIMUM_REGISTER_VALUE 0xAD // Register to write to unlock the optimum register
#define UNLOCK_OPTIMUM_REGISTER 0x00FE // Value to write to unlock the optimum register
#define DEFAULT_OPTIMUM_REGISTER 0x0030 // Default value for the optimum register
#define DEFAULT_EXPECTED_AP_NOLOAD_REGISTER 0x00E419 // Default expected value for AP_NOLOAD_32 (used to validate the ADE7953 communication)
#define DEFAULT_NOLOAD_DYNAMIC_RANGE 20000 // Indicates the 1/X dynamic range before the no load feature kicks in. The higher the more sensible, but more prone to noise. Then there will be a formula to compute the register value.
#define DEFAULT_DISNOLOAD_REGISTER 0 // 0x00 0b00000000 (enable all no-load detection)
#define DEFAULT_LCYCMODE_REGISTER 0b01111111 // 0xFF 0b01111111 (enable accumulation mode for all channels, disable read with reset)
#define DEFAULT_PGA_REGISTER 0 // PGA gain 1
#define DEFAULT_CONFIG_REGISTER 0b1000000100001100 // Enable bit 2, bit 3 (line accumulation for PF), 8 (CRC is enabled), and 15 (keep HPF enabled, keep COMM_LOCK disabled)
#define DEFAULT_IRQENA_REGISTER 0b001101000000000000000000 // Enable CYCEND interrupt (bit 18) and Reset (bit 20, mandatory) and CRC change (bit 21) for line cycle end detection
#define MINIMUM_SAMPLE_TIME 200ULL // The settling time of the ADE7953 is 200 ms, so reading faster than this makes little sense

// Constant hardware-fixed values
// Leaving 
#define FULL_SCALE_LSB_FOR_RMS_VALUES 9032007 // Maximum value of RMS registers (24-bit unsigned) - current (channel A and B) and voltage
#define MAXIMUM_ADC_CHANNEL_INPUT 0.5f // Maximum voltage in volts (absolute) for all ADC channels in ADE7953 (both current and voltage)
#define ENERGY_ACCUMULATION_FREQUENCY 206900 // At full input scale, an LSB is added every this frequency to the energy register

// This is a hardcoded value since the voltage divider implemented (in v5 is 990 kOhm to 1 kOhm) yields this volts per LSB constant
// The computation is as follows:
// The maximum value of register VRMS is 9032007 (24-bit unsigned) with full scale inputs (0.5V absolute, 0.3536V rms).
// The voltage divider ratio is 1000/(990000+1000) =0.001009
// The maximum RMS voltage in input is 0.3536 / 0.001009 = 350.4 V
// The LSB per volt is therefore 9032007 / 350.4 = 25779
// For embedded systems, multiplications are better than divisions, so we use a float constant which is VOLT_PER_LSB = 1 / 25779
#define VOLT_PER_LSB 0.0000387922f
#define CYCLES_PER_SECOND 50 // 50Hz mains frequency
#define POWER_FACTOR_CONVERSION_FACTOR 0.00003052f // PF/LSB computed as 1.0f / 32768.0f (from ADE7953 datasheet)
#define ANGLE_CONVERSION_FACTOR 0.0807f // 0.0807 °/LSB computed as 360.0f * 50.0f / 223000.0f 
#define GRID_FREQUENCY_CONVERSION_FACTOR 223750.0f // Clock of the period measurement, in Hz. To be multiplied by the register value of 0x10E
#define DEFAULT_FALLBACK_FREQUENCY 50 // Most of the world is 50 Hz


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
#define ENERGY_ACTIVE_IMP_KEY "ch%u_actImp"    // Format: ch17_actImp (11 chars)
#define ENERGY_ACTIVE_EXP_KEY "ch%u_actExp"    // Format: ch17_actExp (11 chars)
#define ENERGY_REACTIVE_IMP_KEY "ch%u_reactImp" // Format: ch17_reactImp (13 chars)
#define ENERGY_REACTIVE_EXP_KEY "ch%u_reactExp" // Format: ch17_reactExp (13 chars)
#define ENERGY_APPARENT_KEY "ch%u_apparent"   // Format: ch17_apparent (13 chars)

// Default configuration values
#define DEFAULT_SAMPLE_TIME 200ULL // Will be converted to integer line cycles (so at 50Hz, 200ms = 10 cycles)
#define DEFAULT_CONFIG_AV_GAIN 0x400000
#define DEFAULT_CONFIG_AI_GAIN 0x400000
#define DEFAULT_CONFIG_BI_GAIN 0x400000
#define DEFAULT_CONFIG_AIRMS_OS 0
#define DEFAULT_CONFIG_BIRMS_OS 0
#define DEFAULT_CONFIG_AW_GAIN 0x400000
#define DEFAULT_CONFIG_BW_GAIN 0x400000
#define DEFAULT_CONFIG_AWATT_OS 0
#define DEFAULT_CONFIG_BWATT_OS 0
#define DEFAULT_CONFIG_AVAR_GAIN 0x400000
#define DEFAULT_CONFIG_BVAR_GAIN 0x400000
#define DEFAULT_CONFIG_AVAR_OS 0
#define DEFAULT_CONFIG_BVAR_OS 0
#define DEFAULT_CONFIG_AVA_GAIN 0x400000
#define DEFAULT_CONFIG_BVA_GAIN 0x400000
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

// Rounding values
#define VOLTAGE_DECIMALS 1
#define CURRENT_DECIMALS 3
#define POWER_DECIMALS 1
#define POWER_FACTOR_DECIMALS 3
#define ENERGY_DECIMALS 1

// Guardrails and thresholds
#define MAXIMUM_POWER_FACTOR_CLAMP 1.10f // Values above 1 but below this are still accepted (rounding errors and similar). I noticed I still had a lot of spurious readings with PF around 1.06-1.07 (mainly close to fridge activations, probably due to the compressor)
#define MINIMUM_CURRENT_THREE_PHASE_APPROXIMATION_NO_LOAD 0.01f // The minimum current value for the three-phase approximation to be used as the no-load feature cannot be used
#define MINIMUM_POWER_FACTOR 0.10f // Measuring such low power factors is virtually impossible with such CTs
#define ADE7953_MIN_LINECYC 10UL // Below this the readings are unstable (200 ms)
#define ADE7953_MAX_LINECYC 1000UL // Above this too much time passes (20 seconds)
#define INVALID_SPI_READ_WRITE 0xDEADDEAD // Custom, used to indicate an invalid SPI read/write operation

// ADE7953 Smart Failure Detection
#define ADE7953_MAX_FAILURES_BEFORE_RESTART 100
#define ADE7953_FAILURE_RESET_TIMEOUT_MS (1 * 60 * 1000)

// ADE7953 Critical Failure Detection (missed interrupts)
#ifdef ENV_DEV
#define ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT (10 * 5) // 5x higher limit in dev environment
#else
#define ADE7953_MAX_CRITICAL_FAILURES_BEFORE_REBOOT 10
#endif
#define ADE7953_CRITICAL_FAILURE_RESET_TIMEOUT_MS (5 * 60 * 1000) // Reset counter after 5 minutes

// Check for incorrect readings
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_ABSOLUTE 100.0f // Absolute difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded
#define MAXIMUM_CURRENT_VOLTAGE_DIFFERENCE_RELATIVE 0.20f // Relative difference between Vrms*Irms and the apparent power (computed from the energy registers) before the reading is discarded

// Channel Preferences Keys
#define CHANNEL_ACTIVE_KEY "active_%u" // Format: active_0 (9 chars)
#define CHANNEL_REVERSE_KEY "reverse_%u" // Format: reverse_0 (10 chars)
#define CHANNEL_LABEL_KEY "label_%u" // Format: label_0 (8 chars)
#define CHANNEL_PHASE_KEY "phase_%u" // Format: phase_0 (9 chars)

// CT Specification keys
#define CHANNEL_CT_CURRENT_RATING_KEY "ct_current_%u" // Format: ct_current_0 (12 chars)
#define CHANNEL_CT_VOLTAGE_OUTPUT_KEY "ct_voltage_%u" // Format: ct_voltage_0 (12 chars)
#define CHANNEL_CT_SCALING_FRACTION_KEY "ct_scaling_%u" // Format: ct_scaling_0 (12 chars)

// Default channel values
#define DEFAULT_CHANNEL_ACTIVE false
#define DEFAULT_CHANNEL_0_ACTIVE true // Channel 0 must always be active
#define DEFAULT_CHANNEL_REVERSE false
#define DEFAULT_CHANNEL_PHASE PHASE_1
#define DEFAULT_CHANNEL_LABEL_FORMAT "Channel %u"

// CT Specification defaults
#define DEFAULT_CT_CURRENT_RATING_CHANNEL_0 50.0f   // 50A for channel 0 only as it is "standard" in EnergyMe Home
#define DEFAULT_CT_CURRENT_RATING 30.0f   // 30A
#define DEFAULT_CT_VOLTAGE_OUTPUT 0.333f  // 333mV
#define DEFAULT_CT_SCALING_FRACTION 0.0f  // No scaling by default

#define BIT_8 8
#define BIT_16 16
#define BIT_24 24
#define BIT_32 32

#define INVALID_CHANNEL 255 // Invalid channel identifier, used to indicate no active channel

// Enumeration for different types of ADE7953 interrupts
enum class Ade7953InterruptType {
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

inline const char* ADE7953_CHANNEL_TO_STRING(Ade7953Channel channel) {
    switch (channel) {
        case Ade7953Channel::A: return "A";
        case Ade7953Channel::B: return "B";
        default: return "Unknown";
    }
}

// We don't have an enum for 17 channels since having them as unsigned int is more flexible

enum class MeasurementType{
    VOLTAGE,
    CURRENT,
    ACTIVE_POWER,
    REACTIVE_POWER,
    APPARENT_POWER,
    POWER_FACTOR,
};

inline const char* MEASUREMENT_TYPE_TO_STRING(MeasurementType type) {
    switch (type) {
        case MeasurementType::VOLTAGE: return "Voltage";
        case MeasurementType::CURRENT: return "Current";
        case MeasurementType::ACTIVE_POWER: return "Active Power";
        case MeasurementType::REACTIVE_POWER: return "Reactive Power";
        case MeasurementType::APPARENT_POWER: return "Apparent Power";
        case MeasurementType::POWER_FACTOR: return "Power Factor";
        default: return "Unknown";
    }
}

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

struct CtSpecification
{
  float currentRating;                    // e.g., 30.0 for 30A CT
  float voltageOutput;                    // e.g., 0.333 for 333mV or 1.0 for 1V  
  float scalingFraction;                  // -0.5 to +0.5 for ±50% adjustment
  
  // Computed at runtime - no need to store these in Preferences
  float aLsb;
  // float wLsb;
  // float varLsb;
  // float vaLsb;
  float whLsb;
  float varhLsb;
  float vahLsb;

  CtSpecification()
    : currentRating(DEFAULT_CT_CURRENT_RATING),
      voltageOutput(DEFAULT_CT_VOLTAGE_OUTPUT),
      scalingFraction(DEFAULT_CT_SCALING_FRACTION),
      aLsb(1.0f), 
      // wLsb(1.0f), varLsb(1.0f), vaLsb(1.0f),
      whLsb(1.0f), varhLsb(1.0f), vahLsb(1.0f) {}
};

struct ChannelData
{
  uint8_t index;
  bool active;
  bool reverse;
  char label[NAME_BUFFER_SIZE];
  Phase phase;
  CtSpecification ctSpecification;

  ChannelData()
    : index(0), 
      active(false), 
      reverse(false), 
      phase(PHASE_1), 
      ctSpecification(CtSpecification()) {
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
        uint8_t ssPin,
        uint8_t sckPin,
        uint8_t misoPin,
        uint8_t mosiPin,
        uint8_t resetPin,
        uint8_t interruptPin);
    bool _initializeAde7953(bool &retFlag);
    void stop();


    // Register operations
    /**
     * Reads the value from a register in the ADE7953 energy meter.
     * 
     * @param registerAddress The address of the register to read from. Expected range: 0 to 65535
     * @param numBits The number of bits to read from the register. Expected values: 8, 16, 24 or 32.
     * @param isSignedData Flag indicating whether the data is signed (true) or unsigned (false).
     * @param isVerificationRequired Flag indicating whether to verify the last communication.
     * @return The value read from the register.
     */
    int32_t readRegister(uint16_t registerAddress, uint8_t nBits, bool signedData, bool isVerificationRequired = true);
    /**
     * Writes data to a register in the ADE7953 energy meter.
     * 
     * @param registerAddress The address of the register to write to. (16-bit value)
     * @param nBits The number of bits in the register. (8, 16, 24, or 32)
     * @param data The data to write to the register. (nBits-bit value)
     * @param isVerificationRequired Flag indicating whether to verify the last communication.
     */
    void writeRegister(uint16_t registerAddress, uint8_t nBits, int32_t data, bool isVerificationRequired = true);

    // Task control
    void pauseTasks();
    void resumeTasks();

    // Configuration management
    void getConfiguration(Ade7953Configuration &config);
    bool setConfiguration(const Ade7953Configuration &config);
    void resetConfiguration();

    // Configuration management - JSON operations
    void getConfigurationAsJson(JsonDocument &jsonDocument);
    bool setConfigurationFromJson(const JsonDocument &jsonDocument, bool partial = false);
    void configurationToJson(const Ade7953Configuration &config, JsonDocument &jsonDocument);
    bool configurationFromJson(const JsonDocument &jsonDocument, Ade7953Configuration &config, bool partial = false);

    // Sample time management
    uint64_t getSampleTime();
    bool setSampleTime(uint64_t sampleTime);

    // Channel data management
    bool isChannelActive(uint8_t channelIndex);
    bool hasChannelValidMeasurements(uint8_t channelIndex);
    void getChannelLabel(uint8_t channelIndex, char* buffer, size_t bufferSize); // No need for bool return, fallback is the default constructor value if getChannelData failed
    bool getChannelData(ChannelData &channelData, uint8_t channelIndex);
    bool setChannelData(const ChannelData &channelData, uint8_t channelIndex);
    void resetChannelData(uint8_t channelIndex);

    // Channel data management - JSON operations
    bool getChannelDataAsJson(JsonDocument &jsonDocument, uint8_t channelIndex);
    bool getAllChannelDataAsJson(JsonDocument &jsonDocument);
    bool setChannelDataFromJson(const JsonDocument &jsonDocument, bool partial = false);
    void channelDataToJson(const ChannelData &channelData, JsonDocument &jsonDocument);
    void channelDataFromJson(const JsonDocument &jsonDocument, ChannelData &channelData, bool partial = false);

    // Energy data management
    void resetEnergyValues();
    bool setEnergyValues(
        uint8_t channelIndex,
        float activeEnergyImported,
        float activeEnergyExported,
        float reactiveEnergyImported,
        float reactiveEnergyExported,
        float apparentEnergy
    );

    // Data output
    bool singleMeterValuesToJson(JsonDocument &jsonDocument, uint8_t channelIndex);
    bool fullMeterValuesToJson(JsonDocument &jsonDocument);
    bool getMeterValues(MeterValues &meterValues, uint8_t channelIndex);

    // Aggregated power calculations 
    float getAggregatedActivePower(bool includeChannel0 = true);
    float getAggregatedReactivePower(bool includeChannel0 = true);
    float getAggregatedApparentPower(bool includeChannel0 = true);
    float getAggregatedPowerFactor(bool includeChannel0 = true);

    // Grid frequency
    float getGridFrequency();

    // Task information
    TaskInfo getMeterReadingTaskInfo();
    TaskInfo getEnergySaveTaskInfo();
    TaskInfo getHourlyCsvTaskInfo();
};