// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "boot_recovery_logic.h" // lib/boot_recovery_logic - pure ladder (host-tested)
#include "constants.h"

// Boot-loop ladder (openspec capability: boot-loop-recovery). Runs first thing in
// setup(), before LittleFS, the NVS restore or the logger, and needs nothing but
// NVS and Serial. Every boot that ends before COUNTERS_RESET_TIMEOUT without a
// deliberate restart counts as unstable; three in a row escalate one stage:
// rollback or factory reset, then Recovery (meter passive, no Modbus TCP), then
// Minimal (also no cloud). The stage sticks to the running firmware and survives
// power cycles; a firmware change clears it.

#define BOOT_RECOVERY_STAGE_KEY "stage"
#define BOOT_RECOVERY_UNSTABLE_KEY "unstable"
#define BOOT_RECOVERY_FIRMWARE_KEY "fw"
#define BOOT_RECOVERY_AUTO_ROLLBACK_KEY "rb_auto" // An automatic rollback happened since the last stable boot
#define BOOT_RECOVERY_HISTORY_KEY "hist" // Last HISTORY_CAPACITY boots as the ladder saw them
#define BOOT_RECOVERY_DEBUG_CRASH_KEY "dbg_crash" // Dev builds only: abort after LittleFS while stage < value
#define BOOT_RECOVERY_DEBUG_KEEP_WIFI_KEY "dbg_keepwifi" // Dev builds only: the ladder wipe keeps WiFi
#define BOOT_RECOVERY_DEBUG_NO_ROLLBACK_KEY "dbg_norb" // Dev builds only: the ladder never rolls back
#define BOOT_RECOVERY_FIRMWARE_ID_BYTES 8 // Prefix of app_elf_sha256, ample to tell builds apart
#define BOOT_RECOVERY_FIRMWARE_ID_BUFFER_SIZE (BOOT_RECOVERY_FIRMWARE_ID_BYTES * 2 + 1)

namespace BootRecovery
{
    // Classifies the previous boot and runs the ladder. Does not return when the
    // ladder rolls back or factory resets: both restart the device.
    void begin();

    BootRecoveryLogic::Stage stage();
    bool meterPassive();
    bool cloudDisabled();
    bool isDegraded();

    // Call right before any deliberate restart, so the next boot does not count
    // it as unstable. Survives software resets only (RTC memory).
    void markDeliberateRestart();

    // The boot reached COUNTERS_RESET_TIMEOUT: clear the unstable-boot count, and
    // drop Wiped back to Normal.
    void markStable();

    // Blocked while an operator rollback is in flight, so the ladder never bounces
    // back to the image just left; allowed again after a fresh OTA. Stable boots
    // clear it too.
    void setAutoRollbackBlocked(bool blocked);

    // Leave Recovery/Minimal: the next boot is a full one, re-entering the ladder
    // at Wiped so a repeated failure goes back to Recovery without a second wipe.
    // Returns false when the device is not degraded. The caller restarts.
    bool requestExit();

    void toJson(JsonDocument &doc);

#ifdef ENV_DEV
    // Aborts while the current stage is below the armed threshold (bench aid).
    void injectBootCrashIfArmed();
    // keepWifi keeps the device on its network through the wipe and noRollback
    // walks the ladder even with a valid passive image, so it can run remotely
    bool setBootCrashThreshold(uint8_t untilStage, bool keepWifi, bool noRollback);
#endif
}
