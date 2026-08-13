# firmware-rollback Specification (delta)

## Purpose

Boot the previous firmware from the other OTA app partition on demand, without any download: a cloud `firmware_rollback` command guarded by a sha256 precondition, a fixed local rollback endpoint with honest failure reporting, passive reporting of the rollback target's fingerprint, and deliberate interplay with the crash-driven auto-rollback and pending-OTA validation state.

## ADDED Requirements

### Requirement: Cloud rollback requires a sha256 precondition
The `firmware_rollback` AWS IoT command SHALL require an `expected_sha256` parameter: the 64-hex-character `app_elf_sha256` of the firmware image expected in the passive OTA partition. The device SHALL compare it case-insensitively against the passive partition's application descriptor sha256 before switching. The device SHALL NOT expose any cloud rollback path without this precondition.

- Missing or non-64-hex `expected_sha256` SHALL be rejected with `MISSING_SHA256`.
- Passive partition descriptor unreadable (empty/erased/no valid app descriptor) SHALL be rejected with `NO_ROLLBACK_TARGET`.
- `expected_sha256` differing from the passive partition's sha256 SHALL be rejected with `TARGET_MISMATCH`, unless it equals the RUNNING partition's sha256, in which case the device SHALL report plain `SUCCEEDED` without switching or restarting (idempotent redelivery: the rollback already happened).

#### Scenario: Matching sha triggers the rollback
- **WHEN** a `firmware_rollback` command arrives with `expected_sha256` equal to the passive partition's `app_elf_sha256`
- **THEN** the device switches the boot partition to the passive slot, reports `SUCCEEDED`, and restarts

#### Scenario: Mismatched sha is rejected
- **WHEN** `expected_sha256` matches neither the passive nor the running partition's sha256
- **THEN** the device rejects with `TARGET_MISMATCH` and does not switch or restart

#### Scenario: Redelivered command after a completed rollback is a no-op success
- **WHEN** `expected_sha256` equals the RUNNING partition's sha256 (the device already rolled back to it)
- **THEN** the device reports plain `SUCCEEDED` without switching the boot partition or restarting

#### Scenario: Empty passive slot is rejected
- **WHEN** the passive partition holds no readable application descriptor (e.g. fresh factory device)
- **THEN** the device rejects with `NO_ROLLBACK_TARGET`

#### Scenario: Missing parameter is rejected
- **WHEN** a `firmware_rollback` command arrives without `expected_sha256`, or with a value that is not 64 hex characters
- **THEN** the device rejects with `MISSING_SHA256`

### Requirement: Boot-partition switch is validated at switch time and reported honestly
Both rollback paths (cloud command and local endpoint) SHALL switch via the OTA API that performs full image validation of the target slot (`esp_ota_set_boot_partition` semantics) and SHALL treat its return code as the outcome. The one-byte image-magic check (`Update.canRollBack()`) SHALL NOT be used as the rollback gate. Success SHALL be reported only after the switch has succeeded (act-then-report). If the switch succeeds but the restart cannot be initiated (e.g. restart lock), the device SHALL restore the boot partition to the currently running slot and report failure, so a reported failure never leaves a pending silent partition switch.

#### Scenario: Invalid image in the passive slot fails the switch
- **WHEN** the passive slot's image fails validation at switch time (e.g. partial download with a valid first byte)
- **THEN** the switch API returns an error, the device reports failure (`ROLLBACK_FAILED` on the cloud path, an HTTP error on the local path), and does not restart

#### Scenario: Blocked restart rolls the switch back
- **WHEN** the boot-partition switch succeeds but the restart request is refused
- **THEN** the device restores the boot partition to the running slot and reports failure

### Requirement: Local rollback endpoint acts before responding
`POST /api/v1/ota/rollback` SHALL perform the boot-partition switch first and respond according to the actual outcome: success only when the switch succeeded and the restart was initiated; an error status otherwise. No sha256 precondition is required locally - the authenticated user confirms interactively, and the UI confirmation SHALL display the rollback target's sha256 fingerprint so the confirmation is informed rather than blind.

#### Scenario: Successful local rollback
- **WHEN** an authenticated `POST /api/v1/ota/rollback` arrives and the passive slot validates
- **THEN** the device switches the boot partition, responds success, and restarts

#### Scenario: Failed local rollback reports failure
- **WHEN** the passive slot fails validation or the restart is refused
- **THEN** the response is an HTTP error with `success: false` and the device keeps running the current firmware on the current boot partition

### Requirement: Rollback target fingerprint is observable before any incident
The device SHALL report the passive OTA partition's `app_elf_sha256` (64 hex characters) as `otherPartitionSha256` in `GET /api/v1/ota/status`, alongside the existing current-partition fields. When the passive slot has no readable descriptor the field SHALL be `null`.

#### Scenario: Status exposes the rollback target
- **WHEN** `GET /api/v1/ota/status` is requested on a device that has completed at least one OTA
- **THEN** the response contains `otherPartitionSha256` with the passive slot's 64-hex sha256

#### Scenario: Unreadable passive slot reported as null
- **WHEN** the passive partition holds no valid application descriptor
- **THEN** `otherPartitionSha256` is `null`

### Requirement: Rollback disarms crash-driven auto-rollback and preserves the pending-OTA record
Before restarting, both rollback paths SHALL mark the crash monitor's rollback-tried state (so the crash-driven auto-rollback cannot immediately switch back to the known-bad image the operator just left). The pending-OTA validation record SHALL be left intact: it always belongs to the OTA job that flashed the currently running image, so after the rollback reboot the standard post-OTA validation gives that job its correct terminal status (`FAILED` with a sha256-mismatch/rollback reason) instead of leaving it `IN_PROGRESS` in the fleet forever.

#### Scenario: Auto-rollback does not bounce back after a manual rollback
- **WHEN** a manual rollback boots the previous firmware and that firmware then crashes repeatedly
- **THEN** the crash monitor does not roll back again to the image the operator deliberately left, and proceeds to its other recovery tiers

#### Scenario: Rolling back away from a just-flashed OTA reports that job as failed
- **WHEN** a rollback restarts the device while the pending-OTA validation record for the just-flashed image exists
- **THEN** after the reboot the device publishes `FAILED` with a sha256-mismatch/rollback reason for that OTA job, giving it a correct terminal status
