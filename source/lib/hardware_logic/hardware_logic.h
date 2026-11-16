// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi
//
// Pure hardware-profile helpers (ADE7953 PGA gain, mux map sanity), free of any
// Arduino dependency so they are host-testable (test/test_hardware_logic).

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace HardwareLogic {

// ADE7953 PGA_x[2:0] register values (datasheet Table 6): 000 = x1, 001 = x2, 010 = x4,
// 011 = x8, 100 = x16. 101-111 are not available.
constexpr uint8_t PGA_REGISTER_MAX = 4;

constexpr bool pgaRegisterIsValid(uint8_t pgaRegister) {
    return pgaRegister <= PGA_REGISTER_MAX;
}

// Linear gain for a PGA register value, 0 for an invalid value.
constexpr uint8_t pgaGainFromRegister(uint8_t pgaRegister) {
    return pgaRegisterIsValid(pgaRegister) ? static_cast<uint8_t>(1u << pgaRegister) : 0;
}

// Gain from the CT jack to the ADC pin: input divider ratio (Vadc/Vjack, 1.0 = no divider)
// times PGA gain. A value of 1.0 means the ADC full scale (+-0.5 V peak at gain 1) is
// reached at +-0.5 V at the jack, so the CT scale factors need no correction.
// Home v6.2 / Pro v1.1: 1k/1k divider (0.5) with PGA_IB = 2 -> 1.0.
constexpr float netInputGain(float dividerRatio, uint8_t pgaRegister) {
    return dividerRatio * static_cast<float>(pgaGainFromRegister(pgaRegister));
}

// A mux map is sane when its first `count` entries are distinct physical channels
// below `chipChannels`.
inline bool muxMapIsValid(const uint8_t* map, size_t count, size_t chipChannels) {
    if (map == nullptr || count > chipChannels || chipChannels > 32) return false;
    uint32_t seen = 0;
    for (size_t i = 0; i < count; i++) {
        if (map[i] >= chipChannels) return false;
        const uint32_t bit = 1u << map[i];
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}

} // namespace HardwareLogic
