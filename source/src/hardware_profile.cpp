// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "hardware_profile.h"

#include <Preferences.h>
#include <stdio.h>

#include "constants.h"
#include "factory_keys.h"

// Known PCB hardware profiles.
// To add support for a new PCB version: add a new entry on top of the array.
// The first entry is treated as the "latest" and used as the ultimate fallback.
const HardwareProfile PCB_PROFILES[] = {
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
        // 15 mux channels remain: Y0, Y2-Y15 -> CT1, CT3-CT16.
        .muxChipChannels   = 16, // 74HC4067: 16 physical channels (Y0-Y15)
        .muxChannelCount   = 15, // Y1 absent on v6.1 PCB -> 15 connected mux channels
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
    {
        .version = 60, // v6.0

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

        // Voltage sensing (same network as v6.1)
        .voltageDividerR1 = 153000.0f,
        .voltageDividerR2 = 180.0f,

        // v6.0: all 16 mux channels populated (Y0-Y15 -> CT1-CT16), identity map.
        .muxChipChannels   = 16,
        .muxChannelCount   = 16,
        .totalChannelCount = 17, // muxChannelCount + 1 (channel 0 = direct ADE7953 input)
        .muxChannelMap = {
            0, 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15,
        },
    },
};

static const size_t PCB_PROFILES_COUNT = sizeof(PCB_PROFILES) / sizeof(PCB_PROFILES[0]);

const HardwareProfile* globalHwProfile = nullptr;
bool globalCommunityMode = false;

// Parse a pcb_revision string of the form "vMAJOR.MINOR" (e.g. "v6.1") into the
// packed uint8_t used by HardwareProfile::version (major * 10 + minor).
// Returns true on success; false on any parse/format error.
static bool parsePcbRevision(const char* s, uint8_t& versionOut) {
    if (s == nullptr || s[0] != 'v') return false;
    unsigned int major = 0;
    unsigned int minor = 0;
    int matched = sscanf(s, "v%u.%u", &major, &minor);
    if (matched != 2) return false;
    if (major > 25 || minor > 9) return false; // keep (major*10+minor) within uint8_t
    versionOut = static_cast<uint8_t>(major * 10 + minor);
    return true;
}

static const HardwareProfile* findProfileByVersion(uint8_t version) {
    for (size_t i = 0; i < PCB_PROFILES_COUNT; i++) {
        if (PCB_PROFILES[i].version == version) return &PCB_PROFILES[i];
    }
    return nullptr;
}

// Select the profile used in community (unprovisioned) mode. Honours the optional
// PCB_VERSION_FALLBACK compile-time flag; otherwise returns the latest profile.
static const HardwareProfile* pickCommunityFallback() {
#ifdef PCB_VERSION_FALLBACK
    const HardwareProfile* p = findProfileByVersion(static_cast<uint8_t>(PCB_VERSION_FALLBACK));
    if (p != nullptr) {
        LOG_INFO("Community mode: using PCB_VERSION_FALLBACK=v%u", p->version);
        return p;
    }
    LOG_WARNING("PCB_VERSION_FALLBACK=%d does not match any known profile - using PCB_PROFILES[0] (v%u)",
                (int)PCB_VERSION_FALLBACK, PCB_PROFILES[0].version);
#endif
    return &PCB_PROFILES[0];
}

void initHardwareProfile() {
    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_INFO("Factory NVS not available - running in community mode, cloud disabled");
        return;
    }

    String pcbRevision = prefs.getString(FACTORY_KEY_PCB_REVISION, "");
    prefs.end();

    if (pcbRevision.length() == 0) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_INFO("pcb_revision not set in factory NVS - running in community mode, cloud disabled");
        return;
    }

    uint8_t version = 0;
    if (!parsePcbRevision(pcbRevision.c_str(), version)) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_WARNING("Malformed pcb_revision \"%s\" in factory NVS - running in community mode",
                    pcbRevision.c_str());
        return;
    }

    const HardwareProfile* profile = findProfileByVersion(version);
    if (profile == nullptr) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_WARNING("Unknown pcb_revision \"%s\" (v%u) - running in community mode, cloud disabled",
                    pcbRevision.c_str(), version);
        return;
    }

    globalHwProfile = profile;
    globalCommunityMode = false;
    LOG_INFO("Hardware profile selected: v%u (pcb_revision=\"%s\")", version, pcbRevision.c_str());
}
