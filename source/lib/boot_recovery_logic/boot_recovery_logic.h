// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstdint>

// Pure decision logic for the boot-loop ladder (openspec capability:
// boot-loop-recovery). The firmware module (src/bootrecovery.cpp) reads the
// persisted state and the reset reason, asks decide() what to do, persists the
// result and carries out the action.
namespace BootRecoveryLogic {

// Persisted as a byte, so the values are part of the NVS format.
enum class Stage : uint8_t {
    Normal = 0,   // full boot
    Wiped = 1,    // full boot after the ladder's factory reset
    Recovery = 2, // meter passive, no Modbus TCP; WiFi, web server and cloud MQTT up
    Minimal = 3,  // Recovery plus community mode: only WiFi and the web server
};

enum class ResetKind : uint8_t {
    Neutral,  // power-on, USB, JTAG, reset pin: says nothing about the last boot
    Software, // ESP.restart() / esp_restart()
    Fault,    // panic, watchdogs, brownout, anything unknown
};

enum class Action : uint8_t {
    Boot,         // continue booting at Decision::stage
    RollBack,     // switch to the passive image and restart
    FactoryReset, // wipe user state and restart
};

inline constexpr uint8_t UNSTABLE_BOOTS_TO_ESCALATE = 3;

struct Input {
    Stage storedStage;
    uint8_t storedUnstableBoots;
    bool firmwareMatches;     // stored firmware id equals the running image's
    ResetKind resetKind;
    bool deliberateRestart;   // the previous boot flagged its restart as deliberate
    bool rollbackAllowed;     // passive image validates and no automatic rollback since the last stable boot
};

struct Decision {
    Action action;
    Stage stage;          // stage to persist and, for Boot, to boot with
    uint8_t unstableBoots; // count to persist
};

// Byte read back from NVS. Anything unknown is treated as Normal.
Stage stageFromByte(uint8_t value);
const char *stageToString(Stage stage);

// Whether the previous boot counts as unstable. Only a fault, or a software
// restart nobody flagged as deliberate, does.
bool isUnstableBoot(ResetKind kind, bool deliberateRestart);

Decision decide(const Input &input);

const char *actionToString(Action action);

// One boot as the ladder saw it. Persisted as a raw blob, so the layout is part
// of the NVS format: append fields only.
struct HistoryEntry {
    uint8_t resetReason; // esp_reset_reason_t of the previous boot
    uint8_t deliberate;  // the previous boot flagged its restart as deliberate
    uint8_t newFirmware; // first boot of this image
    uint8_t fromStage;
    uint8_t toStage;
    uint8_t unstableBoots; // count after this boot
    uint8_t action;
};

inline constexpr uint8_t HISTORY_CAPACITY = 12;

// Appends entry to a history of `count` entries, oldest first, dropping the
// oldest when full. Returns the new count.
uint8_t pushHistory(HistoryEntry *entries, uint8_t count, const HistoryEntry &entry);

// What a stage turns off.
inline bool meterPassive(Stage stage) { return stage == Stage::Recovery || stage == Stage::Minimal; }
inline bool cloudDisabled(Stage stage) { return stage == Stage::Minimal; }
inline bool isDegraded(Stage stage) { return meterPassive(stage); }

} // namespace BootRecoveryLogic
