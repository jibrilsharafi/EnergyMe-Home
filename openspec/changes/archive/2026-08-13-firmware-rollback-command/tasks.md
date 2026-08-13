# Tasks: firmware-rollback-command

## 1. Host-testable rollback decision logic

- [x] 1.1 Create `source/lib/rollback_logic/` (mirrors `version_compare` layout): sha256 hex validation (64 hex chars, case-insensitive compare) and the precondition decision function (expected, passiveSha, passiveReadable, runningSha) -> {PROCEED, NOOP_ALREADY_DONE, MISMATCH, MISSING_SHA, NO_TARGET}
- [x] 1.2 Unity tests in `source/test/test_rollback_logic/`: valid/invalid hex, case-insensitivity, all five outcomes, redelivery-after-success case; run `pio test -e native` from WSL (19/19 passed)

## 2. Shared rollback core

- [x] 2.1 Add `CrashMonitor::markRollbackTried()` setter (crashmonitor.h/.cpp), alongside the existing `clearRollbackTried()`
- [x] 2.2 Expose pending-OTA state clearing for the rollback path (public wrapper around `_clearOtaPendingState()` in mqtt.cpp, or equivalent)
- [x] 2.3 Implement shared `attemptFirmwareRollback()` (utils.cpp or the rollback module): read passive descriptor via `esp_ota_get_partition_description`, switch via `esp_ota_set_boot_partition` gating on its return, mark rollback-tried + clear pending-OTA AFTER restart is confirmed scheduled (setRestartSystem holds refused restarts, so the refusal path restores the boot partition instead - state marks never need undoing)
- [x] 2.4 Helper to read the passive slot's sha256 as 64-hex (`getOtherPartitionSha256`/`getRunningPartitionSha256` in utils)

## 3. Cloud command

- [x] 3.1 Add `firmware_rollback` branch to `_handleCommandExecution` in mqtt.cpp: parse `expected_sha256` (string only), map decision outcomes to `MISSING_SHA256` / `NO_ROLLBACK_TARGET` / `TARGET_MISMATCH` (REJECTED), `ROLLBACK_FAILED` (FAILED), plain SUCCEEDED for both PROCEED and NOOP_ALREADY_DONE; publish SUCCEEDED before restart with the same 2 s flush as `restart`
- [x] 3.2 Verified via the dev inject harness on bench device 588c81c479f8: MISSING_SHA256 (absent + malformed), TARGET_MISMATCH, ROLLBACK_FAILED vs a genuinely partial image, SUCCEEDED no-op redelivery - all statuses confirmed published in the DEBUG log; AWS echo-rejects for synthetic execution ids correctly ignored (no publish loop)

## 4. Local endpoint fix + reporting

- [x] 4.1 Rework `_serveOtaRollbackEndpoint` (customserver.cpp): call `attemptFirmwareRollback()` first, respond from its result (success only on switch+restart initiated; 400/423 with `success:false` otherwise); drop `Update.canRollBack()`/`Update.rollBack()`
- [x] 4.2 Add `otherPartitionSha256` (or `null`) to `GET /api/v1/ota/status`
- [x] 4.3 update.html: fetch target sha before the confirm dialog (truncated display), alert when no target exists (api-client.js needed no change)
- [x] 4.4 swagger.yaml: update `/api/v1/ota/rollback` (400/423 responses) and `/api/v1/ota/status` (new nullable field) - CRLF preserved (17+/2- diff)

## 5. Shadow + comment fixes

- [x] 5.1 Add `other_partition_sha256` (or `null`) to the info shadow (shadow.cpp)
- [x] 5.2 Correct the two false bootloader-rollback comments (`mqtt.cpp:1493`, `mqtt.cpp:2823`) to state that `initArduino()` marks the app valid before `setup()` and the validation task is telemetry-only

## 6. Verification

- [x] 6.1 `pio run -e esp32s3-dev` clean build; `pio check -e esp32s3-dev` no new findings (18 pre-existing HIGHs are cppcheck failing on vendored ArduinoJson macros); full native suite 510/510
- [x] 6.2 Hardware e2e on bench device 588c81c479f8 (2026-08-13): two distinct builds OTA'd into the slots (A=4994..., B=b793...); (a) local rollback via REST - 200, landed on the other build, response intact; (b) cloud `firmware_rollback` with correct sha - switched app1->app0, SUCCEEDED published pre-reboot; (c) redelivery - SUCCEEDED no-op, no second switch; (d) wrong sha - REJECTED; (e) `otherPartitionSha256` byte-identical to the sha extracted from the release .bin at offset 0xB0, null + canRollback=false mid-upload; (f) MQTT task minimumFreeStack floor 820 B unchanged by the command path, zero crashes across all reboots. Bonus from a naturally failed upload: partial image in the passive slot -> local 400 / cloud ROLLBACK_FAILED (ESP_ERR_OTA_VALIDATE_FAILED), and the 423 early-gate fired within the post-boot uptime window with no ghost restart afterwards. UI click-through not automated (confirm() blocks browser automation); REST path covers the endpoint
- [x] 6.3 Verified: seeded the pending-OTA record via the dev NVS endpoint (expected sha = running image), rolled back, and after the reboot the validation task published `FAILED` for the job to the real AWS jobs topic; record cleared afterwards

## 7. Spec sync + PR

- [x] 7.1 Update spec deltas if implementation diverged (pending-OTA record is now preserved, not cleared - premise corrected by the adversarial review); `openspec validate --strict` passes
- [x] 7.2 PR #240 to development with `Closes #237`; adversarial code review (9 findings triaged: 2 fixed races, 1 spec correction, 2 hardening, 4 dismissed with evidence) + 4-angle simplify round applied
