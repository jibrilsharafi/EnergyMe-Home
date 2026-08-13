# Design: firmware-rollback-command

## Context

See proposal.md - Why. Facts the approach rests on, verified on this exact toolchain (pioarduino 55.03.32, disassembly of the shipped `libapp_update.a` where the IDF sources are not distributed):

- **`esp_app_desc_t.version` is a frozen constant** (`"487f743"`, the arduino-lib-builder hash) in every build, because pioarduino ships `esp_app_desc` prebuilt and `FIRMWARE_BUILD_VERSION` is a C macro that never reaches `PROJECT_VER`. The **only** per-build field in the descriptor is `app_elf_sha256`. A version string for the passive slot is unknowable on-device; the sha256 fingerprint is ground truth.
- **`esp_ota_get_partition_description(passive, &desc)` works on any partition** (already used on the not-yet-booted slot at `mqtt.cpp:1525`). Returns `ESP_ERR_NOT_FOUND` on an erased/invalid slot. The descriptor sits at flash offset 0x20 and is written early in a download, so a *partial* image can still return a readable descriptor - the sha comparison then correctly mismatches, and image validation (below) is the bootability gate.
- **`Update.canRollBack()` is a one-byte check** (`flash[other][0] == 0xE9`, `Updater.cpp:60-73`) - true for partial images. Not a gate.
- **`esp_ota_set_boot_partition()` runs full `image_validate()` internally** and returns `ESP_ERR_OTA_VALIDATE_FAILED` on a bad image; it never reads the target's `ota_state` (overwrites it with `ESP_OTA_IMG_NEW`), so no otadata state can cause a refusal. This is the real gate.
- **No bootloader rollback exists past early boot**: `initArduino()` calls `esp_ota_mark_app_valid_cancel_rollback()` before `setup()` (`esp32-hal-misc.c:314-321`; no `verifyOta`/`verifyRollbackLater` override in `source/`). Both slots sit at `ESP_OTA_IMG_VALID` in steady state. Comments at `mqtt.cpp:1493` and `:2823` claiming otherwise are corrected in this change.
- CrashMonitor's `_rollbackTried` is RTC-persisted, set only by the crash path (`crashmonitor.cpp:412`), cleared after 180 s stable / successful OTA. The existing manual endpoint never sets it.
- Pending-OTA validation state (`mqtt_ns`: `ota_pending`, `ota_job_id`, `ota_sha256`) survives reboot. It always belongs to the job that flashed the currently running image (written just before that image's first boot, cleared when validation resolves), so after a rollback reboot the existing `FAILED/sha256_mismatch_firmware_rollback` publish is that job's *correct* terminal status - the record must be preserved, not cleared. (The original proposal assumed the opposite; the adversarial review caught it.)

## Goals / Non-Goals

**Goals**: one shared device-side rollback routine with two entry points (cloud command, local endpoint); honest act-then-report semantics; passive observability of the rollback target; no new NVS state.

**Non-Goals**: #160 signature-verification interplay (handled on that branch later - note its abort path leaves a fully-written signature-rejected image in the passive slot, which the sha precondition here happens to defend against on the cloud path); recovering already-stranded 2.3.0 devices (this is forward insurance); a human-readable version name for the passive slot (unknowable on-device; the cloud maps sha to version from release artifacts).

## Decisions

1. **Shared core in `utils.cpp` (or a small module), not duplicated per path.** `attemptFirmwareRollback()` does: refuse up front when a restart is already scheduled or the uptime/safe-mode gate is closed (calling `setRestartSystem` and reacting to `false` is NOT equivalent - its gate branch latches a ghost restart, and its already-scheduled branch would make a duplicate request restore the boot partition of an in-flight rollback) → `esp_ota_set_boot_partition(passive)` → `CrashMonitor::markRollbackTried()` (before the restart is scheduled, since the restart task can preempt the caller) → `setRestartSystem()`; on the residual-race refusal, undo the mark, restore the boot partition, and return failure. Both entry points map its result to their status vocabulary. Alternative (each path calls IDF directly, as today): rejected - that is how the current endpoint got the report-then-act bug.
2. **Fingerprint precondition, not version.** The descriptor version is a frozen constant on this toolchain (see Context) - a version-based precondition would drift silently. `expected_sha256` is compared case-insensitively against the passive descriptor's sha; comparing against the *running* sha detects redelivery-after-success and yields an idempotent `SUCCEEDED` no-op. This makes QoS1 redelivery inside the 300 s staleness window safe (a bare command would ping-pong the slots).
3. **`esp_ota_set_boot_partition()` directly as the gate; drop `Update.canRollBack()`/`Update.rollBack()` from the rollback path.** The Update wrapper adds only the one-byte check and hides the error code. The crash-path usage in `crashmonitor.cpp` stays as-is (out of scope).
4. **Local path: interactive confirm, no sha parameter.** The authenticated local user is present; a last-resort tool must not demand data the operator may not have during an incident. The confirm dialog stops being blind: `otherPartitionSha256` from `/api/v1/ota/status` is shown (first 12 hex chars suffice visually, full value in the API).
5. **Reporting: flat fields.** `otherPartitionSha256` (REST, camelCase per API convention) and `other_partition_sha256` (info shadow, snake_case per shadow convention). No nested partition structure - there are exactly two app slots and no third coming.
6. **Cloud response carries no target data** (existing `_publishCommandStatus` shape: reasonDescription only alongside a reasonCode). The operator learns the target from the info shadow *before* sending, which is the correct order during an incident anyway. Issue #237's "report the target version in the command response" is satisfied cloud-side by the shadow + release-artifact sha→version mapping; no response-schema change.
7. **Host-testable precondition logic in `source/lib/`** (mirrors `version_compare` / `shadow_logic` pattern): sha hex validation, case-insensitive compare, decision function (passive sha, running sha, expected) → {PROCEED, NOOP_ALREADY_DONE, MISMATCH, MISSING, NO_TARGET}. Unity tests under `source/test/`, run via WSL.
8. **`markRollbackTried()` is a new deliberate setter** on CrashMonitor (the existing state is crash-path-internal). It is set *before* restart on both paths, accepting that the 180 s stability timer will re-arm auto-rollback afterwards - that is the documented existing semantic (a stable 3-minute run proves the rolled-back-to image is workable), not a gap this change needs to close.

## Risks / Trade-offs

- [Rollback boots an image whose *data* schema is older than current NVS/LittleFS state] → Accepted: NVS migrations in this codebase are forward-only-additive on read; same exposure as the crash-driven auto-rollback that already exists. Not solvable device-side without version knowledge.
- [Sha in the info shadow goes stale if flash is rewritten out-of-band (USB)] → Shadow republishes on every MQTT (re)connect, which follows any reboot a reflash requires.
- [Operator sends `expected_sha256` of a *build* that was never in the passive slot] → `TARGET_MISMATCH`, fail-safe by construction.
- [Restart-refusal restore path itself fails (`set_boot_partition(running)` error)] → Practically impossible (running image just validated at boot); if it happens, log FATAL and still report failure - next reboot boots the passive slot, which the operator asked for anyway.
- [180 s stability timer re-arms auto-rollback after a manual rollback] → Accepted as designed; see Decision 8.
- [If the rolled-back-to image itself crash-loops fast, the crash ladder skips its rollback rung (`_rollbackTried` is set) and escalates to factory reset] → Accepted: bouncing back to the image the operator deliberately left is worse; slow-crash cases re-arm via the 180 s timer. Known follow-up: route the crash ladder's own rollback through the validated switch primitive (it still uses `Update.rollBack()`, though `esp_ota_set_boot_partition` inside it does validate the image, so it cannot boot a corrupt slot).
- [HTTP response can race the restart task's `CustomServer::stop()` teardown, showing a spurious failure in the UI for a successful rollback] → Accepted: same pre-existing race as `POST /api/v1/system/restart`; a retry hits the already-scheduled gate and fails honestly, and the page reload after reboot shows the truth.

## Migration Plan

Pure firmware addition, no data migration. Fleet gains the capability only on versions carrying it (forward insurance - cannot help devices already stranded on 2.3.0). Cloud side (energyme-infra): extract `app_elf_sha256` per release (offset 0xB0 of `firmware.bin`, or from the device's own info shadow), add the `firmware_rollback` command sender. Ships independently after the firmware.

## Open Questions

(none)
