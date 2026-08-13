// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#pragma once

#include <cstddef>

// Pure, dependency-free decision logic for the firmware_rollback precondition
// (openspec capability: firmware-rollback).
//
// The cloud command must carry the 64-hex app_elf_sha256 it expects to find in
// the passive OTA partition. This module owns the hex validation and the
// decision table; reading the actual partition descriptors is the caller's job
// (esp_ota_get_partition_description), so everything here is host-testable.
//
// The version string in esp_app_desc_t is a frozen arduino-lib-builder constant
// on this toolchain ("487f743" in every build) - app_elf_sha256 is the only
// per-build field, which is why the precondition is a fingerprint, not a
// version.

namespace RollbackLogic {

inline constexpr size_t SHA256_HEX_LEN = 64;

enum class Decision {
    PROCEED,            // expected matches the passive slot: switch and restart
    NOOP_ALREADY_DONE,  // expected matches the RUNNING slot: QoS1 redelivery
                        // after a completed rollback - report success, do nothing
    MISMATCH,           // expected matches neither slot -> TARGET_MISMATCH
    MISSING_SHA,        // expected absent or not 64 hex chars -> MISSING_SHA256
    NO_TARGET,          // passive descriptor unreadable -> NO_ROLLBACK_TARGET
};

// True iff `sha` is exactly 64 hex characters (case-insensitive).
bool isValidSha256Hex(const char *sha);

// Case-insensitive comparison of two sha256 hex strings. Either side failing
// isValidSha256Hex returns false.
bool sha256HexEquals(const char *a, const char *b);

// Decision table for the cloud command. `passiveReadable` is false when the
// passive slot has no readable app descriptor (fresh factory device or erased
// slot); `passiveSha`/`runningSha` are 64-hex strings valid only when their
// respective flags/contract say so (runningSha is always readable - we are
// executing from it).
Decision decide(const char *expectedSha,
                bool passiveReadable,
                const char *passiveSha,
                const char *runningSha);

}  // namespace RollbackLogic
