// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "boot_recovery_logic.h"

namespace BootRecoveryLogic {

Stage stageFromByte(uint8_t value) {
    switch (value) {
        case static_cast<uint8_t>(Stage::Wiped): return Stage::Wiped;
        case static_cast<uint8_t>(Stage::Recovery): return Stage::Recovery;
        case static_cast<uint8_t>(Stage::Minimal): return Stage::Minimal;
        default: return Stage::Normal;
    }
}

const char *stageToString(Stage stage) {
    switch (stage) {
        case Stage::Normal: return "normal";
        case Stage::Wiped: return "wiped";
        case Stage::Recovery: return "recovery";
        case Stage::Minimal: return "minimal";
    }
    return "normal";
}

bool isUnstableBoot(ResetKind kind, bool deliberateRestart) {
    switch (kind) {
        case ResetKind::Fault: return true;
        case ResetKind::Software: return !deliberateRestart;
        case ResetKind::Neutral: return false;
    }
    return false;
}

Decision decide(const Input &input) {
    // A different image starts from scratch: whatever looped was the old one.
    Stage stage = input.firmwareMatches ? input.storedStage : Stage::Normal;
    uint8_t unstable = input.firmwareMatches ? input.storedUnstableBoots : 0;

    if (isUnstableBoot(input.resetKind, input.deliberateRestart) && unstable < UINT8_MAX) unstable++;

    if (unstable < UNSTABLE_BOOTS_TO_ESCALATE) return {Action::Boot, stage, unstable};

    switch (stage) {
        case Stage::Normal:
            if (input.rollbackAllowed) return {Action::RollBack, Stage::Normal, 0};
            return {Action::FactoryReset, Stage::Wiped, 0};
        case Stage::Wiped:
            return {Action::Boot, Stage::Recovery, 0};
        case Stage::Recovery:
        case Stage::Minimal:
            return {Action::Boot, Stage::Minimal, 0};
    }
    return {Action::Boot, stage, 0};
}

} // namespace BootRecoveryLogic
