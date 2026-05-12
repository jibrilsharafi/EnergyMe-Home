// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <AdvancedLogger.h>
#include <stdint.h>
#include <stddef.h>

#include "structs.h"

// Physical 74HC4067 chip maximum: 16 channels (Y0-Y15).
// Used for muxChannelMap array sizing; do not use for runtime iteration.
#define HW_PROFILE_MAX_MUX_CHANNELS 16

// Compile-time maximum total channels across all PCB versions.
// Use this ONLY for static array sizing (e.g. _meterValues[MAX_CHANNEL_COUNT]).
// For runtime iteration use globalHwProfile->totalChannelCount.
#define MAX_CHANNEL_COUNT (HW_PROFILE_MAX_MUX_CHANNELS + 1)

// Hardware profile for a specific PCB version.
// Add a new entry to PCB_PROFILES[] in hardware_profile.cpp to support a new version.
struct HardwareProfile {
    uint8_t version; // PCB version number (e.g. 61 for v6.1)

    // RGB LED pins
    uint8_t ledRedPin;
    uint8_t ledGreenPin;
    uint8_t ledBluePin;

    // General use button pin
    uint8_t buttonPin;

    // Analog multiplexer select pins (74HC4067)
    uint8_t muxS0Pin;
    uint8_t muxS1Pin;
    uint8_t muxS2Pin;
    uint8_t muxS3Pin;

    // ADE7953 SPI pins
    uint8_t ade7953SsPin;
    uint8_t ade7953SckPin;
    uint8_t ade7953MisoPin;
    uint8_t ade7953MosiPin;
    uint8_t ade7953ResetPin;
    uint8_t ade7953InterruptPin;

    // Voltage sensing network resistor values (ohms).
    // Used at runtime to compute _voltPerLsb in Ade7953::begin() (see ade7953.cpp).
    float voltageDividerR1;
    float voltageDividerR2;

    // Total physical channels on the mux chip (e.g. 16 for 74HC4067, 8 for 74HC4051).
    // Used as the runtime upper bound in Multiplexer::setChannel().
    // muxChannelMap is sized to HW_PROFILE_MAX_MUX_CHANNELS (compile-time max across all versions).
    uint8_t muxChipChannels;

    // Number of multiplexer channels actually connected on this PCB version.
    // Must be <= muxChipChannels.
    // Example: v6.1 uses 74HC4067 (16 physical) but Y1 is absent, so muxChannelCount = 15.
    uint8_t muxChannelCount;

    // Total active software channels = muxChannelCount + 1.
    // Channel 0 is always the direct ADE7953 input (not routed through the mux).
    // Use this for all runtime iteration and bounds checks.
    // Example: v6.1 → totalChannelCount = 16 (channels 0-15).
    uint8_t totalChannelCount;

    // Maps a logical mux index (0-based) to the physical 74HC4067 channel number (0-15).
    //
    // The physical mux channel is what gets passed to Multiplexer::setChannel(). It determines
    // the binary state of the S0-S3 select lines (S0=bit0, S1=bit1, S2=bit2, S3=bit3).
    //
    // In v5 this was a direct 1:1 map (logical 0 → physical 0, etc.).
    // In v6.1, Y1 was removed to make space on the PCB (see schematic note), so there is no CT2.
    // Software channels are renumbered to fill the gap: CT3 becomes channel 2, CT4 becomes
    // channel 3, and so on. The map encodes this:
    //   logical 0 → physical 0 (Y0 = CT1)
    //   logical 1 → physical 2 (Y2 = CT3, Y1 skipped)
    //   logical 2 → physical 3 (Y3 = CT4)
    //   ...
    //   logical 14 → physical 15 (Y15 = CT16)
    //
    // Only the first muxChannelCount entries are valid. Array sized to HW_PROFILE_MAX_MUX_CHANNELS.
    uint8_t muxChannelMap[HW_PROFILE_MAX_MUX_CHANNELS];
};

// Active hardware profile, set once by initHardwareProfile(). Always valid after that call.
extern const HardwareProfile* globalHwProfile;

// True when the device is not factory-provisioned (community / open-source use).
// In community mode, cloud (MQTT / AWS) is disabled. All local integrations still work.
extern bool globalCommunityMode;

// Read pcb_revision from NVS factory namespace, select the matching hardware profile,
// and set globalHwProfile and globalCommunityMode. Must be called before any hardware
// initialization in setup().
//
// Selection order:
//   1. NVS factory_ns::pcb_revision parses to a known profile -> use it (provisioned).
//   2. NVS missing / malformed / unknown version:
//      - if PCB_VERSION_FALLBACK is defined at build time and matches a profile -> use it
//      - else -> PCB_PROFILES[0] (latest).
//      In both sub-cases globalCommunityMode is set to true.
void initHardwareProfile();
