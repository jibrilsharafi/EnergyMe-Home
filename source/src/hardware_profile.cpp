// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "hardware_profile.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "factory_keys.h"

// Known PCB hardware profiles.
// To add support for a new PCB version: add a new entry above the older entries of
// the same product - within each product, entries are ordered newest-first, and the
// first entry of a product is that product's "latest" (community fallback).
const HardwareProfile PCB_PROFILES[] = {
    {
        // EnergyMe Home Pro v1.0 (ESP32-S3-WROOM-1U-N16R8, W5500 Ethernet, 12 channels).
        // Pinout extracted from the PCB netlist (energyme-home-pro-pcb, 2026-08-31);
        // to be verified on hardware at bring-up.
        .product = ProductLine::HOME_PRO,
        .version = 10, // v1.0 - Pro PCB numbering restarts at v1.0

        // RGB LED (same as Home v6.x)
        .ledRedPin   = 40,
        .ledGreenPin = 41,
        .ledBluePin  = 39,

        // Button
        .buttonPin = 0,

        // Analog multiplexer (74HC4067) select lines - same pin set as Home v6.x, different order
        .muxS0Pin = 21,
        .muxS1Pin = 47,
        .muxS2Pin = 48,
        .muxS3Pin = 38,

        // ADE7953 SPI (identical to Home v6.x)
        .ade7953SsPin        = 10,
        .ade7953SckPin       = 13,
        .ade7953MisoPin      = 12,
        .ade7953MosiPin      = 11,
        .ade7953ResetPin     = 9,
        .ade7953InterruptPin = 14,

        // Voltage sensing: ZMPT107-1 (2mA/2mA), 3x51kΩ series, 180Ω burden - same as v6.x
        .voltageDividerR1 = 153000.0f,
        .voltageDividerR2 = 180.0f,

        // 11 mux channels wired (CT1-CT11) + 1 direct ADE7953 input (CT0) = 12 channels.
        // Y3-Y7 are grounded/unused on this PCB; the map encodes the routed Y per CT.
        .muxChipChannels   = 16,
        .muxChannelCount   = 11,
        .totalChannelCount = 12,
        .muxChannelMap = {
            //  logical  physical  CT label
            15, //   0       Y15    CT1
            14, //   1       Y14    CT2
            13, //   2       Y13    CT3
            12, //   3       Y12    CT4
            11, //   4       Y11    CT5
            2,  //   5       Y2     CT6
            8,  //   6       Y8     CT7
            1,  //   7       Y1     CT8
            9,  //   8       Y9     CT9
            0,  //   9       Y0     CT10
            10, //  10       Y10    CT11
            0, 0, 0, 0, 0, // unused padding to HW_PROFILE_MAX_MUX_CHANNELS
        },

        // W5500 Ethernet on a dedicated SPI bus (25 MHz crystal; INT and RST wired)
        .hasEthernet = true,
        .ethCsPin    = 16,
        .ethIrqPin   = 5,
        .ethRstPin   = 4,
        .ethSckPin   = 15,
        .ethMisoPin  = 7,
        .ethMosiPin  = 6,
    },
    {
        .product = ProductLine::HOME,
        .version = 61, // v6.1

        // RGB LED
        .ledRedPin   = 40,
        .ledGreenPin = 41,
        .ledBluePin  = 39,

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
        .product = ProductLine::HOME,
        .version = 60, // v6.0

        // RGB LED
        .ledRedPin   = 40,
        .ledGreenPin = 41,
        .ledBluePin  = 39,

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
    {
        .product = ProductLine::HOME,
        .version = 50, // v5.0 (02-12-2024)

        // Pin assignments are wholly different from v6.x - this is not a v6 board
        // with a few pins moved. Values taken from include/pins.h on the
        // legacy/pcb-v5 branch, which is the last firmware built for this PCB.

        // RGB LED
        .ledRedPin   = 39,
        .ledGreenPin = 40,
        .ledBluePin  = 38,

        // Button
        .buttonPin = 0,

        // Analog multiplexer (74HC4067) select lines
        .muxS0Pin = 10,
        .muxS1Pin = 11,
        .muxS2Pin = 3,
        .muxS3Pin = 9,

        // ADE7953 SPI
        .ade7953SsPin        = 48,
        .ade7953SckPin       = 36,
        .ade7953MisoPin      = 35,
        .ade7953MosiPin      = 45,
        .ade7953ResetPin     = 21,
        .ade7953InterruptPin = 37,

        // Mains fed straight through a resistive divider, not via a 2mA transformer
        // as on v6.x: 3x330 kOhm in series on the high side, 1 kOhm on the low side.
        // The ratio differs from v6.x by ~3 orders of magnitude, so a v6 profile on
        // a v5 board reads voltage wrong rather than merely imprecisely.
        .voltageDividerR1 = 990000.0f,
        .voltageDividerR2 = 1000.0f,

        // All 16 mux channels populated (Y0-Y15 -> CT1-CT16), identity map.
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

const char* productLineToString(ProductLine product) {
    switch (product) {
        case ProductLine::HOME:     return PRODUCT_LINE_HOME_STR;
        case ProductLine::HOME_PRO: return PRODUCT_LINE_HOME_PRO_STR;
    }
    return PRODUCT_LINE_HOME_STR;
}

bool productFromArtifactName(const char* name, ProductLine& productOut) {
    if (name == nullptr) return false;
    if (strstr(name, FIRMWARE_ARTIFACT_TOKEN_HOME_PRO) != nullptr) {
        productOut = ProductLine::HOME_PRO;
        return true;
    }
    if (strstr(name, FIRMWARE_ARTIFACT_TOKEN_HOME) != nullptr) {
        productOut = ProductLine::HOME;
        return true;
    }
    return false;
}

bool parseProductLineString(const char* s, ProductLine& productOut) {
    if (s == nullptr) return false;
    if (strcmp(s, PRODUCT_LINE_HOME_STR) == 0) {
        productOut = ProductLine::HOME;
        return true;
    }
    if (strcmp(s, PRODUCT_LINE_HOME_PRO_STR) == 0) {
        productOut = ProductLine::HOME_PRO;
        return true;
    }
    return false;
}

// The product a binary falls back to when factory NVS cannot answer. Pro build envs
// pin PRODUCT_FALLBACK=1 (HOME_PRO) so a Pro binary never falls back to a Home pinout.
static ProductLine buildFallbackProduct() {
#ifdef PRODUCT_FALLBACK
    return static_cast<ProductLine>(PRODUCT_FALLBACK);
#else
    return ProductLine::HOME;
#endif
}

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

static const HardwareProfile* findProfile(ProductLine product, uint8_t version) {
    for (size_t i = 0; i < PCB_PROFILES_COUNT; i++) {
        if (PCB_PROFILES[i].product == product && PCB_PROFILES[i].version == version) return &PCB_PROFILES[i];
    }
    return nullptr;
}

// First (= latest) profile of the given product. Entries are ordered newest-first
// within each product, so the first product match is that product's latest.
static const HardwareProfile* latestProfileForProduct(ProductLine product) {
    for (size_t i = 0; i < PCB_PROFILES_COUNT; i++) {
        if (PCB_PROFILES[i].product == product) return &PCB_PROFILES[i];
    }
    return &PCB_PROFILES[0];
}

// Select the profile used in community (unprovisioned) mode: scoped to the build's
// fallback product, honouring the optional PCB_VERSION_FALLBACK compile-time flag,
// otherwise that product's latest profile.
static const HardwareProfile* pickCommunityFallback() {
    ProductLine product = buildFallbackProduct();
#ifdef PCB_VERSION_FALLBACK
    const HardwareProfile* p = findProfile(product, static_cast<uint8_t>(PCB_VERSION_FALLBACK));
    if (p != nullptr) {
        LOG_INFO("Community mode: using PCB_VERSION_FALLBACK=v%u (%s)", p->version, productLineToString(product));
        return p;
    }
    LOG_WARNING("PCB_VERSION_FALLBACK=%d does not match any known %s profile - using that product's latest",
                (int)PCB_VERSION_FALLBACK, productLineToString(product));
#endif
    return latestProfileForProduct(product);
}

void initHardwareProfile() {
#ifdef FORCE_COMMUNITY_MODE
    globalCommunityMode = true;
    globalHwProfile = pickCommunityFallback();
    LOG_WARNING("FORCE_COMMUNITY_MODE build flag set - ignoring factory NVS, running in community mode");
    return;
#endif

    Preferences prefs;
    if (!prefs.begin(PREFERENCES_NAMESPACE_FACTORY, true)) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_INFO("Factory NVS not available - running in community mode, cloud disabled");
        return;
    }

    String pcbRevision = prefs.getString(FACTORY_KEY_PCB_REVISION, "");
    String productLineStr = prefs.getString(FACTORY_KEY_PRODUCT_LINE, "");
    prefs.end();

    if (pcbRevision.length() == 0) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_INFO("pcb_revision not set in factory NVS - running in community mode, cloud disabled");
        return;
    }

    // Absent product_line means Home: the deployed fleet predates the key and is
    // never backfilled (factory NVS stays write-once at manufacturing).
    ProductLine product = ProductLine::HOME;
    if (productLineStr.length() > 0 && !parseProductLineString(productLineStr.c_str(), product)) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_WARNING("Unknown product_line \"%s\" in factory NVS - running in community mode",
                    productLineStr.c_str());
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

    const HardwareProfile* profile = findProfile(product, version);
    if (profile == nullptr) {
        globalCommunityMode = true;
        globalHwProfile = pickCommunityFallback();
        LOG_WARNING("Unknown pcb_revision \"%s\" (v%u) for product %s - running in community mode, cloud disabled",
                    pcbRevision.c_str(), version, productLineToString(product));
        return;
    }

    globalHwProfile = profile;
    globalCommunityMode = false;
    LOG_INFO("Hardware profile selected: %s v%u (pcb_revision=\"%s\")",
             productLineToString(product), version, pcbRevision.c_str());
}
