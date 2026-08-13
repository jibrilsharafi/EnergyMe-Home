# Tasks: firmware-rollback-command

## 1. Host-testable rollback decision logic

- [ ] 1.1 Create `source/lib/rollback_logic/` (mirrors `version_compare` layout): sha256 hex validation (64 hex chars, case-insensitive compare) and the precondition decision function (expected, passiveSha, passiveReadable, runningSha) -> {PROCEED, NOOP_ALREADY_DONE, MISMATCH, MISSING_SHA, NO_TARGET}
- [ ] 1.2 Unity tests in `source/test/test_rollback_logic/`: valid/invalid hex, case-insensitivity, all five outcomes, redelivery-after-success case; run `pio test -e native` from WSL

## 2. Shared rollback core

- [ ] 2.1 Add `CrashMonitor::markRollbackTried()` setter (crashmonitor.h/.cpp), alongside the existing `clearRollbackTried()`
- [ ] 2.2 Expose pending-OTA state clearing for the rollback path (public wrapper around `_clearOtaPendingState()` in mqtt.cpp, or equivalent)
- [ ] 2.3 Implement shared `attemptFirmwareRollback()` (utils.cpp or the rollback module): read passive descriptor via `esp_ota_get_partition_description`, switch via `esp_ota_set_boot_partition` gating on its return, mark rollback-tried + clear pending-OTA before restart, restore boot partition and undo the tried-mark on restart refusal
- [ ] 2.4 Helper to read the passive slot's sha256 as 64-hex (reuse `_sha256ToHex`; returns false when descriptor unreadable)

## 3. Cloud command

- [ ] 3.1 Add `firmware_rollback` branch to `_handleCommandExecution` in mqtt.cpp: parse `expected_sha256` (string only), map decision outcomes to `MISSING_SHA256` / `NO_ROLLBACK_TARGET` / `TARGET_MISMATCH` (REJECTED), `ROLLBACK_FAILED` (FAILED), plain SUCCEEDED for both PROCEED and NOOP_ALREADY_DONE; publish SUCCEEDED before restart with the same 2 s flush as `restart`
- [ ] 3.2 Verify via the dev inject harness (`POST /api/v1/shadow/inject-command`): all reject paths + no-op path on the bench device

## 4. Local endpoint fix + reporting

- [ ] 4.1 Rework `_serveOtaRollbackEndpoint` (customserver.cpp): call `attemptFirmwareRollback()` first, respond from its result (success only on switch+restart initiated; 400/423/500 with `success:false` otherwise); drop `Update.canRollBack()`/`Update.rollBack()`
- [ ] 4.2 Add `otherPartitionSha256` (or `null`) to `GET /api/v1/ota/status`
- [ ] 4.3 update.html + api-client.js: show the target sha (truncated) in the rollback confirm flow; handle the error response honestly
- [ ] 4.4 swagger.yaml: update `/api/v1/ota/rollback` (423/500 responses) and `/api/v1/ota/status` (new field) - stage with autocrlf off (CRLF file)

## 5. Shadow + comment fixes

- [ ] 5.1 Add `other_partition_sha256` (or `null`) to the info shadow (shadow.cpp)
- [ ] 5.2 Correct the two false bootloader-rollback comments (`mqtt.cpp:1493`, `mqtt.cpp:2823`) to state that `initArduino()` marks the app valid before `setup()` and the validation task is telemetry-only

## 6. Verification

- [ ] 6.1 `pio run -e esp32s3-dev` clean build; `pio check -e esp32s3-dev` no new findings
- [ ] 6.2 Hardware e2e on the bench device (Jibril): real OTA to populate both slots, then (a) local rollback via UI - lands on previous build, honest success; (b) cloud `firmware_rollback` with correct sha - switches; (c) redelivery of same command - SUCCEEDED no-op, no second switch; (d) wrong sha - TARGET_MISMATCH; (e) info shadow and /api/v1/ota/status both show the passive sha before and after
- [ ] 6.3 Confirm no `sha256_mismatch` job status is published after a rollback performed while `ota_pending` was set (set it via a staged OTA, then roll back)

## 7. Spec sync + PR

- [ ] 7.1 Update spec deltas if implementation diverged; `openspec validate firmware-rollback-command`
- [ ] 7.2 PR to development with `Closes #237`; code-review agents (adversarial brief on the command path) + simplify agent per repo policy
