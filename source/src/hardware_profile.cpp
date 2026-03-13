// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "hardware_profile.h"

#include <esp_efuse.h>
#include <esp_efuse_table.h>

// Known PCB hardware profiles.
// To add support for a new PCB version: add a new entry here. No other changes needed.
const HardwareProfile PCB_PROFILES[] = {
    // NOTE: always add new ones on top, so the fallback solutions (using index 0) always use the latest profile.
    {
        .version = 61, // v6.1

        // RGB LED
        .ledRedPin   = 39,
        .ledGreenPin = 40,
        .ledBluePin  = 41,

        // Button
        .buttonPin = 0,

        // Analog multiplexer (74HC4067) select lines
        .muxS0Pin = 48,
        .muxS1Pin = 38,
        .muxS2Pin = 21,
        .muxS3Pin = 47,

        // ADE7953 SPI
        .ade7953SsPin        = 10,
        .ade7953SckPin       = 13,
        .ade7953MisoPin      = 12,
        .ade7953MosiPin      = 11,
        .ade7953ResetPin     = 9,
        .ade7953InterruptPin = 14,

        // Voltage sensing: 2mA/2mA transformer, 3x51kΩ series on high side, 180Ω burden on low side
        .voltageDividerR1 = 153000.0f,
        .voltageDividerR2 = 180.0f,

        // Y1 was removed from the PCB to make space (see schematic note: "CH2 Y1 had to be
        // removed to make space. The proper sequence/mapping will be handled in software.").
        // There is no CT2 jack on v6.1. Software channels are renumbered to fill the gap:
        // CT3 becomes channel 2, CT4 becomes channel 3, and so on.
        // 15 mux channels remain: Y0, Y2-Y15 → CT1, CT3-CT16.
        .muxChipChannels   = 16, // 74HC4067: 16 physical channels (Y0-Y15)
        .muxChannelCount   = 15, // Y1 absent on v6.1 PCB → 15 connected mux channels
        .totalChannelCount = 16, // muxChannelCount + 1 (channel 0 = direct ADE7953 input)
        .muxChannelMap = {
            //  logical  physical  CT label
            0,  //   0       Y0     CT1
            2,  //   1       Y2     CT3  (Y1 removed, CT2 does not exist on v6.1)
            3,  //   2       Y3     CT4
            4,  //   3       Y4     CT5
            5,  //   4       Y5     CT6
            6,  //   5       Y6     CT7
            7,  //   6       Y7     CT8
            8,  //   7       Y8     CT9
            9,  //   8       Y9     CT10
            10, //   9       Y10    CT11
            11, //  10       Y11    CT12
            12, //  11       Y12    CT13
            13, //  12       Y13    CT14
            14, //  13       Y14    CT15
            15, //  14       Y15    CT16
            0,  //  15       (unused padding to reach HW_PROFILE_MAX_MUX_CHANNELS)
        },
    },
};

const HardwareProfile* globalHwProfile = nullptr;
bool globalCommunityMode = false;

bool readEfuseProvisioningData(EfuseProvisioningData& data) {
    uint8_t efuseData[32];

    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, efuseData, sizeof(efuseData) * 8);
    if (err != ESP_OK) {
        LOG_DEBUG("Failed to read eFuse user data: %s", esp_err_to_name(err));
        data.isProvisioned = false;
        return false;
    }

    if (efuseData[0] != 0x01) {
        LOG_DEBUG("Device not provisioned (flag: 0x%02X)", efuseData[0]);
        data.isProvisioned = false;
        return false;
    }

    data.isProvisioned = true;
    data.serial = *((uint32_t*)&efuseData[4]);              // Bytes 4-7: Serial (little-endian)
    data.manufacturingDate = *((uint64_t*)&efuseData[8]);   // Bytes 8-15: Manufacturing date (little-endian)
    data.hardwareVersion = *((uint8_t*)&efuseData[16]);     // Byte 16: Hardware version (little-endian)

    LOG_DEBUG("eFuse data: serial=0x%08X, mfgDate=%llu, hwVer=%u",
              data.serial, data.manufacturingDate, data.hardwareVersion);
    return true;
}

void initHardwareProfile() {
    EfuseProvisioningData efuseData;
    bool provisioned = readEfuseProvisioningData(efuseData);

    if (!provisioned || !efuseData.isProvisioned) {
        globalCommunityMode = true;
        globalHwProfile = &PCB_PROFILES[0]; // Just use the first one as fallback
        LOG_INFO("eFuse not provisioned — running in community mode, cloud disabled");
        return;
    }

    const size_t PCB_PROFILES_COUNT = sizeof(PCB_PROFILES) / sizeof(PCB_PROFILES[0]);

    for (size_t i = 0; i < PCB_PROFILES_COUNT; i++) {
        if (PCB_PROFILES[i].version == efuseData.hardwareVersion) {
            globalHwProfile = &PCB_PROFILES[i];
            LOG_INFO("Hardware profile selected: v%u", efuseData.hardwareVersion);
            return;
        }
    }

    // Unknown hardware version: default to first profile (v6.1) and warn
    globalHwProfile = &PCB_PROFILES[0];
    LOG_WARNING("Unknown hardware version %u in eFuse, defaulting to v%u", efuseData.hardwareVersion, PCB_PROFILES[0].version);
}
