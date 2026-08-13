# Proposal: firmware-rollback-command

## Why

The 2026-08-12 incident (#191, #238) showed that a firmware with degraded heap cannot complete any OTA download - including a downgrade - stranding devices on a bad version with no remote escape hatch, even though the previous firmware still sits intact in the other OTA app partition. Booting it requires no download at all. Issue #237.

Additionally, the existing local rollback endpoint (`POST /api/v1/ota/rollback`) is unsafe as-is: it gates on `Update.canRollBack()`, which only checks `flash[other][0] == 0xE9` (true even for a half-downloaded image), reports success before acting, and discards the return values of `Update.rollBack()` and `setRestartSystem()`. A failed rollback reports success and reboots into the same firmware. This gets fixed as part of this change.

## What Changes

- New AWS IoT command `firmware_rollback` (fifth command, same dispatch as `restart`/`issue_ack` in `mqtt.cpp`) that boots the other OTA app partition without any download. The command **requires** an `expected_sha256` parameter that must match the passive slot's `app_elf_sha256` - this makes redelivery idempotent and prevents booting a partial/unknown image during an incident.
- The passive ("other") partition's `app_elf_sha256` is reported in the `info` shadow (`other_partition_sha256`) and in `GET /api/v1/ota/status` (`otherPartitionSha256`), so the fleet knows every device's rollback target passively, before any incident. Flat fields - no nested structure (only 2 app partitions exist).
- Fix the local rollback endpoint: gate on `esp_ota_set_boot_partition()`'s actual return (which runs full `image_validate()`), not `Update.canRollBack()`'s one-byte magic check; act before responding; report failure as failure. No `expected_sha256` required locally - the authenticated local user confirms interactively, and the UI shows the target sha.
- Both rollback paths (cloud + local) deliberately set CrashMonitor's `_rollbackTried` (so the crash-driven auto-rollback cannot immediately bounce the device back to the known-bad image) and clear the pending-OTA validation state in `mqtt_ns` (so a stale `ota_pending` cannot publish a bogus `sha256_mismatch` job failure after the rollback reboot).
- Correct two misleading comments claiming bootloader auto-rollback protection exists (`mqtt.cpp:1493`, `mqtt.cpp:2823`): `initArduino()` calls `esp_ota_mark_app_valid_cancel_rollback()` before `setup()` runs, so no bootloader-level rollback protects anything past early boot.

Out of scope: issue #160 (signature verification branch) is handled separately later, including its interaction with rollback (signature-rejected image in the passive slot). The known bugs on that branch (frozen `esp_app_desc_t.version` downgrade guard, nonexistent `esp_https_ota_is_complete_data_read` symbol) are fixed there, not here.

## Capabilities

### New Capabilities

- `firmware-rollback`: boot the previous firmware from the other OTA partition on demand (cloud command with sha256 precondition, local endpoint with interactive confirmation), with correct failure reporting and crash-monitor/pending-OTA state interplay.

### Modified Capabilities

- `iot-commands`: the "exactly four transient IoT Commands" requirement becomes five with `firmware_rollback`.
- `iot-device-shadows`: the `info` shadow reports the passive partition's sha256 fingerprint.

## Impact

- `source/src/mqtt.cpp`: command dispatch branch, comment fixes.
- `source/src/customserver.cpp`: rework `_serveOtaRollbackEndpoint`, extend `/api/v1/ota/status`.
- `source/src/shadow.cpp`: `info` shadow field.
- `source/src/crashmonitor.cpp` / `include/crashmonitor.h`: expose a `markRollbackTried()` (or equivalent) setter.
- `source/html/update.html` + `source/js/api-client.js`: show the rollback target sha in the confirm flow.
- `source/resources/swagger.yaml`: rollback + status endpoint docs.
- `source/lib/` + `source/test/`: host-testable helper for sha hex compare / precondition logic, Unity tests.
- Cloud side (energyme-infra, separate repo): sending `firmware_rollback` with the expected sha; CI already publishes `firmware.bin` per release, sha extractable at offset 0xB0. Not part of this change.
